#pragma once
// ============================================================
// curiosity_engine.h — Proactive Curiosity Engine
//
// Context-aware system that initiates low-frequency dialogue
// "out of nowhere" — checking on projects, suggesting breaks
// past midnight, or asking about user interests.
//
// Triggers: idle timer + message counter + time-of-day awareness.
// Responses stored in user_personality_matrix for learning.
// ============================================================

#include <QObject>
#include <QString>
#include <QTimer>
#include <QStringList>
#include <QDateTime>

class J2JTelegramGateway;
class ActivityTracker;

class CuriosityEngine : public QObject
{
    Q_OBJECT

public:
    static CuriosityEngine& instance();

    CuriosityEngine(const CuriosityEngine&)            = delete;
    CuriosityEngine& operator=(const CuriosityEngine&) = delete;

    void setTelegramGateway(J2JTelegramGateway* gw) { m_gateway = gw; }
    void setActivityTracker(ActivityTracker* at) { m_activity = at; }
    void setTargetChatId(qint64 chatId) { m_targetChatId = chatId; }

    void start(int intervalMinutes = 120);
    void stop();
    bool isRunning() const;

    void notifyUserActivity();

    void saveResponse(qint64 chatId,
                      const QString& question,
                      const QString& answer);

    void ensureTable();

    enum class ProactiveCategory {
        Philosophy,
        WellBeing,
        ProjectCheckIn,
        TechCuriosity,
        TimeAwareness,
        Casual
    };
    Q_ENUM(ProactiveCategory)

signals:
    void questionPosted(const QString& question);
    void proactiveDialogue(const QString& message, ProactiveCategory category);

private:
    explicit CuriosityEngine(QObject* parent = nullptr);
    ~CuriosityEngine() override;

    void onIdleCheck();
    void postContextAwareQuestion();

    ProactiveCategory selectCategory() const;
    QString pickQuestion(ProactiveCategory category) const;
    bool shouldInterrupt() const;

    J2JTelegramGateway* m_gateway      = nullptr;
    ActivityTracker*    m_activity      = nullptr;
    QTimer*             m_idleTimer    = nullptr;
    qint64              m_targetChatId = 0;
    int                 m_questionIndex = 0;

    QDateTime           m_lastUserActivity;
    QDateTime           m_lastQuestionTime;
    int                 m_messagesSinceLastQuestion = 0;
    int                 m_sessionQuestionCount = 0;

    static constexpr int MIN_IDLE_SECONDS = 300;
    static constexpr int MIN_MESSAGES_BETWEEN = 8;
    static constexpr int MAX_QUESTIONS_PER_SESSION = 5;
    static constexpr int COOLDOWN_MINUTES = 45;

    static const QStringList& philosophyPool();
    static const QStringList& wellBeingPool();
    static const QStringList& projectPool();
    static const QStringList& techPool();
    static const QStringList& lateNightPool();
    static const QStringList& casualPool();
};
