#pragma once
// -------------------------------------------------------
// pc_controller.h — Низкоуровневый движок управления ПК
// J.A.R.V.I.S. Full PC Voice Control
//
// Win32 SendInput (мышь+клавиатура), Window management,
// WASAPI (громкость), ShellExecute (запуск/файлы).
//
// Не зависит от CommandRegistry — чистый исполнительный слой.
// Голосовая маршрутизация — в pc_command_registry.h/.cpp
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QPoint>
#include <QSize>
#include <QRect>
#include <QMap>

struct HWND__;
using HWND = HWND__*;

// ============================================================
//  Enums
// ============================================================

enum class MouseButton     { Left, Right, Middle };
enum class ScrollDirection { Up, Down, Left, Right };

enum class KeyModifier : unsigned int {
    None  = 0,
    Ctrl  = 1u << 0,
    Alt   = 1u << 1,
    Shift = 1u << 2,
    Win   = 1u << 3
};
inline KeyModifier operator|(KeyModifier a, KeyModifier b)
{
    return static_cast<KeyModifier>(static_cast<unsigned int>(a) | static_cast<unsigned int>(b));
}
inline unsigned int operator&(KeyModifier a, KeyModifier b)
{
    return static_cast<unsigned int>(a) & static_cast<unsigned int>(b);
}

// ============================================================
//  MouseController
// ============================================================

class MouseController : public QObject
{
    Q_OBJECT
public:
    explicit MouseController(QObject* parent = nullptr);

    bool moveTo(int x, int y);
    bool moveToSmooth(int x, int y, int durationMs = 250);

    bool click(MouseButton btn = MouseButton::Left);
    bool doubleClick(MouseButton btn = MouseButton::Left);
    bool rightClick();
    bool middleClick();

    bool clickAt(int x, int y, MouseButton btn = MouseButton::Left);
    bool doubleClickAt(int x, int y);
    bool rightClickAt(int x, int y);

    bool dragFrom(int x1, int y1, int x2, int y2, int durationMs = 400);

    bool scroll(ScrollDirection dir, int clicks = 3);
    bool scrollAt(int x, int y, ScrollDirection dir, int clicks = 3);

    bool pressDown(MouseButton btn = MouseButton::Left);
    bool pressUp(MouseButton btn = MouseButton::Left);

    QPoint currentPos() const;
    static QSize screenSize();
    static int toVirtualX(int px);
    static int toVirtualY(int py);

signals:
    void errorOccurred(const QString& message);

private:
    bool sendMouseInput(unsigned long flags, int x = 0, int y = 0, unsigned long data = 0);
};

// ============================================================
//  KeyboardController
// ============================================================

class KeyboardController : public QObject
{
    Q_OBJECT
public:
    explicit KeyboardController(QObject* parent = nullptr);

    bool pressKey(const QString& keyName, KeyModifier mods = KeyModifier::None);
    bool pressKey(int vkCode, KeyModifier mods = KeyModifier::None);

    bool keyDown(const QString& keyName);
    bool keyUp(const QString& keyName);

    // "Ctrl+Shift+Esc"
    bool pressCombo(const QString& combo);

    bool typeText(const QString& text, int delayBetweenCharsMs = 0);
    bool dictate(const QString& text); // typeText с естественной скоростью

    bool copyClip()       { return pressCombo(QStringLiteral("Ctrl+C")); }
    bool pasteClip()      { return pressCombo(QStringLiteral("Ctrl+V")); }
    bool cutClip()        { return pressCombo(QStringLiteral("Ctrl+X")); }
    bool undo()           { return pressCombo(QStringLiteral("Ctrl+Z")); }
    bool redo()           { return pressCombo(QStringLiteral("Ctrl+Y")); }
    bool selectAll()      { return pressCombo(QStringLiteral("Ctrl+A")); }
    bool save()           { return pressCombo(QStringLiteral("Ctrl+S")); }
    bool find()            { return pressCombo(QStringLiteral("Ctrl+F")); }
    bool newTab()          { return pressCombo(QStringLiteral("Ctrl+T")); }
    bool closeTab()        { return pressCombo(QStringLiteral("Ctrl+W")); }
    bool nextTab()          { return pressCombo(QStringLiteral("Ctrl+Tab")); }
    bool prevTab()          { return pressCombo(QStringLiteral("Ctrl+Shift+Tab")); }
    bool altTab()           { return pressCombo(QStringLiteral("Alt+Tab")); }
    bool taskManagerCombo()  { return pressCombo(QStringLiteral("Ctrl+Shift+Esc")); }
    bool lockScreenCombo()   { return pressCombo(QStringLiteral("Win+L")); }
    bool showDesktopCombo()  { return pressCombo(QStringLiteral("Win+D")); }
    bool openRunCombo()      { return pressCombo(QStringLiteral("Win+R")); }
    bool openSearchCombo()   { return pressCombo(QStringLiteral("Win+S")); }
    bool clipboardHistory()  { return pressCombo(QStringLiteral("Win+V")); }
    bool screenshotFull()    { return pressKey(QStringLiteral("PrintScreen")); }
    bool screenshotArea()    { return pressCombo(QStringLiteral("Win+Shift+S")); }

    static QList<int> parseCombo(const QString& combo);
    static int nameToVK(const QString& name);

signals:
    void errorOccurred(const QString& message);

private:
    bool sendKeyInput(int vkCode, bool keyUp);
    bool sendUnicodeChar(unsigned short ch);
    static const QMap<QString, int>& keyMap();
};

// ============================================================
//  WindowController
// ============================================================

struct WindowInfo {
    HWND    hwnd        = nullptr;
    QString title;
    QString className;
    QRect   rect;
    bool    isVisible    = false;
    bool    isMinimized  = false;
    bool    isMaximized  = false;
    unsigned long pid    = 0;
};

class WindowController : public QObject
{
    Q_OBJECT
public:
    explicit WindowController(QObject* parent = nullptr);

    WindowInfo findWindow(const QString& titlePart) const;
    QList<WindowInfo> allWindows() const;

    bool focusWindow(const QString& titlePart);
    bool focusWindow(HWND hwnd);

    bool minimizeWindow(const QString& titlePart);
    bool maximizeWindow(const QString& titlePart);
    bool restoreWindow(const QString& titlePart);
    bool closeWindow(const QString& titlePart);
    bool moveWindow(const QString& titlePart, int x, int y);
    bool resizeWindow(const QString& titlePart, int w, int h);
    bool setAlwaysOnTop(const QString& titlePart, bool onTop);

    // Действия над текущим активным окном (без поиска по имени)
    bool minimizeActive();
    bool maximizeActive();
    bool closeActive();
    bool alwaysOnTopActive(bool onTop);

    WindowInfo activeWindow() const;
    bool switchToNext(); // Alt+Tab эмуляция

signals:
    void errorOccurred(const QString& message);

private:
    WindowInfo fromHWND(HWND hwnd) const;
};

// ============================================================
//  ClipboardController
// ============================================================

class ClipboardController : public QObject
{
    Q_OBJECT
public:
    explicit ClipboardController(QObject* parent = nullptr);

    QString getText() const;
    bool    setText(const QString& text);
    bool    clear();

signals:
    void errorOccurred(const QString& message);
};

// ============================================================
//  SystemController
// ============================================================

class SystemController : public QObject
{
    Q_OBJECT
public:
    explicit SystemController(QObject* parent = nullptr);

    bool launchApp(const QString& nameOrPath, const QString& args = {});
    bool launchUrl(const QString& url);
    bool openPath(const QString& path); // файл или папка

    bool lockWorkstation();
    bool sleepNow();
    bool shutdownIn(int delaySeconds = 60);
    bool restartIn(int delaySeconds = 60);
    bool cancelShutdown();

    bool setVolume(int percent);
    bool increaseVolume(int step = 10);
    bool decreaseVolume(int step = 10);
    bool muteAudio(bool muted);
    bool toggleMute();
    int  currentVolume() const;

    bool openTaskManager();

    QStringList findFiles(const QString& namePattern,
                          const QString& rootPath = QStringLiteral("C:\\"),
                          int maxResults = 10) const;

signals:
    void volumeChanged(int percent);
    void errorOccurred(const QString& message);
};

// ============================================================
//  PcController  — фасад, агрегирует все под-контроллеры
// ============================================================

class PcController : public QObject
{
    Q_OBJECT
public:
    explicit PcController(QObject* parent = nullptr);

    MouseController*     mouse()     const { return m_mouse;    }
    KeyboardController*  keyboard()  const { return m_keyboard; }
    WindowController*    windows()   const { return m_windows;  }
    ClipboardController* clipboard() const { return m_clip;     }
    SystemController*    system()    const { return m_system;   }

signals:
    void errorOccurred(const QString& message);

private:
    MouseController*     m_mouse    = nullptr;
    KeyboardController*  m_keyboard = nullptr;
    WindowController*    m_windows  = nullptr;
    ClipboardController* m_clip     = nullptr;
    SystemController*    m_system   = nullptr;
};
