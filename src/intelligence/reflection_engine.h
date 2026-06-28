#pragma once
// ============================================================
// reflection_engine.h — Autonomous Self-Reflection Loop
//
// Background thread that activates when the user is inactive
// (PAUSED or offline). Analyzes the last 24h of dialogue for:
//   1. Cognitive shifts (opinion/value changes)
//   2. Emotional state trends
//   3. Productivity patterns
//   4. Behavioral topology (goal-orientedness, context-switching)
//
// Produces a UserSummary and writes an evolving JSON profile.
// The morning summary is queued as the first nudge of the day.
//
// Thread safety: QtConcurrent + QMutex. Never blocks the main
// Telegram event loop.
// ============================================================

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QReadWriteLock>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QPair>
#include <QList>

class MemoryManager;
class SocialPresenceEngine;
class PersonalityEngine;

// ── Emotion signal entry (top-level to avoid nested-struct moc issues) ──

struct EmotionSignal
{
    QString keyword;
    QString emotion;
    double  weight = 0.0;
};

// ── Analysis Results ────────────────────────────────────────

struct EmotionTrend
{
    QString     dominant    = QStringLiteral("neutral");
    double      confidence  = 0.0;
    int         sampleCount = 0;
    QStringList evidenceWords;  // "signals" is a Qt macro — never use it as an identifier
};

struct ProductivityPattern
{
    int     totalMessages    = 0;
    int     questionCount    = 0;
    int     commandCount     = 0;
    int     topicSwitches    = 0;
    double  focusScore       = 0.0;
    QString dominantTopic;
    QStringList topicsDiscussed;
};

struct CognitiveShift
{
    QString   topic;
    QString   previousStance;
    QString   currentStance;
    double    shiftMagnitude = 0.0;
    QDateTime detectedAt;
};

// ── Behavioral Metrics — dialogue topology analysis ─────────

struct BehavioralMetrics
{
    // Goal-orientedness: 0 = purely exploratory, 1 = laser-focused on task
    double goalOrientedness    = 0.5;

    // Context-switching frequency: avg switches per 10 messages
    double contextSwitchRate   = 0.0;

    // Whether the user is becoming more focused or more scattered
    // over time. Positive = improving focus, negative = increasing scatter.
    double focusEvolutionDelta = 0.0;

    // Dominant interaction style for this window
    QString interactionStyle;  // "task-driven", "exploratory", "mixed"

    // Per-topic dwell times (topic → message count in that topic)
    QList<QPair<QString, int>> topicDwellTimes;

    QJsonObject toJson() const;
};

// ── UserSummary ─────────────────────────────────────────────

struct UserSummary
{
    QDateTime               generatedAt;
    EmotionTrend            emotion;
    ProductivityPattern     productivity;
    QList<CognitiveShift>   cognitiveShifts;
    BehavioralMetrics       behavior;
    QString                 morningNudge;

    QJsonObject toJson() const;
};

// ── Reflection Engine ───────────────────────────────────────

class ReflectionEngine : public QObject
{
    Q_OBJECT

public:
    explicit ReflectionEngine(QObject* parent = nullptr);
    ~ReflectionEngine() override;

    void setMemoryManager(MemoryManager* mm);
    void setSocialPresence(SocialPresenceEngine* sp);
    void setPersonalityEngine(PersonalityEngine* pe);

    void start(int checkIntervalMinutes = 30);
    void stop();
    bool isRunning() const;

    void triggerReflection();

    UserSummary lastSummary() const;

    // Behavioral metrics — thread-safe read for SocialPresenceEngine
    BehavioralMetrics currentBehavior() const;

    static QString profileJsonPath();

signals:
    void reflectionComplete(const UserSummary& summary);
    void morningNudgeReady(const QString& nudgeText);
    void profileUpdated(const QString& path);
    void behaviorUpdated(const BehavioralMetrics& metrics);

private slots:
    void onCheckTimer();

private:
    // Core reflection pipeline
    void runReflectionCycle();

    // Analysis phases
    QList<QPair<QString, QString>> fetchLast24hDialogue() const;
    EmotionTrend        analyzeEmotions(const QList<QPair<QString, QString>>& dialogue) const;
    ProductivityPattern analyzeProductivity(const QList<QPair<QString, QString>>& dialogue) const;
    QList<CognitiveShift> detectCognitiveShifts(const QList<QPair<QString, QString>>& dialogue) const;
    BehavioralMetrics   analyzeBehavior(const QList<QPair<QString, QString>>& dialogue) const;

    // Behavioral evolution — compares current metrics against historical
    double calculateBehavioralEvolution(const BehavioralMetrics& current) const;

    // Output phases
    QString     composeMorningNudge(const UserSummary& summary) const;
    void        updateProfileJson(const UserSummary& summary);
    QJsonObject loadExistingProfile() const;
    void        saveProfile(const QJsonObject& profile) const;
    void        storeReflectionMemories(const UserSummary& summary);

    static const QList<EmotionSignal>& emotionLexicon();

    bool shouldReflect() const;

    MemoryManager*        m_memory      = nullptr;
    SocialPresenceEngine* m_presence    = nullptr;
    PersonalityEngine*    m_personality = nullptr;

    QTimer* m_checkTimer = nullptr;

    UserSummary        m_lastSummary;
    BehavioralMetrics  m_currentBehavior;
    mutable QReadWriteLock m_summaryLock;

    QDateTime m_lastReflectionTime;
    int       m_lastReflectionDay = -1;

    static constexpr int    MIN_MESSAGES_FOR_REFLECTION    = 5;
    static constexpr int    MIN_HOURS_BETWEEN_REFLECTIONS  = 4;
    static constexpr double COGNITIVE_SHIFT_THRESHOLD       = 0.3;
};
