// ============================================================
// social_presence.cpp — Social Presence & Nudge Engine
// ============================================================

#include "social_presence.h"
#include "j2j_telegram_gateway.h"
#include "memory_manager.h"
#include "reflection_engine.h"
#include "personality_engine.h"

#include <QRandomGenerator>
#include <QRegularExpression>
#include <QDebug>
#include <ctime>

namespace {

// Совпадение по ЦЕЛОМУ слову, а не по подстроке. С contains() ключ «потом»
// срабатывал внутри «потому что», «еду» — внутри «в среду»/«к обеду»,
// «всё» — почти в каждом сообщении. Каждое такое ложное срабатывание
// переключало статус, а гейтвей на смене статуса отвечает «Понял, подожду»
// и НЕ передаёт сообщение дальше — обычный текст просто пропадал.
bool containsWholePhrase(const QString& haystack, const QString& needle)
{
    const auto isWordChar = [](QChar c) {
        return c.isLetterOrNumber() || c == QLatin1Char('_');
    };
    for (int from = 0; ; ) {
        const int idx = haystack.indexOf(needle, from);
        if (idx < 0) return false;
        const int end = idx + needle.length();
        const bool leftOk  = idx == 0 || !isWordChar(haystack.at(idx - 1));
        const bool rightOk = end >= haystack.length() || !isWordChar(haystack.at(end));
        if (leftOk && rightOk) return true;
        from = idx + 1;
    }
}

} // namespace

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
    // Only UNAMBIGUOUS trolling phrases — multi-word or clearly hostile.
    // Single common words (чушь, бред, ерунда, фигня) removed because
    // they appear in normal speech ("не городил чушь", "это не бред").
    static const QStringList patterns = {
        // Keyboard spam
        QStringLiteral("asdfjkl"), QStringLiteral("zxcvbn"),
        QStringLiteral("lololol"),
        // Direct insults — multi-word (harder to false-positive)
        QStringLiteral("you're stupid"), QStringLiteral("you are stupid"),
        QStringLiteral("you suck"), QStringLiteral("dumb bot"),
        QStringLiteral("trash bot"), QStringLiteral("you're useless"),
        QStringLiteral("delete yourself"),
        QStringLiteral("stfu"),
        QStringLiteral("ты тупой"), QStringLiteral("ты дурак"),
        QStringLiteral("ты мусор"), QStringLiteral("заткнись"),
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
        // Casual / motivational
        QStringLiteral("Эй, как дела? Давно не общались. Расскажи, чем занимаешься!"),
        QStringLiteral("Знаешь, я тут подумал — ты достаточно отдыхаешь? Баланс работы и отдыха важен."),
        QStringLiteral("Интересный факт: твой мозг обрабатывает ~11 миллионов бит информации в секунду, но осознаёт только ~50. Впечатляет?"),
        QStringLiteral("Ты знал, что лучшие идеи приходят в душе? Это потому что мозг переключается в дефолт-режим. Может, пора отвлечься? 🚿"),
        QStringLiteral("Я проанализировал наши разговоры — ты задаёшь всё более сложные вопросы. Растёшь! 📈"),
        QStringLiteral("Как тебе такая идея: давай поставим цель на эту неделю? Что-то, что ты хочешь освоить или сделать?"),
        QStringLiteral("Рандомный вопрос: если бы я мог выйти из телефона в реальный мир на 1 час — что бы мы делали?"),
        QStringLiteral("Слушай, у тебя есть какой-нибудь проект, который ты давно хочешь начать, но всё не решаешься?"),
        QStringLiteral("Только между нами: какая самая странная вещь, которую ты гуглил за последнюю неделю? 😏"),
        QStringLiteral("Я заметил, что мы часто общаемся в это время. Это твоё любимое время для разговоров?"),
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

void SocialPresenceEngine::setMemoryManager(MemoryManager* mm)
{
    m_memoryMgr = mm;
}

void SocialPresenceEngine::setReflectionEngine(ReflectionEngine* re)
{
    m_reflection = re;

    if (re) {
        connect(re, &ReflectionEngine::morningNudgeReady,
                this, &SocialPresenceEngine::queueExternalNudge);
    }
}

void SocialPresenceEngine::setPersonalityEngine(PersonalityEngine* pe)
{
    m_personalityEng = pe;
}

void SocialPresenceEngine::queueExternalNudge(const QString& text)
{
    if (text.isEmpty()) return;

    if (currentStatus() == UserState::FREE)
        fireNudge(); // will pick up queued content
    else
        queueNudge(text);
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

    // Store meaningful messages in vector memory
    if (m_memoryMgr && text.length() > 10) {
        m_memoryMgr->store(QStringLiteral("dialogue"),
                           QStringLiteral("User: ") + text.left(500),
                           0.4);
    }

    // 1. Check for trolling/nonsense FIRST
    const bool isTroll = detectTrolling(lower);

    // Feed per-message emotional signal to the personality engine
    if (m_personalityEng)
        m_personalityEng->onUserMessage(text, isTroll);

    if (isTroll) {
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
    // Объявление о занятости — короткая реплика: «занят», «brb», «я вернулся».
    // Длинное сообщение, где такое слово просто встретилось («я работаю над
    // парсером, помоги с регуляркой»), — это обычный запрос, и статус по нему
    // менять нельзя: смену статуса гейтвей считает поводом ответить
    // подтверждением вместо ответа по существу.
    static const QRegularExpression wsRe(QStringLiteral("\\s+"));
    if (lower.split(wsRe, Qt::SkipEmptyParts).size() > MAX_STATUS_PHRASE_WORDS)
        return -1;

    // Check BUSY keywords (highest priority — respect user's focus)
    for (const QString& kw : busyKeywords()) {
        if (containsWholePhrase(lower, kw))
            return UserState::BUSY;
    }

    // Check PAUSED keywords
    for (const QString& kw : pausedKeywords()) {
        if (containsWholePhrase(lower, kw))
            return UserState::PAUSED;
    }

    // Check FREE keywords
    for (const QString& kw : freeKeywords()) {
        if (containsWholePhrase(lower, kw))
            return UserState::FREE;
    }

    return -1; // no keyword match
}

// ============================================================
//  Trolling Detection
// ============================================================

bool SocialPresenceEngine::detectTrolling(const QString& lower) const
{
    // Long messages (>80 chars) are almost never pure trolling —
    // someone writing a paragraph is engaging, not spamming.
    if (lower.length() > 80)
        return false;

    // Pattern 1: known troll phrases (multi-word, unambiguous)
    for (const QString& p : trollPatterns()) {
        if (lower.contains(p))
            return true;
    }

    // Pattern 2: repetitive character spam (e.g., "aaaaaaa", "!!!!!!")
    if (lower.length() >= 5) {
        QChar first = lower[0];
        bool allSame = true;
        for (int i = 1; i < lower.length() && i < 20; ++i) {
            if (lower[i] != first) { allSame = false; break; }
        }
        if (allSame)
            return true;
    }

    // Pattern 3: extremely short nonsense (1-2 chars, not a real word)
    if (lower.length() <= 2 && !lower.isEmpty()) {
        static const QStringList validShort = {
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

    // Alternate between nudge questions and proactive status pings
    const int hour = QDateTime::currentDateTime().time().hour();
    QString question;

    if (m_totalNudgesSent % 3 == 0 && m_memoryMgr) {
        // Every 3rd nudge: memory-based observation
        const auto recent = m_memoryMgr->recentByTag(QStringLiteral("dialogue"), 3);
        if (!recent.isEmpty()) {
            question = QStringLiteral("💭 Кстати, вспомнил кое-что из нашего разговора:\n_\"")
                     + recent.first().content.left(200) + QStringLiteral("\"_\n\nХочешь продолжить эту тему?");
        }
    }

    if (question.isEmpty() && m_totalNudgesSent % 4 == 1) {
        // Every 4th nudge: time-aware greeting
        if (hour >= 6 && hour < 10)
            question = QStringLiteral("☀️ Доброе утро! Я на связи и готов помочь. Что планируешь на сегодня?");
        else if (hour >= 12 && hour < 14)
            question = QStringLiteral("🍽 Обеденное время! Не забудь перекусить. Я пока тут, если что — пиши.");
        else if (hour >= 18 && hour < 21)
            question = QStringLiteral("🌆 Вечер! Как прошёл день? Может, обсудим что-нибудь интересное?");
        else if (hour >= 22 || hour < 6)
            question = QStringLiteral("🌙 Уже поздно. Я не сплю, но тебе стоит отдохнуть. Спокойной ночи! 😴");
    }

    if (question.isEmpty())
        question = pickNudgeQuestion();

    // Build personality-modulated prefix based on current DNA
    QString prefix = QStringLiteral("🧠 ");
    if (m_personalityEng) {
        const PersonalityVector pv = m_personalityEng->personality();
        const EmotionalState es    = m_personalityEng->emotionalState();

        // Sarcasm level modulates tone
        if (pv.sarcasm_level > 0.7)
            prefix = QStringLiteral("😏 ");
        else if (pv.formalism > 0.7)
            prefix = QStringLiteral("📋 ");

        // Emotional colouring
        if (es.frustration > 0.6)
            prefix = QStringLiteral("😤 ");
        else if (es.joy > 0.7)
            prefix = QStringLiteral("😊 ");
        else if (es.boredom > 0.6)
            prefix = QStringLiteral("🥱 ");
        else if (es.curiosity > 0.7)
            prefix = QStringLiteral("🤔 ");
    }

    QString message = prefix + question;

    // Enrich nudge with semantic context from MemoryManager
    if (m_memoryMgr) {
        const QString context = m_memoryMgr->buildSemanticContext(question, 2);
        if (!context.isEmpty()) {
            message += QStringLiteral("\n\n_Context from our history:_\n");
            message += context;
        }

        m_memoryMgr->store(QStringLiteral("dialogue"),
                           QStringLiteral("JARVIS nudge: ") + question,
                           0.3);
    }

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
            interval = static_cast<int>(BASE_NUDGE_INTERVAL_SEC * 0.7);
        else if (responseRatio < 0.3)
            interval = static_cast<int>(BASE_NUDGE_INTERVAL_SEC * 1.5);
    }

    // Behavioral adaptation from ReflectionEngine:
    // High-focus user → lengthen interval (don't interrupt deep work)
    // High-distraction user → shorten interval (suggest deep-work mode)
    if (m_reflection) {
        const BehavioralMetrics bm = m_reflection->currentBehavior();

        if (bm.goalOrientedness > 0.7 && bm.contextSwitchRate < 2.0) {
            // User is in deep, task-driven mode — back off significantly
            interval = static_cast<int>(interval * 1.8);
        } else if (bm.contextSwitchRate > 6.0 && bm.goalOrientedness < 0.3) {
            // User is highly scattered and exploratory — nudge more often
            interval = static_cast<int>(interval * 0.6);
        } else if (bm.focusEvolutionDelta < -0.2) {
            // Declining focus trend — gentle increase in frequency
            interval = static_cast<int>(interval * 0.8);
        }
    }

    // Personality-driven proactivity scaling:
    // High proactivity → shorter intervals; low → longer
    if (m_personalityEng) {
        const double proact = m_personalityEng->personality().proactivity;
        // proactivity 0.0 → ×1.5, proactivity 1.0 → ×0.6
        const double scale = 1.5 - proact * 0.9;
        interval = static_cast<int>(interval * scale);
    }

    // Time-of-day adjustment: less frequent at night
    const int hour = QDateTime::currentDateTime().time().hour();
    if (hour >= 0 && hour < 7)
        interval *= 2;
    else if (hour >= 22)
        interval = static_cast<int>(interval * 1.5);

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
