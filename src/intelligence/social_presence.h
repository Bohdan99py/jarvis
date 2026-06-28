#pragma once
// ============================================================
// social_presence.h — Social Presence & Nudge Engine
//
// Tracks user availability via natural language analysis of
// incoming Telegram messages. Autonomously initiates proactive
// nudges (philosophical/reflective questions) when the user is
// FREE and the nudge timer fires.
//
// State machine:
//   FREE   (0) — user is available, nudges can fire
//   BUSY   (1) — user reported activity, nudges are queued
//   PAUSED (2) — user explicitly paused interaction
//
// No hardcoded configs. Status is derived purely from user
// input keyword analysis. Thread-safe via QMutex.
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QMutex>
#include <QQueue>
#include <QDateTime>
#include <ctime>

class J2JTelegramGateway;

// ── User availability state machine ─────────────────────────

struct UserState
{
    enum Status { FREE = 0, BUSY = 1, PAUSED = 2 };

    QString     current_topic;
    Status      status        = FREE;
    std::time_t last_activity = 0;
    std::time_t next_ping     = 0;
};

// ── Social Presence Engine ──────────────────────────────────

class SocialPresenceEngine : public QObject
{
    Q_OBJECT

public:
    explicit SocialPresenceEngine(QObject* parent = nullptr);
    ~SocialPresenceEngine() override;

    void setTelegramGateway(J2JTelegramGateway* gw);
    void setTargetChatId(qint64 chatId);

    void start();
    void stop();
    bool isRunning() const;

    // Called on every incoming Telegram message.
    // Parses text for availability keywords, updates UserState,
    // and returns a sarcastic response if trolling is detected
    // (empty string if the message is normal).
    QString processIncomingMessage(const QString& text);

    // Thread-safe state accessors
    UserState::Status currentStatus() const;
    QString           currentTopic() const;

    // Force status from external triggers
    void setStatus(UserState::Status status, const QString& topic = QString());

signals:
    void statusChanged(int newStatus, const QString& topic);
    void nudgeSent(const QString& question);
    void trollDetected(const QString& response);

private slots:
    void onNudgeCheck();

private:
    // Keyword analysis — returns detected status or -1 if no keywords matched
    int  detectAvailability(const QString& lower) const;
    bool detectTrolling(const QString& lower) const;
    QString pickSarcasticResponse() const;

    void scheduleNextPing();
    void fireNudge();
    void queueNudge(const QString& question);
    void flushQueuedNudges();

    QString pickNudgeQuestion() const;

    // Adaptive interval calculation based on user response patterns
    int computeNudgeIntervalSec() const;

    J2JTelegramGateway* m_gateway      = nullptr;
    QTimer*             m_nudgeTimer   = nullptr;
    qint64              m_targetChatId = 0;

    UserState           m_state;
    mutable QMutex      m_stateMutex;

    QQueue<QString>     m_pendingNudges;
    mutable QMutex      m_queueMutex;

    int                 m_nudgeIndex        = 0;
    int                 m_totalNudgesSent   = 0;
    int                 m_totalResponses    = 0;
    QDateTime           m_lastNudgeTime;
    QDateTime           m_lastResponseTime;

    static constexpr int BASE_NUDGE_INTERVAL_SEC = 1800;  // 30 min default
    static constexpr int MIN_NUDGE_INTERVAL_SEC  = 600;   // 10 min floor
    static constexpr int MAX_NUDGE_INTERVAL_SEC  = 7200;  // 2 hour ceiling
    static constexpr int NUDGE_CHECK_INTERVAL_MS = 30000; // check every 30s

    static const QStringList& nudgeQuestionPool();
    static const QStringList& sarcasticResponsePool();

    // Keyword sets for availability detection (built once, reused)
    static const QStringList& busyKeywords();
    static const QStringList& freeKeywords();
    static const QStringList& pausedKeywords();
    static const QStringList& trollPatterns();
};
