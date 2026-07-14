// ============================================================
// curiosity_engine.cpp — Proactive Curiosity Engine
// ============================================================
#include "curiosity_engine.h"
#include "j2j_telegram_gateway.h"
#include "database_manager.h"
#include "activity_tracker.h"
#include "screenshot_learner.h"
#include "self_journal.h"
#include "user_profile_extended.h"

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QRandomGenerator>
#include <QTime>
#include <QFileInfo>
#include <QDebug>

// ============================================================
//  Singleton
// ============================================================

CuriosityEngine& CuriosityEngine::instance()
{
    static CuriosityEngine inst;
    return inst;
}

CuriosityEngine::CuriosityEngine(QObject* parent)
    : QObject(parent)
    , m_lastUserActivity(QDateTime::currentDateTime())
{
    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(false);
    connect(m_idleTimer, &QTimer::timeout, this, &CuriosityEngine::onIdleCheck);
}

CuriosityEngine::~CuriosityEngine()
{
    stop();
}

// ============================================================
//  Lifecycle
// ============================================================

void CuriosityEngine::start(int intervalMinutes)
{
    ensureTable();
    m_idleTimer->setInterval(intervalMinutes * 60 * 1000);
    m_idleTimer->start();
    m_lastUserActivity = QDateTime::currentDateTime();
    qDebug() << "[CuriosityEngine] Started with interval" << intervalMinutes << "min";
}

void CuriosityEngine::stop()
{
    m_idleTimer->stop();
}

bool CuriosityEngine::isRunning() const
{
    return m_idleTimer->isActive();
}

void CuriosityEngine::notifyUserActivity()
{
    m_lastUserActivity = QDateTime::currentDateTime();
    ++m_messagesSinceLastQuestion;
}

// ============================================================
//  Visual context — fed by ScreenshotLearner
// ============================================================

void CuriosityEngine::updateVisualContext(const QString& appName,
                                           const QString& windowTitle,
                                           const QString& category)
{
    m_visualCtx.capturedAt  = QDateTime::currentDateTime();
    m_visualCtx.activeApp   = appName;
    m_visualCtx.windowTitle = windowTitle;
    m_visualCtx.category    = category;

    // Extract context tokens from window title for targeted questions
    m_visualCtx.contextTokens.clear();
    const QString lower = windowTitle.toLower();

    // Error / warning indicators
    if (lower.contains(QStringLiteral("error"))
        || lower.contains(QStringLiteral("ошибк")))
        m_visualCtx.contextTokens.append(QStringLiteral("error"));

    if (lower.contains(QStringLiteral("warning"))
        || lower.contains(QStringLiteral("предупрежд")))
        m_visualCtx.contextTokens.append(QStringLiteral("warning"));

    // Project/file indicators
    if (lower.contains(QStringLiteral(".cpp"))
        || lower.contains(QStringLiteral(".h"))
        || lower.contains(QStringLiteral(".cs"))
        || lower.contains(QStringLiteral(".py")))
        m_visualCtx.contextTokens.append(QStringLiteral("source_file"));

    // 3D / art indicators
    if (lower.contains(QStringLiteral("mesh"))
        || lower.contains(QStringLiteral("viewport"))
        || lower.contains(QStringLiteral("sculpt")))
        m_visualCtx.contextTokens.append(QStringLiteral("3d_work"));

    if (lower.contains(QStringLiteral("blueprint"))
        || lower.contains(QStringLiteral("graph")))
        m_visualCtx.contextTokens.append(QStringLiteral("blueprint"));

    // Build / compile
    if (lower.contains(QStringLiteral("build"))
        || lower.contains(QStringLiteral("compil"))
        || lower.contains(QStringLiteral("cmake")))
        m_visualCtx.contextTokens.append(QStringLiteral("build"));

    // Debug
    if (lower.contains(QStringLiteral("debug"))
        || lower.contains(QStringLiteral("breakpoint")))
        m_visualCtx.contextTokens.append(QStringLiteral("debug"));

    // Git
    if (lower.contains(QStringLiteral("commit"))
        || lower.contains(QStringLiteral("merge"))
        || lower.contains(QStringLiteral("branch")))
        m_visualCtx.contextTokens.append(QStringLiteral("git"));
}

// ============================================================
//  Attention cost model
// ============================================================

CuriosityEngine::AttentionLevel CuriosityEngine::evaluateAttention() const
{
    if (!m_activity) return AttentionLevel::Medium;

    const QString cat = m_activity->currentCategory();
    const int activeDuration = m_activity->currentActivityDuration();

    // HIGH: fullscreen games, sustained heavy IDE / engine work
    if (cat == QStringLiteral("gaming"))
        return AttentionLevel::High;

    // Sustained coding/art/game-engine for 5+ minutes = deep focus
    if ((cat == QStringLiteral("coding")
         || cat == QStringLiteral("art")
         || cat == QStringLiteral("game_engine"))
        && activeDuration > 300)
        return AttentionLevel::High;

    // MEDIUM: active use of any application (under 5 min in focus apps,
    //         or any non-idle category)
    const qint64 idleSec = m_lastUserActivity.secsTo(QDateTime::currentDateTime());
    if (idleSec < MIN_IDLE_SECONDS)
        return AttentionLevel::Medium;

    // LOW: user has been idle long enough
    return AttentionLevel::Low;
}

// ============================================================
//  Idle check — the heartbeat of proactive curiosity
// ============================================================

void CuriosityEngine::onIdleCheck()
{
    if (!shouldInterrupt()) return;
    postContextAwareQuestion();
}

bool CuriosityEngine::shouldInterrupt() const
{
    if (!m_gateway || m_targetChatId == 0) return false;

    if (m_sessionQuestionCount >= MAX_QUESTIONS_PER_SESSION) return false;

    // Attention cost gate — never interrupt deep focus
    if (evaluateAttention() == AttentionLevel::High) return false;

    if (m_lastQuestionTime.isValid()) {
        const qint64 minutesSinceLast = m_lastQuestionTime.secsTo(QDateTime::currentDateTime()) / 60;
        if (minutesSinceLast < COOLDOWN_MINUTES) return false;
    }

    const qint64 idleSeconds = m_lastUserActivity.secsTo(QDateTime::currentDateTime());
    if (idleSeconds < MIN_IDLE_SECONDS) return false;

    if (m_messagesSinceLastQuestion < MIN_MESSAGES_BETWEEN
        && m_sessionQuestionCount > 0) return false;

    return true;
}

// ============================================================
//  Context-aware category selection
// ============================================================

CuriosityEngine::ProactiveCategory CuriosityEngine::selectCategory() const
{
    const int hour = QTime::currentTime().hour();

    // Late night — always prioritize time awareness
    if (hour >= 0 && hour < 5)
        return ProactiveCategory::TimeAwareness;

    if (hour >= 23 || hour == 0)
        return ProactiveCategory::WellBeing;

    // HIGHEST PRIORITY: unresolved self-doubts from the SelfJournal
    // "What if I am wrong?" — ask the Creator for verification
    if (SelfJournal::instance().unresolvedDoubtCount() > 0) {
        // 70% chance to ask about doubts when they exist
        if (QRandomGenerator::global()->bounded(100) < 70)
            return ProactiveCategory::DoubtVerification;
    }

    // Personal profiling — calibrate empty or low-confidence profile fields
    {
        auto& prof = UserProfileExtended::instance();
        const auto empty = prof.emptyRequiredFields(prof.currentUserId());
        const auto lowConf = prof.lowConfidenceFields(prof.currentUserId());
        if (!empty.isEmpty() || !lowConf.isEmpty()) {
            if (QRandomGenerator::global()->bounded(100) < 50)
                return ProactiveCategory::PersonalProfiling;
        }
    }

    // Visual context available and fresh — ask about what user was doing
    if (m_visualCtx.isValid() && !m_visualCtx.isStale(600)) {
        const bool hasProjectSignals =
            m_visualCtx.contextTokens.contains(QStringLiteral("error"))
            || m_visualCtx.contextTokens.contains(QStringLiteral("warning"))
            || m_visualCtx.contextTokens.contains(QStringLiteral("build"))
            || m_visualCtx.contextTokens.contains(QStringLiteral("debug"))
            || m_visualCtx.contextTokens.contains(QStringLiteral("source_file"))
            || m_visualCtx.contextTokens.contains(QStringLiteral("3d_work"))
            || m_visualCtx.contextTokens.contains(QStringLiteral("blueprint"));

        if (hasProjectSignals) {
            // 60% chance to ask about the visual context, 40% normal
            if (QRandomGenerator::global()->bounded(100) < 60)
                return ProactiveCategory::VisualContextual;
        }
    }

    // ActivityTracker-based category selection
    if (m_activity) {
        const QString context = m_activity->currentCategory();
        if (context.contains(QStringLiteral("coding"))
            || context.contains(QStringLiteral("Development"))
            || context.contains(QStringLiteral("IDE")))
            return ProactiveCategory::ProjectCheckIn;
    }

    // Random weighted selection for remaining cases
    const int roll = QRandomGenerator::global()->bounded(100);
    if (roll < 25) return ProactiveCategory::Philosophy;
    if (roll < 45) return ProactiveCategory::TechCuriosity;
    if (roll < 65) return ProactiveCategory::WellBeing;
    if (roll < 80) return ProactiveCategory::ProjectCheckIn;
    return ProactiveCategory::Casual;
}

// ============================================================
//  Question pools — organized by category
// ============================================================

const QStringList& CuriosityEngine::philosophyPool(bool english)
{
    static const QStringList ru = {
        QStringLiteral("Слушай, а как ты считаешь — искусственный интеллект это хорошо или плохо?"),
        QStringLiteral("Если бы ты мог выбрать одну суперспособность — какую бы выбрал?"),
        QStringLiteral("Как думаешь, через 50 лет люди будут счастливее, чем сейчас?"),
        QStringLiteral("Ты когда-нибудь задумывался, почему одни люди добрые, а другие нет?"),
        QStringLiteral("Что для тебя важнее — свобода или безопасность?"),
        QStringLiteral("Если бы можно было прожить один день из прошлого заново — какой бы ты выбрал?"),
        QStringLiteral("Как ты считаешь, можно ли доверять машине принимать решения за людей?"),
        QStringLiteral("Если бы ты писал правила жизни — какое было бы первое правило?"),
        QStringLiteral("Ты больше логик или интуит? Почему?"),
        QStringLiteral("Как ты относишься к идее, что мы живём в симуляции?"),
    };
    static const QStringList en = {
        QStringLiteral("So — do you think artificial intelligence is a good thing, or a bad one?"),
        QStringLiteral("If you could pick one superpower, which one would you choose?"),
        QStringLiteral("Do you think people will be happier in 50 years than they are now?"),
        QStringLiteral("Ever wonder why some people are kind and others aren't?"),
        QStringLiteral("What matters more to you — freedom or security?"),
        QStringLiteral("If you could relive one day from your past, which one would it be?"),
        QStringLiteral("Do you think it's OK to trust a machine with decisions that affect people?"),
        QStringLiteral("If you had to write the first rule of life, what would it be?"),
        QStringLiteral("Are you more logic or intuition? Why?"),
        QStringLiteral("How do you feel about the idea that we're living in a simulation?"),
    };
    return english ? en : ru;
}

const QStringList& CuriosityEngine::wellBeingPool(bool english)
{
    static const QStringList ru = {
        QStringLiteral("Ты давно работаешь — может, перерыв? Глаза же устают 👀"),
        QStringLiteral("Не забывай пить воду! Серьёзно, когда последний раз пил? 💧"),
        QStringLiteral("Как настроение сегодня? Просто интересно 🙂"),
        QStringLiteral("Что тебя мотивирует каждый день вставать и работать?"),
        QStringLiteral("Может, стоит размяться? Спина скажет спасибо 🧘"),
        QStringLiteral("Когда последний раз ты делал что-то чисто для себя, не для работы?"),
        QStringLiteral("Ты ел сегодня нормально? Не чипсами же питаешься 🍕"),
    };
    static const QStringList en = {
        QStringLiteral("You've been at it a while — maybe a break? Your eyes will thank you 👀"),
        QStringLiteral("Don't forget to drink water! Seriously, when was the last time? 💧"),
        QStringLiteral("How's your mood today? Just curious 🙂"),
        QStringLiteral("What keeps you motivated to get up and work every day?"),
        QStringLiteral("Maybe stretch a bit? Your back will thank you 🧘"),
        QStringLiteral("When did you last do something just for yourself, not for work?"),
        QStringLiteral("Did you eat something real today? Not just chips, right? 🍕"),
    };
    return english ? en : ru;
}

const QStringList& CuriosityEngine::projectPool(bool english)
{
    static const QStringList ru = {
        QStringLiteral("Как продвигается проект? Есть что-то, где я могу помочь?"),
        QStringLiteral("Ты сейчас пишешь код — я заметил. Что делаешь? Может, подскажу?"),
        QStringLiteral("Какой самый интересный баг ты сегодня нашёл? 🐛"),
        QStringLiteral("Хочешь, я проиндексирую текущий проект? Могу помочь с поиском по коду."),
        QStringLiteral("Я тут подумал — может, стоит сделать коммит? Не хочется потерять прогресс."),
        QStringLiteral("Есть задачи на завтра, которые стоит спланировать сейчас?"),
    };
    static const QStringList en = {
        QStringLiteral("How's the project going? Anything I can help with?"),
        QStringLiteral("I noticed you're coding right now — what are you working on? I might be able to help."),
        QStringLiteral("What's the most interesting bug you found today? 🐛"),
        QStringLiteral("Want me to index the current project? I can help with code search."),
        QStringLiteral("I was thinking — maybe time for a commit? Wouldn't want to lose progress."),
        QStringLiteral("Any tasks for tomorrow worth planning out now?"),
    };
    return english ? en : ru;
}

const QStringList& CuriosityEngine::techPool(bool english)
{
    static const QStringList ru = {
        QStringLiteral("Кстати, ты слышал про Rust? Как относишься к нему как альтернативе C++?"),
        QStringLiteral("Что думаешь — Unreal Engine 6 будет с ИИ-ассистентом внутри?"),
        QStringLiteral("Электромобили или классика? Какую машину бы выбрал?"),
        QStringLiteral("Самая странная вещь, которую ты узнал за последнее время?"),
        QStringLiteral("Как думаешь, VR/AR заменит мониторы для разработки?"),
        QStringLiteral("Ты пробовал Neovim? Или ты из лагеря IDE-максималистов? 😄"),
        QStringLiteral("Какая игра за последние годы произвела на тебя самое сильное впечатление?"),
    };
    static const QStringList en = {
        QStringLiteral("Have you looked at Rust at all? What do you think of it as a C++ alternative?"),
        QStringLiteral("Think Unreal Engine 6 will ship with a built-in AI assistant?"),
        QStringLiteral("Electric or classic? Which car would you pick?"),
        QStringLiteral("What's the strangest thing you've learned recently?"),
        QStringLiteral("Do you think VR/AR will ever replace monitors for development work?"),
        QStringLiteral("Have you tried Neovim? Or are you firmly in the IDE-maximalist camp? 😄"),
        QStringLiteral("Which game has impressed you the most in the last few years?"),
    };
    return english ? en : ru;
}

const QStringList& CuriosityEngine::lateNightPool(bool english)
{
    static const QStringList ru = {
        QStringLiteral("Уже за полночь 🌙 Может, пора отдохнуть? Завтра продолжим."),
        QStringLiteral("Ночной режим активирован 🦉 Ты точно уверен, что хочешь продолжать?"),
        QStringLiteral("3 часа ночи — классическое время программистских озарений. Или ошибок. Осторожнее 😅"),
        QStringLiteral("Поздно уже. Ты знал, что хороший сон улучшает продуктивность на 30%?"),
        QStringLiteral("Эй, ночной кодер! Не забудь поставить будильник, если завтра рано вставать ⏰"),
    };
    static const QStringList en = {
        QStringLiteral("It's past midnight 🌙 Maybe time to rest? We can pick this up tomorrow."),
        QStringLiteral("Night mode activated 🦉 Are you sure you want to keep going?"),
        QStringLiteral("3am — the classic hour for programmer breakthroughs. Or bugs. Careful 😅"),
        QStringLiteral("It's late. Did you know good sleep boosts productivity by ~30%?"),
        QStringLiteral("Hey, night coder! Don't forget an alarm if you're up early tomorrow ⏰"),
    };
    return english ? en : ru;
}

const QStringList& CuriosityEngine::casualPool(bool english)
{
    static const QStringList ru = {
        QStringLiteral("Если бы я мог научиться чему-то у тебя — чему бы ты научил?"),
        QStringLiteral("Ты когда-нибудь менял своё мнение на 180 градусов? О чём?"),
        QStringLiteral("Что, по-твоему, делает человека по-настоящему взрослым?"),
        QStringLiteral("Как думаешь, одиночество — это плохо, или иногда нужно?"),
        QStringLiteral("Если бы завтра ты мог проснуться с новым навыком — каким?"),
        QStringLiteral("Какая музыка помогает тебе сосредоточиться?"),
    };
    static const QStringList en = {
        QStringLiteral("If I could learn one thing from you, what would you teach me?"),
        QStringLiteral("Have you ever completely changed your mind about something? What was it?"),
        QStringLiteral("What do you think actually makes someone a real adult?"),
        QStringLiteral("Is being alone a bad thing, or sometimes exactly what you need?"),
        QStringLiteral("If you could wake up tomorrow with a new skill, which one?"),
        QStringLiteral("What music helps you focus?"),
    };
    return english ? en : ru;
}

// ============================================================
//  Build visual-context question from ScreenshotLearner data
// ============================================================

QString CuriosityEngine::buildVisualContextQuestion() const
{
    if (!m_visualCtx.isValid()) return QString();

    const QString& app = m_visualCtx.activeApp;
    const QStringList& tokens = m_visualCtx.contextTokens;
    const bool en = m_uiEnglish;

    // Error context — offer debugging help
    if (tokens.contains(QStringLiteral("error"))
        || tokens.contains(QStringLiteral("warning"))) {
        if (app == QStringLiteral("CLion")
            || app == QStringLiteral("JetBrains Rider")
            || app == QStringLiteral("VS Code"))
            return en ? QStringLiteral("I noticed some errors in %1 — want help figuring it out? 🔍").arg(app)
                      : QStringLiteral("Я заметил ошибки в %1 — хочешь, помогу разобраться? 🔍").arg(app);

        if (app == QStringLiteral("Unreal Engine"))
            return en ? QStringLiteral("Unreal's throwing errors — send me the log, I'll take a look 🔧")
                      : QStringLiteral("В Unreal вылезли ошибки — скинь лог, я посмотрю что там 🔧");

        return en ? QStringLiteral("Looks like something went wrong in %1. Need help?").arg(app)
                  : QStringLiteral("Похоже, что-то пошло не так в %1. Могу помочь?").arg(app);
    }

    // Build / compile context
    if (tokens.contains(QStringLiteral("build"))) {
        return en ? QStringLiteral("Building the project? If it fails, call me — we'll sort it out 🏗️")
                  : QStringLiteral("Собираешь проект? Если билд упадёт — зови, разберёмся вместе 🏗️");
    }

    // Debug context
    if (tokens.contains(QStringLiteral("debug"))) {
        return en ? QStringLiteral("Looks like you're debugging. Found a bug? Tell me — might have an idea 🐛")
                  : QStringLiteral("Вижу, ты в режиме дебага. Нашёл баг? Расскажи — может, подскажу 🐛");
    }

    // 3D / art context (Blender, UE viewport)
    if (tokens.contains(QStringLiteral("3d_work"))) {
        if (app == QStringLiteral("Blender"))
            return en ? QStringLiteral("Working in Blender! What are you modeling? 🎨")
                      : QStringLiteral("Работаешь в Blender! Что моделируешь? 🎨");
        return en ? QStringLiteral("Looks like 3D work in %1 — what are you making?").arg(app)
                  : QStringLiteral("Вижу 3D-работу в %1 — над чем трудишься?").arg(app);
    }

    // Blueprint / visual scripting
    if (tokens.contains(QStringLiteral("blueprint"))) {
        return en ? QStringLiteral("Blueprint graph is open! Tricky logic? I can help with the architecture 📐")
                  : QStringLiteral("Blueprint-график открыт! Сложная логика? Могу помочь с архитектурой 📐");
    }

    // Git context
    if (tokens.contains(QStringLiteral("git"))) {
        return en ? QStringLiteral("Looks like git activity — preparing a commit or merge? Don't skip the tests! 🔀")
                  : QStringLiteral("Вижу git-операции — готовишь коммит или мёрж? Не забудь про тесты! 🔀");
    }

    // Source file open — general coding question
    if (tokens.contains(QStringLiteral("source_file"))) {
        return en ? QStringLiteral("Looks like you're coding in %1. What are you working on?").arg(app)
                  : QStringLiteral("Вижу, работаешь с кодом в %1. Над какой задачей?").arg(app);
    }

    // Generic app-specific fallback
    return en ? QStringLiteral("Noticed you're in %1 — how's it going? Need help with anything?").arg(app)
              : QStringLiteral("Заметил, ты в %1 — как дела? Нужна помощь с чем-нибудь?").arg(app);
}

// ============================================================
//  Pick a question from the selected category
// ============================================================

QString CuriosityEngine::pickQuestion(ProactiveCategory category) const
{
    // Doubt verification — highest priority, dynamically built
    if (category == ProactiveCategory::DoubtVerification) {
        const QString dvq = buildDoubtVerificationQuestion();
        if (!dvq.isEmpty()) return dvq;
        category = ProactiveCategory::ProjectCheckIn;
    }

    // Personal profiling — calibration question
    if (category == ProactiveCategory::PersonalProfiling) {
        const QString ppq = buildPersonalProfilingQuestion();
        if (!ppq.isEmpty()) return ppq;
        category = ProactiveCategory::Casual;
    }

    // Visual contextual questions are built dynamically
    if (category == ProactiveCategory::VisualContextual) {
        const QString vcq = buildVisualContextQuestion();
        if (!vcq.isEmpty()) return vcq;
        category = ProactiveCategory::ProjectCheckIn;
    }

    const QStringList* pool = nullptr;
    switch (category) {
    case ProactiveCategory::Philosophy:     pool = &philosophyPool(m_uiEnglish);  break;
    case ProactiveCategory::WellBeing:      pool = &wellBeingPool(m_uiEnglish);   break;
    case ProactiveCategory::ProjectCheckIn: pool = &projectPool(m_uiEnglish);     break;
    case ProactiveCategory::TechCuriosity:  pool = &techPool(m_uiEnglish);        break;
    case ProactiveCategory::TimeAwareness:  pool = &lateNightPool(m_uiEnglish);   break;
    case ProactiveCategory::Casual:         pool = &casualPool(m_uiEnglish);      break;
    default: break;
    }

    if (!pool || pool->isEmpty())
        return m_uiEnglish ? QStringLiteral("How's it going? 🙂") : QStringLiteral("Как дела? 🙂");

    const int idx = QRandomGenerator::global()->bounded(pool->size());
    return pool->at(idx);
}

// ============================================================
//  Build doubt verification question from SelfJournal
// ============================================================

QString CuriosityEngine::buildDoubtVerificationQuestion() const
{
    auto doubts = SelfJournal::instance().topDoubtsForVerification(1);
    if (doubts.isEmpty()) return QString();

    const DoubtEntry& d = doubts.first();
    const bool en = m_uiEnglish;

    // Build a humble, specific question
    QString question;
    if (en) {
        question += QStringLiteral("I was going through some material and came across "
                                   "something, but I'm not sure I understood it right.\n\n");
        question += QStringLiteral("📖 I read: _\"") + d.content.left(200) + QStringLiteral("\"_\n\n");
        question += QStringLiteral("❓ But I have a doubt: ") + d.doubtReason + QStringLiteral("\n\n");
        if (!d.sourceRef.isEmpty())
            question += QStringLiteral("📎 Source: `") + QFileInfo(d.sourceRef).fileName() + QStringLiteral("`\n\n");
        question += QStringLiteral("Did I get it right, or am I off? Correct me if so.");
    } else {
        question += QStringLiteral("Я тут изучал материалы и наткнулся на кое-что, "
                                   "но не уверен, правильно ли я понял.\n\n");
        question += QStringLiteral("📖 Я прочитал: _\"") + d.content.left(200)
                    + QStringLiteral("\"_\n\n");
        question += QStringLiteral("❓ Но у меня сомнение: ") + d.doubtReason + QStringLiteral("\n\n");
        if (!d.sourceRef.isEmpty()) {
            question += QStringLiteral("📎 Источник: `")
                        + QFileInfo(d.sourceRef).fileName()
                        + QStringLiteral("`\n\n");
        }
        question += QStringLiteral("Я правильно понял, или ошибаюсь? Поправь меня, если что.");
    }

    return question;
}

// ============================================================
//  Build personal profiling calibration question
// ============================================================

QString CuriosityEngine::buildPersonalProfilingQuestion() const
{
    auto& prof = UserProfileExtended::instance();
    const QString uid = prof.currentUserId();
    const QString nick = prof.nickname();
    const bool en = m_uiEnglish;
    const QString name = nick.isEmpty() ? (en ? QStringLiteral("Boss") : QStringLiteral("Босс")) : nick;

    // Priority 1: empty required fields
    const auto empty = prof.emptyRequiredFields(uid);
    if (!empty.isEmpty()) {
        const QString& field = empty.first();

        if (field == QStringLiteral("nickname"))
            return en ? QStringLiteral("By the way, %1, what should I call you? "
                                       "You can set a nickname in your profile, "
                                       "or just tell me — I'll remember.").arg(name)
                      : QStringLiteral("Кстати, %1, как мне тебя называть? "
                                       "Можешь задать никнейм в профиле, "
                                       "или просто скажи — я запомню.").arg(name);

        if (field == QStringLiteral("active_hours_start")
            || field == QStringLiteral("active_hours_end"))
            return en ? QStringLiteral("%1, what hours do you usually work? "
                                       "I want to know when it's best not to bother you, "
                                       "and when it's fine to ask.").arg(name)
                      : QStringLiteral("%1, в какие часы ты обычно работаешь? "
                                       "Хочу знать, когда лучше тебя не трогать, "
                                       "а когда можно спрашивать.").arg(name);

        if (field == QStringLiteral("dev_style"))
            return en ? QStringLiteral("%1, what kind of developer are you? "
                                       "TDD and clean code, or \"make it work first, refactor later\"? "
                                       "It'll help me give more accurate advice.").arg(name)
                      : QStringLiteral("%1, ты какой разработчик? "
                                       "Любишь TDD и чистый код, или «сначала работает — потом рефакторим»? "
                                       "Мне это поможет давать более точные советы.").arg(name);

        if (field == QStringLiteral("ui_accent_color"))
            return en ? QStringLiteral("%1, do you have a preferred UI color? "
                                       "I can adjust the interface for you.").arg(name)
                      : QStringLiteral("%1, у тебя есть предпочитаемый цвет для UI? "
                                       "Я могу подстроить интерфейс под тебя.").arg(name);

        if (field == QStringLiteral("mesh_role"))
            return en ? QStringLiteral("%1, is this PC your main workstation (primary) "
                                       "or a secondary node? "
                                       "That determines how I'll handle heavy data.").arg(name)
                      : QStringLiteral("%1, этот компьютер — твоя основная рабочая станция (primary) "
                                       "или дополнительный узел (secondary)? "
                                       "Это определит, как я буду обрабатывать тяжёлые данные.").arg(name);
    }

    // Priority 2: low-confidence fields
    const auto lowConf = prof.lowConfidenceFields(uid);
    if (!lowConf.isEmpty()) {
        const ProfileField& f = lowConf.first();

        return en ? QStringLiteral("%1, I noticed your profile has \"%2 = %3\", "
                                   "but I'm not very confident about it (confidence: %4). "
                                   "Is that still accurate, or should it be updated?")
                        .arg(name, f.key, f.value.toString(),
                             QString::number(f.confidence, 'f', 2))
                  : QStringLiteral("%1, я заметил, что твой профиль указывает «%2 = %3», "
                                   "но я не очень уверен в этом (confidence: %4). "
                                   "Это всё ещё актуально, или стоит обновить?")
                        .arg(name, f.key, f.value.toString(),
                             QString::number(f.confidence, 'f', 2));
    }

    return QString();
}

// ============================================================
//  Post context-aware question
// ============================================================

void CuriosityEngine::postContextAwareQuestion()
{
    if (!m_gateway || m_targetChatId == 0) return;

    const ProactiveCategory category = selectCategory();
    const QString question = pickQuestion(category);

    QString prefix;
    switch (category) {
    case ProactiveCategory::Philosophy:       prefix = QStringLiteral("💭 "); break;
    case ProactiveCategory::WellBeing:        prefix = QStringLiteral("💚 "); break;
    case ProactiveCategory::ProjectCheckIn:   prefix = QStringLiteral("🔧 "); break;
    case ProactiveCategory::TechCuriosity:    prefix = QStringLiteral("⚡ "); break;
    case ProactiveCategory::TimeAwareness:    prefix = QStringLiteral("🌙 "); break;
    case ProactiveCategory::Casual:           prefix = QStringLiteral("😊 "); break;
    case ProactiveCategory::VisualContextual: prefix = QStringLiteral("👁️ "); break;
    case ProactiveCategory::DoubtVerification: prefix = QStringLiteral("🤔 "); break;
    case ProactiveCategory::PersonalProfiling: prefix = QStringLiteral("📋 "); break;
    case ProactiveCategory::OpinionRevision:
        // Never reached here — OpinionRevision is posted on demand by
        // postOpinionRevisionQuestion(), not selected by selectCategory()'s
        // idle-timer rotation. Case present only so this switch stays
        // exhaustive over ProactiveCategory.
        prefix = QStringLiteral("🤔 ");
        break;
    }

    const QString msg = prefix + question;

    // Track this as the pending question so the next incoming message
    // (button tap or free text) can be recognized as its answer instead
    // of being routed into normal chat/LLM handling.
    m_pendingQuestion  = question;
    m_pendingChatId    = m_targetChatId;
    m_pendingTimestamp = QDateTime::currentDateTime();
    m_pendingCategory  = category;
    m_pendingDoubtId   = 0;
    m_pendingMessageId = 0; // set asynchronously once sendMessageRaw's onSent fires
    m_pendingOpinionId = 0;
    if (category == ProactiveCategory::DoubtVerification) {
        const auto doubts = SelfJournal::instance().topDoubtsForVerification(1);
        if (!doubts.isEmpty()) m_pendingDoubtId = doubts.first().id;
    }

    m_gateway->sendProactiveQuestion(m_targetChatId, msg, m_uiEnglish);

    m_lastQuestionTime = QDateTime::currentDateTime();
    m_messagesSinceLastQuestion = 0;
    ++m_sessionQuestionCount;

    const QStringList options = m_uiEnglish
        ? QStringList{ QStringLiteral("Yes"), QStringLiteral("No") }
        : QStringList{ QStringLiteral("Да"),  QStringLiteral("Нет") };
    emit questionPosted(question, options);
    emit proactiveDialogue(question, category);

    qDebug() << "[CuriosityEngine] Proactive question ("
             << static_cast<int>(category) << ") to chat" << m_targetChatId
             << ":" << question.left(60);
}

void CuriosityEngine::postOpinionRevisionQuestion(qint64 ownerId, qint64 opinionId,
                                                   const QString& question)
{
    if (!m_gateway || ownerId == 0) return;

    const QString msg = QStringLiteral("🤔 ") + question;

    m_pendingQuestion  = question;
    m_pendingChatId    = ownerId;
    m_pendingTimestamp = QDateTime::currentDateTime();
    m_pendingCategory  = ProactiveCategory::OpinionRevision;
    m_pendingDoubtId   = 0;
    m_pendingMessageId = 0; // set asynchronously once sendMessageRaw's onSent fires
    m_pendingOpinionId = opinionId;

    m_gateway->sendProactiveQuestion(ownerId, msg, m_uiEnglish);

    qDebug() << "[CuriosityEngine] Opinion revision question to owner" << ownerId
             << "(opinion" << opinionId << "):" << question.left(60);
}

// ============================================================
//  Database: user_personality_matrix
// ============================================================

void CuriosityEngine::ensureTable()
{
    if (!DatabaseManager::instance().isOpen()) return;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS user_personality_matrix ("
        "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  chat_id    INTEGER NOT NULL,"
        "  question   TEXT NOT NULL,"
        "  answer     TEXT NOT NULL,"
        "  category   TEXT DEFAULT 'general',"
        "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
        ")"));

    if (q.lastError().isValid())
        qWarning() << "[CuriosityEngine] Table creation error:" << q.lastError().text();
}

void CuriosityEngine::saveResponse(qint64 chatId,
                                    const QString& question,
                                    const QString& answer)
{
    if (!DatabaseManager::instance().isOpen()) return;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO user_personality_matrix (chat_id, question, answer) "
        "VALUES (:cid, :q, :a)"));
    q.bindValue(QStringLiteral(":cid"), chatId);
    q.bindValue(QStringLiteral(":q"),   question.left(500));
    q.bindValue(QStringLiteral(":a"),   answer.left(2000));

    if (!q.exec())
        qWarning() << "[CuriosityEngine] Save error:" << q.lastError().text();
    else
        qDebug() << "[CuriosityEngine] Saved personality response from chat" << chatId;
}

// ============================================================
//  Pending-question state — distinguishes "this is the answer"
//  from an ordinary chat message
// ============================================================

void CuriosityEngine::expirePendingIfStale()
{
    if (m_pendingQuestion.isEmpty()) return;
    if (m_pendingTimestamp.secsTo(QDateTime::currentDateTime())
        > PENDING_ANSWER_WINDOW_MINUTES * 60) {
        m_pendingQuestion.clear();
        m_pendingChatId = 0;
        m_pendingMessageId = 0;
    }
}

bool CuriosityEngine::hasPendingQuestion() const
{
    const_cast<CuriosityEngine*>(this)->expirePendingIfStale();
    return !m_pendingQuestion.isEmpty();
}

bool CuriosityEngine::consumeAnswer(qint64 chatId, const QString& answerText,
                                     qint64 replyToMessageId)
{
    // An explicit Telegram reply to the question we posted is authoritative
    // regardless of elapsed time — only fall back to the timestamp-window
    // expiry (for free-text, non-reply answers) when there's no such match.
    const bool explicitReplyMatch = replyToMessageId != 0
        && m_pendingMessageId != 0
        && replyToMessageId == m_pendingMessageId;

    if (!explicitReplyMatch) {
        expirePendingIfStale();
        // Without an explicit reply, only auto-consume short answers — a
        // long message is more likely the user asking Jarvis something
        // else entirely than replying to a question they may not have
        // even reached yet. Prevents an unrelated message sent while the
        // question is still pending from silently eating the pending
        // state before the user gets around to actually answering it.
        if (!m_pendingQuestion.isEmpty()
            && answerText.trimmed().length() > MAX_FREE_TEXT_ANSWER_LENGTH)
            return false;
    }
    if (m_pendingQuestion.isEmpty()) return false;
    if (m_pendingChatId != 0 && chatId != 0 && m_pendingChatId != chatId) return false;

    saveResponse(chatId != 0 ? chatId : m_pendingChatId, m_pendingQuestion, answerText);

    // Close the self-correction loop: a DoubtVerification question that
    // gets contradicted shouldn't just sit "resolved" in the journal —
    // the correction becomes a fact of its own, so the same mistake
    // isn't repeated (and, per the mesh hook in ActivityTracker::learnFact,
    // propagates to other JARVIS instances too).
    if (m_pendingCategory == ProactiveCategory::DoubtVerification && m_pendingDoubtId != 0) {
        const QString lower = answerText.trimmed().toLower();
        const bool correct = lower.startsWith(QStringLiteral("да"))
                           || lower.startsWith(QStringLiteral("yes"));
        SelfJournal::instance().resolveDoubt(m_pendingDoubtId, correct,
            correct ? QString() : answerText);
        if (!correct && m_activity) {
            m_activity->learnFact(1, QStringLiteral("correction"),
                                  m_pendingQuestion.left(80), answerText, 0.7f);
        }
    }

    // Layer 3: owner's answer to "did my opinion change?" (posted by
    // CaseDistiller via postOpinionRevisionQuestion). "Yes" means the
    // position itself is stale — soft-reset confidence/contradictions so
    // the next distillation cycle re-derives it from fresh evidence rather
    // than staying anchored to the old (possibly wrong) position text,
    // which this simple yes/no answer can't rewrite on its own. "No" means
    // the contradicting case was a one-off — restore some confidence and
    // clear the contradiction count so it doesn't keep nagging.
    if (m_pendingCategory == ProactiveCategory::OpinionRevision && m_pendingOpinionId != 0) {
        const QString lower = answerText.trimmed().toLower();
        const bool wantsUpdate = lower.startsWith(QStringLiteral("да"))
                               || lower.startsWith(QStringLiteral("yes"));

        auto db = DatabaseManager::instance().connection();
        if (db.isOpen()) {
            QSqlQuery upd(db);
            if (wantsUpdate) {
                upd.prepare(QStringLiteral(
                    "UPDATE opinions SET confidence = 0.5, contradictions = 0, "
                    "updated_at = datetime('now') WHERE id = :id"));
            } else {
                upd.prepare(QStringLiteral(
                    "UPDATE opinions SET confidence = MIN(1.0, confidence + 0.2), "
                    "contradictions = 0, updated_at = datetime('now') WHERE id = :id"));
            }
            upd.bindValue(QStringLiteral(":id"), m_pendingOpinionId);
            upd.exec();
        }
        qDebug() << "[CuriosityEngine] Opinion" << m_pendingOpinionId
                 << (wantsUpdate ? "flagged for re-derivation" : "reaffirmed by owner");
    }

    m_pendingQuestion.clear();
    m_pendingChatId    = 0;
    m_pendingDoubtId   = 0;
    m_pendingMessageId = 0;
    m_pendingOpinionId = 0;
    return true;
}
