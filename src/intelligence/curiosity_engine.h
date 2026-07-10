#pragma once
// ============================================================
// curiosity_engine.h — Proactive Curiosity Engine
//
// Context-aware system that initiates low-frequency dialogue
// "out of nowhere" — checking on projects, suggesting breaks
// past midnight, or asking about user interests.
//
// Triggers: idle timer + message counter + time-of-day awareness
//           + visual context from ScreenshotLearner.
//
// Attention cost model:
//   HIGH  — fullscreen game, heavy typing in IDE → stay silent
//   MEDIUM — active browsing, casual app use → low-priority only
//   LOW   — idle for 3-5 min with stale project context → speak up
//
// Responses stored in user_personality_matrix for learning.
// ============================================================

#include <QObject>
#include <QString>
#include <QTimer>
#include <QStringList>
#include <QDateTime>

class J2JTelegramGateway;
class ActivityTracker;
class ScreenshotLearner;

// ============================================================
//  VisualContext — latest image-based learning tokens
// ============================================================

struct VisualContext
{
    QDateTime   capturedAt;
    QString     activeApp;       // "CLion", "Blender", "Unreal Engine"
    QString     windowTitle;     // raw window title
    QString     category;        // "coding", "art", "gaming", ...
    QStringList contextTokens;   // extracted keywords: "error", "mesh", "blueprint", ...

    bool isValid() const { return capturedAt.isValid() && !activeApp.isEmpty(); }
    bool isStale(int maxAgeSec = 600) const {
        return !isValid() || capturedAt.secsTo(QDateTime::currentDateTime()) > maxAgeSec;
    }
};

// ============================================================
//  CuriosityEngine
// ============================================================

class CuriosityEngine : public QObject
{
    Q_OBJECT

public:
    static CuriosityEngine& instance();

    CuriosityEngine(const CuriosityEngine&)            = delete;
    CuriosityEngine& operator=(const CuriosityEngine&) = delete;

    void setTelegramGateway(J2JTelegramGateway* gw) { m_gateway = gw; }
    void setActivityTracker(ActivityTracker* at) { m_activity = at; }
    void setScreenshotLearner(ScreenshotLearner* sl) { m_screenshotLearner = sl; }
    void setTargetChatId(qint64 chatId) { m_targetChatId = chatId; }
    void setUiEnglish(bool english) { m_uiEnglish = english; }

    void start(int intervalMinutes = 120);
    void stop();
    bool isRunning() const;

    void notifyUserActivity();

    void saveResponse(qint64 chatId,
                      const QString& question,
                      const QString& answer);

    // --- Pending-question state: lets callers tell "this is the answer
    //     to my proactive question" apart from a normal chat message ---
    bool hasPendingQuestion() const;
    // If a question is pending for chatId, records answerText as the
    // answer, clears the pending state, and returns true. Otherwise
    // returns false and leaves state untouched.
    //
    // replyToMessageId (from the incoming Telegram message's
    // reply_to_message.message_id, 0 if not an explicit reply): if it
    // matches m_pendingMessageId, the answer is accepted regardless of how
    // much time has passed — an explicit Telegram reply is the caller
    // saying "this is that answer," not a guess. Otherwise falls back to
    // the PENDING_ANSWER_WINDOW_MINUTES timestamp check.
    bool consumeAnswer(qint64 chatId, const QString& answerText,
                       qint64 replyToMessageId = 0);

    // Set by the gateway once it knows the Telegram message_id of the
    // question just posted (see J2JTelegramGateway::sendProactiveQuestion).
    void setPendingMessageId(qint64 messageId) { m_pendingMessageId = messageId; }

    // On-demand — called by CaseDistiller right when its nightly cycle
    // finds a case that may contradict a stored opinion. Unlike
    // postContextAwareQuestion(), NOT part of the idle-timer
    // selectCategory() rotation (that's for Jarvis deciding to initiate
    // small talk when idle; this is reporting a concrete finding as soon as
    // it's found). Posts to ownerId directly rather than m_targetChatId, so
    // it works for any owner, not just the primary desktop target — though
    // note the single pending-question slot below is still global, so a
    // second pending question (from any source) before this one is
    // answered will overwrite it.
    void postOpinionRevisionQuestion(qint64 ownerId, qint64 opinionId, const QString& question);

    void ensureTable();

    // --- Visual context from ScreenshotLearner ---
    void updateVisualContext(const QString& appName,
                             const QString& windowTitle,
                             const QString& category);
    const VisualContext& lastVisualContext() const { return m_visualCtx; }

    // --- Attention cost model ---
    enum class AttentionLevel { Low, Medium, High };
    Q_ENUM(AttentionLevel)
    AttentionLevel evaluateAttention() const;

    enum class ProactiveCategory {
        Philosophy,
        WellBeing,
        ProjectCheckIn,
        TechCuriosity,
        TimeAwareness,
        Casual,
        VisualContextual,  // question derived from screenshot context
        DoubtVerification, // "What if I am wrong?" — asks user to verify
        PersonalProfiling, // calibration question for empty/low-conf profile fields
        OpinionRevision    // CaseDistiller found a case that may contradict
                           // a stored opinion — posted on demand, not part
                           // of the idle-timer selectCategory() rotation
    };
    Q_ENUM(ProactiveCategory)

signals:
    // options is always {"Да","Нет"} / {"Yes","No"} today — Jarvis still
    // accepts free-text answers too, buttons are just the fast path.
    void questionPosted(const QString& question, const QStringList& options);
    void proactiveDialogue(const QString& message, ProactiveCategory category);

private:
    explicit CuriosityEngine(QObject* parent = nullptr);
    ~CuriosityEngine() override;

    void onIdleCheck();
    void postContextAwareQuestion();

    ProactiveCategory selectCategory() const;
    QString pickQuestion(ProactiveCategory category) const;
    QString buildVisualContextQuestion() const;
    QString buildDoubtVerificationQuestion() const;
    QString buildPersonalProfilingQuestion() const;
    bool shouldInterrupt() const;
    void expirePendingIfStale();

    J2JTelegramGateway* m_gateway          = nullptr;
    ActivityTracker*    m_activity          = nullptr;
    ScreenshotLearner*  m_screenshotLearner = nullptr;
    QTimer*             m_idleTimer        = nullptr;
    qint64              m_targetChatId     = 0;
    int                 m_questionIndex    = 0;
    bool                m_uiEnglish        = false;

    QDateTime           m_lastUserActivity;
    QDateTime           m_lastQuestionTime;
    int                 m_messagesSinceLastQuestion = 0;
    int                 m_sessionQuestionCount = 0;

    // Visual context from last ScreenshotLearner capture
    VisualContext       m_visualCtx;

    // Pending question awaiting a user answer (Telegram button, PC button,
    // or free text) — lets Jarvis::processCommand tell "this message is
    // the answer to my question" apart from a normal chat message.
    QString             m_pendingQuestion;
    qint64              m_pendingChatId    = 0;
    QDateTime           m_pendingTimestamp;
    ProactiveCategory   m_pendingCategory  = ProactiveCategory::Casual;
    qint64              m_pendingDoubtId   = 0; // set when category == DoubtVerification
    qint64              m_pendingMessageId = 0; // Telegram message_id of the posted question
    qint64              m_pendingOpinionId = 0; // set when category == OpinionRevision

    static constexpr int MIN_IDLE_SECONDS = 300;
    static constexpr int MIN_MESSAGES_BETWEEN = 8;
    static constexpr int MAX_QUESTIONS_PER_SESSION = 5;
    static constexpr int COOLDOWN_MINUTES = 45;
    // Fallback window for free-text answers with no explicit Telegram
    // reply_to_message match (see consumeAnswer) — generous because
    // proactive questions fire specifically when the user has been idle,
    // so a realistic reply often arrives well after a few minutes. An
    // explicit reply-to-this-message match (m_pendingMessageId) has no
    // time limit at all.
    static constexpr int PENDING_ANSWER_WINDOW_MINUTES = 240;

    static const QStringList& philosophyPool(bool english);
    static const QStringList& wellBeingPool(bool english);
    static const QStringList& projectPool(bool english);
    static const QStringList& techPool(bool english);
    static const QStringList& lateNightPool(bool english);
    static const QStringList& casualPool(bool english);
};
