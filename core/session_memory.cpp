// -------------------------------------------------------
// session_memory.cpp — Контекстная память J.A.R.V.I.S.
// -------------------------------------------------------

#include "session_memory.h"
#include "jarvis_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QSet>
#include <QTime>

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
// Извлечение ключевых слов (для тем сессии и поиска по истории)
// ============================================================
//
// Общий хелпер для:
//  - updateContext()      — накопление "тем" текущей сессии (m_sessionTopics)
//  - buildHistoryContext() — извлечение темы из запроса "вспомни что было..."
// Стоп-слова включают как общую RU/EN мусорную лексику, так и слова,
// характерные для самого запроса истории ("вспомни", "на прошлой неделе"
// и т.п.) — чтобы они не попадали ни в темы, ни в ключевые слова поиска.

namespace {

QStringList extractSearchWords(const QString& text)
{
    static const QSet<QString> stopWords = {
        // Общие RU
        QStringLiteral("и"),       QStringLiteral("в"),       QStringLiteral("на"),
        QStringLiteral("с"),       QStringLiteral("из"),      QStringLiteral("к"),
        QStringLiteral("по"),      QStringLiteral("у"),       QStringLiteral("от"),
        QStringLiteral("за"),      QStringLiteral("для"),     QStringLiteral("без"),
        QStringLiteral("что"),     QStringLiteral("как"),     QStringLiteral("это"),
        QStringLiteral("там"),     QStringLiteral("где"),     QStringLiteral("тут"),
        QStringLiteral("же"),      QStringLiteral("бы"),      QStringLiteral("не"),
        QStringLiteral("но"),      QStringLiteral("ли"),      QStringLiteral("ни"),
        QStringLiteral("мне"),     QStringLiteral("мой"),     QStringLiteral("его"),
        QStringLiteral("ее"),      QStringLiteral("её"),      QStringLiteral("они"),
        QStringLiteral("ты"),      QStringLiteral("я"),       QStringLiteral("мы"),
        QStringLiteral("вы"),      QStringLiteral("при"),     QStringLiteral("про"),
        QStringLiteral("над"),     QStringLiteral("под"),     QStringLiteral("или"),
        QStringLiteral("если"),    QStringLiteral("когда"),   QStringLiteral("есть"),
        QStringLiteral("было"),    QStringLiteral("будет"),   QStringLiteral("сейчас"),
        QStringLiteral("просто"),  QStringLiteral("очень"),   QStringLiteral("можешь"),
        QStringLiteral("нужно"),   QStringLiteral("надо"),    QStringLiteral("давай"),
        QStringLiteral("пожалуйста"), QStringLiteral("джарвис"), QStringLiteral("делали"),
        QStringLiteral("делал"),   QStringLiteral("занимались"),
        // Запрос истории / периоды (RU) — не должны попадать в темы/поиск
        QStringLiteral("вспомни"), QStringLiteral("вспомнить"), QStringLiteral("помнишь"),
        QStringLiteral("история"), QStringLiteral("сессий"),  QStringLiteral("сессия"),
        QStringLiteral("сессии"),  QStringLiteral("сегодня"), QStringLiteral("вчера"),
        QStringLiteral("позавчера"), QStringLiteral("прошлой"), QStringLiteral("прошлую"),
        QStringLiteral("прошлом"), QStringLiteral("этой"),    QStringLiteral("неделе"),
        QStringLiteral("недели"),  QStringLiteral("неделю"),  QStringLiteral("месяце"),
        QStringLiteral("месяц"),   QStringLiteral("последние"), QStringLiteral("дней"),
        QStringLiteral("день"),    QStringLiteral("дня"),
        // Общие EN
        QStringLiteral("the"),  QStringLiteral("a"),    QStringLiteral("an"),
        QStringLiteral("to"),   QStringLiteral("in"),   QStringLiteral("on"),
        QStringLiteral("at"),   QStringLiteral("for"),  QStringLiteral("of"),
        QStringLiteral("and"),  QStringLiteral("or"),   QStringLiteral("but"),
        QStringLiteral("with"), QStringLiteral("from"), QStringLiteral("is"),
        QStringLiteral("are"),  QStringLiteral("was"),  QStringLiteral("be"),
        QStringLiteral("i"),    QStringLiteral("you"),  QStringLiteral("my"),
        QStringLiteral("we"),   QStringLiteral("did"),  QStringLiteral("have"),
        QStringLiteral("this"), QStringLiteral("that"), QStringLiteral("about"),
        QStringLiteral("please"), QStringLiteral("could"), QStringLiteral("would"),
        QStringLiteral("jarvis"),
        // Запрос истории / периоды (EN)
        QStringLiteral("remember"), QStringLiteral("recall"), QStringLiteral("session"),
        QStringLiteral("history"),  QStringLiteral("today"),  QStringLiteral("yesterday"),
        QStringLiteral("last"),     QStringLiteral("week"),   QStringLiteral("month"),
        QStringLiteral("days"),     QStringLiteral("day"),    QStringLiteral("what"),
    };

    static const QRegularExpression splitter(
        QStringLiteral("[\\s,.:;!?\\-\"'()\\[\\]{}/\\\\]+"));

    QStringList result;
    QSet<QString> seen;
    for (QString w : text.toLower().split(splitter, Qt::SkipEmptyParts)) {
        w = w.trimmed();
        if (w.length() < 4) continue;
        if (stopWords.contains(w)) continue;

        // Грубый "стемминг": обрезаем длинные слова до 6 символов, чтобы
        // словоформы ("профиль"/"профилю"/"профиля") совпадали при поиске
        // по журналу сессий без полноценной морфологии.
        if (w.length() > 6) w = w.left(6);

        if (seen.contains(w)) continue;
        seen.insert(w);
        result.append(w);
    }
    return result;
}

} // namespace

// ============================================================
// SessionMemory
// ============================================================

SessionMemory::SessionMemory(QObject* parent)
    : QObject(parent)
{
    m_sessionStart = QDateTime::currentDateTime();
    loadPersistent();
}

SessionMemory::~SessionMemory()
{
    flushSessionSummary();
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

// Возвращает последние N команд пользователя (только role == "user")
QStringList SessionMemory::recentCommands(int n) const
{
    QStringList result;
    for (int i = m_sessionMessages.size() - 1; i >= 0 && result.size() < n; --i) {
        if (m_sessionMessages[i].role == QStringLiteral("user")) {
            result.prepend(m_sessionMessages[i].content);
        }
    }
    return result;
}

// Возвращает последний ответ ассистента
QString SessionMemory::lastResponse() const
{
    for (int i = m_sessionMessages.size() - 1; i >= 0; --i) {
        if (m_sessionMessages[i].role == QStringLiteral("assistant")) {
            return m_sessionMessages[i].content;
        }
    }
    return QString();
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

    // Накопление "тем" сессии для поискового журнала (buildHistoryContext).
    // Дедуп + ограничение размера — храним только последние MAX_SESSION_TOPICS.
    for (const QString& w : extractSearchWords(userInput)) {
        m_sessionTopics.removeAll(w);
        m_sessionTopics.append(w);
    }
    while (m_sessionTopics.size() > MAX_SESSION_TOPICS) {
        m_sessionTopics.removeFirst();
    }

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

    // Сохраняем сводку сессии после каждого сообщения — crash-safe:
    // даже при аварийном завершении на диске останется актуальная
    // запись для команды "вспомни что было сегодня/на этой неделе".
    flushSessionSummary();

    emit memoryUpdated();
}

// ============================================================
// Постоянная память
// ============================================================

QString SessionMemory::persistentFilePath() const
{
    return JarvisPaths::subPath(QStringLiteral("jarvis_memory.json"));
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
// Поисковый журнал сессий
// ============================================================

void SessionMemory::recordFileTouched(const QString& path)
{
    if (path.isEmpty()) return;

    m_sessionFilesTouched.removeAll(path);
    m_sessionFilesTouched.append(path);
    while (m_sessionFilesTouched.size() > MAX_SESSION_FILES) {
        m_sessionFilesTouched.removeFirst();
    }
}

QJsonObject SessionMemory::buildSessionSummaryObject() const
{
    QJsonObject obj;
    obj[QStringLiteral("date")]         = m_sessionStart.toString(Qt::ISODate);
    obj[QStringLiteral("endDate")]      = QDateTime::currentDateTime().toString(Qt::ISODate);
    obj[QStringLiteral("commandCount")] = m_taskContext.commandCount;
    obj[QStringLiteral("lastTopic")]    = m_taskContext.lastTopic;
    obj[QStringLiteral("topics")]       = QJsonArray::fromStringList(m_sessionTopics);
    obj[QStringLiteral("filesTouched")] = QJsonArray::fromStringList(m_sessionFilesTouched);
    obj[QStringLiteral("messageCount")] = m_sessionMessages.size();

    // Краткая выжимка: первые и последние сообщения сессии — достаточно,
    // чтобы Claude мог сослаться на "о чём шла речь" без полного лога.
    QString summary;
    const int count     = m_sessionMessages.size();
    const int headCount = qMin(count, 3);
    for (int i = 0; i < headCount; ++i) {
        const auto& msg = m_sessionMessages[i];
        summary += msg.role + QStringLiteral(": ")
                 + msg.content.left(100) + QStringLiteral("\n");
    }
    if (count > headCount) {
        const int tailStart = qMax(headCount, count - 2);
        if (tailStart > headCount) summary += QStringLiteral("...\n");
        for (int i = tailStart; i < count; ++i) {
            const auto& msg = m_sessionMessages[i];
            summary += msg.role + QStringLiteral(": ")
                     + msg.content.left(100) + QStringLiteral("\n");
        }
    }
    obj[QStringLiteral("summary")] = summary.trimmed();

    return obj;
}

void SessionMemory::flushSessionSummary()
{
    // Пока в сессии вообще ничего не произошло — не создаём пустую запись.
    if (m_sessionMessages.isEmpty()) return;

    const QJsonObject obj = buildSessionSummaryObject();

    if (m_currentSessionIndex >= 0 && m_currentSessionIndex < m_pastSessions.size()) {
        // Обновляем уже созданную запись текущей сессии "на месте".
        m_pastSessions[m_currentSessionIndex] = obj;
    } else {
        m_pastSessions.append(obj);
        while (m_pastSessions.size() > MAX_PAST_SESSIONS) {
            m_pastSessions.removeFirst();
        }
        // Текущая сессия всегда последняя добавленная — после возможной
        // обрезки спереди она остаётся последним элементом массива.
        m_currentSessionIndex = m_pastSessions.size() - 1;
    }

    savePersistent();
}

// ============================================================
// Поиск по истории: "вспомни что было ..."
// ============================================================

QString SessionMemory::buildHistoryContext(const QString& userQuery) const
{
    const QString lower = userQuery.toLower();

    // --- 1. Похоже ли это на запрос истории? ---
    static const QStringList triggers = {
        QStringLiteral("вспомни"),      QStringLiteral("вспомнить"),
        QStringLiteral("помнишь"),      QStringLiteral("что было"),
        QStringLiteral("что мы дела"),  QStringLiteral("что я дела"),
        QStringLiteral("чем занима"),   QStringLiteral("история сесси"),
        QStringLiteral("remember what"),QStringLiteral("what did we"),
        QStringLiteral("what have we"), QStringLiteral("what was i"),
        QStringLiteral("recall"),       QStringLiteral("session history"),
    };
    bool isRecall = false;
    for (const auto& t : triggers) {
        if (lower.contains(t)) { isRecall = true; break; }
    }
    if (!isRecall) return QString();

    // --- 2. Период из запроса ---
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime rangeStart, rangeEnd;
    QString periodLabel;

    auto startOfDay = [](const QDate& d) { return QDateTime(d, QTime(0, 0, 0)); };
    auto endOfDay   = [](const QDate& d) { return QDateTime(d, QTime(23, 59, 59)); };

    if (lower.contains(QStringLiteral("сегодня")) || lower.contains(QStringLiteral("today"))) {
        rangeStart  = startOfDay(now.date());
        rangeEnd    = now;
        periodLabel = QStringLiteral("сегодня");
    } else if (lower.contains(QStringLiteral("позавчера"))) {
        const QDate d = now.date().addDays(-2);
        rangeStart  = startOfDay(d);
        rangeEnd    = endOfDay(d);
        periodLabel = QStringLiteral("позавчера");
    } else if (lower.contains(QStringLiteral("вчера")) || lower.contains(QStringLiteral("yesterday"))) {
        const QDate d = now.date().addDays(-1);
        rangeStart  = startOfDay(d);
        rangeEnd    = endOfDay(d);
        periodLabel = QStringLiteral("вчера");
    } else if (lower.contains(QStringLiteral("прошлой недел"))
            || lower.contains(QStringLiteral("прошлую недел"))
            || lower.contains(QStringLiteral("last week"))) {
        // Понедельник-воскресенье ПРЕДЫДУЩЕЙ календарной недели.
        const int dow = now.date().dayOfWeek(); // 1=Пн .. 7=Вс
        const QDate thisMonday = now.date().addDays(1 - dow);
        const QDate lastMonday = thisMonday.addDays(-7);
        const QDate lastSunday = thisMonday.addDays(-1);
        rangeStart  = startOfDay(lastMonday);
        rangeEnd    = endOfDay(lastSunday);
        periodLabel = QStringLiteral("на прошлой неделе");
    } else if (lower.contains(QStringLiteral("этой недел")) || lower.contains(QStringLiteral("this week"))) {
        const int dow = now.date().dayOfWeek();
        const QDate thisMonday = now.date().addDays(1 - dow);
        rangeStart  = startOfDay(thisMonday);
        rangeEnd    = now;
        periodLabel = QStringLiteral("на этой неделе");
    } else if (lower.contains(QStringLiteral("прошлом месяц")) || lower.contains(QStringLiteral("last month"))) {
        const QDate firstOfThis  = QDate(now.date().year(), now.date().month(), 1);
        const QDate lastOfPrev   = firstOfThis.addDays(-1);
        const QDate firstOfPrev  = QDate(lastOfPrev.year(), lastOfPrev.month(), 1);
        rangeStart  = startOfDay(firstOfPrev);
        rangeEnd    = endOfDay(lastOfPrev);
        periodLabel = QStringLiteral("в прошлом месяце");
    } else {
        // "за последние N дней" / "last N days"
        static const QRegularExpression reDays(
            QStringLiteral(R"((\d+)\s*(дн|day))"), QRegularExpression::CaseInsensitiveOption);
        const auto m = reDays.match(lower);
        if (m.hasMatch()) {
            const int n = qMax(1, m.captured(1).toInt());
            rangeStart  = startOfDay(now.date().addDays(-n));
            rangeEnd    = now;
            periodLabel = QStringLiteral("за последние ") + QString::number(n) + QStringLiteral(" дн.");
        }
    }

    // --- 3. Ключевые слова темы (то, что осталось после триггеров/периода) ---
    const QStringList queryWords = extractSearchWords(userQuery);

    // --- 4. Поиск по m_pastSessions (включая текущую, ещё не финализированную) ---
    QVector<QJsonObject> matches;
    for (const auto& v : m_pastSessions) {
        const QJsonObject s = v.toObject();

        if (rangeStart.isValid()) {
            const QDateTime sessionStart =
                QDateTime::fromString(s[QStringLiteral("date")].toString(), Qt::ISODate);
            const QDateTime sessionEnd =
                QDateTime::fromString(s[QStringLiteral("endDate")].toString(), Qt::ISODate);
            const bool inRange =
                (sessionStart.isValid() && sessionStart >= rangeStart && sessionStart <= rangeEnd)
                || (sessionEnd.isValid()   && sessionEnd   >= rangeStart && sessionEnd   <= rangeEnd);
            if (!inRange) continue;
        }

        if (!queryWords.isEmpty()) {
            QString haystack = s[QStringLiteral("summary")].toString().toLower()
                              + QChar(' ') + s[QStringLiteral("lastTopic")].toString().toLower();
            for (const auto& t : s[QStringLiteral("topics")].toArray())
                haystack += QChar(' ') + t.toString().toLower();
            for (const auto& f : s[QStringLiteral("filesTouched")].toArray())
                haystack += QChar(' ') + f.toString().toLower();

            bool keywordOk = false;
            for (const auto& kw : queryWords) {
                if (haystack.contains(kw)) { keywordOk = true; break; }
            }
            if (!keywordOk) continue;
        }

        matches.append(s);
    }

    // --- 5. Формируем контекст для Claude/Gemini ---
    QString context;
    context += QStringLiteral("\n\n--- Журнал сессий (автоматически от JARVIS) ---\n");
    if (!periodLabel.isEmpty()) {
        context += QStringLiteral("# Запрошенный период: ") + periodLabel + QStringLiteral("\n");
    }
    if (!queryWords.isEmpty()) {
        context += QStringLiteral("# Ключевые слова темы: ")
                 + queryWords.join(QStringLiteral(", ")) + QStringLiteral("\n");
    }

    if (matches.isEmpty()) {
        context += QStringLiteral(
            "По заданному периоду/теме в журнале сессий ничего не найдено. "
            "Сообщи пользователю честно, что данных за этот период/по этой теме нет — "
            "не придумывай содержимое.\n");
        context += QStringLiteral("--- Конец журнала сессий ---\n");
        return context;
    }

    // Самые свежие — самые релевантные; ограничиваем, чтобы не раздувать контекст.
    constexpr int MAX_SHOW = 10;
    const int from = qMax(0, matches.size() - MAX_SHOW);
    if (from > 0) {
        context += QStringLiteral("# (показаны последние ") + QString::number(MAX_SHOW)
                 + QStringLiteral(" из ") + QString::number(matches.size())
                 + QStringLiteral(" найденных сессий)\n");
    }

    for (int i = from; i < matches.size(); ++i) {
        const QJsonObject& s = matches[i];
        const QString date = s[QStringLiteral("date")].toString();

        context += QStringLiteral("\n## Сессия от ") + date + QStringLiteral("\n");

        QStringList topics;
        for (const auto& t : s[QStringLiteral("topics")].toArray()) topics.append(t.toString());
        if (!topics.isEmpty()) {
            context += QStringLiteral("Темы: ") + topics.join(QStringLiteral(", ")) + QStringLiteral("\n");
        }

        QStringList files;
        for (const auto& f : s[QStringLiteral("filesTouched")].toArray()) files.append(f.toString());
        if (!files.isEmpty()) {
            context += QStringLiteral("Файлы: ") + files.join(QStringLiteral(", ")) + QStringLiteral("\n");
        }

        const QString summary = s[QStringLiteral("summary")].toString();
        if (!summary.isEmpty()) {
            context += QStringLiteral("Содержание:\n") + summary + QStringLiteral("\n");
        }
    }

    context += QStringLiteral(
        "\nЭто реальные данные из журнала сессий JARVIS — отвечай пользователю на "
        "их основе, в свободной форме (\"На прошлой неделе вы занимались...\"). "
        "Не упоминай слова 'журнал' или JSON-структуры — расскажи как человек, "
        "вспоминающий, чем вы вместе занимались.\n");
    context += QStringLiteral("--- Конец журнала сессий ---\n");
    return context;
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
    // Вайбкодинг убран — кодинг режим всегда активен через [FILE]/[DIFF] блоки.
    prompt += QStringLiteral(
        "=== РЕЖИМ: ДИАЛОГ + КОДИНГ ===\n"
        "Можешь и разговаривать, и писать код. На обычные вопросы — отвечай текстом. "
        "На кодинг-запросы — используй блоки из раздела ниже.\n\n"
    );
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

    // --- Профиль предпочтений (UserProfile, обучается со временем) ---
    if (!m_userProfileSummary.isEmpty()) {
        prompt += QStringLiteral(
            "=== ПРОФИЛЬ ПОЛЬЗОВАТЕЛЯ (выучено JARVIS со временем) ===\n")
                + m_userProfileSummary + QStringLiteral("\n"
            "Используй это для адаптации тона и приоритетов ответа (например, "
            "если сейчас вечер и обычно идёт разработка — отвечай технически и "
            "по делу; если сценарий 'Игра' — короче и без лишних деталей). "
            "НЕ упоминай напрямую существование 'профиля' или 'сценариев' — "
            "веди себя естественно.\n\n");
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
        // m_currentSessionIndex — это ТЕКУЩАЯ (ещё не завершённая) сессия,
        // её не показываем как "прошлую".
        const int total = m_pastSessions.size();
        int shown = 0;
        for (int i = total - 1; i >= 0 && shown < 5; --i) {
            if (i == m_currentSessionIndex) continue;
            const QJsonObject s = m_pastSessions[i].toObject();

            QStringList topics;
            for (const auto& t : s[QStringLiteral("topics")].toArray()) topics.append(t.toString());
            const QString topic = s[QStringLiteral("lastTopic")].toString();

            QString line = QStringLiteral("- ") + s[QStringLiteral("date")].toString() + QStringLiteral(": ");
            if (!topics.isEmpty()) {
                line += topics.mid(0, 5).join(QStringLiteral(", "));
            } else if (!topic.isEmpty()) {
                line += topic;
            } else {
                continue; // нечего показать
            }
            prompt += line + QStringLiteral("\n");
            ++shown;
        }
        prompt += QStringLiteral(
            "Если пользователь спрашивает 'вспомни что было ...' — JARVIS автоматически "
            "подставит подробный журнал сессий в конец сообщения; отвечай на основе него, "
            "а не только этого краткого списка.\n");
    }

    return prompt;
}
