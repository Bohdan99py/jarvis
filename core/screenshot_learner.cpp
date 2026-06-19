// ============================================================
// screenshot_learner.cpp — J.A.R.V.I.S. App Usage Pattern Learner
// ============================================================

#include "screenshot_learner.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QSettings>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ============================================================
//  Конструктор / Деструктор
// ============================================================

ScreenshotLearner::ScreenshotLearner(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &ScreenshotLearner::onCapture);

    // Приложения которые игнорируем (шум)
    m_ignoredApps = {
        QStringLiteral("explorer"),
        QStringLiteral("SearchHost"),
        QStringLiteral("ShellExperienceHost"),
        QStringLiteral("StartMenuExperienceHost"),
        QStringLiteral("TextInputHost"),
        QStringLiteral("LockApp"),
        QStringLiteral("SystemSettings"),
        QStringLiteral("ApplicationFrameHost")
    };

    m_lastChangeTime = QDateTime::currentDateTime();

    // Загружаем настройку enabled
    QSettings s(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
    m_enabled = s.value(QStringLiteral("learner/enabled"), true).toBool();

    // ensureTable() вызовется в start() — не в конструкторе,
    // чтобы DatabaseManager успел инициализироваться
}

ScreenshotLearner::~ScreenshotLearner()
{
    stop();
}

// ============================================================
//  Собственная БД — отдельный файл, не конфликтует с DatabaseManager
// ============================================================

static const QString kLearnerDbConn = QStringLiteral("jarvis_learner");

static QSqlDatabase learnerDb()
{
    if (QSqlDatabase::contains(kLearnerDbConn)) {
        return QSqlDatabase::database(kLearnerDbConn);
    }

    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    QString path = dir + QStringLiteral("/app_patterns.db");

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kLearnerDbConn);
    db.setDatabaseName(path);
    if (!db.open()) {
        qWarning() << "[Learner] Cannot open DB:" << path << db.lastError().text();
    }
    return db;
}

// ============================================================
//  Таблица в SQLite
// ============================================================

void ScreenshotLearner::ensureTable()
{
    QSqlDatabase db = learnerDb();
    if (!db.isOpen()) {
        qWarning() << "[Learner] Database not available";
        return;
    }

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS app_usage_log ("
        "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp    DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  app_name     TEXT NOT NULL,"
        "  window_title TEXT,"
        "  duration_sec INTEGER DEFAULT 0,"
        "  hour_of_day  INTEGER,"
        "  day_of_week  INTEGER"
        ")"));

    // Индексы для быстрых запросов
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_app_usage_hour "
        "ON app_usage_log(hour_of_day, day_of_week)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_app_usage_time "
        "ON app_usage_log(timestamp)"));
}

// ============================================================
//  Управление
// ============================================================

void ScreenshotLearner::start(int intervalMinutes)
{
    if (m_running) return;
    if (!m_enabled) return;

    // Создаём таблицу при первом запуске (БД уже инициализирована)
    ensureTable();

    m_running = true;
    m_lastChangeTime = QDateTime::currentDateTime();
    m_lastAppName.clear();

    // Первый захват — через интервал таймера (не сразу)
    // Чтобы не нагружать стек при инициализации
    m_timer->start(intervalMinutes * 60 * 1000);
    qDebug() << "[Learner] Started, interval:" << intervalMinutes << "min";
}

void ScreenshotLearner::stop()
{
    if (!m_running) return;

    // Записываем последнее приложение перед остановкой
    if (!m_lastAppName.isEmpty()) {
        int duration = static_cast<int>(m_lastChangeTime.secsTo(QDateTime::currentDateTime()));
        if (duration >= MIN_DURATION_SEC) {
            recordCurrentApp();
        }
    }

    m_timer->stop();
    m_running = false;
    qDebug() << "[Learner] Stopped";
}

void ScreenshotLearner::setEnabled(bool enabled)
{
    m_enabled = enabled;
    QSettings s(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
    s.setValue(QStringLiteral("learner/enabled"), enabled);

    if (!enabled && m_running) stop();
}

bool ScreenshotLearner::isEnabled() const { return m_enabled; }

void ScreenshotLearner::setIgnoredApps(const QStringList& apps)
{
    m_ignoredApps = apps;
}

// ============================================================
//  Windows API: получаем активное окно
// ============================================================

QString ScreenshotLearner::getActiveWindowTitle() const
{
#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return {};
    wchar_t title[512] = {};
    int len = GetWindowTextW(hwnd, title, 512);
    if (len <= 0) return {};
    return QString::fromWCharArray(title, len);
#else
    return QStringLiteral("Unknown");
#endif
}

QString ScreenshotLearner::getActiveProcessName() const
{
#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return {};

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return {};

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) return {};

    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    bool ok = QueryFullProcessImageNameW(hProcess, 0, path, &size);
    CloseHandle(hProcess);

    if (!ok || size == 0) return {};

    QString fullPath = QString::fromWCharArray(path, static_cast<int>(size));
    // Извлекаем имя файла без расширения
    int lastSlash = fullPath.lastIndexOf(QLatin1Char('\\'));
    QString fileName = (lastSlash >= 0) ? fullPath.mid(lastSlash + 1) : fullPath;
    int dotPos = fileName.lastIndexOf(QLatin1Char('.'));
    if (dotPos > 0) fileName = fileName.left(dotPos);
    return fileName;
#else
    return QStringLiteral("unknown");
#endif
}

QString ScreenshotLearner::cleanAppName(const QString& processName) const
{
    // Делаем имя более читаемым
    static const QHash<QString, QString> nameMap = {
        { QStringLiteral("clion64"),     QStringLiteral("CLion") },
        { QStringLiteral("idea64"),      QStringLiteral("IntelliJ IDEA") },
        { QStringLiteral("rider64"),     QStringLiteral("JetBrains Rider") },
        { QStringLiteral("chrome"),      QStringLiteral("Chrome") },
        { QStringLiteral("brave"),       QStringLiteral("Brave") },
        { QStringLiteral("firefox"),     QStringLiteral("Firefox") },
        { QStringLiteral("msedge"),      QStringLiteral("Edge") },
        { QStringLiteral("Code"),        QStringLiteral("VS Code") },
        { QStringLiteral("Telegram"),    QStringLiteral("Telegram") },
        { QStringLiteral("Discord"),     QStringLiteral("Discord") },
        { QStringLiteral("slack"),       QStringLiteral("Slack") },
        { QStringLiteral("steam"),       QStringLiteral("Steam") },
        { QStringLiteral("spotify"),     QStringLiteral("Spotify") },
        { QStringLiteral("UE4Editor"),   QStringLiteral("Unreal Engine") },
        { QStringLiteral("UnrealEditor"),QStringLiteral("Unreal Engine") },
        { QStringLiteral("blender"),     QStringLiteral("Blender") },
        { QStringLiteral("krita"),       QStringLiteral("Krita") },
        { QStringLiteral("GIMP"),        QStringLiteral("GIMP") },
        { QStringLiteral("Photoshop"),   QStringLiteral("Photoshop") },
        { QStringLiteral("davinci"),     QStringLiteral("DaVinci Resolve") },
        { QStringLiteral("obs64"),       QStringLiteral("OBS Studio") },
        { QStringLiteral("jarvis"),      QStringLiteral("JARVIS") },
        { QStringLiteral("WINWORD"),     QStringLiteral("Word") },
        { QStringLiteral("EXCEL"),       QStringLiteral("Excel") },
        { QStringLiteral("POWERPNT"),    QStringLiteral("PowerPoint") },
        { QStringLiteral("notepad++"),   QStringLiteral("Notepad++") },
        { QStringLiteral("WindowsTerminal"), QStringLiteral("Terminal") },
        { QStringLiteral("GitHubDesktop"), QStringLiteral("GitHub Desktop") },
    };

    QString lower = processName.toLower();
    for (auto it = nameMap.constBegin(); it != nameMap.constEnd(); ++it) {
        if (lower.contains(it.key().toLower())) return it.value();
    }
    return processName; // возвращаем как есть
}

// ============================================================
//  Захват и запись
// ============================================================

void ScreenshotLearner::onCapture()
{
    if (!m_enabled) return;

    QString processName = getActiveProcessName();
    if (processName.isEmpty()) return;

    // Игнорируем системные приложения
    for (const QString& ignored : m_ignoredApps) {
        if (processName.compare(ignored, Qt::CaseInsensitive) == 0) return;
    }

    QString appName = cleanAppName(processName);
    QString title = getActiveWindowTitle();
    QDateTime now = QDateTime::currentDateTime();

    // Если приложение сменилось — записываем предыдущее
    if (appName != m_lastAppName && !m_lastAppName.isEmpty()) {
        int duration = static_cast<int>(m_lastChangeTime.secsTo(now));
        if (duration >= MIN_DURATION_SEC) {
            recordCurrentApp();
        }
        m_lastChangeTime = now;
    }

    m_lastAppName = appName;
    m_lastWindowTitle = title;

    // Периодически проверяем — есть ли предложение
    checkSuggestions();
}

void ScreenshotLearner::recordCurrentApp()
{
    if (m_lastAppName.isEmpty()) return;

    QDateTime now = QDateTime::currentDateTime();
    int duration = static_cast<int>(m_lastChangeTime.secsTo(now));
    int hour = m_lastChangeTime.time().hour();
    int dow = m_lastChangeTime.date().dayOfWeek(); // 1=Mon

    QSqlDatabase db = learnerDb();
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO app_usage_log "
        "(app_name, window_title, duration_sec, hour_of_day, day_of_week) "
        "VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(m_lastAppName);
    q.addBindValue(m_lastWindowTitle.left(200)); // обрезаем длинные заголовки
    q.addBindValue(duration);
    q.addBindValue(hour);
    q.addBindValue(dow);

    if (q.exec()) {
        emit patternRecorded(m_lastAppName, duration);
    } else {
        qWarning() << "[Learner] Insert error:" << q.lastError().text();
    }
}

// ============================================================
//  Предсказания
// ============================================================

void ScreenshotLearner::checkSuggestions()
{
    auto suggestions = suggestionsForNow(1);
    if (!suggestions.isEmpty() && suggestions.first().confidence >= 0.6f) {
        // Не предлагаем если это уже активное приложение
        if (suggestions.first().appName != m_lastAppName) {
            emit suggestionReady(suggestions.first());
        }
    }
}

QVector<AppSuggestion> ScreenshotLearner::suggestionsForNow(int topN) const
{
    QVector<AppSuggestion> result;

    QSqlDatabase db = learnerDb();
    if (!db.isOpen()) return result;

    QDateTime now = QDateTime::currentDateTime();
    int hour = now.time().hour();
    int dow = now.date().dayOfWeek();

    // Ищем самые частые приложения для этого часа и дня недели
    // Учитываем ±1 час для сглаживания
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT app_name, COUNT(*) as freq, "
        "       MAX(window_title) as last_title, "
        "       SUM(duration_sec) as total_dur "
        "FROM app_usage_log "
        "WHERE hour_of_day BETWEEN ? AND ? "
        "  AND day_of_week = ? "
        "  AND timestamp > datetime('now', '-30 days') "
        "GROUP BY app_name "
        "ORDER BY freq DESC "
        "LIMIT ?"));
    q.addBindValue(qMax(0, hour - 1));
    q.addBindValue(qMin(23, hour + 1));
    q.addBindValue(dow);
    q.addBindValue(topN + 3); // берём с запасом для фильтрации

    if (!q.exec()) return result;

    // Считаем общую частоту для нормализации confidence
    int totalFreq = 0;
    struct Row { QString app; int freq; QString title; };
    QVector<Row> rows;
    while (q.next()) {
        Row r;
        r.app   = q.value(0).toString();
        r.freq  = q.value(1).toInt();
        r.title = q.value(2).toString();
        totalFreq += r.freq;
        rows.push_back(r);
    }

    if (totalFreq == 0) return result;

    for (const auto& r : rows) {
        if (result.size() >= topN) break;
        // Пропускаем JARVIS — не нужно предлагать открыть самого себя
        if (r.app == QStringLiteral("JARVIS")) continue;

        AppSuggestion s;
        s.appName    = r.app;
        s.frequency  = r.freq;
        s.confidence = static_cast<float>(r.freq) / static_cast<float>(totalFreq);
        s.lastTitle  = r.title;
        result.push_back(s);
    }

    return result;
}

bool ScreenshotLearner::hasSuggestion(float threshold) const
{
    auto s = suggestionsForNow(1);
    return !s.isEmpty() && s.first().confidence >= threshold;
}

// ============================================================
//  Статистика
// ============================================================

QVector<AppUsageStat> ScreenshotLearner::todayStats() const
{
    QVector<AppUsageStat> result;
    QSqlDatabase db = learnerDb();
    if (!db.isOpen()) return result;

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "SELECT app_name, "
        "       SUM(duration_sec) / 60 as total_min, "
        "       COUNT(*) as sessions "
        "FROM app_usage_log "
        "WHERE date(timestamp) = date('now') "
        "GROUP BY app_name "
        "ORDER BY total_min DESC"));

    while (q.next()) {
        AppUsageStat s;
        s.appName      = q.value(0).toString();
        s.totalMinutes = q.value(1).toInt();
        s.sessionCount = q.value(2).toInt();
        result.push_back(s);
    }
    return result;
}

QVector<AppUsageStat> ScreenshotLearner::weekStats() const
{
    QVector<AppUsageStat> result;
    QSqlDatabase db = learnerDb();
    if (!db.isOpen()) return result;

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "SELECT app_name, "
        "       SUM(duration_sec) / 60 as total_min, "
        "       COUNT(*) as sessions "
        "FROM app_usage_log "
        "WHERE timestamp > datetime('now', '-7 days') "
        "GROUP BY app_name "
        "ORDER BY total_min DESC"));

    while (q.next()) {
        AppUsageStat s;
        s.appName      = q.value(0).toString();
        s.totalMinutes = q.value(1).toInt();
        s.sessionCount = q.value(2).toInt();
        result.push_back(s);
    }
    return result;
}

int ScreenshotLearner::totalRecords() const
{
    QSqlDatabase db = learnerDb();
    if (!db.isOpen()) return 0;
    QSqlQuery q(db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM app_usage_log"));
    return q.next() ? q.value(0).toInt() : 0;
}

// ============================================================
//  Очистка
// ============================================================

void ScreenshotLearner::clearAllData()
{
    QSqlDatabase db = learnerDb();
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.exec(QStringLiteral("DELETE FROM app_usage_log"));
    qDebug() << "[Learner] All data cleared";
}

void ScreenshotLearner::cleanupOlderThan(int days)
{
    QSqlDatabase db = learnerDb();
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM app_usage_log "
        "WHERE timestamp < datetime('now', ? || ' days')"));
    q.addBindValue(QStringLiteral("-%1").arg(days));
    q.exec();
    qDebug() << "[Learner] Cleaned up records older than" << days << "days";
}