// -------------------------------------------------------
// clipboard_tools.cpp — см. clipboard_tools.h
// -------------------------------------------------------

#include "clipboard_tools.h"

#include "clipboard_watcher.h"
#include "tool_registry.h"

#include <QDebug>

namespace JarvisTools {

void registerClipboardTools(ToolRegistry& reg, ClipboardWatcher* watcher)
{
    if (!watcher) {
        qWarning() << "[Tools] registerClipboardTools: watcher is null";
        return;
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("clipboard_history");
        t.category    = QStringLiteral("clipboard");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "What the user copied earlier in this session, newest first, with the app "
            "it came from. Index 1 is the most recent clip. Use it for \"what did I "
            "copy before this\", \"that command I copied earlier\", or to find the "
            "index for clipboard_restore. By default only previews are returned - ask "
            "for one specific index to see its full text.");
        t.schema = ToolSchema()
                       .integer("limit", "How many entries to list (default 10)", false)
                       .integer("index", "Return the full text of this entry instead", false)
                       .str("kind", "Only entries of this kind: url, path, code, error, text", false)
                       .build();
        t.handler = [watcher](const QJsonObject& a) -> ToolResult {
            const int index = a.value(QStringLiteral("index")).toInt(0);

            if (index > 0) {
                const ClipEntry* entry = watcher->at(index);
                if (!entry)
                    return ToolResult::failure(
                        QStringLiteral("No clipboard entry #%1 (history holds %2).")
                            .arg(index).arg(watcher->historySize()));
                return ToolResult::success(
                    entry->text,
                    QStringLiteral("Буфер #%1: %2").arg(index).arg(entry->preview(40)));
            }

            const int limit = qBound(1, a.value(QStringLiteral("limit")).toInt(10), 50);
            const QString kind = a.value(QStringLiteral("kind")).toString().trimmed();

            const QVector<ClipEntry> all = watcher->history(0);
            QStringList lines;
            int shown = 0;
            for (int i = 0; i < all.size() && shown < limit; ++i) {
                if (!kind.isEmpty() && all[i].kind.compare(kind, Qt::CaseInsensitive) != 0)
                    continue;
                lines << QStringLiteral("%1. %2").arg(i + 1).arg(all[i].preview());
                ++shown;
            }

            if (lines.isEmpty())
                return ToolResult::success(
                    QStringLiteral("Clipboard history is empty for that filter."),
                    QStringLiteral("История буфера пуста"));

            return ToolResult::success(lines.join(QChar('\n')),
                                       QStringLiteral("Буфер: записей %1").arg(shown));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("clipboard_restore");
        t.category    = QStringLiteral("clipboard");
        // Перезапись буфера обмена уничтожает то, что там лежало, а
        // человек мог скопировать это секунду назад для другого дела.
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Put an earlier clip back into the clipboard so the user can paste it "
            "again. Index 1 is the most recent clip, 2 the one before it - which is "
            "what \"give me back what I copied before this\" means.");
        t.schema = ToolSchema()
                       .integer("index", "Which entry from clipboard_history (1 = newest)")
                       .build();
        t.preview = [watcher](const QJsonObject& a) {
            const int index = a.value(QStringLiteral("index")).toInt(0);
            const ClipEntry* entry = watcher->at(index);
            return entry ? QStringLiteral("Вернуть в буфер: %1").arg(entry->preview(50))
                         : QStringLiteral("Вернуть в буфер запись #%1").arg(index);
        };
        t.handler = [watcher](const QJsonObject& a) -> ToolResult {
            const int index = a.value(QStringLiteral("index")).toInt(0);
            const ClipEntry* entry = watcher->at(index);
            if (!entry)
                return ToolResult::failure(
                    QStringLiteral("No clipboard entry #%1 (history holds %2).")
                        .arg(index).arg(watcher->historySize()));

            const QString preview = entry->preview(50);
            if (!watcher->restore(index))
                return ToolResult::failure(QStringLiteral("Could not write to the clipboard."));

            return ToolResult::success(
                QStringLiteral("Clipboard now holds entry #%1 again.").arg(index),
                QStringLiteral("В буфере: %1").arg(preview));
        };
        reg.registerTool(t);
    }
}

} // namespace JarvisTools
