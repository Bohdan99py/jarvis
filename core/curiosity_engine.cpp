// ============================================================
// curiosity_engine.cpp — Core Curiosity Engine
// ============================================================
#include "curiosity_engine.h"
#include "j2j_telegram_gateway.h"
#include "database_manager.h"

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QRandomGenerator>
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
{
    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(false);
    connect(m_idleTimer, &QTimer::timeout, this, &CuriosityEngine::postRandomQuestion);
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

// ============================================================
//  Question pool
// ============================================================

const QStringList& CuriosityEngine::questionPool()
{
    static const QStringList pool = {
        QStringLiteral("Слушай, а как ты считаешь — искусственный интеллект это хорошо или плохо?"),
        QStringLiteral("Если бы ты мог выбрать одну суперспособность — какую бы выбрал?"),
        QStringLiteral("Как думаешь, через 50 лет люди будут счастливее, чем сейчас?"),
        QStringLiteral("Ты когда-нибудь задумывался, почему одни люди добрые, а другие нет?"),
        QStringLiteral("Что для тебя важнее — свобода или безопасность?"),
        QStringLiteral("Если бы можно было прожить один день из прошлого заново — какой бы ты выбрал?"),
        QStringLiteral("Как ты считаешь, можно ли доверять машине принимать решения за людей?"),
        QStringLiteral("Что тебя мотивирует каждый день вставать и работать?"),
        QStringLiteral("Самая странная вещь, которую ты узнал за последнее время?"),
        QStringLiteral("Если бы ты писал правила жизни — какое было бы первое правило?"),
        QStringLiteral("Ты больше логик или интуит? Почему?"),
        QStringLiteral("Как думаешь, одиночество — это плохо, или иногда нужно?"),
        QStringLiteral("Что, по-твоему, делает человека по-настоящему взрослым?"),
        QStringLiteral("Если бы я мог научиться чему-то у тебя — чему бы ты научил?"),
        QStringLiteral("Ты когда-нибудь менял своё мнение на 180 градусов? О чём?"),
        QStringLiteral("Как ты относишься к идее, что мы живём в симуляции?"),
    };
    return pool;
}

QString CuriosityEngine::pickQuestion()
{
    const auto& pool = questionPool();
    m_questionIndex = QRandomGenerator::global()->bounded(pool.size());
    return pool[m_questionIndex];
}

// ============================================================
//  Post question to Telegram
// ============================================================

void CuriosityEngine::postRandomQuestion()
{
    if (!m_gateway || m_targetChatId == 0) return;

    const QString question = pickQuestion();
    const QString msg = QStringLiteral("💭 ") + question;

    m_gateway->sendMessage(m_targetChatId, msg);

    emit questionPosted(question);
    qDebug() << "[CuriosityEngine] Posted question to chat" << m_targetChatId
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
