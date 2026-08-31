#pragma once
// -------------------------------------------------------
// trigger_engine.h — «Когда X, сделай Y»
//
// EventFeed до сих пор был односторонним: подсистемы клали в него
// строки, а слушал их только человек, если открывал панель. То
// есть JARVIS знал, что сборка закончилась или что подключили
// ESP32, и не делал с этим ничего.
//
// Триггер замыкает эту дугу:
//
//     событие/процесс/устройство/время/файл
//              │
//         TriggerEngine
//              │
//     инструмент или сценарий  (через тот же PermissionGate)
//
// Планировщик здесь не отдельная подсистема, а один из видов
// условия: «в 08:00» отличается от «когда запустился Unreal»
// только тем, откуда пришёл сигнал, а всё остальное — проверка
// правил, кулдаун, разрешения, журнал — общее.
//
// Три вещи, без которых это опасно, встроены сразу:
//   • кулдаун: правило не может сработать чаще, чем раз в N секунд;
//   • отсечка своих же событий: то, что триггер положил в ленту,
//     не может запустить триггер (иначе правило кормит само себя);
//   • разрешения не обходятся. Фоновое срабатывание, требующее
//     подтверждения, отклоняется PermissionGate — как и любое
//     другое действие без интерактивной сессии.
// -------------------------------------------------------

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QVector>

#include "event_feed.h"

class ToolRegistry;
class PermissionGate;
class WorkflowManager;
class DeviceHub;
class QTimer;
class QFileSystemWatcher;

// ============================================================
//  Условие
// ============================================================
struct TriggerWhen
{
    enum Kind {
        Unknown = 0,
        Event,             // строка в ленте: source + уровень + подстрока
        ProcessStarted,    // появился процесс с таким именем
        ProcessStopped,    // исчез процесс с таким именем
        DeviceConnected,   // устройство в DeviceHub перешло в Connected
        DailyTime,         // в HH:mm (по желанию — только в эти дни недели)
        Every,             // раз в N минут, пока JARVIS запущен
        Startup,           // один раз при запуске JARVIS
        FileChanged        // файл или содержимое папки изменилось
    };

    Kind kind = Unknown;

    QString    source;      // Event: источник ленты (пусто = любой)
    EventLevel minLevel = EventLevel::Info;   // Event: не ниже этого уровня
    QString    match;       // Event: подстрока заголовка (пусто = любой)

    QString process;        // ProcessStarted/Stopped: "UnrealEditor.exe"
    QString device;         // DeviceConnected: id или имя устройства
    QTime   at;             // DailyTime
    QList<int> days;        // DailyTime: 1=Пн … 7=Вс; пусто = каждый день
    int     minutes = 0;    // Every
    QString path;           // FileChanged

    bool isValid() const;
    QString human() const;  // «когда запускается UnrealEditor.exe»

    static Kind kindFromString(const QString& name);
    static QString kindName(Kind kind);

    static TriggerWhen fromJson(const QJsonObject& obj);
    QJsonObject        toJson() const;
};

// ============================================================
//  Действие
// ============================================================
struct TriggerThen
{
    QString     workflow;   // либо запустить сценарий…
    QString     tool;       // …либо один инструмент
    QJsonObject args;
    QString     notify;     // и/или просто сказать строкой в ленту

    bool isValid() const { return !workflow.isEmpty() || !tool.isEmpty() || !notify.isEmpty(); }
    QString human() const;

    static TriggerThen fromJson(const QJsonObject& obj);
    QJsonObject        toJson() const;
};

// ============================================================
//  Правило
// ============================================================
struct TriggerRule
{
    QString     name;           // ключ, регистр не важен
    QString     description;
    bool        enabled = true;
    TriggerWhen when;
    TriggerThen then;

    // Минимальный интервал между срабатываниями. Ноль означал бы,
    // что правило на «CPU выше 90%» выстрелит на каждом отсчёте.
    int         cooldownSeconds = 60;

    QDateTime   lastFired;
    int         fireCount = 0;

    bool isValid() const { return !name.isEmpty() && when.isValid() && then.isValid(); }
    QString human() const;

    static TriggerRule fromJson(const QJsonObject& obj);
    QJsonObject        toJson() const;
};

// ============================================================
//  TriggerEngine
// ============================================================
class TriggerEngine : public QObject
{
    Q_OBJECT

public:
    TriggerEngine(ToolRegistry* tools,
                  PermissionGate* gate,
                  WorkflowManager* workflows,
                  QObject* parent = nullptr);

    // Хаб опрашивается только если есть правило про устройства —
    // провайдеры дешёвые, но не бесплатные.
    void setDeviceHub(DeviceHub* hub) { m_devices = hub; }

    // Подписки и таймеры включаются здесь, а не в конструкторе:
    // правила ссылаются на сценарии и инструменты, а те создаются
    // позже. Startup-правила срабатывают через несколько секунд
    // после start() — на старте машина и без нас занята.
    void start();
    void stop();

    void setEnabled(bool on);
    bool isEnabled() const { return m_enabled; }

    // --- Хранилище ---
    void    load();
    bool    save() const;
    QString storagePath() const;

    QVector<TriggerRule> all() const { return m_rules; }
    const TriggerRule*   find(const QString& name) const;
    QStringList          names() const;
    int                  count() const { return m_rules.size(); }

    bool addOrReplace(const TriggerRule& rule);
    bool remove(const QString& name);
    bool setRuleEnabled(const QString& name, bool on);

    // Ручной прогон — «проверь, что это правило делает». Кулдаун и
    // флаг enabled при manual = true не проверяются.
    QString fire(const QString& name, bool manual, bool* okOut = nullptr);

    QString summaryForModel() const;

signals:
    void listChanged();
    void ruleFired(const QString& name, bool ok, const QString& report);

private:
    void onEvent(const FeedEvent& event);
    void onClockTick();
    void onProcessTick();
    void onFileChanged(const QString& path);

    void rebuildSchedule();      // какие таймеры и наблюдатели вообще нужны
    bool ready(const TriggerRule& rule) const;   // включено и кулдаун прошёл

    // По имени, а не по ссылке: действие правила может через инструменты
    // отредактировать или удалить сам список правил, и ссылка на элемент
    // вектора этого не переживёт.
    QString runRuleNamed(const QString& name, const QString& because);

    ToolRegistry*    m_tools     = nullptr;
    PermissionGate*  m_gate      = nullptr;
    WorkflowManager* m_workflows = nullptr;
    DeviceHub*       m_devices   = nullptr;

    QVector<TriggerRule> m_rules;
    bool m_enabled = true;
    bool m_started = false;

    // Одно срабатывание за раз: сценарий внутри правила сам кладёт
    // события в ленту, и без этого флага правило на «сценарий
    // завершился» перезапускало бы себя.
    bool m_firing = false;

    QTimer* m_clock    = nullptr;   // время: DailyTime + Every
    QTimer* m_procTick = nullptr;   // процессы и устройства

    // Один таймер на два источника, но снимок процессов и опрос хаба
    // стоят по-разному — берём только то, чего действительно ждут.
    bool m_watchProcesses = false;
    bool m_watchDevices   = false;
    QFileSystemWatcher* m_watcher = nullptr;

    QSet<QString> m_processes;      // имена процессов на прошлом опросе
    QSet<QString> m_connected;      // устройства, бывшие на связи
    QHash<QString, QDate> m_firedToday;   // DailyTime: правило -> дата

    static constexpr int kClockMs  = 20000;   // достаточно для минутной точности
    static constexpr int kProcMs   = 5000;
    static constexpr int kStartupDelayMs = 8000;
};

namespace JarvisTools {

// list_triggers / save_trigger / delete_trigger / enable_trigger /
// test_trigger — правила должны создаваться словами, иначе ими никто
// не будет пользоваться.
void registerTriggerTools(ToolRegistry& registry, TriggerEngine* engine);

} // namespace JarvisTools
