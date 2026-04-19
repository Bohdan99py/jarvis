// -------------------------------------------------------
// session_memory.cpp — Контекстная память J.A.R.V.I.S.
// -------------------------------------------------------

#include "session_memory.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

// ============================================================
// ChatMessage
// ============================================================

QJsonObject ChatMessage::toJson() const
{
    return QJsonObject{
        {QStringLiteral("role"),      role},
        {QStringLiteral("content"),   content},
        {QStringLiteral("timestamp"), timestamp.toString(Qt::ISODate)}
    };
}

ChatMessage ChatMessage::fromJson(const QJsonObject& obj)
{
    ChatMessage msg;
    msg.role      = obj[QStringLiteral("role")].toString();
    msg.content   = obj[QStringLiteral("content")].toString();
    msg.timestamp = QDateTime::fromString(
        obj[QStringLiteral("timestamp")].toString(), Qt::ISODate);
    return msg;
}

// ============================================================
// TaskContext
// ============================================================

QJsonObject TaskContext::toJson() const
{
    return QJsonObject{
        {QStringLiteral("currentTask"),    currentTask},
        {QStringLiteral("lastTopic"),      lastTopic},
        {QStringLiteral("recentApps"),     QJsonArray::fromStringList(recentApps)},
        {QStringLiteral("recentSearches"), QJsonArray::fromStringList(recentSearches)},
        {QStringLiteral("commandCount"),   commandCount}
    };
}

TaskContext TaskContext::fromJson(const QJsonObject& obj)
{
    TaskContext ctx;
    ctx.currentTask = obj[QStringLiteral("currentTask")].toString();
    ctx.lastTopic   = obj[QStringLiteral("lastTopic")].toString();
    ctx.commandCount = obj[QStringLiteral("commandCount")].toInt();

    for (const auto& v : obj[QStringLiteral("recentApps")].toArray())
        ctx.recentApps.append(v.toString());
    for (const auto& v : obj[QStringLiteral("recentSearches")].toArray())
        ctx.recentSearches.append(v.toString());

    return ctx;
}

void TaskContext::clear()
{
    currentTask.clear();
    lastTopic.clear();
    recentApps.clear();
    recentSearches.clear();
    commandCount = 0;
}

// ============================================================
// SessionMemory
// ============================================================

SessionMemory::SessionMemory(QObject* parent)
    : QObject(parent)
{
    loadPersistent();
}

SessionMemory::~SessionMemory()
{
    // Сохраняем summary текущей сессии перед завершением
    if (!m_sessionMessages.isEmpty()) {
        QJsonObject sessionSummaryObj;
        sessionSummaryObj[QStringLiteral("date")] =
            QDateTime::currentDateTime().toString(Qt::ISODate);
        sessionSummaryObj[QStringLiteral("commandCount")] = m_taskContext.commandCount;
        sessionSummaryObj[QStringLiteral("lastTopic")]    = m_taskContext.lastTopic;

        // Краткое summary: первые и последние сообщения
        QString summary;
        int count = m_sessionMessages.size();
        int show = qMin(count, 4);
        for (int i = 0; i < show; ++i) {
            const auto& msg = m_sessionMessages[i];
            summary += msg.role + QStringLiteral(": ")
                     + msg.content.left(80) + QStringLiteral("\n");
        }
        if (count > 4) {
            summary += QStringLiteral("... (") + QString::number(count - 4)
                     + QStringLiteral(" ещё сообщений)\n");
            const auto& last = m_sessionMessages.last();
            summary += last.role + QStringLiteral(": ") + last.content.left(80);
        }
        sessionSummaryObj[QStringLiteral("summary")] = summary;

        m_pastSessions.append(sessionSummaryObj);

        // Ограничить количество сохранённых сессий
        while (m_pastSessions.size() > MAX_PAST_SESSIONS) {
            m_pastSessions.removeFirst();
        }

        savePersistent();
    }
}

// ============================================================
// Сессия: сообщения
// ============================================================

void SessionMemory::addMessage(const QString& role, const QString& content)
{
    ChatMessage msg;
    msg.role      = role;
    msg.content   = content;
    msg.timestamp = QDateTime::currentDateTime();
    m_sessionMessages.append(msg);

    // Ротация: при превышении лимита удаляем старые
    while (m_sessionMessages.size() > MAX_SESSION_MESSAGES) {
        m_sessionMessages.removeFirst();
    }
}

QJsonArray SessionMemory::recentMessagesAsJson(int maxMessages) const
{
    QJsonArray arr;
    int start = qMax(0, m_sessionMessages.size() - maxMessages);
    for (int i = start; i < m_sessionMessages.size(); ++i) {
        const auto& msg = m_sessionMessages[i];
        QJsonObject obj;
        obj[QStringLiteral("role")]    = msg.role;
        obj[QStringLiteral("content")] = msg.content;
        arr.append(obj);
    }
    return arr;
}

QString SessionMemory::sessionSummary() const
{
    QString summary;
    int start = qMax(0, m_sessionMessages.size() - 10);
    for (int i = start; i < m_sessionMessages.size(); ++i) {
        const auto& msg = m_sessionMessages[i];
        summary += msg.role + QStringLiteral(": ") + msg.content + QStringLiteral("\n");
    }
    return summary;
}

// ============================================================
// Обновление контекста
// ============================================================

void SessionMemory::updateContext(const QString& userInput, const QString& response)
{
    Q_UNUSED(response)

    m_taskContext.commandCount++;

    QString lower = userInput.toLower();

    // Определяем тему
    if (lower.contains(QStringLiteral("найди")) || lower.contains(QStringLiteral("search"))
        || lower.contains(QStringLiteral("гугл"))) {
        // Извлекаем поисковый запрос
        QString query = userInput;
        for (const auto& prefix : {QStringLiteral("найди "), QStringLiteral("search "),
                                    QStringLiteral("гугл ")}) {
            if (lower.startsWith(prefix)) {
                query = userInput.mid(prefix.length()).trimmed();
                break;
            }
        }
        m_taskContext.lastTopic = query;
        m_taskContext.recentSearches.prepend(query);
        if (m_taskContext.recentSearches.size() > 10)
            m_taskContext.recentSearches.removeLast();
    }

    // Отслеживание запущенных приложений
    if (lower.contains(QStringLiteral("запусти")) || lower.contains(QStringLiteral("открой"))
        || lower.contains(QStringLiteral("launch"))) {
        QString app = userInput;
        for (const auto& prefix : {QStringLiteral("запусти "), QStringLiteral("открой "),
                                    QStringLiteral("launch ")}) {
            if (lower.startsWith(prefix)) {
                app = userInput.mid(prefix.length()).trimmed();
                break;
            }
        }
        m_taskContext.recentApps.prepend(app);
        if (m_taskContext.recentApps.size() > 10)
            m_taskContext.recentApps.removeLast();
        m_taskContext.currentTask = QStringLiteral("Работа с ") + app;
    }

    // Статистика команд
    recordCommandUsage(lower.split(QChar(' ')).first());

    emit memoryUpdated();
}

// ============================================================
// Постоянная память
// ============================================================

QString SessionMemory::persistentFilePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/jarvis_memory.json");
}

void SessionMemory::loadPersistent()
{
    QFile file(persistentFilePath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
    m_persistentFacts = root[QStringLiteral("facts")].toObject();
    m_commandStats    = root[QStringLiteral("commandStats")].toObject();
    m_pastSessions    = root[QStringLiteral("pastSessions")].toArray();
}

void SessionMemory::savePersistent()
{
    QJsonObject root;
    root[QStringLiteral("facts")]        = m_persistentFacts;
    root[QStringLiteral("commandStats")] = m_commandStats;
    root[QStringLiteral("pastSessions")] = m_pastSessions;
    root[QStringLiteral("savedAt")]      = QDateTime::currentDateTime().toString(Qt::ISODate);

    QFile file(persistentFilePath());
    if (!file.open(QIODevice::WriteOnly)) return;

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

void SessionMemory::rememberFact(const QString& key, const QString& value)
{
    m_persistentFacts[key] = value;
    savePersistent();
    emit memoryUpdated();
}

QString SessionMemory::recallFact(const QString& key) const
{
    return m_persistentFacts[key].toString();
}

void SessionMemory::recordCommandUsage(const QString& command)
{
    int count = m_commandStats[command].toInt(0);
    m_commandStats[command] = count + 1;
}

// ============================================================
// Системный промпт для Claude API
// ============================================================

QString SessionMemory::buildSystemPrompt() const
{
    QString prompt = QStringLiteral(
        "Ты — J.A.R.V.I.S., персональный ИИ-ассистент на Windows. "
        "Ты помогаешь пользователю с задачами на ПК. "
        "Отвечай кратко, по делу, на русском языке. "
        "Ты можешь выполнять системные команды: запускать приложения, искать в интернете, "
        "печатать текст в окнах, блокировать экран.\n\n"
    );

    // Добавляем факты о пользователе
    if (!m_persistentFacts.isEmpty()) {
        prompt += QStringLiteral("Известные факты о пользователе:\n");
        for (auto it = m_persistentFacts.begin(); it != m_persistentFacts.end(); ++it) {
            prompt += QStringLiteral("- ") + it.key() + QStringLiteral(": ")
                    + it.value().toString() + QStringLiteral("\n");
        }
        prompt += QStringLiteral("\n");
    }

    // Контекст текущей задачи
    if (!m_taskContext.currentTask.isEmpty()) {
        prompt += QStringLiteral("Текущая задача пользователя: ")
                + m_taskContext.currentTask + QStringLiteral("\n");
    }
    if (!m_taskContext.lastTopic.isEmpty()) {
        prompt += QStringLiteral("Последняя тема: ")
                + m_taskContext.lastTopic + QStringLiteral("\n");
    }
    if (!m_taskContext.recentApps.isEmpty()) {
        prompt += QStringLiteral("Недавние приложения: ")
                + m_taskContext.recentApps.join(QStringLiteral(", ")) + QStringLiteral("\n");
    }

    // Краткие сведения из прошлых сессий
    if (!m_pastSessions.isEmpty()) {
        prompt += QStringLiteral("\nИз прошлых сессий:\n");
        int show = qMin(m_pastSessions.size(), 3);
        for (int i = m_pastSessions.size() - show; i < m_pastSessions.size(); ++i) {
            QJsonObject s = m_pastSessions[i].toObject();
            prompt += QStringLiteral("- ") + s[QStringLiteral("date")].toString()
                    + QStringLiteral(": ") + s[QStringLiteral("lastTopic")].toString()
                    + QStringLiteral(" (") + QString::number(s[QStringLiteral("commandCount")].toInt())
                    + QStringLiteral(" команд)\n");
        }
    }

    prompt += QStringLiteral(
        "\nЕсли пользователь просит выполнить системную команду, "
        "ответь ТОЛЬКО командой в формате [CMD:команда]. "
        "Например: [CMD:запусти notepad.exe] или [CMD:найди погода москва]. "
        "Если вопрос разговорный — просто ответь текстом.\n"
    );

    return prompt;
}