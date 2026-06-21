// -------------------------------------------------------
// macro_recorder.cpp — Запись и воспроизведение макросов
// J.A.R.V.I.S. Full PC Voice Control
// -------------------------------------------------------

#include "macro_recorder.h"
#include "pc_controller.h"

#include <QThread>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>

MacroRecorder* MacroRecorder::s_instance = nullptr;

// ============================================================
//  MacroEvent — сериализация
// ============================================================

QJsonObject MacroEvent::toJson() const
{
    QJsonObject obj;
    obj["type"]    = static_cast<int>(type);
    obj["delayMs"] = delayMs;
    obj["x"]       = x;
    obj["y"]       = y;
    obj["button"]  = button;
    obj["scroll"]  = scrollDelta;
    obj["vk"]      = vkCode;
    obj["text"]    = text;
    return obj;
}

MacroEvent MacroEvent::fromJson(const QJsonObject& obj)
{
    MacroEvent ev;
    ev.type        = static_cast<MacroEventType>(obj["type"].toInt());
    ev.delayMs     = obj["delayMs"].toInteger();
    ev.x           = obj["x"].toInt();
    ev.y           = obj["y"].toInt();
    ev.button      = obj["button"].toInt();
    ev.scrollDelta = obj["scroll"].toInt();
    ev.vkCode      = obj["vk"].toInt();
    ev.text        = obj["text"].toString();
    return ev;
}

// ============================================================
//  Macro — сериализация
// ============================================================

QJsonObject Macro::toJson() const
{
    QJsonObject obj;
    obj["name"]      = name;
    obj["loopCount"] = loopCount;

    QJsonArray tr;
    for (const auto& t : voiceTriggers) tr.append(t);
    obj["triggers"] = tr;

    QJsonArray ev;
    for (const auto& e : events) ev.append(e.toJson());
    obj["events"] = ev;
    return obj;
}

Macro Macro::fromJson(const QJsonObject& obj)
{
    Macro m;
    m.name      = obj["name"].toString();
    m.loopCount = obj["loopCount"].toInt(1);

    for (const auto& v : obj["triggers"].toArray())
        m.voiceTriggers.append(v.toString());
    for (const auto& v : obj["events"].toArray())
        m.events.append(MacroEvent::fromJson(v.toObject()));
    return m;
}

bool Macro::save(const QString& path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return true;
}

Macro Macro::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return fromJson(QJsonDocument::fromJson(f.readAll()).object());
}

// ============================================================
//  MacroRecorder
// ============================================================

MacroRecorder::MacroRecorder(PcController* pc, QObject* parent)
    : QObject(parent), m_pc(pc)
{
    s_instance = this;
    m_macroDir = QDir::homePath() + QStringLiteral("/.jarvis/macros");
    QDir().mkpath(m_macroDir);
    loadAll(m_macroDir);
}

MacroRecorder::~MacroRecorder()
{
    removeHooks();
    if (s_instance == this) s_instance = nullptr;
}

// ============================================================
//  Запись
// ============================================================

bool MacroRecorder::installHooks()
{
    HMODULE hMod = GetModuleHandleW(nullptr);
    m_mouseHook    = SetWindowsHookExW(WH_MOUSE_LL,    mouseProc,    hMod, 0);
    m_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, hMod, 0);
    return m_mouseHook && m_keyboardHook;
}

void MacroRecorder::removeHooks()
{
    if (m_mouseHook)    { UnhookWindowsHookEx(m_mouseHook);    m_mouseHook    = nullptr; }
    if (m_keyboardHook) { UnhookWindowsHookEx(m_keyboardHook); m_keyboardHook = nullptr; }
}

void MacroRecorder::startRecording(const QString& macroName)
{
    if (m_recording) return;
    m_current      = Macro{};
    m_current.name = macroName;
    m_recording    = true;
    m_lastEventMs  = 0;
    m_timer.restart();

    if (!installHooks()) {
        emit errorOccurred(QStringLiteral("Не удалось установить хуки ввода"));
        m_recording = false;
        return;
    }
    emit recordingStarted(macroName);
}

void MacroRecorder::stopRecording()
{
    if (!m_recording) return;
    m_recording = false;
    removeHooks();
    saveMacro(m_current);
    emit recordingStopped(m_current);
}

void MacroRecorder::addMouseEvent(MacroEventType type, int x, int y, int btn, int scroll)
{
    if (!m_recording) return;
    const qint64 now   = m_timer.elapsed();
    const qint64 delay = now - m_lastEventMs;
    m_lastEventMs = now;

    MacroEvent ev;
    ev.type        = type;
    ev.delayMs     = delay;
    ev.x           = x;
    ev.y           = y;
    ev.button      = btn;
    ev.scrollDelta = scroll;
    m_current.events.append(ev);
}

void MacroRecorder::addKeyEvent(MacroEventType type, int vk)
{
    if (!m_recording) return;
    const qint64 now   = m_timer.elapsed();
    const qint64 delay = now - m_lastEventMs;
    m_lastEventMs = now;

    MacroEvent ev;
    ev.type    = type;
    ev.delayMs = delay;
    ev.vkCode  = vk;
    m_current.events.append(ev);
}

// ============================================================
//  Win32 Hooks (статические колбеки)
// ============================================================

LRESULT CALLBACK MacroRecorder::mouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && s_instance && s_instance->m_recording) {
        auto* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        switch (wParam) {
        case WM_MOUSEMOVE:
            s_instance->addMouseEvent(MacroEventType::MouseMove, ms->pt.x, ms->pt.y, 0, 0);
            break;
        case WM_LBUTTONDOWN:
            s_instance->addMouseEvent(MacroEventType::MouseClick, ms->pt.x, ms->pt.y, 0, 0);
            break;
        case WM_RBUTTONDOWN:
            s_instance->addMouseEvent(MacroEventType::MouseClick, ms->pt.x, ms->pt.y, 1, 0);
            break;
        case WM_MBUTTONDOWN:
            s_instance->addMouseEvent(MacroEventType::MouseClick, ms->pt.x, ms->pt.y, 2, 0);
            break;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(ms->mouseData);
            s_instance->addMouseEvent(MacroEventType::MouseScroll, ms->pt.x, ms->pt.y, 0, delta);
            break;
        }
        default: break;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK MacroRecorder::keyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && s_instance && s_instance->m_recording) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
            s_instance->addKeyEvent(MacroEventType::KeyPress, static_cast<int>(kb->vkCode));
        else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
            s_instance->addKeyEvent(MacroEventType::KeyRelease, static_cast<int>(kb->vkCode));
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ============================================================
//  Воспроизведение
// ============================================================

bool MacroRecorder::play(const QString& name, int repeatCount)
{
    const auto it = m_macros.find(name);
    if (it == m_macros.end()) {
        emit errorOccurred(QStringLiteral("Макрос не найден: ") + name);
        return false;
    }
    return play(it.value(), repeatCount);
}

bool MacroRecorder::play(const Macro& macro, int repeatCount)
{
    if (m_playing) return false;
    m_playing = true;
    emit playbackStarted(macro.name);

    MouseController*    mouse = m_pc->mouse();
    KeyboardController*  kb    = m_pc->keyboard();
    const int total = macro.events.size();

    const int loops = (repeatCount > 0) ? repeatCount : macro.loopCount;

    for (int rep = 0; rep < loops && m_playing; ++rep) {
        for (int i = 0; i < total && m_playing; ++i) {
            const auto& ev = macro.events[i];

            const qint64 delay = static_cast<qint64>(ev.delayMs / m_speed);
            if (delay > 0 && delay < 5000)
                QThread::msleep(static_cast<unsigned long>(delay));

            switch (ev.type) {
            case MacroEventType::MouseMove:
                mouse->moveTo(ev.x, ev.y);
                break;
            case MacroEventType::MouseClick: {
                mouse->moveTo(ev.x, ev.y);
                const MouseButton btn = (ev.button == 1) ? MouseButton::Right  :
                                        (ev.button == 2) ? MouseButton::Middle :
                                                            MouseButton::Left;
                mouse->click(btn);
                break;
            }
            case MacroEventType::MouseScroll:
                mouse->moveTo(ev.x, ev.y);
                mouse->scroll(ev.scrollDelta > 0 ? ScrollDirection::Up : ScrollDirection::Down,
                              qMax(1, qAbs(ev.scrollDelta) / WHEEL_DELTA));
                break;
            case MacroEventType::KeyPress: {
                INPUT inp = {};
                inp.type   = INPUT_KEYBOARD;
                inp.ki.wVk = static_cast<WORD>(ev.vkCode);
                SendInput(1, &inp, sizeof(INPUT));
                break;
            }
            case MacroEventType::KeyRelease: {
                INPUT inp = {};
                inp.type       = INPUT_KEYBOARD;
                inp.ki.wVk     = static_cast<WORD>(ev.vkCode);
                inp.ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(1, &inp, sizeof(INPUT));
                break;
            }
            case MacroEventType::TypeText:
                kb->typeText(ev.text);
                break;
            }

            emit playbackProgress(i + 1, total);
        }
    }

    m_playing = false;
    emit playbackStopped(macro.name);
    return true;
}

void MacroRecorder::stopPlayback()
{
    m_playing = false;
}

// ============================================================
//  Хранилище
// ============================================================

void MacroRecorder::saveMacro(const Macro& macro)
{
    m_macros[macro.name] = macro;
    const QString path = m_macroDir + QStringLiteral("/") + macro.name + QStringLiteral(".json");
    if (!macro.save(path))
        emit errorOccurred(QStringLiteral("Не удалось сохранить макрос: ") + path);
    else
        emit feedbackReady(QStringLiteral("Макрос «%1» сохранён, %2 событий")
                               .arg(macro.name).arg(macro.events.size()));
}

bool MacroRecorder::loadMacro(const QString& name)
{
    const QString path = m_macroDir + QStringLiteral("/") + name + QStringLiteral(".json");
    const Macro m = Macro::load(path);
    if (m.name.isEmpty()) return false;
    m_macros[m.name] = m;
    return true;
}

void MacroRecorder::loadAll(const QString& dir)
{
    const QDir d(dir);
    for (const auto& entry : d.entryList({"*.json"}, QDir::Files)) {
        const Macro m = Macro::load(dir + QStringLiteral("/") + entry);
        if (!m.name.isEmpty())
            m_macros[m.name] = m;
    }
}

QStringList MacroRecorder::availableMacros() const
{
    return m_macros.keys();
}

Macro* MacroRecorder::getMacro(const QString& name)
{
    const auto it = m_macros.find(name);
    return it != m_macros.end() ? &it.value() : nullptr;
}

bool MacroRecorder::routeVoiceTrigger(const QString& spokenText)
{
    const QString norm = spokenText.toLower().trimmed();
    for (const auto& macro : m_macros) {
        for (const auto& trigger : macro.voiceTriggers) {
            if (norm.contains(trigger.toLower())) {
                const Macro copy = macro;
                QThread* thread = QThread::create([this, copy]() { play(copy, copy.loopCount); });
                connect(thread, &QThread::finished, thread, &QThread::deleteLater);
                thread->start();
                return true;
            }
        }
    }
    return false;
}
