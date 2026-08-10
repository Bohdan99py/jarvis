// -------------------------------------------------------
// command_registry.cpp — Реестр СИСТЕМНЫХ команд J.A.R.V.I.S.
// -------------------------------------------------------

#include "command_registry.h"

// ============================================================
// isSystemCommand — быстрая проверка:
// является ли ввод одной из аварийных системных команд?
//
// Используется в Jarvis::processCommand чтобы решить —
// проверять реестр или сразу передавать в Brain/API.
//
// Список намеренно минимальный: только то что должно
// срабатывать независимо от любого контекста.
// ============================================================

bool CommandRegistry::isSystemCommand(const QString& lower)
{
    static const QStringList systemKeywords = {
        // Ключи API
        QStringLiteral("apikey"),
        QStringLiteral("ollamakey"),
        // Помощь
        QStringLiteral("помощь"),
        QStringLiteral("help"),
        QStringLiteral("команды"),
        QStringLiteral("commands"),
        // Очистка / выход
        QStringLiteral("очистить лог"),
        QStringLiteral("clear log"),
        QStringLiteral("выход"),
        QStringLiteral("exit"),
        QStringLiteral("quit"),
    };

    for (const auto& kw : systemKeywords) {
        if (lower == kw || lower.startsWith(kw + QChar(' '))) {
            return true;
        }
    }
    return false;
}

// ============================================================
// registerCommand
// ============================================================

void CommandRegistry::registerCommand(const QStringList& keywords,
                                      std::function<QString(const QString&)> handler,
                                      const QString& description,
                                      bool prefixMatch)
{
    m_commands.append(Command{keywords, std::move(handler), description, prefixMatch});
}

// ============================================================
// tryExecute
// ============================================================

CommandRegistry::Result CommandRegistry::tryExecute(const QString& input) const
{
    const QString trimmed = input.trimmed();
    const QString lower   = trimmed.toLower();

    for (const auto& cmd : m_commands) {
        for (const auto& kw : cmd.keywords) {
            bool matches = cmd.prefixMatch
                ? lower.startsWith(kw)
                : (lower == kw || lower.startsWith(kw + QChar(' ')));

            if (matches) {
                return {true, cmd.handler(trimmed)};
            }
        }
    }
    return {false, QString()};
}

// ============================================================
// helpText
// ============================================================

QString CommandRegistry::helpText() const
{
    QString text = QStringLiteral("Системные команды:\n");
    for (const auto& cmd : m_commands) {
        if (!cmd.description.isEmpty()) {
            text += QStringLiteral("• ") + cmd.description + QStringLiteral("\n");
        }
    }
    return text.trimmed();
}
