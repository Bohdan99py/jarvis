// -------------------------------------------------------
// command_registry.cpp — Реестр команд J.A.R.V.I.S.
// -------------------------------------------------------

#include "command_registry.h"

void CommandRegistry::registerCommand(const QStringList& keywords,
                                      std::function<QString(const QString&)> handler,
                                      const QString& description,
                                      bool prefixMatch)
{
    m_commands.append(Command{keywords, std::move(handler), description, prefixMatch});
}

CommandRegistry::Result CommandRegistry::tryExecute(const QString& input) const
{
    const QString lower = input.trimmed().toLower();

    for (const auto& cmd : m_commands) {
        for (const auto& kw : cmd.keywords) {
            if (cmd.prefixMatch) {
                if (lower.startsWith(kw)) {
                    return {true, cmd.handler(input)};
                }
            } else {
                if (lower.contains(kw)) {
                    return {true, cmd.handler(input)};
                }
            }
        }
    }

    return {false, QString()};
}

QString CommandRegistry::helpText() const
{
    QString text = QStringLiteral("Доступные команды:\n");
    for (const auto& cmd : m_commands) {
        if (!cmd.description.isEmpty()) {
            text += QStringLiteral("• ") + cmd.description + QStringLiteral("\n");
        }
    }
    return text.trimmed();
}