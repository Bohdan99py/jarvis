#pragma once
// -------------------------------------------------------
// command_registry.h — Реестр команд J.A.R.V.I.S.
// -------------------------------------------------------

#include <QString>
#include <QVector>
#include <QStringList>
#include <functional>

struct Command
{
    QStringList keywords;
    std::function<QString(const QString& fullInput)> handler;
    QString description;
    bool prefixMatch = false; // true = keyword — это префикс (напр. "запусти <arg>")
};

class CommandRegistry
{
public:
    CommandRegistry() = default;

    void registerCommand(const QStringList& keywords,
                         std::function<QString(const QString&)> handler,
                         const QString& description,
                         bool prefixMatch = false);

    // Попытка найти и выполнить команду. Возвращает {true, result} или {false, ""}
    struct Result {
        bool matched = false;
        QString response;
    };

    Result tryExecute(const QString& input) const;

    // Список команд для помощи
    QString helpText() const;

private:
    QVector<Command> m_commands;
};