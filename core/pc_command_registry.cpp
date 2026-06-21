// -------------------------------------------------------
// pc_command_registry.cpp — Голосовые команды управления ПК
// J.A.R.V.I.S. Full PC Voice Control
// -------------------------------------------------------

#include "pc_command_registry.h"
#include "pc_controller.h"
#include "macro_recorder.h"
#include "command_registry.h"

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
//  Локальные хелперы парсинга (файл-приватные, без утечки в .h)
// ============================================================

namespace {

// Извлечь число (цифрой или словом, RU/EN) из всей фразы.
// Используется, например, для "громче на 20" / "прокрути вверх 5".
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

// Срезать ключевое слово/фразу из начала строки и вернуть остаток
// (то, что пользователь сказал ПОСЛЕ команды).
// "запусти notepad" + "запусти" → "notepad"
QString remainderAfter(const QString& fullInput, const QString& keyword)
{
    const QString lower = fullInput.toLower();
    const int idx = lower.indexOf(keyword.toLower());
    if (idx < 0) return {};
    QString rest = fullInput.mid(idx + keyword.length()).trimmed();
    return rest;
}

} // namespace

// ============================================================
//  Конструктор / деструктор
// ============================================================

PcCommandRegistry::PcCommandRegistry(QObject* parent) : QObject(parent)
{
    m_pc     = new PcController(this);
    m_macros = new MacroRecorder(m_pc, this);

    connect(m_macros, &MacroRecorder::feedbackReady,
            this, &PcCommandRegistry::feedbackReady);
}

PcCommandRegistry::~PcCommandRegistry() = default;

void PcCommandRegistry::setDictationMode(bool on)
{
    m_dictationMode = on;
    emit feedbackReady(on
        ? QStringLiteral("Режим диктовки включён")
        : QStringLiteral("Режим диктовки выключен"));
}

// ============================================================
//  registerInto — точка входа
// ============================================================

void PcCommandRegistry::registerInto(CommandRegistry& registry)
{
    registerMouseCommands(registry);
    registerKeyboardCommands(registry);
    registerWindowCommands(registry);
    registerSystemCommands(registry);
    registerMediaCommands(registry);
    registerTextCommands(registry);
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

    r.registerCommand({"кликни", "клик", "нажми мышью", "click"},
        [mouse](const QString&) -> QString {
            return mouse->click()
                ? QStringLiteral("Клик")
                : QStringLiteral("Ошибка клика");
        },
        QStringLiteral("кликни — левый клик мышью"));

    r.registerCommand({"правый клик", "правая кнопка", "right click", "контекстное меню"},
        [mouse](const QString&) -> QString {
            return mouse->rightClick()
                ? QStringLiteral("Правый клик")
                : QStringLiteral("Ошибка правого клика");
        },
        QStringLiteral("правый клик — контекстное меню"));

    r.registerCommand({"двойной клик", "дабл клик", "double click"},
        [mouse](const QString&) -> QString {
            return mouse->doubleClick()
                ? QStringLiteral("Двойной клик")
                : QStringLiteral("Ошибка двойного клика");
        },
        QStringLiteral("двойной клик"));

    r.registerCommand({"прокрути вверх", "скролл вверх", "scroll up"},
        [mouse](const QString& full) -> QString {
            const int n = extractNumber(full, 3);
            return mouse->scroll(ScrollDirection::Up, n)
                ? QStringLiteral("Прокручено вверх")
                : QStringLiteral("Ошибка скролла");
        },
        QStringLiteral("прокрути вверх [N] — прокрутить колесо вверх"));

    r.registerCommand({"прокрути вниз", "скролл вниз", "scroll down"},
        [mouse](const QString& full) -> QString {
            const int n = extractNumber(full, 3);
            return mouse->scroll(ScrollDirection::Down, n)
                ? QStringLiteral("Прокручено вниз")
                : QStringLiteral("Ошибка скролла");
        },
        QStringLiteral("прокрути вниз [N] — прокрутить колесо вниз"));

    r.registerCommand({"мышь в центр", "курсор в центр", "mouse center"},
        [mouse](const QString&) -> QString {
            const QSize sz = MouseController::screenSize();
            return mouse->moveTo(sz.width() / 2, sz.height() / 2)
                ? QStringLiteral("Курсор в центре")
                : QStringLiteral("Ошибка перемещения");
        },
        QStringLiteral("мышь в центр — курсор в центр экрана"));

    // Сетка экрана 3x3
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
                    ? QStringLiteral("Курсор перемещён")
                    : QStringLiteral("Ошибка перемещения");
            },
            QStringLiteral("позиция курсора по сетке экрана"));
    }
}

// ============================================================
//  КЛАВИАТУРА
// ============================================================

void PcCommandRegistry::registerKeyboardCommands(CommandRegistry& r)
{
    KeyboardController* kb = m_pc->keyboard();

    r.registerCommand({"скопировать", "копировать", "copy"},
        [kb](const QString&) -> QString {
            return kb->copyClip() ? QStringLiteral("Скопировано") : QStringLiteral("Ошибка");
        }, QStringLiteral("скопировать — Ctrl+C"));

    r.registerCommand({"вставить", "вставь", "paste"},
        [kb](const QString&) -> QString {
            return kb->pasteClip() ? QStringLiteral("Вставлено") : QStringLiteral("Ошибка");
        }, QStringLiteral("вставить — Ctrl+V"));

    r.registerCommand({"вырезать", "вырежь", "cut"},
        [kb](const QString&) -> QString {
            return kb->cutClip() ? QStringLiteral("Вырезано") : QStringLiteral("Ошибка");
        }, QStringLiteral("вырезать — Ctrl+X"));

    r.registerCommand({"отменить", "отмена", "undo"},
        [kb](const QString&) -> QString {
            return kb->undo() ? QStringLiteral("Отменено") : QStringLiteral("Ошибка");
        }, QStringLiteral("отменить — Ctrl+Z"));

    r.registerCommand({"повторить действие", "вернуть", "redo"},
        [kb](const QString&) -> QString {
            return kb->redo() ? QStringLiteral("Повторено") : QStringLiteral("Ошибка");
        }, QStringLiteral("повторить действие — Ctrl+Y"));

    r.registerCommand({"выделить всё", "выбрать всё", "select all"},
        [kb](const QString&) -> QString {
            return kb->selectAll() ? QStringLiteral("Выделено всё") : QStringLiteral("Ошибка");
        }, QStringLiteral("выделить всё — Ctrl+A"));

    r.registerCommand({"сохрани файл", "сохранить файл", "save file"},
        [kb](const QString&) -> QString {
            return kb->save() ? QStringLiteral("Сохранено") : QStringLiteral("Ошибка");
        }, QStringLiteral("сохрани файл — Ctrl+S"));

    r.registerCommand({"найди на странице", "поиск на странице", "find on page"},
        [kb](const QString&) -> QString {
            return kb->find() ? QStringLiteral("Поиск открыт") : QStringLiteral("Ошибка");
        }, QStringLiteral("найди на странице — Ctrl+F"));

    r.registerCommand({"нажми энтер", "нажми enter", "энтер", "подтверди ввод"},
        [kb](const QString&) -> QString {
            return kb->pressKey(QStringLiteral("Enter")) ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("нажми энтер"));

    r.registerCommand({"нажми эскейп", "нажми escape", "эскейп", "закрой диалог"},
        [kb](const QString&) -> QString {
            return kb->pressKey(QStringLiteral("Escape")) ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("нажми эскейп"));

    r.registerCommand({"нажми таб", "нажми tab", "следующее поле"},
        [kb](const QString&) -> QString {
            return kb->pressKey(QStringLiteral("Tab")) ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("нажми таб"));

    r.registerCommand({"удали символ", "нажми backspace"},
        [kb](const QString&) -> QString {
            return kb->pressKey(QStringLiteral("Backspace")) ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("удали символ — Backspace"));

    r.registerCommand({"удали вперёд", "нажми delete"},
        [kb](const QString&) -> QString {
            return kb->pressKey(QStringLiteral("Delete")) ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("удали вперёд — Delete"));

    // Стрелки
    struct ArrowCmd { QStringList tr; QString key; };
    static const ArrowCmd arrows[] = {
        {{"стрелка вверх","нажми вверх","arrow up"},     "Up"},
        {{"стрелка вниз","нажми вниз","arrow down"},     "Down"},
        {{"стрелка влево","нажми влево","arrow left"},   "Left"},
        {{"стрелка вправо","нажми вправо","arrow right"},"Right"},
        {{"нажми home","в начало строки"},               "Home"},
        {{"нажми end","в конец строки"},                 "End"},
        {{"страница вверх","page up"},                   "PageUp"},
        {{"страница вниз","page down"},                  "PageDown"},
    };
    for (const auto& a : arrows) {
        const QString key = a.key;
        r.registerCommand(a.tr,
            [kb, key](const QString&) -> QString {
                return kb->pressKey(key) ? QString() : QStringLiteral("Ошибка");
            },
            QStringLiteral("нажать клавишу ") + key);
    }

    // F1-F12
    for (int f = 1; f <= 12; ++f) {
        const QString fname = QStringLiteral("F%1").arg(f);
        const QStringList tr = {
            QStringLiteral("нажми %1").arg(fname.toLower()),
            QStringLiteral("клавиша %1").arg(fname.toLower())
        };
        r.registerCommand(tr,
            [kb, fname](const QString&) -> QString {
                return kb->pressKey(fname) ? QString() : QStringLiteral("Ошибка");
            },
            QStringLiteral("нажать ") + fname);
    }

    r.registerCommand({"alt tab", "переключи окна"},
        [kb](const QString&) -> QString {
            return kb->altTab() ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("alt tab — переключить окно"));

    r.registerCommand({"диспетчер задач", "task manager"},
        [kb](const QString&) -> QString {
            return kb->taskManagerCombo() ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("диспетчер задач — Ctrl+Shift+Esc"));

    r.registerCommand({"сделай скриншот", "скриншот", "снимок экрана"},
        [kb](const QString&) -> QString {
            return kb->screenshotFull() ? QStringLiteral("Скриншот сделан") : QStringLiteral("Ошибка");
        }, QStringLiteral("сделай скриншот — PrintScreen"));

    r.registerCommand({"скриншот области", "снимок области", "выдели экран"},
        [kb](const QString&) -> QString {
            return kb->screenshotArea() ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("скриншот области — Win+Shift+S"));

    r.registerCommand({"новая вкладка", "новый таб", "new tab"},
        [kb](const QString&) -> QString {
            return kb->newTab() ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("новая вкладка — Ctrl+T"));

    r.registerCommand({"закрой вкладку", "close tab"},
        [kb](const QString&) -> QString {
            return kb->closeTab() ? QStringLiteral("Вкладка закрыта") : QStringLiteral("Ошибка");
        }, QStringLiteral("закрой вкладку — Ctrl+W"));

    r.registerCommand({"следующая вкладка", "next tab"},
        [kb](const QString&) -> QString {
            return kb->nextTab() ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("следующая вкладка — Ctrl+Tab"));

    r.registerCommand({"предыдущая вкладка", "previous tab"},
        [kb](const QString&) -> QString {
            return kb->prevTab() ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("предыдущая вкладка — Ctrl+Shift+Tab"));

    // Произвольное комбо: "нажми комбо ctrl shift n"
    r.registerCommand({"нажми комбо", "комбо клавиш"},
        [kb](const QString& full) -> QString {
            QString rest = remainderAfter(full, QStringLiteral("комбо"));
            if (rest.isEmpty()) return QStringLiteral("Не указана комбинация");
            rest.replace(QLatin1Char(' '), QLatin1Char('+'));
            return kb->pressCombo(rest)
                ? QStringLiteral("Нажато: ") + rest
                : QStringLiteral("Неверная комбинация: ") + rest;
        },
        QStringLiteral("нажми комбо <клавиши через пробел> — например 'нажми комбо ctrl shift n'"));
}

// ============================================================
//  ОКНА
// ============================================================

void PcCommandRegistry::registerWindowCommands(CommandRegistry& r)
{
    WindowController* wins = m_pc->windows();

    r.registerCommand({"переключи окно", "другое окно", "switch window"},
        [wins](const QString&) -> QString {
            return wins->switchToNext() ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("переключи окно — Alt+Tab"));

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
                    ? QStringLiteral("Переключился на ") + appName
                    : QStringLiteral("Окно не найдено: ") + appName;
            },
            QStringLiteral("переключиться на окно ") + appName);
    }

    r.registerCommand({"сверни окно", "сверни", "minimize window"},
        [wins](const QString&) -> QString {
            return wins->minimizeActive() ? QStringLiteral("Свёрнуто") : QStringLiteral("Ошибка");
        }, QStringLiteral("сверни окно — свернуть активное окно"));

    r.registerCommand({"разверни окно", "разверни на весь экран", "maximize window"},
        [wins](const QString&) -> QString {
            return wins->maximizeActive() ? QStringLiteral("Развёрнуто") : QStringLiteral("Ошибка");
        }, QStringLiteral("разверни окно — на весь экран"));

    r.registerCommand({"закрой окно", "close window"},
        [wins](const QString&) -> QString {
            return wins->closeActive() ? QStringLiteral("Окно закрыто") : QStringLiteral("Ошибка");
        }, QStringLiteral("закрой окно — закрыть активное окно"));

    r.registerCommand({"закрепи окно поверх", "окно поверх всех", "always on top"},
        [wins](const QString&) -> QString {
            return wins->alwaysOnTopActive(true)
                ? QStringLiteral("Закреплено поверх остальных")
                : QStringLiteral("Ошибка");
        }, QStringLiteral("закрепи окно поверх — always on top"));

    r.registerCommand({"открепи окно", "убери поверх всех"},
        [wins](const QString&) -> QString {
            return wins->alwaysOnTopActive(false) ? QStringLiteral("Откреплено") : QStringLiteral("Ошибка");
        }, QStringLiteral("открепи окно — снять always on top"));

    r.registerCommand({"покажи рабочий стол", "show desktop"},
        [this](const QString&) -> QString {
            return m_pc->keyboard()->showDesktopCombo() ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("покажи рабочий стол — Win+D"));
}

// ============================================================
//  СИСТЕМА
// ============================================================

void PcCommandRegistry::registerSystemCommands(CommandRegistry& r)
{
    SystemController* sys = m_pc->system();
    KeyboardController* kb = m_pc->keyboard();

    r.registerCommand({"заблокируй компьютер", "заблокируй", "lock"},
        [sys](const QString&) -> QString {
            return sys->lockWorkstation() ? QStringLiteral("Заблокировано") : QStringLiteral("Ошибка");
        }, QStringLiteral("заблокируй компьютер"));

    r.registerCommand({"спящий режим", "уйди в сон", "sleep mode"},
        [sys](const QString&) -> QString {
            return sys->sleepNow() ? QStringLiteral("Ухожу в сон") : QStringLiteral("Ошибка");
        }, QStringLiteral("спящий режим"));

    r.registerCommand({"выключи компьютер", "shutdown"},
        [sys](const QString& full) -> QString {
            const int delay = extractNumber(full, 60);
            sys->shutdownIn(delay);
            return QStringLiteral("Выключение через %1 секунд").arg(delay);
        }, QStringLiteral("выключи компьютер [через N секунд]"));

    r.registerCommand({"перезагрузи компьютер", "restart", "reboot"},
        [sys](const QString& full) -> QString {
            const int delay = extractNumber(full, 60);
            sys->restartIn(delay);
            return QStringLiteral("Перезагрузка через %1 секунд").arg(delay);
        }, QStringLiteral("перезагрузи компьютер [через N секунд]"));

    r.registerCommand({"отмени выключение", "cancel shutdown"},
        [sys](const QString&) -> QString {
            return sys->cancelShutdown()
                ? QStringLiteral("Выключение отменено")
                : QStringLiteral("Нечего отменять");
        }, QStringLiteral("отмени выключение"));

    r.registerCommand({"запусти", "запустить приложение"},
        [sys](const QString& full) -> QString {
            const QString app = remainderAfter(full, QStringLiteral("запусти"));
            if (app.isEmpty()) return QStringLiteral("Не указано приложение");
            return sys->launchApp(app)
                ? QStringLiteral("Запускаю ") + app
                : QStringLiteral("Не найдено: ") + app;
        },
        QStringLiteral("запусти <приложение> — например 'запусти notepad'"),
        /*prefixMatch=*/true);

    r.registerCommand({"открой выполнить", "win r"},
        [kb](const QString&) -> QString {
            return kb->openRunCombo() ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("открой выполнить — Win+R"));

    r.registerCommand({"поиск windows", "windows search"},
        [kb](const QString&) -> QString {
            return kb->openSearchCombo() ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("поиск windows — Win+S"));

    r.registerCommand({"открой проводник", "file explorer"},
        [sys](const QString&) -> QString {
            return sys->launchApp(QStringLiteral("explorer.exe")) ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("открой проводник"));

    r.registerCommand({"открой калькулятор", "calculator"},
        [sys](const QString&) -> QString {
            return sys->launchApp(QStringLiteral("calc.exe")) ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("открой калькулятор"));

    r.registerCommand({"открой настройки windows", "settings"},
        [sys](const QString&) -> QString {
            return sys->launchApp(QStringLiteral("ms-settings:")) ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("открой настройки windows"));
}

// ============================================================
//  МЕДИА
// ============================================================

void PcCommandRegistry::registerMediaCommands(CommandRegistry& r)
{
    KeyboardController* kb  = m_pc->keyboard();
    SystemController*   sys = m_pc->system();

    r.registerCommand({"пауза музыка", "play pause", "плей"},
        [kb](const QString&) -> QString {
            return kb->pressKey(QStringLiteral("MediaPlay")) ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("пауза музыка — play/pause"));

    r.registerCommand({"следующий трек", "next track"},
        [kb](const QString&) -> QString {
            return kb->pressKey(QStringLiteral("MediaNext")) ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("следующий трек"));

    r.registerCommand({"предыдущий трек", "previous track"},
        [kb](const QString&) -> QString {
            return kb->pressKey(QStringLiteral("MediaPrev")) ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("предыдущий трек"));

    r.registerCommand({"громче", "сделай громче", "volume up"},
        [sys](const QString& full) -> QString {
            const int step = extractNumber(full, 10);
            sys->increaseVolume(step);
            return QStringLiteral("Громкость %1%").arg(sys->currentVolume());
        }, QStringLiteral("громче [на N]"));

    r.registerCommand({"тише", "сделай тише", "volume down"},
        [sys](const QString& full) -> QString {
            const int step = extractNumber(full, 10);
            sys->decreaseVolume(step);
            return QStringLiteral("Громкость %1%").arg(sys->currentVolume());
        }, QStringLiteral("тише [на N]"));

    r.registerCommand({"заглуши звук", "mute"},
        [sys](const QString&) -> QString {
            return sys->muteAudio(true) ? QStringLiteral("Звук выключен") : QStringLiteral("Ошибка");
        }, QStringLiteral("заглуши звук"));

    r.registerCommand({"включи звук", "unmute"},
        [sys](const QString&) -> QString {
            return sys->muteAudio(false) ? QStringLiteral("Звук включён") : QStringLiteral("Ошибка");
        }, QStringLiteral("включи звук"));

    r.registerCommand({"установи громкость", "громкость"},
        [sys](const QString& full) -> QString {
            const int n = extractNumber(full, -1);
            if (n < 0) return QStringLiteral("Текущая громкость %1%").arg(sys->currentVolume());
            sys->setVolume(n);
            return QStringLiteral("Громкость %1%").arg(n);
        }, QStringLiteral("установи громкость <N> — например 'установи громкость 50'"));
}

// ============================================================
//  ТЕКСТ И ДИКТОВКА
// ============================================================

void PcCommandRegistry::registerTextCommands(CommandRegistry& r)
{
    KeyboardController* kb = m_pc->keyboard();

    r.registerCommand({"начни диктовку", "начать диктовку", "режим диктовки"},
        [this](const QString&) -> QString {
            setDictationMode(true);
            return QStringLiteral("Диктовка включена, говорите");
        }, QStringLiteral("начни диктовку — текст печатается как есть"));

    r.registerCommand({"останови диктовку", "остановить диктовку", "стоп диктовка"},
        [this](const QString&) -> QString {
            setDictationMode(false);
            return QStringLiteral("Диктовка выключена");
        }, QStringLiteral("останови диктовку"));

    r.registerCommand({"напечатай", "напиши текст"},
        [kb](const QString& full) -> QString {
            const QString text = remainderAfter(full, QStringLiteral("напечатай"));
            if (text.isEmpty()) return QStringLiteral("Нет текста для печати");
            return kb->dictate(text) ? QStringLiteral("Напечатано") : QStringLiteral("Ошибка ввода");
        },
        QStringLiteral("напечатай <текст> — ввести текст в активное окно"),
        /*prefixMatch=*/true);

    r.registerCommand({"вставь текст из буфера", "paste clipboard"},
        [kb](const QString&) -> QString {
            return kb->pasteClip() ? QStringLiteral("Вставлено") : QStringLiteral("Ошибка");
        }, QStringLiteral("вставь текст из буфера"));

    r.registerCommand({"история буфера обмена", "clipboard history"},
        [kb](const QString&) -> QString {
            return kb->clipboardHistory() ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("история буфера обмена — Win+V"));
}

// ============================================================
//  БРАУЗЕР
// ============================================================

void PcCommandRegistry::registerBrowserCommands(CommandRegistry& r)
{
    KeyboardController* kb  = m_pc->keyboard();
    SystemController*   sys = m_pc->system();

    r.registerCommand({"обнови страницу", "refresh", "reload"},
        [kb](const QString&) -> QString {
            return kb->pressKey(QStringLiteral("F5")) ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("обнови страницу — F5"));

    r.registerCommand({"страница назад", "browser back"},
        [kb](const QString&) -> QString {
            return kb->pressKey(QStringLiteral("BrowserBack")) ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("страница назад"));

    r.registerCommand({"страница вперёд", "browser forward"},
        [kb](const QString&) -> QString {
            return kb->pressKey(QStringLiteral("BrowserForward")) ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("страница вперёд"));

    r.registerCommand({"открой адресную строку", "address bar"},
        [kb](const QString&) -> QString {
            return kb->pressCombo(QStringLiteral("Ctrl+L")) ? QString() : QStringLiteral("Ошибка");
        }, QStringLiteral("открой адресную строку — Ctrl+L"));

    r.registerCommand({"открой сайт", "перейди на сайт"},
        [sys](const QString& full) -> QString {
            QString url = remainderAfter(full, QStringLiteral("сайт"));
            if (url.isEmpty()) return QStringLiteral("Не указан адрес сайта");
            if (!url.startsWith(QStringLiteral("http")))
                url = QStringLiteral("https://") + url;
            return sys->launchUrl(url) ? QStringLiteral("Открываю ") + url : QStringLiteral("Ошибка");
        },
        QStringLiteral("открой сайт <адрес> — например 'открой сайт github.com'"),
        /*prefixMatch=*/true);

    r.registerCommand({"открой ютуб", "youtube"},
        [sys](const QString&) -> QString {
            sys->launchUrl(QStringLiteral("https://youtube.com"));
            return QStringLiteral("Открываю YouTube");
        }, QStringLiteral("открой ютуб"));

    r.registerCommand({"открой гитхаб", "github"},
        [sys](const QString&) -> QString {
            sys->launchUrl(QStringLiteral("https://github.com/Bohdan99py/jarvis"));
            return QStringLiteral("Открываю репозиторий");
        }, QStringLiteral("открой гитхаб"));
}

// ============================================================
//  ФАЙЛЫ
// ============================================================

void PcCommandRegistry::registerFileCommands(CommandRegistry& r)
{
    SystemController* sys = m_pc->system();

    r.registerCommand({"найди файл", "поиск файла"},
        [sys](const QString& full) -> QString {
            const QString name = remainderAfter(full, QStringLiteral("файл"));
            if (name.isEmpty()) return QStringLiteral("Не указано имя файла");
            const QString pattern = QStringLiteral("*%1*").arg(name);
            const QStringList found = sys->findFiles(pattern);
            if (found.isEmpty()) return QStringLiteral("Файл не найден: ") + name;
            return QStringLiteral("Найдено %1, первый: %2")
                       .arg(found.size()).arg(found.first());
        },
        QStringLiteral("найди файл <имя> — например 'найди файл report.pdf'"),
        /*prefixMatch=*/true);
}

// ============================================================
//  МАКРОСЫ (голосовые триггеры готовых сценариев)
// ============================================================

void PcCommandRegistry::registerMacroVoiceCommands(CommandRegistry& r)
{
    SystemController* sys = m_pc->system();
    WindowController* wins = m_pc->windows();

    r.registerCommand({"утренний режим", "доброе утро"},
        [sys](const QString&) -> QString {
            sys->launchUrl(QStringLiteral("https://github.com/Bohdan99py/jarvis"));
            QThread::msleep(1500);
            sys->launchApp(QStringLiteral("discord"));
            QThread::msleep(500);
            sys->setVolume(40);
            return QStringLiteral("Доброе утро! Всё готово.");
        }, QStringLiteral("утренний режим — запустить рабочие приложения"));

    r.registerCommand({"ночной режим", "спокойной ночи"},
        [sys, wins](const QString&) -> QString {
            wins->closeWindow(QStringLiteral("Chrome"));
            wins->closeWindow(QStringLiteral("Firefox"));
            sys->setVolume(0);
            sys->lockWorkstation();
            return QStringLiteral("Спокойной ночи!");
        }, QStringLiteral("ночной режим — закрыть браузеры, заглушить, заблокировать"));

    r.registerCommand({"режим разработки", "начать работу над проектом"},
        [sys](const QString&) -> QString {
            sys->launchApp(QStringLiteral("clion64.exe"));
            QThread::msleep(500);
            sys->launchApp(QStringLiteral("discord"));
            QThread::msleep(500);
            sys->setVolume(25);
            return QStringLiteral("Режим разработки. CLion и Discord запущены.");
        }, QStringLiteral("режим разработки — запустить рабочее окружение"));

    // Произвольные пользовательские макросы — запись/воспроизведение
    MacroRecorder* macros = m_macros;

    r.registerCommand({"запиши макрос", "начни запись макроса"},
        [macros](const QString& full) -> QString {
            QString name = remainderAfter(full, QStringLiteral("макрос"));
            if (name.isEmpty()) name = QStringLiteral("Безымянный");
            macros->startRecording(name);
            return QStringLiteral("Записываю макрос «%1»").arg(name);
        },
        QStringLiteral("запиши макрос <имя> — начать запись действий"),
        /*prefixMatch=*/true);

    r.registerCommand({"останови запись макроса", "стоп запись"},
        [macros](const QString&) -> QString {
            macros->stopRecording();
            return QStringLiteral("Запись остановлена");
        }, QStringLiteral("останови запись макроса"));

    r.registerCommand({"запусти макрос", "воспроизведи макрос"},
        [macros](const QString& full) -> QString {
            const QString name = remainderAfter(full, QStringLiteral("макрос"));
            if (name.isEmpty()) return QStringLiteral("Не указано имя макроса");
            return macros->play(name)
                ? QStringLiteral("Выполняю макрос «%1»").arg(name)
                : QStringLiteral("Макрос не найден: ") + name;
        },
        QStringLiteral("запусти макрос <имя>"),
        /*prefixMatch=*/true);
}
