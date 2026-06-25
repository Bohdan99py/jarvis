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

    // ВАЖНО: "запусти " у тебя свободно (не занято cmdTypeText/cmdPressKey/cmdCombo,
    // которые используют "напечатай "/"нажми "/"комбо "), поэтому можно регистрировать.
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
