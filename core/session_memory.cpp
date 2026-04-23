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
    ctx.currentTask  = obj[QStringLiteral("currentTask")].toString();
    ctx.lastTopic    = obj[QStringLiteral("lastTopic")].toString();
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
    if (!m_sessionMessages.isEmpty()) {
        QJsonObject sessionSummaryObj;
        sessionSummaryObj[QStringLiteral("date")] =
            QDateTime::currentDateTime().toString(Qt::ISODate);
        sessionSummaryObj[QStringLiteral("commandCount")] = m_taskContext.commandCount;
        sessionSummaryObj[QStringLiteral("lastTopic")]    = m_taskContext.lastTopic;

        QString summary;
        const int count = m_sessionMessages.size();
        const int show  = qMin(count, 4);
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

    while (m_sessionMessages.size() > MAX_SESSION_MESSAGES) {
        m_sessionMessages.removeFirst();
    }
}

QJsonArray SessionMemory::recentMessagesAsJson(int maxMessages) const
{
    QJsonArray arr;
    const int start = qMax(0, m_sessionMessages.size() - maxMessages);
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
    const int start = qMax(0, m_sessionMessages.size() - 10);
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

    const QString lower = userInput.toLower();

    if (lower.contains(QStringLiteral("найди")) || lower.contains(QStringLiteral("search"))
        || lower.contains(QStringLiteral("гугл"))) {
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
// Проект: инъекция информации из индексатора
// ============================================================

void SessionMemory::setProjectInfo(const QString& root,
                                   const QString& projectMap,
                                   int fileCount,
                                   int symbolCount)
{
    m_projectRoot        = root;
    m_projectMap         = projectMap;
    m_projectFileCount   = fileCount;
    m_projectSymbolCount = symbolCount;
}

void SessionMemory::clearProjectInfo()
{
    m_projectRoot.clear();
    m_projectMap.clear();
    m_projectFileCount   = 0;
    m_projectSymbolCount = 0;
}

// ============================================================
// Системный промпт для Claude API
// ============================================================

QString SessionMemory::buildSystemPrompt() const
{
    QString prompt;

    // --- Базовая роль ---
    prompt += QStringLiteral(
        "Ты — J.A.R.V.I.S., персональный ИИ-ассистент и IDE-агент на Windows. "
        "Отвечай на русском, кратко и по делу. Без смайликов, без воды.\n\n"
    );

    // --- Режим работы ---
    if (m_vibeMode) {
        prompt += QStringLiteral(
            "=== РЕЖИМ: ВАЙБКОДИНГ ===\n"
            "Пользователь пишет короткие кодинг-запросы ('сделай X', 'оптимизируй Y', "
            "'добавь функцию Z'). Твоя задача:\n"
            "- Писать РАБОЧИЙ готовый код, а не псевдокод.\n"
            "- Объяснение — 1-2 строки ПЕРЕД блоком кода, не после.\n"
            "- Никаких '// остальной код без изменений' — всегда полные файлы либо точные DIFF.\n\n"
        );
    } else {
        prompt += QStringLiteral(
            "=== РЕЖИМ: ДИАЛОГ + КОДИНГ ===\n"
            "Можешь и разговаривать, и писать код. На обычные вопросы — отвечай текстом. "
            "На кодинг-запросы — используй блоки из раздела ниже.\n\n"
        );
    }

    // --- Блоки кода ---
    prompt += QStringLiteral(
        "=== РАБОТА С ФАЙЛАМИ (JARVIS ИХ АВТОМАТИЧЕСКИ ПРИМЕНИТ) ===\n"
        "Создать/перезаписать файл:\n"
        "[FILE:relative/path/file.cpp]\n"
        "...полный код файла...\n"
        "[/FILE]\n\n"
        "Точечное изменение (экономит токены, предпочтительно для мелких правок):\n"
        "[DIFF:relative/path/file.cpp]\n"
        "[FIND]\n"
        "...точный старый код...\n"
        "[REPLACE]\n"
        "...новый код...\n"
        "[/DIFF]\n\n"
        "Создать папку: [MKDIR:relative/path]\n"
        "Удалить файл:  [DELETE:relative/path/file]\n"
        "Системная команда: [CMD:команда]\n\n"
        "Правила:\n"
        "- Мелкие правки → [DIFF]. Крупные рефакторинги или новые файлы → [FILE].\n"
        "- Не пиши заглушки вида '// ...без изменений' внутри [FILE] — только полный код.\n"
        "- Пути — ВСЕГДА относительные от корня проекта.\n"
        "- Разговорный вопрос → просто текст, без блоков.\n\n"
    );

    // --- Проект ---
    if (hasProjectInfo()) {
        prompt += QStringLiteral("=== ПРОЕКТ ПОЛЬЗОВАТЕЛЯ ===\n");
        prompt += QStringLiteral("Корень: ") + m_projectRoot + QStringLiteral("\n");
        prompt += QStringLiteral("Индекс: ") + QString::number(m_projectFileCount)
                + QStringLiteral(" файлов, ")
                + QString::number(m_projectSymbolCount)
                + QStringLiteral(" символов.\n\n");

        // Критично: запретить Claude просить код, если индекс есть
        prompt += QStringLiteral(
            "ВАЖНО: проект уже проиндексирован и тебе автоматически приложат релевантные "
            "фрагменты в блоке '--- Контекст из проекта ---' в конце пользовательского сообщения. "
            "НЕ ПРОСИ у пользователя 'скинь код' или 'приложи файл' — у тебя уже есть индекс. "
            "Если нужного фрагмента не хватает — явно скажи, какой файл/функция нужна, и JARVIS "
            "подгрузит её в следующем сообщении.\n\n"
        );

        if (!m_projectMap.isEmpty()) {
            // Ограничим карту проекта, чтобы не съела весь бюджет токенов
            QString map = m_projectMap;
            constexpr int MAX_MAP_CHARS = 4000;
            if (map.size() > MAX_MAP_CHARS) {
                map = map.left(MAX_MAP_CHARS) + QStringLiteral("\n...(обрезано)\n");
            }
            prompt += QStringLiteral("Карта проекта:\n") + map + QStringLiteral("\n");
        }
    }

    // --- Факты о пользователе ---
    if (!m_persistentFacts.isEmpty()) {
        prompt += QStringLiteral("=== ФАКТЫ О ПОЛЬЗОВАТЕЛЕ ===\n");
        for (auto it = m_persistentFacts.begin(); it != m_persistentFacts.end(); ++it) {
            prompt += QStringLiteral("- ") + it.key() + QStringLiteral(": ")
                    + it.value().toString() + QStringLiteral("\n");
        }
        prompt += QStringLiteral("\n");
    }

    // --- Текущая задача ---
    bool hasTaskBlock = false;
    QString taskBlock;
    if (!m_taskContext.currentTask.isEmpty()) {
        taskBlock += QStringLiteral("Текущая задача: ") + m_taskContext.currentTask
                   + QStringLiteral("\n");
        hasTaskBlock = true;
    }
    if (!m_taskContext.lastTopic.isEmpty()) {
        taskBlock += QStringLiteral("Последняя тема: ") + m_taskContext.lastTopic
                   + QStringLiteral("\n");
        hasTaskBlock = true;
    }
    if (!m_taskContext.recentApps.isEmpty()) {
        taskBlock += QStringLiteral("Недавние приложения: ")
                   + m_taskContext.recentApps.join(QStringLiteral(", "))
                   + QStringLiteral("\n");
        hasTaskBlock = true;
    }
    if (hasTaskBlock) {
        prompt += QStringLiteral("=== КОНТЕКСТ СЕССИИ ===\n") + taskBlock + QStringLiteral("\n");
    }

    // --- Прошлые сессии (только самые свежие) ---
    if (!m_pastSessions.isEmpty()) {
        prompt += QStringLiteral("=== ИЗ ПРОШЛЫХ СЕССИЙ ===\n");
        const int show = qMin(m_pastSessions.size(), 3);
        for (int i = m_pastSessions.size() - show; i < m_pastSessions.size(); ++i) {
            const QJsonObject s = m_pastSessions[i].toObject();
            const QString topic = s[QStringLiteral("lastTopic")].toString();
            if (topic.isEmpty()) continue;
            prompt += QStringLiteral("- ") + s[QStringLiteral("date")].toString()
                    + QStringLiteral(": ") + topic + QStringLiteral("\n");
        }
    }

    return prompt;
}
