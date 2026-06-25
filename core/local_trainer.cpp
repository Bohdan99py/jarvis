// ============================================================
// local_trainer.cpp — J.A.R.V.I.S. Local Model Training
// ============================================================

#include "local_trainer.h"
#include "database_manager.h"

#include <QProcess>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDebug>

// ============================================================
//  Конструктор / Деструктор
// ============================================================

LocalTrainer::LocalTrainer(QObject* parent)
    : QObject(parent)
{
    m_systemPrompt = QStringLiteral(
        "You are JARVIS (Just A Rather Very Intelligent System), "
        "a personal AI assistant running locally on the user's Windows PC. "
        "You help with programming (C++, Unreal Engine, Qt), "
        "system control, file management, and general questions. "
        "You respond concisely and adapt to the user's communication style. "
        "If the user speaks Russian, respond in Russian. "
        "If English, respond in English. "
        "You learn from the user's preferences shown in the examples below."
    );
}

LocalTrainer::~LocalTrainer()
{
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
    // Удаляем временный Modelfile
    if (!m_modelfilePath.isEmpty())
        QFile::remove(m_modelfilePath);
}

// ============================================================
//  Настройки
// ============================================================

void LocalTrainer::setBaseModel(const QString& model)   { m_baseModel = model; }
void LocalTrainer::setOutputModel(const QString& name)  { m_outputModel = name; }
void LocalTrainer::setMaxPairs(int n)                   { m_maxPairs = qBound(10, n, 500); }
void LocalTrainer::setSystemPrompt(const QString& prompt) { m_systemPrompt = prompt; }

// ============================================================
//  Проверка Ollama
// ============================================================

bool LocalTrainer::isOllamaAvailable() const
{
    QProcess proc;
    proc.start(QStringLiteral("ollama"), { QStringLiteral("--version") });
    if (!proc.waitForStarted(3000)) return false;
    proc.waitForFinished(5000);
    return proc.exitCode() == 0;
}

QStringList LocalTrainer::installedModels() const
{
    QProcess proc;
    proc.start(QStringLiteral("ollama"), { QStringLiteral("list") });
    if (!proc.waitForStarted(3000)) return {};
    proc.waitForFinished(10000);

    QStringList models;
    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    for (int i = 1; i < lines.size(); ++i) { // skip header
        const QString name = lines[i].section(QLatin1Char('\t'), 0, 0)
                                     .section(QLatin1Char(' '), 0, 0)
                                     .trimmed();
        if (!name.isEmpty()) models.append(name);
    }
    return models;
}

// ============================================================
//  Сбор данных из training_logs
// ============================================================

QVector<TrainingPair> LocalTrainer::collectTrainingData(DatabaseManager* db) const
{
    QVector<TrainingPair> pairs;
    if (!db) return pairs;

    // Получаем все логи через публичный API, фильтруем по rating > 0
    QList<DbTrainingLog> allLogs = db->getTrainingLogs(1, m_maxPairs * 3);

    for (const DbTrainingLog& log : allLogs) {
        if (log.rating <= 0) continue; // только лайкнутые

        TrainingPair tp;
        tp.userMessage = log.userMessage.trimmed();
        tp.aiResponse  = log.aiResponse.trimmed();
        tp.model       = log.model;
        tp.rating      = log.rating;

        // Пропускаем пустые
        if (tp.userMessage.isEmpty() || tp.aiResponse.isEmpty()) continue;

        // Обрезаем слишком длинные (> 2000 символов) — не влезут в контекст
        if (tp.userMessage.size() > 2000) tp.userMessage = tp.userMessage.left(2000);
        if (tp.aiResponse.size() > 2000)  tp.aiResponse  = tp.aiResponse.left(2000);

        pairs.append(tp);
        if (pairs.size() >= m_maxPairs) break;
    }

    qDebug() << "[Trainer] Collected" << pairs.size() << "training pairs (liked)";
    return pairs;
}

// ============================================================
//  Генерация Modelfile
// ============================================================

QString LocalTrainer::escapeForModelfile(const QString& text) const
{
    QString s = text;
    s.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    s.replace(QLatin1Char('"'),  QStringLiteral("\\\""));
    s.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    s.replace(QLatin1Char('\r'), QString());
    return s;
}

QString LocalTrainer::buildModelfile(const QVector<TrainingPair>& pairs) const
{
    QString mf;
    mf.reserve(pairs.size() * 500 + 2000);

    // ── Базовая модель ────────────────────────────────────
    mf += QStringLiteral("FROM %1\n\n").arg(m_baseModel);

    // ── Параметры ─────────────────────────────────────────
    mf += QStringLiteral("PARAMETER temperature 0.7\n");
    mf += QStringLiteral("PARAMETER num_ctx 4096\n");
    mf += QStringLiteral("PARAMETER top_p 0.9\n");
    mf += QStringLiteral("PARAMETER repeat_penalty 1.1\n\n");

    // ── Системный промпт ──────────────────────────────────
    mf += QStringLiteral("SYSTEM \"\"\"\n");
    mf += m_systemPrompt;
    mf += QStringLiteral("\n\"\"\"\n\n");

    // ── Примеры (few-shot) ────────────────────────────────
    // Каждая пара = MESSAGE user + MESSAGE assistant
    for (const auto& tp : pairs) {
        mf += QStringLiteral("MESSAGE user \"%1\"\n")
                  .arg(escapeForModelfile(tp.userMessage));
        mf += QStringLiteral("MESSAGE assistant \"%1\"\n\n")
                  .arg(escapeForModelfile(tp.aiResponse));
    }

    return mf;
}

// ============================================================
//  Запуск обучения
// ============================================================

void LocalTrainer::train(DatabaseManager* db)
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        emit trainingFinished(false,
            QStringLiteral("Training already in progress"));
        return;
    }

    emit trainingStarted();
    emit trainingProgress(QStringLiteral("🔍 Checking Ollama..."));

    // 1. Проверяем Ollama
    if (!isOllamaAvailable()) {
        emit trainingFinished(false,
            QStringLiteral("Ollama not found. Install from: https://ollama.com"));
        return;
    }
    emit trainingProgress(QStringLiteral("✅ Ollama is running"));

    // 2. Проверяем базовую модель
    QStringList models = installedModels();
    if (!models.contains(m_baseModel)) {
        emit trainingProgress(
            QStringLiteral("⬇ Base model '%1' not installed. Pulling...").arg(m_baseModel));

        QProcess pull;
        pull.start(QStringLiteral("ollama"), { QStringLiteral("pull"), m_baseModel });
        if (!pull.waitForStarted(5000)) {
            emit trainingFinished(false,
                QStringLiteral("Failed to download model %1").arg(m_baseModel));
            return;
        }
        pull.waitForFinished(600000); // 10 минут на скачивание
        if (pull.exitCode() != 0) {
            emit trainingFinished(false,
                QStringLiteral("Error downloading %1:\n%2")
                    .arg(m_baseModel, QString::fromUtf8(pull.readAllStandardError())));
            return;
        }
        emit trainingProgress(QStringLiteral("✅ Base model '%1' ready").arg(m_baseModel));
    } else {
        emit trainingProgress(QStringLiteral("✅ Base model '%1' available").arg(m_baseModel));
    }

    // 3. Собираем данные
    emit trainingProgress(QStringLiteral("📊 Collecting liked responses..."));
    QVector<TrainingPair> pairs = collectTrainingData(db);

    if (pairs.isEmpty()) {
        emit trainingFinished(false,
            QStringLiteral("No liked responses for training!\n"
                           "Click 👍 on responses you like to build a training set."));
        return;
    }
    emit trainingProgress(
        QStringLiteral("📊 Found %1 training pairs").arg(pairs.size()));

    // 4. Генерируем Modelfile
    emit trainingProgress(QStringLiteral("📝 Building Modelfile..."));
    QString modelfileText = buildModelfile(pairs);

    // Сохраняем во временный файл
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_modelfilePath = tempDir + QStringLiteral("/jarvis_modelfile.txt");

    QFile mfFile(m_modelfilePath);
    if (!mfFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit trainingFinished(false,
            QStringLiteral("Cannot write Modelfile to: %1").arg(m_modelfilePath));
        return;
    }
    mfFile.write(modelfileText.toUtf8());
    mfFile.close();

    emit trainingProgress(
        QStringLiteral("📝 Modelfile saved (%1 pairs, %2 KB)")
            .arg(pairs.size())
            .arg(modelfileText.size() / 1024));

    // 5. Запускаем ollama create
    emit trainingProgress(
        QStringLiteral("🚀 Creating model '%1'...").arg(m_outputModel));

    m_process = new QProcess(this);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &LocalTrainer::onProcessFinished);

    m_process->start(QStringLiteral("ollama"),
        { QStringLiteral("create"), m_outputModel,
          QStringLiteral("-f"), m_modelfilePath });

    if (!m_process->waitForStarted(10000)) {
        emit trainingFinished(false,
            QStringLiteral("Failed to start: ollama create"));
        m_process->deleteLater();
        m_process = nullptr;
    }
}

void LocalTrainer::onProcessFinished(int exitCode, QProcess::ExitStatus /*status*/)
{
    QString output = QString::fromUtf8(m_process->readAllStandardOutput());
    QString errors = QString::fromUtf8(m_process->readAllStandardError());

    // Удаляем временный Modelfile
    if (!m_modelfilePath.isEmpty()) {
        QFile::remove(m_modelfilePath);
        m_modelfilePath.clear();
    }

    m_process->deleteLater();
    m_process = nullptr;

    if (exitCode == 0) {
        emit trainingProgress(QStringLiteral("✅ Model '%1' created successfully!").arg(m_outputModel));
        emit trainingFinished(true,
            QStringLiteral("Model '%1' created!\n\n"
                           "Go to Settings → select '%1' as Ollama model.\n"
                           "Or say: \"Jarvis, switch to %1\"")
                .arg(m_outputModel));
    } else {
        emit trainingFinished(false,
            QStringLiteral("ollama create failed (exit %1):\n%2\n%3")
                .arg(exitCode).arg(output, errors));
    }
}

// ============================================================
//  Удаление модели
// ============================================================

void LocalTrainer::removeModel()
{
    QProcess proc;
    proc.start(QStringLiteral("ollama"),
        { QStringLiteral("rm"), m_outputModel });

    if (!proc.waitForStarted(5000)) {
        emit modelRemoved(false);
        return;
    }
    proc.waitForFinished(30000);
    emit modelRemoved(proc.exitCode() == 0);
}
