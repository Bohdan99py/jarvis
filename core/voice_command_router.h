#pragma once

// ============================================================
// voice_command_router.h  —  J.A.R.V.I.S.
// Маршрутизатор голосовых команд → действия ПК
// C++17 / Qt6 / MSVC 2022
// ============================================================

#include <QObject>
#include <QString>
#include <QMap>
#include <QList>
#include <functional>

class PcController;

// ============================================================
//  Результат выполнения команды
// ============================================================

struct CommandResult {
    bool    success  = false;
    QString feedback;        // Что сказать пользователю голосом

    static CommandResult ok(const QString& msg = {})
    { return {true, msg}; }

    static CommandResult fail(const QString& msg)
    { return {false, msg}; }
};

// ============================================================
//  Описание одной команды
// ============================================================

using CommandHandler = std::function<CommandResult(const QStringList& tokens)>;

struct CommandEntry {
    QStringList  triggers;    // Ключевые слова (любое из них активирует)
    CommandHandler handler;
    QString      description; // Для справки / помощи
    bool         isEN = false; // false = RU, true = EN
};

// ============================================================
//  VoiceCommandRouter
// ============================================================

class VoiceCommandRouter : public QObject
{
    Q_OBJECT
public:
    explicit VoiceCommandRouter(PcController* pc, QObject* parent = nullptr);

    // Главная точка входа — вызывается из VoiceEngine с распознанным текстом
    CommandResult route(const QString& spokenText);

    // Список всех команд (для справки)
    QStringList helpList() const;

    // Добавить кастомную команду в рантайме
    void addCommand(const CommandEntry& entry);

    // Включить/выключить режим диктовки
    // В режиме диктовки весь текст идёт как typeText, минуя маршрутизатор
    void setDictationMode(bool on);
    bool isDictationMode() const { return m_dictationMode; }

signals:
    void commandMatched(const QString& trigger, bool success);
    void feedbackReady(const QString& text);  // Подаётся в TTS
    void unhandled(const QString& text);      // Передать в AI

private:
    void registerAll();

    // Блоки регистрации команд
    void registerMouseCommands();
    void registerKeyboardCommands();
    void registerWindowCommands();
    void registerSystemCommands();
    void registerMediaCommands();
    void registerTextCommands();
    void registerBrowserCommands();
    void registerFileCommands();
    void registerMacroCommands();

    // Утилиты парсинга
    static int    extractNumber(const QStringList& tokens, int defaultVal = 1);
    static QString extractQuoted(const QString& text);
    static QString tokensAfter(const QStringList& tokens, int idx);

    // Матчинг: возвращает индекс совпавшей команды или -1
    int matchCommand(const QString& normalized) const;

    PcController* m_pc = nullptr;
    QList<CommandEntry> m_commands;
    bool m_dictationMode = false;

    // Курсорная сетка экрана для команд "левый край", "центр" и т.д.
    struct ScreenGrid {
        int x, y;
        QStringList names;
    };
    QList<ScreenGrid> buildScreenGrid() const;
};
