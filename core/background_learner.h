#pragma once
// ============================================================
// background_learner.h — J.A.R.V.I.S. фоновое обучение
// ============================================================

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QStringList>
#include <atomic>   // std::atomic вместо QAtomicInt (у Qt нет store/load в Qt6)

// ── Воркер — выполняется в отдельном потоке ──────────────────

class LearnerWorker : public QObject
{
    Q_OBJECT
public:
    explicit LearnerWorker(QObject* parent = nullptr);

    void setWatchPaths(const QStringList& paths);

public slots:
    void runFullCycle();
    void runIndexOnly();
    void runPatternAnalysisOnly();

signals:
    void progress(int current, int total, const QString& currentFile);
    void cycleFinished(int filesIndexed, int patternsFound);
    void error(const QString& message);

private:
    int  indexProjectFiles();
    void indexSingleFile(const QString& path);
    QString extractSymbolsFromCpp(const QString& content);
    QString extractSymbolsFromPython(const QString& content);
    QString computeHash(const QString& content);

    int  analyzeChat();
    void extractPatternsFromSession(const QString& sessionId);
    float computeConfidence(int frequency, int totalMessages);

    QStringList         m_watchPaths;
    std::atomic<int>    m_stopRequested{0};   // std::atomic — есть store/load

    static const QStringList k_codeExtensions;
};

// ── Контроллер — живёт в главном потоке ──────────────────────

class BackgroundLearner : public QObject
{
    Q_OBJECT
public:
    explicit BackgroundLearner(QObject* parent = nullptr);
    ~BackgroundLearner() override;

    void setWatchPaths(const QStringList& paths);
    void start();
    void stop();
    bool isRunning() const;
    void scheduleNext(int intervalMinutes = 30);

signals:
    void learningProgress(int current, int total, const QString& file);
    void learningFinished(int filesIndexed, int patternsFound);
    void learningError(const QString& message);

    // внутренние — для передачи в поток через QueuedConnection
    void requestFullCycle();
    void requestIndexOnly();
    void requestPatternAnalysis();

private slots:
    void onTimerFired();
    void onCycleFinished(int files, int patterns);

private:
    QThread*       m_thread  = nullptr;
    LearnerWorker* m_worker  = nullptr;
    QTimer*        m_timer   = nullptr;
    QStringList    m_watchPaths;
    bool           m_running = false;
};
