#pragma once
// ============================================================
// case_distiller.h — Layer 2: distill repeated cases into heuristics
//
// Nightly (real elapsed-time, not app-uptime) pass over llm_cache: for
// each owner_id (0 = desktop, else a Telegram chat_id — see
// LlmCacheManager), clusters not-yet-distilled cases by keyword overlap
// and asks Claude once per qualifying cluster to synthesize a general
// principle. The result is stored per-owner in the `heuristics` table —
// "pay tokens once, know forever" for a whole family of similar
// questions. Learning stays individual per person: clustering and
// storage never cross owner_id boundaries.
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QTimer>

class ClaudeApi;

class CaseDistiller : public QObject
{
    Q_OBJECT

public:
    explicit CaseDistiller(ClaudeApi* claudeApi, QObject* parent = nullptr);

    void start();
    void stop();

    // Exposed for testing/manual triggering — bypasses the elapsed-time
    // gate and runs one distillation cycle immediately.
    void runCycleNow();

signals:
    void cycleFinished(int heuristicsCreated);

private:
    struct CaseRow {
        QString hash;
        QString query;
        QString response;
    };
    struct Cluster {
        qint64      ownerId = 0;
        QStringList hashes;
        QStringList queries;
        QStringList responses;
    };
    // A new case that may confirm or contradict an existing opinion —
    // the single best-overlapping not-yet-considered case found for that
    // opinion's topic since it was last updated.
    struct OpinionCheck {
        qint64  opinionId = 0;
        qint64  ownerId   = 0;
        QString topic;
        QString position;
        QString caseQuery;
        QString caseResponse;
    };

    void onTimerTick();
    void runCycle();
    QList<Cluster> buildClusters();
    QList<Cluster> clusterCases(qint64 ownerId, const QList<CaseRow>& rows);
    void processNextCluster();
    static QString buildPrompt(const Cluster& c);
    void storeHeuristic(const Cluster& c, const QString& claudeResponse);

    // Layer 3: contradiction checking, runs after the clustering pass above
    // finishes each cycle.
    void startOpinionCheckPhase();
    QList<OpinionCheck> buildOpinionChecks();
    void processNextOpinionCheck();
    static QString buildOpinionCheckPrompt(const OpinionCheck& check);
    void applyOpinionCheckResult(const OpinionCheck& check, const QString& claudeResponse);

    ClaudeApi*    m_claudeApi = nullptr;
    QTimer*       m_timer     = nullptr;
    QList<Cluster> m_pendingClusters;
    QList<OpinionCheck> m_pendingOpinionChecks;
    int            m_createdThisCycle = 0;

    static constexpr float kClusterOverlapThreshold = 0.5f;
    static constexpr int   kMinClusterSize           = 3;
    static constexpr qint64 kMinHoursBetweenRuns      = 20;
    // A contradicted opinion isn't silently overwritten below this
    // confidence, or after this many contradictions — instead the owner is
    // asked (see CuriosityEngine::postOpinionRevisionQuestion).
    static constexpr float kOpinionRevisionConfidenceFloor = 0.3f;
    static constexpr int   kOpinionRevisionContradictionThreshold = 2;
};
