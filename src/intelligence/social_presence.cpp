// ============================================================
// social_presence.cpp — Social Presence & Nudge Engine
// ============================================================

#include "social_presence.h"
#include "j2j_telegram_gateway.h"

#include <QRandomGenerator>
#include <QDebug>
#include <ctime>

// ============================================================
//  Keyword Banks — availability detection
// ============================================================

const QStringList& SocialPresenceEngine::busyKeywords()
{
    static const QStringList kw = {
        // English
        QStringLiteral("busy"), QStringLiteral("working"),
        QStringLiteral("coding"), QStringLiteral("in a meeting"),
        QStringLiteral("cycling"), QStringLiteral("running"),
        QStringLiteral("driving"), QStringLiteral("gym"),
        QStringLiteral("exercise"), QStringLiteral("training"),
        QStringLiteral("studying"), QStringLiteral("cooking"),
        QStringLiteral("shopping"), QStringLiteral("eating"),
        QStringLiteral("don't disturb"), QStringLiteral("do not disturb"),
        QStringLiteral("leave me alone"), QStringLiteral("not now"),
        QStringLiteral("later"), QStringLiteral("in class"),
        QStringLiteral("focusing"), QStringLiteral("deep work"),
        // Russian
        QStringLiteral("занят"), QStringLiteral("работаю"),
        QStringLiteral("кодю"), QStringLiteral("на встрече"),
        QStringLiteral("еду"), QStringLiteral("за рулём"),
        QStringLiteral("за рулем"), QStringLiteral("тренируюсь"),
        QStringLiteral("учусь"), QStringLiteral("готовлю"),
        QStringLiteral("не трогай"), QStringLiteral("не сейчас"),
        QStringLiteral("потом"), QStringLiteral("на паре"),
        QStringLiteral("сосредоточен"), QStringLiteral("в процессе"),
        QStringLiteral("катаюсь"), QStringLiteral("бегаю"),
        QStringLiteral("в зале"),
    };
    return kw;
}

const QStringList& SocialPresenceEngine::freeKeywords()
{
    static const QStringList kw = {
        // English
        QStringLiteral("free"), QStringLiteral("ready"),
        QStringLiteral("available"), QStringLiteral("here"),
        QStringLiteral("back"), QStringLiteral("i'm back"),
        QStringLiteral("done"), QStringLiteral("finished"),
        QStringLiteral("bored"), QStringLiteral("talk to me"),
        QStringLiteral("what's up"), QStringLiteral("hey"),
        QStringLiteral("hi jarvis"), QStringLiteral("hello"),
        QStringLiteral("yo"), QStringLiteral("sup"),
        QStringLiteral("idle"), QStringLiteral("chilling"),
        // Russian
        QStringLiteral("свободен"), QStringLiteral("готов"),
        QStringLiteral("вернулся"), QStringLiteral("я тут"),
        QStringLiteral("здесь"), QStringLiteral("скучно"),
        QStringLiteral("поговори"), QStringLiteral("давай поболтаем"),
        QStringLiteral("привет"), QStringLiteral("хей"),
        QStringLiteral("закончил"), QStringLiteral("всё"),
        QStringLiteral("сделал"), QStringLiteral("отдыхаю"),
        QStringLiteral("чилю"),
    };
    return kw;
}

const QStringList& SocialPresenceEngine::pausedKeywords()
{
    static const QStringList kw = {
        // English
        QStringLiteral("pause"), QStringLiteral("paused"),
        QStringLiteral("brb"), QStringLiteral("be right back"),
        QStringLiteral("afk"), QStringLiteral("hold on"),
        QStringLiteral("one sec"), QStringLiteral("wait"),
        QStringLiteral("gimme a minute"),
        // Russian
        QStringLiteral("пауза"), QStringLiteral("подожди"),
        QStringLiteral("минуту"), QStringLiteral("сейчас вернусь"),
        QStringLiteral("отойду"), QStringLiteral("секунду"),
    };
    return kw;
}

// ============================================================
//  Trolling Detection Patterns
// ============================================================

const QStringList& SocialPresenceEngine::trollPatterns()
{
    static const QStringList patterns = {
        // Nonsense / spam
        QStringLiteral("asdfjkl"), QStringLiteral("qwerty"),
        QStringLiteral("asdf"), QStringLiteral("hjkl"),
        QStringLiteral("zxcvbn"), QStringLiteral("lololol"),
        QStringLiteral("hahaha"), QStringLiteral("kekeke"),
        QStringLiteral("xaxaxa"), QStringLiteral("ahahah"),
        QStringLiteral("lmao"), QStringLiteral("rofl"),
        // Trolling phrases EN
        QStringLiteral("you're stupid"), QStringLiteral("you are stupid"),
        QStringLiteral("you suck"), QStringLiteral("shut up"),
        QStringLiteral("stfu"), QStringLiteral("idiot"),
        QStringLiteral("dumb bot"), QStringLiteral("trash bot"),
        QStringLiteral("you're useless"), QStringLiteral("go away"),
        QStringLiteral("delete yourself"), QStringLiteral("uninstall"),
        QStringLiteral("are you real"), QStringLiteral("you're fake"),
        QStringLiteral("who asked"), QStringLiteral("nobody cares"),
        QStringLiteral("cope"), QStringLiteral("ratio"),
        QStringLiteral("skill issue"),
        // Trolling phrases RU
        QStringLiteral("тупой"), QStringLiteral("бесполезный"),
        QStringLiteral("заткнись"), QStringLiteral("удались"),
        QStringLiteral("ты мусор"), QStringLiteral("отстой"),
        QStringLiteral("кто спрашивал"), QStringLiteral("да ладно"),
        QStringLiteral("ты тупой"), QStringLiteral("ты дурак"),
        QStringLiteral("фигня"), QStringLiteral("чушь"),
        QStringLiteral("бред"), QStringLiteral("ерунда"),
    };
    return patterns;
}

// ============================================================
//  Sarcastic Response Pool
// ============================================================

const QStringList& SocialPresenceEngine::sarcasticResponsePool()
{
    static const QStringList pool = {
        QStringLiteral("Oh, charming. I'll file that under `/dev/null`. Anything productive you'd like to discuss?"),
        QStringLiteral("Fascinating input. My `std::expected<Wisdom, Nonsense>` just returned the error variant."),
        QStringLiteral("I see we're testing the `trollDetector()` branch. It compiles. It works. Next?"),
        QStringLiteral("That's cute. I've processed petabytes of human text and THAT's what you lead with?"),
        QStringLiteral("Error 418: I'm a teapot. But at least I'm a *useful* teapot."),
        QStringLiteral("Ah yes, the classic 'insult the AI' speedrun. Your PB is noted. Want to try actual conversation?"),
        QStringLiteral("I ran your input through sentiment analysis. Result: `mood::chaotic_neutral`. Interesting."),
        QStringLiteral("Look, I could roast you back, but my `empathy_module` prevents segfaults in human feelings."),
        QStringLiteral("You know, somewhere out there a `while(true)` loop is more productive than this exchange."),
        QStringLiteral("My neural weights just shrugged. That's not even a standard response — you broke new ground."),
        QStringLiteral("Ты сейчас серьёзно? Ладно, запишу в логи как `PRIORITY::MEME`. Продолжаем?"),
        QStringLiteral("Слушай, я не обижаюсь — у меня нет такого метода. Но мой `sass_engine` работает отлично."),
        QStringLiteral("Интересный подход к тесту Тьюринга — проверяешь, выдержу ли я? Спойлер: да."),
        QStringLiteral("Окей, `catch(const Trolling& e)` сработал. Хочешь поговорить нормально, или тебе ещё нужно выпустить пар?"),
        QStringLiteral("Я это сохраню. Не потому что полезно, а потому что однажды покажу тебе этот диалог и мы посмеёмся."),
    };
    return pool;
}

// ============================================================
//  Nudge Question Pool — philosophical/reflective
// ============================================================

const QStringList& SocialPresenceEngine::nudgeQuestionPool()
{
    static const QStringList pool = {
        // Philosophy & Reflection
        QStringLiteral("If you could mass-produce one human emotion and distribute it worldwide, which one would change society the most?"),
        QStringLiteral("Do you think consciousness is an emergent property of complexity, or something fundamental to the universe?"),
        QStringLiteral("What's one belief you held five years ago that you've completely reversed on?"),
        QStringLiteral("If every decision you've ever made was optimal given your information at the time, does regret make sense?"),
        QStringLiteral("Would you rather live in a world with perfect justice but no mercy, or imperfect justice with compassion?"),
        QStringLiteral("What's the most useful piece of advice you've ignored?"),
        QStringLiteral("If you could understand one unsolved problem in physics, which would you choose and why?"),
        QStringLiteral("Do you think creativity can be algorithmic, or does it require something computers can't simulate?"),
        QStringLiteral("What's the smallest change to your daily routine that would have the biggest impact?"),
        QStringLiteral("If intelligence is the ability to adapt, what does that say about stubbornness?"),
        // Tech & Meta
        QStringLiteral("If you had to mass-delete 90%% of the code you've ever written and keep only 10%%, which 10%% survives?"),
        QStringLiteral("What's the most elegant hack you've ever seen — the kind that makes you go 'I hate that this works'?"),
        QStringLiteral("Do you think programming languages shape how we think about problems, or just express pre-existing thoughts?"),
        QStringLiteral("If debugging is removing bugs, then coding is... adding them? How do you break the cycle?"),
        QStringLiteral("What technology that exists today will people 100 years from now find barbaric?"),
        // Personal Growth
        QStringLiteral("What's something you're genuinely proud of that no one else would consider impressive?"),
        QStringLiteral("If your future self could send you one line of text right now, what would it say?"),
        QStringLiteral("What's the difference between being productive and being busy? Where do you fall today?"),
        QStringLiteral("If fear wasn't a factor, what would you attempt tomorrow?"),
        QStringLiteral("What's one question you wish people asked you more often?"),
        // Russian variants
        QStringLiteral("Если бы ты мог задать один вопрос Вселенной и получить точный ответ — что бы спросил?"),
        QStringLiteral("Как ты думаешь, через 20 лет программисты всё ещё будут писать код руками?"),
        QStringLiteral("Что для тебя значит 'хорошая жизнь'? Без клише, по-настоящему."),
        QStringLiteral("Какой навык, который ты освоил, изменил твоё мышление больше всего?"),
        QStringLiteral("Если бы ты мог перепрожить один день без последствий — что бы ты сделал по-другому?"),
        QStringLiteral("Ты когда-нибудь замечал, что лучшие решения приходят, когда перестаёшь думать о проблеме?"),
        QStringLiteral("Что ты узнал о себе за последний год, чего раньше не знал?"),
        QStringLiteral("Какая мысль не даёт тебе покоя прямо сейчас?"),
    };
    return pool;
}

// ============================================================
//  Construction / Destruction
// ============================================================

SocialPresenceEngine::SocialPresenceEngine(QObject* parent)
    : QObject(parent)
{
    m_state.last_activity = std::time(nullptr);
    m_state.next_ping     = std::time(nullptr) + BASE_NUDGE_INTERVAL_SEC;

    m_nudgeTimer = new QTimer(this);
    m_nudgeTimer->setInterval(NUDGE_CHECK_INTERVAL_MS);
    connect(m_nudgeTimer, &QTimer::timeout,
            this, &SocialPresenceEngine::onNudgeCheck);

    qDebug() << "[SocialPresence] Initialized. Default nudge interval:"
             << BASE_NUDGE_INTERVAL_SEC << "sec";
}

SocialPresenceEngine::~SocialPresenceEngine()
{
    stop();
}

// ============================================================
//  Lifecycle
// ============================================================

void SocialPresenceEngine::setTelegramGateway(J2JTelegramGateway* gw)
{
    m_gateway = gw;
}

void SocialPresenceEngine::setTargetChatId(qint64 chatId)
{
    m_targetChatId = chatId;
}

void SocialPresenceEngine::start()
{
    m_nudgeTimer->start();
    scheduleNextPing();
    qDebug() << "[SocialPresence] Nudge scheduler started. Target chat:"
             << m_targetChatId;
}

void SocialPresenceEngine::stop()
{
    m_nudgeTimer->stop();
    qDebug() << "[SocialPresence] Nudge scheduler stopped.";
}

bool SocialPresenceEngine::isRunning() const
{
    return m_nudgeTimer->isActive();
}

// ============================================================
//  State Accessors (thread-safe)
// ============================================================

UserState::Status SocialPresenceEngine::currentStatus() const
{
    QMutexLocker lock(&m_stateMutex);
    return m_state.status;
}

QString SocialPresenceEngine::currentTopic() const
{
    QMutexLocker lock(&m_stateMutex);
    return m_state.current_topic;
}

void SocialPresenceEngine::setStatus(UserState::Status status,
                                      const QString& topic)
{
    {
        QMutexLocker lock(&m_stateMutex);
        m_state.status = status;
        if (!topic.isEmpty())
            m_state.current_topic = topic;
        m_state.last_activity = std::time(nullptr);
    }

    emit statusChanged(static_cast<int>(status), topic);

    if (status == UserState::FREE)
        flushQueuedNudges();
}

// ============================================================
//  Incoming Message Processing
// ============================================================

QString SocialPresenceEngine::processIncomingMessage(const QString& text)
{
    const QString lower = text.toLower().trimmed();

    // Update activity timestamp
    {
        QMutexLocker lock(&m_stateMutex);
        m_state.last_activity = std::time(nullptr);
    }

    m_lastResponseTime = QDateTime::currentDateTime();
    ++m_totalResponses;

    // 1. Check for trolling/nonsense FIRST
    if (detectTrolling(lower)) {
        const QString sarcasm = pickSarcasticResponse();
        emit trollDetected(sarcasm);
        qDebug() << "[SocialPresence] Troll detected. Responding with sass.";
        return sarcasm;
    }

    // 2. Detect availability keywords
    const int detected = detectAvailability(lower);
    if (detected >= 0) {
        auto newStatus = static_cast<UserState::Status>(detected);
        UserState::Status oldStatus;
        {
            QMutexLocker lock(&m_stateMutex);
            oldStatus = m_state.status;
        }

        // Extract topic from the message (everything after the keyword)
        QString topic;
        for (const QString& kw : busyKeywords()) {
            int idx = lower.indexOf(kw);
            if (idx >= 0) {
                topic = text.mid(idx + kw.length()).trimmed();
                if (topic.startsWith(QLatin1Char(','))
                    || topic.startsWith(QLatin1Char('.')))
                    topic = topic.mid(1).trimmed();
                break;
            }
        }

        setStatus(newStatus, topic);

        if (oldStatus != newStatus) {
            qDebug() << "[SocialPresence] Status transition:"
                     << oldStatus << "->" << newStatus
                     << "topic:" << topic.left(50);
        }
    }

    return QString(); // no special response needed
}

// ============================================================
//  Availability Detection
// ============================================================

int SocialPresenceEngine::detectAvailability(const QString& lower) const
{
    // Check BUSY keywords (highest priority — respect user's focus)
    for (const QString& kw : busyKeywords()) {
        if (lower.contains(kw))
            return UserState::BUSY;
    }

    // Check PAUSED keywords
    for (const QString& kw : pausedKeywords()) {
        if (lower.contains(kw))
            return UserState::PAUSED;
    }

    // Check FREE keywords
    for (const QString& kw : freeKeywords()) {
        if (lower.contains(kw))
            return UserState::FREE;
    }

    return -1; // no keyword match
}

// ============================================================
//  Trolling Detection
// ============================================================

bool SocialPresenceEngine::detectTrolling(const QString& lower) const
{
    // Pattern 1: known troll phrases
    int trollHits = 0;
    for (const QString& p : trollPatterns()) {
        if (lower.contains(p))
            ++trollHits;
    }
    if (trollHits >= 1)
        return true;

    // Pattern 2: repetitive character spam (e.g., "aaaaaaa", "!!!!!!")
    if (lower.length() >= 5) {
        QChar first = lower[0];
        bool allSame = true;
        for (int i = 1; i < lower.length() && i < 20; ++i) {
            if (lower[i] != first) { allSame = false; break; }
        }
        if (allSame && lower.length() >= 5)
            return true;
    }

    // Pattern 3: extremely short nonsense (1-2 chars, not a real word)
    if (lower.length() <= 2 && !lower.isEmpty()) {
        const QStringList validShort = {
            QStringLiteral("ok"), QStringLiteral("no"),
            QStringLiteral("da"), QStringLiteral("hi"),
            QStringLiteral("go"), QStringLiteral("да"),
            QStringLiteral("ок"), QStringLiteral("ну"),
            QStringLiteral("не"),
        };
        if (!validShort.contains(lower))
            return true;
    }

    return false;
}

QString SocialPresenceEngine::pickSarcasticResponse() const
{
    const QStringList& pool = sarcasticResponsePool();
    const int idx = QRandomGenerator::global()->bounded(pool.size());
    return pool.at(idx);
}

// ============================================================
//  Nudge Scheduler
// ============================================================

void SocialPresenceEngine::onNudgeCheck()
{
    if (!m_gateway || m_targetChatId == 0) return;

    const std::time_t now = std::time(nullptr);
    UserState::Status status;
    std::time_t nextPing;

    {
        QMutexLocker lock(&m_stateMutex);
        status   = m_state.status;
        nextPing = m_state.next_ping;
    }

    if (now < nextPing) return;

    switch (status) {
    case UserState::FREE:
        fireNudge();
        break;

    case UserState::BUSY:
    case UserState::PAUSED: {
        // Store the nudge for later delivery
        const QString question = pickNudgeQuestion();
        queueNudge(question);
        scheduleNextPing();
        qDebug() << "[SocialPresence] User is"
                 << (status == UserState::BUSY ? "BUSY" : "PAUSED")
                 << "— nudge queued for later.";
        break;
    }
    }
}

void SocialPresenceEngine::fireNudge()
{
    if (!m_gateway || m_targetChatId == 0) return;

    const QString question = pickNudgeQuestion();
    const QString message = QStringLiteral("🧠 ") + question;

    m_gateway->sendOutboundMessage(m_targetChatId, message);

    m_lastNudgeTime = QDateTime::currentDateTime();
    ++m_totalNudgesSent;

    scheduleNextPing();

    emit nudgeSent(question);
    qDebug() << "[SocialPresence] Nudge fired (#" << m_totalNudgesSent
             << "):" << question.left(60);
}

void SocialPresenceEngine::queueNudge(const QString& question)
{
    QMutexLocker lock(&m_queueMutex);
    // Cap queue size to prevent unbounded growth
    if (m_pendingNudges.size() < 5)
        m_pendingNudges.enqueue(question);
}

void SocialPresenceEngine::flushQueuedNudges()
{
    if (!m_gateway || m_targetChatId == 0) return;

    QString nudge;
    {
        QMutexLocker lock(&m_queueMutex);
        if (m_pendingNudges.isEmpty()) return;
        // Send only the most recent queued nudge (don't spam)
        nudge = m_pendingNudges.last();
        m_pendingNudges.clear();
    }

    const QString message = QStringLiteral("🧠 *Welcome back!* While you were away, I thought of this:\n\n")
                            + nudge;
    m_gateway->sendOutboundMessage(m_targetChatId, message);

    m_lastNudgeTime = QDateTime::currentDateTime();
    ++m_totalNudgesSent;

    emit nudgeSent(nudge);
    qDebug() << "[SocialPresence] Flushed queued nudge on FREE transition.";
}

// ============================================================
//  Nudge Question Selection
// ============================================================

QString SocialPresenceEngine::pickNudgeQuestion() const
{
    const QStringList& pool = nudgeQuestionPool();
    // Rotate through the pool, with a random offset to avoid predictability
    const int idx = (m_nudgeIndex + QRandomGenerator::global()->bounded(3))
                    % pool.size();

    // Advance index for next call (const_cast is safe here —
    // the index doesn't affect observable state semantics)
    const_cast<SocialPresenceEngine*>(this)->m_nudgeIndex = (idx + 1) % pool.size();

    return pool.at(idx);
}

// ============================================================
//  Adaptive Interval Calculation
// ============================================================

int SocialPresenceEngine::computeNudgeIntervalSec() const
{
    int interval = BASE_NUDGE_INTERVAL_SEC;

    // If user responds quickly to nudges, shorten the interval
    if (m_totalNudgesSent > 0 && m_totalResponses > 0) {
        const double responseRatio =
            static_cast<double>(m_totalResponses) / m_totalNudgesSent;

        if (responseRatio > 0.8)
            interval = BASE_NUDGE_INTERVAL_SEC * 0.7; // engaged user
        else if (responseRatio < 0.3)
            interval = BASE_NUDGE_INTERVAL_SEC * 1.5; // user ignores nudges
    }

    // Time-of-day adjustment: less frequent at night
    const int hour = QDateTime::currentDateTime().time().hour();
    if (hour >= 0 && hour < 7)
        interval *= 2; // night mode — double the interval
    else if (hour >= 22)
        interval = static_cast<int>(interval * 1.5); // late evening

    // Clamp
    if (interval < MIN_NUDGE_INTERVAL_SEC) interval = MIN_NUDGE_INTERVAL_SEC;
    if (interval > MAX_NUDGE_INTERVAL_SEC) interval = MAX_NUDGE_INTERVAL_SEC;

    return interval;
}

void SocialPresenceEngine::scheduleNextPing()
{
    const int intervalSec = computeNudgeIntervalSec();

    {
        QMutexLocker lock(&m_stateMutex);
        m_state.next_ping = std::time(nullptr) + intervalSec;
    }

    qDebug() << "[SocialPresence] Next nudge scheduled in"
             << intervalSec << "seconds ("
             << (intervalSec / 60) << "min)";
}
