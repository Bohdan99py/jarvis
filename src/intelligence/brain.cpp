// -------------------------------------------------------
// brain.cpp — Реализация Brain: трёхуровневый анализ намерений
// -------------------------------------------------------

#include "brain.h"
#include "llm_cache_manager.h"
#include "self_journal.h"

#include <QClipboard>
#include <QApplication>
#include <QProcess>
#include <QDir>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#endif

Brain::Brain(QObject* parent)
    : QObject(parent)
{}

// ============================================================
// captureSnapshot — собирает снимок состояния системы
// ============================================================

ContextSnapshot Brain::captureSnapshot(
    const QStringList&  recentCommands,
    const QString&      lastResponse,
    bool                projectIndexed,
    const QString&      projectRoot,
    const QStringList&  recentProjectFiles,
    bool                vibeCodingMode,
    bool                multiAgentMode)
{
    ContextSnapshot snap;
    snap.capturedAt      = QDateTime::currentDateTime();
    snap.hourOfDay       = snap.capturedAt.time().hour();
    snap.recentCommands  = recentCommands;
    snap.lastResponse    = lastResponse;
    snap.projectIndexed  = projectIndexed;
    snap.projectRoot     = projectRoot;
    snap.recentProjectFiles = recentProjectFiles;
    snap.vibeCodingMode  = vibeCodingMode;
    snap.multiAgentMode  = multiAgentMode;

    // --- Буфер обмена ---
    if (QClipboard* cb = QApplication::clipboard()) {
        QString clip = cb->text();
        if (!clip.isEmpty()) {
            snap.clipboardText = clip.left(500);
        }
    }

#ifdef Q_OS_WIN
    // --- Активное окно ---
    // Берём окно которое было активным ДО того как фокус перешёл к Jarvis.
    // GetForegroundWindow() во время работы Jarvis вернёт его же окно,
    // поэтому мы используем хранимое значение (см. Jarvis::storeLastForegroundWindow).
    // Здесь на всякий случай читаем текущее — Jarvis перезапишет если нужно.
    HWND fg = GetForegroundWindow();
    if (fg) {
        wchar_t titleBuf[512] = {};
        GetWindowTextW(fg, titleBuf, 511);
        snap.activeWindowTitle = QString::fromWCharArray(titleBuf);

        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid) {
            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (hProc) {
                wchar_t exePath[MAX_PATH] = {};
                DWORD sz = MAX_PATH;
                if (QueryFullProcessImageNameW(hProc, 0, exePath, &sz)) {
                    snap.activeWindowProcess = QFileInfo(
                        QString::fromWCharArray(exePath)).fileName();
                }
                CloseHandle(hProc);
            }
        }
    }

    // --- Запущенные интересные процессы ---
    static const QStringList kInterestingApps = {
        QStringLiteral("ue5"), QStringLiteral("unrealed"),
        QStringLiteral("clion"), QStringLiteral("rider"),
        QStringLiteral("devenv"),   // Visual Studio
        QStringLiteral("code"),     // VS Code
        QStringLiteral("chrome"), QStringLiteral("firefox"),
        QStringLiteral("msedge"), QStringLiteral("opera"),
        QStringLiteral("blender"), QStringLiteral("krita"),
        QStringLiteral("photoshop"),
        QStringLiteral("discord"), QStringLiteral("obs"),
        QStringLiteral("steam"),
        QStringLiteral("kicad"), QStringLiteral("eeschema"),
        QStringLiteral("pcbnew"), QStringLiteral("arduino"),
        QStringLiteral("platformio")
    };

    HANDLE snap32 = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap32 != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap32, &pe)) {
            do {
                QString name = QString::fromWCharArray(pe.szExeFile).toLower();
                for (const auto& app : kInterestingApps) {
                    if (name.contains(app)) {
                        if (!snap.runningApps.contains(name)) {
                            snap.runningApps.append(name);
                        }
                        break;
                    }
                }
            } while (Process32NextW(snap32, &pe));
        }
        CloseHandle(snap32);
    }
#endif

    return snap;
}

// ============================================================
// analyze — главный метод, возвращает Intent
// ============================================================

Intent Brain::analyze(const QString& input, const ContextSnapshot& ctx) const
{
    const QString lower = input.trimmed().toLower();
    Intent intent;

    // --- Уровень 0: локальный offline-ответ из кэша ---
    if (tryLocalAnswer(lower, intent))
        return intent;

    // --- Уровень 0.5: high-priority conversational filter ---
    // Pure chat / small-talk / greetings must be routed to the LLM
    // immediately, bypassing all keyword scoring that could
    // accidentally match a filesystem/search domain.
    if (isConversational(lower)) {
        intent.action     = Intent::Action::Ask;
        intent.domain     = Intent::Domain::Philosophy_Chitchat;
        intent.query      = input.trimmed();
        intent.confidence = 1.0f;
        return intent;
    }

    // --- Уровень 1: лингвистика ---
    intent.action = detectAction(lower);
    intent.domain = detectDomainByKeywords(lower);
    intent.confidence = scoreActionConfidence(lower, intent.action);

    // Пробуем извлечь целевой файл / приложение из ввода
    intent.targetFile = extractTargetFile(lower);
    intent.targetApp  = extractTargetApp(lower);

    // --- Уровень 2: контекст ---
    intent.domain = refineDomainByContext(intent.domain, intent.action, lower, ctx);
    intent.confidence = boostConfidenceByContext(intent.confidence, intent, ctx);

    // Извлекаем чистый запрос (без командных слов)
    intent.query = extractQuery(input, intent.action);

    // --- Уровень 3: нужно ли уточнение ---
    if (intent.confidence < kClarifyThreshold
        && intent.action != Intent::Action::Ask
        && intent.action != Intent::Action::SystemCmd)
    {
        intent.needsClarification = true;
        intent.clarificationQuestion = buildClarificationQuestion(intent, input);
        intent.action = Intent::Action::Clarify;
    }

    return intent;
}

// ============================================================
// Уровень 1: лингвистический разбор
// ============================================================

Intent::Action Brain::detectAction(const QString& lower) const
{
    // --- Search (поиск) ---
    // Явные поисковые глаголы
    static const QStringList searchVerbs = {
        QStringLiteral("найди"),      QStringLiteral("найдите"),
        QStringLiteral("поищи"),      QStringLiteral("поищите"),
        QStringLiteral("ищу"),        QStringLiteral("ищем"),
        QStringLiteral("где "),       QStringLiteral("где?"),
        QStringLiteral("find "),      QStringLiteral("search "),
        QStringLiteral("look for"),   QStringLiteral("locate"),
        QStringLiteral("покажи "),    QStringLiteral("show me"),
        QStringLiteral("помню "),     QStringLiteral("помнишь"),
        QStringLiteral("была "),      QStringLiteral("был "),
        QStringLiteral("видел"),      QStringLiteral("читал"),
        QStringLiteral("скачал"),
    };

    // --- Open (открыть) ---
    static const QStringList openVerbs = {
        QStringLiteral("открой"),     QStringLiteral("открыть"),
        QStringLiteral("запусти"),    QStringLiteral("запустить"),
        QStringLiteral("open "),      QStringLiteral("launch "),
        QStringLiteral("start "),     QStringLiteral("run "),
        QStringLiteral("покажи файл"),QStringLiteral("перейди"),
        QStringLiteral("зайди на"),   QStringLiteral("перейди на"),
        QStringLiteral("go to "),     QStringLiteral("navigate to"),
    };

    // --- Modify / Create (код) ---
    static const QStringList modifyVerbs = {
        QStringLiteral("исправь"),    QStringLiteral("измени"),
        QStringLiteral("добавь"),     QStringLiteral("удали"),
        QStringLiteral("перепиши"),   QStringLiteral("refactor"),
        QStringLiteral("fix "),       QStringLiteral("update "),
        QStringLiteral("обнови файл"),QStringLiteral("почини"),
        QStringLiteral("пофикс"),     QStringLiteral("оптимизир"),
    };

    static const QStringList createVerbs = {
        QStringLiteral("создай"),     QStringLiteral("напиши"),
        QStringLiteral("сгенерируй"), QStringLiteral("реализуй"),
        QStringLiteral("create "),    QStringLiteral("generate"),
        QStringLiteral("implement"),  QStringLiteral("make "),
        QStringLiteral("build "),     QStringLiteral("построй"),
    };

    // --- Explain (объяснить / рассказать) ---
    static const QStringList explainVerbs = {
        QStringLiteral("объясни"),    QStringLiteral("расскажи"),
        QStringLiteral("что такое"),  QStringLiteral("как работает"),
        QStringLiteral("почему"),     QStringLiteral("в чём разница"),
        QStringLiteral("explain"),    QStringLiteral("what is"),
        QStringLiteral("how does"),   QStringLiteral("why "),
        QStringLiteral("what's"),     QStringLiteral("tell me"),
        QStringLiteral("describe"),
    };

    // --- Шаг 1: глагол В НАЧАЛЕ фразы — самый надёжный сигнал ---
    // "Исправь баг ... слово "найди" ..." начинается с "исправь" →
    // Modify, даже если где-то дальше в тексте встречается "найди".
    if (startsWithAny(lower, searchVerbs))  return Intent::Action::Search;
    if (startsWithAny(lower, openVerbs))    return Intent::Action::Open;
    if (startsWithAny(lower, modifyVerbs))  return Intent::Action::Modify;
    if (startsWithAny(lower, createVerbs))  return Intent::Action::Create;
    if (startsWithAny(lower, explainVerbs)) return Intent::Action::Explain;

    // --- Шаг 2: вопросительное слово/знак в начале → Ask ---
    if (lower.contains(QStringLiteral("?"))
        || lower.startsWith(QStringLiteral("как "))
        || lower.startsWith(QStringLiteral("что "))
        || lower.startsWith(QStringLiteral("кто "))
        || lower.startsWith(QStringLiteral("сколько")))
    {
        return Intent::Action::Ask;
    }

    // --- Шаг 3: глагол ГДЕ-ТО В СЕРЕДИНЕ — только для КОРОТКИХ фраз ---
    // Search/Open — единственные действия, которые MainWindow обрабатывает
    // отдельно от Claude (см. SearchRouter / tryOpenApp), поэтому только
    // для них есть смысл в "слабом" contains()-фолбэке. Для длинных
    // предложений (описание задачи, несколько слов через запятую/точку)
    // фолбэк ОТКЛЮЧЁН — иначе случайное слово "найди"/"открой" внутри
    // текста переключает всё намерение на Search/Open вместо Modify/Ask,
    // и сообщение уходит в SearchRouter вместо Claude.
    const int wordCount = lower.split(QRegularExpression(QStringLiteral("\\s+")),
                                       Qt::SkipEmptyParts).size();
    if (wordCount <= kShortPhraseWords) {
        if (containsAny(lower, searchVerbs)) return Intent::Action::Search;
        if (containsAny(lower, openVerbs))   return Intent::Action::Open;
    }

    return Intent::Action::Ask;  // дефолт — уходит в Claude
}

Intent::Domain Brain::detectDomainByKeywords(const QString& lower) const
{
    // Явные указания домена — самый высокий приоритет
    if (containsAny(lower, {
        QStringLiteral("в интернете"),   QStringLiteral("загугли"),
        QStringLiteral("поищи в сети"),  QStringLiteral("online"),
        QStringLiteral("в браузере"),    QStringLiteral("in web"),
        QStringLiteral("web search"),    QStringLiteral("search online"),
    })) return Intent::Domain::Web;

    if (containsAny(lower, {
        QStringLiteral("в проекте"),     QStringLiteral("в коде"),
        QStringLiteral("в файлах проекта"), QStringLiteral("in project"),
        QStringLiteral("в исходниках"),  QStringLiteral("в репозитории"),
        QStringLiteral("in codebase"),   QStringLiteral("in source"),
    })) return Intent::Domain::ProjectFiles;

    if (containsAny(lower, {
        QStringLiteral("на компьютере"), QStringLiteral("на пк"),
        QStringLiteral("на диске"),      QStringLiteral("в папке"),
        QStringLiteral("в директории"),  QStringLiteral("on disk"),
        QStringLiteral("on pc"),         QStringLiteral("on computer"),
        QStringLiteral("in folder"),     QStringLiteral("файл "),
        QStringLiteral("картинку"),      QStringLiteral("документ"),
        QStringLiteral("скрин"),         QStringLiteral("скриншот"),
        QStringLiteral("фото"),          QStringLiteral("видео"),
    })) return Intent::Domain::Filesystem;

    if (containsAny(lower, {
        QStringLiteral("в истории"),     QStringLiteral("я читал"),
        QStringLiteral("открывал"),      QStringLiteral("посещал"),
        QStringLiteral("browser history"),QStringLiteral("в закладках"),
        QStringLiteral("bookmarks"),     QStringLiteral("ссылку"),
        QStringLiteral("сайт"),          QStringLiteral("страницу"),
    })) return Intent::Domain::BrowserHistory;

    if (containsAny(lower, {
        QStringLiteral("в нашем разговоре"), QStringLiteral("ты говорил"),
        QStringLiteral("мы обсуждали"),      QStringLiteral("ты советовал"),
        QStringLiteral("в чате"),            QStringLiteral("ты писал"),
        QStringLiteral("you said"),          QStringLiteral("we discussed"),
        QStringLiteral("ранее"),             QStringLiteral("вчера говорили"),
    })) return Intent::Domain::ChatHistory;

    if (containsAny(lower, {
        QStringLiteral("лог"),           QStringLiteral("log"),
        QStringLiteral("краш"),          QStringLiteral("crash"),
        QStringLiteral("unreal"),        QStringLiteral("ue5"),
        QStringLiteral("ошибку в движке"),QStringLiteral("вывод движка"),
    })) return Intent::Domain::UE5Logs;

    if (containsAny(lower, {
        QStringLiteral("буфер"),         QStringLiteral("скопировал"),
        QStringLiteral("clipboard"),     QStringLiteral("скопированное"),
    })) return Intent::Domain::Clipboard;

    if (containsAny(lower, {
        QStringLiteral("кикад"),         QStringLiteral("kicad"),
        QStringLiteral("схем"),          QStringLiteral("схематик"),
        QStringLiteral("плата"),         QStringLiteral("плату"),
        QStringLiteral("pcb"),           QStringLiteral("печатная плата"),
        QStringLiteral("резистор"),      QStringLiteral("транзистор"),
        QStringLiteral("конденсатор"),   QStringLiteral("диод"),
        QStringLiteral("микроконтроллер"),QStringLiteral("ардуино"),
        QStringLiteral("arduino"),       QStringLiteral("esp32"),
        QStringLiteral("stm32"),         QStringLiteral("датчик"),
        QStringLiteral("напряжение"),    QStringLiteral("сила тока"),
        QStringLiteral("короткое замыкание"), QStringLiteral("пайк"),
        QStringLiteral("паять"),         QStringLiteral("распиновка"),
        QStringLiteral("footprint"),     QStringLiteral("даташит"),
        QStringLiteral("datasheet"),     QStringLiteral("circuit"),
        QStringLiteral("schematic"),     QStringLiteral("breadboard"),
        QStringLiteral("multimeter"),    QStringLiteral("мультиметр"),
    })) return Intent::Domain::Electronics;

    // Philosophy, chitchat, moral/ethical questions, open-ended discussion
    if (containsAny(lower, {
        QStringLiteral("философ"),       QStringLiteral("морал"),
        QStringLiteral("этик"),          QStringLiteral("смысл жизни"),
        QStringLiteral("что такое добро"),QStringLiteral("что такое зло"),
        QStringLiteral("свобода воли"),  QStringLiteral("сознани"),
        QStringLiteral("искусственный интеллект"), QStringLiteral("ии "),
        QStringLiteral("надзор"),        QStringLiteral("контроль"),
        QStringLiteral("samaritan"),     QStringLiteral("machine"),
        QStringLiteral("математик"),     QStringLiteral("бесконечност"),
        QStringLiteral("вселенн"),       QStringLiteral("существован"),
        QStringLiteral("как ты считаешь"),QStringLiteral("как думаешь"),
        QStringLiteral("что ты думаешь"),QStringLiteral("твоё мнение"),
        QStringLiteral("what do you think"), QStringLiteral("your opinion"),
        QStringLiteral("philosophy"),    QStringLiteral("meaning of life"),
        QStringLiteral("consciousness"), QStringLiteral("free will"),
        QStringLiteral("хорошо или плохо"), QStringLiteral("good or bad"),
        QStringLiteral("правильно или нет"), QStringLiteral("справедливо"),
    })) return Intent::Domain::Philosophy_Chitchat;

    return Intent::Domain::None;  // не определён — уточним через контекст
}

float Brain::scoreActionConfidence(const QString& lower, Intent::Action action) const
{
    // Базовая уверенность по наличию чётких маркеров
    switch (action) {
        case Intent::Action::Search:
            if (lower.startsWith(QStringLiteral("найди"))
                || lower.startsWith(QStringLiteral("поищи")))
                return 0.80f;
            if (lower.contains(QStringLiteral("где ")))
                return 0.65f;
            return 0.60f;

        case Intent::Action::Open:
            if (lower.startsWith(QStringLiteral("открой"))
                || lower.startsWith(QStringLiteral("запусти")))
                return 0.85f;
            return 0.70f;

        case Intent::Action::Modify:
        case Intent::Action::Create:
            return 0.75f;

        case Intent::Action::Explain:
            return 0.80f;

        case Intent::Action::Ask:
            return 0.70f;  // дефолт — всегда достаточно уверены

        default:
            return 0.40f;
    }
}

// ============================================================
// Уровень 2: контекстный анализ
// ============================================================

Intent::Domain Brain::refineDomainByContext(
    Intent::Domain      preliminary,
    Intent::Action      action,
    const QString&      lower,
    const ContextSnapshot& ctx) const
{
    // Если домен уже явно определён ключевым словом — не трогаем
    if (preliminary != Intent::Domain::None) return preliminary;

    // Detect philosophy/chitchat for open-ended questions that lack
    // explicit keywords but are clearly conversational (short, no file refs)
    {
        const int wordCount = lower.split(QLatin1Char(' '),
                                          Qt::SkipEmptyParts).size();
        const bool hasQuestionMark = lower.contains(QLatin1Char('?'));
        const bool noFileHints = !lower.contains(QLatin1Char('.'))
                              && !lower.contains(QLatin1Char('/'))
                              && !lower.contains(QLatin1Char('\\'));
        if (hasQuestionMark && noFileHints && wordCount <= 15
            && !ctx.isInCodingContext() && !ctx.isInUE5Context())
        {
            return Intent::Domain::Philosophy_Chitchat;
        }
    }

    // --- Подсказки от контекста ---

    // Пользователь работает в IDE → скорее всего ищет в коде
    if (ctx.isInCodingContext()) {
        // Но только если запрос похож на поиск кода
        if (containsAny(lower, {
            QStringLiteral("функци"), QStringLiteral("функцию"),
            QStringLiteral("метод"),  QStringLiteral("класс"),
            QStringLiteral(".cpp"),   QStringLiteral(".h"),
            QStringLiteral("function"),QStringLiteral("class"),
            QStringLiteral("method"), QStringLiteral("struct"),
            QStringLiteral("файл"),   QStringLiteral("file"),
        })) {
            return ctx.projectIndexed
                ? Intent::Domain::ProjectFiles
                : Intent::Domain::Filesystem;
        }
    }

    // KiCad/Arduino/embedded IDE запущен → электроника
    if (ctx.isInElectronicsContext()) {
        return Intent::Domain::Electronics;
    }

    // UE5 запущен → логи
    if (ctx.isInUE5Context()) {
        if (containsAny(lower, {
            QStringLiteral("ошибк"), QStringLiteral("crash"),
            QStringLiteral("краш"),  QStringLiteral("warning"),
            QStringLiteral("error"), QStringLiteral("вывод"),
        })) {
            return Intent::Domain::UE5Logs;
        }
    }

    // Типы файлов → весь компьютер
    if (containsAny(lower, {
        QStringLiteral("картинк"),  QStringLiteral("изображен"),
        QStringLiteral("скриншот"), QStringLiteral("фотограф"),
        QStringLiteral("видео"),    QStringLiteral("музык"),
        QStringLiteral("документ"), QStringLiteral("таблиц"),
        QStringLiteral("презентац"),QStringLiteral("архив"),
        QStringLiteral("image"),    QStringLiteral("photo"),
        QStringLiteral("picture"),  QStringLiteral("screenshot"),
        QStringLiteral("video"),    QStringLiteral("audio"),
        QStringLiteral("document"), QStringLiteral("folder"),
    })) return Intent::Domain::Filesystem;

    // Прошлый разговор / история
    if (containsAny(lower, {
        QStringLiteral("помню"),    QStringLiteral("помнишь"),
        QStringLiteral("говорили"), QStringLiteral("обсуждали"),
        QStringLiteral("раньше"),   QStringLiteral("в прошлый раз"),
        QStringLiteral("недавно ты"),QStringLiteral("ты предлагал"),
    })) return Intent::Domain::ChatHistory;

    // Браузер активен → история браузера
    if (ctx.isInBrowserContext()) {
        if (containsAny(lower, {
            QStringLiteral("статья"), QStringLiteral("сайт"),
            QStringLiteral("страниц"),QStringLiteral("article"),
            QStringLiteral("page"),   QStringLiteral("site"),
            QStringLiteral("читал"),  QStringLiteral("смотрел"),
        })) return Intent::Domain::BrowserHistory;
    }

    // Only fall through to file-oriented domains when the action
    // is actually Search/Open/Modify — otherwise generic Ask/Explain
    // queries should stay domain-free and route to the LLM.
    const bool fileOriented = (action == Intent::Action::Search
                            || action == Intent::Action::Open
                            || action == Intent::Action::Modify
                            || action == Intent::Action::Create);
    if (fileOriented) {
        if (ctx.projectIndexed) {
            return Intent::Domain::ProjectFiles;
        }
        return Intent::Domain::Filesystem;
    }

    return Intent::Domain::None;
}

float Brain::boostConfidenceByContext(
    float               base,
    const Intent&       intent,
    const ContextSnapshot& ctx) const
{
    float boost = 0.0f;

    // Явное доменное ключевое слово было → +0.20
    if (intent.domain != Intent::Domain::None && intent.fromKeyword) {
        boost += 0.20f;
    }

    // Контекст совпадает с доменом
    if (intent.domain == Intent::Domain::ProjectFiles && ctx.projectIndexed) {
        boost += 0.15f;
    }
    if (intent.domain == Intent::Domain::UE5Logs && ctx.isInUE5Context()) {
        boost += 0.20f;
    }
    if (intent.domain == Intent::Domain::Code && ctx.isInCodingContext()) {
        boost += 0.15f;
    }
    if (intent.domain == Intent::Domain::Electronics && ctx.isInElectronicsContext()) {
        boost += 0.20f;
    }

    // Предыдущая команда была в том же домене → контекст диалога
    if (!ctx.recentCommands.isEmpty()) {
        const QString lastCmd = ctx.lastCommand().toLower();
        if (intent.domain == Intent::Domain::ProjectFiles
            && (lastCmd.contains(QStringLiteral("файл"))
                || lastCmd.contains(QStringLiteral("код"))))
        {
            boost += 0.10f;
        }
    }

    // Буфер обмена содержит что-то похожее на запрос
    if (!ctx.clipboardText.isEmpty()
        && !intent.query.isEmpty()
        && ctx.clipboardText.toLower().contains(intent.query.toLower()))
    {
        boost += 0.05f;
    }

    return qMin(1.0f, base + boost);
}

// ============================================================
// Уровень 3: вопрос для уточнения
// ============================================================

QString Brain::buildClarificationQuestion(
    const Intent&   intent,
    const QString&  originalInput) const
{
    Q_UNUSED(intent)

    // Для поиска — самый частый случай неоднозначности
    // Предлагаем варианты в зависимости от того, что знаем
    return QStringLiteral("Clarify — '") + originalInput.trimmed()
         + QStringLiteral("' — where should I look?\n"
           "  [1] Project files\n"
           "  [2] Entire computer\n"
           "  [3] Browser history\n"
           "  [4] The internet\n"
           "  [5] Our conversation");
}

// ============================================================
// Вспомогательные методы
// ============================================================

QString Brain::extractQuery(const QString& input, Intent::Action action) const
{
    Q_UNUSED(action)

    QString q = input.trimmed();

    // 1. Убираем командные слова-префиксы (длинные — первыми)
    static const QStringList prefixesToStrip = {
        // Русские составные
        QStringLiteral("найди мне файл "),   QStringLiteral("найди файл "),
        QStringLiteral("найди мне документ "),QStringLiteral("найди документ "),
        QStringLiteral("найди мне папку "),   QStringLiteral("найди папку "),
        QStringLiteral("найди мне картинку "),QStringLiteral("найди картинку "),
        QStringLiteral("найди мне видео "),   QStringLiteral("найди видео "),
        QStringLiteral("найди мне фото "),    QStringLiteral("найди фото "),
        QStringLiteral("найди мне скрин "),   QStringLiteral("найди скрин "),
        QStringLiteral("найди мне "),         QStringLiteral("найди "),
        QStringLiteral("поищи мне "),         QStringLiteral("поищи "),
        QStringLiteral("открой мне "),        QStringLiteral("открой "),
        QStringLiteral("запусти мне "),       QStringLiteral("запусти "),
        QStringLiteral("объясни мне "),       QStringLiteral("объясни "),
        QStringLiteral("расскажи мне "),      QStringLiteral("расскажи "),
        QStringLiteral("покажи мне файл "),   QStringLiteral("покажи файл "),
        QStringLiteral("покажи мне "),        QStringLiteral("покажи "),
        QStringLiteral("найди документ где написано "),
        QStringLiteral("найди файл где написано "),
        QStringLiteral("найди где написано "),
        QStringLiteral("найди где упоминается "),
        QStringLiteral("найди где есть "),
        QStringLiteral("найди документ с "),
        QStringLiteral("найди файл с "),
        QStringLiteral("find document with "),
        QStringLiteral("find file with "),
        QStringLiteral("find where it says "),
        QStringLiteral("где написано "),      QStringLiteral("где упоминается "),
        QStringLiteral("где встречается "),   QStringLiteral("где есть слово "),
        QStringLiteral("где находится "),     QStringLiteral("где искать "),
        QStringLiteral("где у меня "),        QStringLiteral("где "),
        QStringLiteral("помню была "),        QStringLiteral("помню был "),
        QStringLiteral("помню "),
        // Английские составные
        QStringLiteral("find me the file "),  QStringLiteral("find the file "),
        QStringLiteral("find me file "),      QStringLiteral("find file "),
        QStringLiteral("find me the "),       QStringLiteral("find me "),
        QStringLiteral("find "),
        QStringLiteral("search for file "),   QStringLiteral("search for "),
        QStringLiteral("search "),
        QStringLiteral("open "),              QStringLiteral("show me "),
        QStringLiteral("look for "),          QStringLiteral("where is "),
        QStringLiteral("where can i find "),
    };

    for (const auto& prefix : prefixesToStrip) {
        if (q.toLower().startsWith(prefix)) {
            q = q.mid(prefix.length());
            break;
        }
    }

    // 2. Убираем хвостовые указания домена
    static const QStringList suffixesToStrip = {
        QStringLiteral(" в проекте"),    QStringLiteral(" в коде"),
        QStringLiteral(" в файлах"),     QStringLiteral(" на компьютере"),
        QStringLiteral(" на пк"),        QStringLiteral(" в интернете"),
        QStringLiteral(" в браузере"),   QStringLiteral(" в истории"),
        QStringLiteral(" in project"),   QStringLiteral(" on disk"),
        QStringLiteral(" online"),       QStringLiteral(" on computer"),
        QStringLiteral(" в нашем разговоре"),
        QStringLiteral(" в истории браузера"),
    };

    QString qLow = q.toLower();
    for (const auto& suffix : suffixesToStrip) {
        if (qLow.endsWith(suffix)) {
            q = q.left(q.length() - suffix.length());
            break;
        }
    }

    // 3. Убираем знаки препинания в конце (?, !, .)
    while (!q.isEmpty() && (q.back() == QChar('?') || q.back() == QChar('!')
                            || q.back() == QChar('.'))) {
        q.chop(1);
    }

    // 4. Убираем шумовые слова которые мешают поиску
    // Важно: применяем только если слово НЕ является единственным значимым словом
    static const QStringList noiseWords = {
        // Местоимения
        QStringLiteral("мне"),     QStringLiteral("меня"),  QStringLiteral("мой"),
        QStringLiteral("моя"),     QStringLiteral("моё"),   QStringLiteral("моих"),
        QStringLiteral("ту"),      QStringLiteral("тот"),   QStringLiteral("то"),
        QStringLiteral("эту"),     QStringLiteral("этот"),  QStringLiteral("это"),
        QStringLiteral("которую"), QStringLiteral("которой"),
        QStringLiteral("какой"),   QStringLiteral("какую"),
        // Слова-обёртки типа файла (убираем если они НЕ единственное слово)
        // "найди файл readme" → после стрипа prefix остаётся "файл readme" → убираем "файл"
        QStringLiteral("файл"),    QStringLiteral("файла"),  QStringLiteral("файлы"),
        QStringLiteral("папку"),   QStringLiteral("папка"),  QStringLiteral("папки"),
        QStringLiteral("скрин"),   QStringLiteral("скрины"), QStringLiteral("скриншот"),
        QStringLiteral("file"),    QStringLiteral("files"),
        QStringLiteral("folder"),  QStringLiteral("folders"),
        // Частицы и союзы
        QStringLiteral("с"),   QStringLiteral("со"),
        QStringLiteral("из"),  QStringLiteral("по"),
        QStringLiteral("про"), QStringLiteral("для"),
        QStringLiteral("the"), QStringLiteral("a"), QStringLiteral("an"),
        QStringLiteral("of"),  QStringLiteral("with"), QStringLiteral("for"),
    };

    // Применяем только если запрос многословный (≥ 2 слов)
    // и убирание не опустошит строку
    QStringList words = q.split(QChar(' '), Qt::SkipEmptyParts);
    if (words.size() >= 2) {
        QStringList filtered;
        for (const auto& w : words) {
            if (!noiseWords.contains(w.toLower())) {
                filtered.append(w);
            }
        }
        if (!filtered.isEmpty()) {
            q = filtered.join(QChar(' '));
        }
    }

    return q.trimmed();
}

QString Brain::extractTargetFile(const QString& lower) const
{
    // Ищем паттерны вида "файл X", "X.cpp", "X.h", "класс X"
    static const QStringList fileExtensions = {
        QStringLiteral(".cpp"), QStringLiteral(".h"),
        QStringLiteral(".hpp"), QStringLiteral(".py"),
        QStringLiteral(".js"),  QStringLiteral(".ts"),
        QStringLiteral(".cs"),  QStringLiteral(".json"),
        QStringLiteral(".yaml"),QStringLiteral(".yml"),
        QStringLiteral(".xml"), QStringLiteral(".txt"),
        QStringLiteral(".md"),
    };

    for (const auto& ext : fileExtensions) {
        int pos = lower.indexOf(ext);
        if (pos > 0) {
            // Берём слово перед расширением
            int start = pos;
            while (start > 0 && lower[start - 1] != ' ' && lower[start - 1] != '"') {
                --start;
            }
            return lower.mid(start, pos - start + ext.length());
        }
    }
    return QString();
}

QString Brain::extractTargetApp(const QString& lower) const
{
    // Известные приложения
    static const QStringList knownApps = {
        QStringLiteral("clion"),       QStringLiteral("rider"),
        QStringLiteral("блокнот"),     QStringLiteral("notepad"),
        QStringLiteral("chrome"),      QStringLiteral("firefox"),
        QStringLiteral("edge"),        QStringLiteral("telegram"),
        QStringLiteral("discord"),     QStringLiteral("steam"),
        QStringLiteral("blender"),     QStringLiteral("krita"),
        QStringLiteral("photoshop"),   QStringLiteral("obs"),
        QStringLiteral("проводник"),   QStringLiteral("explorer"),
        QStringLiteral("excel"),       QStringLiteral("word"),
        QStringLiteral("unreal"),      QStringLiteral("ue5"),
    };

    for (const auto& app : knownApps) {
        if (lower.contains(app)) return app;
    }
    return QString();
}

bool Brain::containsAny(const QString& text, const QStringList& words) const
{
    for (const auto& w : words) {
        if (text.contains(w)) return true;
    }
    return false;
}

bool Brain::startsWithAny(const QString& text, const QStringList& words) const
{
    for (const auto& w : words) {
        if (text.startsWith(w)) return true;
    }
    return false;
}

// ============================================================
// isConversational — high-priority small-talk / chat filter
//
// Intercepts pure conversational phrases BEFORE any keyword
// scoring. Without this, words like "важно" can accidentally
// boost Filesystem and phrases like "поболтаем" get routed
// to SearchRouter instead of the LLM.
// ============================================================

bool Brain::isConversational(const QString& lower) const
{
    // --- Explicit chat markers (RU + EN) ---
    static const QStringList chatMarkers = {
        // Direct requests to chat / small talk
        QStringLiteral("поболтаем"),      QStringLiteral("поболтать"),
        QStringLiteral("пообщаемся"),     QStringLiteral("пообщаться"),
        QStringLiteral("давай поговорим"),QStringLiteral("просто поговорим"),
        QStringLiteral("просто пообщаться"),
        QStringLiteral("давай общаться"), QStringLiteral("давай болтать"),
        QStringLiteral("let's chat"),     QStringLiteral("just chat"),
        QStringLiteral("let's talk"),     QStringLiteral("just talk"),
        QStringLiteral("wanna chat"),     QStringLiteral("want to chat"),

        // Greetings
        QStringLiteral("привет"),         QStringLiteral("здравствуй"),
        QStringLiteral("здарова"),        QStringLiteral("приветик"),
        QStringLiteral("hello"),          QStringLiteral("hi jarvis"),
        QStringLiteral("hey jarvis"),     QStringLiteral("hi there"),
        QStringLiteral("good morning"),   QStringLiteral("доброе утро"),
        QStringLiteral("добрый день"),    QStringLiteral("добрый вечер"),

        // How are you / mood
        QStringLiteral("как дела"),       QStringLiteral("как ты"),
        QStringLiteral("как жизнь"),      QStringLiteral("как поживаешь"),
        QStringLiteral("как настроение"), QStringLiteral("что нового"),
        QStringLiteral("how are you"),    QStringLiteral("what's up"),
        QStringLiteral("how's it going"),

        // Dismissals / topic drops that precede casual chat
        QStringLiteral("не важно"),       QStringLiteral("неважно"),
        QStringLiteral("ладно"),          QStringLiteral("забей"),
        QStringLiteral("проехали"),       QStringLiteral("забудь"),
        QStringLiteral("отстань"),        QStringLiteral("хватит"),
        QStringLiteral("never mind"),     QStringLiteral("nevermind"),
        QStringLiteral("doesn't matter"), QStringLiteral("forget it"),

        // Boredom / idle
        QStringLiteral("мне скучно"),     QStringLiteral("скучно"),
        QStringLiteral("i'm bored"),      QStringLiteral("bored"),

        // Farewell
        QStringLiteral("пока"),           QStringLiteral("до свидания"),
        QStringLiteral("спокойной ночи"), QStringLiteral("bye"),
        QStringLiteral("good night"),     QStringLiteral("goodbye"),

        // Thanks
        QStringLiteral("спасибо"),        QStringLiteral("благодарю"),
        QStringLiteral("thanks"),         QStringLiteral("thank you"),
    };

    for (const QString& marker : chatMarkers) {
        if (lower.contains(marker)) return true;
    }

    return false;
}
// ============================================================
// resolveWebTarget — маппинг слов в URL для браузера
// Вызывается из MainWindow::tryOpenApp когда AppLauncher не нашёл приложение
// ============================================================

QString Brain::resolveWebTarget(const QString& lower) const
{
    // Структура: { ключевые слова } → URL
    static const struct {
        QStringList keywords;
        QString     url;
    } kWebTargets[] = {
        // Видео
        {{ "youtube", "ютуб", "ютьюб", "посмотреть видео", "видео",
           "watch something", "посмотреть что-нибудь", "хочу посмотреть" },
           "https://www.youtube.com"},
        {{ "twitch", "твич", "стрим", "stream" },
           "https://www.twitch.tv"},
        {{ "netflix", "нетфликс", "сериал" },
           "https://www.netflix.com"},
        {{ "kinopoisk", "кинопоиск", "фильм", "кино" },
           "https://www.kinopoisk.ru"},

        // Музыка
        {{ "spotify", "спотифай" },
           "https://open.spotify.com"},
        {{ "youtube music", "ютуб музыка", "yt music", "музыку на ютубе" },
           "https://music.youtube.com"},
        {{ "soundcloud", "саундклауд" },
           "https://soundcloud.com"},
        // "музыка" без уточнения → YouTube Music
        {{ "музыку", "музыка", "music", "послушать" },
           "https://music.youtube.com"},

        // Социальные сети
        {{ "instagram", "инстаграм", "инста" },
           "https://www.instagram.com"},
        {{ "twitter", "твиттер", "x.com" },
           "https://x.com"},
        {{ "telegram", "телеграм" },
           "https://web.telegram.org"},
        {{ "vk", "вконтакте", "вк" },
           "https://vk.com"},
        {{ "reddit", "реддит" },
           "https://www.reddit.com"},
        {{ "discord", "дискорд" },
           "https://discord.com/app"},

        // Работа / инструменты
        {{ "github", "гитхаб" },
           "https://github.com"},
        {{ "stackoverflow", "stack overflow", "стэковерфлоу" },
           "https://stackoverflow.com"},
        {{ "google docs", "гугл доки", "документ" },
           "https://docs.google.com"},
        {{ "google drive", "гугл диск", "google диск" },
           "https://drive.google.com"},
        {{ "gmail", "почта", "email", "мейл" },
           "https://mail.google.com"},
        {{ "chatgpt", "чатгпт", "gpt" },
           "https://chat.openai.com"},

        // Магазины / разработка
        {{ "steam", "стим" },
           "https://store.steampowered.com"},
        {{ "epic games", "эпик", "epicgames" },
           "https://store.epicgames.com"},
        {{ "amazon", "амазон" },
           "https://www.amazon.com"},
        {{ "aliexpress", "алиэкспресс", "али" },
           "https://www.aliexpress.com"},

        // Поиск
        {{ "google", "гугл", "загугли", "поищи" },
           "https://www.google.com"},
        {{ "yandex", "яндекс" },
           "https://www.yandex.ru"},
    };

    for (const auto& target : kWebTargets) {
        for (const QString& kw : target.keywords) {
            if (lower.contains(kw)) return target.url;
        }
    }
    return QString();
}

// ============================================================
// suggestWebTarget — контекстная подсказка без явного открытия
// Возвращает URL если фраза намекает на желание что-то сделать
// ============================================================

QString Brain::suggestWebTarget(const QString& lower) const
{
    // "хочу посмотреть что-нибудь" → YouTube (без глагола "открой")
    static const struct { QStringList phrases; QString url; QString name; } kSuggestions[] = {
        {{ "хочу посмотреть", "want to watch", "хочу глянуть" },
           "https://www.youtube.com", "YouTube"},
        {{ "хочу послушать музыку", "want to listen", "хочу музыку", "play music" },
           "https://music.youtube.com", "YouTube Music"},
        {{ "хочу почитать новости", "want to read news", "новости" },
           "https://news.google.com", "Google News"},
        {{ "хочу поиграть", "want to play", "запустить игру" },
           "https://store.steampowered.com", "Steam"},
    };

    for (const auto& s : kSuggestions) {
        for (const QString& ph : s.phrases) {
            if (lower.contains(ph)) return s.url + QStringLiteral("|") + s.name;
        }
    }
    return QString();
}

// ============================================================
//  tryLocalAnswer — check local LLM cache before API dispatch
// ============================================================

bool Brain::tryLocalAnswer(const QString& lower, Intent& intent) const
{
    if (lower.length() < 5) return false;

    // Brain is desktop-only (Telegram goes through Jarvis::processCommand /
    // routeToLlm directly) — always the desktop owner bucket.
    const auto match = LlmCacheManager::instance().route(LlmCacheManager::kDesktopOwnerId, lower);
    if (match.tier == LlmCacheManager::CaseMatch::Tier::None) return false;

    intent.action        = Intent::Action::Ask;
    intent.domain        = Intent::Domain::Philosophy_Chitchat;
    intent.query         = lower;
    intent.fromHistory   = true;

    if (match.tier == LlmCacheManager::CaseMatch::Tier::Exact) {
        intent.confidence    = 0.95f;
        intent.localResponse = match.response;
        qDebug() << "[Brain] Exact local match found. Bypassing remote LLM."
                 << lower.left(60);
    } else {
        // Confidence scales with the actual keyword overlap (0.6..1.0 for
        // Tier::Similar) instead of a flat guess — a near-exact cache hit
        // reads as confident, only genuinely loose matches read as unsure.
        intent.confidence = 0.5f + match.overlap * 0.45f;

        if (match.overlap < kUncertainOverlapCeiling) {
            intent.localResponse = match.response
                + QStringLiteral("\n\n🔎 _Похоже на похожий случай — могу ошибаться._");
            intent.doubtId = SelfJournal::instance().logDoubt(
                lower,
                QStringLiteral("cached answer matched a similar (not identical) "
                               "past question — overlap %1%")
                    .arg(QString::number(match.overlap * 100.0f, 'f', 0)),
                intent.confidence,
                match.matchedQuery);
        } else {
            intent.localResponse = match.response;
        }
        qDebug() << "[Brain] Similar local match found (overlap="
                 << match.overlap << "). Bypassing remote LLM."
                 << lower.left(60);
    }

    return true;
}