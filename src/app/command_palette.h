#pragma once
// -------------------------------------------------------
// command_palette.h — Ctrl+Space и Ctrl+K из любого места
//
// Одно окно, два режима — потому что разница между ними
// в поведении, а не во внешнем виде:
//
//   Ctrl+Space  Act   — задание уходит агенту, в списке
//                       видно шаги выполнения
//   Ctrl+K      Find  — мгновенный локальный поиск без
//                       модели и без сети, Enter открывает
//                       или запускает найденное
//
// Tab переключает режимы, не закрывая окно: половина
// запросов начинается как поиск и заканчивается заданием.
// -------------------------------------------------------

#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QObject>
#include <QPoint>
#include <QVector>
#include <QWidget>

#include <functional>

#include "global_search.h"

class Jarvis;
class QLabel;
class QLineEdit;
class QListWidget;

// ============================================================
//  GlobalHotkey — RegisterHotKey + фильтр нативных сообщений
// ============================================================
class GlobalHotkey : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit GlobalHotkey(QObject* parent = nullptr);
    ~GlobalHotkey() override;

    // modifiers — MOD_CONTROL | MOD_ALT | ..., virtualKey — VK_*.
    //
    // Окно, которому Windows доставит WM_HOTKEY, здесь своё: скрытое,
    // общее на все хоткеи, живущее столько же, сколько процесс. Раньше
    // им было главное окно — то есть горячие клавиши существовали ровно
    // столько, сколько существовал его HWND, и «JARVIS без окна» упирался
    // в это в первую очередь.
    bool registerHotkey(quint32 modifiers, quint32 virtualKey);
    void unregisterHotkey();

    bool isRegistered() const { return m_registered; }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void activated();

private:
    quintptr m_owner      = 0;
    int      m_id         = 0;
    bool     m_registered = false;
};

// ============================================================
//  CommandPalette
// ============================================================
class CommandPalette : public QWidget
{
    Q_OBJECT

public:
    enum class Mode { Act, Find };

    explicit CommandPalette(Jarvis* jarvis, QWidget* parent = nullptr);

    void showPalette(Mode mode = Mode::Act);

    // Команды интерфейса (пункты меню) живут в ActionRegistry на стороне
    // окна, а Jarvis про них не знает — поэтому их запуск палитре
    // передают снаружи, а не через Jarvis::activateSearchHit.
    void setCommandRunner(std::function<void(const QString& actionId)> runner);
    void togglePalette(Mode mode = Mode::Act);

protected:
    void keyPressEvent(QKeyEvent* event) override;

    // Панель без рамки: у неё нет ни заголовка, чтобы её тащить, ни
    // крестика, чтобы закрыть. Плюс она поверх всех окон. Пока эти три
    // события не обработаны, увод фокуса мышью оставляет её висеть на
    // экране навсегда — закрыть можно было только выходом из JARVIS.
    void changeEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void setMode(Mode mode);
    void onTextChanged(const QString& text);
    void submit();          // Enter
    void runSearch(const QString& query);
    void activateCurrentHit();

    void addLine(const QString& text, const QString& color);
    void clearList();
    void moveSelection(int delta);

    Jarvis*      m_jarvis = nullptr;
    QLineEdit*   m_input  = nullptr;
    QListWidget* m_list   = nullptr;
    QLabel*      m_hint   = nullptr;

    std::function<void(const QString&)> m_commandRunner;

    Mode               m_mode = Mode::Act;
    bool               m_busy = false;

    QPoint             m_dragFrom;          // курсор относительно окна
    bool               m_dragging = false;
    QElapsedTimer      m_shownAt;           // защита от «показалась и сразу спряталась»
    QVector<SearchHit> m_hits;   // индексы совпадают с data(Qt::UserRole) строк
};

// Вешает на палитру её горячие клавиши: Ctrl+Space (выполнить) и
// Ctrl+K (найти). Отдельная функция, потому что комбинации задаются
// константами Win32, и тащить <windows.h> в main() ради двух чисел
// не стоит. Объекты хоткеев становятся детьми parent.
//
// О том, что комбинация занята другой программой, сообщает лентой
// событий: молча остаться без Ctrl+Space хуже, чем узнать об этом.
void installPaletteHotkeys(CommandPalette* palette, QObject* parent);
