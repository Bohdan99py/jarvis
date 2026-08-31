#pragma once
// -------------------------------------------------------
// system_tools.h — Набор системных инструментов агента
//
// Здесь описано ЧТО JARVIS умеет делать с машиной. Само
// исполнение переиспользует PcController (мышь, клавиатура,
// окна, громкость, запуск) — новых низкоуровневых движков
// не появляется, появляется только описание для модели.
//
// Категории:
//   apps     — запуск приложений, URL, путей
//   windows  — список/фокус/состояние окон
//   files    — поиск, чтение, запись, папки
//   system   — статус, процессы, громкость, питание
//   input    — клавиатура, мышь, буфер обмена
//   shell    — произвольная команда (Dangerous)
// -------------------------------------------------------

class ToolRegistry;
class PcController;
class ContextTracker;

namespace JarvisTools {

// Регистрирует весь системный набор. pc обязателен.
void registerSystemTools(ToolRegistry& registry, PcController* pc);

// get_context — «что сейчас на экране». Отдельно от системного набора,
// потому что требует живого ContextTracker, а он есть не у всех хостов
// (Telegram-шлюз, тесты).
void registerContextTools(ToolRegistry& registry, ContextTracker* tracker);

} // namespace JarvisTools
