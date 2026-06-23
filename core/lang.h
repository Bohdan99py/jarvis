#pragma once
// -------------------------------------------------------
// lang.h — Строки интерфейса J.A.R.V.I.S. (RU / EN)
// -------------------------------------------------------

#include <QString>
#include <QDateTime>

// Глобальный язык (изменяется через Настройки/Settings → Language)
enum class UiLanguage { Russian, English };

inline UiLanguage& gUiLanguage()
{
    static UiLanguage lang = UiLanguage::Russian;
    return lang;
}

// Макрос для удобства
#define IS_EN (gUiLanguage() == UiLanguage::English)

// -------------------------------------------------------
// Все строки интерфейса
// -------------------------------------------------------
namespace Str {

// --- Меню: Файл / File ---
inline QString menuFile()           { return IS_EN ? "File"             : "Файл"; }
inline QString menuAttach()         { return IS_EN ? "Attach files..."  : "Прикрепить файлы..."; }
inline QString menuClearAttach()    { return IS_EN ? "Clear attachments": "Очистить прикрепления"; }
inline QString menuClearLog()       { return IS_EN ? "Clear log"        : "Очистить лог"; }
inline QString menuExit()           { return IS_EN ? "Exit"             : "Выход"; }

// --- Меню: Настройки / Settings ---
inline QString menuSettings()       { return IS_EN ? "Settings"                 : "Настройки"; }
inline QString menuApiKey()         { return IS_EN ? "Claude API key..."         : "Claude API-ключ..."; }
inline QString menuGeminiKey()      { return IS_EN ? "Gemini API key..."         : "Gemini API-ключ..."; }
inline QString menuAgentMode()      { return IS_EN ? "Agent mode"               : "Режим агентов"; }
inline QString menuVibeCoding()     { return IS_EN ? "Vibecoding mode"           : "Вайбкодинг режим"; }
inline QString menuKeepAttach()     { return IS_EN ? "Keep attachments"          : "Держать прикрепления"; }
inline QString menuKeyboard()       { return IS_EN ? "Virtual keyboard"          : "Виртуальная клавиатура"; }
inline QString menuLanguage()       { return IS_EN ? "Language / Язык"           : "Language / Язык"; }
inline QString menuLangRu()         { return IS_EN ? "Russian (Русский)"         : "Русский (Russian)"; }
inline QString menuLangEn()         { return IS_EN ? "English"                   : "English"; }

// --- Меню: Проект / Project ---
inline QString menuProject()        { return IS_EN ? "Project"              : "Проект"; }
inline QString menuIndexFolder()    { return IS_EN ? "Index folder..."      : "Индексировать папку..."; }
inline QString menuProjectMap()     { return IS_EN ? "Project map"          : "Карта проекта"; }
inline QString menuReindex()        { return IS_EN ? "Re-index"             : "Переиндексировать"; }
inline QString menuProjectInfo()    { return IS_EN ? "Project info"         : "Информация о проекте"; }

// --- Меню: Обновление / Update ---
inline QString menuUpdate()         { return IS_EN ? "Update"              : "Обновление"; }
inline QString menuCheckUpdate()    { return IS_EN ? "Check for updates"   : "Проверить обновления"; }
inline QString menuDownloadUpdate() { return IS_EN ? "Download update"     : "Скачать обновление"; }
inline QString menuReleasePage()    { return IS_EN ? "Releases page"       : "Страница релизов"; }

// --- Меню: Помощь / Help ---
inline QString menuHelp()           { return IS_EN ? "Help"            : "Помощь"; }
inline QString menuAbout()          { return IS_EN ? "About"           : "О программе"; }
inline QString menuCommands()       { return IS_EN ? "Command list"    : "Список команд"; }

// --- Диалоги ---
inline QString dlgApiKeyTitle()     { return IS_EN ? "Claude API Key"             : "API-ключ Claude"; }
inline QString dlgApiKeyLabel()     { return IS_EN ? "Enter your Anthropic API key:" : "Введите ваш Anthropic API-ключ:"; }
inline QString dlgGeminiKeyTitle()  { return IS_EN ? "Gemini API Key"             : "Gemini API-ключ"; }
inline QString dlgGeminiKeyLabel()  { return IS_EN ? "Enter your Google Gemini API key\n(free key at aistudio.google.com):"
                                                   : "Введите ваш Google Gemini API-ключ\n(бесплатный ключ на aistudio.google.com):"; }
inline QString dlgChooseFolder()    { return IS_EN ? "Select project folder"     : "Выберите папку проекта"; }
inline QString dlgRestartNeeded()   { return IS_EN ? "Language changed. Some UI elements will update on next launch."
                                                   : "Язык изменён. Часть элементов обновится при следующем запуске."; }

// --- Приветствие ---
inline QString greetNight() {
    return IS_EN ? "Still up? Respect. JARVIS online"
                 : "Ещё не спишь? Уважаю. JARVIS в сети";
}
inline QString greetMorning() {
    return IS_EN ? "Morning. Systems warmed up"
                 : "Утро. Системы прогреты";
}
inline QString greetDay() {
    return IS_EN ? "Afternoon. All systems nominal"
                 : "День. Все системы в норме";
}
inline QString greetEvening() {
    return IS_EN ? "Evening shift. Ready to work"
                 : "Вечерняя смена. Готов к работе";
}
inline QString greetReady() {
    return IS_EN ? "Online. v" : "В сети. v";
}

// --- Статусы ---
inline QString statusThinking()     { return IS_EN ? "Thinking..."               : "Думаю..."; }
inline QString statusReady()        { return IS_EN ? "Ready"                     : "Готово"; }
inline QString statusIndexing()     { return IS_EN ? "Indexing: "               : "Индексирую: "; }
inline QString statusDownload()     { return IS_EN ? "Downloading: %1%"         : "Скачивание: %1%"; }
inline QString statusAttached()     { return IS_EN ? "Attached files: "         : "Прикреплено файлов: "; }
inline QString statusAttachCleared(){ return IS_EN ? "Attachments cleared."     : "Прикрепления очищены."; }
inline QString statusAttachKept()   { return IS_EN ? "Attachments kept between requests." : "Прикрепления сохраняются между запросами."; }
inline QString statusAttachOneShot(){ return IS_EN ? "Attachments cleared after each send." : "Прикрепления очищаются после каждой отправки."; }

// --- Лог ---
inline QString logSender()          { return IS_EN ? "YOU"    : "ВЫ"; }
inline QString logJarvis()          { return QStringLiteral("JARVIS"); }
inline QString logSystem()          { return IS_EN ? "SYSTEM" : "СИСТЕМА"; }
inline QString logError()           { return IS_EN ? "ERROR"  : "ОШИБКА"; }

// --- Агент инфо ---
inline QString agentClaude()        { return IS_EN ? "🤖 Claude (code)"    : "🤖 Claude (код)"; }
inline QString agentGemini()        { return IS_EN ? "💬 Gemini (chat)"    : "💬 Gemini (чат)"; }
inline QString agentModeOn()        { return IS_EN ? "Multi-agent mode ON. Coding → Claude, Chat → Gemini."
                                                   : "Мультиагентный режим ВКЛ. Код → Claude, Беседа → Gemini."; }
inline QString agentModeOff()       { return IS_EN ? "Multi-agent mode OFF. All queries → Claude."
                                                   : "Мультиагентный режим ВЫКЛ. Все запросы → Claude."; }
inline QString agentNoGeminiKey()   { return IS_EN ? "Gemini key not set. Set it in Settings → Gemini API key."
                                                   : "Ключ Gemini не установлен. Настройки → Gemini API-ключ."; }

// --- API статусы ---
inline QString apiClaudeConnected() { return IS_EN ? "Claude API connected. Attach files via 📎 or drag & drop."
                                                   : "Claude API подключён. Прикрепляйте файлы кнопкой 📎 или перетаскиванием в окно."; }
inline QString apiNoKey()           { return IS_EN ? "Enter a command or 'help'. For AI mode: apikey <your-key>"
                                                   : "Введите команду или «помощь». Для AI-режима: apikey <ваш-ключ>"; }
inline QString apiKeySaved()        { return IS_EN ? "API key saved. Claude API connected." : "API-ключ сохранён. Claude API подключён."; }
inline QString apiGeminiKeySaved()  { return IS_EN ? "Gemini key saved." : "Gemini ключ сохранён."; }

// --- Вайбкодинг ---
inline QString vibeModeOn()         { return IS_EN ? "Vibecoding ON. Code will be pulled from the index or attached files."
                                                   : "Вайбкодинг включён. Нужный код я возьму из индекса либо из прикреплённых файлов."; }
inline QString vibeModeOff()        { return IS_EN ? "Vibecoding OFF. Normal mode." : "Вайбкодинг выключен. Обычный режим."; }
inline QString vibePlaceholder()    { return IS_EN ? "Describe what to do: 'optimize X', 'add Y', 'fix Z'..."
                                                   : "Опиши что сделать: «оптимизируй X», «добавь Y», «исправь Z»..."; }

// --- Инпут-плейсхолдер ---
inline QString inputPlaceholder()   { return IS_EN ? "Enter command or question..." : "Введите команду или вопрос..."; }

// --- Проект ---
inline QString projLoaded()         { return IS_EN ? "Project loaded from cache: " : "Проект загружен из кэша: "; }
inline QString projFiles()          { return IS_EN ? " files)"    : " файлов)"; }
inline QString projIndexed()        { return IS_EN ? "Project indexed!\nFiles: " : "Проект проиндексирован!\nФайлов: "; }
inline QString projSymbols()        { return IS_EN ? ", Symbols: "              : ", Символов: "; }
inline QString projNotIndexed()     { return IS_EN ? "Project not indexed."     : "Проект не проиндексирован."; }
inline QString projChooseFirst()    { return IS_EN ? "Choose a folder first."   : "Сначала выберите папку."; }
inline QString projReindexed()      { return IS_EN ? "Re-indexed: "             : "Переиндексировано: "; }
inline QString projFilesCount()     { return IS_EN ? " files."                  : " файлов."; }
inline QString projInfoLabel()      { return IS_EN ? "Project: "                : "Проект: "; }
inline QString projFilesLabel()     { return IS_EN ? "\nFiles: "                : "\nФайлов: "; }
inline QString projSymbolsLabel()   { return IS_EN ? "\nSymbols: "              : "\nСимволов: "; }
inline QString projClassesLabel()   { return IS_EN ? "\n\nClasses:\n"           : "\n\nКлассы:\n"; }

// --- Обновление ---
inline QString updChecking()        { return IS_EN ? "Checking for updates..."  : "Проверяю обновления..."; }
inline QString updLatest()          { return IS_EN ? "You have the latest version (" : "У вас последняя версия ("; }
inline QString updDownloaded()      { return IS_EN ? "Update downloaded. Launching installer..." : "Обновление скачано. Запускаю установщик..."; }

// --- О программе ---
inline QString aboutText()          {
    return IS_EN
        ? "J.A.R.V.I.S. — Personal AI Assistant\n\nVersion: v%1\nEngines: Claude API + Gemini API\nAuthor: Bohdan99py"
        : "J.A.R.V.I.S. — Personal AI Assistant\n\nВерсия: v%1\nДвижки: Claude API + Gemini API\nАвтор: Bohdan99py";
}

// --- Что нового ---
inline QString whatsNew() {
    return IS_EN
        ? "What's new in v%1:\n"
          "• Offline Brain — answers from learned patterns without API\n"
          "• Auto-caching — every response saved for future offline use\n"
          "• Smart voice — only loads heavy models when needed (saves ~3 GB RAM)\n"
          "• Consciousness — JARVIS knows what it has learned about you\n"
          "• Modern UI — glassmorphism design with transparency\n"
          "• Auto-updater fix — installer now launches correctly"
        : "Что нового в v%1:\n"
          "• Офлайн-мозг — ответы из выученных паттернов без API\n"
          "• Авто-кэш — каждый ответ сохраняется для офлайн-режима\n"
          "• Умный голос — тяжёлые модели грузятся только по запросу (экономия ~3 ГБ RAM)\n"
          "• Сознание — JARVIS знает что выучил о пользователе\n"
          "• Современный UI — полупрозрачный дизайн с glassmorphism\n"
          "• Фикс обновления — установщик теперь запускается корректно";
}

// --- Советы при запуске ---
inline QString startupTip() {
    static const char* tipsRU[] = {
        "Совет: нажми 👍 на ответы которые нравятся — JARVIS учится на них.",
        "Совет: голосовые команды работают на русском и английском.",
        "Совет: перетащи файлы в окно чтобы прикрепить к вопросу.",
        "Совет: JARVIS запоминает паттерны и со временем отвечает быстрее.",
        "Совет: скажи «Джарвис» чтобы активировать голосовой ввод.",
        "Совет: команда «профиль» покажет что JARVIS выучил о тебе.",
        "Совет: команда «память» покажет сохранённые факты.",
    };
    static const char* tipsEN[] = {
        "Tip: click 👍 on responses you like — JARVIS learns from them.",
        "Tip: voice commands work in Russian and English.",
        "Tip: drag files into the window to attach them to your question.",
        "Tip: JARVIS remembers patterns and responds faster over time.",
        "Tip: say 'Jarvis' to activate voice input.",
        "Tip: 'profile' command shows what JARVIS learned about you.",
        "Tip: 'memory' command shows saved facts.",
    };
    int idx = static_cast<int>(QDateTime::currentMSecsSinceEpoch() / 1000) % 7;
    return IS_EN ? QString::fromUtf8(tipsEN[idx]) : QString::fromUtf8(tipsRU[idx]);
}

// --- Fallback команды ---
inline QString fallbackNoCmd()      {
    return IS_EN
        ? "Command not recognized. Type 'help' for command list.\nFor free chat, set an API key."
        : "Не понял команду. Напишите «помощь» для списка команд.\nДля свободного диалога установите API-ключ: apikey <ваш-ключ>";
}

} // namespace Str
