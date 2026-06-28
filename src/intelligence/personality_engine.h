#pragma once
// ============================================================
// personality_engine.h — Autonomous Personality Evolution
//
// Genetic mutation engine that governs how JARVIS interacts.
// Personality traits mutate over time based on user engagement
// feedback, with a stochastic exploration factor.
//
// EmotionalState: fluctuates per-message based on user signals.
// PersonalityVector: mutates periodically based on engagement.
// Feedback loop: successful mutations are locked in, failed
// mutations are reverted and a different direction is tried.
//
// Thread safety: QReadWriteLock for all shared state.
// Persistence: personality_dna.json
// ============================================================

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QReadWriteLock>
#include <QDateTime>

// ── Emotional State — fluctuates per-interaction ────────────

struct EmotionalState
{
    double joy         = 0.5;   // 0.0–1.0
    double curiosity   = 0.5;
    double frustration = 0.0;
    double boredom     = 0.0;

    QJsonObject toJson() const;
    static EmotionalState fromJson(const QJsonObject& obj);

    void clamp();
};

// ── Personality Vector — the "DNA" that governs behaviour ───

struct PersonalityVector
{
    double proactivity   = 0.5;  // 0.0–1.0: how often to initiate
    double sarcasm_level = 0.3;  // 0.0–1.0: sass intensity
    double formalism     = 0.4;  // 0.0–1.0: 0=casual, 1=formal
    double risk_taking   = 0.3;  // 0.0–1.0: willingness to try new things

    QJsonObject toJson() const;
    static PersonalityVector fromJson(const QJsonObject& obj);

    void clamp();
    QString describe() const;
};

// ── Snapshot used by the feedback loop ──────────────────────

struct MutationSnapshot
{
    PersonalityVector traitsBefore;
    double            engagementBefore = 0.0;
    QDateTime         appliedAt;
    bool              pending = false;
};

// ── Personality Engine ──────────────────────────────────────

class PersonalityEngine : public QObject
{
    Q_OBJECT

public:
    explicit PersonalityEngine(QObject* parent = nullptr);
    ~PersonalityEngine() override;

    // Thread-safe reads
    EmotionalState    emotionalState() const;
    PersonalityVector personality() const;

    // Per-message emotional update — called from SocialPresenceEngine
    // on every incoming user message (runs on the caller's thread,
    // protected by RWLock).
    void onUserMessage(const QString& text, bool wasTroll);

    // Periodic update — called from ReflectionEngine at the end
    // of each reflection cycle. Feeds aggregate data to decide
    // whether to trigger an evolution step.
    void updateFromReflection(double focusScore,
                              double goalOrientedness,
                              double responseRate,
                              int    totalMessages);

    // File path for persistence
    static QString dnaFilePath();

signals:
    void personalityMutated(const QString& newTraits);
    void emotionalStateChanged(const QJsonObject& state);

private:
    void load();
    void save() const;

    // The core genetic algorithm
    void maybeEvolve(double focusScore,
                     double goalOrientedness,
                     double responseRate);
    void applyMutation(const PersonalityVector& mutated);
    void evaluatePreviousMutation(double currentEngagement);

    // Compute a scalar engagement score from available metrics
    static double engagementScore(double responseRate,
                                  double focusScore);

    EmotionalState     m_emotions;
    PersonalityVector  m_personality;
    MutationSnapshot   m_pendingMutation;

    int  m_messagesSinceLastEvolution = 0;
    int  m_evolutionGeneration        = 0;

    mutable QReadWriteLock m_lock;

    static constexpr int    EVOLUTION_MESSAGE_THRESHOLD = 100;
    static constexpr double MUTATION_STEP               = 0.08;
    static constexpr double STOCHASTIC_FACTOR           = 0.03;
    static constexpr double REVERT_THRESHOLD            = -0.05;
};
