// -------------------------------------------------------
// trigger_engine.cpp — см. trigger_engine.h
// -------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN     // без этого rpcndr.h делает #define small char
#endif

#include "trigger_engine.h"

#include "action_log.h"
#include "device_hub.h"
#include "edit_journal.h"
#include "permission_gate.h"
#include "tool_registry.h"
#include "workflow_manager.h"

#include "jarvis_paths.h"

#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <QDebug>

#include <algorithm>

#include <windows.h>
#include <tlhelp32.h>

namespace {

// Имя процесса сравниваем без учёта регистра и расширения: человек
// говорит «Unreal», в правиле может стоять «UnrealEditor.exe», а в
// системе — «UnrealEditor.exe».
QString normalizedProcess(const QString& name)
{
    QString n = name.trimmed().toLower();
    if (n.endsWith(QLatin1String(".exe")))
        n.chop(4);
    return n;
}

QSet<QString> runningProcessNames()
{
    QSet<QString> out;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return out;

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snap, &entry)) {
        do {
            out.insert(normalizedProcess(QString::fromWCharArray(entry.szExeFile)));
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return out;
}

const char* const kDayNames[7] = { "mon", "tue", "wed", "thu", "fri", "sat", "sun" };

QList<int> parseDays(const QString& text)
{
    QList<int> out;
    const QStringList parts = text.toLower().split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                                   Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        bool isNumber = false;
        const int n = p.toInt(&isNumber);
        if (isNumber && n >= 1 && n <= 7) {
            if (!out.contains(n))
                out << n;
            continue;
        }
        for (int i = 0; i < 7; ++i) {
            if (p.startsWith(QLatin1String(kDayNames[i]))) {
                if (!out.contains(i + 1))
                    out << (i + 1);
                break;
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

QString daysText(const QList<int>& days)
{
    if (days.isEmpty())
        return QStringLiteral("каждый день");
    QStringList names;
    for (int d : days) {
        if (d >= 1 && d <= 7)
            names << QString::fromLatin1(kDayNames[d - 1]);
    }
    return names.join(QChar(','));
}

EventLevel levelFromName(const QString& name, EventLevel fallback)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("info"))    return EventLevel::Info;
    if (n == QLatin1String("ok") || n == QLatin1String("good")) return EventLevel::Good;
    if (n == QLatin1String("warning") || n == QLatin1String("warn")) return EventLevel::Warning;
    if (n == QLatin1String("error"))   return EventLevel::Error;
    return fallback;
}

} // namespace

// ============================================================
//  Условие
// ============================================================

TriggerWhen::Kind TriggerWhen::kindFromString(const QString& name)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("event"))            return Event;
    if (n == QLatin1String("process_started"))  return ProcessStarted;
    if (n == QLatin1String("process_stopped"))  return ProcessStopped;
    if (n == QLatin1String("device_connected")) return DeviceConnected;
    if (n == QLatin1String("daily_time"))       return DailyTime;
    if (n == QLatin1String("every"))            return Every;
    if (n == QLatin1String("startup"))          return Startup;
    if (n == QLatin1String("file_changed"))     return FileChanged;
    return Unknown;
}

QString TriggerWhen::kindName(Kind kind)
{
    switch (kind) {
    case Event:           return QStringLiteral("event");
    case ProcessStarted:  return QStringLiteral("process_started");
    case ProcessStopped:  return QStringLiteral("process_stopped");
    case DeviceConnected: return QStringLiteral("device_connected");
    case DailyTime:       return QStringLiteral("daily_time");
    case Every:           return QStringLiteral("every");
    case Startup:         return QStringLiteral("startup");
    case FileChanged:     return QStringLiteral("file_changed");
    case Unknown:         break;
    }
    return QStringLiteral("unknown");
}

bool TriggerWhen::isValid() const
{
    switch (kind) {
    case Event:           return true;                   // пустой фильтр = любое событие
    case ProcessStarted:
    case ProcessStopped:  return !process.trimmed().isEmpty();
    case DeviceConnected: return !device.trimmed().isEmpty();
    case DailyTime:       return at.isValid();
    case Every:           return minutes > 0;
    case Startup:         return true;
    case FileChanged:     return !path.trimmed().isEmpty();
    case Unknown:         break;
    }
    return false;
}

QString TriggerWhen::human() const
{
    switch (kind) {
    case Event: {
        QString s = QStringLiteral("событие");
        if (!source.isEmpty())
            s += QStringLiteral(" от %1").arg(source.toUpper());
        if (minLevel != EventLevel::Info)
            s += QStringLiteral(" уровня %1+").arg(eventLevelName(minLevel));
        if (!match.isEmpty())
            s += QStringLiteral(" со словом \"%1\"").arg(match);
        return s;
    }
    case ProcessStarted:  return QStringLiteral("запускается %1").arg(process);
    case ProcessStopped:  return QStringLiteral("закрывается %1").arg(process);
    case DeviceConnected: return QStringLiteral("подключается %1").arg(device);
    case DailyTime:       return QStringLiteral("в %1 (%2)")
                                     .arg(at.toString(QStringLiteral("HH:mm")), daysText(days));
    case Every:           return QStringLiteral("каждые %1 мин").arg(minutes);
    case Startup:         return QStringLiteral("при запуске JARVIS");
    case FileChanged:     return QStringLiteral("меняется %1").arg(path);
    case Unknown:         break;
    }
    return QStringLiteral("(условие не задано)");
}

TriggerWhen TriggerWhen::fromJson(const QJsonObject& obj)
{
    TriggerWhen w;
    w.kind     = kindFromString(obj.value(QStringLiteral("kind")).toString());
    w.source   = obj.value(QStringLiteral("source")).toString();
    w.minLevel = levelFromName(obj.value(QStringLiteral("level")).toString(), EventLevel::Info);
    w.match    = obj.value(QStringLiteral("match")).toString();
    w.process  = obj.value(QStringLiteral("process")).toString();
    w.device   = obj.value(QStringLiteral("device")).toString();
    w.minutes  = obj.value(QStringLiteral("minutes")).toInt(0);
    w.path     = obj.value(QStringLiteral("path")).toString();

    const QString atText = obj.value(QStringLiteral("at")).toString();
    if (!atText.isEmpty())
        w.at = QTime::fromString(atText, QStringLiteral("HH:mm"));

    const QJsonArray days = obj.value(QStringLiteral("days")).toArray();
    for (const QJsonValue& v : days) {
        const int d = v.toInt(0);
        if (d >= 1 && d <= 7)
            w.days << d;
    }
    return w;
}

QJsonObject TriggerWhen::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("kind")] = kindName(kind);

    if (!source.isEmpty())  obj[QStringLiteral("source")]  = source;
    if (minLevel != EventLevel::Info)
        obj[QStringLiteral("level")] = eventLevelName(minLevel);
    if (!match.isEmpty())   obj[QStringLiteral("match")]   = match;
    if (!process.isEmpty()) obj[QStringLiteral("process")] = process;
    if (!device.isEmpty())  obj[QStringLiteral("device")]  = device;
    if (minutes > 0)        obj[QStringLiteral("minutes")] = minutes;
    if (!path.isEmpty())    obj[QStringLiteral("path")]    = path;
    if (at.isValid())       obj[QStringLiteral("at")]      = at.toString(QStringLiteral("HH:mm"));

    if (!days.isEmpty()) {
        QJsonArray arr;
        for (int d : days)
            arr.append(d);
        obj[QStringLiteral("days")] = arr;
    }
    return obj;
}

// ============================================================
//  Действие
// ============================================================

QString TriggerThen::human() const
{
    QStringList parts;
    if (!workflow.isEmpty())
        parts << QStringLiteral("сценарий \"%1\"").arg(workflow);
    if (!tool.isEmpty())
        parts << tool;
    if (!notify.isEmpty())
        parts << QStringLiteral("сообщить: %1").arg(notify);
    return parts.join(QStringLiteral(" + "));
}

TriggerThen TriggerThen::fromJson(const QJsonObject& obj)
{
    TriggerThen t;
    t.workflow = obj.value(QStringLiteral("workflow")).toString();
    t.tool     = obj.value(QStringLiteral("tool")).toString();
    t.args     = obj.value(QStringLiteral("args")).toObject();
    t.notify   = obj.value(QStringLiteral("notify")).toString();
    return t;
}

QJsonObject TriggerThen::toJson() const
{
    QJsonObject obj;
    if (!workflow.isEmpty()) obj[QStringLiteral("workflow")] = workflow;
    if (!tool.isEmpty())     obj[QStringLiteral("tool")]     = tool;
    if (!args.isEmpty())     obj[QStringLiteral("args")]     = args;
    if (!notify.isEmpty())   obj[QStringLiteral("notify")]   = notify;
    return obj;
}

// ============================================================
//  Правило
// ============================================================

QString TriggerRule::human() const
{
    return QStringLiteral("%1%2 — когда %3, то %4")
        .arg(enabled ? QString() : QStringLiteral("(выключено) "),
             name, when.human(), then.human());
}

TriggerRule TriggerRule::fromJson(const QJsonObject& obj)
{
    TriggerRule r;
    r.name        = obj.value(QStringLiteral("name")).toString();
    r.description = obj.value(QStringLiteral("description")).toString();
    r.enabled     = obj.value(QStringLiteral("enabled")).toBool(true);
    r.when        = TriggerWhen::fromJson(obj.value(QStringLiteral("when")).toObject());
    r.then        = TriggerThen::fromJson(obj.value(QStringLiteral("then")).toObject());
    r.cooldownSeconds = obj.value(QStringLiteral("cooldown_seconds")).toInt(60);
    r.fireCount   = obj.value(QStringLiteral("fire_count")).toInt(0);

    const QString last = obj.value(QStringLiteral("last_fired")).toString();
    if (!last.isEmpty())
        r.lastFired = QDateTime::fromString(last, Qt::ISODate);
    return r;
}

QJsonObject TriggerRule::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("name")]        = name;
    obj[QStringLiteral("description")] = description;
    obj[QStringLiteral("enabled")]     = enabled;
    obj[QStringLiteral("when")]        = when.toJson();
    obj[QStringLiteral("then")]        = then.toJson();
    obj[QStringLiteral("cooldown_seconds")] = cooldownSeconds;
    if (fireCount > 0)
        obj[QStringLiteral("fire_count")] = fireCount;
    if (lastFired.isValid())
        obj[QStringLiteral("last_fired")] = lastFired.toString(Qt::ISODate);
    return obj;
}

// ============================================================
//  TriggerEngine
// ============================================================

TriggerEngine::TriggerEngine(ToolRegistry* tools,
                             PermissionGate* gate,
                             WorkflowManager* workflows,
                             QObject* parent)
    : QObject(parent)
    , m_tools(tools)
    , m_gate(gate)
    , m_workflows(workflows)
{
    load();
}

QString TriggerEngine::storagePath() const
{
    return JarvisPaths::subPath(QStringLiteral("agent/triggers.json"));
}

void TriggerEngine::load()
{
    m_rules.clear();

    QFile f(storagePath());
    if (!f.exists()) {
        // Один выключенный пример: по нему видно устройство правила,
        // и он ничего не делает, пока его не включат.
        TriggerRule sample;
        sample.name        = QStringLiteral("Unreal");
        sample.description = QStringLiteral("Пример: запуск Unreal включает сценарий Diagnostics");
        sample.enabled     = false;
        sample.when.kind    = TriggerWhen::ProcessStarted;
        sample.when.process = QStringLiteral("UnrealEditor.exe");
        sample.then.workflow = QStringLiteral("Diagnostics");
        sample.cooldownSeconds = 300;
        m_rules.append(sample);

        save();
        emit listChanged();
        return;
    }

    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[Triggers] cannot read" << storagePath() << f.errorString();
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    const QJsonArray arr = doc.isArray()
        ? doc.array()
        : doc.object().value(QStringLiteral("triggers")).toArray();

    for (const QJsonValue& v : arr) {
        const TriggerRule r = TriggerRule::fromJson(v.toObject());
        if (r.isValid())
            m_rules.append(r);
        else
            qWarning() << "[Triggers] skipped a malformed rule"
                       << v.toObject().value(QStringLiteral("name")).toString();
    }

    emit listChanged();
}

bool TriggerEngine::save() const
{
    QJsonArray arr;
    for (const TriggerRule& r : m_rules)
        arr.append(r.toJson());

    QFile f(storagePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[Triggers] cannot write" << storagePath() << f.errorString();
        return false;
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

void TriggerEngine::start()
{
    if (m_started)
        return;
    m_started = true;

    connect(&EventFeed::instance(), &EventFeed::eventPosted,
            this, &TriggerEngine::onEvent);

    m_clock = new QTimer(this);
    m_clock->setInterval(kClockMs);
    connect(m_clock, &QTimer::timeout, this, &TriggerEngine::onClockTick);

    m_procTick = new QTimer(this);
    m_procTick->setInterval(kProcMs);
    connect(m_procTick, &QTimer::timeout, this, &TriggerEngine::onProcessTick);

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &TriggerEngine::onFileChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &TriggerEngine::onFileChanged);

    // Здесь же снимается первая база сравнения для процессов и устройств:
    // без неё всё, что уже запущено, в первый тик выглядело бы как
    // «только что стартовало».
    rebuildSchedule();

    QTimer::singleShot(kStartupDelayMs, this, [this]() {
        if (!m_enabled)
            return;
        QStringList due;
        for (const TriggerRule& rule : m_rules) {
            if (rule.when.kind == TriggerWhen::Startup && ready(rule))
                due << rule.name;
        }
        for (const QString& name : due)
            runRuleNamed(name, QStringLiteral("запуск JARVIS"));
    });

    qDebug() << "[Triggers] started with" << m_rules.size() << "rules";
}

void TriggerEngine::stop()
{
    if (m_clock)    m_clock->stop();
    if (m_procTick) m_procTick->stop();
}

void TriggerEngine::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    rebuildSchedule();
}

void TriggerEngine::rebuildSchedule()
{
    if (!m_started)
        return;

    bool needClock = false;
    QStringList paths;

    m_watchProcesses = false;
    m_watchDevices   = false;

    if (m_enabled) {
        for (const TriggerRule& r : m_rules) {
            if (!r.enabled)
                continue;
            switch (r.when.kind) {
            case TriggerWhen::DailyTime:
            case TriggerWhen::Every:
                needClock = true;
                break;
            case TriggerWhen::ProcessStarted:
            case TriggerWhen::ProcessStopped:
                m_watchProcesses = true;
                break;
            case TriggerWhen::DeviceConnected:
                m_watchDevices = true;
                break;
            case TriggerWhen::FileChanged:
                if (QFileInfo::exists(r.when.path))
                    paths << r.when.path;
                else
                    qWarning() << "[Triggers]" << r.name << "watches a missing path"
                               << r.when.path;
                break;
            default:
                break;
            }
        }
    }

    if (m_clock) {
        if (needClock && !m_clock->isActive())      m_clock->start();
        else if (!needClock && m_clock->isActive()) m_clock->stop();
    }
    const bool needProc = m_watchProcesses || m_watchDevices;
    if (m_procTick) {
        if (needProc && !m_procTick->isActive()) {
            // Свежая база сравнения на момент включения: иначе всё, что
            // запустилось, пока правил не было, сойдёт за «только что».
            if (m_watchProcesses)
                m_processes = runningProcessNames();
            if (m_watchDevices && m_devices) {
                m_connected.clear();
                for (const DeviceInfo& d : m_devices->devices()) {
                    if (d.status == DeviceInfo::Status::Connected)
                        m_connected.insert(d.id);
                }
            }
            m_procTick->start();
        } else if (!needProc && m_procTick->isActive()) {
            m_procTick->stop();
        }
    }

    if (m_watcher) {
        const QStringList watched = m_watcher->files() + m_watcher->directories();
        if (!watched.isEmpty())
            m_watcher->removePaths(watched);
        if (!paths.isEmpty())
            m_watcher->addPaths(paths);
    }
}

bool TriggerEngine::ready(const TriggerRule& rule) const
{
    if (!m_enabled || m_firing || !rule.enabled || !rule.isValid())
        return false;
    if (rule.lastFired.isValid() && rule.cooldownSeconds > 0
        && rule.lastFired.secsTo(QDateTime::currentDateTime()) < rule.cooldownSeconds)
        return false;
    return true;
}

// ------------------------------------------------------------
//  Источники сигналов
// ------------------------------------------------------------

void TriggerEngine::onEvent(const FeedEvent& event)
{
    // Собственные строки в ленте игнорируем всегда: иначе правило,
    // которое сообщает о срабатывании, запускает само себя.
    if (event.source.compare(QLatin1String("TRIGGER"), Qt::CaseInsensitive) == 0)
        return;

    QStringList due;
    for (const TriggerRule& rule : m_rules) {
        if (rule.when.kind != TriggerWhen::Event || !ready(rule))
            continue;

        const TriggerWhen& w = rule.when;
        if (!w.source.isEmpty()
            && event.source.compare(w.source, Qt::CaseInsensitive) != 0)
            continue;
        if (static_cast<int>(event.level) < static_cast<int>(w.minLevel))
            continue;
        if (!w.match.isEmpty()
            && !event.title.contains(w.match, Qt::CaseInsensitive)
            && !event.detail.contains(w.match, Qt::CaseInsensitive))
            continue;

        due << rule.name;
    }

    for (const QString& name : due)
        runRuleNamed(name, QStringLiteral("событие: %1").arg(event.title));
}

void TriggerEngine::onClockTick()
{
    const QDateTime now = QDateTime::currentDateTime();

    QVector<QPair<QString, QString>> due;   // имя правила -> причина

    for (const TriggerRule& rule : m_rules) {
        if (!ready(rule))
            continue;

        if (rule.when.kind == TriggerWhen::Every) {
            const int minutes = rule.when.minutes;
            if (rule.lastFired.isValid()
                && rule.lastFired.secsTo(now) < 60ll * minutes)
                continue;
            due << qMakePair(rule.name, QStringLiteral("каждые %1 мин").arg(minutes));
            continue;
        }

        if (rule.when.kind != TriggerWhen::DailyTime)
            continue;

        const QList<int>& days = rule.when.days;
        if (!days.isEmpty() && !days.contains(now.date().dayOfWeek()))
            continue;

        // Окно, а не «время уже прошло»: иначе правило на 08:00,
        // включённое вечером, сработало бы немедленно.
        const int secsSince = rule.when.at.secsTo(now.time());
        if (secsSince < 0 || secsSince > 180)
            continue;
        if (m_firedToday.value(rule.name.toLower()) == now.date())
            continue;

        m_firedToday.insert(rule.name.toLower(), now.date());
        due << qMakePair(rule.name,
                         QStringLiteral("время %1")
                             .arg(rule.when.at.toString(QStringLiteral("HH:mm"))));
    }

    for (const auto& item : due)
        runRuleNamed(item.first, item.second);
}

void TriggerEngine::onProcessTick()
{
    QVector<QPair<QString, QString>> due;   // имя правила -> причина

    QSet<QString> now;
    QSet<QString> before;
    if (m_watchProcesses) {
        now    = runningProcessNames();
        before = m_processes;
        m_processes = now;
    }

    for (const TriggerRule& rule : m_rules) {
        if (!m_watchProcesses)
            break;
        const TriggerWhen::Kind kind = rule.when.kind;
        if (kind != TriggerWhen::ProcessStarted && kind != TriggerWhen::ProcessStopped)
            continue;
        if (!ready(rule))
            continue;

        const QString proc = normalizedProcess(rule.when.process);
        if (proc.isEmpty())
            continue;

        const bool wasThere = before.contains(proc);
        const bool isThere  = now.contains(proc);

        if (kind == TriggerWhen::ProcessStarted && !wasThere && isThere)
            due << qMakePair(rule.name,
                             QStringLiteral("запустился %1").arg(rule.when.process));
        else if (kind == TriggerWhen::ProcessStopped && wasThere && !isThere)
            due << qMakePair(rule.name,
                             QStringLiteral("закрылся %1").arg(rule.when.process));
    }

    if (!m_devices || !m_watchDevices) {
        for (const auto& item : due)
            runRuleNamed(item.first, item.second);
        return;
    }

    QSet<QString> connectedNow;
    const QVector<DeviceInfo> devices = m_devices->devices();
    for (const DeviceInfo& d : devices) {
        if (d.status == DeviceInfo::Status::Connected)
            connectedNow.insert(d.id);
    }

    for (const TriggerRule& rule : m_rules) {
        if (rule.when.kind != TriggerWhen::DeviceConnected || !ready(rule))
            continue;

        for (const DeviceInfo& d : devices) {
            if (d.status != DeviceInfo::Status::Connected)
                continue;
            if (m_connected.contains(d.id))
                continue;   // был на связи и до этого — не событие
            const QString needle = rule.when.device;
            if (d.id.compare(needle, Qt::CaseInsensitive) != 0
                && !d.name.contains(needle, Qt::CaseInsensitive))
                continue;

            due << qMakePair(rule.name,
                             QStringLiteral("подключилось: %1").arg(d.name));
            break;
        }
    }

    m_connected = connectedNow;

    for (const auto& item : due)
        runRuleNamed(item.first, item.second);
}

void TriggerEngine::onFileChanged(const QString& path)
{
    QStringList due;
    for (const TriggerRule& rule : m_rules) {
        if (rule.when.kind != TriggerWhen::FileChanged || !ready(rule))
            continue;
        if (QFileInfo(rule.when.path).absoluteFilePath()
            != QFileInfo(path).absoluteFilePath())
            continue;
        due << rule.name;
    }

    for (const QString& name : due) {
        runRuleNamed(name,
                     QStringLiteral("изменилось: %1").arg(QFileInfo(path).fileName()));
    }

    // Редактор часто сохраняет файл через replace — наблюдатель при этом
    // теряет путь, и второе изменение уже не придёт. Возвращаем.
    if (m_watcher && QFileInfo::exists(path)
        && !m_watcher->files().contains(path)
        && !m_watcher->directories().contains(path)) {
        m_watcher->addPath(path);
    }
}

// ------------------------------------------------------------
//  Срабатывание
// ------------------------------------------------------------

QString TriggerEngine::runRuleNamed(const QString& name, const QString& because)
{
    if (m_firing)
        return QStringLiteral("Another trigger is already firing.");

    const TriggerRule* found = find(name);
    if (!found)
        return QStringLiteral("No trigger named '%1'.").arg(name);

    // Работаем по копии: инструмент внутри правила может дойти до
    // save_trigger или delete_trigger и перестроить сам вектор.
    TriggerRule rule = *found;

    m_firing = true;
    rule.lastFired = QDateTime::currentDateTime();
    rule.fireCount++;

    // Всё, что запустит правило, попадёт в журнал действий с этим
    // актором — «кто это сделал» потом не придётся угадывать.
    ActionLog::Actor scope(QStringLiteral("trigger:") + rule.name);

    // Правки — одним батчем с именем правила: сработало в фоне, человек
    // этого не видел, и «что это вообще было» должно читаться из журнала.
    EditJournal::Scope edits(QStringLiteral("правило \"") + rule.name + QChar('"'));

    QStringList report;
    report << QStringLiteral("Trigger \"%1\" (%2)").arg(rule.name, because);
    bool ok = true;

    if (!rule.then.notify.isEmpty()) {
        EventFeed::instance().post(QStringLiteral("trigger"), EventLevel::Info,
                                   rule.then.notify,
                                   QStringLiteral("%1 — %2").arg(rule.name, because));
        report << QStringLiteral("  сообщено: ") + rule.then.notify;
    }

    if (!rule.then.workflow.isEmpty()) {
        if (!m_workflows) {
            report << QStringLiteral("  сценарии не подключены");
            ok = false;
        } else {
            bool wfOk = false;
            const QString wfReport = m_workflows->run(rule.then.workflow, &wfOk);
            report << QStringLiteral("  ") + wfReport.split(QChar('\n')).join(QStringLiteral("\n  "));
            ok = ok && wfOk;
        }
    }

    if (!rule.then.tool.isEmpty()) {
        const ToolSpec* spec = m_tools ? m_tools->find(rule.then.tool) : nullptr;
        if (!spec) {
            report << QStringLiteral("  неизвестный инструмент: ") + rule.then.tool;
            ok = false;
        } else if (!m_gate) {
            report << QStringLiteral("  разрешения не подключены");
            ok = false;
        } else {
            const QString summary = m_tools->describeCall(rule.then.tool, rule.then.args);
            QString reason;
            if (!m_gate->evaluateBlocking(rule.then.tool, spec->risk, summary, &reason)) {
                // Фоновое срабатывание без интерактивной сессии сюда и
                // приходит: гейт отказывает, а не ждёт никого.
                ActionLog::instance().record(rule.then.tool, rule.then.args,
                                             ActionOutcome::Denied, reason,
                                             static_cast<int>(spec->risk));
                report << QStringLiteral("  отклонено: ")
                          + (reason.isEmpty() ? QStringLiteral("нет подтверждения") : reason);
                ok = false;
            } else {
                const ToolResult res = m_tools->invoke(rule.then.tool, rule.then.args);
                report << QStringLiteral("  %1 — %2")
                              .arg(summary, res.ok ? res.display : res.text);
                ok = ok && res.ok;
            }
        }
    }

    m_firing = false;

    // Статистику пишем обратно только если правило пережило собственное
    // срабатывание и его не переписали другим содержимым.
    for (TriggerRule& stored : m_rules) {
        if (stored.name.compare(rule.name, Qt::CaseInsensitive) != 0)
            continue;
        stored.lastFired = rule.lastFired;
        stored.fireCount = rule.fireCount;
        break;
    }
    save();

    const QString text = report.join(QChar('\n'));

    if (!ok) {
        EventFeed::instance().post(
            QStringLiteral("trigger"), EventLevel::Warning,
            QStringLiteral("Правило \"%1\" не отработало").arg(rule.name),
            text.section(QChar('\n'), 1),
            QStringLiteral("trigger-failed/") + rule.name);
    } else if (rule.then.notify.isEmpty()) {
        EventFeed::instance().post(
            QStringLiteral("trigger"), EventLevel::Good,
            QStringLiteral("Правило \"%1\": %2").arg(rule.name, rule.then.human()),
            because, QStringLiteral("trigger/") + rule.name);
    }

    emit ruleFired(rule.name, ok, text);
    return text;
}

QString TriggerEngine::fire(const QString& name, bool manual, bool* okOut)
{
    if (okOut)
        *okOut = false;

    const TriggerRule* rule = find(name);
    if (!rule) {
        return QStringLiteral("No trigger named '%1'. Available: %2")
            .arg(name, names().isEmpty() ? QStringLiteral("(none)")
                                         : names().join(QStringLiteral(", ")));
    }
    if (!rule->isValid())
        return QStringLiteral("Rule '%1' is incomplete.").arg(rule->name);
    if (!manual && !ready(*rule))
        return QStringLiteral("Rule '%1' is not ready to fire.").arg(rule->name);
    if (m_firing)
        return QStringLiteral("Another trigger is already firing.");

    // Копия имени: runRuleNamed трогает m_rules, а rule указывает внутрь.
    const QString ruleName = rule->name;

    const QString report = runRuleNamed(ruleName, manual ? QStringLiteral("ручной запуск")
                                                         : QStringLiteral("условие"));
    if (okOut)
        *okOut = true;
    return report;
}

// ------------------------------------------------------------
//  Хранилище
// ------------------------------------------------------------

const TriggerRule* TriggerEngine::find(const QString& name) const
{
    for (const TriggerRule& r : m_rules) {
        if (r.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return &r;
    }
    return nullptr;
}

QStringList TriggerEngine::names() const
{
    QStringList out;
    out.reserve(m_rules.size());
    for (const TriggerRule& r : m_rules)
        out << r.name;
    return out;
}

bool TriggerEngine::addOrReplace(const TriggerRule& rule)
{
    if (!rule.isValid())
        return false;

    bool replaced = false;
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules[i].name.compare(rule.name, Qt::CaseInsensitive) != 0)
            continue;
        // Историю срабатываний правка правила не обнуляет: она про то,
        // работает ли оно вообще, а не про текущую редакцию текста.
        TriggerRule updated = rule;
        updated.lastFired = m_rules[i].lastFired;
        updated.fireCount = m_rules[i].fireCount;
        m_rules[i] = updated;
        replaced = true;
        break;
    }
    if (!replaced)
        m_rules.append(rule);

    const bool ok = save();
    rebuildSchedule();
    emit listChanged();
    return ok;
}

bool TriggerEngine::remove(const QString& name)
{
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules[i].name.compare(name.trimmed(), Qt::CaseInsensitive) != 0)
            continue;
        m_rules.remove(i);
        const bool ok = save();
        rebuildSchedule();
        emit listChanged();
        return ok;
    }
    return false;
}

bool TriggerEngine::setRuleEnabled(const QString& name, bool on)
{
    for (TriggerRule& r : m_rules) {
        if (r.name.compare(name.trimmed(), Qt::CaseInsensitive) != 0)
            continue;
        r.enabled = on;
        const bool ok = save();
        rebuildSchedule();
        emit listChanged();
        return ok;
    }
    return false;
}

QString TriggerEngine::summaryForModel() const
{
    if (m_rules.isEmpty())
        return QStringLiteral("No triggers defined yet.");

    QStringList lines;
    for (const TriggerRule& r : m_rules) {
        QString line = r.human();
        if (r.fireCount > 0) {
            line += QStringLiteral("  [сработало %1 раз, последний %2]")
                        .arg(r.fireCount)
                        .arg(r.lastFired.toString(QStringLiteral("dd.MM HH:mm")));
        }
        lines << line;
    }
    return lines.join(QChar('\n'));
}

// ============================================================
//  Инструменты
// ============================================================

namespace JarvisTools {

namespace {

// Плоская схема вместо вложенных объектов: модель заметно надёжнее
// заполняет kind + одно значение, чем структуру с ветвлением.
TriggerWhen whenFromFlatArgs(const QJsonObject& a, QString* errorOut)
{
    TriggerWhen w;
    const QString kindName = a.value(QStringLiteral("when")).toString();
    w.kind = TriggerWhen::kindFromString(kindName);
    if (w.kind == TriggerWhen::Unknown) {
        *errorOut = QStringLiteral("Unknown condition '%1'.").arg(kindName);
        return w;
    }

    const QString value = a.value(QStringLiteral("value")).toString().trimmed();

    switch (w.kind) {
    case TriggerWhen::Event:
        w.source   = value;
        w.match    = a.value(QStringLiteral("match")).toString().trimmed();
        w.minLevel = levelFromName(a.value(QStringLiteral("level")).toString(),
                                   EventLevel::Info);
        break;
    case TriggerWhen::ProcessStarted:
    case TriggerWhen::ProcessStopped:
        w.process = value;
        break;
    case TriggerWhen::DeviceConnected:
        w.device = value;
        break;
    case TriggerWhen::DailyTime:
        w.at = QTime::fromString(value, QStringLiteral("HH:mm"));
        if (!w.at.isValid())
            *errorOut = QStringLiteral("Time must look like 08:30, got '%1'.").arg(value);
        w.days = parseDays(a.value(QStringLiteral("days")).toString());
        break;
    case TriggerWhen::Every:
        w.minutes = value.toInt();
        if (w.minutes <= 0)
            *errorOut = QStringLiteral("'every' needs a number of minutes, got '%1'.").arg(value);
        break;
    case TriggerWhen::FileChanged:
        w.path = value;
        if (!QFileInfo::exists(value))
            *errorOut = QStringLiteral("Path does not exist: %1").arg(value);
        break;
    case TriggerWhen::Startup:
    case TriggerWhen::Unknown:
        break;
    }
    return w;
}

} // namespace

void registerTriggerTools(ToolRegistry& reg, TriggerEngine* engine)
{
    if (!engine) {
        qWarning() << "[Tools] registerTriggerTools: engine is null";
        return;
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("list_triggers");
        t.category    = QStringLiteral("triggers");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "List the rules that make JARVIS act on its own: \"when X happens, do Y\". "
            "Shows the condition, the action, whether the rule is enabled and how often "
            "it has fired.");
        t.schema  = ToolSchema::empty();
        t.handler = [engine](const QJsonObject&) -> ToolResult {
            return ToolResult::success(engine->summaryForModel(),
                                       QStringLiteral("Правил: %1").arg(engine->count()));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("save_trigger");
        t.category    = QStringLiteral("triggers");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Create or replace a rule that runs something automatically when a "
            "condition is met - \"when I plug in the ESP32, open the serial monitor\", "
            "\"at 08:00 start my Development workflow\", \"every 30 minutes back up the "
            "project\". The action itself still goes through the normal permission "
            "check when the rule fires, and a rule that needs confirmation will be "
            "declined if it fires while nobody is at the desktop app.");
        t.schema = ToolSchema()
                       .str("name", "Short rule name, e.g. Unreal or MorningStart")
                       .choice("when", { QStringLiteral("event"),
                                         QStringLiteral("process_started"),
                                         QStringLiteral("process_stopped"),
                                         QStringLiteral("device_connected"),
                                         QStringLiteral("daily_time"),
                                         QStringLiteral("every"),
                                         QStringLiteral("startup"),
                                         QStringLiteral("file_changed") },
                               "What kind of condition")
                       .str("value",
                            "The condition's value: process name for process_*, device "
                            "name for device_connected, HH:mm for daily_time, minutes for "
                            "every, path for file_changed, event source (or empty for any) "
                            "for event. Ignored for startup.", false)
                       .str("match", "event only: substring the event text must contain", false)
                       .choice("level", { QStringLiteral("info"), QStringLiteral("ok"),
                                          QStringLiteral("warning"), QStringLiteral("error") },
                               "event only: minimum event level", false)
                       .str("days", "daily_time only: mon,tue,wed... (empty = every day)", false)
                       .str("description", "One line describing the rule", false)
                       .str("then_workflow", "Name of a workflow to run", false)
                       .str("then_tool", "Name of a single tool to call", false)
                       .raw("then_args", QJsonObject{
                                { QStringLiteral("type"), QStringLiteral("object") },
                                { QStringLiteral("description"),
                                  QStringLiteral("Arguments for then_tool") } }, false)
                       .str("then_notify", "Text to put in the event feed when it fires", false)
                       .integer("cooldown_seconds",
                                "Minimum seconds between two firings (default 60)", false)
                       .boolean("enabled", "Active right away (default true)", false)
                       .build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Сохранить правило \"%1\": когда %2")
                .arg(a.value(QStringLiteral("name")).toString(),
                     a.value(QStringLiteral("when")).toString());
        };
        t.handler = [engine, &reg](const QJsonObject& a) -> ToolResult {
            TriggerRule rule;
            rule.name        = a.value(QStringLiteral("name")).toString().trimmed();
            rule.description = a.value(QStringLiteral("description")).toString();
            rule.enabled     = a.value(QStringLiteral("enabled")).toBool(true);
            rule.cooldownSeconds =
                qMax(0, a.value(QStringLiteral("cooldown_seconds")).toInt(60));

            if (rule.name.isEmpty())
                return ToolResult::failure(QStringLiteral("A rule needs a name."));

            QString error;
            rule.when = whenFromFlatArgs(a, &error);
            if (!error.isEmpty())
                return ToolResult::failure(error);
            if (!rule.when.isValid())
                return ToolResult::failure(
                    QStringLiteral("Condition '%1' needs a value.")
                        .arg(TriggerWhen::kindName(rule.when.kind)));

            rule.then.workflow = a.value(QStringLiteral("then_workflow")).toString().trimmed();
            rule.then.tool     = a.value(QStringLiteral("then_tool")).toString().trimmed();
            rule.then.args     = a.value(QStringLiteral("then_args")).toObject();
            rule.then.notify   = a.value(QStringLiteral("then_notify")).toString().trimmed();

            if (!rule.then.isValid())
                return ToolResult::failure(QStringLiteral(
                    "A rule needs something to do: then_workflow, then_tool or then_notify."));

            // Ссылку на несуществующий инструмент ловим сейчас: правило,
            // молча ломающееся через неделю в фоне, найти почти нечем.
            if (!rule.then.tool.isEmpty() && !reg.contains(rule.then.tool))
                return ToolResult::failure(
                    QStringLiteral("Unknown tool '%1'. Available: %2")
                        .arg(rule.then.tool, reg.names().join(QStringLiteral(", "))));

            if (!engine->addOrReplace(rule))
                return ToolResult::failure(QStringLiteral("Could not save the rule."));

            return ToolResult::success(
                QStringLiteral("Saved trigger: %1").arg(rule.human()),
                QStringLiteral("Правило \"%1\" сохранено").arg(rule.name));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("enable_trigger");
        t.category    = QStringLiteral("triggers");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Turn an existing rule on or off without deleting it.");
        t.schema = ToolSchema()
                       .str("name", "Rule name")
                       .boolean("enabled", "true to switch it on, false to switch it off")
                       .build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("%1 правило \"%2\"")
                .arg(a.value(QStringLiteral("enabled")).toBool(true)
                         ? QStringLiteral("Включить") : QStringLiteral("Выключить"),
                     a.value(QStringLiteral("name")).toString());
        };
        t.handler = [engine](const QJsonObject& a) -> ToolResult {
            const QString name = a.value(QStringLiteral("name")).toString().trimmed();
            const bool on = a.value(QStringLiteral("enabled")).toBool(true);
            if (!engine->setRuleEnabled(name, on))
                return ToolResult::failure(QStringLiteral("No trigger named '%1'.").arg(name));
            return ToolResult::success(
                QStringLiteral("Trigger '%1' is now %2").arg(name,
                    on ? QStringLiteral("enabled") : QStringLiteral("disabled")),
                QStringLiteral("Правило \"%1\": %2").arg(name,
                    on ? QStringLiteral("включено") : QStringLiteral("выключено")));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("delete_trigger");
        t.category    = QStringLiteral("triggers");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral("Delete an automation rule by name.");
        t.schema  = ToolSchema().str("name", "Rule name").build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Удалить правило \"%1\"")
                .arg(a.value(QStringLiteral("name")).toString());
        };
        t.handler = [engine](const QJsonObject& a) -> ToolResult {
            const QString name = a.value(QStringLiteral("name")).toString().trimmed();
            if (!engine->remove(name))
                return ToolResult::failure(QStringLiteral("No trigger named '%1'.").arg(name));
            return ToolResult::success(QStringLiteral("Deleted trigger '%1'").arg(name),
                                       QStringLiteral("Правило удалено"));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("test_trigger");
        t.category    = QStringLiteral("triggers");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Run a rule's action right now, without waiting for its condition. Use it "
            "to show the user what a rule they just described will actually do.");
        t.schema  = ToolSchema().str("name", "Rule name").build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Проверить правило \"%1\"")
                .arg(a.value(QStringLiteral("name")).toString());
        };
        t.handler = [engine](const QJsonObject& a) -> ToolResult {
            const QString name = a.value(QStringLiteral("name")).toString().trimmed();
            bool ok = false;
            const QString report = engine->fire(name, true, &ok);
            return ok ? ToolResult::success(report,
                                            QStringLiteral("Правило \"%1\" проверено").arg(name))
                      : ToolResult::failure(report);
        };
        reg.registerTool(t);
    }
}

} // namespace JarvisTools
