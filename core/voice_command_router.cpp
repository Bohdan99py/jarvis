// ============================================================
// voice_command_router.cpp  —  J.A.R.V.I.S.
// Маршрутизатор голосовых команд → действия ПК
// C++17 / Qt6 / MSVC 2022
// ============================================================

#include "voice_command_router.h"
#include "pc_controller.h"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
#include <algorithm>

// ============================================================
//  Утилиты парсинга
// ============================================================

int VoiceCommandRouter::extractNumber(const QStringList& tokens, int defaultVal)
{
    // Ищем цифровое число или числительное в русском/английском
    static const QMap<QString,int> words = {
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
    for (const auto& t : tokens) {
        bool ok = false;
        int n = t.toInt(&ok);
        if (ok) return n;
        auto it = words.find(t.toLower());
        if (it != words.end()) return it.value();
    }
    return defaultVal;
}

QString VoiceCommandRouter::extractQuoted(const QString& text)
{
    // Извлечь текст в кавычках или после "напечатай / написать / type"
    int start = text.indexOf('"');
    int end   = text.lastIndexOf('"');
    if (start >= 0 && end > start)
        return text.mid(start + 1, end - start - 1);
    return {};
}

QString VoiceCommandRouter::tokensAfter(const QStringList& tokens, int idx)
{
    if (idx + 1 >= tokens.size()) return {};
    return tokens.mid(idx + 1).join(' ');
}

// ============================================================
//  Конструктор
// ============================================================

VoiceCommandRouter::VoiceCommandRouter(PcController* pc, QObject* parent)
    : QObject(parent), m_pc(pc)
{
    registerAll();
}

void VoiceCommandRouter::registerAll()
{
    registerMouseCommands();
    registerKeyboardCommands();
    registerWindowCommands();
    registerSystemCommands();
    registerMediaCommands();
    registerTextCommands();
    registerBrowserCommands();
    registerFileCommands();
    registerMacroCommands();
}

void VoiceCommandRouter::addCommand(const CommandEntry& entry)
{
    m_commands.append(entry);
}

// ============================================================
//  Матчинг
// ============================================================

int VoiceCommandRouter::matchCommand(const QString& normalized) const
{
    // Сначала точное вхождение триггера в строку
    for (int i = 0; i < m_commands.size(); ++i) {
        for (const auto& trigger : m_commands[i].triggers) {
            if (normalized.contains(trigger))
                return i;
        }
    }
    return -1;
}

// ============================================================
//  route()  — главная точка входа
// ============================================================

CommandResult VoiceCommandRouter::route(const QString& spokenText)
{
    if (spokenText.trimmed().isEmpty())
        return CommandResult::fail({});

    // Режим диктовки — печатаем всё как есть
    if (m_dictationMode) {
        bool ok = m_pc->keyboard()->dictate(spokenText);
        return ok ? CommandResult::ok() : CommandResult::fail(QStringLiteral("Ошибка диктовки"));
    }

    const QString norm = spokenText.toLower().trimmed();
    const QStringList tokens = norm.split(' ', Qt::SkipEmptyParts);

    int idx = matchCommand(norm);
    if (idx < 0) {
        emit unhandled(spokenText);  // → AI
        return CommandResult::fail({});
    }

    CommandResult result = m_commands[idx].handler(tokens);
    emit commandMatched(m_commands[idx].triggers.first(), result.success);
    if (!result.feedback.isEmpty())
        emit feedbackReady(result.feedback);
    return result;
}

void VoiceCommandRouter::setDictationMode(bool on)
{
    m_dictationMode = on;
    emit feedbackReady(on
        ? QStringLiteral("Режим диктовки включён")
        : QStringLiteral("Режим диктовки выключен"));
}

QStringList VoiceCommandRouter::helpList() const
{
    QStringList list;
    for (const auto& cmd : m_commands)
        list << cmd.triggers.first() + QStringLiteral(" — ") + cmd.description;
    return list;
}

// ============================================================
//  РЕГИСТРАЦИЯ КОМАНД
// ============================================================

// ----------  МЫШЬ  ------------------------------------------

void VoiceCommandRouter::registerMouseCommands()
{
    auto* mouse = m_pc->mouse();

    // --- Клик ---
    m_commands.append({
        {"кликни", "клик", "нажми мышью", "click"},
        [mouse](const QStringList&) -> CommandResult {
            return mouse->click()
                ? CommandResult::ok(QStringLiteral("Клик"))
                : CommandResult::fail(QStringLiteral("Ошибка клика"));
        },
        QStringLiteral("Левый клик мышью")
    });

    // --- Правый клик ---
    m_commands.append({
        {"правый клик", "правая кнопка", "right click", "контекстное меню"},
        [mouse](const QStringList&) -> CommandResult {
            return mouse->rightClick()
                ? CommandResult::ok()
                : CommandResult::fail(QStringLiteral("Ошибка правого клика"));
        },
        QStringLiteral("Правый клик — контекстное меню")
    });

    // --- Двойной клик ---
    m_commands.append({
        {"двойной клик", "дабл клик", "double click", "открыть двойным"},
        [mouse](const QStringList&) -> CommandResult {
            return mouse->doubleClick()
                ? CommandResult::ok()
                : CommandResult::fail(QStringLiteral("Ошибка двойного клика"));
        },
        QStringLiteral("Двойной клик")
    });

    // --- Скролл вверх ---
    m_commands.append({
        {"прокрути вверх", "скролл вверх", "scroll up", "листай вверх"},
        [mouse](const QStringList& tokens) -> CommandResult {
            int n = VoiceCommandRouter::extractNumber(tokens, 3);
            return mouse->scroll(ScrollDirection::Up, n)
                ? CommandResult::ok()
                : CommandResult::fail(QStringLiteral("Ошибка скролла"));
        },
        QStringLiteral("Прокрутить страницу вверх")
    });

    // --- Скролл вниз ---
    m_commands.append({
        {"прокрути вниз", "скролл вниз", "scroll down", "листай вниз"},
        [mouse](const QStringList& tokens) -> CommandResult {
            int n = VoiceCommandRouter::extractNumber(tokens, 3);
            return mouse->scroll(ScrollDirection::Down, n)
                ? CommandResult::ok()
                : CommandResult::fail(QStringLiteral("Ошибка скролла"));
        },
        QStringLiteral("Прокрутить страницу вниз")
    });

    // --- Переместить мышь в центр ---
    m_commands.append({
        {"мышь в центр", "курсор в центр", "mouse center"},
        [mouse](const QStringList&) -> CommandResult {
            auto sz = MouseController::screenSize();
            return mouse->moveTo(sz.width()/2, sz.height()/2)
                ? CommandResult::ok()
                : CommandResult::fail({});
        },
        QStringLiteral("Переместить курсор в центр экрана")
    });

    // --- Переместить по сетке экрана ---
    // "мышь верхний левый" / "мышь правый нижний" / "мышь центр"
    struct GridPoint { QStringList names; float rx, ry; };
    static const GridPoint grid[] = {
        {{"верхний левый", "top left"},             0.1f, 0.1f},
        {{"верхний центр", "top center"},           0.5f, 0.1f},
        {{"верхний правый","top right"},            0.9f, 0.1f},
        {{"левый",  "left side"},                   0.1f, 0.5f},
        {{"центр",  "center", "середина"},          0.5f, 0.5f},
        {{"правый", "right side"},                  0.9f, 0.5f},
        {{"нижний левый",  "bottom left"},          0.1f, 0.9f},
        {{"нижний центр",  "bottom center"},        0.5f, 0.9f},
        {{"нижний правый", "bottom right"},         0.9f, 0.9f},
    };

    for (const auto& gp : grid) {
        float rx = gp.rx, ry = gp.ry;
        QStringList triggers;
        for (const auto& n : gp.names)
            triggers << QStringLiteral("мышь %1").arg(n)
                     << QStringLiteral("курсор %1").arg(n)
                     << QStringLiteral("mouse %1").arg(n);

        m_commands.append({
            triggers,
            [mouse, rx, ry](const QStringList&) -> CommandResult {
                auto sz = MouseController::screenSize();
                int x = static_cast<int>(sz.width()  * rx);
                int y = static_cast<int>(sz.height() * ry);
                return mouse->moveToSmooth(x, y, 200)
                    ? CommandResult::ok()
                    : CommandResult::fail({});
            },
            QStringLiteral("Переместить курсор")
        });
    }
}

// ----------  КЛАВИАТУРА  ------------------------------------

void VoiceCommandRouter::registerKeyboardCommands()
{
    auto* kb = m_pc->keyboard();

    // --- Копировать / Вставить / Вырезать ---
    m_commands.append({{"скопировать", "копировать", "copy","ctrl c"},
        [kb](const QStringList&) -> CommandResult {
            return kb->copy() ? CommandResult::ok(QStringLiteral("Скопировано"))
                              : CommandResult::fail({});
        }, QStringLiteral("Ctrl+C — скопировать")});

    m_commands.append({{"вставить", "вставь", "paste","ctrl v"},
        [kb](const QStringList&) -> CommandResult {
            return kb->paste() ? CommandResult::ok(QStringLiteral("Вставлено"))
                               : CommandResult::fail({});
        }, QStringLiteral("Ctrl+V — вставить")});

    m_commands.append({{"вырезать", "вырежь", "cut","ctrl x"},
        [kb](const QStringList&) -> CommandResult {
            return kb->cut() ? CommandResult::ok()
                             : CommandResult::fail({});
        }, QStringLiteral("Ctrl+X — вырезать")});

    m_commands.append({{"отменить", "отмена", "undo","ctrl z"},
        [kb](const QStringList&) -> CommandResult {
            return kb->undo() ? CommandResult::ok(QStringLiteral("Отменено"))
                              : CommandResult::fail({});
        }, QStringLiteral("Ctrl+Z — отменить")});

    m_commands.append({{"повторить", "вернуть", "redo","ctrl y"},
        [kb](const QStringList&) -> CommandResult {
            return kb->redo() ? CommandResult::ok()
                              : CommandResult::fail({});
        }, QStringLiteral("Ctrl+Y — повторить")});

    m_commands.append({{"выделить всё", "выбрать всё", "select all","ctrl a"},
        [kb](const QStringList&) -> CommandResult {
            return kb->selectAll() ? CommandResult::ok()
                                   : CommandResult::fail({});
        }, QStringLiteral("Ctrl+A — выделить всё")});

    m_commands.append({{"сохранить", "сохрани", "save","ctrl s"},
        [kb](const QStringList&) -> CommandResult {
            return kb->save() ? CommandResult::ok(QStringLiteral("Сохранено"))
                              : CommandResult::fail({});
        }, QStringLiteral("Ctrl+S — сохранить")});

    m_commands.append({{"найти", "поиск", "find","ctrl f"},
        [kb](const QStringList&) -> CommandResult {
            return kb->find() ? CommandResult::ok()
                              : CommandResult::fail({});
        }, QStringLiteral("Ctrl+F — открыть поиск")});

    // --- Навигация ---
    m_commands.append({{"enter", "энтер", "нажми enter", "подтверди"},
        [kb](const QStringList&) -> CommandResult {
            return kb->pressKey(QStringLiteral("Enter"))
                ? CommandResult::ok() : CommandResult::fail({});
        }, QStringLiteral("Нажать Enter")});

    m_commands.append({{"escape", "эскейп", "отмени", "закрой диалог"},
        [kb](const QStringList&) -> CommandResult {
            return kb->pressKey(QStringLiteral("Escape"))
                ? CommandResult::ok() : CommandResult::fail({});
        }, QStringLiteral("Нажать Escape")});

    m_commands.append({{"tab", "таб", "следующее поле"},
        [kb](const QStringList&) -> CommandResult {
            return kb->pressKey(QStringLiteral("Tab"))
                ? CommandResult::ok() : CommandResult::fail({});
        }, QStringLiteral("Нажать Tab")});

    m_commands.append({{"backspace", "удалить символ", "удали символ"},
        [kb](const QStringList&) -> CommandResult {
            return kb->pressKey(QStringLiteral("Backspace"))
                ? CommandResult::ok() : CommandResult::fail({});
        }, QStringLiteral("Нажать Backspace")});

    m_commands.append({{"delete", "удалить вперёд"},
        [kb](const QStringList&) -> CommandResult {
            return kb->pressKey(QStringLiteral("Delete"))
                ? CommandResult::ok() : CommandResult::fail({});
        }, QStringLiteral("Нажать Delete")});

    // Стрелки
    const struct { QStringList tr; QString key; } arrows[] = {
        {{"стрелка вверх","вверх","arrow up","up"},     "Up"},
        {{"стрелка вниз","вниз","arrow down","down"},   "Down"},
        {{"стрелка влево","влево","arrow left","left"},  "Left"},
        {{"стрелка вправо","вправо","arrow right","right"},"Right"},
        {{"home","начало строки"},                       "Home"},
        {{"end","конец строки"},                         "End"},
        {{"page up","страница вверх"},                   "PageUp"},
        {{"page down","страница вниз"},                  "PageDown"},
    };
    for (const auto& a : arrows) {
        QString key = a.key;
        m_commands.append({
            a.tr,
            [kb, key](const QStringList&) -> CommandResult {
                return kb->pressKey(key) ? CommandResult::ok() : CommandResult::fail({});
            },
            QStringLiteral("Стрелка ") + key
        });
    }

    // --- Функциональные клавиши ---
    for (int f = 1; f <= 12; ++f) {
        QString fname = QStringLiteral("F%1").arg(f);
        QStringList tr = { fname.toLower(),
                           QStringLiteral("нажми %1").arg(fname.toLower()),
                           QStringLiteral("клавиша %1").arg(fname.toLower()) };
        m_commands.append({
            tr,
            [kb, fname](const QStringList&) -> CommandResult {
                return kb->pressKey(fname) ? CommandResult::ok() : CommandResult::fail({});
            },
            QStringLiteral("Нажать ") + fname
        });
    }

    // --- Комбинации ---
    m_commands.append({{"alt tab","переключить окна"},
        [kb](const QStringList&) -> CommandResult {
            return kb->altTab() ? CommandResult::ok() : CommandResult::fail({});
        }, QStringLiteral("Alt+Tab — переключить окно")});

    m_commands.append({{"диспетчер задач","task manager","ctrl shift esc"},
        [kb](const QStringList&) -> CommandResult {
            return kb->taskManager() ? CommandResult::ok() : CommandResult::fail({});
        }, QStringLiteral("Ctrl+Shift+Esc — диспетчер задач")});

    m_commands.append({{"скриншот","screenshot","printscreen","снимок экрана"},
        [kb](const QStringList&) -> CommandResult {
            return kb->screenshot() ? CommandResult::ok(QStringLiteral("Скриншот сделан"))
                                    : CommandResult::fail({});
        }, QStringLiteral("PrintScreen — снимок экрана")});

    m_commands.append({{"скриншот области","снимок области","win shift s","выделить экран"},
        [kb](const QStringList&) -> CommandResult {
            return kb->screenshotArea() ? CommandResult::ok()
                                        : CommandResult::fail({});
        }, QStringLiteral("Win+Shift+S — снимок области")});

    m_commands.append({{"новая вкладка","новый таб","new tab"},
        [kb](const QStringList&) -> CommandResult {
            return kb->openNewTab() ? CommandResult::ok() : CommandResult::fail({});
        }, QStringLiteral("Ctrl+T — новая вкладка")});

    m_commands.append({{"закрыть вкладку","close tab"},
        [kb](const QStringList&) -> CommandResult {
            return kb->closeTab() ? CommandResult::ok(QStringLiteral("Вкладка закрыта"))
                                  : CommandResult::fail({});
        }, QStringLiteral("Ctrl+W — закрыть вкладку")});

    m_commands.append({{"следующая вкладка","next tab"},
        [kb](const QStringList&) -> CommandResult {
            return kb->nextTab() ? CommandResult::ok() : CommandResult::fail({});
        }, QStringLiteral("Ctrl+Tab — следующая вкладка")});

    m_commands.append({{"предыдущая вкладка","previous tab"},
        [kb](const QStringList&) -> CommandResult {
            return kb->prevTab() ? CommandResult::ok() : CommandResult::fail({});
        }, QStringLiteral("Ctrl+Shift+Tab — предыдущая вкладка")});

    // --- Ввод произвольного комбо ---
    m_commands.append({
        {"комбо", "combo", "нажми комбо"},
        [kb](const QStringList& tokens) -> CommandResult {
            // "нажми комбо ctrl+shift+n"
            for (int i = 0; i < tokens.size(); ++i) {
                if (tokens[i] == QStringLiteral("комбо") ||
                    tokens[i] == QStringLiteral("combo")) {
                    QString rest = VoiceCommandRouter::tokensAfter(tokens, i);
                    rest.replace(' ', '+');
                    return kb->pressCombo(rest)
                        ? CommandResult::ok(QStringLiteral("Комбо ") + rest)
                        : CommandResult::fail(QStringLiteral("Неверная комбинация"));
                }
            }
            return CommandResult::fail(QStringLiteral("Не найдена комбинация"));
        },
        QStringLiteral("Нажать произвольную комбинацию клавиш: 'комбо ctrl+shift+n'")
    });
}

// ----------  ОКНА  ------------------------------------------

void VoiceCommandRouter::registerWindowCommands()
{
    auto* wins = m_pc->windows();

    m_commands.append({
        {"переключи окно", "другое окно", "switch window"},
        [wins](const QStringList&) -> CommandResult {
            return wins->switchToNext() ? CommandResult::ok()
                                        : CommandResult::fail({});
        }, QStringLiteral("Переключить на следующее окно (Alt+Tab)")
    });

    // Фокус на окно по имени: "открой хром" / "переключись на visual studio"
    const struct { QStringList tr; QString name; } apps[] = {
        {{"открой хром","переключись на chrome","chrome","хром"},      "Chrome"},
        {{"открой edge","переключись на edge","edge"},                  "Edge"},
        {{"firefox","файрфокс"},                                        "Firefox"},
        {{"visual studio","вижуал студио"},                             "Visual Studio"},
        {{"clion","клион"},                                             "CLion"},
        {{"explorer","проводник","файловый менеджер"},                  "Explorer"},
        {{"notepad","блокнот"},                                         "Notepad"},
        {{"discord","дискорд"},                                         "Discord"},
        {{"telegram","телеграм"},                                       "Telegram"},
        {{"unreal engine","ue5","unreal","анрил"},                     "Unreal"},
        {{"cursor","курсор ide"},                                       "Cursor"},
        {{"vs code","vscode","виэс код"},                              "Visual Studio Code"},
    };

    for (const auto& a : apps) {
        QString appName = a.name;
        m_commands.append({
            a.tr,
            [wins, appName](const QStringList&) -> CommandResult {
                bool ok = wins->focusWindow(appName);
                return ok ? CommandResult::ok(QStringLiteral("Переключился на ") + appName)
                          : CommandResult::fail(QStringLiteral("Окно не найдено: ") + appName);
            },
            QStringLiteral("Переключиться на ") + appName
        });
    }

    m_commands.append({
        {"свернуть", "minimize", "свернуть окно"},
        [wins](const QStringList&) -> CommandResult {
            auto info = wins->activeWindow();
            if (!info.hwnd) return CommandResult::fail({});
            ShowWindow(info.hwnd, SW_MINIMIZE);
            return CommandResult::ok(QStringLiteral("Свёрнуто"));
        }, QStringLiteral("Свернуть активное окно")
    });

    m_commands.append({
        {"развернуть", "maximize", "на весь экран"},
        [wins](const QStringList&) -> CommandResult {
            auto info = wins->activeWindow();
            if (!info.hwnd) return CommandResult::fail({});
            ShowWindow(info.hwnd, SW_MAXIMIZE);
            return CommandResult::ok();
        }, QStringLiteral("Развернуть окно на весь экран")
    });

    m_commands.append({
        {"закрыть окно", "close window", "alt f4"},
        [wins](const QStringList&) -> CommandResult {
            auto info = wins->activeWindow();
            if (!info.hwnd) return CommandResult::fail({});
            PostMessage(info.hwnd, WM_CLOSE, 0, 0);
            return CommandResult::ok(QStringLiteral("Окно закрыто"));
        }, QStringLiteral("Закрыть активное окно")
    });

    m_commands.append({
        {"поверх всех", "always on top", "закрепить окно"},
        [wins](const QStringList&) -> CommandResult {
            auto info = wins->activeWindow();
            if (!info.hwnd) return CommandResult::fail({});
            wins->setAlwaysOnTop(info.title, true);
            return CommandResult::ok(QStringLiteral("Окно закреплено поверх остальных"));
        }, QStringLiteral("Поставить окно поверх всех")
    });

    m_commands.append({
        {"рабочий стол", "показать рабочий стол", "show desktop","win d"},
        [this](const QStringList&) -> CommandResult {
            return m_pc->keyboard()->showDesktop()
                ? CommandResult::ok()
                : CommandResult::fail({});
        }, QStringLiteral("Win+D — показать рабочий стол")
    });
}

// ----------  СИСТЕМА  ---------------------------------------

void VoiceCommandRouter::registerSystemCommands()
{
    auto* sys  = m_pc->system();
    auto* kb   = m_pc->keyboard();

    m_commands.append({{"заблокировать","заблокируй","lock","блокировка"},
        [sys](const QStringList&) -> CommandResult {
            return sys->lockWorkstation()
                ? CommandResult::ok(QStringLiteral("Заблокировано"))
                : CommandResult::fail({});
        }, QStringLiteral("Заблокировать компьютер")});

    m_commands.append({{"спящий режим","sleep","в сон"},
        [sys](const QStringList&) -> CommandResult {
            return sys->sleep()
                ? CommandResult::ok(QStringLiteral("Уходим в сон"))
                : CommandResult::fail({});
        }, QStringLiteral("Спящий режим")});

    m_commands.append({{"выключить","shutdown","выключи компьютер"},
        [sys](const QStringList& tokens) -> CommandResult {
            int delay = VoiceCommandRouter::extractNumber(tokens, 60);
            sys->shutdown(delay);
            return CommandResult::ok(QStringLiteral("Выключение через %1 секунд").arg(delay));
        }, QStringLiteral("Выключить компьютер (выключить через N секунд)")});

    m_commands.append({{"перезагрузить","restart","reboot","перезагрузи"},
        [sys](const QStringList& tokens) -> CommandResult {
            int delay = VoiceCommandRouter::extractNumber(tokens, 60);
            sys->restart(delay);
            return CommandResult::ok(QStringLiteral("Перезагрузка через %1 секунд").arg(delay));
        }, QStringLiteral("Перезагрузить компьютер")});

    m_commands.append({{"отмена выключения","cancel shutdown"},
        [sys](const QStringList&) -> CommandResult {
            return sys->cancelShutdown()
                ? CommandResult::ok(QStringLiteral("Выключение отменено"))
                : CommandResult::fail({});
        }, QStringLiteral("Отменить таймер выключения")});

    m_commands.append({{"запусти","открой приложение","launch","запустить"},
        [sys](const QStringList& tokens) -> CommandResult {
            // "запусти notepad" / "открой приложение calc"
            for (int i = 0; i < tokens.size(); ++i) {
                if (tokens[i] == QStringLiteral("запусти") ||
                    tokens[i] == QStringLiteral("launch")  ||
                    tokens[i] == QStringLiteral("запустить")) {
                    QString app = VoiceCommandRouter::tokensAfter(tokens, i);
                    return sys->launchApp(app)
                        ? CommandResult::ok(QStringLiteral("Запускаю ") + app)
                        : CommandResult::fail(QStringLiteral("Не найдено: ") + app);
                }
            }
            return CommandResult::fail({});
        }, QStringLiteral("Запустить приложение: 'запусти notepad'")});

    // Win+R — Выполнить
    m_commands.append({{"выполнить","run dialog","win r"},
        [kb](const QStringList&) -> CommandResult {
            return kb->openRun() ? CommandResult::ok()
                                 : CommandResult::fail({});
        }, QStringLiteral("Win+R — открыть «Выполнить»")});

    // Поиск Windows
    m_commands.append({{"поиск windows","win search","windows search"},
        [kb](const QStringList&) -> CommandResult {
            return kb->openSearch() ? CommandResult::ok()
                                    : CommandResult::fail({});
        }, QStringLiteral("Win+S — поиск Windows")});

    m_commands.append({{"проводник","file explorer","explorer"},
        [sys](const QStringList&) -> CommandResult {
            return sys->launchApp(QStringLiteral("explorer.exe"))
                ? CommandResult::ok()
                : CommandResult::fail({});
        }, QStringLiteral("Открыть проводник")});

    m_commands.append({{"калькулятор","calculator"},
        [sys](const QStringList&) -> CommandResult {
            return sys->launchApp(QStringLiteral("calc.exe"))
                ? CommandResult::ok()
                : CommandResult::fail({});
        }, QStringLiteral("Открыть калькулятор")});

    m_commands.append({{"блокнот","notepad"},
        [sys](const QStringList&) -> CommandResult {
            return sys->launchApp(QStringLiteral("notepad.exe"))
                ? CommandResult::ok()
                : CommandResult::fail({});
        }, QStringLiteral("Открыть блокнот")});

    m_commands.append({{"настройки","settings","параметры"},
        [sys](const QStringList&) -> CommandResult {
            return sys->launchApp(QStringLiteral("ms-settings:"))
                ? CommandResult::ok()
                : CommandResult::fail({});
        }, QStringLiteral("Открыть Параметры Windows")});
}

// ----------  МЕДИА  -----------------------------------------

void VoiceCommandRouter::registerMediaCommands()
{
    auto* kb  = m_pc->keyboard();
    auto* sys = m_pc->system();

    m_commands.append({{"пауза","pause","play pause","плей"},
        [kb](const QStringList&) -> CommandResult {
            return kb->pressKey(QStringLiteral("MediaPlay"))
                ? CommandResult::ok()
                : CommandResult::fail({});
        }, QStringLiteral("Play/Pause медиа")});

    m_commands.append({{"следующий трек","next track","следующая"},
        [kb](const QStringList&) -> CommandResult {
            return kb->pressKey(QStringLiteral("MediaNext"))
                ? CommandResult::ok()
                : CommandResult::fail({});
        }, QStringLiteral("Следующий трек")});

    m_commands.append({{"предыдущий трек","previous track","предыдущая"},
        [kb](const QStringList&) -> CommandResult {
            return kb->pressKey(QStringLiteral("MediaPrev"))
                ? CommandResult::ok()
                : CommandResult::fail({});
        }, QStringLiteral("Предыдущий трек")});

    m_commands.append({{"громче","volume up","увеличить громкость"},
        [sys](const QStringList& tokens) -> CommandResult {
            int step = VoiceCommandRouter::extractNumber(tokens, 10);
            return sys->increaseVolume(step)
                ? CommandResult::ok(QStringLiteral("Громкость %1%").arg(sys->currentVolume()))
                : CommandResult::fail({});
        }, QStringLiteral("Увеличить громкость")});

    m_commands.append({{"тише","volume down","уменьшить громкость"},
        [sys](const QStringList& tokens) -> CommandResult {
            int step = VoiceCommandRouter::extractNumber(tokens, 10);
            return sys->decreaseVolume(step)
                ? CommandResult::ok(QStringLiteral("Громкость %1%").arg(sys->currentVolume()))
                : CommandResult::fail({});
        }, QStringLiteral("Уменьшить громкость")});

    m_commands.append({{"заглушить","mute","выключить звук"},
        [sys](const QStringList&) -> CommandResult {
            return sys->mute(true)
                ? CommandResult::ok(QStringLiteral("Звук выключен"))
                : CommandResult::fail({});
        }, QStringLiteral("Заглушить звук")});

    m_commands.append({{"включить звук","unmute","восстановить звук"},
        [sys](const QStringList&) -> CommandResult {
            return sys->mute(false)
                ? CommandResult::ok(QStringLiteral("Звук включён"))
                : CommandResult::fail({});
        }, QStringLiteral("Включить звук")});

    m_commands.append({{"громкость","volume","установить громкость"},
        [sys](const QStringList& tokens) -> CommandResult {
            int n = VoiceCommandRouter::extractNumber(tokens, -1);
            if (n < 0) {
                return CommandResult::ok(
                    QStringLiteral("Громкость %1%").arg(sys->currentVolume()));
            }
            return sys->setVolume(n)
                ? CommandResult::ok(QStringLiteral("Громкость %1%").arg(n))
                : CommandResult::fail({});
        }, QStringLiteral("Установить громкость: 'громкость 50'")});
}

// ----------  ТЕКСТ И ДИКТОВКА  ------------------------------

void VoiceCommandRouter::registerTextCommands()
{
    auto* kb   = m_pc->keyboard();
    auto* clip = m_pc->clipboard();

    // Режим диктовки
    m_commands.append({{"начать диктовку","dictation mode","диктовка"},
        [this](const QStringList&) -> CommandResult {
            setDictationMode(true);
            return CommandResult::ok(QStringLiteral("Диктовка включена. Говорите."));
        }, QStringLiteral("Включить режим диктовки")});

    m_commands.append({{"остановить диктовку","stop dictation","стоп диктовка"},
        [this](const QStringList&) -> CommandResult {
            setDictationMode(false);
            return CommandResult::ok(QStringLiteral("Диктовка выключена"));
        }, QStringLiteral("Выключить режим диктовки")});

    // Печатать конкретный текст
    m_commands.append({
        {"напечатай","напиши","type","print text","введи"},
        [kb](const QStringList& tokens) -> CommandResult {
            // "напечатай hello world"
            for (int i = 0; i < tokens.size(); ++i) {
                if (tokens[i] == QStringLiteral("напечатай") ||
                    tokens[i] == QStringLiteral("напиши")    ||
                    tokens[i] == QStringLiteral("type")      ||
                    tokens[i] == QStringLiteral("введи")) {
                    QString text = VoiceCommandRouter::tokensAfter(tokens, i);
                    return kb->dictate(text)
                        ? CommandResult::ok()
                        : CommandResult::fail(QStringLiteral("Ошибка ввода"));
                }
            }
            return CommandResult::fail({});
        },
        QStringLiteral("Напечатать текст: 'напечатай hello world'")
    });

    // Вставить из буфера
    m_commands.append({{"вставить текст","вставь текст","paste clipboard"},
        [kb](const QStringList&) -> CommandResult {
            return kb->paste()
                ? CommandResult::ok(QStringLiteral("Вставлено"))
                : CommandResult::fail({});
        }, QStringLiteral("Вставить текст из буфера обмена")});

    // Буфер обмена — история
    m_commands.append({{"история буфера","clipboard history","win v"},
        [this](const QStringList&) -> CommandResult {
            return m_pc->system()->openClipboardHistory()
                ? CommandResult::ok()
                : CommandResult::fail({});
        }, QStringLiteral("Win+V — история буфера обмена")});

    // Новая строка
    m_commands.append({{"новая строка","enter","перенос","new line"},
        [kb](const QStringList&) -> CommandResult {
            return kb->pressKey(QStringLiteral("Enter"))
                ? CommandResult::ok()
                : CommandResult::fail({});
        }, QStringLiteral("Нажать Enter (новая строка)")});
}

// ----------  БРАУЗЕР  ---------------------------------------

void VoiceCommandRouter::registerBrowserCommands()
{
    auto* kb  = m_pc->keyboard();
    auto* sys = m_pc->system();

    m_commands.append({{"обновить","обнови","refresh","reload","f5"},
        [kb](const QStringList&) -> CommandResult {
            return kb->pressKey(QStringLiteral("F5"))
                ? CommandResult::ok()
                : CommandResult::fail({});
        }, QStringLiteral("F5 — обновить страницу")});

    m_commands.append({{"назад","back","browser back"},
        [kb](const QStringList&) -> CommandResult {
            return kb->pressKey(QStringLiteral("BrowserBack"))
                ? CommandResult::ok()
                : CommandResult::fail({});
        }, QStringLiteral("Браузер — назад")});

    m_commands.append({{"вперёд","forward","browser forward"},
        [kb](const QStringList&) -> CommandResult {
            return kb->pressKey(QStringLiteral("BrowserForward"))
                ? CommandResult::ok()
                : CommandResult::fail({});
        }, QStringLiteral("Браузер — вперёд")});

    m_commands.append({{"адресная строка","address bar","ctrl l","открыть адрес"},
        [kb](const QStringList&) -> CommandResult {
            return kb->pressCombo(QStringLiteral("Ctrl+L"))
                ? CommandResult::ok()
                : CommandResult::fail({});
        }, QStringLiteral("Ctrl+L — перейти в адресную строку")});

    m_commands.append({
        {"открой сайт","перейди на","navigate to","зайди на"},
        [kb, sys](const QStringList& tokens) -> CommandResult {
            for (int i = 0; i < tokens.size(); ++i) {
                if (tokens[i] == QStringLiteral("сайт")   ||
                    tokens[i] == QStringLiteral("на")      ||
                    tokens[i] == QStringLiteral("navigate")||
                    tokens[i] == QStringLiteral("to")) {
                    QString url = VoiceCommandRouter::tokensAfter(tokens, i);
                    if (!url.startsWith(QStringLiteral("http")))
                        url = QStringLiteral("https://") + url;
                    return sys->launchUrl(url)
                        ? CommandResult::ok(QStringLiteral("Открываю ") + url)
                        : CommandResult::fail({});
                }
            }
            return CommandResult::fail({});
        },
        QStringLiteral("Открыть сайт: 'открой сайт github.com'")
    });

    m_commands.append({{"ютуб","youtube"},
        [sys](const QStringList&) -> CommandResult {
            sys->launchUrl(QStringLiteral("https://youtube.com"));
            return CommandResult::ok(QStringLiteral("Открываю YouTube"));
        }, QStringLiteral("Открыть YouTube")});

    m_commands.append({{"гитхаб","github"},
        [sys](const QStringList&) -> CommandResult {
            sys->launchUrl(QStringLiteral("https://github.com/Bohdan99py/jarvis"));
            return CommandResult::ok(QStringLiteral("Открываю репозиторий"));
        }, QStringLiteral("Открыть GitHub репозиторий")});
}

// ----------  ФАЙЛЫ  -----------------------------------------

void VoiceCommandRouter::registerFileCommands()
{
    auto* sys = m_pc->system();

    m_commands.append({
        {"найди файл","find file","поиск файла"},
        [this, sys](const QStringList& tokens) -> CommandResult {
            for (int i = 0; i < tokens.size(); ++i) {
                if (tokens[i] == QStringLiteral("файл") ||
                    tokens[i] == QStringLiteral("file")) {
                    QString pattern = QStringLiteral("*") +
                                      VoiceCommandRouter::tokensAfter(tokens, i) +
                                      QStringLiteral("*");
                    QStringList found = sys->findFiles(pattern);
                    if (found.isEmpty())
                        return CommandResult::fail(QStringLiteral("Файл не найден"));
                    emit feedbackReady(QStringLiteral("Найдено %1 файлов, первый: %2")
                                           .arg(found.size()).arg(found.first()));
                    return CommandResult::ok();
                }
            }
            return CommandResult::fail({});
        },
        QStringLiteral("Найти файл: 'найди файл report.pdf'")
    });
}

// ----------  МАКРОСЫ  ---------------------------------------

void VoiceCommandRouter::registerMacroCommands()
{
    // Утренний режим
    m_commands.append({
        {"утренний режим","morning mode","доброе утро"},
        [this](const QStringList&) -> CommandResult {
            auto* sys = m_pc->system();
            auto* kb  = m_pc->keyboard();
            // 1. Запустить браузер
            sys->launchUrl(QStringLiteral("https://github.com/Bohdan99py/jarvis"));
            QThread::msleep(1500);
            // 2. Открыть Discord
            sys->launchApp(QStringLiteral("discord"));
            QThread::msleep(500);
            // 3. Громкость 40%
            sys->setVolume(40);
            return CommandResult::ok(QStringLiteral("Доброе утро! Всё готово."));
        },
        QStringLiteral("Утренний режим — запустить рабочие приложения")
    });

    // Ночной режим
    m_commands.append({
        {"ночной режим","night mode","спокойной ночи"},
        [this](const QStringList&) -> CommandResult {
            auto* sys  = m_pc->system();
            auto* wins = m_pc->windows();
            // Закрываем все браузеры
            wins->closeWindow(QStringLiteral("Chrome"));
            wins->closeWindow(QStringLiteral("Firefox"));
            sys->setVolume(0);
            sys->lockWorkstation();
            return CommandResult::ok(QStringLiteral("Спокойной ночи!"));
        },
        QStringLiteral("Ночной режим — закрыть браузеры, заглушить, заблокировать")
    });

    // Режим разработки
    m_commands.append({
        {"режим разработки","dev mode","начать работу"},
        [this](const QStringList&) -> CommandResult {
            auto* sys = m_pc->system();
            sys->launchApp(QStringLiteral("clion64.exe"));
            QThread::msleep(500);
            sys->launchApp(QStringLiteral("discord"));
            QThread::msleep(500);
            sys->setVolume(25);
            return CommandResult::ok(QStringLiteral("Режим разработки. CLion и Discord запущены."));
        },
        QStringLiteral("Запустить рабочее окружение разработчика")
    });
}
