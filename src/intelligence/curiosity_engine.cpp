// ============================================================
// curiosity_engine.cpp — Proactive Curiosity Engine
// ============================================================
#include "curiosity_engine.h"
#include "j2j_telegram_gateway.h"
#include "database_manager.h"
#include "activity_tracker.h"

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QRandomGenerator>
#include <QTime>
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

    if (hour >= 0 && hour < 5)
        return ProactiveCategory::TimeAwareness;

    if (hour >= 23 || hour == 0)
        return ProactiveCategory::WellBeing;

    if (m_activity) {
        const QString context = m_activity->currentCategory();
        if (context.contains(QStringLiteral("Development")) ||
            context.contains(QStringLiteral("IDE")))
            return ProactiveCategory::ProjectCheckIn;
    }

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
//  Pick a question from the selected category
// ============================================================

QString CuriosityEngine::pickQuestion(ProactiveCategory category) const
{
    const QStringList* pool = nullptr;
    switch (category) {
    case ProactiveCategory::Philosophy:    pool = &philosophyPool();  break;
    case ProactiveCategory::WellBeing:     pool = &wellBeingPool();   break;
    case ProactiveCategory::ProjectCheckIn: pool = &projectPool();    break;
    case ProactiveCategory::TechCuriosity: pool = &techPool();        break;
    case ProactiveCategory::TimeAwareness: pool = &lateNightPool();   break;
    case ProactiveCategory::Casual:        pool = &casualPool();      break;
    }

    if (!pool || pool->isEmpty())
        return QStringLiteral("Как дела? 🙂");

    const int idx = QRandomGenerator::global()->bounded(pool->size());
    return pool->at(idx);
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
    case ProactiveCategory::Philosophy:     prefix = QStringLiteral("💭 "); break;
    case ProactiveCategory::WellBeing:      prefix = QStringLiteral("💚 "); break;
    case ProactiveCategory::ProjectCheckIn: prefix = QStringLiteral("🔧 "); break;
    case ProactiveCategory::TechCuriosity:  prefix = QStringLiteral("⚡ "); break;
    case ProactiveCategory::TimeAwareness:  prefix = QStringLiteral("🌙 "); break;
    case ProactiveCategory::Casual:         prefix = QStringLiteral("😊 "); break;
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
