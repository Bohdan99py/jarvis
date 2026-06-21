#pragma once
// -------------------------------------------------------
// macro_recorder.h — Запись и воспроизведение макросов
// J.A.R.V.I.S. Full PC Voice Control
//
// Запись через глобальные Win32-хуки (WH_MOUSE_LL / WH_KEYBOARD_LL).
// Хранение — JSON в %USERPROFILE%/.jarvis/macros/.
// Используется из PcCommandRegistry ("запиши макрос ...").
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QElapsedTimer>
#include <QJsonObject>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

class PcController;

// ============================================================
//  Событие макроса
// ============================================================

enum class MacroEventType {
    MouseMove,
    MouseClick,
    MouseScroll,
    KeyPress,
    KeyRelease,
    TypeText,
};

struct MacroEvent {
    MacroEventType type = MacroEventType::MouseMove;
    qint64  delayMs     = 0;   // Пауза перед событием относительно предыдущего

    int x = 0, y = 0;          // Mouse
    int button = 0;             // 0=left,1=right,2=middle
    int scrollDelta = 0;

    int vkCode = 0;              // Keyboard
    QString text;                 // TypeText

    QJsonObject toJson() const;
    static MacroEvent fromJson(const QJsonObject& obj);
};

// ============================================================
//  Макрос
// ============================================================

struct Macro {
    QString            name;
    QList<MacroEvent>  events;
    QStringList        voiceTriggers;
    int                loopCount = 1;

    QJsonObject toJson() const;
    static Macro fromJson(const QJsonObject& obj);
    bool save(const QString& path) const;
    static Macro load(const QString& path);
};

// ============================================================
//  MacroRecorder
// ============================================================

class MacroRecorder : public QObject
{
    Q_OBJECT
public:
    explicit MacroRecorder(PcController* pc, QObject* parent = nullptr);
    ~MacroRecorder() override;

    void startRecording(const QString& macroName);
    void stopRecording();
    bool isRecording() const { return m_recording; }

    bool play(const QString& name, int repeatCount = 1);
    bool play(const Macro& macro, int repeatCount = 1);
    void stopPlayback();
    bool isPlaying() const { return m_playing; }

    void saveMacro(const Macro& macro);
    bool loadMacro(const QString& name);
    void loadAll(const QString& dir);
    QStringList availableMacros() const;
    Macro* getMacro(const QString& name);

    bool routeVoiceTrigger(const QString& spokenText);

    void setPlaybackSpeed(double speed) { m_speed = speed; }

signals:
    void recordingStarted(const QString& name);
    void recordingStopped(const Macro& macro);
    void playbackStarted(const QString& name);
    void playbackStopped(const QString& name);
    void playbackProgress(int current, int total);
    void feedbackReady(const QString& text);
    void errorOccurred(const QString& msg);

private:
    bool installHooks();
    void removeHooks();

    static LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK keyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

    void addMouseEvent(MacroEventType type, int x, int y, int btn, int scroll);
    void addKeyEvent(MacroEventType type, int vk);

    PcController* m_pc        = nullptr;
    bool          m_recording = false;
    bool          m_playing   = false;
    double        m_speed     = 1.0;
    Macro         m_current;
    QElapsedTimer m_timer;
    qint64        m_lastEventMs = 0;

    QMap<QString, Macro> m_macros;
    QString m_macroDir;

    static MacroRecorder* s_instance;
    HHOOK m_mouseHook    = nullptr;
    HHOOK m_keyboardHook = nullptr;
};
