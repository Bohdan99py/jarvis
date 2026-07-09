// -------------------------------------------------------
// pc_command_registry.cpp — Голосовые команды управления ПК
// J.A.R.V.I.S. Full PC Voice Control
//
// Клавиатура НЕ регистрируется здесь — она у тебя уже есть
// через KeyEmulator + cmdTypeText/cmdPressKey/cmdCombo в jarvis.cpp.
// -------------------------------------------------------

#include "pc_command_registry.h"
#include "pc_controller.h"
#include "macro_recorder.h"
#include "command_registry.h"
#include "virtual_keyboard.h"   // KeyEmulator — для печати текста внутри макросов

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <QStringList>
#include <QThread>
#include <QFile>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QProcess>

// ============================================================
//  Локальные хелперы парсинга (файл-приватные)
// ============================================================

namespace {

int extractNumber(const QString& fullInput, int defaultVal)
{
    static const QMap<QString, int> words = {
        {"один",1},{"одну",1},{"одно",1},{"one",1},
        {"два",2},{"две",2},{"two",2},
        {"три",3},{"three",3},
        {"четыре",4},{"four",4},
        {"пять",5},{"five",5},
        {"шесть",6},{"six",6},
        {"семь",7},{"seven",7},
        {"восемь",8},{"eight",8},
        {"девять",9},{"nine",9},
        {"десять",10},{"ten",10},
        {"двадцать",20},{"twenty",20},
        {"тридцать",30},{"thirty",30},
        {"пятьдесят",50},{"fifty",50},
        {"сто",100},{"hundred",100},
    };
    const QStringList tokens = fullInput.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const auto& t : tokens) {
        bool ok = false;
        const int n = t.toInt(&ok);
        if (ok) return n;
        const auto it = words.find(t);
        if (it != words.end()) return it.value();
    }
    return defaultVal;
}

// "запусти notepad" + "запусти" → "notepad"
QString remainderAfter(const QString& fullInput, const QString& keyword)
{
    const QString lower = fullInput.toLower();
    const int idx = lower.indexOf(keyword.toLower());
    if (idx < 0) return {};
    return fullInput.mid(idx + keyword.length()).trimmed();
}

} // namespace

// ============================================================
//  Конструктор / деструктор
// ============================================================

PcCommandRegistry::PcCommandRegistry(KeyEmulator* keyEmulator, QObject* parent)
    : QObject(parent), m_keyEmulator(keyEmulator)
{
    m_pc     = new PcController(this);
    m_macros = new MacroRecorder(m_pc, this);

    connect(m_macros, &MacroRecorder::feedbackReady,
            this, &PcCommandRegistry::feedbackReady);
}

PcCommandRegistry::~PcCommandRegistry() = default;

// ============================================================
//  registerInto — точка входа
// ============================================================

void PcCommandRegistry::registerInto(CommandRegistry& registry)
{
    registerMouseCommands(registry);
    registerWindowCommands(registry);
    registerSystemCommands(registry);
    registerMediaCommands(registry);
    registerBrowserCommands(registry);
    registerFileCommands(registry);
    registerMacroVoiceCommands(registry);
}

// ============================================================
//  МЫШЬ
// ============================================================

void PcCommandRegistry::registerMouseCommands(CommandRegistry& r)
{
    MouseController* mouse = m_pc->mouse();

    r.registerCommand({"кликни", "клик", "click"},
        [mouse](const QString&) -> QString {
            return mouse->click()
                ? QStringLiteral("Click")
                : QStringLiteral("Click failed");
        },
        QStringLiteral("click — left mouse click"));

    r.registerCommand({"правый клик", "правая кнопка", "right click", "контекстное меню"},
        [mouse](const QString&) -> QString {
            return mouse->rightClick()
                ? QStringLiteral("Right click")
                : QStringLiteral("Right click failed");
        },
        QStringLiteral("right click — context menu"));

    r.registerCommand({"двойной клик", "дабл клик", "double click"},
        [mouse](const QString&) -> QString {
            return mouse->doubleClick()
                ? QStringLiteral("Double click")
                : QStringLiteral("Double click failed");
        },
        QStringLiteral("double click"));

    r.registerCommand({"прокрути вверх", "скролл вверх", "scroll up"},
        [mouse](const QString& full) -> QString {
            const int n = extractNumber(full, 3);
            return mouse->scroll(ScrollDirection::Up, n)
                ? QStringLiteral("Scrolled up")
                : QStringLiteral("Scroll failed");
        },
        QStringLiteral("scroll up [N] — scroll wheel up"));

    r.registerCommand({"прокрути вниз", "скролл вниз", "scroll down"},
        [mouse](const QString& full) -> QString {
            const int n = extractNumber(full, 3);
            return mouse->scroll(ScrollDirection::Down, n)
                ? QStringLiteral("Scrolled down")
                : QStringLiteral("Scroll failed");
        },
        QStringLiteral("scroll down [N] — scroll wheel down"));

    r.registerCommand({"мышь в центр", "курсор в центр", "mouse center"},
        [mouse](const QString&) -> QString {
            const QSize sz = MouseController::screenSize();
            return mouse->moveTo(sz.width() / 2, sz.height() / 2)
                ? QStringLiteral("Cursor centered")
                : QStringLiteral("Move failed");
        },
        QStringLiteral("mouse center — cursor to screen center"));

    struct GridPoint { QStringList names; float rx, ry; };
    static const GridPoint grid[] = {
        {{"мышь верхний левый", "mouse top left"},     0.1f, 0.1f},
        {{"мышь верхний правый","mouse top right"},    0.9f, 0.1f},
        {{"мышь левый",  "mouse left"},                0.1f, 0.5f},
        {{"мышь правый", "mouse right"},               0.9f, 0.5f},
        {{"мышь нижний левый",  "mouse bottom left"},  0.1f, 0.9f},
        {{"мышь нижний правый", "mouse bottom right"}, 0.9f, 0.9f},
    };
    for (const auto& gp : grid) {
        const float rx = gp.rx, ry = gp.ry;
        r.registerCommand(gp.names,
            [mouse, rx, ry](const QString&) -> QString {
                const QSize sz = MouseController::screenSize();
                const int x = static_cast<int>(sz.width()  * rx);
                const int y = static_cast<int>(sz.height() * ry);
                return mouse->moveToSmooth(x, y, 200)
                    ? QStringLiteral("Cursor moved")
                    : QStringLiteral("Move failed");
            },
            QStringLiteral("cursor grid position"));
    }
}

// ============================================================
//  ОКНА
// ============================================================

void PcCommandRegistry::registerWindowCommands(CommandRegistry& r)
{
    WindowController* wins = m_pc->windows();

    r.registerCommand({"переключи окно", "другое окно", "switch window"},
        [wins](const QString&) -> QString {
            return wins->switchToNext() ? QString() : QStringLiteral("Error");
        }, QStringLiteral("switch window — Alt+Tab"));

    struct AppCmd { QStringList tr; QString name; };
    static const AppCmd apps[] = {
        {{"открой хром","переключись на chrome"},      "Chrome"},
        {{"открой edge","переключись на edge"},         "Edge"},
        {{"открой firefox","переключись на firefox"},   "Firefox"},
        {{"открой visual studio"},                       "Visual Studio"},
        {{"открой clion","переключись на clion"},        "CLion"},
        {{"открой проводник","переключись на explorer"}, "Explorer"},
        {{"открой блокнот","переключись на notepad"},    "Notepad"},
        {{"открой discord","переключись на discord"},    "Discord"},
        {{"открой telegram","переключись на telegram"},  "Telegram"},
        {{"открой unreal engine","переключись на unreal"},"Unreal"},
        {{"открой cursor"},                               "Cursor"},
        {{"открой vs code","открой vscode"},              "Visual Studio Code"},
    };
    for (const auto& a : apps) {
        const QString appName = a.name;
        r.registerCommand(a.tr,
            [wins, appName](const QString&) -> QString {
                return wins->focusWindow(appName)
                    ? QStringLiteral("Switched to ") + appName
                    : QStringLiteral("Window not found: ") + appName;
            },
            QStringLiteral("switch to ") + appName);
    }

    r.registerCommand({"сверни окно", "сверни", "minimize window"},
        [wins](const QString&) -> QString {
            return wins->minimizeActive() ? QStringLiteral("Minimized") : QStringLiteral("Error");
        }, QStringLiteral("minimize window"));

    r.registerCommand({"разверни окно", "разверни на весь экран", "maximize window"},
        [wins](const QString&) -> QString {
            return wins->maximizeActive() ? QStringLiteral("Maximized") : QStringLiteral("Error");
        }, QStringLiteral("maximize window — fullscreen"));

    r.registerCommand({"закрой окно", "close window"},
        [wins](const QString&) -> QString {
            return wins->closeActive() ? QStringLiteral("Window closed") : QStringLiteral("Error");
        }, QStringLiteral("close window"));

    r.registerCommand({"закрепи окно поверх", "окно поверх всех", "always on top"},
        [wins](const QString&) -> QString {
            return wins->alwaysOnTopActive(true)
                ? QStringLiteral("Pinned on top")
                : QStringLiteral("Error");
        }, QStringLiteral("pin window on top — always on top"));

    r.registerCommand({"открепи окно", "убери поверх всех"},
        [wins](const QString&) -> QString {
            return wins->alwaysOnTopActive(false) ? QStringLiteral("Unpinned") : QStringLiteral("Error");
        }, QStringLiteral("unpin window — remove always on top"));

    r.registerCommand({"покажи рабочий стол", "show desktop"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->showDesktopCombo() ? QString() : QStringLiteral("Error");
        }, QStringLiteral("show desktop — Win+D"));
}

// ============================================================
//  СИСТЕМА
// ============================================================

void PcCommandRegistry::registerSystemCommands(CommandRegistry& r)
{
    SystemController* sys = m_pc->system();

    r.registerCommand({"заблокируй компьютер", "заблокируй", "lock"},
        [sys](const QString&) -> QString {
            return sys->lockWorkstation() ? QStringLiteral("Locked") : QStringLiteral("Error");
        }, QStringLiteral("lock computer"));

    r.registerCommand({"спящий режим", "уйди в сон", "sleep mode"},
        [sys](const QString&) -> QString {
            return sys->sleepNow() ? QStringLiteral("Going to sleep") : QStringLiteral("Error");
        }, QStringLiteral("sleep mode"));

    r.registerCommand({"выключи компьютер", "shutdown"},
        [sys](const QString& full) -> QString {
            const int delay = extractNumber(full, 60);
            sys->shutdownIn(delay);
            return QStringLiteral("Shutting down in %1 seconds").arg(delay);
        }, QStringLiteral("shutdown [in N seconds]"));

    r.registerCommand({"перезагрузи компьютер", "restart", "reboot"},
        [sys](const QString& full) -> QString {
            const int delay = extractNumber(full, 60);
            sys->restartIn(delay);
            return QStringLiteral("Restarting in %1 seconds").arg(delay);
        }, QStringLiteral("restart [in N seconds]"));

    r.registerCommand({"отмени выключение", "cancel shutdown"},
        [sys](const QString&) -> QString {
            return sys->cancelShutdown()
                ? QStringLiteral("Shutdown cancelled")
                : QStringLiteral("Nothing to cancel");
        }, QStringLiteral("cancel shutdown"));

    r.registerCommand({"запусти"},
        [sys](const QString& full) -> QString {
            const QString app = remainderAfter(full, QStringLiteral("запусти"));
            if (app.isEmpty()) return QStringLiteral("No app specified");
            return sys->launchApp(app)
                ? QStringLiteral("Launching ") + app
                : QStringLiteral("Not found: ") + app;
        },
        QStringLiteral("launch <app> — e.g. 'launch notepad'"),
        /*prefixMatch=*/true);

    r.registerCommand({"открой выполнить", "win r"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->openRunCombo() ? QString() : QStringLiteral("Error");
        }, QStringLiteral("open Run — Win+R"));

    r.registerCommand({"поиск windows", "windows search"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->openSearchCombo() ? QString() : QStringLiteral("Error");
        }, QStringLiteral("Windows search — Win+S"));

    r.registerCommand({"открой проводник", "file explorer"},
        [sys](const QString&) -> QString {
            return sys->launchApp(QStringLiteral("explorer.exe")) ? QString() : QStringLiteral("Error");
        }, QStringLiteral("open File Explorer"));

    r.registerCommand({"открой калькулятор", "calculator"},
        [sys](const QString&) -> QString {
            return sys->launchApp(QStringLiteral("calc.exe")) ? QString() : QStringLiteral("Error");
        }, QStringLiteral("open Calculator"));

    r.registerCommand({"открой настройки windows", "settings"},
        [sys](const QString&) -> QString {
            return sys->launchApp(QStringLiteral("ms-settings:")) ? QString() : QStringLiteral("Error");
        }, QStringLiteral("open Windows Settings"));

    r.registerCommand({"диспетчер задач", "task manager"},
        [sys](const QString&) -> QString {
            return sys->openTaskManager() ? QString() : QStringLiteral("Error");
        }, QStringLiteral("Task Manager"));

    // ── Bluetooth ──
    r.registerCommand({"включи блютуз", "bluetooth on", "enable bluetooth"},
        [](const QString&) -> QString {
            QProcess p;
            p.start(QStringLiteral("powershell.exe"), {
                QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
                QStringLiteral("Add-Type -AssemblyName System.Runtime.WindowsRuntime; "
                    "[Windows.Devices.Radios.Radio,Windows.System.Devices,ContentType=WindowsRuntime] | Out-Null; "
                    "$radios = [Windows.Devices.Radios.Radio]::GetRadiosAsync().GetAwaiter().GetResult(); "
                    "foreach($r in $radios){if($r.Kind -eq 'Bluetooth'){$r.SetStateAsync('On').GetAwaiter().GetResult()}}")
            });
            return p.waitForFinished(10000) && p.exitCode() == 0
                ? QStringLiteral("Bluetooth ON")
                : QStringLiteral("Failed — try Windows Settings");
        }, QStringLiteral("enable Bluetooth"));

    r.registerCommand({"выключи блютуз", "bluetooth off", "disable bluetooth"},
        [](const QString&) -> QString {
            QProcess p;
            p.start(QStringLiteral("powershell.exe"), {
                QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
                QStringLiteral("Add-Type -AssemblyName System.Runtime.WindowsRuntime; "
                    "[Windows.Devices.Radios.Radio,Windows.System.Devices,ContentType=WindowsRuntime] | Out-Null; "
                    "$radios = [Windows.Devices.Radios.Radio]::GetRadiosAsync().GetAwaiter().GetResult(); "
                    "foreach($r in $radios){if($r.Kind -eq 'Bluetooth'){$r.SetStateAsync('Off').GetAwaiter().GetResult()}}")
            });
            return p.waitForFinished(10000) && p.exitCode() == 0
                ? QStringLiteral("Bluetooth OFF")
                : QStringLiteral("Failed — try Windows Settings");
        }, QStringLiteral("disable Bluetooth"));

    // ── Empty recycle bin ──
    r.registerCommand({"очисти корзину", "empty recycle bin", "clear trash"},
        [](const QString&) -> QString {
            QProcess p;
            p.start(QStringLiteral("powershell.exe"), {
                QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
                QStringLiteral("Clear-RecycleBin -Force -ErrorAction SilentlyContinue")
            });
            return p.waitForFinished(10000)
                ? QStringLiteral("Recycle bin emptied")
                : QStringLiteral("Error");
        }, QStringLiteral("empty recycle bin"));

    // ── Screenshot (Win+Shift+S) ──
    r.registerCommand({"скриншот", "сделай скриншот", "screenshot"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->screenshotArea() ? QStringLiteral("Screenshot tool opened") : QStringLiteral("Error");
        }, QStringLiteral("screenshot area — Win+Shift+S"));

    // ── Open recycle bin ──
    r.registerCommand({"открой корзину", "open recycle bin"},
        [sys](const QString&) -> QString {
            return sys->launchApp(QStringLiteral("explorer.exe"),
                QStringLiteral("shell:RecycleBinFolder"))
                ? QString() : QStringLiteral("Error");
        }, QStringLiteral("open Recycle Bin"));

    // ── Control Panel / Device Manager ──
    r.registerCommand({"панель управления", "control panel"},
        [sys](const QString&) -> QString {
            return sys->launchApp(QStringLiteral("control.exe")) ? QString() : QStringLiteral("Error");
        }, QStringLiteral("Control Panel"));

    r.registerCommand({"диспетчер устройств", "device manager"},
        [sys](const QString&) -> QString {
            return sys->launchApp(QStringLiteral("devmgmt.msc")) ? QString() : QStringLiteral("Error");
        }, QStringLiteral("Device Manager"));

    // ── Show desktop ──
    r.registerCommand({"покажи рабочий стол", "show desktop", "рабочий стол"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->showDesktopCombo() ? QString() : QStringLiteral("Error");
        }, QStringLiteral("show desktop — Win+D"));
}

// ============================================================
//  МЕДИА
// ============================================================

void PcCommandRegistry::registerMediaCommands(CommandRegistry& r)
{
    SystemController* sys = m_pc->system();

    r.registerCommand({"пауза музыка", "play pause", "плей"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->pressKey(QStringLiteral("MediaPlay")) ? QString() : QStringLiteral("Error");
        }, QStringLiteral("play/pause media"));

    r.registerCommand({"следующий трек", "next track"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->pressKey(QStringLiteral("MediaNext")) ? QString() : QStringLiteral("Error");
        }, QStringLiteral("next track"));

    r.registerCommand({"предыдущий трек", "previous track"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->pressKey(QStringLiteral("MediaPrev")) ? QString() : QStringLiteral("Error");
        }, QStringLiteral("previous track"));

    r.registerCommand({"громче", "сделай громче", "volume up"},
        [sys](const QString& full) -> QString {
            const int step = extractNumber(full, 10);
            sys->increaseVolume(step);
            return QStringLiteral("Volume %1%").arg(sys->currentVolume());
        }, QStringLiteral("volume up [by N]"));

    r.registerCommand({"тише", "сделай тише", "volume down"},
        [sys](const QString& full) -> QString {
            const int step = extractNumber(full, 10);
            sys->decreaseVolume(step);
            return QStringLiteral("Volume %1%").arg(sys->currentVolume());
        }, QStringLiteral("volume down [by N]"));

    r.registerCommand({"заглуши звук", "mute"},
        [sys](const QString&) -> QString {
            return sys->muteAudio(true) ? QStringLiteral("Muted") : QStringLiteral("Error");
        }, QStringLiteral("mute audio"));

    r.registerCommand({"включи звук", "unmute"},
        [sys](const QString&) -> QString {
            return sys->muteAudio(false) ? QStringLiteral("Unmuted") : QStringLiteral("Error");
        }, QStringLiteral("unmute audio"));

    r.registerCommand({"установи громкость", "громкость"},
        [sys](const QString& full) -> QString {
            const int n = extractNumber(full, -1);
            if (n < 0) return QStringLiteral("Current volume %1%").arg(sys->currentVolume());
            sys->setVolume(n);
            return QStringLiteral("Volume %1%").arg(n);
        }, QStringLiteral("set volume <N> — e.g. 'set volume 50'"));
}

// ============================================================
//  БРАУЗЕР
// ============================================================

void PcCommandRegistry::registerBrowserCommands(CommandRegistry& r)
{
    SystemController* sys = m_pc->system();

    r.registerCommand({"обнови страницу", "refresh", "reload"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->pressKey(QStringLiteral("F5")) ? QString() : QStringLiteral("Error");
        }, QStringLiteral("refresh page — F5"));

    r.registerCommand({"страница назад", "browser back"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->pressKey(QStringLiteral("BrowserBack")) ? QString() : QStringLiteral("Error");
        }, QStringLiteral("page back"));

    r.registerCommand({"страница вперёд", "browser forward"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->pressKey(QStringLiteral("BrowserForward")) ? QString() : QStringLiteral("Error");
        }, QStringLiteral("page forward"));

    r.registerCommand({"открой адресную строку", "address bar"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->pressCombo(QStringLiteral("Ctrl+L")) ? QString() : QStringLiteral("Error");
        }, QStringLiteral("open address bar — Ctrl+L"));

    r.registerCommand({"открой сайт"},
        [sys](const QString& full) -> QString {
            QString url = remainderAfter(full, QStringLiteral("сайт"));
            if (url.isEmpty()) return QStringLiteral("No URL specified");
            if (!url.startsWith(QStringLiteral("http")))
                url = QStringLiteral("https://") + url;
            return sys->launchUrl(url) ? QStringLiteral("Opening ") + url : QStringLiteral("Error");
        },
        QStringLiteral("open site <url> — e.g. 'open site github.com'"),
        /*prefixMatch=*/true);

    r.registerCommand({"открой ютуб", "youtube"},
        [sys](const QString&) -> QString {
            sys->launchUrl(QStringLiteral("https://youtube.com"));
            return QStringLiteral("Opening YouTube");
        }, QStringLiteral("open YouTube"));

    r.registerCommand({"открой гитхаб", "github"},
        [sys](const QString&) -> QString {
            sys->launchUrl(QStringLiteral("https://github.com/Bohdan99py/jarvis"));
            return QStringLiteral("Opening repository");
        }, QStringLiteral("open GitHub"));

    r.registerCommand({"включи музыку", "play music", "ютуб мюзик", "youtube music"},
        [sys](const QString&) -> QString {
            sys->launchUrl(QStringLiteral("https://music.youtube.com"));
            return QStringLiteral("Opening YouTube Music");
        }, QStringLiteral("open YouTube Music"));

    r.registerCommand({"новая вкладка", "new tab"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->newTab() ? QString() : QStringLiteral("Error");
        }, QStringLiteral("new tab — Ctrl+T"));

    r.registerCommand({"закрой вкладку", "close tab"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->closeTab() ? QString() : QStringLiteral("Error");
        }, QStringLiteral("close tab — Ctrl+W"));

    r.registerCommand({"следующая вкладка", "next tab"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->nextTab() ? QString() : QStringLiteral("Error");
        }, QStringLiteral("next tab — Ctrl+Tab"));

    r.registerCommand({"предыдущая вкладка", "previous tab", "prev tab"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->prevTab() ? QString() : QStringLiteral("Error");
        }, QStringLiteral("previous tab — Ctrl+Shift+Tab"));

    r.registerCommand({"полный экран", "fullscreen", "f11"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->pressKey(QStringLiteral("F11")) ? QString() : QStringLiteral("Error");
        }, QStringLiteral("fullscreen toggle — F11"));

    r.registerCommand({"сохрани страницу", "save page"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->save() ? QString() : QStringLiteral("Error");
        }, QStringLiteral("save page — Ctrl+S"));
}

// ============================================================
//  ФАЙЛЫ
// ============================================================

void PcCommandRegistry::registerFileCommands(CommandRegistry& r)
{
    SystemController* sys = m_pc->system();

    r.registerCommand({"найди файл"},
        [sys](const QString& full) -> QString {
            const QString name = remainderAfter(full, QStringLiteral("файл"));
            if (name.isEmpty()) return QStringLiteral("No filename specified");
            const QString pattern = QStringLiteral("*%1*").arg(name);
            const QStringList found = sys->findFiles(pattern);
            if (found.isEmpty()) return QStringLiteral("File not found: ") + name;
            return QStringLiteral("Found %1, first: %2")
                       .arg(found.size()).arg(found.first());
        },
        QStringLiteral("find file <name> — e.g. 'find file report.pdf'"),
        /*prefixMatch=*/true);

    r.registerCommand({"удали файл", "delete file"},
        [](const QString& full) -> QString {
            QString path = remainderAfter(full, QStringLiteral("файл"));
            if (path.isEmpty()) path = remainderAfter(full, QStringLiteral("file"));
            if (path.isEmpty()) return QStringLiteral("Specify file path");
            path = path.trimmed().remove(QLatin1Char('"'));
            QFileInfo fi(path);
            if (!fi.exists()) return QStringLiteral("File not found: ") + path;
            if (fi.isDir()) return QStringLiteral("That's a directory. Use 'удали папку'");
            return QFile::moveToTrash(path)
                ? QStringLiteral("Moved to trash: ") + fi.fileName()
                : QStringLiteral("Failed to delete: ") + path;
        },
        QStringLiteral("delete file <path> — moves to recycle bin"),
        /*prefixMatch=*/true);

    r.registerCommand({"удали папку", "delete folder"},
        [](const QString& full) -> QString {
            QString path = remainderAfter(full, QStringLiteral("папку"));
            if (path.isEmpty()) path = remainderAfter(full, QStringLiteral("folder"));
            if (path.isEmpty()) return QStringLiteral("Specify folder path");
            path = path.trimmed().remove(QLatin1Char('"'));
            QDir dir(path);
            if (!dir.exists()) return QStringLiteral("Folder not found: ") + path;
            return QFile::moveToTrash(path)
                ? QStringLiteral("Moved to trash: ") + dir.dirName()
                : QStringLiteral("Failed to delete: ") + path;
        },
        QStringLiteral("delete folder <path> — moves to recycle bin"),
        /*prefixMatch=*/true);

    r.registerCommand({"открой папку", "open folder"},
        [sys](const QString& full) -> QString {
            QString path = remainderAfter(full, QStringLiteral("папку"));
            if (path.isEmpty()) path = remainderAfter(full, QStringLiteral("folder"));
            if (path.isEmpty()) return QStringLiteral("Specify folder path");
            path = path.trimmed().remove(QLatin1Char('"'));
            return sys->openPath(path) ? QString() : QStringLiteral("Not found: ") + path;
        },
        QStringLiteral("open folder <path>"),
        /*prefixMatch=*/true);

    r.registerCommand({"размер папки", "folder size"},
        [](const QString& full) -> QString {
            QString path = remainderAfter(full, QStringLiteral("папки"));
            if (path.isEmpty()) path = remainderAfter(full, QStringLiteral("size"));
            if (path.isEmpty()) return QStringLiteral("Specify path");
            path = path.trimmed().remove(QLatin1Char('"'));
            QDir dir(path);
            if (!dir.exists()) return QStringLiteral("Not found: ") + path;
            qint64 total = 0;
            QDirIterator it(path, QDirIterator::Subdirectories);
            while (it.hasNext()) { it.next(); total += it.fileInfo().size(); }
            const double mb = total / (1024.0 * 1024.0);
            return QStringLiteral("%1: %2 MB").arg(dir.dirName()).arg(mb, 0, 'f', 1);
        },
        QStringLiteral("folder size <path>"),
        /*prefixMatch=*/true);

    r.registerCommand({"создай папку", "create folder"},
        [sys](const QString& full) -> QString {
            QString name = remainderAfter(full, QStringLiteral("папку"));
            if (name.isEmpty()) name = remainderAfter(full, QStringLiteral("folder"));
            name = name.trimmed().remove(QLatin1Char('"'));
            if (name.isEmpty()) return QStringLiteral("Specify a folder name");

            const QString path = QDir(QStandardPaths::writableLocation(
                QStandardPaths::DesktopLocation)).filePath(name);
            return sys->createFolder(path)
                ? QStringLiteral("Created: ") + path
                : QStringLiteral("Failed to create folder: ") + path;
        },
        QStringLiteral("create folder <name> — creates it on the Desktop"),
        /*prefixMatch=*/true);

    r.registerCommand({"перемести файл", "move file"},
        [sys](const QString& full) -> QString {
            QString rest = remainderAfter(full, QStringLiteral("файл"));
            if (rest.isEmpty()) rest = remainderAfter(full, QStringLiteral("file"));
            if (rest.isEmpty())
                return QStringLiteral("Specify: move file <name> to <folder>");

            int sepIdx = rest.indexOf(QStringLiteral(" в "), 0, Qt::CaseInsensitive);
            if (sepIdx < 0) sepIdx = rest.indexOf(QStringLiteral(" to "), 0, Qt::CaseInsensitive);
            if (sepIdx < 0) return QStringLiteral("Specify a destination — e.g. '... в документы'");

            const QString fileName  = rest.left(sepIdx).trimmed().remove(QLatin1Char('"'));
            const QString destAlias = rest.mid(sepIdx + 3).trimmed().toLower();
            if (fileName.isEmpty()) return QStringLiteral("Specify a file name");

            QStandardPaths::StandardLocation destLoc = QStandardPaths::DownloadLocation;
            if (destAlias.contains(QStringLiteral("рабоч")) || destAlias.contains(QStringLiteral("desktop")))
                destLoc = QStandardPaths::DesktopLocation;
            else if (destAlias.contains(QStringLiteral("документ")) || destAlias.contains(QStringLiteral("document")))
                destLoc = QStandardPaths::DocumentsLocation;
            else if (destAlias.contains(QStringLiteral("изображени")) || destAlias.contains(QStringLiteral("picture")))
                destLoc = QStandardPaths::PicturesLocation;

            // Source must already live inside one of the allowed roots.
            QString sourcePath;
            for (const QString& root : SystemController::allowedOrganizeRoots()) {
                if (root.isEmpty()) continue;
                const QStringList found = sys->findFiles(QStringLiteral("*%1*").arg(fileName), root, 1);
                if (!found.isEmpty()) { sourcePath = found.first(); break; }
            }
            if (sourcePath.isEmpty()) return QStringLiteral("File not found: ") + fileName;

            const QString destDir  = QStandardPaths::writableLocation(destLoc);
            const QString destPath = QDir(destDir).filePath(QFileInfo(sourcePath).fileName());
            return sys->moveFile(sourcePath, destPath)
                ? QStringLiteral("Moved to: ") + destPath
                : QStringLiteral("Failed to move: ") + sourcePath;
        },
        QStringLiteral("move file <name> to <folder> — e.g. 'move file report.pdf to documents'"),
        /*prefixMatch=*/true);
}

// ============================================================
//  МАКРОСЫ (готовые сценарии + запись/воспроизведение голосом)
// ============================================================

void PcCommandRegistry::registerMacroVoiceCommands(CommandRegistry& r)
{
    SystemController*  sys  = m_pc->system();
    WindowController*   wins = m_pc->windows();
    MacroRecorder*      macros = m_macros;

    r.registerCommand({"утренний режим", "доброе утро", "good morning"},
        [sys](const QString&) -> QString {
            sys->launchUrl(QStringLiteral("https://github.com/Bohdan99py/jarvis"));
            QThread::msleep(1500);
            sys->launchApp(QStringLiteral("discord"));
            QThread::msleep(500);
            sys->setVolume(40);
            return QStringLiteral("Good morning! All set.");
        }, QStringLiteral("morning mode — launch work apps"));

    r.registerCommand({"ночной режим", "спокойной ночи", "good night"},
        [sys, wins](const QString&) -> QString {
            wins->closeWindow(QStringLiteral("Chrome"));
            wins->closeWindow(QStringLiteral("Firefox"));
            sys->setVolume(0);
            sys->lockWorkstation();
            return QStringLiteral("Good night!");
        }, QStringLiteral("night mode — close browsers, mute, lock"));

    r.registerCommand({"режим разработки", "начать работу над проектом", "dev mode"},
        [sys](const QString&) -> QString {
            sys->launchApp(QStringLiteral("clion64.exe"));
            QThread::msleep(500);
            sys->launchApp(QStringLiteral("discord"));
            QThread::msleep(500);
            sys->setVolume(25);
            return QStringLiteral("Dev mode. CLion and Discord launched.");
        }, QStringLiteral("dev mode — launch work environment"));

    r.registerCommand({"режим игры", "gaming mode", "хочу поиграть"},
        [sys, wins](const QString&) -> QString {
            wins->closeWindow(QStringLiteral("CLion"));
            wins->closeWindow(QStringLiteral("VS Code"));
            QThread::msleep(500);
            sys->launchApp(QStringLiteral("steam"));
            sys->setVolume(70);
            return QStringLiteral("Gaming mode. IDE closed, Steam launched, volume 70%.");
        }, QStringLiteral("gaming mode — close IDE, launch Steam"));

    r.registerCommand({"режим музыки", "music mode", "включи фоновую музыку"},
        [sys](const QString&) -> QString {
            sys->launchUrl(QStringLiteral("https://music.youtube.com"));
            QThread::msleep(1000);
            sys->setVolume(35);
            return QStringLiteral("Music mode. YouTube Music opened, volume 35%.");
        }, QStringLiteral("music mode — open YouTube Music, set volume"));

    r.registerCommand({"режим презентации", "presentation mode"},
        [sys, this](const QString&) -> QString {
            sys->setVolume(80);
            QThread::msleep(200);
            m_pc->keyboard()->pressKey(QStringLiteral("F11"));
            return QStringLiteral("Presentation mode. Fullscreen, volume 80%.");
        }, QStringLiteral("presentation — fullscreen + loud"));

    r.registerCommand({"всё закрой", "close all", "закрой всё"},
        [wins](const QString&) -> QString {
            const auto windows = wins->allWindows();
            int closed = 0;
            for (const auto& w : windows) {
                if (w.title.contains(QStringLiteral("J.A.R.V.I.S."))) continue;
                if (w.title.contains(QStringLiteral("Explorer"))) continue;
                if (w.title.isEmpty()) continue;
                wins->closeWindow(w.title.left(30));
                ++closed;
                QThread::msleep(100);
            }
            return QStringLiteral("Closed %1 windows").arg(closed);
        }, QStringLiteral("close all windows (except JARVIS)"));

    r.registerCommand({"запиши макрос", "начни запись макроса", "record macro"},
        [macros](const QString& full) -> QString {
            QString name = remainderAfter(full, QStringLiteral("макрос"));
            if (name.isEmpty()) name = QStringLiteral("Unnamed");
            macros->startRecording(name);
            return QStringLiteral("Recording macro '%1'").arg(name);
        },
        QStringLiteral("record macro <name> — start recording mouse/keyboard actions"),
        /*prefixMatch=*/true);

    r.registerCommand({"останови запись макроса", "стоп запись", "stop recording"},
        [macros](const QString&) -> QString {
            macros->stopRecording();
            return QStringLiteral("Recording stopped");
        }, QStringLiteral("stop macro recording"));

    r.registerCommand({"запусти макрос", "воспроизведи макрос", "play macro"},
        [macros](const QString& full) -> QString {
            const QString name = remainderAfter(full, QStringLiteral("макрос"));
            if (name.isEmpty()) return QStringLiteral("No macro name specified");
            return macros->play(name)
                ? QStringLiteral("Playing macro '%1'").arg(name)
                : QStringLiteral("Macro not found: ") + name;
        },
        QStringLiteral("play macro <name>"),
        /*prefixMatch=*/true);
}
