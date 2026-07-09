// -------------------------------------------------------
// pc_controller.cpp — Низкоуровневый движок управления ПК
// J.A.R.V.I.S. Full PC Voice Control
// -------------------------------------------------------

#include "pc_controller.h"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <powrprof.h>

#include <QThread>
#include <QGuiApplication>
#include <QScreen>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

// ============================================================
//  Helpers
// ============================================================

namespace {

void msDelay(int ms)
{
    if (ms > 0)
        QThread::msleep(static_cast<unsigned long>(ms));
}

// RAII-обёртка для громкости через WASAPI — избегаем дублирования
// инициализации COM в каждом методе SystemController.
class AudioEndpoint
{
public:
    AudioEndpoint()
    {
        CoInitialize(nullptr);
        IMMDeviceEnumerator* enumerator = nullptr;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator),
                                      reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr) || !enumerator) return;

        IMMDevice* device = nullptr;
        enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        enumerator->Release();
        if (!device) return;

        device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                         reinterpret_cast<void**>(&m_volume));
        device->Release();
    }

    ~AudioEndpoint()
    {
        if (m_volume) m_volume->Release();
    }

    bool isValid() const { return m_volume != nullptr; }
    IAudioEndpointVolume* operator->() const { return m_volume; }

private:
    IAudioEndpointVolume* m_volume = nullptr;
};

} // namespace

// ============================================================
//  MouseController
// ============================================================

MouseController::MouseController(QObject* parent) : QObject(parent) {}

QSize MouseController::screenSize()
{
    return QGuiApplication::primaryScreen()->size();
}

int MouseController::toVirtualX(int px)
{
    int sw = GetSystemMetrics(SM_CXSCREEN);
    return sw > 0 ? (px * 65535) / sw : 0;
}

int MouseController::toVirtualY(int py)
{
    int sh = GetSystemMetrics(SM_CYSCREEN);
    return sh > 0 ? (py * 65535) / sh : 0;
}

bool MouseController::sendMouseInput(unsigned long flags, int x, int y, unsigned long data)
{
    INPUT inp = {};
    inp.type           = INPUT_MOUSE;
    inp.mi.dwFlags     = flags;
    inp.mi.dx          = x;
    inp.mi.dy          = y;
    inp.mi.mouseData   = data;
    inp.mi.dwExtraInfo = 0;

    if (SendInput(1, &inp, sizeof(INPUT)) != 1) {
        emit errorOccurred(QStringLiteral("SendInput (mouse) failed: %1").arg(GetLastError()));
        return false;
    }
    return true;
}

bool MouseController::moveTo(int x, int y)
{
    return sendMouseInput(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE,
                          toVirtualX(x), toVirtualY(y));
}

bool MouseController::moveToSmooth(int x, int y, int durationMs)
{
    const QPoint from = currentPos();
    const int steps = qMax(1, durationMs / 10);
    for (int i = 1; i <= steps; ++i) {
        const int cx = from.x() + (x - from.x()) * i / steps;
        const int cy = from.y() + (y - from.y()) * i / steps;
        if (!moveTo(cx, cy)) return false;
        msDelay(10);
    }
    return true;
}

bool MouseController::click(MouseButton btn)
{
    unsigned long downFlag = MOUSEEVENTF_LEFTDOWN, upFlag = MOUSEEVENTF_LEFTUP;
    if (btn == MouseButton::Right)  { downFlag = MOUSEEVENTF_RIGHTDOWN;  upFlag = MOUSEEVENTF_RIGHTUP;  }
    if (btn == MouseButton::Middle) { downFlag = MOUSEEVENTF_MIDDLEDOWN; upFlag = MOUSEEVENTF_MIDDLEUP; }

    if (!sendMouseInput(downFlag)) return false;
    msDelay(20);
    return sendMouseInput(upFlag);
}

bool MouseController::doubleClick(MouseButton btn)
{
    if (!click(btn)) return false;
    msDelay(60);
    return click(btn);
}

bool MouseController::rightClick()  { return click(MouseButton::Right);  }
bool MouseController::middleClick() { return click(MouseButton::Middle); }

bool MouseController::clickAt(int x, int y, MouseButton btn)
{
    if (!moveTo(x, y)) return false;
    msDelay(30);
    return click(btn);
}

bool MouseController::doubleClickAt(int x, int y)
{
    if (!moveTo(x, y)) return false;
    msDelay(30);
    return doubleClick();
}

bool MouseController::rightClickAt(int x, int y)
{
    if (!moveTo(x, y)) return false;
    msDelay(30);
    return rightClick();
}

bool MouseController::pressDown(MouseButton btn)
{
    unsigned long flag = MOUSEEVENTF_LEFTDOWN;
    if (btn == MouseButton::Right)  flag = MOUSEEVENTF_RIGHTDOWN;
    if (btn == MouseButton::Middle) flag = MOUSEEVENTF_MIDDLEDOWN;
    return sendMouseInput(flag);
}

bool MouseController::pressUp(MouseButton btn)
{
    unsigned long flag = MOUSEEVENTF_LEFTUP;
    if (btn == MouseButton::Right)  flag = MOUSEEVENTF_RIGHTUP;
    if (btn == MouseButton::Middle) flag = MOUSEEVENTF_MIDDLEUP;
    return sendMouseInput(flag);
}

bool MouseController::dragFrom(int x1, int y1, int x2, int y2, int durationMs)
{
    if (!moveTo(x1, y1)) return false;
    msDelay(50);
    if (!pressDown())     return false;
    msDelay(50);
    if (!moveToSmooth(x2, y2, durationMs)) { pressUp(); return false; }
    msDelay(50);
    return pressUp();
}

bool MouseController::scroll(ScrollDirection dir, int clicks)
{
    if (dir == ScrollDirection::Up || dir == ScrollDirection::Down) {
        unsigned long data = (dir == ScrollDirection::Up)
                             ? static_cast<unsigned long>(WHEEL_DELTA * clicks)
                             : static_cast<unsigned long>(-WHEEL_DELTA * clicks);
        return sendMouseInput(MOUSEEVENTF_WHEEL, 0, 0, data);
    }
    unsigned long data = (dir == ScrollDirection::Right)
                         ? static_cast<unsigned long>(WHEEL_DELTA * clicks)
                         : static_cast<unsigned long>(-WHEEL_DELTA * clicks);
    return sendMouseInput(MOUSEEVENTF_HWHEEL, 0, 0, data);
}

bool MouseController::scrollAt(int x, int y, ScrollDirection dir, int clicks)
{
    if (!moveTo(x, y)) return false;
    msDelay(30);
    return scroll(dir, clicks);
}

QPoint MouseController::currentPos() const
{
    POINT p;
    GetCursorPos(&p);
    return QPoint(p.x, p.y);
}

// ============================================================
//  KeyboardController
// ============================================================

KeyboardController::KeyboardController(QObject* parent) : QObject(parent) {}

const QMap<QString, int>& KeyboardController::keyMap()
{
    static QMap<QString, int> map = {
        {"F1",VK_F1}, {"F2",VK_F2}, {"F3",VK_F3}, {"F4",VK_F4},
        {"F5",VK_F5}, {"F6",VK_F6}, {"F7",VK_F7}, {"F8",VK_F8},
        {"F9",VK_F9}, {"F10",VK_F10}, {"F11",VK_F11}, {"F12",VK_F12},

        {"Enter",VK_RETURN}, {"Return",VK_RETURN},
        {"Escape",VK_ESCAPE}, {"Esc",VK_ESCAPE},
        {"Tab",VK_TAB},
        {"Space",VK_SPACE},
        {"Backspace",VK_BACK},
        {"Delete",VK_DELETE}, {"Del",VK_DELETE},
        {"Insert",VK_INSERT}, {"Ins",VK_INSERT},
        {"Home",VK_HOME},
        {"End",VK_END},
        {"PageUp",VK_PRIOR}, {"PgUp",VK_PRIOR},
        {"PageDown",VK_NEXT}, {"PgDn",VK_NEXT},

        {"Left",VK_LEFT}, {"Right",VK_RIGHT},
        {"Up",VK_UP},     {"Down",VK_DOWN},

        {"Ctrl",VK_CONTROL}, {"Control",VK_CONTROL},
        {"Alt",VK_MENU},
        {"Shift",VK_SHIFT},
        {"Win",VK_LWIN}, {"Windows",VK_LWIN},
        {"RCtrl",VK_RCONTROL}, {"RAlt",VK_RMENU}, {"RShift",VK_RSHIFT},

        {"CapsLock",VK_CAPITAL},
        {"NumLock",VK_NUMLOCK},
        {"ScrollLock",VK_SCROLL},
        {"PrintScreen",VK_SNAPSHOT}, {"PrtScn",VK_SNAPSHOT},
        {"Pause",VK_PAUSE},
        {"Apps",VK_APPS},

        {"Num0",VK_NUMPAD0}, {"Num1",VK_NUMPAD1}, {"Num2",VK_NUMPAD2},
        {"Num3",VK_NUMPAD3}, {"Num4",VK_NUMPAD4}, {"Num5",VK_NUMPAD5},
        {"Num6",VK_NUMPAD6}, {"Num7",VK_NUMPAD7}, {"Num8",VK_NUMPAD8},
        {"Num9",VK_NUMPAD9}, {"NumMul",VK_MULTIPLY}, {"NumDiv",VK_DIVIDE},
        {"NumAdd",VK_ADD}, {"NumSub",VK_SUBTRACT}, {"NumDot",VK_DECIMAL},

        {"0",'0'},{"1",'1'},{"2",'2'},{"3",'3'},{"4",'4'},
        {"5",'5'},{"6",'6'},{"7",'7'},{"8",'8'},{"9",'9'},

        {"MediaPlay",VK_MEDIA_PLAY_PAUSE}, {"MediaStop",VK_MEDIA_STOP},
        {"MediaNext",VK_MEDIA_NEXT_TRACK}, {"MediaPrev",VK_MEDIA_PREV_TRACK},
        {"VolumeUp",VK_VOLUME_UP}, {"VolumeDown",VK_VOLUME_DOWN},
        {"VolumeMute",VK_VOLUME_MUTE},

        {"BrowserBack",VK_BROWSER_BACK}, {"BrowserForward",VK_BROWSER_FORWARD},
        {"BrowserRefresh",VK_BROWSER_REFRESH},

        {";",VK_OEM_1}, {"=",VK_OEM_PLUS}, {",",VK_OEM_COMMA},
        {"-",VK_OEM_MINUS}, {"/",VK_OEM_2}, {"`",VK_OEM_3},
        {"[",VK_OEM_4}, {"\\",VK_OEM_5}, {"]",VK_OEM_6}, {"'",VK_OEM_7},
    };

    static bool lettersAdded = false;
    if (!lettersAdded) {
        lettersAdded = true;
        for (char c = 'A'; c <= 'Z'; ++c) {
            map[QString(QChar(c))]                 = static_cast<int>(c);
            map[QString(QChar(c)).toLower()]       = static_cast<int>(c);
        }
    }
    return map;
}

int KeyboardController::nameToVK(const QString& name)
{
    const auto& m = keyMap();
    auto it = m.find(name);
    if (it != m.end()) return it.value();
    if (name.length() == 1) {
        SHORT vk = VkKeyScanW(name.at(0).unicode());
        if (vk != -1) return vk & 0xFF;
    }
    return 0;
}

bool KeyboardController::sendKeyInput(int vkCode, bool up)
{
    INPUT inp = {};
    inp.type       = INPUT_KEYBOARD;
    inp.ki.wVk     = static_cast<WORD>(vkCode);
    inp.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;

    static const int extended[] = {
        VK_INSERT, VK_DELETE, VK_HOME, VK_END, VK_PRIOR, VK_NEXT,
        VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_NUMLOCK, VK_RCONTROL, VK_RMENU
    };
    for (int ext : extended) {
        if (vkCode == ext) { inp.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY; break; }
    }

    if (SendInput(1, &inp, sizeof(INPUT)) != 1) {
        emit errorOccurred(QStringLiteral("SendInput (key) failed: %1 vk=%2")
                               .arg(GetLastError()).arg(vkCode));
        return false;
    }
    return true;
}

bool KeyboardController::sendUnicodeChar(unsigned short ch)
{
    INPUT inp[2] = {};
    inp[0].type       = INPUT_KEYBOARD;
    inp[0].ki.wScan   = ch;
    inp[0].ki.dwFlags = KEYEVENTF_UNICODE;
    inp[1].type       = INPUT_KEYBOARD;
    inp[1].ki.wScan   = ch;
    inp[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    return SendInput(2, inp, sizeof(INPUT)) == 2;
}

bool KeyboardController::pressKey(const QString& keyName, KeyModifier mods)
{
    const int vk = nameToVK(keyName);
    if (vk == 0) {
        emit errorOccurred(QStringLiteral("Unknown key: %1").arg(keyName));
        return false;
    }
    return pressKey(vk, mods);
}

bool KeyboardController::pressKey(int vkCode, KeyModifier mods)
{
    if (mods & KeyModifier::Ctrl)  sendKeyInput(VK_CONTROL, false);
    if (mods & KeyModifier::Alt)   sendKeyInput(VK_MENU,    false);
    if (mods & KeyModifier::Shift) sendKeyInput(VK_SHIFT,   false);
    if (mods & KeyModifier::Win)   sendKeyInput(VK_LWIN,    false);

    msDelay(10);
    sendKeyInput(vkCode, false);
    msDelay(10);
    sendKeyInput(vkCode, true);
    msDelay(10);

    if (mods & KeyModifier::Win)   sendKeyInput(VK_LWIN,    true);
    if (mods & KeyModifier::Shift) sendKeyInput(VK_SHIFT,   true);
    if (mods & KeyModifier::Alt)   sendKeyInput(VK_MENU,    true);
    if (mods & KeyModifier::Ctrl)  sendKeyInput(VK_CONTROL, true);
    return true;
}

bool KeyboardController::keyDown(const QString& keyName)
{
    const int vk = nameToVK(keyName);
    return vk ? sendKeyInput(vk, false) : false;
}

bool KeyboardController::keyUp(const QString& keyName)
{
    const int vk = nameToVK(keyName);
    return vk ? sendKeyInput(vk, true) : false;
}

QList<int> KeyboardController::parseCombo(const QString& combo)
{
    QList<int> result;
    const QStringList parts = combo.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const auto& part : parts) {
        const int vk = nameToVK(part.trimmed());
        if (vk) result.append(vk);
    }
    return result;
}

bool KeyboardController::pressCombo(const QString& combo)
{
    const QList<int> keys = parseCombo(combo);
    if (keys.isEmpty()) return false;

    for (int i = 0; i < keys.size() - 1; ++i)
        sendKeyInput(keys[i], false);
    msDelay(10);
    sendKeyInput(keys.last(), false);
    msDelay(10);
    sendKeyInput(keys.last(), true);
    msDelay(10);
    for (int i = keys.size() - 2; i >= 0; --i)
        sendKeyInput(keys[i], true);
    msDelay(10);
    return true;
}

bool KeyboardController::typeText(const QString& text, int delayMs)
{
    for (const QChar& ch : text) {
        if (!sendUnicodeChar(ch.unicode())) return false;
        if (delayMs > 0) msDelay(delayMs);
    }
    return true;
}

bool KeyboardController::dictate(const QString& text)
{
    return typeText(text, 15); // ~естественная скорость печати
}

// ============================================================
//  WindowController
// ============================================================

WindowController::WindowController(QObject* parent) : QObject(parent) {}

namespace {
struct EnumCtx { QList<WindowInfo> list; };

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto* ctx = reinterpret_cast<EnumCtx*>(lParam);
    if (!IsWindowVisible(hwnd)) return TRUE;

    wchar_t buf[512] = {};
    GetWindowTextW(hwnd, buf, 512);
    if (wcslen(buf) == 0) return TRUE;

    WindowInfo info;
    info.hwnd        = hwnd;
    info.title       = QString::fromWCharArray(buf);
    info.isVisible   = true;
    info.isMinimized = IsIconic(hwnd) != 0;
    info.isMaximized = IsZoomed(hwnd) != 0;

    RECT r;
    if (GetWindowRect(hwnd, &r))
        info.rect = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);

    wchar_t cls[256] = {};
    GetClassNameW(hwnd, cls, 256);
    info.className = QString::fromWCharArray(cls);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    info.pid = pid;

    ctx->list.append(info);
    return TRUE;
}
} // namespace

QList<WindowInfo> WindowController::allWindows() const
{
    EnumCtx ctx;
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.list;
}

WindowInfo WindowController::findWindow(const QString& titlePart) const
{
    const QString lp = titlePart.toLower();
    for (const auto& info : allWindows()) {
        if (info.title.toLower().contains(lp))
            return info;
    }
    return {};
}

bool WindowController::focusWindow(const QString& titlePart)
{
    const WindowInfo info = findWindow(titlePart);
    if (!info.hwnd) {
        emit errorOccurred(QStringLiteral("Window not found: %1").arg(titlePart));
        return false;
    }
    return focusWindow(info.hwnd);
}

bool WindowController::focusWindow(HWND hwnd)
{
    if (!hwnd) return false;
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);

    const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const DWORD thisThread       = GetCurrentThreadId();
    if (foregroundThread != thisThread) {
        AttachThreadInput(foregroundThread, thisThread, TRUE);
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
        AttachThreadInput(foregroundThread, thisThread, FALSE);
    } else {
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
    }
    msDelay(80);
    return GetForegroundWindow() == hwnd;
}

bool WindowController::minimizeWindow(const QString& titlePart)
{
    const auto info = findWindow(titlePart);
    return info.hwnd ? ShowWindow(info.hwnd, SW_MINIMIZE) != 0 : false;
}

bool WindowController::maximizeWindow(const QString& titlePart)
{
    const auto info = findWindow(titlePart);
    if (!info.hwnd) return false;
    focusWindow(info.hwnd);
    return ShowWindow(info.hwnd, SW_MAXIMIZE) != 0;
}

bool WindowController::restoreWindow(const QString& titlePart)
{
    const auto info = findWindow(titlePart);
    return info.hwnd ? ShowWindow(info.hwnd, SW_RESTORE) != 0 : false;
}

bool WindowController::closeWindow(const QString& titlePart)
{
    const auto info = findWindow(titlePart);
    return info.hwnd ? PostMessageW(info.hwnd, WM_CLOSE, 0, 0) != 0 : false;
}

bool WindowController::moveWindow(const QString& titlePart, int x, int y)
{
    const auto info = findWindow(titlePart);
    if (!info.hwnd) return false;
    return SetWindowPos(info.hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER) != 0;
}

bool WindowController::resizeWindow(const QString& titlePart, int w, int h)
{
    const auto info = findWindow(titlePart);
    if (!info.hwnd) return false;
    return SetWindowPos(info.hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER) != 0;
}

bool WindowController::setAlwaysOnTop(const QString& titlePart, bool onTop)
{
    const auto info = findWindow(titlePart);
    if (!info.hwnd) return false;
    HWND insertAfter = onTop ? HWND_TOPMOST : HWND_NOTOPMOST;
    return SetWindowPos(info.hwnd, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE) != 0;
}

WindowInfo WindowController::fromHWND(HWND hwnd) const
{
    WindowInfo info;
    if (!hwnd) return info;

    wchar_t buf[512] = {};
    GetWindowTextW(hwnd, buf, 512);
    info.hwnd        = hwnd;
    info.title       = QString::fromWCharArray(buf);
    info.isVisible   = IsWindowVisible(hwnd) != 0;
    info.isMinimized = IsIconic(hwnd) != 0;
    info.isMaximized = IsZoomed(hwnd) != 0;

    RECT r;
    if (GetWindowRect(hwnd, &r))
        info.rect = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    info.pid = pid;
    return info;
}

WindowInfo WindowController::activeWindow() const
{
    return fromHWND(GetForegroundWindow());
}

bool WindowController::minimizeActive()
{
    HWND h = GetForegroundWindow();
    return h ? ShowWindow(h, SW_MINIMIZE) != 0 : false;
}

bool WindowController::maximizeActive()
{
    HWND h = GetForegroundWindow();
    return h ? ShowWindow(h, SW_MAXIMIZE) != 0 : false;
}

bool WindowController::closeActive()
{
    HWND h = GetForegroundWindow();
    return h ? PostMessageW(h, WM_CLOSE, 0, 0) != 0 : false;
}

bool WindowController::alwaysOnTopActive(bool onTop)
{
    HWND h = GetForegroundWindow();
    if (!h) return false;
    HWND insertAfter = onTop ? HWND_TOPMOST : HWND_NOTOPMOST;
    return SetWindowPos(h, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE) != 0;
}

bool WindowController::switchToNext()
{
    INPUT inp[4] = {};
    inp[0].type = INPUT_KEYBOARD; inp[0].ki.wVk = VK_MENU;
    inp[1].type = INPUT_KEYBOARD; inp[1].ki.wVk = VK_TAB;
    inp[2].type = INPUT_KEYBOARD; inp[2].ki.wVk = VK_TAB;  inp[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inp[3].type = INPUT_KEYBOARD; inp[3].ki.wVk = VK_MENU; inp[3].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(4, inp, sizeof(INPUT)) == 4;
}

// ============================================================
//  ClipboardController
// ============================================================

ClipboardController::ClipboardController(QObject* parent) : QObject(parent) {}

QString ClipboardController::getText() const { return QApplication::clipboard()->text(); }

bool ClipboardController::setText(const QString& text)
{
    QApplication::clipboard()->setText(text);
    return true;
}

bool ClipboardController::clear()
{
    QApplication::clipboard()->clear();
    return true;
}

// ============================================================
//  SystemController
// ============================================================

SystemController::SystemController(QObject* parent) : QObject(parent) {}

bool SystemController::launchApp(const QString& nameOrPath, const QString& args)
{
    const std::wstring wpath = nameOrPath.toStdWString();
    const std::wstring wargs = args.toStdWString();
    HINSTANCE res = ShellExecuteW(nullptr, L"open",
                                   wpath.c_str(),
                                   wargs.empty() ? nullptr : wargs.c_str(),
                                   nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(res) <= 32) {
        emit errorOccurred(QStringLiteral("ShellExecute failed: %1").arg(GetLastError()));
        return false;
    }
    return true;
}

bool SystemController::launchUrl(const QString& url)
{
    const std::wstring wu = url.toStdWString();
    return reinterpret_cast<INT_PTR>(
               ShellExecuteW(nullptr, L"open", wu.c_str(), nullptr, nullptr, SW_SHOWNORMAL)
           ) > 32;
}

bool SystemController::openPath(const QString& path) { return launchApp(path); }

bool SystemController::lockWorkstation() { return LockWorkStation() != 0; }

bool SystemController::sleepNow() { return SetSuspendState(FALSE, FALSE, FALSE) != 0; }

bool SystemController::shutdownIn(int delaySeconds)
{
    const QString cmd = QStringLiteral("shutdown /s /t %1").arg(delaySeconds);
    return _wsystem(cmd.toStdWString().c_str()) == 0;
}

bool SystemController::restartIn(int delaySeconds)
{
    const QString cmd = QStringLiteral("shutdown /r /t %1").arg(delaySeconds);
    return _wsystem(cmd.toStdWString().c_str()) == 0;
}

bool SystemController::cancelShutdown()
{
    return _wsystem(L"shutdown /a") == 0;
}

bool SystemController::openTaskManager() { return launchApp(QStringLiteral("taskmgr.exe")); }

bool SystemController::setVolume(int percent)
{
    AudioEndpoint ep;
    if (!ep.isValid()) {
        emit errorOccurred(QStringLiteral("Audio endpoint unavailable"));
        return false;
    }
    const float level = qBound(0, percent, 100) / 100.0f;
    ep->SetMasterVolumeLevelScalar(level, nullptr);
    emit volumeChanged(percent);
    return true;
}

bool SystemController::increaseVolume(int step) { return setVolume(qMin(100, currentVolume() + step)); }
bool SystemController::decreaseVolume(int step) { return setVolume(qMax(0,   currentVolume() - step)); }

bool SystemController::muteAudio(bool muted)
{
    AudioEndpoint ep;
    if (!ep.isValid()) return false;
    ep->SetMute(muted ? TRUE : FALSE, nullptr);
    return true;
}

bool SystemController::toggleMute()
{
    AudioEndpoint ep;
    if (!ep.isValid()) return false;
    BOOL isMuted = FALSE;
    ep->GetMute(&isMuted);
    ep->SetMute(!isMuted, nullptr);
    return true;
}

int SystemController::currentVolume() const
{
    AudioEndpoint ep;
    if (!ep.isValid()) return 0;
    float level = 0.0f;
    ep->GetMasterVolumeLevelScalar(&level);
    return static_cast<int>(level * 100.0f);
}

QStringList SystemController::findFiles(const QString& pattern,
                                         const QString& root,
                                         int maxResults) const
{
    QStringList found;
    QDirIterator it(root, QStringList{pattern},
                    QDir::Files | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext() && found.size() < maxResults)
        found << it.next();
    return found;
}

// ============================================================
//  SystemController — safe file organization primitives
// ============================================================

QStringList SystemController::allowedOrganizeRoots()
{
    return {
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
    };
}

bool SystemController::isPathAllowedForOrganize(const QString& path)
{
    if (path.trimmed().isEmpty()) return false;

    // Resolve to an absolute path without requiring the target to exist
    // yet (createFolder/moveFile destinations may not exist), but still
    // normalize "." / ".." segments.
    const QString abs = QDir::cleanPath(QFileInfo(path).absoluteFilePath());

    // Hard-blocked regardless of allow-list — belt and suspenders even
    // though these would never match an allowed root below.
    static const QStringList blocked = {
        QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)),
        QStringLiteral("C:/Windows"), QStringLiteral("C:/Program Files"),
        QStringLiteral("C:/Program Files (x86)"), QStringLiteral("C:/ProgramData"),
    };
    for (const QString& b : blocked) {
        if (!b.isEmpty() && (abs == b || abs.startsWith(b + QLatin1Char('/'), Qt::CaseInsensitive)))
            return false;
    }

    for (const QString& root : allowedOrganizeRoots()) {
        if (root.isEmpty()) continue;
        const QString cleanRoot = QDir::cleanPath(root);
        if (abs == cleanRoot || abs.startsWith(cleanRoot + QLatin1Char('/'), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

bool SystemController::createFolder(const QString& path)
{
    if (!isPathAllowedForOrganize(path)) {
        emit errorOccurred(QStringLiteral("createFolder: path not in allowed roots: ") + path);
        return false;
    }
    return QDir().mkpath(path);
}

bool SystemController::moveFile(const QString& sourcePath, const QString& destPath)
{
    if (!isPathAllowedForOrganize(sourcePath) || !isPathAllowedForOrganize(destPath)) {
        emit errorOccurred(QStringLiteral("moveFile: path not in allowed roots"));
        return false;
    }
    QFileInfo src(sourcePath);
    if (!src.exists() || !src.isFile()) {
        emit errorOccurred(QStringLiteral("moveFile: source does not exist: ") + sourcePath);
        return false;
    }
    if (QFile::exists(destPath)) {
        emit errorOccurred(QStringLiteral("moveFile: destination already exists: ") + destPath);
        return false;
    }
    QDir().mkpath(QFileInfo(destPath).absolutePath());

    if (QFile::rename(sourcePath, destPath))
        return true;

    // Cross-volume fallback: copy then remove the original.
    if (QFile::copy(sourcePath, destPath))
        return QFile::remove(sourcePath);

    emit errorOccurred(QStringLiteral("moveFile: failed to move ") + sourcePath);
    return false;
}

bool SystemController::copyFile(const QString& sourcePath, const QString& destPath)
{
    if (!isPathAllowedForOrganize(sourcePath) || !isPathAllowedForOrganize(destPath)) {
        emit errorOccurred(QStringLiteral("copyFile: path not in allowed roots"));
        return false;
    }
    QFileInfo src(sourcePath);
    if (!src.exists() || !src.isFile()) {
        emit errorOccurred(QStringLiteral("copyFile: source does not exist: ") + sourcePath);
        return false;
    }
    if (QFile::exists(destPath)) {
        emit errorOccurred(QStringLiteral("copyFile: destination already exists: ") + destPath);
        return false;
    }
    QDir().mkpath(QFileInfo(destPath).absolutePath());
    return QFile::copy(sourcePath, destPath);
}

bool SystemController::renameFile(const QString& sourcePath, const QString& newName)
{
    if (newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\'))) {
        emit errorOccurred(QStringLiteral("renameFile: newName must not contain a path"));
        return false;
    }
    const QString destPath = QFileInfo(sourcePath).absoluteDir().filePath(newName);
    return moveFile(sourcePath, destPath);
}

// ============================================================
//  PcController — фасад
// ============================================================

PcController::PcController(QObject* parent) : QObject(parent)
{
    m_mouse    = new MouseController(this);
    m_keyboard = new KeyboardController(this);
    m_windows  = new WindowController(this);
    m_clip     = new ClipboardController(this);
    m_system   = new SystemController(this);

    auto fwd = [this](const QString& msg){ emit errorOccurred(msg); };
    connect(m_mouse,    &MouseController::errorOccurred,    this, fwd);
    connect(m_keyboard, &KeyboardController::errorOccurred, this, fwd);
    connect(m_windows,  &WindowController::errorOccurred,   this, fwd);
    connect(m_clip,     &ClipboardController::errorOccurred,this, fwd);
    connect(m_system,   &SystemController::errorOccurred,   this, fwd);
}
