#pragma once
// ============================================================
// training_processing_worker.h — Background Training Data Pipeline
//
// Asynchronously processes pending voice_journal entries:
//   1. Reads unprocessed entries from SQLite
//   2. Matches each transcript with the closest AI response
//      timestamped in chat_history (within a configurable window)
//   3. Compiles valid (user, assistant) pairs into training_logs
//   4. Appends pairs to the .jsonl export dataset
//   5. Cleans up raw audio files once fully processed
//
// Thread-safe: all DB writes are serialized under a mutex.
// Managed by PassiveListener or Jarvis core.
// ============================================================

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QMutex>
#include <QString>
#include <atomic>

// ── Worker — runs in a dedicated QThread ────────────────────

class TrainingProcessingWorker : public QObject
{
    Q_OBJECT
public:
    explicit TrainingProcessingWorker(QObject* parent = nullptr);

    void setUserId(qint64 id)            { m_userId = id; }
    void setDatasetPath(const QString& p) { m_datasetPath = p; }

    // Max time window (seconds) to match a journal entry
    // with a subsequent AI response in chat_history.
    void setMatchWindowSec(int sec)      { m_matchWindowSec = sec; }

    void requestStop()                   { m_stopRequested.store(true); }

public slots:
    void processPendingEntries();

signals:
    void progress(int processed, int totalPending);
    void cycleFinished(int pairsCreated, int entriesCleaned);
    void error(const QString& message);

private:
    struct MatchedPair {
        qint64  journalId;
        QString userTranscript;
        QString aiResponse;
        QString model;
        QString sessionId;
        QString audioFile;
    };

    QVector<MatchedPair> matchWithResponses(
        const QList<QMap<QString,QVariant>>& entries);

    bool appendToJsonl(const MatchedPair& pair);
    void cleanupAudioFile(const QString& path);

    qint64            m_userId          = 1;
    QString           m_datasetPath;
    int               m_matchWindowSec  = 120;
    QMutex            m_dbMutex;
    std::atomic<bool> m_stopRequested{false};
};

// ── Controller — lives in the UI/main thread ────────────────

class TrainingPipelineController : public QObject
{
    Q_OBJECT
public:
    explicit TrainingPipelineController(QObject* parent = nullptr);
    ~TrainingPipelineController() override;

    void setUserId(qint64 id);
    void setDatasetPath(const QString& path);

    void start(int intervalMinutes = 10);
    void stop();
    bool isRunning() const { return m_running; }

    void runOnce();

signals:
    void pipelineProgress(int processed, int totalPending);
    void pipelineFinished(int pairsCreated, int entriesCleaned);
    void pipelineError(const QString& message);

    // internal — cross-thread dispatch
    void requestProcess();

private slots:
    void onTimerFired();
    void onCycleFinished(int pairs, int cleaned);

private:
    QThread*                   m_thread = nullptr;
    TrainingProcessingWorker*  m_worker = nullptr;
    QTimer*                    m_timer  = nullptr;
    bool                       m_running = false;
};
