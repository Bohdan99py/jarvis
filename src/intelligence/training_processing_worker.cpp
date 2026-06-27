// ============================================================
// training_processing_worker.cpp — Background Training Data Pipeline
// ============================================================

#include "training_processing_worker.h"
#include "database_manager.h"
#include "jarvis_paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>

// ============================================================
//  TrainingProcessingWorker
// ============================================================

TrainingProcessingWorker::TrainingProcessingWorker(QObject* parent)
    : QObject(parent)
{}

void TrainingProcessingWorker::processPendingEntries()
{
    m_stopRequested.store(false);

    auto& db = DatabaseManager::instance();

    auto entries = db.getUnprocessedJournalEntries(m_userId, 500);
    if (entries.isEmpty()) {
        emit cycleFinished(0, 0);
        return;
    }

    const int total = entries.size();
    emit progress(0, total);

    auto matched = matchWithResponses(entries);

    int pairsCreated  = 0;
    int entriesCleaned = 0;

    for (int i = 0; i < matched.size(); ++i) {
        if (m_stopRequested.load()) break;

        const MatchedPair& pair = matched[i];

        {
            QMutexLocker lock(&m_dbMutex);

            DbTrainingLog log;
            log.userId      = m_userId;
            log.userMessage = pair.userTranscript;
            log.aiResponse  = pair.aiResponse;
            log.model       = pair.model;
            log.sessionId   = pair.sessionId;
            log.rating      = 1;

            qint64 id = db.addTrainingLog(log);
            if (id > 0) {
                ++pairsCreated;
                appendToJsonl(pair);
            }

            db.markJournalEntryProcessed(pair.journalId);
        }

        cleanupAudioFile(pair.audioFile);
        if (!pair.audioFile.isEmpty())
            ++entriesCleaned;

        if ((i + 1) % 10 == 0)
            emit progress(i + 1, total);
    }

    // Mark remaining unmatched entries as processed to avoid re-scanning
    {
        QMutexLocker lock(&m_dbMutex);
        QSet<qint64> matchedIds;
        for (const auto& m : matched)
            matchedIds.insert(m.journalId);

        for (const auto& e : entries) {
            qint64 eid = e["id"].toLongLong();
            if (!matchedIds.contains(eid))
                db.markJournalEntryProcessed(eid);
        }
    }

    qDebug() << "[TrainingPipeline] Cycle done:"
             << pairsCreated << "pairs," << entriesCleaned << "audio files cleaned";

    emit progress(total, total);
    emit cycleFinished(pairsCreated, entriesCleaned);
}

QVector<TrainingProcessingWorker::MatchedPair>
TrainingProcessingWorker::matchWithResponses(
    const QList<QMap<QString,QVariant>>& entries)
{
    QVector<MatchedPair> result;
    auto& db = DatabaseManager::instance();

    auto recentMessages = db.getRecentMessages(m_userId, 2000);

    // Build a time-indexed list of assistant responses for fast lookup
    struct AssistantMsg {
        QDateTime time;
        QString   content;
        QString   model;
        QString   sessionId;
    };
    QVector<AssistantMsg> assistantMsgs;
    for (const auto& msg : recentMessages) {
        if (msg.role == QStringLiteral("assistant") && !msg.content.trimmed().isEmpty()) {
            assistantMsgs.append({msg.createdAt, msg.content, msg.model, msg.sessionId});
        }
    }

    for (const auto& entry : entries) {
        if (m_stopRequested.load()) break;

        const QString transcript = entry["transcript"].toString().simplified().trimmed();
        if (transcript.length() < 5) continue;

        const QDateTime entryTime = entry["created_at"].toDateTime();
        const qint64    entryId   = entry["id"].toLongLong();

        // Find the closest assistant response AFTER this journal entry
        // within the match window
        const AssistantMsg* bestMatch = nullptr;
        qint64 bestDelta = m_matchWindowSec + 1;

        for (const auto& am : assistantMsgs) {
            qint64 deltaSec = entryTime.secsTo(am.time);
            if (deltaSec >= 0 && deltaSec <= m_matchWindowSec && deltaSec < bestDelta) {
                // Minimum response quality: 20+ chars
                if (am.content.length() >= 20) {
                    bestMatch = &am;
                    bestDelta = deltaSec;
                }
            }
        }

        if (!bestMatch) continue;

        MatchedPair pair;
        pair.journalId      = entryId;
        pair.userTranscript = transcript;
        pair.aiResponse     = bestMatch->content;
        pair.model          = bestMatch->model;
        pair.sessionId      = bestMatch->sessionId;
        pair.audioFile      = entry.contains("audio_file")
                                  ? entry["audio_file"].toString()
                                  : QString();

        result.append(pair);
    }

    return result;
}

bool TrainingProcessingWorker::appendToJsonl(const MatchedPair& pair)
{
    if (m_datasetPath.isEmpty()) return false;

    QDir().mkpath(m_datasetPath);
    const QString filePath = QDir(m_datasetPath).filePath(
        QStringLiteral("training_pairs.jsonl"));

    QFile file(filePath);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        emit error(QStringLiteral("Cannot open %1: %2")
                       .arg(filePath, file.errorString()));
        return false;
    }

    QJsonObject obj;
    obj[QStringLiteral("instruction")] = pair.userTranscript;
    obj[QStringLiteral("input")]       = QString();
    obj[QStringLiteral("output")]      = pair.aiResponse;
    obj[QStringLiteral("model")]       = pair.model;

    file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    file.write("\n");
    file.close();
    return true;
}

void TrainingProcessingWorker::cleanupAudioFile(const QString& path)
{
    if (path.isEmpty()) return;

    QFileInfo fi(path);
    if (fi.exists() && fi.isFile()) {
        if (QFile::remove(path)) {
            qDebug() << "[TrainingPipeline] Cleaned up audio:" << fi.fileName();
        }
    }
}

// ============================================================
//  TrainingPipelineController
// ============================================================

TrainingPipelineController::TrainingPipelineController(QObject* parent)
    : QObject(parent)
{
    m_thread = new QThread(this);
    m_worker = new TrainingProcessingWorker();
    m_worker->moveToThread(m_thread);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);

    connect(m_worker, &TrainingProcessingWorker::progress,
            this,     &TrainingPipelineController::pipelineProgress);
    connect(m_worker, &TrainingProcessingWorker::cycleFinished,
            this,     &TrainingPipelineController::onCycleFinished);
    connect(m_worker, &TrainingProcessingWorker::error,
            this,     &TrainingPipelineController::pipelineError);

    connect(this,     &TrainingPipelineController::requestProcess,
            m_worker, &TrainingProcessingWorker::processPendingEntries,
            Qt::QueuedConnection);

    connect(m_timer, &QTimer::timeout,
            this,    &TrainingPipelineController::onTimerFired);

    m_thread->start(QThread::LowPriority);
}

TrainingPipelineController::~TrainingPipelineController()
{
    stop();

    m_worker->requestStop();
    m_worker->disconnect();
    m_thread->quit();
    if (!m_thread->wait(5000)) {
        m_thread->terminate();
        m_thread->wait(1000);
    }
    delete m_worker;
    m_worker = nullptr;
}

void TrainingPipelineController::setUserId(qint64 id)
{
    m_worker->setUserId(id);
}

void TrainingPipelineController::setDatasetPath(const QString& path)
{
    m_worker->setDatasetPath(path);
}

void TrainingPipelineController::start(int intervalMinutes)
{
    if (m_running) return;
    m_running = true;
    m_timer->start(intervalMinutes * 60 * 1000);

    // Run immediately on start
    QTimer::singleShot(2000, this, [this]() { emit requestProcess(); });

    qDebug() << "[TrainingPipeline] Started, interval:" << intervalMinutes << "min";
}

void TrainingPipelineController::stop()
{
    if (!m_running) return;
    m_timer->stop();
    m_worker->requestStop();
    m_running = false;
    qDebug() << "[TrainingPipeline] Stopped";
}

void TrainingPipelineController::runOnce()
{
    emit requestProcess();
}

void TrainingPipelineController::onTimerFired()
{
    emit requestProcess();
}

void TrainingPipelineController::onCycleFinished(int pairs, int cleaned)
{
    emit pipelineFinished(pairs, cleaned);
}
