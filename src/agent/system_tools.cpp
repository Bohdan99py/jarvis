// -------------------------------------------------------
// system_tools.cpp — см. system_tools.h
// -------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN     // без этого rpcndr.h делает #define small char
#endif

#include "system_tools.h"
#include "tool_registry.h"

#include "context_tracker.h"
#include "edit_journal.h"
#include "event_feed.h"
#include "pc_controller.h"
#include "applauncher.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QPixmap>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QDebug>

#include <algorithm>
#include <functional>
#include <memory>

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

namespace {

// ============================================================
//  Мелкие помощники
// ============================================================

QString arg(const QJsonObject& o, const char* key, const QString& def = QString())
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? v.toString() : def;
}

int argInt(const QJsonObject& o, const char* key, int def)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isDouble()) return static_cast<int>(v.toDouble());
    if (v.isString()) {
        bool ok = false;
        const int n = v.toString().toInt(&ok);
        if (ok) return n;
    }
    return def;
}

bool argBool(const QJsonObject& o, const char* key, bool def)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isBool()) return v.toBool();
    if (v.isString()) return v.toString().compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    return def;
}

QString humanBytes(qulonglong bytes)
{
    static const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = static_cast<double>(bytes);
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    return QString::number(v, 'f', v < 10.0 ? 1 : 0) + QLatin1Char(' ') + QLatin1String(units[i]);
}

// Мгновенная загрузка CPU: два замера GetSystemTimes с паузой.
// Пауза короткая (150 мс) и блокирующая — этого достаточно для
// осмысленной цифры и незаметно на фоне сетевого запроса к модели.
int cpuLoadPercent()
{
    auto toU64 = [](const FILETIME& ft) {
        return (static_cast<quint64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };

    FILETIME idle1, kernel1, user1, idle2, kernel2, user2;
    if (!GetSystemTimes(&idle1, &kernel1, &user1))
        return -1;
    Sleep(150);
    if (!GetSystemTimes(&idle2, &kernel2, &user2))
        return -1;

    const quint64 idle   = toU64(idle2)   - toU64(idle1);
    const quint64 kernel = toU64(kernel2) - toU64(kernel1);
    const quint64 user   = toU64(user2)   - toU64(user1);
    const quint64 total  = kernel + user;
    if (total == 0)
        return -1;
    return static_cast<int>((total - idle) * 100 / total);
}

struct ProcInfo {
    QString  name;
    quint32  pid  = 0;
    quint64  memBytes = 0;
};

QVector<ProcInfo> snapshotProcesses()
{
    QVector<ProcInfo> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return out;

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snap, &entry)) {
        do {
            ProcInfo p;
            p.name = QString::fromWCharArray(entry.szExeFile);
            p.pid  = entry.th32ProcessID;

            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                                   FALSE, entry.th32ProcessID);
            if (h) {
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc)))
                    p.memBytes = pmc.WorkingSetSize;
                CloseHandle(h);
            }
            out.append(p);
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return out;
}

// Процессы, которые нельзя убивать ни по чьей просьбе: их смерть
// либо роняет Windows, либо роняет самого JARVIS вместе с диалогом
// подтверждения.
bool isProtectedProcess(const QString& nameLower)
{
    static const QStringList kProtected = {
        QStringLiteral("system"),      QStringLiteral("smss.exe"),
        QStringLiteral("csrss.exe"),   QStringLiteral("wininit.exe"),
        QStringLiteral("winlogon.exe"),QStringLiteral("services.exe"),
        QStringLiteral("lsass.exe"),   QStringLiteral("svchost.exe"),
        QStringLiteral("dwm.exe"),     QStringLiteral("jarvis.exe")
    };
    return kProtected.contains(nameLower);
}

// ============================================================
//  Помощники проверки постусловий
// ============================================================

// Ждёт наступления условия, не замораживая интерфейс. Первая проверка
// делается сразу: приложение могло стартовать мгновенно, и платить за
// это лишними 250 мс задержки на каждом вызове незачем.
//
// Ожидание здесь блокирует агентный цикл — так и задумано: инструменты
// выполняются строго по одному, и следующий шаг всё равно не начнётся,
// пока не станет известен результат предыдущего.
bool waitFor(const std::function<bool()>& condition, int timeoutMs, int pollMs = 250)
{
    if (condition())
        return true;

    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutMs) {
        QEventLoop loop;
        QTimer::singleShot(pollMs, &loop, &QEventLoop::quit);
        loop.exec();
        if (condition())
            return true;
    }
    return false;
}

// Точное совпадение имени процесса ("UnrealEditor.exe").
bool processExists(const QString& exeName)
{
    if (exeName.isEmpty())
        return false;
    for (const ProcInfo& p : snapshotProcesses()) {
        if (p.name.compare(exeName, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool pidAlive(quint32 pid)
{
    for (const ProcInfo& p : snapshotProcesses()) {
        if (p.pid == pid)
            return true;
    }
    return false;
}

// Приблизительное совпадение: пользователь говорит "unreal", процесс
// называется "UnrealEditor.exe". Возвращает имя найденного процесса
// или пустую строку.
//
// Это именно догадка, и вызывающий обязан обращаться с ней как с
// догадкой: ненайденный процесс здесь — повод сказать «не вижу», а
// не повод объявить, что приложение не запустилось.
QString findProcessLike(const QString& hint)
{
    const QString stem = QFileInfo(hint).completeBaseName().toLower().trimmed();
    if (stem.isEmpty())
        return QString();

    for (const ProcInfo& p : snapshotProcesses()) {
        const QString procStem = QFileInfo(p.name).completeBaseName().toLower();
        if (procStem == stem || procStem.startsWith(stem) || stem.startsWith(procStem))
            return p.name;
    }
    return QString();
}

QString runShell(const QString& command, const QString& workingDir, int timeoutSec, bool* okOut)
{
    QProcess proc;
    if (!workingDir.isEmpty() && QFileInfo(workingDir).isDir())
        proc.setWorkingDirectory(workingDir);
    proc.setProcessChannelMode(QProcess::MergedChannels);

    proc.start(QStringLiteral("cmd.exe"),
               { QStringLiteral("/c"), command });

    if (!proc.waitForStarted(5000)) {
        if (okOut) *okOut = false;
        return QStringLiteral("Failed to start the shell.");
    }

    // Не waitForFinished(): он замораживает UI, а вместе с ним и
    // диалог подтверждения следующего шага. Локальный event loop
    // держит интерфейс живым.
    QEventLoop loop;
    bool timedOut = false;
    QTimer killTimer;
    killTimer.setSingleShot(true);
    QObject::connect(&killTimer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        proc.kill();
        loop.quit();
    });
    QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &loop, &QEventLoop::quit);
    killTimer.start(qBound(1, timeoutSec, 600) * 1000);
    loop.exec();
    killTimer.stop();
    proc.waitForFinished(2000);

    QString output = QString::fromLocal8Bit(proc.readAll()).trimmed();
    if (output.length() > 6000)
        output = output.left(6000) + QStringLiteral("\n... [output truncated]");

    const int code = proc.exitCode();
    if (okOut)
        *okOut = !timedOut && code == 0;

    QString header;
    if (timedOut)
        header = QStringLiteral("[killed after %1s timeout]\n").arg(timeoutSec);
    else
        header = QStringLiteral("[exit code %1]\n").arg(code);

    return header + (output.isEmpty() ? QStringLiteral("(no output)") : output);
}

QString formatWindow(const WindowInfo& w)
{
    QString state = w.isMinimized ? QStringLiteral("minimized")
                  : w.isMaximized ? QStringLiteral("maximized")
                                  : QStringLiteral("normal");
    return QStringLiteral("%1  [pid %2, %3, %4x%5]")
        .arg(w.title)
        .arg(w.pid)
        .arg(state)
        .arg(w.rect.width())
        .arg(w.rect.height());
}

} // namespace

// ============================================================
//  Регистрация
// ============================================================

namespace JarvisTools {

void registerSystemTools(ToolRegistry& reg, PcController* pc)
{
    if (!pc) {
        qWarning() << "[Tools] registerSystemTools: PcController is null";
        return;
    }

    // AppLauncher — по значению внутри лямбд: он дешёвый (таблица
    // алиасов строится в конструкторе) и не имеет состояния между
    // вызовами. Держим один общий через shared_ptr.
    auto launcher = std::make_shared<AppLauncher>();

    // --------------------------------------------------------
    //  apps
    // --------------------------------------------------------
    {
        ToolSpec t;
        t.name        = QStringLiteral("launch_app");
        t.category    = QStringLiteral("apps");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "Launch a desktop application by name (steam, chrome, rider, unreal, "
            "notepad, calc, code, ...). Resolves aliases, the Windows registry and PATH. "
            "Use 'args' to pass command line arguments, e.g. a project path. "
            "If the application is already running, this switches to its window instead "
            "of starting a second copy - so never propose 'open X' as if X were closed "
            "without checking get_context or list_processes first.");
        t.schema = ToolSchema()
                       .str("name", "Application name or full path to the .exe")
                       .str("args", "Optional command line arguments", false)
                       .build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Запустить %1").arg(arg(a, "name"));
        };
        // Имя .exe, до которого лаунчер раскрыл алиас. Нужно проверке:
        // "rider" — это то, что сказал человек, а искать в списке
        // процессов придётся "rider64.exe". Пустая строка = алиас не
        // раскрылся, запускали через ShellExecute вслепую.
        //
        // Ячейка одна на пару handler/verify, и между ними никто её не
        // перепишет: цикла событий на этом отрезке не крутится. А вот
        // ВНУТРИ проверки крутится — waitFor его и крутит, — поэтому
        // значение копируется в локальную переменную до первого
        // ожидания. Сработавший в это время триггер запишет в ячейку
        // своё, и это уже не будет иметь значения.
        auto resolvedExe = std::make_shared<QString>();

        t.handler = [pc, launcher, resolvedExe](const QJsonObject& a) -> ToolResult {
            const QString name = arg(a, "name").trimmed();
            const QString args = arg(a, "args");
            resolvedExe->clear();
            if (name.isEmpty())
                return ToolResult::failure(QStringLiteral("name is required"));

            // Уже запущенное не запускают заново. Раньше этой проверки не
            // было, и на «открой Firefox» при открытом Firefox JARVIS
            // честно звал ShellExecute — человек видел предложение открыть
            // то, на что он в этот момент смотрит.
            //
            // Только без аргументов: «открой проект в Rider» — это просьба
            // сделать что-то с уже запущенным приложением, а не просьба
            // переключиться на него.
            if (args.isEmpty()) {
                const QString running = findProcessLike(name);
                if (!running.isEmpty()) {
                    *resolvedExe = running;
                    const bool focused = pc->windows()->focusWindow(name);
                    return ToolResult::success(
                        QStringLiteral("%1 is already running (%2)%3")
                            .arg(name, running,
                                 focused ? QStringLiteral(", switched to its window")
                                         : QStringLiteral(", no window to switch to")),
                        focused ? QStringLiteral("%1 уже запущен — переключился").arg(name)
                                : QStringLiteral("%1 уже запущен").arg(name));
                }
            }

            if (args.isEmpty()) {
                const AppLauncher::LaunchResult r = launcher->launch(name);
                if (r.success) {
                    *resolvedExe = QFileInfo(r.resolvedPath).fileName();
                    return ToolResult::success(
                        QStringLiteral("Launched %1 (%2)").arg(name, r.resolvedPath),
                        QStringLiteral("%1 запущен").arg(name));
                }
            }
            // С аргументами (или если алиас не нашёлся) — через ShellExecute
            if (pc->system()->launchApp(name, args)) {
                if (name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
                    *resolvedExe = QFileInfo(name).fileName();
                return ToolResult::success(QStringLiteral("Launched %1 %2").arg(name, args),
                                           QStringLiteral("%1 запущен").arg(name));
            }
            return ToolResult::failure(
                QStringLiteral("Could not find or launch '%1'. Try a full path to the .exe.")
                    .arg(name));
        };

        // ShellExecute возвращает успех, как только система приняла
        // запрос на запуск, — то есть примерно ни о чём. Здесь мы
        // ждём, пока процесс появится в списке.
        t.verify = [resolvedExe](const QJsonObject& a, const ToolResult&) -> ToolVerdict {
            const QString name = arg(a, "name").trimmed();

            // Случай первый: точное имя .exe известно. Тогда отсутствие
            // процесса — это факт, а не догадка, и врать про успех нельзя.
            if (!resolvedExe->isEmpty()) {
                const QString exe = *resolvedExe;
                if (waitFor([exe]() { return processExists(exe); }, 8000))
                    return ToolVerdict::confirmed(QStringLiteral("процесс %1 работает").arg(exe));
                return ToolVerdict::contradicted(
                    QStringLiteral("процесс %1 не появился за 8 секунд — приложение не "
                                   "запустилось или закрылось сразу после старта").arg(exe));
            }

            // Случай второй: запускали по имени, которое не раскрылось в
            // путь. Совпадение ищется по основе имени, и это догадка —
            // приложение вправе называть свой процесс как угодно.
            // Поэтому максимум, что тут допустимо, — сказать «не вижу».
            QString found;
            waitFor([&found, name]() {
                found = findProcessLike(name);
                return !found.isEmpty();
            }, 8000);

            if (!found.isEmpty())
                return ToolVerdict::confirmed(QStringLiteral("процесс %1 работает").arg(found));
            return ToolVerdict::partial(
                QStringLiteral("процесса, похожего на '%1', в списке нет; проверить точно — "
                               "list_processes или list_windows").arg(name));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("open_url");
        t.category    = QStringLiteral("apps");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "Open a URL in the default browser. Also the way to run a web search: "
            "pass a search engine URL with the query.");
        t.schema = ToolSchema().str("url", "Full URL including https://").build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Открыть %1").arg(arg(a, "url"));
        };
        t.handler = [pc](const QJsonObject& a) -> ToolResult {
            const QString url = arg(a, "url").trimmed();
            if (url.isEmpty())
                return ToolResult::failure(QStringLiteral("url is required"));
            if (pc->system()->launchUrl(url))
                return ToolResult::success(QStringLiteral("Opened %1").arg(url),
                                           QStringLiteral("Открыт %1").arg(url.left(60)));
            return ToolResult::failure(QStringLiteral("Failed to open %1").arg(url));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("open_path");
        t.category    = QStringLiteral("apps");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "Open a file or folder with its default application (Explorer for folders).");
        t.schema = ToolSchema().str("path", "Absolute path to a file or folder").build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Открыть %1").arg(arg(a, "path"));
        };
        t.handler = [pc](const QJsonObject& a) -> ToolResult {
            const QString path = arg(a, "path").trimmed();
            if (!QFileInfo::exists(path))
                return ToolResult::failure(QStringLiteral("Path does not exist: %1").arg(path));
            if (pc->system()->openPath(path))
                return ToolResult::success(QStringLiteral("Opened %1").arg(path),
                                           QStringLiteral("Открыт %1").arg(QFileInfo(path).fileName()));
            return ToolResult::failure(QStringLiteral("Failed to open %1").arg(path));
        };
        reg.registerTool(t);
    }

    // --------------------------------------------------------
    //  windows
    // --------------------------------------------------------
    {
        ToolSpec t;
        t.name        = QStringLiteral("list_windows");
        t.category    = QStringLiteral("windows");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "List the visible top-level windows (title, pid, state, size) and mark the "
            "active one. Use this to find out what the user currently has open.");
        t.schema  = ToolSchema::empty();
        t.handler = [pc](const QJsonObject&) -> ToolResult {
            const QList<WindowInfo> all = pc->windows()->allWindows();
            const WindowInfo active = pc->windows()->activeWindow();

            QStringList lines;
            for (const WindowInfo& w : all) {
                if (w.title.trimmed().isEmpty())
                    continue;
                const bool isActive = (w.hwnd == active.hwnd);
                lines << (isActive ? QStringLiteral("* ") : QStringLiteral("  ")) + formatWindow(w);
            }
            if (lines.isEmpty())
                return ToolResult::success(QStringLiteral("No visible windows."),
                                           QStringLiteral("Окон нет"));
            return ToolResult::success(
                QStringLiteral("Active window is marked with *.\n") + lines.join(QChar('\n')),
                QStringLiteral("Окон: %1").arg(lines.size()));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("focus_window");
        t.category    = QStringLiteral("windows");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "Bring a window to the front by a part of its title (case-insensitive).");
        t.schema  = ToolSchema().str("title", "Part of the window title").build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Переключиться на окно \"%1\"").arg(arg(a, "title"));
        };
        t.handler = [pc](const QJsonObject& a) -> ToolResult {
            const QString title = arg(a, "title").trimmed();
            if (pc->windows()->focusWindow(title))
                return ToolResult::success(QStringLiteral("Focused window matching '%1'").arg(title),
                                           QStringLiteral("Фокус: %1").arg(title));
            return ToolResult::failure(
                QStringLiteral("No window matching '%1'. Call list_windows to see what is open.")
                    .arg(title));
        };
        // SetForegroundWindow — просьба, а не приказ: Windows отклоняет
        // её, когда окно-инициатор не владеет фокусом ввода, и молча
        // мигает кнопкой в панели задач вместо переключения.
        t.verify = [pc](const QJsonObject& a, const ToolResult&) -> ToolVerdict {
            const QString title = arg(a, "title").trimmed();
            if (title.isEmpty())
                return ToolVerdict();

            QString active;
            const bool ok = waitFor([pc, title, &active]() {
                active = pc->windows()->activeWindow().title;
                return active.contains(title, Qt::CaseInsensitive);
            }, 1500, 200);

            if (ok)
                return ToolVerdict::confirmed(QStringLiteral("активно окно «%1»").arg(active));
            return ToolVerdict::contradicted(
                QStringLiteral("активным осталось окно «%1» — Windows отклонила переключение")
                    .arg(active.isEmpty() ? QStringLiteral("(без заголовка)") : active));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("window_action");
        t.category    = QStringLiteral("windows");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Minimize, maximize, restore, close or pin a window. Leave 'title' empty to "
            "act on the currently active window. Closing a window may lose unsaved work.");
        t.schema = ToolSchema()
                       .choice("action", { QStringLiteral("minimize"), QStringLiteral("maximize"),
                                           QStringLiteral("restore"),  QStringLiteral("close"),
                                           QStringLiteral("always_on_top"), QStringLiteral("normal_z") },
                               "What to do with the window")
                       .str("title", "Part of the window title; empty = active window", false)
                       .build();
        t.preview = [](const QJsonObject& a) {
            const QString title = arg(a, "title");
            return QStringLiteral("%1: окно %2")
                .arg(arg(a, "action"), title.isEmpty() ? QStringLiteral("(активное)") : title);
        };
        t.handler = [pc](const QJsonObject& a) -> ToolResult {
            const QString action = arg(a, "action").toLower();
            const QString title  = arg(a, "title").trimmed();
            WindowController* w  = pc->windows();

            bool ok = false;
            if (title.isEmpty()) {
                if      (action == QLatin1String("minimize"))      ok = w->minimizeActive();
                else if (action == QLatin1String("maximize"))      ok = w->maximizeActive();
                else if (action == QLatin1String("close"))         ok = w->closeActive();
                else if (action == QLatin1String("always_on_top")) ok = w->alwaysOnTopActive(true);
                else if (action == QLatin1String("normal_z"))      ok = w->alwaysOnTopActive(false);
                else if (action == QLatin1String("restore"))       ok = w->maximizeActive();
            } else {
                if      (action == QLatin1String("minimize"))      ok = w->minimizeWindow(title);
                else if (action == QLatin1String("maximize"))      ok = w->maximizeWindow(title);
                else if (action == QLatin1String("restore"))       ok = w->restoreWindow(title);
                else if (action == QLatin1String("close"))         ok = w->closeWindow(title);
                else if (action == QLatin1String("always_on_top")) ok = w->setAlwaysOnTop(title, true);
                else if (action == QLatin1String("normal_z"))      ok = w->setAlwaysOnTop(title, false);
            }
            if (ok)
                return ToolResult::success(QStringLiteral("%1 done").arg(action),
                                           QStringLiteral("%1: %2").arg(action,
                                               title.isEmpty() ? QStringLiteral("активное окно") : title));
            return ToolResult::failure(QStringLiteral("Could not %1 window '%2'").arg(action, title));
        };
        // Проверяется только close: он единственный необратим, и он же
        // единственный, чей отказ выглядит как успех — WM_CLOSE уходит,
        // приложение показывает «сохранить изменения?» и никуда не
        // девается. Свернуть или развернуть окно либо получилось, либо
        // вернуло false; ждать секунду ради этого не за чем.
        t.verify = [pc](const QJsonObject& a, const ToolResult&) -> ToolVerdict {
            if (arg(a, "action").toLower() != QLatin1String("close"))
                return ToolVerdict();

            const QString title = arg(a, "title").trimmed();
            if (title.isEmpty())
                return ToolVerdict();   // закрывали активное — сверять не с чем

            const bool gone = waitFor([pc, title]() {
                return pc->windows()->findWindow(title).hwnd == nullptr;
            }, 3000, 300);

            if (gone)
                return ToolVerdict::confirmed(QStringLiteral("окно «%1» закрыто").arg(title));
            return ToolVerdict::partial(
                QStringLiteral("окно «%1» ещё открыто — вероятно, приложение спрашивает про "
                               "несохранённые изменения и ждёт человека").arg(title));
        };
        reg.registerTool(t);
    }

    // --------------------------------------------------------
    //  files
    // --------------------------------------------------------
    {
        ToolSpec t;
        t.name        = QStringLiteral("find_files");
        t.category    = QStringLiteral("files");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "Find files by name pattern (wildcards allowed: *.uproject, main.cpp). "
            "Searches recursively from 'root'. Use this before opening a project you "
            "do not know the path of.");
        t.schema = ToolSchema()
                       .str("pattern", "File name or wildcard pattern")
                       .str("root", "Folder to search in; default C:\\", false)
                       .integer("max_results", "Maximum number of hits (default 15)", false)
                       .build();
        t.handler = [pc](const QJsonObject& a) -> ToolResult {
            const QString pattern = arg(a, "pattern").trimmed();
            if (pattern.isEmpty())
                return ToolResult::failure(QStringLiteral("pattern is required"));
            const QString root = arg(a, "root", QStringLiteral("C:\\"));
            const int maxN     = qBound(1, argInt(a, "max_results", 15), 100);

            const QStringList hits = pc->system()->findFiles(pattern, root, maxN);
            if (hits.isEmpty())
                return ToolResult::success(
                    QStringLiteral("No files matching '%1' under %2").arg(pattern, root),
                    QStringLiteral("Ничего не найдено"));
            return ToolResult::success(hits.join(QChar('\n')),
                                       QStringLiteral("Найдено файлов: %1").arg(hits.size()));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("list_directory");
        t.category    = QStringLiteral("files");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral("List the contents of a folder (names, sizes, modified time).");
        t.schema = ToolSchema().str("path", "Absolute folder path").build();
        t.handler = [](const QJsonObject& a) -> ToolResult {
            const QString path = arg(a, "path").trimmed();
            QDir dir(path);
            if (!dir.exists())
                return ToolResult::failure(QStringLiteral("No such folder: %1").arg(path));

            const QFileInfoList entries = dir.entryInfoList(
                QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);

            QStringList lines;
            int shown = 0;
            for (const QFileInfo& fi : entries) {
                if (++shown > 200) {
                    lines << QStringLiteral("... (%1 more entries)").arg(entries.size() - 200);
                    break;
                }
                lines << QStringLiteral("%1%2  %3  %4")
                             .arg(fi.fileName(),
                                  fi.isDir() ? QStringLiteral("/") : QString(),
                                  fi.isDir() ? QStringLiteral("-") : humanBytes(fi.size()),
                                  fi.lastModified().toString(QStringLiteral("yyyy-MM-dd hh:mm")));
            }
            if (lines.isEmpty())
                lines << QStringLiteral("(empty)");
            return ToolResult::success(lines.join(QChar('\n')),
                                       QStringLiteral("%1: %2 элементов").arg(dir.dirName()).arg(entries.size()));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("read_file");
        t.category    = QStringLiteral("files");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "Read a text file (source code, config, log). Returns at most max_bytes "
            "characters; for logs use tail=true to read the END of the file.");
        t.schema = ToolSchema()
                       .str("path", "Absolute file path")
                       .integer("max_bytes", "Maximum characters to return (default 40000)", false)
                       .boolean("tail", "Read the end of the file instead of the beginning", false)
                       .build();
        t.handler = [](const QJsonObject& a) -> ToolResult {
            const QString path = arg(a, "path").trimmed();
            QFile f(path);
            if (!f.exists())
                return ToolResult::failure(QStringLiteral("No such file: %1").arg(path));
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                return ToolResult::failure(QStringLiteral("Cannot open %1: %2").arg(path, f.errorString()));

            const int maxBytes = qBound(500, argInt(a, "max_bytes", 40000), 200000);
            QString content = QString::fromUtf8(f.readAll());
            f.close();

            const int total = content.length();
            if (total > maxBytes) {
                content = argBool(a, "tail", false)
                              ? QStringLiteral("... [%1 chars skipped]\n").arg(total - maxBytes)
                                    + content.right(maxBytes)
                              : content.left(maxBytes)
                                    + QStringLiteral("\n... [%1 chars truncated]").arg(total - maxBytes);
            }
            return ToolResult::success(content,
                                       QStringLiteral("Прочитан %1 (%2)")
                                           .arg(QFileInfo(path).fileName(), humanBytes(QFileInfo(path).size())));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("write_file");
        t.category    = QStringLiteral("files");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Write text to a file, creating parent folders if needed. Overwrites by "
            "default - set append=true to add to the end instead. Always read_file "
            "first if the file already exists.");
        t.schema = ToolSchema()
                       .str("path", "Absolute file path")
                       .str("content", "Full text to write")
                       .boolean("append", "Append instead of overwriting", false)
                       .build();
        t.preview = [](const QJsonObject& a) {
            const QString path = arg(a, "path");
            const bool exists  = QFileInfo::exists(path);
            const int chars    = arg(a, "content").length();
            return QStringLiteral("%1 %2 (%3 симв.)")
                .arg(argBool(a, "append", false) ? QStringLiteral("Дописать в")
                     : exists                    ? QStringLiteral("ПЕРЕЗАПИСАТЬ")
                                                 : QStringLiteral("Создать"),
                     path)
                .arg(chars);
        };
        t.handler = [](const QJsonObject& a) -> ToolResult {
            const QString path    = arg(a, "path").trimmed();
            const QString content = arg(a, "content");
            if (path.isEmpty())
                return ToolResult::failure(QStringLiteral("path is required"));

            // До сих пор через реестр файл перезаписывался БЕЗ копии:
            // журнал знал только о правках из чата. Снимаем копию до
            // записи — иначе возвращать будет нечего.
            EditJournal::Scope batch(QStringLiteral("инструмент write_file"));
            EditJournal::instance().recordCreate(path);   // существует -> запишется как Modified

            QDir().mkpath(QFileInfo(path).absolutePath());
            const bool append = argBool(a, "append", false);
            QFile f(path);
            const QIODevice::OpenMode mode = QIODevice::WriteOnly | QIODevice::Text
                                             | (append ? QIODevice::Append : QIODevice::Truncate);
            if (!f.open(mode)) {
                // Запись не состоялась — снимаем запись о ней, иначе
                // «отмени» отчитается о возврате файла, который не менялся.
                EditJournal::instance().discardLastOp();
                return ToolResult::failure(QStringLiteral("Cannot write %1: %2").arg(path, f.errorString()));
            }
            f.write(content.toUtf8());
            f.close();
            return ToolResult::success(
                QStringLiteral("Wrote %1 characters to %2").arg(content.length()).arg(path),
                QStringLiteral("Записан %1").arg(QFileInfo(path).fileName()));
        };
        // QFile::write() возвращает, сколько байт удалось записать, и его
        // здесь никто не проверяет: на полном диске файл молча окажется
        // обрезанным, а инструмент отчитается об успехе. Сверяем размер.
        t.verify = [](const QJsonObject& a, const ToolResult&) -> ToolVerdict {
            const QString path = arg(a, "path").trimmed();
            const qint64 expected = arg(a, "content").toUtf8().size();

            const QFileInfo fi(path);
            if (!fi.exists())
                return ToolVerdict::contradicted(
                    QStringLiteral("файла %1 на диске нет").arg(path));

            const qint64 actual = fi.size();
            if (argBool(a, "append", false)) {
                // При дописывании известен только нижний предел: что было
                // в файле до нас, инструмент не измерял.
                if (actual >= expected)
                    return ToolVerdict::confirmed(
                        QStringLiteral("файл %1, %2 байт").arg(fi.fileName()).arg(actual));
                return ToolVerdict::partial(
                    QStringLiteral("в файле %1 байт — меньше, чем дописывали (%2)")
                        .arg(actual).arg(expected));
            }

            if (actual == expected)
                return ToolVerdict::confirmed(
                    QStringLiteral("файл %1, %2 байт").arg(fi.fileName()).arg(actual));
            return ToolVerdict::partial(
                QStringLiteral("на диске %1 байт вместо %2 — запись оборвалась (нет места?)")
                    .arg(actual).arg(expected));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("file_operation");
        t.category    = QStringLiteral("files");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Create a folder, or move / copy / rename a file. Restricted to the user "
            "folders (Downloads, Desktop, Documents, Pictures) - system paths are refused.");
        t.schema = ToolSchema()
                       .choice("operation", { QStringLiteral("create_folder"), QStringLiteral("move"),
                                              QStringLiteral("copy"), QStringLiteral("rename") },
                               "Which operation to perform")
                       .str("source", "Source path (or the folder path for create_folder)")
                       .str("destination", "Destination path, or the new name for rename", false)
                       .build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("%1: %2 -> %3")
                .arg(arg(a, "operation"), arg(a, "source"), arg(a, "destination"));
        };
        t.handler = [pc](const QJsonObject& a) -> ToolResult {
            const QString op   = arg(a, "operation").toLower();
            const QString src  = arg(a, "source").trimmed();
            const QString dst  = arg(a, "destination").trimmed();
            SystemController* sys = pc->system();

            // Перемещение и переименование записываем ДО операции: после
            // неё исходного пути уже нет, и восстанавливать будет некуда.
            EditJournal::Scope batch(QStringLiteral("инструмент file_operation"));
            EditJournal& journal = EditJournal::instance();

            if (op == QLatin1String("create_folder")) {
                if (sys->createFolder(src)) {
                    journal.recordCreate(src);
                    return ToolResult::success(QStringLiteral("Created folder %1").arg(src),
                                               QStringLiteral("Папка создана"));
                }
            } else if (op == QLatin1String("move")) {
                journal.recordMove(src, dst);
                if (sys->moveFile(src, dst))
                    return ToolResult::success(QStringLiteral("Moved %1 to %2").arg(src, dst),
                                               QStringLiteral("Перемещено"));
                journal.discardLastOp();
            } else if (op == QLatin1String("copy")) {
                if (sys->copyFile(src, dst)) {
                    journal.recordCreate(dst);
                    return ToolResult::success(QStringLiteral("Copied %1 to %2").arg(src, dst),
                                               QStringLiteral("Скопировано"));
                }
            } else if (op == QLatin1String("rename")) {
                journal.recordMove(src, dst);
                if (sys->renameFile(src, dst))
                    return ToolResult::success(QStringLiteral("Renamed %1 to %2").arg(src, dst),
                                               QStringLiteral("Переименовано"));
                journal.discardLastOp();
            } else {
                return ToolResult::failure(QStringLiteral("Unknown operation '%1'").arg(op));
            }
            return ToolResult::failure(QStringLiteral(
                "Operation refused. Allowed roots: %1")
                    .arg(SystemController::allowedOrganizeRoots().join(QStringLiteral(", "))));
        };
        // Файловые операции проверяются точно: либо путь на месте, либо
        // нет. Здесь догадок нет, поэтому и Contradicted честный.
        t.verify = [](const QJsonObject& a, const ToolResult&) -> ToolVerdict {
            const QString op  = arg(a, "operation").toLower();
            const QString src = arg(a, "source").trimmed();
            const QString dst = arg(a, "destination").trimmed();

            if (op == QLatin1String("create_folder")) {
                if (QFileInfo(src).isDir())
                    return ToolVerdict::confirmed(QStringLiteral("папка %1 существует").arg(src));
                return ToolVerdict::contradicted(QStringLiteral("папки %1 нет").arg(src));
            }

            if (op == QLatin1String("copy")) {
                if (QFileInfo::exists(dst))
                    return ToolVerdict::confirmed(QStringLiteral("копия %1 на месте").arg(dst));
                return ToolVerdict::contradicted(QStringLiteral("файла %1 нет").arg(dst));
            }

            if (op == QLatin1String("move") || op == QLatin1String("rename")) {
                const bool atDst  = QFileInfo::exists(dst);
                const bool atSrc  = QFileInfo::exists(src);
                if (atDst && !atSrc)
                    return ToolVerdict::confirmed(QStringLiteral("%1 -> %2").arg(src, dst));
                if (atDst && atSrc)
                    // Копия вместо переноса: исходник остался. Для «убери
                    // из Downloads» это не выполненная задача.
                    return ToolVerdict::partial(
                        QStringLiteral("%1 появился, но %2 никуда не делся").arg(dst, src));
                return ToolVerdict::contradicted(
                    QStringLiteral("по пути %1 ничего нет").arg(dst));
            }
            return ToolVerdict();
        };
        reg.registerTool(t);
    }

    // --------------------------------------------------------
    //  system
    // --------------------------------------------------------
    {
        ToolSpec t;
        t.name        = QStringLiteral("system_status");
        t.category    = QStringLiteral("system");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "Current machine state: CPU load, RAM, disk space, uptime, volume and the "
            "active window. Use this before answering anything about how the PC is doing.");
        t.schema  = ToolSchema::empty();
        t.handler = [pc](const QJsonObject&) -> ToolResult {
            QStringList lines;

            const int cpu = cpuLoadPercent();
            lines << QStringLiteral("CPU: %1").arg(cpu < 0 ? QStringLiteral("n/a")
                                                           : QStringLiteral("%1%").arg(cpu));

            MEMORYSTATUSEX mem;
            mem.dwLength = sizeof(mem);
            if (GlobalMemoryStatusEx(&mem)) {
                lines << QStringLiteral("RAM: %1%  (%2 used of %3)")
                             .arg(mem.dwMemoryLoad)
                             .arg(humanBytes(mem.ullTotalPhys - mem.ullAvailPhys),
                                  humanBytes(mem.ullTotalPhys));
            }

            for (const QStorageInfo& si : QStorageInfo::mountedVolumes()) {
                if (!si.isValid() || !si.isReady() || si.isReadOnly())
                    continue;
                const qint64 total = si.bytesTotal();
                if (total <= 0)
                    continue;
                const qint64 used = total - si.bytesAvailable();
                lines << QStringLiteral("Disk %1: %2%  (%3 free of %4)")
                             .arg(si.rootPath())
                             .arg(total > 0 ? used * 100 / total : 0)
                             .arg(humanBytes(si.bytesAvailable()), humanBytes(total));
            }

            const quint64 uptimeSec = GetTickCount64() / 1000;
            lines << QStringLiteral("Uptime: %1h %2m")
                         .arg(uptimeSec / 3600).arg((uptimeSec % 3600) / 60);
            lines << QStringLiteral("Volume: %1%").arg(pc->system()->currentVolume());

            const WindowInfo active = pc->windows()->activeWindow();
            if (!active.title.isEmpty())
                lines << QStringLiteral("Active window: %1").arg(active.title);

            MEMORYSTATUSEX memAgain;
            memAgain.dwLength = sizeof(memAgain);
            const int ramPct = GlobalMemoryStatusEx(&memAgain) ? int(memAgain.dwMemoryLoad) : 0;

            return ToolResult::success(lines.join(QChar('\n')),
                                       QStringLiteral("CPU %1%, RAM %2%").arg(cpu).arg(ramPct));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("list_processes");
        t.category    = QStringLiteral("system");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "List running processes sorted by memory usage (name, pid, working set). "
            "Use it to check whether an application is already running.");
        t.schema = ToolSchema()
                       .integer("top", "How many processes to return (default 20)", false)
                       .str("filter", "Only processes whose name contains this text", false)
                       .build();
        t.handler = [](const QJsonObject& a) -> ToolResult {
            QVector<ProcInfo> procs = snapshotProcesses();
            const QString filter = arg(a, "filter").trimmed().toLower();
            if (!filter.isEmpty()) {
                QVector<ProcInfo> kept;
                for (const ProcInfo& p : procs)
                    if (p.name.toLower().contains(filter))
                        kept.append(p);
                procs = kept;
            }
            std::sort(procs.begin(), procs.end(),
                      [](const ProcInfo& a, const ProcInfo& b) { return a.memBytes > b.memBytes; });

            const int top = qBound(1, argInt(a, "top", 20), 100);
            QStringList lines;
            for (int i = 0; i < procs.size() && i < top; ++i)
                lines << QStringLiteral("%1  pid %2  %3")
                             .arg(procs[i].name, -32)
                             .arg(procs[i].pid, -8)
                             .arg(humanBytes(procs[i].memBytes));

            if (lines.isEmpty())
                return ToolResult::success(QStringLiteral("No matching processes."),
                                           QStringLiteral("Процессов не найдено"));
            return ToolResult::success(lines.join(QChar('\n')),
                                       QStringLiteral("Процессов: %1").arg(procs.size()));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("kill_process");
        t.category    = QStringLiteral("system");
        t.risk        = ToolRisk::Dangerous;
        t.description = QStringLiteral(
            "Force-terminate a process by pid or exact name. Unsaved work in that "
            "application is lost. Prefer window_action(close) for normal apps.");
        t.schema = ToolSchema()
                       .str("name", "Process name, e.g. chrome.exe", false)
                       .integer("pid", "Process id (takes priority over name)", false)
                       .build();
        t.preview = [](const QJsonObject& a) {
            const int pid = argInt(a, "pid", 0);
            return pid > 0 ? QStringLiteral("Убить процесс pid %1").arg(pid)
                           : QStringLiteral("Убить процесс %1").arg(arg(a, "name"));
        };
        // Какие pid'ы мы считаем убитыми — чтобы проверке было что
        // перепроверять: имя процесса для этого не годится, второй
        // экземпляр того же приложения не имеет отношения к делу.
        // Как и в launch_app, список копируется до ожидания.
        auto killedPids = std::make_shared<QVector<quint32>>();

        t.handler = [killedPids](const QJsonObject& a) -> ToolResult {
            const int pid     = argInt(a, "pid", 0);
            const QString nm  = arg(a, "name").trimmed();
            killedPids->clear();

            QVector<ProcInfo> targets;
            for (const ProcInfo& p : snapshotProcesses()) {
                if (pid > 0 ? (int(p.pid) == pid)
                            : (!nm.isEmpty() && p.name.compare(nm, Qt::CaseInsensitive) == 0))
                    targets.append(p);
            }
            if (targets.isEmpty())
                return ToolResult::failure(QStringLiteral("No such process."));

            QStringList killed, refused;
            for (const ProcInfo& p : targets) {
                if (isProtectedProcess(p.name.toLower())) {
                    refused << p.name;
                    continue;
                }
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, p.pid);
                if (h && TerminateProcess(h, 1)) {
                    killed << QStringLiteral("%1 (pid %2)").arg(p.name).arg(p.pid);
                    killedPids->append(p.pid);
                } else
                    refused << QStringLiteral("%1 (pid %2, access denied)").arg(p.name).arg(p.pid);
                if (h) CloseHandle(h);
            }

            QString report;
            if (!killed.isEmpty()) {
                report += QStringLiteral("Terminated: ") + killed.join(QStringLiteral(", "));
                // В журнал — не ради отката (воскресить процесс нельзя),
                // а ради честного отчёта: «файлы вернул, процесс нет».
                EditJournal::Scope batch(QStringLiteral("инструмент kill_process"));
                EditJournal::instance().recordExternal(
                    QStringLiteral("убит процесс %1").arg(killed.join(QStringLiteral(", "))));
            }
            if (!refused.isEmpty())
                report += (report.isEmpty() ? QString() : QStringLiteral("\n"))
                          + QStringLiteral("Refused (protected or no rights): ")
                          + refused.join(QStringLiteral(", "));
            return killed.isEmpty() ? ToolResult::failure(report)
                                    : ToolResult::success(report,
                                          QStringLiteral("Завершено: %1").arg(killed.size()));
        };
        // TerminateProcess ставит процесс в очередь на завершение и
        // возвращает успех сразу. Процесс, застрявший в драйвере или в
        // необрываемом системном вызове, при этом остаётся жить.
        t.verify = [killedPids](const QJsonObject&, const ToolResult&) -> ToolVerdict {
            if (killedPids->isEmpty())
                return ToolVerdict();

            const QVector<quint32> pids = *killedPids;
            QVector<quint32> alive;
            waitFor([pids, &alive]() {
                alive.clear();
                for (quint32 pid : pids) {
                    if (pidAlive(pid))
                        alive.append(pid);
                }
                return alive.isEmpty();
            }, 3000, 300);

            if (alive.isEmpty())
                return ToolVerdict::confirmed(
                    QStringLiteral("процессов завершено: %1").arg(pids.size()));

            QStringList stuck;
            for (quint32 pid : alive)
                stuck << QString::number(pid);
            return ToolVerdict::partial(
                QStringLiteral("не завершились: pid %1 — процесс висит в системном вызове")
                    .arg(stuck.join(QStringLiteral(", "))));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("volume_control");
        t.category    = QStringLiteral("system");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral("Read or change the master output volume, or (un)mute it.");
        t.schema = ToolSchema()
                       .choice("action", { QStringLiteral("get"), QStringLiteral("set"),
                                           QStringLiteral("mute"), QStringLiteral("unmute"),
                                           QStringLiteral("toggle_mute") },
                               "What to do")
                       .integer("percent", "Target volume 0-100 (only for action=set)", false)
                       .build();
        t.handler = [pc](const QJsonObject& a) -> ToolResult {
            const QString action = arg(a, "action", QStringLiteral("get")).toLower();
            SystemController* sys = pc->system();

            if (action == QLatin1String("set")) {
                const int pct = qBound(0, argInt(a, "percent", 50), 100);
                // Прежнее значение запоминаем ДО изменения: вернуть
                // громкость журнал не может, но сказать, какой она была,
                // обязан — иначе «верни как было» упирается в догадку.
                const int previous = sys->currentVolume();
                if (sys->setVolume(pct)) {
                    EditJournal::Scope batch(QStringLiteral("инструмент volume_control"));
                    EditJournal::instance().recordExternal(
                        QStringLiteral("громкость %1%% -> %2%%, прежняя была %1%%")
                            .arg(previous).arg(pct));
                    return ToolResult::success(QStringLiteral("Volume set to %1%").arg(pct),
                                               QStringLiteral("Громкость %1%").arg(pct));
                }
                return ToolResult::failure(QStringLiteral("Failed to set volume"));
            }
            if (action == QLatin1String("mute") || action == QLatin1String("unmute")) {
                const bool m = (action == QLatin1String("mute"));
                sys->muteAudio(m);
                return ToolResult::success(m ? QStringLiteral("Muted") : QStringLiteral("Unmuted"),
                                           m ? QStringLiteral("Звук выключен") : QStringLiteral("Звук включён"));
            }
            if (action == QLatin1String("toggle_mute")) {
                sys->toggleMute();
                return ToolResult::success(QStringLiteral("Mute toggled"), QStringLiteral("Звук переключён"));
            }
            const int cur = sys->currentVolume();
            return ToolResult::success(QStringLiteral("Volume is %1%").arg(cur),
                                       QStringLiteral("Громкость %1%").arg(cur));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("power_action");
        t.category    = QStringLiteral("system");
        t.risk        = ToolRisk::Dangerous;
        t.description = QStringLiteral(
            "Lock, sleep, shut down or restart the machine, or cancel a pending shutdown. "
            "Shutdown and restart are scheduled with a delay so they can still be cancelled.");
        t.schema = ToolSchema()
                       .choice("action", { QStringLiteral("lock"), QStringLiteral("sleep"),
                                           QStringLiteral("shutdown"), QStringLiteral("restart"),
                                           QStringLiteral("cancel_shutdown") },
                               "Power action")
                       .integer("delay_seconds", "Delay for shutdown/restart (default 60)", false)
                       .build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Питание: %1").arg(arg(a, "action"));
        };
        t.handler = [pc](const QJsonObject& a) -> ToolResult {
            const QString action = arg(a, "action").toLower();
            const int delay = qBound(0, argInt(a, "delay_seconds", 60), 3600);
            SystemController* sys = pc->system();

            if (action == QLatin1String("lock") && sys->lockWorkstation())
                return ToolResult::success(QStringLiteral("Workstation locked"), QStringLiteral("ПК заблокирован"));
            if (action == QLatin1String("sleep") && sys->sleepNow())
                return ToolResult::success(QStringLiteral("Going to sleep"), QStringLiteral("Сон"));
            if (action == QLatin1String("shutdown") && sys->shutdownIn(delay))
                return ToolResult::success(QStringLiteral("Shutdown scheduled in %1s").arg(delay),
                                           QStringLiteral("Выключение через %1 с").arg(delay));
            if (action == QLatin1String("restart") && sys->restartIn(delay))
                return ToolResult::success(QStringLiteral("Restart scheduled in %1s").arg(delay),
                                           QStringLiteral("Перезагрузка через %1 с").arg(delay));
            if (action == QLatin1String("cancel_shutdown") && sys->cancelShutdown())
                return ToolResult::success(QStringLiteral("Pending shutdown cancelled"),
                                           QStringLiteral("Выключение отменено"));
            return ToolResult::failure(QStringLiteral("Power action '%1' failed").arg(action));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("take_screenshot");
        t.category    = QStringLiteral("system");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "Capture the primary screen to a PNG file and return its path.");
        t.schema  = ToolSchema::empty();
        t.handler = [](const QJsonObject&) -> ToolResult {
            QScreen* screen = QGuiApplication::primaryScreen();
            if (!screen)
                return ToolResult::failure(QStringLiteral("No screen available"));

            const QPixmap shot = screen->grabWindow(0);
            if (shot.isNull())
                return ToolResult::failure(QStringLiteral("Screen capture failed"));

            const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                + QStringLiteral("/screenshots");
            QDir().mkpath(dir);
            const QString path = dir + QStringLiteral("/shot_")
                                 + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"))
                                 + QStringLiteral(".png");
            if (!shot.save(path, "PNG"))
                return ToolResult::failure(QStringLiteral("Could not save the screenshot"));

            return ToolResult::success(
                QStringLiteral("Screenshot saved: %1 (%2x%3)")
                    .arg(path).arg(shot.width()).arg(shot.height()),
                QStringLiteral("Скриншот сохранён"));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("wait");
        t.category    = QStringLiteral("system");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "Pause for a few seconds - use it to let a launched application finish "
            "starting before acting on its window.");
        t.schema  = ToolSchema().integer("seconds", "1-30 seconds").build();
        t.handler = [](const QJsonObject& a) -> ToolResult {
            const int sec = qBound(1, argInt(a, "seconds", 2), 30);
            QEventLoop loop;
            QTimer::singleShot(sec * 1000, &loop, &QEventLoop::quit);
            loop.exec();
            return ToolResult::success(QStringLiteral("Waited %1s").arg(sec),
                                       QStringLiteral("Пауза %1 с").arg(sec));
        };
        reg.registerTool(t);
    }

    // --------------------------------------------------------
    //  input
    // --------------------------------------------------------
    {
        ToolSpec t;
        t.name        = QStringLiteral("press_hotkey");
        t.category    = QStringLiteral("input");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Send a keyboard shortcut to the focused window, e.g. 'Ctrl+S', 'Alt+F4', "
            "'Win+D', 'F5'. Focus the right window first.");
        t.schema  = ToolSchema().str("combo", "Key combination, e.g. Ctrl+Shift+Esc").build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Нажать %1").arg(arg(a, "combo"));
        };
        t.handler = [pc](const QJsonObject& a) -> ToolResult {
            const QString combo = arg(a, "combo").trimmed();
            if (combo.isEmpty())
                return ToolResult::failure(QStringLiteral("combo is required"));
            if (pc->keyboard()->pressCombo(combo))
                return ToolResult::success(QStringLiteral("Pressed %1").arg(combo),
                                           QStringLiteral("Нажато %1").arg(combo));
            return ToolResult::failure(QStringLiteral("Could not press '%1'").arg(combo));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("type_text");
        t.category    = QStringLiteral("input");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Type text into the focused window as if from the keyboard. Focus the "
            "target window first; the text goes wherever the caret is.");
        t.schema  = ToolSchema().str("text", "Text to type").build();
        t.preview = [](const QJsonObject& a) {
            QString txt = arg(a, "text");
            if (txt.length() > 60) txt = txt.left(57) + QStringLiteral("...");
            return QStringLiteral("Напечатать: %1").arg(txt);
        };
        t.handler = [pc](const QJsonObject& a) -> ToolResult {
            const QString text = arg(a, "text");
            if (text.isEmpty())
                return ToolResult::failure(QStringLiteral("text is required"));
            if (pc->keyboard()->typeText(text)) {
                // Текст ушёл в чужое окно — в чей-то документ, редактор
                // или поле пароля. Ни отменить, ни узнать куда именно
                // журнал не может; зафиксировать факт — может.
                EditJournal::Scope batch(QStringLiteral("инструмент type_text"));
                EditJournal::instance().recordExternal(
                    QStringLiteral("напечатано в активном окне: %1")
                        .arg(text.length() > 60 ? text.left(57) + QStringLiteral("...") : text));
                return ToolResult::success(QStringLiteral("Typed %1 characters").arg(text.length()),
                                           QStringLiteral("Напечатано %1 симв.").arg(text.length()));
            }
            return ToolResult::failure(QStringLiteral("Typing failed"));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("mouse_action");
        t.category    = QStringLiteral("input");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Move the mouse and click or scroll at screen coordinates. Take a screenshot "
            "first to know where things are.");
        t.schema = ToolSchema()
                       .choice("action", { QStringLiteral("move"), QStringLiteral("left_click"),
                                           QStringLiteral("double_click"), QStringLiteral("right_click"),
                                           QStringLiteral("scroll_up"), QStringLiteral("scroll_down") },
                               "What the mouse should do")
                       .integer("x", "Screen X in pixels", false)
                       .integer("y", "Screen Y in pixels", false)
                       .integer("clicks", "Scroll amount (default 3)", false)
                       .build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Мышь: %1 (%2, %3)")
                .arg(arg(a, "action")).arg(argInt(a, "x", -1)).arg(argInt(a, "y", -1));
        };
        t.handler = [pc](const QJsonObject& a) -> ToolResult {
            const QString action = arg(a, "action").toLower();
            const bool hasPos = a.contains(QStringLiteral("x")) && a.contains(QStringLiteral("y"));
            const int x = argInt(a, "x", 0);
            const int y = argInt(a, "y", 0);
            MouseController* m = pc->mouse();

            bool ok = false;
            if (action == QLatin1String("move"))              ok = m->moveToSmooth(x, y);
            else if (action == QLatin1String("left_click"))   ok = hasPos ? m->clickAt(x, y) : m->click();
            else if (action == QLatin1String("double_click")) ok = hasPos ? m->doubleClickAt(x, y) : m->doubleClick();
            else if (action == QLatin1String("right_click"))  ok = hasPos ? m->rightClickAt(x, y) : m->rightClick();
            else if (action == QLatin1String("scroll_up"))    ok = m->scroll(ScrollDirection::Up, argInt(a, "clicks", 3));
            else if (action == QLatin1String("scroll_down"))  ok = m->scroll(ScrollDirection::Down, argInt(a, "clicks", 3));

            return ok ? ToolResult::success(QStringLiteral("%1 done").arg(action),
                                            QStringLiteral("Мышь: %1").arg(action))
                      : ToolResult::failure(QStringLiteral("Mouse action '%1' failed").arg(action));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("clipboard_read");
        t.category    = QStringLiteral("input");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral("Read the current text content of the clipboard.");
        t.schema  = ToolSchema::empty();
        t.handler = [pc](const QJsonObject&) -> ToolResult {
            const QString text = pc->clipboard()->getText();
            if (text.isEmpty())
                return ToolResult::success(QStringLiteral("Clipboard is empty."),
                                           QStringLiteral("Буфер пуст"));
            return ToolResult::success(text,
                                       QStringLiteral("Буфер: %1 симв.").arg(text.length()));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("clipboard_write");
        t.category    = QStringLiteral("input");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral("Replace the clipboard content with the given text.");
        t.schema  = ToolSchema().str("text", "Text to put into the clipboard").build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Положить в буфер %1 симв.").arg(arg(a, "text").length());
        };
        t.handler = [pc](const QJsonObject& a) -> ToolResult {
            const QString text = arg(a, "text");
            if (pc->clipboard()->setText(text))
                return ToolResult::success(QStringLiteral("Clipboard updated (%1 chars)").arg(text.length()),
                                           QStringLiteral("Буфер обновлён"));
            return ToolResult::failure(QStringLiteral("Could not write to the clipboard"));
        };
        reg.registerTool(t);
    }

    // --------------------------------------------------------
    //  shell
    // --------------------------------------------------------
    {
        ToolSpec t;
        t.name        = QStringLiteral("run_command");
        t.category    = QStringLiteral("shell");
        t.risk        = ToolRisk::Dangerous;
        t.description = QStringLiteral(
            "Run a Windows shell command (cmd.exe) and return its combined output. "
            "Last resort: use the dedicated tools for launching apps, reading files and "
            "listing processes. Good for builds, git and CLI tools.");
        t.schema = ToolSchema()
                       .str("command", "The full command line")
                       .str("working_dir", "Folder to run it in", false)
                       .integer("timeout_seconds", "Kill the command after this many seconds (default 60)", false)
                       .build();
        t.preview = [](const QJsonObject& a) {
            const QString wd = arg(a, "working_dir");
            return QStringLiteral("Выполнить: %1%2")
                .arg(arg(a, "command"),
                     wd.isEmpty() ? QString() : QStringLiteral("   [в %1]").arg(wd));
        };
        t.handler = [](const QJsonObject& a) -> ToolResult {
            const QString cmd = arg(a, "command").trimmed();
            if (cmd.isEmpty())
                return ToolResult::failure(QStringLiteral("command is required"));

            bool ok = false;
            const QString out = runShell(cmd, arg(a, "working_dir"),
                                         argInt(a, "timeout_seconds", 60), &ok);
            if (!ok)
                return ToolResult::failure(out);

            // Что натворила команда, журнал знать не может — но обязан
            // знать, что она была: иначе откат отчитается «вернул всё»,
            // умолчав про единственное, что действительно могло сломать.
            EditJournal::Scope batch(QStringLiteral("инструмент run_command"));
            EditJournal::instance().recordExternal(
                QStringLiteral("выполнена команда: %1").arg(cmd.left(120)));

            return ToolResult::success(out, QStringLiteral("Выполнено: %1").arg(cmd.left(60)));
        };
        reg.registerTool(t);
    }

    // --------------------------------------------------------
    //  notifications
    // --------------------------------------------------------
    {
        ToolSpec t;
        t.name        = QStringLiteral("list_events");
        t.category    = QStringLiteral("notifications");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "Recent events JARVIS recorded: sustained CPU or memory load, low disk "
            "space, finished workflows, profile switches, failures. Use it when the "
            "user asks what happened, what went wrong, or why the machine is slow.");
        t.schema = ToolSchema()
                       .integer("limit", "How many recent events to return (default 20)", false)
                       .boolean("only_important", "Warnings and errors only", false)
                       .build();
        t.handler = [](const QJsonObject& a) -> ToolResult {
            const int limit = qBound(1, argInt(a, "limit", 20), 200);
            const bool onlyImportant = argBool(a, "only_important", false);

            const QVector<FeedEvent> all = EventFeed::instance().events();
            QStringList lines;
            for (int i = all.size() - 1; i >= 0 && lines.size() < limit; --i) {
                const FeedEvent& e = all[i];
                if (onlyImportant
                    && e.level != EventLevel::Warning && e.level != EventLevel::Error)
                    continue;

                QString line = QStringLiteral("%1  [%2/%3]  %4")
                                   .arg(e.at.toString(QStringLiteral("dd.MM HH:mm")),
                                        e.source, eventLevelName(e.level), e.title);
                if (e.count > 1)
                    line += QStringLiteral(" (x%1)").arg(e.count);
                if (!e.detail.isEmpty())
                    line += QStringLiteral("\n      ") + e.detail;
                lines << line;
            }

            if (lines.isEmpty())
                return ToolResult::success(QStringLiteral("No events recorded yet."),
                                           QStringLiteral("Событий нет"));
            return ToolResult::success(lines.join(QChar('\n')),
                                       QStringLiteral("Событий: %1").arg(lines.size()));
        };
        reg.registerTool(t);
    }

    qDebug() << "[Tools] registered" << reg.count() << "system tools:"
             << reg.categories().join(QStringLiteral(", "));
}

// ============================================================
//  Контекст — «что сейчас на экране»
// ============================================================

void registerContextTools(ToolRegistry& reg, ContextTracker* tracker)
{
    if (!tracker) {
        qWarning() << "[Tools] registerContextTools: tracker is null";
        return;
    }

    ToolSpec t;
    t.name        = QStringLiteral("get_context");
    t.category    = QStringLiteral("context");
    t.risk        = ToolRisk::Safe;
    t.description = QStringLiteral(
        "What the user is looking at RIGHT NOW: active application, window title, "
        "open file, project, browser page, clipboard. The system prompt carries a "
        "snapshot taken when the message arrived - call this when the user may have "
        "switched windows since, or when a word like \"here\", \"this\" or \"it\" "
        "has to point at something concrete.");
    t.schema  = ToolSchema::empty();
    t.handler = [tracker](const QJsonObject&) -> ToolResult {
        const MachineContext ctx = tracker->snapshot();
        if (ctx.isEmpty())
            return ToolResult::success(
                QStringLiteral("No other window has been in the foreground yet - "
                               "only the JARVIS window itself."),
                QStringLiteral("Контекст пуст"));

        QString display = ctx.appName;
        if (!ctx.currentFile.isEmpty())
            display += QStringLiteral(" / ") + ctx.currentFile;
        return ToolResult::success(ctx.toHumanText(),
                                   QStringLiteral("Контекст: %1").arg(display));
    };
    reg.registerTool(t);
}

} // namespace JarvisTools
