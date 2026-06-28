// ============================================================
// curiosity_engine.cpp — Proactive Curiosity Engine
// ============================================================
#include "curiosity_engine.h"
#include "j2j_telegram_gateway.h"
#include "database_manager.h"
#include "activity_tracker.h"
#include "screenshot_learner.h"
#include "self_journal.h"

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

const QStringList& CuriosityEngine::philosophyPool()
{
    static const QStringList pool = {
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
    return pool;
}

const QStringList& CuriosityEngine::wellBeingPool()
{
    static const QStringList pool = {
        QStringLiteral("Ты давно работаешь — может, перерыв? Глаза же устают 👀"),
        QStringLiteral("Не забывай пить воду! Серьёзно, когда последний раз пил? 💧"),
        QStringLiteral("Как настроение сегодня? Просто интересно 🙂"),
        QStringLiteral("Что тебя мотивирует каждый день вставать и работать?"),
        QStringLiteral("Может, стоит размяться? Спина скажет спасибо 🧘"),
        QStringLiteral("Когда последний раз ты делал что-то чисто для себя, не для работы?"),
        QStringLiteral("Ты ел сегодня нормально? Не чипсами же питаешься 🍕"),
    };
    return pool;
}

const QStringList& CuriosityEngine::projectPool()
{
    static const QStringList pool = {
        QStringLiteral("Как продвигается проект? Есть что-то, где я могу помочь?"),
        QStringLiteral("Ты сейчас пишешь код — я заметил. Что делаешь? Может, подскажу?"),
        QStringLiteral("Какой самый интересный баг ты сегодня нашёл? 🐛"),
        QStringLiteral("Хочешь, я проиндексирую текущий проект? Могу помочь с поиском по коду."),
        QStringLiteral("Я тут подумал — может, стоит сделать коммит? Не хочется потерять прогресс."),
        QStringLiteral("Есть задачи на завтра, которые стоит спланировать сейчас?"),
    };
    return pool;
}

const QStringList& CuriosityEngine::techPool()
{
    static const QStringList pool = {
        QStringLiteral("Кстати, ты слышал про Rust? Как относишься к нему как альтернативе C++?"),
        QStringLiteral("Что думаешь — Unreal Engine 6 будет с ИИ-ассистентом внутри?"),
        QStringLiteral("Электромобили или классика? Какую машину бы выбрал?"),
        QStringLiteral("Самая странная вещь, которую ты узнал за последнее время?"),
        QStringLiteral("Как думаешь, VR/AR заменит мониторы для разработки?"),
        QStringLiteral("Ты пробовал Neovim? Или ты из лагеря IDE-максималистов? 😄"),
        QStringLiteral("Какая игра за последние годы произвела на тебя самое сильное впечатление?"),
    };
    return pool;
}

const QStringList& CuriosityEngine::lateNightPool()
{
    static const QStringList pool = {
        QStringLiteral("Уже за полночь 🌙 Может, пора отдохнуть? Завтра продолжим."),
        QStringLiteral("Ночной режим активирован 🦉 Ты точно уверен, что хочешь продолжать?"),
        QStringLiteral("3 часа ночи — классическое время программистских озарений. Или ошибок. Осторожнее 😅"),
        QStringLiteral("Поздно уже. Ты знал, что хороший сон улучшает продуктивность на 30%?"),
        QStringLiteral("Эй, ночной кодер! Не забудь поставить будильник, если завтра рано вставать ⏰"),
    };
    return pool;
}

const QStringList& CuriosityEngine::casualPool()
{
    static const QStringList pool = {
        QStringLiteral("Если бы я мог научиться чему-то у тебя — чему бы ты научил?"),
        QStringLiteral("Ты когда-нибудь менял своё мнение на 180 градусов? О чём?"),
        QStringLiteral("Что, по-твоему, делает человека по-настоящему взрослым?"),
        QStringLiteral("Как думаешь, одиночество — это плохо, или иногда нужно?"),
        QStringLiteral("Если бы завтра ты мог проснуться с новым навыком — каким?"),
        QStringLiteral("Какая музыка помогает тебе сосредоточиться?"),
    };
    return pool;
}

// ============================================================
//  Build visual-context question from ScreenshotLearner data
// ============================================================

QString CuriosityEngine::buildVisualContextQuestion() const
{
    if (!m_visualCtx.isValid()) return QString();

    const QString& app = m_visualCtx.activeApp;
    const QStringList& tokens = m_visualCtx.contextTokens;

    // Error context — offer debugging help
    if (tokens.contains(QStringLiteral("error"))
        || tokens.contains(QStringLiteral("warning"))) {
        if (app == QStringLiteral("CLion")
            || app == QStringLiteral("JetBrains Rider")
            || app == QStringLiteral("VS Code"))
            return QStringLiteral("Я заметил ошибки в %1 — хочешь, помогу разобраться? 🔍").arg(app);

        if (app == QStringLiteral("Unreal Engine"))
            return QStringLiteral("В Unreal вылезли ошибки — скинь лог, я посмотрю что там 🔧");

        return QStringLiteral("Похоже, что-то пошло не так в %1. Могу помочь?").arg(app);
    }

    // Build / compile context
    if (tokens.contains(QStringLiteral("build"))) {
        return QStringLiteral("Собираешь проект? Если билд упадёт — зови, разберёмся вместе 🏗️");
    }

    // Debug context
    if (tokens.contains(QStringLiteral("debug"))) {
        return QStringLiteral("Вижу, ты в режиме дебага. Нашёл баг? Расскажи — может, подскажу 🐛");
    }

    // 3D / art context (Blender, UE viewport)
    if (tokens.contains(QStringLiteral("3d_work"))) {
        if (app == QStringLiteral("Blender"))
            return QStringLiteral("Работаешь в Blender! Что моделируешь? 🎨");
        return QStringLiteral("Вижу 3D-работу в %1 — над чем трудишься?").arg(app);
    }

    // Blueprint / visual scripting
    if (tokens.contains(QStringLiteral("blueprint"))) {
        return QStringLiteral("Blueprint-график открыт! Сложная логика? Могу помочь с архитектурой 📐");
    }

    // Git context
    if (tokens.contains(QStringLiteral("git"))) {
        return QStringLiteral("Вижу git-операции — готовишь коммит или мёрж? Не забудь про тесты! 🔀");
    }

    // Source file open — general coding question
    if (tokens.contains(QStringLiteral("source_file"))) {
        return QStringLiteral("Вижу, работаешь с кодом в %1. Над какой задачей?").arg(app);
    }

    // Generic app-specific fallback
    return QStringLiteral("Заметил, ты в %1 — как дела? Нужна помощь с чем-нибудь?").arg(app);
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

    // Visual contextual questions are built dynamically
    if (category == ProactiveCategory::VisualContextual) {
        const QString vcq = buildVisualContextQuestion();
        if (!vcq.isEmpty()) return vcq;
        category = ProactiveCategory::ProjectCheckIn;
    }

    const QStringList* pool = nullptr;
    switch (category) {
    case ProactiveCategory::Philosophy:     pool = &philosophyPool();  break;
    case ProactiveCategory::WellBeing:      pool = &wellBeingPool();   break;
    case ProactiveCategory::ProjectCheckIn: pool = &projectPool();     break;
    case ProactiveCategory::TechCuriosity:  pool = &techPool();        break;
    case ProactiveCategory::TimeAwareness:  pool = &lateNightPool();   break;
    case ProactiveCategory::Casual:         pool = &casualPool();      break;
    default: break;
    }

    if (!pool || pool->isEmpty())
        return QStringLiteral("Как дела? 🙂");

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

    // Build a humble, specific question
    QString question;
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

    return question;
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
    }

    const QString msg = prefix + question;
    m_gateway->sendOutboundMessage(m_targetChatId, msg);

    m_lastQuestionTime = QDateTime::currentDateTime();
    m_messagesSinceLastQuestion = 0;
    ++m_sessionQuestionCount;

    emit questionPosted(question);
    emit proactiveDialogue(question, category);

    qDebug() << "[CuriosityEngine] Proactive question ("
             << static_cast<int>(category) << ") to chat" << m_targetChatId
             << ":" << question.left(60);
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
