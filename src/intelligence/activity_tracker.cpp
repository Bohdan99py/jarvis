// ============================================================
// activity_tracker.cpp — Deep context awareness for J.A.R.V.I.S.
// ============================================================

#include "activity_tracker.h"
#include "database_manager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>

#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

// ============================================================
// Constructor / Destructor
// ============================================================

ActivityTracker::ActivityTracker(QObject* parent)
    : QObject(parent)
{
    ensureTables();
}

ActivityTracker::~ActivityTracker()
{
    stop();
}

// ============================================================
// Start / Stop
// ============================================================

void ActivityTracker::start(int intervalSeconds)
{
    if (m_running) return;

    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &ActivityTracker::onCapture);
    }

    m_running = true;
    m_activityStart = QDateTime::currentDateTime();
    m_timer->start(intervalSeconds * 1000);

    // Immediate first capture
    onCapture();
}

void ActivityTracker::stop()
{
    if (!m_running) return;
    m_running = false;
    if (m_timer) m_timer->stop();
}

// ============================================================
// Capture
// ============================================================

void ActivityTracker::onCapture()
{
    const QString proc  = getActiveProcessName();
    const QString title = getActiveWindowTitle();

    if (proc.isEmpty()) return;

    const QString app = cleanAppName(proc);
    recordActivity(app, title);
}

void ActivityTracker::recordActivity(const QString& appName, const QString& windowTitle)
{
    const QString cat = categorizeApp(appName, windowTitle);
    const QDateTime now = QDateTime::currentDateTime();

    // Activity changed?
    if (appName != m_currentApp) {
        // Save previous activity duration
        if (!m_currentApp.isEmpty() && m_activityStart.isValid()) {
            const int dur = static_cast<int>(m_activityStart.secsTo(now));
            if (dur >= MIN_DURATION_SEC) {
                RecentEntry entry;
                entry.timestamp   = m_activityStart;
                entry.app         = m_currentApp;
                entry.title       = m_currentTitle;
                entry.category    = m_currentCategory;
                entry.durationSec = dur;
                m_recentActivity.append(entry);
                while (m_recentActivity.size() > MAX_RECENT)
                    m_recentActivity.removeFirst();
            }

            // Record transition
            Transition tr;
            tr.fromApp = m_currentApp;
            tr.toApp   = appName;
            tr.when    = now;
            m_transitions.append(tr);
            while (m_transitions.size() > MAX_TRANSITIONS)
                m_transitions.removeFirst();
        }

        m_currentApp      = appName;
        m_currentTitle    = windowTitle;
        m_currentCategory = cat;
        m_activityStart   = now;

        emit activityChanged(appName, cat);
    } else {
        // Same app, just update title
        m_currentTitle = windowTitle;
    }
}

// ============================================================
// Activity Context (for system prompt)
// ============================================================

QString ActivityTracker::buildActivityContext() const
{
    if (m_currentApp.isEmpty()) return QString();

    QString ctx;

    // Current activity
    ctx += QStringLiteral("Currently active: ") + m_currentApp;
    if (!m_currentTitle.isEmpty()) {
        // Truncate very long titles
        QString title = m_currentTitle;
        if (title.length() > 100) title = title.left(100) + QStringLiteral("...");
        ctx += QStringLiteral(" — \"") + title + QStringLiteral("\"");
    }
    ctx += QStringLiteral(" [") + m_currentCategory + QStringLiteral("]");

    const int dur = currentActivityDuration();
    if (dur > 60) {
        ctx += QStringLiteral(" for ") + QString::number(dur / 60) + QStringLiteral("min");
    }
    ctx += QStringLiteral("\n");

    // Recent transitions (last 5)
    if (m_transitions.size() > 1) {
        ctx += QStringLiteral("Recent workflow: ");
        QStringList flow;
        const int start = qMax(0, m_transitions.size() - 5);
        for (int i = start; i < m_transitions.size(); ++i) {
            if (flow.isEmpty() || flow.last() != m_transitions[i].fromApp)
                flow.append(m_transitions[i].fromApp);
            flow.append(m_transitions[i].toApp);
        }
        // Deduplicate consecutive
        QStringList deduped;
        for (const auto& f : flow) {
            if (deduped.isEmpty() || deduped.last() != f)
                deduped.append(f);
        }
        ctx += deduped.join(QStringLiteral(" → ")) + QStringLiteral("\n");
    }

    // Time distribution in recent activity
    QMap<QString, int> catTime;
    for (const auto& e : m_recentActivity) {
        catTime[e.category] += e.durationSec;
    }
    if (!catTime.isEmpty()) {
        ctx += QStringLiteral("Session focus: ");
        QStringList parts;
        for (auto it = catTime.constBegin(); it != catTime.constEnd(); ++it) {
            if (it.value() > 30) {
                parts.append(it.key() + QStringLiteral(" ") + QString::number(it.value() / 60) + QStringLiteral("min"));
            }
        }
        if (!parts.isEmpty())
            ctx += parts.join(QStringLiteral(", "));
        ctx += QStringLiteral("\n");
    }

    return ctx;
}

QString ActivityTracker::recentActivitySummary(int minutes) const
{
    const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-minutes * 60);
    QMap<QString, int> appTime;
    QMap<QString, QString> appLastTitle;

    for (const auto& e : m_recentActivity) {
        if (e.timestamp < cutoff) continue;
        appTime[e.app] += e.durationSec;
        appLastTitle[e.app] = e.title;
    }

    if (appTime.isEmpty()) return QStringLiteral("No recent activity tracked.");

    // Sort by time spent
    QVector<QPair<QString, int>> sorted;
    for (auto it = appTime.constBegin(); it != appTime.constEnd(); ++it)
        sorted.append({it.key(), it.value()});
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    QString result;
    for (const auto& [app, secs] : sorted) {
        result += QStringLiteral("- ") + app + QStringLiteral(": ")
                + QString::number(secs / 60) + QStringLiteral("min");
        const QString& title = appLastTitle[app];
        if (!title.isEmpty()) {
            QString t = title;
            if (t.length() > 60) t = t.left(60) + QStringLiteral("...");
            result += QStringLiteral(" (\"") + t + QStringLiteral("\")");
        }
        result += QStringLiteral("\n");
    }
    return result;
}

int ActivityTracker::currentActivityDuration() const
{
    if (!m_activityStart.isValid()) return 0;
    return static_cast<int>(m_activityStart.secsTo(QDateTime::currentDateTime()));
}

// ============================================================
// Categorize application
// ============================================================

QString ActivityTracker::categorizeApp(const QString& processName, const QString& windowTitle)
{
    const QString p = processName.toLower();
    const QString t = windowTitle.toLower();

    // Coding / IDEs
    static const QStringList codeApps = {
        "clion", "rider", "devenv", "code", "pycharm", "webstorm",
        "intellij", "goland", "rustrover", "notepad++", "sublime"
    };
    for (const auto& a : codeApps)
        if (p.contains(a)) return QStringLiteral("coding");
    if (t.contains(QStringLiteral(".cpp")) || t.contains(QStringLiteral(".py"))
        || t.contains(QStringLiteral(".js")) || t.contains(QStringLiteral(".h"))
        || t.contains(QStringLiteral(".cs")))
        return QStringLiteral("coding");

    // Game engines
    if (p.contains("unrealed") || p.contains("ue5") || p.contains("unity")
        || p.contains("godot"))
        return QStringLiteral("game_engine");

    // Art / Design
    static const QStringList artApps = {
        "krita", "photoshop", "blender", "gimp", "illustrator",
        "figma", "inkscape", "aftereffects", "davinci", "premiere",
        "obs", "aseprite", "clip studio"
    };
    for (const auto& a : artApps)
        if (p.contains(a)) return QStringLiteral("art");

    // Browsing
    static const QStringList browsers = {
        "chrome", "firefox", "msedge", "opera", "brave", "vivaldi", "arc"
    };
    for (const auto& b : browsers)
        if (p.contains(b)) return QStringLiteral("browsing");

    // Communication
    static const QStringList comms = {
        "discord", "telegram", "slack", "teams", "zoom", "skype", "whatsapp"
    };
    for (const auto& c : comms)
        if (p.contains(c)) return QStringLiteral("communication");

    // Office
    static const QStringList office = {
        "word", "excel", "powerpoint", "onenote", "outlook",
        "libreoffice", "notion"
    };
    for (const auto& o : office)
        if (p.contains(o)) return QStringLiteral("office");

    // Gaming
    static const QStringList launchers = {"steam", "epicgames", "gog", "origin", "battle.net"};
    for (const auto& l : launchers)
        if (p.contains(l)) return QStringLiteral("gaming");

    // Terminal
    if (p.contains("cmd") || p.contains("powershell") || p.contains("terminal")
        || p.contains("wt") || p.contains("conhost"))
        return QStringLiteral("terminal");

    // Music
    if (p.contains("spotify") || p.contains("musicbee") || p.contains("itunes")
        || p.contains("foobar"))
        return QStringLiteral("music");

    return QStringLiteral("other");
}

QString ActivityTracker::cleanAppName(const QString& processName)
{
    QString name = QFileInfo(processName).completeBaseName();

    // Remove common suffixes
    static const QStringList suffixes = {"64", "32", ".exe"};
    for (const auto& s : suffixes) {
        if (name.endsWith(s, Qt::CaseInsensitive))
            name.chop(s.length());
    }

    // Capitalize
    if (!name.isEmpty())
        name[0] = name[0].toUpper();

    return name;
}

// ============================================================
// User role detection
// ============================================================

QString ActivityTracker::detectUserRole() const
{
    QMap<QString, int> catWeight;
    for (const auto& e : m_recentActivity)
        catWeight[e.category] += e.durationSec;

    // Also count current session
    if (!m_currentCategory.isEmpty())
        catWeight[m_currentCategory] += currentActivityDuration();

    if (catWeight.isEmpty()) return QStringLiteral("general user");

    // Find dominant category
    QString dominant;
    int maxTime = 0;
    for (auto it = catWeight.constBegin(); it != catWeight.constEnd(); ++it) {
        if (it.value() > maxTime) {
            maxTime = it.value();
            dominant = it.key();
        }
    }

    if (dominant == QStringLiteral("coding"))       return QStringLiteral("programmer");
    if (dominant == QStringLiteral("game_engine"))   return QStringLiteral("game developer");
    if (dominant == QStringLiteral("art"))           return QStringLiteral("artist / designer");
    if (dominant == QStringLiteral("browsing"))      return QStringLiteral("researcher");
    if (dominant == QStringLiteral("communication")) return QStringLiteral("communicator");
    if (dominant == QStringLiteral("office"))        return QStringLiteral("analyst");
    if (dominant == QStringLiteral("terminal"))      return QStringLiteral("sysadmin / devops");
    if (dominant == QStringLiteral("gaming"))        return QStringLiteral("gamer");

    return QStringLiteral("general user");
}

// ============================================================
// Knowledge Base
// ============================================================

void ActivityTracker::ensureTables()
{
    auto& db = DatabaseManager::instance();
    if (!db.isOpen()) return;

    QSqlQuery q(db.isOpen() ? QSqlDatabase::database() : QSqlDatabase());
    q.exec(R"(CREATE TABLE IF NOT EXISTS activity_log (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id     INTEGER NOT NULL DEFAULT 1,
        app_name    TEXT    NOT NULL,
        window_title TEXT   NOT NULL DEFAULT '',
        category    TEXT    NOT NULL DEFAULT 'other',
        duration_sec INTEGER NOT NULL DEFAULT 0,
        hour_of_day INTEGER NOT NULL DEFAULT 0,
        day_of_week INTEGER NOT NULL DEFAULT 1,
        created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
    ))");
    q.exec("CREATE INDEX IF NOT EXISTS idx_activity_user ON activity_log(user_id, created_at DESC)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_activity_cat  ON activity_log(user_id, category, hour_of_day)");

    q.exec(R"(CREATE TABLE IF NOT EXISTS knowledge_base (
        id              INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id         INTEGER NOT NULL DEFAULT 1,
        category        TEXT    NOT NULL DEFAULT 'general',
        key             TEXT    NOT NULL,
        value           TEXT    NOT NULL,
        confidence      REAL    NOT NULL DEFAULT 0.5,
        reinforcements  INTEGER NOT NULL DEFAULT 1,
        learned_at      TEXT    NOT NULL DEFAULT (datetime('now')),
        last_seen       TEXT    NOT NULL DEFAULT (datetime('now')),
        UNIQUE(user_id, key)
    ))");
    q.exec("CREATE INDEX IF NOT EXISTS idx_kb_user ON knowledge_base(user_id, category)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_kb_conf ON knowledge_base(user_id, confidence DESC)");
}

void ActivityTracker::learnFact(qint64 userId, const QString& category,
                                const QString& key, const QString& value,
                                float confidence)
{
    auto& db = DatabaseManager::instance();
    if (!db.isOpen()) return;

    // Resolve the user's current role for origin tagging
    QString originRole = QStringLiteral("local");
    auto userOpt = db.getUser(userId);
    if (userOpt && !userOpt->currentRole.isEmpty()) {
        originRole = userOpt->currentRole;
    }

    QSqlQuery q(QSqlDatabase::database());
    q.prepare(R"(INSERT INTO knowledge_base (user_id, category, key, value, confidence, origin_profile_role)
                 VALUES (:uid, :cat, :key, :val, :conf, :origin)
                 ON CONFLICT(user_id, key) DO UPDATE SET
                     value = :val2,
                     confidence = MIN(1.0, confidence + 0.1),
                     reinforcements = reinforcements + 1,
                     last_seen = datetime('now'))");
    q.bindValue(":uid",    userId);
    q.bindValue(":cat",    category);
    q.bindValue(":key",    key);
    q.bindValue(":val",    value);
    q.bindValue(":conf",   confidence);
    q.bindValue(":origin", originRole);
    q.bindValue(":val2",   value);
    q.exec();

    emit knowledgeLearned(key, value);
}

void ActivityTracker::reinforceFact(qint64 userId, const QString& key)
{
    auto& db = DatabaseManager::instance();
    if (!db.isOpen()) return;

    QSqlQuery q(QSqlDatabase::database());
    q.prepare(R"(UPDATE knowledge_base SET
                     confidence = MIN(1.0, confidence + 0.05),
                     reinforcements = reinforcements + 1,
                     last_seen = datetime('now')
                 WHERE user_id = :uid AND key = :key)");
    q.bindValue(":uid", userId);
    q.bindValue(":key", key);
    q.exec();
}

QString ActivityTracker::knowledgeSummary(qint64 userId, int maxFacts) const
{
    auto& db = DatabaseManager::instance();
    if (!db.isOpen()) return QString();

    QSqlQuery q(QSqlDatabase::database());
    q.prepare(R"(SELECT category, key, value, confidence, reinforcements
                 FROM knowledge_base
                 WHERE user_id = :uid AND confidence >= 0.4
                 ORDER BY confidence DESC, reinforcements DESC
                 LIMIT :lim)");
    q.bindValue(":uid", userId);
    q.bindValue(":lim", maxFacts);

    if (!q.exec()) return QString();

    QMap<QString, QStringList> byCategory;
    while (q.next()) {
        const QString cat  = q.value(0).toString();
        const QString key  = q.value(1).toString();
        const QString val  = q.value(2).toString();
        byCategory[cat].append(key + QStringLiteral(": ") + val);
    }

    if (byCategory.isEmpty()) return QString();

    QString result;
    for (auto it = byCategory.constBegin(); it != byCategory.constEnd(); ++it) {
        result += QStringLiteral("[") + it.key() + QStringLiteral("]\n");
        for (const auto& fact : it.value())
            result += QStringLiteral("- ") + fact + QStringLiteral("\n");
    }
    return result.trimmed();
}

// ============================================================
// Knowledge extraction from conversations
// ============================================================

void ActivityTracker::extractKnowledge(qint64 userId, const QString& userInput,
                                       const QString& aiResponse)
{
    const QString input = userInput.trimmed().toLower();

    // --- Extract from user input ---
    if (input.length() >= 10) {
        static const QRegularExpression reUse(
            QStringLiteral("(?:i use|i work with|i prefer|my (?:project|team|company) uses?)\\s+(.{3,40})"),
            QRegularExpression::CaseInsensitiveOption);
        auto m = reUse.match(input);
        if (m.hasMatch())
            learnFact(userId, QStringLiteral("tool"), m.captured(1).trimmed(), m.captured(1).trimmed(), 0.7f);

        static const QRegularExpression reRole(
            QStringLiteral("(?:i am a|i'm a|я работаю|я по профессии|моя специальность)\\s+(.{3,40})"),
            QRegularExpression::CaseInsensitiveOption);
        m = reRole.match(input);
        if (m.hasMatch())
            learnFact(userId, QStringLiteral("role"), QStringLiteral("profession"), m.captured(1).trimmed(), 0.9f);

        static const QRegularExpression reProject(
            QStringLiteral("(?:my project|мой проект|working on|работаю над)\\s+(.{3,60})"),
            QRegularExpression::CaseInsensitiveOption);
        m = reProject.match(input);
        if (m.hasMatch())
            learnFact(userId, QStringLiteral("project"), QStringLiteral("current_project"), m.captured(1).trimmed(), 0.7f);

        static const QRegularExpression reLang(
            QStringLiteral("(?:write|writing|code|coding|program|develop)(?:ing)?\\s+(?:in\\s+)?(c\\+\\+|python|rust|go|java|javascript|typescript|c#|kotlin|swift)"),
            QRegularExpression::CaseInsensitiveOption);
        m = reLang.match(input);
        if (m.hasMatch())
            learnFact(userId, QStringLiteral("skill"), QStringLiteral("language_") + m.captured(1).toLower(), m.captured(1), 0.7f);

        static const QRegularExpression reRemember(
            QStringLiteral("(?:remember that|keep in mind|note that|запомни что|учти что|имей в виду)\\s+(.{5,200})"),
            QRegularExpression::CaseInsensitiveOption);
        m = reRemember.match(input);
        if (m.hasMatch())
            learnFact(userId, QStringLiteral("preference"), m.captured(1).left(50), m.captured(1).trimmed(), 0.8f);

        // Hardware/EDA mentions
        static const QRegularExpression reHw(
            QStringLiteral("(?:my board|my pcb|using|with)\\s+(kicad|altium|eagle|stm32|esp32|arduino|raspberry|atmega|nrf52|fpga)"),
            QRegularExpression::CaseInsensitiveOption);
        m = reHw.match(input);
        if (m.hasMatch())
            learnFact(userId, QStringLiteral("tool"), QStringLiteral("hw_") + m.captured(1).toLower(), m.captured(1), 0.7f);

        // 3D/game engine mentions
        static const QRegularExpression re3d(
            QStringLiteral("(?:using|in|with|my)\\s+(unreal engine|ue5|ue4|unity|godot|blender|freecad|substance|maya|houdini)"),
            QRegularExpression::CaseInsensitiveOption);
        m = re3d.match(input);
        if (m.hasMatch())
            learnFact(userId, QStringLiteral("tool"), QStringLiteral("engine_") + m.captured(1).toLower().replace(' ', '_'), m.captured(1), 0.7f);

        // Version mentions: "UE 5.4", "Python 3.12", "Qt 6.7"
        static const QRegularExpression reVer(
            QStringLiteral("(unreal|ue|python|qt|blender|kicad|cmake|clion|rider|node|npm|gcc|clang|msvc|cuda|opengl|vulkan|directx)\\s*(\\d+\\.\\d+(?:\\.\\d+)?)"),
            QRegularExpression::CaseInsensitiveOption);
        m = reVer.match(input);
        if (m.hasMatch())
            learnFact(userId, QStringLiteral("environment"), m.captured(1).toLower() + QStringLiteral("_version"), m.captured(1) + QStringLiteral(" ") + m.captured(2), 0.8f);

        // OS/platform mentions
        static const QRegularExpression reOS(
            QStringLiteral("(?:running|on|using)\\s+(windows 1[01]|ubuntu|debian|arch|fedora|macos|linux)"),
            QRegularExpression::CaseInsensitiveOption);
        m = reOS.match(input);
        if (m.hasMatch())
            learnFact(userId, QStringLiteral("environment"), QStringLiteral("os"), m.captured(1), 0.8f);

        // Personal info: name, location, timezone
        static const QRegularExpression reName(
            QStringLiteral("(?:my name is|i'm called|call me|меня зовут|моё имя)\\s+([A-Za-zА-Яа-яёЁ]{2,20})"),
            QRegularExpression::CaseInsensitiveOption);
        m = reName.match(input);
        if (m.hasMatch())
            learnFact(userId, QStringLiteral("personal"), QStringLiteral("name"), m.captured(1).trimmed(), 0.95f);

        static const QRegularExpression reLoc(
            QStringLiteral("(?:i(?:'m| am) (?:from|in|based in)|я из|живу в)\\s+([A-Za-zА-Яа-яёЁ ]{2,30})"),
            QRegularExpression::CaseInsensitiveOption);
        m = reLoc.match(input);
        if (m.hasMatch())
            learnFact(userId, QStringLiteral("personal"), QStringLiteral("location"), m.captured(1).trimmed(), 0.8f);
    }

    // --- Extract from AI response (facts about what was discussed/decided) ---
    const QString resp = aiResponse.trimmed();
    if (resp.length() < 20 || resp.length() > 3000) return;
    if (resp.contains(QStringLiteral("[FILE:")) || resp.contains(QStringLiteral("[DIFF:"))) return;

    // Topic extraction: if AI discusses a specific technology in depth, learn it
    struct TopicPattern {
        QRegularExpression re;
        QString category;
        QString keyPrefix;
    };
    static const TopicPattern topicPatterns[] = {
        { QRegularExpression(QStringLiteral("\\b(cmake|qmake|meson|bazel|ninja|make)\\b"), QRegularExpression::CaseInsensitiveOption),
          QStringLiteral("workflow"), QStringLiteral("build_system") },
        { QRegularExpression(QStringLiteral("\\b(docker|kubernetes|k8s|podman)\\b"), QRegularExpression::CaseInsensitiveOption),
          QStringLiteral("workflow"), QStringLiteral("containers") },
        { QRegularExpression(QStringLiteral("\\b(github|gitlab|bitbucket|azure devops)\\b"), QRegularExpression::CaseInsensitiveOption),
          QStringLiteral("workflow"), QStringLiteral("vcs_platform") },
        { QRegularExpression(QStringLiteral("\\b(postgresql|mysql|sqlite|mongodb|redis)\\b"), QRegularExpression::CaseInsensitiveOption),
          QStringLiteral("tool"), QStringLiteral("database") },
    };

    const QString combined = (input + QStringLiteral(" ") + resp.left(500)).toLower();
    for (const auto& tp : topicPatterns) {
        auto m = tp.re.match(combined);
        if (m.hasMatch()) {
            const QString val = m.captured(1);
            learnFact(userId, tp.category, tp.keyPrefix + QStringLiteral("_") + val.toLower(),
                      val, 0.55f);
        }
    }
}

// ============================================================
// Windows API
// ============================================================

QString ActivityTracker::getActiveWindowTitle() const
{
#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return QString();

    wchar_t buf[512];
    int len = GetWindowTextW(hwnd, buf, 512);
    if (len <= 0) return QString();
    return QString::fromWCharArray(buf, len);
#else
    return QString();
#endif
}

QString ActivityTracker::getActiveProcessName() const
{
#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return QString();

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return QString();

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return QString();

    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    QString result;
    if (QueryFullProcessImageNameW(proc, 0, path, &size))
        result = QFileInfo(QString::fromWCharArray(path, size)).fileName();
    CloseHandle(proc);
    return result;
#else
    return QString();
#endif
}
