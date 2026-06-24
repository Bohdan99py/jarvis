// ============================================================
// background_learner.cpp — J.A.R.V.I.S. фоновое обучение
// ============================================================
#include "background_learner.h"
#include "database_manager.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDebug>
#include <QDateTime>

// ============================================================
//  Конфигурация
// ============================================================

const QStringList LearnerWorker::k_codeExtensions = {
    ".cpp", ".h", ".hpp", ".cxx", ".cc",
    ".py",
    ".cs",
    ".glsl", ".hlsl",
    ".json", ".ini", ".cfg"
};

// ============================================================
//  LearnerWorker
// ============================================================

LearnerWorker::LearnerWorker(QObject* parent) : QObject(parent) {}

void LearnerWorker::setWatchPaths(const QStringList& paths)
{
    m_watchPaths = paths;
}

void LearnerWorker::runFullCycle()
{
    qDebug() << "[Learner] Starting full cycle";
    m_stopRequested.store(0);

    int filesIndexed  = indexProjectFiles();
    int patternsFound = analyzeChat();

    // Автоматически чистим мусор из training_logs
    int cleaned = DatabaseManager::instance().cleanupTrainingLogs(1);
    if (cleaned > 0)
        qDebug() << "[Learner] Auto-cleanup removed" << cleaned << "noise entries from training_logs";

    qDebug() << "[Learner] Cycle done. Files:" << filesIndexed
             << "Patterns:" << patternsFound;
    emit cycleFinished(filesIndexed, patternsFound);
}

void LearnerWorker::runIndexOnly()
{
    m_stopRequested.store(0);
    int n = indexProjectFiles();
    emit cycleFinished(n, 0);
}

void LearnerWorker::runPatternAnalysisOnly()
{
    m_stopRequested.store(0);
    int n = analyzeChat();
    emit cycleFinished(0, n);
}

// ============================================================
//  Фаза 1 — Индексация файлов проекта
// ============================================================

int LearnerWorker::indexProjectFiles()
{
    if (m_watchPaths.isEmpty()) {
        qWarning() << "[Learner] No watch paths set";
        return 0;
    }

    QStringList allFiles;
    for (const QString& root : m_watchPaths) {
        QDirIterator it(root,
                        QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString path = it.next();
            QFileInfo fi(path);
            if (k_codeExtensions.contains("." + fi.suffix().toLower()))
                allFiles.append(path);
            if (m_stopRequested.load() != 0) return 0;  // std::atomic::load — OK
        }
    }

    QList<QString> stale = DatabaseManager::instance().getStaleFiles(allFiles);
    int total   = stale.size();
    int indexed = 0;

    qDebug() << "[Learner] Files to index:" << total << "of" << allFiles.size();

    for (const QString& path : stale) {
        if (m_stopRequested.load() != 0) break;
        emit progress(indexed, total, path);
        indexSingleFile(path);
        ++indexed;
    }
    return indexed;
}

void LearnerWorker::indexSingleFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit error(QStringLiteral("Cannot read: %1").arg(path));
        return;
    }

    QTextStream stream(&f);
    stream.setEncoding(QStringConverter::Utf8);
    QString content = stream.readAll();
    f.close();

    if (content.isEmpty()) return;

    QFileInfo fi(path);

    DbIndexedFile rec;               // Db* тип из database_manager.h
    rec.filePath       = path;
    rec.fileName       = fi.fileName();
    rec.extension      = "." + fi.suffix().toLower();
    rec.contentHash    = computeHash(content);
    rec.sizeBytes      = fi.size();
    rec.fileModifiedAt = fi.lastModified();

    if (rec.extension == ".cpp" || rec.extension == ".h" ||
        rec.extension == ".hpp" || rec.extension == ".cxx")
    {
        rec.symbols = extractSymbolsFromCpp(content);
    } else if (rec.extension == ".py") {
        rec.symbols = extractSymbolsFromPython(content);
    } else {
        rec.symbols = "[]";
    }

    QJsonDocument symDoc = QJsonDocument::fromJson(rec.symbols.toUtf8());
    int symCount = symDoc.isArray() ? symDoc.array().size() : 0;
    rec.summary = QStringLiteral("%1 | %2 symbols | %3 bytes")
        .arg(rec.fileName).arg(symCount).arg(rec.sizeBytes);

    DatabaseManager::instance().upsertFile(rec);
    qDebug() << "[Learner] Indexed:" << rec.fileName << "(" << symCount << "symbols)";
}

QString LearnerWorker::extractSymbolsFromCpp(const QString& content)
{
    QJsonArray symbols;

    // Классы и структуры
    {
        static const QRegularExpression re(
            R"(\b(?:class|struct)\s+(\w+)\s*(?:final\s*)?(?::\s*[^{]+)?\{)",
            QRegularExpression::MultilineOption);
        auto it = re.globalMatch(content);
        while (it.hasNext()) {
            auto m = it.next();
            QJsonObject obj;
            obj["type"] = "class";
            obj["name"] = m.captured(1);
            obj["line"] = content.left(m.capturedStart()).count('\n') + 1;
            symbols.append(obj);
        }
    }

    // Функции / методы
    {
        static const QRegularExpression re(
            R"(^\s*(?:(?:static|virtual|inline|explicit|override|const)\s+)*)"
            R"([\w:<>*&]+\s+(\w+)\s*\([^)]*\)\s*(?:const\s*)?(?:override\s*)?(?:noexcept\s*)?[\{;])",
            QRegularExpression::MultilineOption);
        auto it = re.globalMatch(content);
        while (it.hasNext()) {
            auto m = it.next();
            QString name = m.captured(1);
            static const QSet<QString> skip = {
                "if","for","while","switch","return","else","do","case","try","catch"
            };
            if (skip.contains(name)) continue;
            QJsonObject obj;
            obj["type"] = "function";
            obj["name"] = name;
            obj["line"] = content.left(m.capturedStart()).count('\n') + 1;
            symbols.append(obj);
        }
    }

    // #include зависимости
    {
        static const QRegularExpression re(R"(#include\s*[<"]([^>"]+)[>"])");
        QJsonArray includes;
        auto it = re.globalMatch(content);
        while (it.hasNext()) includes.append(it.next().captured(1));
        if (!includes.isEmpty()) {
            QJsonObject obj;
            obj["type"]     = "includes";
            obj["name"]     = "dependencies";
            obj["includes"] = includes;
            symbols.append(obj);
        }
    }

    return QJsonDocument(symbols).toJson(QJsonDocument::Compact);
}

QString LearnerWorker::extractSymbolsFromPython(const QString& content)
{
    QJsonArray symbols;

    {
        static const QRegularExpression re(R"(^class\s+(\w+))",
                                           QRegularExpression::MultilineOption);
        auto it = re.globalMatch(content);
        while (it.hasNext()) {
            auto m = it.next();
            QJsonObject obj;
            obj["type"] = "class";
            obj["name"] = m.captured(1);
            obj["line"] = content.left(m.capturedStart()).count('\n') + 1;
            symbols.append(obj);
        }
    }
    {
        static const QRegularExpression re(R"(^(?:    )?def\s+(\w+)\s*\()",
                                           QRegularExpression::MultilineOption);
        auto it = re.globalMatch(content);
        while (it.hasNext()) {
            auto m = it.next();
            QJsonObject obj;
            obj["type"] = "function";
            obj["name"] = m.captured(1);
            obj["line"] = content.left(m.capturedStart()).count('\n') + 1;
            symbols.append(obj);
        }
    }

    return QJsonDocument(symbols).toJson(QJsonDocument::Compact);
}

QString LearnerWorker::computeHash(const QString& content)
{
    return QCryptographicHash::hash(
        content.toUtf8(), QCryptographicHash::Md5).toHex();
}

// ============================================================
//  Фаза 2 — Анализ истории чатов → паттерны поведения
// ============================================================

int LearnerWorker::analyzeChat()
{
    auto sessions = DatabaseManager::instance().getSessions(1, 500);
    if (sessions.isEmpty()) return 0;

    int processed = 0;
    for (const auto& sessionMeta : sessions) {
        if (m_stopRequested.load() != 0) break;
        extractPatternsFromSession(sessionMeta["session_id"]);
        ++processed;
        if (processed % 10 == 0)
            emit progress(processed, sessions.size(),
                          QStringLiteral("Analysing sessions %1/%2")
                          .arg(processed).arg(sessions.size()));
    }

    return DatabaseManager::instance().getTopPatterns(1, 1000).size();
}

void LearnerWorker::extractPatternsFromSession(const QString& sessionId)
{
    auto messages = DatabaseManager::instance().getSession(sessionId);
    if (messages.size() < 2) return;

    int total = messages.size();

    for (int i = 0; i < messages.size() - 1; ++i) {
        const DbChatMessage& userMsg = messages[i];
        const DbChatMessage& aiMsg   = messages[i + 1];

        if (userMsg.role != "user" || aiMsg.role != "assistant") continue;

        QString trigger  = userMsg.content.simplified();
        QString response = aiMsg.content.simplified();

        if (trigger.length() < 3   || trigger.length() > 500)   continue;
        if (response.length() < 5  || response.length() > 2000) continue;

        QJsonObject ctx;
        ctx["model"]       = aiMsg.model;
        ctx["session_id"]  = sessionId;
        ctx["hour"]        = userMsg.createdAt.time().hour();
        ctx["day_of_week"] = static_cast<int>(userMsg.createdAt.date().dayOfWeek());

        DbBehaviorPattern pattern;
        pattern.userId    = 1;
        pattern.trigger   = trigger.toLower().simplified();
        pattern.response  = response.left(500);
        pattern.context   = QJsonDocument(ctx).toJson(QJsonDocument::Compact);
        pattern.confidence= computeConfidence(1, total);

        DatabaseManager::instance().upsertPattern(pattern);
    }
}

float LearnerWorker::computeConfidence(int frequency, int totalMessages)
{
    if (totalMessages <= 0) return 0.0f;
    float raw = static_cast<float>(frequency) / static_cast<float>(totalMessages);
    return qMin(raw * 10.0f, 1.0f);
}

// ============================================================
//  BackgroundLearner — контроллер (главный поток)
// ============================================================

BackgroundLearner::BackgroundLearner(QObject* parent) : QObject(parent)
{
    m_thread = new QThread(this);
    m_worker = new LearnerWorker();
    m_worker->moveToThread(m_thread);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);

    connect(m_worker, &LearnerWorker::progress,
            this,     &BackgroundLearner::learningProgress);
    connect(m_worker, &LearnerWorker::error,
            this,     &BackgroundLearner::learningError);
    connect(m_worker, &LearnerWorker::cycleFinished,
            this,     &BackgroundLearner::onCycleFinished);

    connect(this,     &BackgroundLearner::requestFullCycle,
            m_worker, &LearnerWorker::runFullCycle,
            Qt::QueuedConnection);
    connect(this,     &BackgroundLearner::requestIndexOnly,
            m_worker, &LearnerWorker::runIndexOnly,
            Qt::QueuedConnection);
    connect(this,     &BackgroundLearner::requestPatternAnalysis,
            m_worker, &LearnerWorker::runPatternAnalysisOnly,
            Qt::QueuedConnection);

    connect(m_timer, &QTimer::timeout,
            this,    &BackgroundLearner::onTimerFired);
}

BackgroundLearner::~BackgroundLearner()
{
    stop();
    delete m_worker;
    m_worker = nullptr;
}

void BackgroundLearner::setWatchPaths(const QStringList& paths)
{
    m_watchPaths = paths;
    if (m_worker) m_worker->setWatchPaths(paths);
}

void BackgroundLearner::start()
{
    if (m_running) return;
    m_worker->setWatchPaths(m_watchPaths);
    m_thread->start(QThread::LowPriority);
    m_running = true;

    // Первый прогон через 3 секунды после запуска UI
    QTimer::singleShot(3000, this, [this]() { emit requestFullCycle(); });

    scheduleNext(30);
    qDebug() << "[Learner] Started. Watch paths:" << m_watchPaths;
}

void BackgroundLearner::stop()
{
    if (!m_running) return;
    m_timer->stop();
    m_thread->quit();
    if (!m_thread->wait(5000)) {
        m_thread->terminate();
        m_thread->wait(1000);
    }
    m_running = false;
    qDebug() << "[Learner] Stopped";
}

bool BackgroundLearner::isRunning() const
{
    return m_running && m_thread->isRunning();
}

void BackgroundLearner::scheduleNext(int intervalMinutes)
{
    m_timer->stop();
    m_timer->start(intervalMinutes * 60 * 1000);
    qDebug() << "[Learner] Next cycle in" << intervalMinutes << "min";
}

void BackgroundLearner::onTimerFired()
{
    qDebug() << "[Learner] Timer fired — starting cycle";
    emit requestFullCycle();
}

void BackgroundLearner::onCycleFinished(int files, int patterns)
{
    qDebug() << "[Learner] Cycle finished. Files:" << files << "Patterns:" << patterns;
    emit learningFinished(files, patterns);
}
