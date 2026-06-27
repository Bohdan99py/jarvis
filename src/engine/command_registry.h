#pragma once
// -------------------------------------------------------
// command_registry.h — Реестр СИСТЕМНЫХ команд J.A.R.V.I.S.
//
// После появления Brain этот класс отвечает ТОЛЬКО за
// аварийные системные команды которые должны срабатывать
// независимо от намерения:
//   - apikey / geminikey — смена ключей
//   - помощь / help — список команд
//   - очистить / clear — очистка лога
//   - выход / exit / quit — завершение
//
// Всё остальное (поиск, открытие, вопросы) теперь
// обрабатывается через Brain → Intent → Router.
//
// hasClaudeIntent() оставлен для совместимости с Jarvis,
// но его роль теперь вспомогательная.
// -------------------------------------------------------

#include <QString>
#include <QVector>
#include <QStringList>
#include <functional>

struct Command
{
    QStringList keywords;
    std::function<QString(const QString& fullInput)> handler;
    QString     description;
    bool        prefixMatch = false; // true = начинается с kw, false = содержит kw
};

class CommandRegistry
{
public:
    CommandRegistry() = default;

    void registerCommand(const QStringList& keywords,
                         std::function<QString(const QString&)> handler,
                         const QString& description,
                         bool prefixMatch = false);

    struct Result {
        bool    matched  = false;
        QString response;
    };

    Result tryExecute(const QString& input) const;
    QString helpText() const;

    // Оставлен для Jarvis::processCommand — определяет
    // нужно ли вообще проверять реестр или сразу в API
    static bool isSystemCommand(const QString& lowerInput);

private:
    QVector<Command> m_commands;
};
