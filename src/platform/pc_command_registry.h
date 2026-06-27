#pragma once
// -------------------------------------------------------
// pc_command_registry.h — Голосовые команды управления ПК
// J.A.R.V.I.S. Full PC Voice Control
//
// ВАЖНО: клавиатура (печать текста/нажатие клавиш/комбо) у тебя
// УЖЕ реализована через KeyEmulator + cmdTypeText/cmdPressKey/cmdCombo
// в jarvis.cpp ("напечатай "/"нажми "/"комбо "). Этот модуль их
// НЕ дублирует и НЕ трогает — добавляет только то, чего не было:
//
//   • Мышь        — клик/двойной клик/правый клик/скролл/позиционирование
//   • Окна        — переключение по имени, свернуть/развернуть/закрыть/
//                   поверх всех, фокус приложений
//   • Система     — запуск приложений, блокировка, сон, выключение,
//                   громкость (WASAPI), диспетчер задач
//   • Браузер     — назад/вперёд/обновить/адресная строка/открыть сайт
//   • Файлы       — поиск файла по имени
//   • Макросы     — запись/воспроизведение голосом (мышь+клавиатура),
//                   готовые сценарии (утро/ночь/режим разработки)
//
// НЕ заменяет CommandRegistry и не лезет в его структуру.
// Регистрирует команды через штатный
//     registry.registerCommand(keywords, handler, description, prefixMatch)
//
// Использование в Jarvis (поле, не указатель — как остальные
// контроллеры в jarvis.h):
//
//     // jarvis.h:
//     #include "pc_command_registry.h"
//     PcCommandRegistry* m_pcCommands = nullptr;
//
//     // jarvis.cpp, конструктор, после m_keyEmulator = new KeyEmulator(this):
//     m_pcCommands = new PcCommandRegistry(m_keyEmulator, this);
//     m_pcCommands->registerInto(m_registry);   // m_registry — твой объект CommandRegistry
// -------------------------------------------------------

#include <QObject>
#include <QString>

class CommandRegistry;
class PcController;
class MacroRecorder;
class KeyEmulator;

class PcCommandRegistry : public QObject
{
    Q_OBJECT
public:
    // keyEmulator может быть nullptr — тогда печать текста в макросах
    // (TypeText-события) будет недоступна, но мышь/окна/система всё
    // равно зарегистрируются и будут работать.
    explicit PcCommandRegistry(KeyEmulator* keyEmulator, QObject* parent = nullptr);
    ~PcCommandRegistry() override;

    // Регистрирует все PC-control команды в переданный реестр.
    // Вызывать один раз при старте, после конструирования registry.
    void registerInto(CommandRegistry& registry);

    // Доступ к контроллеру — пригодится для голосового вайбкодинга,
    // ScreenAgent и т.п. (см. screen_agent.h/.cpp)
    PcController*  controller() const { return m_pc; }
    MacroRecorder* macros()     const { return m_macros; }

signals:
    // Голосовая обратная связь для TTS — подключи к своему
    // Jarvis::speakAsync через connect снаружи.
    void feedbackReady(const QString& text);

private:
    void registerMouseCommands(CommandRegistry& r);
    void registerWindowCommands(CommandRegistry& r);
    void registerSystemCommands(CommandRegistry& r);
    void registerMediaCommands(CommandRegistry& r);
    void registerBrowserCommands(CommandRegistry& r);
    void registerFileCommands(CommandRegistry& r);
    void registerMacroVoiceCommands(CommandRegistry& r);

    PcController*  m_pc          = nullptr;
    MacroRecorder* m_macros      = nullptr;
    KeyEmulator*   m_keyEmulator = nullptr; // не владеем, используем для макросов
};
