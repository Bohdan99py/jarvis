// -------------------------------------------------------
// brain.cpp — Реализация Brain: трёхуровневый анализ намерений
// -------------------------------------------------------

#include "brain.h"

#include <QClipboard>
#include <QApplication>
#include <QProcess>
#include <QDir>

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
        QStringLiteral("steam")
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

    // --- Уровень 1: лингвистика ---
    intent.action = detectAction(lower);
    intent.domain = detectDomainByKeywords(lower);
    intent.confidence = scoreActionConfidence(lower, intent.action);

    // Пробуем извлечь целевой файл / приложение из ввода
    intent.targetFile = extractTargetFile(lower);
    intent.targetApp  = extractTargetApp(lower);

    // --- Уровень 2: контекст ---
    intent.domain = refineDomainByContext(intent.domain, lower, ctx);
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

    if (containsAny(lower, searchVerbs))  return Intent::Action::Search;
    if (containsAny(lower, openVerbs))    return Intent::Action::Open;
    if (containsAny(lower, modifyVerbs))  return Intent::Action::Modify;
    if (containsAny(lower, createVerbs))  return Intent::Action::Create;
    if (containsAny(lower, explainVerbs)) return Intent::Action::Explain;

    // Если есть вопросительное слово или знак — скорее всего Ask
    if (lower.contains(QStringLiteral("?"))
        || lower.startsWith(QStringLiteral("как "))
        || lower.startsWith(QStringLiteral("что "))
        || lower.startsWith(QStringLiteral("кто "))
        || lower.startsWith(QStringLiteral("сколько")))
    {
        return Intent::Action::Ask;
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
    const QString&      lower,
    const ContextSnapshot& ctx) const
{
    // Если домен уже явно определён ключевым словом — не трогаем
    if (preliminary != Intent::Domain::None) return preliminary;

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

    // Если проект индексирован и ничего другого не подошло —
    // поиск вероятнее в проекте (для разработчика это частый случай)
    if (ctx.projectIndexed) {
        return Intent::Domain::ProjectFiles;
    }

    // Финальный дефолт — весь компьютер
    return Intent::Domain::Filesystem;
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
    return QStringLiteral("Уточни — «") + originalInput.trimmed()
         + QStringLiteral("» — где искать?\n"
           "  [1] В файлах проекта\n"
           "  [2] На компьютере (все файлы)\n"
           "  [3] В истории браузера\n"
           "  [4] В интернете\n"
           "  [5] В нашем разговоре");
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