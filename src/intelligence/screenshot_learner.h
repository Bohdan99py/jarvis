#pragma once
// ============================================================
// screenshot_learner.h — J.A.R.V.I.S. App Usage Pattern Learner
//
// Что делает:
//   - Каждые N минут записывает какое приложение активно
//   - Хранит паттерны: час + день_недели + приложение + длительность
//   - Анализирует привычки и предлагает запустить приложение
//   - "Сейчас 9 утра понедельник — обычно вы открываете CLion"
//
// Как работает:
//   - Windows API: GetForegroundWindow → GetWindowText → ProcessName
//   - SQLite: таблица app_usage_log (timestamp, app, title, duration)
//   - Анализ: GROUP BY hour, day_of_week, app → частотность
//   - Предсказание: топ-3 приложения для текущего часа
//
// Никаких скриншотов на диск — только метаданные!
// Текст окна и имя процесса, без содержимого экрана.
// ============================================================

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QSqlDatabase>

// ============================================================
//  Структуры данных
// ============================================================

struct AppSuggestion {
    QString appName;       // "CLion", "Brave", "Steam"
    int     frequency;     // сколько раз использовалось в этот час
    float   confidence;    // 0.0 — 1.0
    QString lastTitle;     // последний заголовок окна
};

struct AppUsageStat {
    QString appName;
    int     totalMinutes;
    int     sessionCount;
};

struct AppLogEntry {
    qint64    id;
    QDateTime timestamp;
    QString   appName;      // имя процесса (jarvis.exe → JARVIS)
    QString   windowTitle;  // заголовок окна
    int       durationSec;  // сколько секунд было активно
    int       hourOfDay;    // 0–23
    int       dayOfWeek;    // 1=Mon … 7=Sun
};

// ============================================================
//  ScreenshotLearner
// ============================================================

class ScreenshotLearner : public QObject
{
    Q_OBJECT
public:
    explicit ScreenshotLearner(QObject* parent = nullptr);
    ~ScreenshotLearner() override;

    // ── Управление ────────────────────────────────────────
    void start(int intervalMinutes = 2);
    void stop();
    bool isRunning() const { return m_running; }

    // ── Предсказания ──────────────────────────────────────
    // Топ-N приложений для текущего часа и дня
    QVector<AppSuggestion> suggestionsForNow(int topN = 3) const;

    // Есть ли уверенное предсказание? (confidence > threshold)
    bool hasSuggestion(float threshold = 0.6f) const;

    // ── Статистика ────────────────────────────────────────
    QVector<AppUsageStat> todayStats() const;
    QVector<AppUsageStat> weekStats() const;
    int totalRecords() const;

    // ── Данные ────────────────────────────────────────────
    // Удалить все записи (сброс обучения)
    void clearAllData();
    // Удалить записи старше N дней
    void cleanupOlderThan(int days = 90);

    // ── Настройки ─────────────────────────────────────────
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // Игнорировать определённые приложения (Desktop, Explorer и т.д.)
    void setIgnoredApps(const QStringList& apps);

signals:
    // Новая запись добавлена
    void patternRecorded(const QString& appName, int durationSec);
    // Есть предложение для пользователя
    void suggestionReady(const AppSuggestion& suggestion);
    // Ошибка
    void error(const QString& message);

private slots:
    void onCapture();

private:
    void ensureTable();
    void recordCurrentApp();
    void checkSuggestions();

    // Windows API
    QString getActiveWindowTitle() const;
    QString getActiveProcessName() const;
    QString cleanAppName(const QString& processName) const;

    QTimer*     m_timer           = nullptr;
    bool        m_running         = false;
    bool        m_enabled         = true;
    QString     m_lastAppName;
    QString     m_lastWindowTitle;
    QDateTime   m_lastChangeTime;
    QStringList m_ignoredApps;

    // Минимальная длительность для записи (секунд)
    static constexpr int MIN_DURATION_SEC = 10;
    // Не записывать одно и то же чаще чем раз в N секунд
    static constexpr int DEDUP_INTERVAL_SEC = 30;
};
