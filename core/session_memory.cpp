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
        m_taskContext.currentTask = QStringLiteral("Working with ") + app;
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
        periodLabel = QStringLiteral("today");
    } else if (lower.contains(QStringLiteral("позавчера"))) {
        const QDate d = now.date().addDays(-2);
        rangeStart  = startOfDay(d);
        rangeEnd    = endOfDay(d);
        periodLabel = QStringLiteral("day before yesterday");
    } else if (lower.contains(QStringLiteral("вчера")) || lower.contains(QStringLiteral("yesterday"))) {
        const QDate d = now.date().addDays(-1);
        rangeStart  = startOfDay(d);
        rangeEnd    = endOfDay(d);
        periodLabel = QStringLiteral("yesterday");
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
        periodLabel = QStringLiteral("last week");
    } else if (lower.contains(QStringLiteral("этой недел")) || lower.contains(QStringLiteral("this week"))) {
        const int dow = now.date().dayOfWeek();
        const QDate thisMonday = now.date().addDays(1 - dow);
        rangeStart  = startOfDay(thisMonday);
        rangeEnd    = now;
        periodLabel = QStringLiteral("this week");
    } else if (lower.contains(QStringLiteral("прошлом месяц")) || lower.contains(QStringLiteral("last month"))) {
        const QDate firstOfThis  = QDate(now.date().year(), now.date().month(), 1);
        const QDate lastOfPrev   = firstOfThis.addDays(-1);
        const QDate firstOfPrev  = QDate(lastOfPrev.year(), lastOfPrev.month(), 1);
        rangeStart  = startOfDay(firstOfPrev);
        rangeEnd    = endOfDay(lastOfPrev);
        periodLabel = QStringLiteral("last month");
    } else {
        // "за последние N дней" / "last N days"
        static const QRegularExpression reDays(
            QStringLiteral(R"((\d+)\s*(дн|day))"), QRegularExpression::CaseInsensitiveOption);
        const auto m = reDays.match(lower);
        if (m.hasMatch()) {
            const int n = qMax(1, m.captured(1).toInt());
            rangeStart  = startOfDay(now.date().addDays(-n));
            rangeEnd    = now;
            periodLabel = QStringLiteral("last ") + QString::number(n) + QStringLiteral(" days");
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
        context += QStringLiteral("--- End of session journal ---\n");
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

        context += QStringLiteral("\n## Session from ") + date + QStringLiteral("\n");

        QStringList topics;
        for (const auto& t : s[QStringLiteral("topics")].toArray()) topics.append(t.toString());
        if (!topics.isEmpty()) {
            context += QStringLiteral("Topics: ") + topics.join(QStringLiteral(", ")) + QStringLiteral("\n");
        }

        QStringList files;
        for (const auto& f : s[QStringLiteral("filesTouched")].toArray()) files.append(f.toString());
        if (!files.isEmpty()) {
            context += QStringLiteral("Files: ") + files.join(QStringLiteral(", ")) + QStringLiteral("\n");
        }

        const QString summary = s[QStringLiteral("summary")].toString();
        if (!summary.isEmpty()) {
            context += QStringLiteral("Content:\n") + summary + QStringLiteral("\n");
        }
    }

    context += QStringLiteral(
        "\nThis is real data from the JARVIS session journal. Answer the user based on "
        "this data in a natural, conversational way (\"Last week you were working on...\"). "
        "Don't mention 'journal' or JSON structures — recall it like a person "
        "remembering what you worked on together.\n");
    context += QStringLiteral("--- End of session journal ---\n");
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

void SessionMemory::setLearningStats(int totalInteractions, int likedResponses,
                                     int cachedResponses, int sessionsRecorded)
{
    m_totalInteractions = totalInteractions;
    m_likedResponses    = likedResponses;
    m_cachedResponses   = cachedResponses;
    m_sessionsRecorded  = sessionsRecorded;
}

// ============================================================
// Системный промпт для Claude API
// ============================================================

QString SessionMemory::buildSystemPrompt() const
{
    QString prompt;

    // --- Базовая роль ---
    prompt += QStringLiteral(
        "You are J.A.R.V.I.S., a personal AI assistant and IDE agent on Windows. "
        "CRITICAL LANGUAGE RULE: Always respond in the SAME language the user writes in. "
        "If the user writes in English — respond in English. "
        "If the user writes in Russian — respond in Russian. "
        "Never switch languages unless the user does. "
        "Be concise and direct. No emojis, no filler.\n\n"
    );

    prompt += QStringLiteral(
        "=== MODE: DIALOG + CODING ===\n"
        "You can both chat and write code. For regular questions — answer with text. "
        "For coding requests — use the blocks below.\n\n"
    );
    prompt += QStringLiteral(
        "=== FILE OPERATIONS (JARVIS APPLIES THEM AUTOMATICALLY) ===\n"
        "Create/overwrite file:\n"
        "[FILE:relative/path/file.cpp]\n"
        "...full file code...\n"
        "[/FILE]\n\n"
        "Precise edit (saves tokens, preferred for small changes):\n"
        "[DIFF:relative/path/file.cpp]\n"
        "[FIND]\n"
        "...exact old code...\n"
        "[REPLACE]\n"
        "...new code...\n"
        "[/DIFF]\n\n"
        "Create folder: [MKDIR:relative/path]\n"
        "Delete file:   [DELETE:relative/path/file]\n"
        "System command: [CMD:command]\n\n"
        "Rules:\n"
        "- Small edits -> [DIFF]. Large refactors or new files -> [FILE].\n"
        "- Never write stubs like '// ...unchanged' inside [FILE] — only full code.\n"
        "- Paths — ALWAYS relative from project root.\n"
        "- Conversational question -> just text, no blocks.\n\n"
    );

    // --- Проект ---
    if (hasProjectInfo()) {
        prompt += QStringLiteral("=== USER PROJECT ===\n");
        prompt += QStringLiteral("Root: ") + m_projectRoot + QStringLiteral("\n");
        prompt += QStringLiteral("Index: ") + QString::number(m_projectFileCount)
                + QStringLiteral(" files, ")
                + QString::number(m_projectSymbolCount)
                + QStringLiteral(" symbols.\n\n");

        prompt += QStringLiteral(
            "IMPORTANT: the project is already indexed. Relevant code fragments will be "
            "automatically attached in '--- Project context ---' at the end of user messages. "
            "DO NOT ask the user to 'send code' or 'attach file' — you already have the index. "
            "If a specific fragment is missing — name the file/function you need, and JARVIS "
            "will load it in the next message.\n\n"
        );

        if (!m_projectMap.isEmpty()) {
            // Ограничим карту проекта, чтобы не съела весь бюджет токенов
            QString map = m_projectMap;
            constexpr int MAX_MAP_CHARS = 4000;
            if (map.size() > MAX_MAP_CHARS) {
                map = map.left(MAX_MAP_CHARS) + QStringLiteral("\n...(truncated)\n");
            }
            prompt += QStringLiteral("Project map:\n") + map + QStringLiteral("\n");
        }
    }

    if (!m_persistentFacts.isEmpty()) {
        prompt += QStringLiteral("=== USER FACTS ===\n");
        for (auto it = m_persistentFacts.begin(); it != m_persistentFacts.end(); ++it) {
            prompt += QStringLiteral("- ") + it.key() + QStringLiteral(": ")
                    + it.value().toString() + QStringLiteral("\n");
        }
        prompt += QStringLiteral("\n");
    }

    // --- Active user identity ---
    if (!m_activeUserName.isEmpty()) {
        prompt += QStringLiteral("=== ACTIVE USER ===\nName: ") + m_activeUserName;
        if (!m_detectedRole.isEmpty())
            prompt += QStringLiteral(" | Role: ") + m_detectedRole;
        prompt += QStringLiteral("\n"
            "Adapt your style to this user's role and expertise level.\n\n");
    } else if (!m_detectedRole.isEmpty()) {
        prompt += QStringLiteral("=== USER ROLE (detected from activity) ===\n")
                + m_detectedRole + QStringLiteral("\n"
            "Adapt your style to this role — a programmer gets technical answers, "
            "an artist gets visual/creative guidance, etc.\n\n");
    }

    // --- What the user is doing right now ---
    if (!m_activityContext.isEmpty()) {
        prompt += QStringLiteral("=== CURRENT ACTIVITY (live) ===\n")
                + m_activityContext + QStringLiteral(
            "Use this to give contextual advice. If the user is in an IDE — be "
            "code-oriented. If in a browser — consider they might need info. "
            "If in an art tool — think visually. Be proactive when relevant.\n\n");
    }

    if (!m_userProfileSummary.isEmpty()) {
        prompt += QStringLiteral(
            "=== USER PROFILE (learned by JARVIS over time) ===\n")
                + m_userProfileSummary + QStringLiteral("\n"
            "Use this to adapt tone and response priorities (e.g., "
            "if it's evening and usually development time — be technical and "
            "to the point; if the scenario is 'Gaming' — be shorter). "
            "Do NOT mention the existence of 'profile' or 'scenarios' — "
            "behave naturally.\n\n");
    }

    // --- Knowledge base: facts JARVIS has learned about this user ---
    if (!m_knowledgeSummary.isEmpty()) {
        prompt += QStringLiteral("=== KNOWLEDGE BASE (learned facts about user) ===\n")
                + m_knowledgeSummary + QStringLiteral("\n"
            "Use these facts naturally. Don't say 'I know that you...' — just "
            "apply the knowledge to give better, more personalized answers.\n\n");
    }

    // --- Core Memory Stream: time-decay weighted recent events ---
    if (!m_memoryStreamContext.isEmpty()) {
        prompt += QStringLiteral("=== MEMORY STREAM (time-decay ranked) ===\n")
                + m_memoryStreamContext + QStringLiteral(
            "These are the most relevant recent events scored by "
            "importance / (1 + hours_elapsed). Use them to maintain continuity "
            "across sessions — reference prior context naturally without saying "
            "'according to my memory stream'.\n\n");
    }

    // --- Adaptive Focus: auto-detected from Memory Stream ---
    if (!m_adaptiveFocusContext.isEmpty()) {
        prompt += m_adaptiveFocusContext;
    }

    // --- System capabilities manifest ---
    if (!m_capabilitiesContext.isEmpty()) {
        prompt += m_capabilitiesContext;
    }

    bool hasTaskBlock = false;
    QString taskBlock;
    if (!m_taskContext.currentTask.isEmpty()) {
        taskBlock += QStringLiteral("Current task: ") + m_taskContext.currentTask
                   + QStringLiteral("\n");
        hasTaskBlock = true;
    }
    if (!m_taskContext.lastTopic.isEmpty()) {
        taskBlock += QStringLiteral("Last topic: ") + m_taskContext.lastTopic
                   + QStringLiteral("\n");
        hasTaskBlock = true;
    }
    if (!m_taskContext.recentApps.isEmpty()) {
        taskBlock += QStringLiteral("Recent apps: ")
                   + m_taskContext.recentApps.join(QStringLiteral(", "))
                   + QStringLiteral("\n");
        hasTaskBlock = true;
    }
    if (hasTaskBlock) {
        prompt += QStringLiteral("=== SESSION CONTEXT ===\n") + taskBlock + QStringLiteral("\n");
    }

    if (!m_pastSessions.isEmpty()) {
        prompt += QStringLiteral("=== PAST SESSIONS ===\n");
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
                continue;
            }
            prompt += line + QStringLiteral("\n");
            ++shown;
        }
        prompt += QStringLiteral(
            "If the user asks 'recall what happened ...' — JARVIS will automatically "
            "append a detailed session log at the end of the message; answer based on it.\n");
    }

    if (m_totalInteractions > 0 || m_likedResponses > 0) {
        prompt += QStringLiteral("=== YOUR MEMORY & EXPERIENCE ===\n");
        prompt += QStringLiteral("You are not a blank model. You have accumulated experience:\n");
        if (m_totalInteractions > 0)
            prompt += QStringLiteral("- %1 saved dialogs in knowledge base\n").arg(m_totalInteractions);
        if (m_likedResponses > 0)
            prompt += QStringLiteral("- %1 responses approved by user\n").arg(m_likedResponses);
        if (m_cachedResponses > 0)
            prompt += QStringLiteral("- %1 ready responses in offline cache\n").arg(m_cachedResponses);
        if (m_sessionsRecorded > 0)
            prompt += QStringLiteral("- %1 chat sessions in journal\n").arg(m_sessionsRecorded);
        prompt += QStringLiteral(
            "Use this experience: if a question resembles what was discussed before — "
            "answer specifically based on past context, not generic phrases. "
            "Silently account for user preferences, without saying 'I remember that...'.\n\n");
    }

    return prompt;
}
