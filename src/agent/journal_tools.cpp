// -------------------------------------------------------
// journal_tools.cpp — см. journal_tools.h
// -------------------------------------------------------

#include "journal_tools.h"

#include "edit_journal.h"
#include "tool_registry.h"

namespace JarvisTools {

void registerJournalTools(ToolRegistry& reg, bool english)
{
    {
        ToolSpec t;
        t.name        = QStringLiteral("list_changes");
        t.category    = QStringLiteral("journal");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "What was changed on disk and what of it can still be undone. Each batch "
            "is one assistant answer, one workflow or one trigger. Actions that changed "
            "the machine without touching a file - a killed process, a shell command, a "
            "commit, typed text - are listed too and marked as not undoable. Call this "
            "before promising the user that something can be reverted.");
        t.schema  = ToolSchema::empty();
        t.handler = [english](const QJsonObject&) -> ToolResult {
            EditJournal& journal = EditJournal::instance();
            return ToolResult::success(
                journal.history(english, 8),
                journal.hasUndoableBatch() ? QStringLiteral("Журнал правок")
                                           : QStringLiteral("Журнал правок пуст"));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("undo_last_change");
        t.category    = QStringLiteral("journal");
        // Откат восстанавливает файлы из копий — по сути та же запись на
        // диск, что и write_file, и подтверждается так же.
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Roll back the last batch of file changes - the whole batch, not one file. "
            "Restores from the backups taken before each write. Anything irreversible "
            "in that batch (a killed process, a shell command, a commit) is reported "
            "back instead of being undone: pass that on to the user verbatim rather "
            "than claiming everything is back to how it was.");
        t.schema  = ToolSchema::empty();
        t.preview = [](const QJsonObject&) {
            return QStringLiteral("Откатить последние правки");
        };
        t.handler = [english](const QJsonObject&) -> ToolResult {
            EditJournal& journal = EditJournal::instance();
            if (!journal.hasUndoableBatch())
                return ToolResult::failure(
                    QStringLiteral("The edit journal is empty - nothing to undo."));

            const QString report = journal.undoLast(english);
            return ToolResult::success(report, QStringLiteral("Правки откачены"));
        };
        reg.registerTool(t);
    }
}

} // namespace JarvisTools
