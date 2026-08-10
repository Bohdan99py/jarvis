#pragma once
// -------------------------------------------------------
// lang.h — J.A.R.V.I.S. UI strings (EN / RU)
// -------------------------------------------------------

#include <QString>
#include <QDateTime>

enum class UiLanguage { Russian, English };

inline UiLanguage& gUiLanguage()
{
    static UiLanguage lang = UiLanguage::English;
    return lang;
}

#define IS_EN (gUiLanguage() == UiLanguage::English)

// -------------------------------------------------------
// All UI strings
// -------------------------------------------------------
namespace Str {

// --- Menu: File ---
inline QString menuFile()           { return IS_EN ? "File"             : "Файл"; }
inline QString menuAttach()         { return IS_EN ? "Attach files..."  : "Прикрепить файлы..."; }
inline QString menuClearAttach()    { return IS_EN ? "Clear attachments": "Очистить прикрепления"; }
inline QString menuClearLog()       { return IS_EN ? "Clear log"        : "Очистить лог"; }
inline QString menuExit()           { return IS_EN ? "Exit"             : "Выход"; }

// --- Menu: Settings ---
inline QString menuSettings()       { return IS_EN ? "Settings"                 : "Настройки"; }
inline QString menuApiKey()         { return IS_EN ? "Claude API key..."         : "Claude API-ключ..."; }
inline QString menuAgentMode()      { return IS_EN ? "Agent mode"               : "Режим агентов"; }
inline QString menuVibeCoding()     { return IS_EN ? "Vibecoding mode"           : "Вайбкодинг режим"; }
inline QString menuKeepAttach()     { return IS_EN ? "Keep attachments"          : "Держать прикрепления"; }
inline QString menuKeyboard()       { return IS_EN ? "Virtual keyboard"          : "Виртуальная клавиатура"; }
inline QString menuLanguage()       { return IS_EN ? "Language / Язык"           : "Language / Язык"; }
inline QString menuLangRu()         { return IS_EN ? "Russian (Русский)"         : "Русский (Russian)"; }
inline QString menuLangEn()         { return IS_EN ? "English"                   : "English"; }

// --- Menu: Project ---
inline QString menuProject()        { return IS_EN ? "Project"              : "Проект"; }
inline QString menuIndexFolder()    { return IS_EN ? "Index folder..."      : "Индексировать папку..."; }
inline QString menuProjectMap()     { return IS_EN ? "Project map"          : "Карта проекта"; }
inline QString menuReindex()        { return IS_EN ? "Re-index"             : "Переиндексировать"; }
inline QString menuProjectInfo()    { return IS_EN ? "Project info"         : "Информация о проекте"; }

// --- Menu: Update ---
inline QString menuUpdate()         { return IS_EN ? "Update"              : "Обновление"; }
inline QString menuCheckUpdate()    { return IS_EN ? "Check for updates"   : "Проверить обновления"; }
inline QString menuDownloadUpdate() { return IS_EN ? "Download update"     : "Скачать обновление"; }
inline QString menuReleasePage()    { return IS_EN ? "Releases page"       : "Страница релизов"; }

// --- Menu: Help ---
inline QString menuHelp()           { return IS_EN ? "Help"            : "Помощь"; }
inline QString menuAbout()          { return IS_EN ? "About"           : "О программе"; }
inline QString menuCommands()       { return IS_EN ? "Command list"    : "Список команд"; }

// --- Dialogs ---
inline QString dlgApiKeyTitle()     { return IS_EN ? "Claude API Key"             : "API-ключ Claude"; }
inline QString dlgApiKeyLabel()     { return IS_EN ? "Enter your Anthropic API key:" : "Введите ваш Anthropic API-ключ:"; }
inline QString dlgChooseFolder()    { return IS_EN ? "Select project folder"     : "Выберите папку проекта"; }
inline QString dlgRestartNeeded()   { return IS_EN ? "Language changed. Some UI elements will update on next launch."
                                                   : "Язык изменён. Часть элементов обновится при следующем запуске."; }

// --- Greetings ---
inline QString greetNight() {
    return IS_EN ? "Still up? Bold move. JARVIS online — let's make it count."
                 : "Ещё не спишь? Уважаю. JARVIS в сети";
}
inline QString greetMorning() {
    return IS_EN ? "Morning, sir. Systems warmed up, coffee not included."
                 : "Утро. Системы прогреты";
}
inline QString greetDay() {
    return IS_EN ? "Afternoon. All systems nominal — as they should be."
                 : "День. Все системы в норме";
}
inline QString greetEvening() {
    return IS_EN ? "Evening shift. I don't get tired, but I appreciate the company."
                 : "Вечерняя смена. Готов к работе";
}
inline QString greetReady() {
    return IS_EN ? "Online. v" : "В сети. v";
}

// --- Status ---
inline QString statusThinking()     { return IS_EN ? "Processing..."              : "Думаю..."; }
inline QString statusReady()        { return IS_EN ? "Standing by"                : "Готово"; }
inline QString statusIndexing()     { return IS_EN ? "Indexing: "               : "Индексирую: "; }
inline QString statusDownload()     { return IS_EN ? "Downloading: %1%"         : "Скачивание: %1%"; }
inline QString statusAttached()     { return IS_EN ? "Files attached: "         : "Прикреплено файлов: "; }
inline QString statusAttachCleared(){ return IS_EN ? "Attachments cleared. Slate wiped clean." : "Прикрепления очищены."; }
inline QString statusAttachKept()   { return IS_EN ? "Attachments persist between requests." : "Прикрепления сохраняются между запросами."; }
inline QString statusAttachOneShot(){ return IS_EN ? "Attachments cleared after each send." : "Прикрепления очищаются после каждой отправки."; }

// --- Log labels ---
inline QString logSender()          { return IS_EN ? "YOU"    : "ВЫ"; }
inline QString logJarvis()          { return QStringLiteral("JARVIS"); }
inline QString logSystem()          { return IS_EN ? "SYSTEM" : "СИСТЕМА"; }
inline QString logError()           { return IS_EN ? "ERROR"  : "ОШИБКА"; }

// --- Agent info ---
inline QString agentClaude()        { return IS_EN ? "🤖 Claude (code)"    : "🤖 Claude (код)"; }
inline QString agentModeOff()       { return IS_EN ? "Multi-agent mode OFF. All queries → Claude."
                                                   : "Мультиагентный режим ВЫКЛ. Все запросы → Claude."; }

// --- API status ---
inline QString apiClaudeConnected() { return IS_EN ? "Claude API connected. Attach files via 📎 or drag & drop."
                                                   : "Claude API подключён. Прикрепляйте файлы кнопкой 📎 или перетаскиванием в окно."; }
inline QString apiNoKey()           { return IS_EN ? "Enter a command or 'help'. For AI mode: apikey <your-key>"
                                                   : "Введите команду или «помощь». Для AI-режима: apikey <ваш-ключ>"; }
inline QString apiKeySaved()        { return IS_EN ? "API key locked in. Claude API connected — at your service." : "API-ключ сохранён. Claude API подключён."; }

// --- Vibecoding ---
inline QString vibeModeOn()         { return IS_EN ? "Vibecoding ON. I'll pull the code from the index or attachments."
                                                   : "Вайбкодинг включён. Нужный код я возьму из индекса либо из прикреплённых файлов."; }
inline QString vibeModeOff()        { return IS_EN ? "Vibecoding OFF. Back to standard ops." : "Вайбкодинг выключен. Обычный режим."; }
inline QString vibePlaceholder()    { return IS_EN ? "Describe what to do: 'optimize X', 'add Y', 'fix Z'..."
                                                   : "Опиши что сделать: «оптимизируй X», «добавь Y», «исправь Z»..."; }

// --- Input placeholder ---
inline QString inputPlaceholder()   { return IS_EN ? "Talk to me — command, question, anything..." : "Введите команду или вопрос..."; }

// --- Project ---
inline QString projLoaded()         { return IS_EN ? "Project loaded from cache: " : "Проект загружен из кэша: "; }
inline QString projFiles()          { return IS_EN ? " files)"    : " файлов)"; }
inline QString projIndexed()        { return IS_EN ? "Project indexed.\nFiles: " : "Проект проиндексирован!\nФайлов: "; }
inline QString projSymbols()        { return IS_EN ? ", Symbols: "              : ", Символов: "; }
inline QString projNotIndexed()     { return IS_EN ? "No project indexed yet."  : "Проект не проиндексирован."; }
inline QString projChooseFirst()    { return IS_EN ? "Pick a folder first."     : "Сначала выберите папку."; }
inline QString projReindexed()      { return IS_EN ? "Re-indexed: "             : "Переиндексировано: "; }
inline QString projFilesCount()     { return IS_EN ? " files."                  : " файлов."; }
inline QString projInfoLabel()      { return IS_EN ? "Project: "                : "Проект: "; }
inline QString projFilesLabel()     { return IS_EN ? "\nFiles: "                : "\nФайлов: "; }
inline QString projSymbolsLabel()   { return IS_EN ? "\nSymbols: "              : "\nСимволов: "; }
inline QString projClassesLabel()   { return IS_EN ? "\n\nClasses:\n"           : "\n\nКлассы:\n"; }

// --- Update ---
inline QString updChecking()        { return IS_EN ? "Checking for updates..."  : "Проверяю обновления..."; }
inline QString updLatest()          { return IS_EN ? "You're running the latest (" : "У вас последняя версия ("; }
inline QString updDownloaded()      { return IS_EN ? "Update downloaded. Launching installer..." : "Обновление скачано. Запускаю установщик..."; }

// --- About ---
inline QString aboutText()          {
    return IS_EN
        ? "J.A.R.V.I.S. — Just A Rather Very Intelligent System\n\nVersion: v%1\nEngines: Claude API + Ollama\nAuthor: Bohdan99py"
        : "J.A.R.V.I.S. — Personal AI Assistant\n\nВерсия: v%1\nДвижки: Claude API + Ollama\nАвтор: Bohdan99py";
}

// --- What's New ---
inline QString whatsNew() {
    return IS_EN
        ? "What's new in v%1:\n"
          "• Deep Context — JARVIS tracks your workflow and adapts in real time\n"
          "• Multi-user — local user profiles, each with their own memory\n"
          "• Knowledge Base — learns facts about you across sessions\n"
          "• Offline Brain — answers from learned patterns without API\n"
          "• Auto-caching — every response saved for future offline use\n"
          "• Modern UI — glassmorphism design with transparency"
        : "Что нового в v%1:\n"
          "• Офлайн-мозг — ответы из выученных паттернов без API\n"
          "• Авто-кэш — каждый ответ сохраняется для офлайн-режима\n"
          "• Умный голос — тяжёлые модели грузятся только по запросу (экономия ~3 ГБ RAM)\n"
          "• Сознание — JARVIS знает что выучил о пользователе\n"
          "• Современный UI — полупрозрачный дизайн с glassmorphism\n"
          "• Фикс обновления — установщик теперь запускается корректно";
}

// --- Startup tips ---
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
        "Pro tip: hit 👍 on responses you like — I learn from your taste.",
        "Voice commands work in both Russian and English. Try it.",
        "Drag files into this window to attach them. I'll read anything.",
        "I remember patterns. The more you talk, the faster I get.",
        "Say 'Jarvis' out loud to activate voice input. Like the movies.",
        "Type 'profile' to see what I've learned about your work style.",
        "Type 'memory' to see all the facts I've stored for you.",
    };
    int idx = static_cast<int>(QDateTime::currentMSecsSinceEpoch() / 1000) % 7;
    return IS_EN ? QString::fromUtf8(tipsEN[idx]) : QString::fromUtf8(tipsRU[idx]);
}

// --- Fallback ---
inline QString fallbackNoCmd()      {
    return IS_EN
        ? "Didn't catch that. Type 'help' to see what I can do.\nFor free conversation, set an API key first."
        : "Не понял команду. Напишите «помощь» для списка команд.\nДля свободного диалога установите API-ключ: apikey <ваш-ключ>";
}

} // namespace Str
