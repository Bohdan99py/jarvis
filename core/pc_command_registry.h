#pragma once
// -------------------------------------------------------
// pc_command_registry.h — Голосовые команды управления ПК
// J.A.R.V.I.S. Full PC Voice Control
//
// НЕ заменяет CommandRegistry и не лезет в его структуру.
// Это фабрика: строит готовый PcController + регистрирует
// ~80 команд (мышь/клавиатура/окна/система/медиа/браузер/
// макросы) в ТВОЙ существующий CommandRegistry через его
// штатный registerCommand(keywords, handler, description, prefixMatch).
//
// Использование в Jarvis (или где у тебя CommandRegistry создаётся):
//
//     m_registry = new CommandRegistry();
//     m_pcCommands = new PcCommandRegistry(this);
//     m_pcCommands->registerInto(*m_registry);
//     ... регистрируй остальные свои команды как обычно ...
//
// PcCommandRegistry владеет PcController и MacroRecorder —
// живёт столько же, сколько Jarvis/MainWindow.
// -------------------------------------------------------

#include <QObject>
#include <QString>

class CommandRegistry;
class PcController;
class MacroRecorder;

class PcCommandRegistry : public QObject
{
    Q_OBJECT
public:
    explicit PcCommandRegistry(QObject* parent = nullptr);
    ~PcCommandRegistry() override;

    // Регистрирует все PC-control команды в переданный реестр.
    // Вызывать один раз при старте, после конструирования registry.
    void registerInto(CommandRegistry& registry);

    // Доступ к контроллеру — пригодится для голосового вайбкодинга,
    // ScreenAgent и т.п. (см. screen_agent.h/.cpp)
    PcController* controller() const { return m_pc; }
    MacroRecorder* macros()    const { return m_macros; }

    // Режим диктовки — когда включён, текст не парсится как команда,
    // а печатается как есть в активное окно. Переключается командой
    // "начать диктовку" / "остановить диктовку", но снаружи (например
    // из Jarvis::processCommand) тоже можно проверить/выставить.
    bool isDictationMode() const { return m_dictationMode; }
    void setDictationMode(bool on);

signals:
    // Голосовая обратная связь для TTS — подключи к своему
    // voice_input / TTS слою.
    void feedbackReady(const QString& text);

private:
    void registerMouseCommands(CommandRegistry& r);
    void registerKeyboardCommands(CommandRegistry& r);
    void registerWindowCommands(CommandRegistry& r);
    void registerSystemCommands(CommandRegistry& r);
    void registerMediaCommands(CommandRegistry& r);
    void registerTextCommands(CommandRegistry& r);
    void registerBrowserCommands(CommandRegistry& r);
    void registerFileCommands(CommandRegistry& r);
    void registerMacroVoiceCommands(CommandRegistry& r);

    PcController*  m_pc     = nullptr;
    MacroRecorder* m_macros = nullptr;
    bool m_dictationMode = false;
};
