#pragma once
// -------------------------------------------------------
// journal_tools.h — Откат как инструмент
//
// Откат существовал только как фраза в чате: «отмени правки» ловил
// CommandRegistry по ключевым словам. Значит, модель не могла ни
// предложить откат, ни выполнить его в цепочке действий, ни сказать
// заранее, что именно откатится — она про эту возможность просто
// не знала.
//
// Здесь тот же EditJournal объявлен инструментами: их видно из
// чата, голоса, Ctrl+K и триггера.
// -------------------------------------------------------

class ToolRegistry;

namespace JarvisTools {

// list_changes / undo_last_change
void registerJournalTools(ToolRegistry& registry, bool english = false);

} // namespace JarvisTools
