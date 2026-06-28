#pragma once
// ============================================================
// reflection_engine.h — Autonomous Self-Reflection Loop
//
// Background thread that activates when the user is inactive
// (PAUSED or offline). Analyzes the last 24h of dialogue for:
//   1. Cognitive shifts (opinion/value changes)
//   2. Emotional state trends
//   3. Productivity patterns
//
// Produces a UserSummary and writes an evolving JSON profile.
// The morning summary is queued as the first nudge of the day.
//
// Thread safety: QThread + QMutex. Never blocks the main
// Telegram event loop.
// ============================================================

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>

class MemoryManager;
class SocialPresenceEngine;

// ── Analysis Results ────────────────────────────────────────

struct EmotionTrend
{
    QString dominant;              // "neutral", "positive", "stressed", "curious", "frustrated"
    double  confidence = 0.0;      // 0.0–1.0
    int     sampleCount = 0;
    QStringList signals;           // evidence phrases
};

struct ProductivityPattern
{
    int     totalMessages      = 0;
    int     questionCount      = 0;  // user asked questions
    int     commandCount       = 0;  // slash commands used
    int     topicSwitches      = 0;  // how often topic changed
    double  focusScore         = 0.0; // 0–1: sustained single-topic vs scattered
    QString dominantTopic;
    QStringList topicsDiscussed;
};

struct CognitiveShift
{
    QString topic;
    QString previousStance;
    QString currentStance;
    double  shiftMagnitude = 0.0;  // 0–1
    QDateTime detectedAt;
};

struct UserSummary
{
    QDateTime           generatedAt;
    EmotionTrend        emotion;
    ProductivityPattern productivity;
    QList<CognitiveShift> cognitiveShifts;

    // Pre-formatted morning nudge text
    QString             morningNudge;

    QJsonObject toJson() const;
};

// ── Reflection Engine ───────────────────────────────────────

class ReflectionEngine : public QObject
{
    Q_OBJECT

public:
    explicit ReflectionEngine(QObject* parent = nullptr);
    ~ReflectionEngine() override;

    void setMemoryManager(MemoryManager* mm)        { m_memory = mm; }
    void setSocialPresence(SocialPresenceEngine* sp) { m_presence = sp; }

    // Start the background reflection loop
    void start(int checkIntervalMinutes = 30);
    void stop();
    bool isRunning() const;

    // Force an immediate reflection cycle (for testing/debug)
    void triggerReflection();

    // Access the latest summary
    UserSummary lastSummary() const;

    // Path to the evolving UserProfile JSON
    static QString profileJsonPath();

signals:
    void reflectionComplete(const UserSummary& summary);
    void morningNudgeReady(const QString& nudgeText);
    void profileUpdated(const QString& path);

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

    // Output phases
    QString     composeMorningNudge(const UserSummary& summary) const;
    void        updateProfileJson(const UserSummary& summary);
    QJsonObject loadExistingProfile() const;
    void        saveProfile(const QJsonObject& profile) const;

    // Store reflection results into MemoryManager
    void storeReflectionMemories(const UserSummary& summary);

    // Emotional keyword matching
    struct EmotionSignal { QString keyword; QString emotion; double weight; };
    static const QList<EmotionSignal>& emotionLexicon();

    // Determine if a reflection should run
    bool shouldReflect() const;

    MemoryManager*         m_memory     = nullptr;
    SocialPresenceEngine*  m_presence   = nullptr;

    QThread*  m_workerThread = nullptr;
    QTimer*   m_checkTimer   = nullptr;

    UserSummary   m_lastSummary;
    mutable QMutex m_summaryMutex;

    QDateTime m_lastReflectionTime;
    bool      m_morningNudgePending = false;
    int       m_lastReflectionDay   = -1; // day-of-year of last morning summary

    static constexpr int    MIN_MESSAGES_FOR_REFLECTION = 5;
    static constexpr int    MIN_HOURS_BETWEEN_REFLECTIONS = 4;
    static constexpr double COGNITIVE_SHIFT_THRESHOLD = 0.3;
};
