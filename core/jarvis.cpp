// -------------------------------------------------------
// jarvis.cpp — Ядро J.A.R.V.I.S.: команды, TTS, мозги
// -------------------------------------------------------

#include "jarvis.h"
#include "virtual_keyboard.h"
#include "session_memory.h"
#include "claude_api.h"
#include "action_predictor.h"
#include "auto_updater.h"
#include "project_indexer.h"
#include "code_actions.h"

#include <sapi.h>
#include <shellapi.h>
#include <lmcons.h>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QThread>
#include <QMutexLocker>
#include <QMap>
#include <QSet>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

// Дефайны из CMake
#ifndef JARVIS_VERSION
#define JARVIS_VERSION "2.0.0"
#endif
#ifndef JARVIS_GITHUB_USER
#define JARVIS_GITHUB_USER "Bohdan99py"
#endif
#ifndef JARVIS_GITHUB_REPO
#define JARVIS_GITHUB_REPO "jarvis"
#endif

// ============================================================
// Конструктор / Деструктор
// ============================================================

Jarvis::Jarvis(QObject* parent)
    : QObject(parent)
{
    m_memory      = new SessionMemory(this);
    m_claudeApi   = new ClaudeApi(m_memory, this);
    m_predictor   = new ActionPredictor(m_memory, this);
    m_keyEmulator = new KeyEmulator(this);
    m_indexer     = new ProjectIndexer(this);
    m_codeActions = new CodeActions(this);

    // Автообновление
    m_updater = new AutoUpdater(
        QStringLiteral(JARVIS_VERSION),
        QStringLiteral(JARVIS_GITHUB_USER),
        QStringLiteral(JARVIS_GITHUB_REPO),
        this
    );

    registerCommands();

    // Реакция на ошибки API
    connect(m_claudeApi, &ClaudeApi::apiError, this, [this](const QString& err) {
        emit asyncResponseError(err);
    });

    // Синхронизация информации об индексе с системным промптом
    connect(m_indexer, &ProjectIndexer::indexingFinished, this,
            [this](int, int) { syncProjectInfoToMemory(); });
    connect(m_indexer, &ProjectIndexer::fileReindexed, this,
            [this](const QString&) { syncProjectInfoToMemory(); });

    // Если индекс загружен из кэша на старте
    if (m_indexer->fileCount() > 0) {
        syncProjectInfoToMemory();
    }
}

Jarvis::~Jarvis()
{
    m_predictor->savePatterns();
    m_memory->savePersistent();
    m_indexer->saveIndex();
}

// ============================================================
// Синхронизация данных индекса с SessionMemory (для system prompt)
// ============================================================

void Jarvis::syncProjectInfoToMemory()
{
    if (m_indexer->fileCount() == 0) {
        m_memory->clearProjectInfo();
        return;
    }

    m_memory->setProjectInfo(
        m_indexer->projectRoot(),
        m_indexer->projectMap(),
        m_indexer->fileCount(),
        m_indexer->symbolCount()
    );

    // code_actions должен знать корень проекта
    m_codeActions->setProjectRoot(m_indexer->projectRoot());
}

// ============================================================
// Регистрация команд
// ============================================================

void Jarvis::registerCommands()
{
    // --- API-ключ ---
    m_registry.registerCommand(
        {QStringLiteral("apikey "), QStringLiteral("ключ ")},
        [this](const QString& s) { return cmdSetApiKey(s); },
        QStringLiteral("apikey <ключ> — установить Claude API-ключ"),
        true
    );

    // --- Обновление ---
    m_registry.registerCommand(
        {QStringLiteral("обновление"), QStringLiteral("обнови"),
         QStringLiteral("update"), QStringLiteral("версия"), QStringLiteral("version")},
        [this](const QString& s) { return cmdCheckUpdate(s); },
        QStringLiteral("обновление — проверить обновления")
    );

    // --- Индексатор проекта ---
    m_registry.registerCommand(
        {QStringLiteral("индекс "), QStringLiteral("index "),
         QStringLiteral("проект "), QStringLiteral("project ")},
        [this](const QString& s) { return cmdIndexProject(s); },
        QStringLiteral("индекс <путь> — индексировать C++ проект"),
        true
    );

    m_registry.registerCommand(
        {QStringLiteral("найди символ "), QStringLiteral("find "),
         QStringLiteral("символ "), QStringLiteral("symbol ")},
        [this](const QString& s) { return cmdFindSymbol(s); },
        QStringLiteral("найди символ <имя> — найти класс/функцию в проекте"),
        true
    );

    m_registry.registerCommand(
        {QStringLiteral("карта"), QStringLiteral("map"),
         QStringLiteral("структура"), QStringLiteral("structure")},
        [this](const QString& s) { return cmdProjectMap(s); },
        QStringLiteral("карта — карта проекта (классы, функции)")
    );

    m_registry.registerCommand(
        {QStringLiteral("grep "), QStringLiteral("поиск ")},
        [this](const QString& s) { return cmdGrep(s); },
        QStringLiteral("grep <текст> — поиск текста в файлах проекта"),
        true
    );

    // --- Память ---
    m_registry.registerCommand(
        {QStringLiteral("запомни "), QStringLiteral("remember ")},
        [this](const QString& s) { return cmdRememberFact(s); },
        QStringLiteral("запомни <ключ>=<значение> — запомнить факт"),
        true
    );

    m_registry.registerCommand(
        {QStringLiteral("вспомни "), QStringLiteral("recall ")},
        [this](const QString& s) { return cmdRecallFact(s); },
        QStringLiteral("вспомни <ключ> — вспомнить факт"),
        true
    );

    m_registry.registerCommand(
        {QStringLiteral("память"), QStringLiteral("memory")},
        [this](const QString& s) { return cmdShowMemory(s); },
        QStringLiteral("память — показать сохранённые факты")
    );

    m_registry.registerCommand(
        {QStringLiteral("статистика"), QStringLiteral("stats")},
        [this](const QString& s) { return cmdShowStats(s); },
        QStringLiteral("статистика — статистика использования")
    );

    // --- Приветствие ---
    m_registry.registerCommand(
        {QStringLiteral("привет"), QStringLiteral("здравств"),
         QStringLiteral("hello"), QStringLiteral("hi")},
        [this](const QString& s) { return cmdGreeting(s); },
        QStringLiteral("привет — приветствие")
    );

    // --- Время / Дата ---
    m_registry.registerCommand(
        {QStringLiteral("время"), QStringLiteral("time"), QStringLiteral("час")},
        [this](const QString& s) { return cmdTime(s); },
        QStringLiteral("время — текущее время")
    );

    m_registry.registerCommand(
        {QStringLiteral("дата"), QStringLiteral("date"), QStringLiteral("день")},
        [this](const QString& s) { return cmdDate(s); },
        QStringLiteral("дата — текущая дата")
    );

    m_registry.registerCommand(
        {QStringLiteral("кто я"), QStringLiteral("имя"), QStringLiteral("username")},
        [this](const QString& s) { return cmdUserName(s); },
        QStringLiteral("кто я — имя пользователя")
    );

    // --- Приложения ---
    m_registry.registerCommand(
        {QStringLiteral("блокнот"), QStringLiteral("notepad")},
        [this](const QString&) { return cmdLaunchApp(QStringLiteral("notepad.exe")); },
        QStringLiteral("блокнот — открыть Notepad")
    );
    m_registry.registerCommand(
        {QStringLiteral("калькулятор"), QStringLiteral("calc")},
        [this](const QString&) { return cmdLaunchApp(QStringLiteral("calc.exe")); },
        QStringLiteral("калькулятор — открыть калькулятор")
    );
    m_registry.registerCommand(
        {QStringLiteral("проводник"), QStringLiteral("explorer")},
        [this](const QString&) { return cmdLaunchApp(QStringLiteral("explorer.exe")); },
        QStringLiteral("проводник — открыть Explorer")
    );
    m_registry.registerCommand(
        {QStringLiteral("диспетчер"), QStringLiteral("taskmgr")},
        [this](const QString&) { return cmdLaunchApp(QStringLiteral("taskmgr.exe")); },
        QStringLiteral("диспетчер — диспетчер задач")
    );
    m_registry.registerCommand(
        {QStringLiteral("настройки"), QStringLiteral("settings")},
        [this](const QString&) { return cmdLaunchApp(QStringLiteral("ms-settings:")); },
        QStringLiteral("настройки — параметры Windows")
    );
    m_registry.registerCommand(
        {QStringLiteral("браузер"), QStringLiteral("chrome"), QStringLiteral("browser")},
        [this](const QString& s) { return cmdBrowser(s); },
        QStringLiteral("браузер — открыть Google")
    );

    // --- Запуск произвольного приложения ---
    m_registry.registerCommand(
        {QStringLiteral("запусти "), QStringLiteral("открой "), QStringLiteral("launch ")},
        [this](const QString& s) {
            QString app = extractArg(s, {QStringLiteral("запусти "),
                                         QStringLiteral("открой "),
                                         QStringLiteral("launch ")});
            return cmdLaunchApp(app);
        },
        QStringLiteral("запусти <программа> — запуск приложения"),
        true
    );

    // --- Поиск ---
    m_registry.registerCommand(
        {QStringLiteral("найди "), QStringLiteral("search "), QStringLiteral("гугл")},
        [this](const QString& s) {
            QString q = extractArg(s, {QStringLiteral("найди "),
                                       QStringLiteral("search "),
                                       QStringLiteral("гугл ")});
            return cmdWebSearch(q);
        },
        QStringLiteral("найди <запрос> — поиск в Google"),
        true
    );

    // --- YouTube ---
    m_registry.registerCommand(
        {QStringLiteral("youtube"), QStringLiteral("ютуб")},
        [this](const QString& s) {
            QString q = extractArg(s, {QStringLiteral("youtube "),
                                       QStringLiteral("ютуб ")});
            return cmdYoutube(q);
        },
        QStringLiteral("youtube <запрос> — поиск на YouTube"),
        true
    );

    // --- Блокировка ---
    m_registry.registerCommand(
        {QStringLiteral("заблокируй"), QStringLiteral("lock")},
        [this](const QString& s) { return cmdLock(s); },
        QStringLiteral("заблокируй — блокировка экрана")
    );

    // --- Виртуальная клавиатура ---
    m_registry.registerCommand(
        {QStringLiteral("напечатай "), QStringLiteral("type ")},
        [this](const QString& s) { return cmdTypeText(s); },
        QStringLiteral("напечатай <текст> — напечатать в активном окне"),
        true
    );

    m_registry.registerCommand(
        {QStringLiteral("нажми "), QStringLiteral("press ")},
        [this](const QString& s) { return cmdPressKey(s); },
        QStringLiteral("нажми <клавиша> — нажать клавишу"),
        true
    );

    m_registry.registerCommand(
        {QStringLiteral("комбо "), QStringLiteral("combo ")},
        [this](const QString& s) { return cmdCombo(s); },
        QStringLiteral("комбо <клавиши> — комбинация клавиш"),
        true
    );

    // --- Помощь (последним) ---
    m_registry.registerCommand(
        {QStringLiteral("помощь"), QStringLiteral("help"), QStringLiteral("команд")},
        [this](const QString&) { return cmdHelp(QString()); },
        QStringLiteral("помощь — список команд")
    );
}

// ============================================================
// Утилиты
// ============================================================

QString Jarvis::extractArg(const QString& input, const QStringList& prefixes)
{
    const QString trimmed = input.trimmed();
    const QString lower   = trimmed.toLower();

    for (const auto& prefix : prefixes) {
        if (lower.startsWith(prefix)) {
            return trimmed.mid(prefix.length()).trimmed();
        }
    }
    return trimmed;
}

WORD Jarvis::parseVirtualKey(const QString& name)
{
    static const QMap<QString, WORD> keyMap = {
        {QStringLiteral("enter"),     VK_RETURN},
        {QStringLiteral("tab"),       VK_TAB},
        {QStringLiteral("escape"),    VK_ESCAPE},
        {QStringLiteral("esc"),       VK_ESCAPE},
        {QStringLiteral("space"),     VK_SPACE},
        {QStringLiteral("пробел"),    VK_SPACE},
        {QStringLiteral("backspace"), VK_BACK},
        {QStringLiteral("delete"),    VK_DELETE},
        {QStringLiteral("up"),        VK_UP},
        {QStringLiteral("down"),      VK_DOWN},
        {QStringLiteral("left"),      VK_LEFT},
        {QStringLiteral("right"),     VK_RIGHT},
        {QStringLiteral("home"),      VK_HOME},
        {QStringLiteral("end"),       VK_END},
        {QStringLiteral("pageup"),    VK_PRIOR},
        {QStringLiteral("pagedown"),  VK_NEXT},
        {QStringLiteral("insert"),    VK_INSERT},
        {QStringLiteral("ctrl"),      VK_CONTROL},
        {QStringLiteral("alt"),       VK_MENU},
        {QStringLiteral("shift"),     VK_SHIFT},
        {QStringLiteral("win"),       VK_LWIN},
        {QStringLiteral("f1"),  VK_F1},  {QStringLiteral("f2"),  VK_F2},
        {QStringLiteral("f3"),  VK_F3},  {QStringLiteral("f4"),  VK_F4},
        {QStringLiteral("f5"),  VK_F5},  {QStringLiteral("f6"),  VK_F6},
        {QStringLiteral("f7"),  VK_F7},  {QStringLiteral("f8"),  VK_F8},
        {QStringLiteral("f9"),  VK_F9},  {QStringLiteral("f10"), VK_F10},
        {QStringLiteral("f11"), VK_F11}, {QStringLiteral("f12"), VK_F12},
    };

    QString lower = name.trimmed().toLower();
    auto it = keyMap.find(lower);
    if (it != keyMap.end()) return it.value();

    if (lower.length() == 1) {
        QChar ch = lower.at(0).toUpper();
        if (ch >= QChar('A') && ch <= QChar('Z'))
            return static_cast<WORD>(ch.unicode());
    }
    return 0;
}

// ============================================================
// Детектор "коддинг-интента"
// ============================================================

bool Jarvis::isCodingIntent(const QString& input)
{
    const QString lower = input.toLower();

    // Глаголы: сделай/создай/добавь/исправь/оптимизируй/рефакторинг/...
    static const QStringList verbs = {
        QStringLiteral("сделай"),     QStringLiteral("создай"),
        QStringLiteral("напиши"),     QStringLiteral("добавь"),
        QStringLiteral("исправь"),    QStringLiteral("пофикс"),
        QStringLiteral("фикс"),       QStringLiteral("fix"),
        QStringLiteral("оптимизир"),  QStringLiteral("optimize"),
        QStringLiteral("рефактор"),   QStringLiteral("refactor"),
        QStringLiteral("перепиши"),   QStringLiteral("rewrite"),
        QStringLiteral("реализуй"),   QStringLiteral("implement"),
        QStringLiteral("интегрир"),   QStringLiteral("integrate"),
        QStringLiteral("подключи"),   QStringLiteral("удали из"),
        QStringLiteral("убери"),      QStringLiteral("remove"),
        QStringLiteral("замени"),     QStringLiteral("replace"),
        QStringLiteral("улучши"),     QStringLiteral("improve"),
        QStringLiteral("доработай"),  QStringLiteral("доделай"),
        QStringLiteral("почини"),     QStringLiteral("объясни код"),
        QStringLiteral("ревью"),      QStringLiteral("review"),
        QStringLiteral("проверь код"),QStringLiteral("проверь файл"),
        QStringLiteral("migrate"),    QStringLiteral("port "),
        QStringLiteral("add "),       QStringLiteral("create "),
        QStringLiteral("make "),      QStringLiteral("build "),
    };
    for (const auto& v : verbs) {
        if (lower.contains(v)) return true;
    }

    // Сущности: функция/класс/метод/файл/модуль/компонент/баг/ошибку
    static const QStringList entities = {
        QStringLiteral("функци"),     QStringLiteral("function"),
        QStringLiteral("метод"),      QStringLiteral("method"),
        QStringLiteral("класс"),      QStringLiteral("class "),
        QStringLiteral("струк"),      QStringLiteral("struct"),
        QStringLiteral("модул"),      QStringLiteral("module"),
        QStringLiteral("компонент"),  QStringLiteral("component"),
        QStringLiteral("плагин"),     QStringLiteral("plugin"),
        QStringLiteral(" баг"),       QStringLiteral(" bug"),
        QStringLiteral("ошибк"),      QStringLiteral(" error"),
        QStringLiteral(".cpp"),       QStringLiteral(".h"),
        QStringLiteral(".hpp"),       QStringLiteral(".cxx"),
        QStringLiteral(".py"),        QStringLiteral(".js"),
        QStringLiteral(".ts"),        QStringLiteral("cmake"),
    };
    for (const auto& e : entities) {
        if (lower.contains(e)) return true;
    }

    return false;
}

// ============================================================
// Извлечение ключевых слов (без предлогов/мусора)
// ============================================================

QStringList Jarvis::extractKeywords(const QString& input)
{
    // Стоп-слова (русские + английские + глаголы намерений)
    static const QSet<QString> stopWords = {
        // предлоги/союзы/частицы RU
        QStringLiteral("и"),   QStringLiteral("в"),  QStringLiteral("на"), QStringLiteral("с"),
        QStringLiteral("из"),  QStringLiteral("к"),  QStringLiteral("по"), QStringLiteral("у"),
        QStringLiteral("от"),  QStringLiteral("за"), QStringLiteral("для"),QStringLiteral("без"),
        QStringLiteral("что"), QStringLiteral("как"),QStringLiteral("это"),QStringLiteral("там"),
        QStringLiteral("где"), QStringLiteral("тут"),QStringLiteral("же"), QStringLiteral("бы"),
        QStringLiteral("не"),  QStringLiteral("но"), QStringLiteral("ли"), QStringLiteral("ни"),
        QStringLiteral("мне"), QStringLiteral("мой"),QStringLiteral("его"),QStringLiteral("ее"),
        QStringLiteral("её"),  QStringLiteral("они"),QStringLiteral("ты"), QStringLiteral("я"),
        QStringLiteral("мы"),  QStringLiteral("вы"),
        // глаголы действий — НЕ названия модулей
        QStringLiteral("сделай"),   QStringLiteral("создай"),  QStringLiteral("напиши"),
        QStringLiteral("добавь"),   QStringLiteral("исправь"), QStringLiteral("оптимизируй"),
        QStringLiteral("рефактори"),QStringLiteral("перепиши"),QStringLiteral("реализуй"),
        QStringLiteral("улучши"),   QStringLiteral("замени"),  QStringLiteral("убери"),
        QStringLiteral("почини"),   QStringLiteral("доработай"),QStringLiteral("проверь"),
        QStringLiteral("объясни"),  QStringLiteral("покажи"),  QStringLiteral("дай"),
        QStringLiteral("хочу"),     QStringLiteral("надо"),    QStringLiteral("нужно"),
        QStringLiteral("нужен"),    QStringLiteral("нужна"),
        // английские
        QStringLiteral("the"), QStringLiteral("a"),   QStringLiteral("an"),
        QStringLiteral("to"),  QStringLiteral("in"),  QStringLiteral("on"),
        QStringLiteral("at"),  QStringLiteral("for"), QStringLiteral("of"),
        QStringLiteral("and"), QStringLiteral("or"),  QStringLiteral("but"),
        QStringLiteral("with"),QStringLiteral("from"),QStringLiteral("is"),
        QStringLiteral("are"), QStringLiteral("was"), QStringLiteral("be"),
        QStringLiteral("make"),QStringLiteral("create"),QStringLiteral("add"),
        QStringLiteral("fix"), QStringLiteral("improve"),QStringLiteral("refactor"),
        QStringLiteral("i"),   QStringLiteral("you"), QStringLiteral("my"),
        // слова-сущности без информации
        QStringLiteral("функцию"),  QStringLiteral("функция"), QStringLiteral("функции"),
        QStringLiteral("метод"),    QStringLiteral("методы"),  QStringLiteral("класс"),
        QStringLiteral("файл"),     QStringLiteral("файлы"),   QStringLiteral("код"),
        QStringLiteral("коде"),     QStringLiteral("кода"),    QStringLiteral("проект"),
        QStringLiteral("проекта"),  QStringLiteral("function"),QStringLiteral("method"),
        QStringLiteral("class"),    QStringLiteral("file"),    QStringLiteral("code"),
    };

    static const QRegularExpression splitter(QStringLiteral("[\\s,.:;!?\\-\"'()\\[\\]{}/\\\\]+"));
    QStringList raw = input.split(splitter, Qt::SkipEmptyParts);
    QStringList result;
    QSet<QString> seen;

    for (QString w : raw) {
        w = w.trimmed().toLower();
        if (w.length() < 3) continue;
        if (stopWords.contains(w)) continue;
        if (seen.contains(w)) continue;
        seen.insert(w);
        result.append(w);
    }
    return result;
}

// ============================================================
// Построение контекста из проекта для Claude
// ============================================================

QString Jarvis::buildProjectContext(const QString& userQuery) const
{
    if (m_indexer->fileCount() == 0) return QString();

    const QStringList keywords = extractKeywords(userQuery);
    const bool coding = isCodingIntent(userQuery);

    // Лимиты, чтобы не уронить API
    constexpr int MAX_FILES          = 3;      // сколько целых файлов прицепить
    constexpr int MAX_FILE_CHARS     = 14000;  // бюджет на один файл
    constexpr int MAX_TOTAL_CHARS    = 32000;  // суммарный бюджет на контекст
    constexpr int MAX_SYMBOL_MATCHES = 8;      // сколько сниппетов символов
    constexpr int MAX_GREP_HITS      = 10;     // сколько grep-совпадений

    // === Собираем кандидатов: файлы ===
    QStringList pickedFiles;       // относительные пути
    QSet<QString> pickedFilesSet;

    auto addFile = [&](const QString& relPath) {
        if (pickedFiles.size() >= MAX_FILES) return;
        if (pickedFilesSet.contains(relPath)) return;
        pickedFilesSet.insert(relPath);
        pickedFiles.append(relPath);
    };

    // 1. Поиск по имени файла напрямую (virtual_keyboard, session_memory и т.п.)
    for (const auto& kw : keywords) {
        if (pickedFiles.size() >= MAX_FILES) break;
        auto files = m_indexer->findFile(kw);
        for (const auto& f : files) {
            addFile(f.relativePath.isEmpty() ? f.filePath : f.relativePath);
            if (pickedFiles.size() >= MAX_FILES) break;
        }
    }

    // 2. Поиск по символам (если по именам не нашли или осталось место)
    QVector<CodeSymbol> symbolHits;
    for (const auto& kw : keywords) {
        if (symbolHits.size() >= MAX_SYMBOL_MATCHES) break;
        auto found = m_indexer->findSymbol(kw);
        for (const auto& sym : found) {
            if (symbolHits.size() >= MAX_SYMBOL_MATCHES) break;
            bool duplicate = false;
            for (const auto& existing : symbolHits) {
                if (existing.name == sym.name && existing.filePath == sym.filePath) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) symbolHits.append(sym);
        }
    }

    // Если это кодинг-интент и у нас всё ещё нет файлов,
    // но есть символы — добавим файлы из символов.
    if (coding && pickedFiles.isEmpty() && !symbolHits.isEmpty()) {
        for (const auto& sym : symbolHits) {
            addFile(sym.filePath);
            if (pickedFiles.size() >= MAX_FILES) break;
        }
    }

    // 3. Если всё ещё пусто и это кодинг-интент — делаем grep по ключевым словам.
    QVector<ProjectIndexer::GrepResult> grepHits;
    if (coding && pickedFiles.isEmpty() && symbolHits.isEmpty()) {
        for (const auto& kw : keywords) {
            auto r = m_indexer->grep(kw, MAX_GREP_HITS);
            for (const auto& hit : r) {
                grepHits.append(hit);
                if (grepHits.size() >= MAX_GREP_HITS) break;
            }
            if (grepHits.size() >= MAX_GREP_HITS) break;
        }
    }

    // === Сборка блока контекста ===
    QString context;
    context.reserve(8192);
    int budget = MAX_TOTAL_CHARS;

    // Заголовок + инструкция Клоду
    context += QStringLiteral("\n\n--- Контекст из проекта (автоматически от JARVIS) ---\n");
    context += QStringLiteral("# Root: ") + m_indexer->projectRoot() + QStringLiteral("\n");
    if (coding) {
        context += QStringLiteral(
            "# Режим: КОДИНГ. Используй приложенные файлы как авторитетный источник. "
            "Отвечай сразу блоками [FILE:...] или [DIFF:...]. "
            "Если нужен ещё какой-то файл — назови его и жди следующего сообщения.\n");
    } else {
        context += QStringLiteral(
            "# Режим: ЧТЕНИЕ. Отвечай на вопрос пользователя, опираясь на эти фрагменты.\n");
    }

    auto appendAndTrim = [&](const QString& chunk) -> bool {
        if (chunk.size() >= budget) {
            context += chunk.left(budget);
            context += QStringLiteral("\n... (обрезано по лимиту) ...\n");
            budget = 0;
            return false;
        }
        context += chunk;
        budget -= chunk.size();
        return true;
    };

    // ---- Файлы целиком (для кодинг-запросов) ----
    for (const QString& rel : pickedFiles) {
        if (budget <= 0) break;

        // Берём весь файл: getFileLines(path, 1, -1) — но API этого не даёт, так что читаем сами
        // через getFileLines большим диапазоном.
        const QString content = m_indexer->getFileLines(rel, 1, 100000);
        if (content.isEmpty()) continue;

        QString trimmed = content;
        if (trimmed.size() > MAX_FILE_CHARS) {
            trimmed = trimmed.left(MAX_FILE_CHARS)
                    + QStringLiteral("\n// ... (файл обрезан, ")
                    + QString::number(content.size() - MAX_FILE_CHARS)
                    + QStringLiteral(" символов скрыто) ...\n");
        }

        QString header = QStringLiteral("\n### FILE: ") + rel + QStringLiteral("\n```\n");
        QString footer = QStringLiteral("\n```\n");
        if (!appendAndTrim(header + trimmed + footer)) break;
    }

    // ---- Отдельные символы (если ещё есть бюджет) ----
    if (budget > 1000 && !symbolHits.isEmpty()) {
        QString hdr = QStringLiteral("\n### Найденные символы:\n");
        appendAndTrim(hdr);
        int written = 0;
        for (const auto& sym : symbolHits) {
            if (budget <= 500) break;
            // Не дублируем сниппет для уже приложенных файлов
            if (pickedFilesSet.contains(sym.filePath)) continue;

            const QString snippet = m_indexer->getCodeSnippet(sym, 5);
            if (snippet.isEmpty()) continue;

            QString block = QStringLiteral("// ") + sym.filePath
                          + QStringLiteral(" — ") + sym.kindToString()
                          + QStringLiteral(" ") + sym.name + QStringLiteral("\n```\n")
                          + snippet + QStringLiteral("\n```\n");
            if (!appendAndTrim(block)) break;
            ++written;
            if (written >= 6) break;
        }
    }

    // ---- Grep-совпадения (последний fallback) ----
    if (budget > 500 && !grepHits.isEmpty()) {
        QString block = QStringLiteral("\n### Совпадения grep:\n");
        for (const auto& h : grepHits) {
            block += h.filePath + QStringLiteral(":") + QString::number(h.line)
                   + QStringLiteral("  ") + h.lineText.left(200) + QStringLiteral("\n");
        }
        appendAndTrim(block);
    }

    // Если реально ничего не нашли — возвращаем намёк вместо пустоты,
    // чтобы Claude не начал спрашивать код у пользователя.
    if (pickedFiles.isEmpty() && symbolHits.isEmpty() && grepHits.isEmpty()) {
        context += QStringLiteral(
            "\n(Автопоиск не нашёл прямых совпадений в индексе. "
            "Опирайся на карту проекта в system prompt; если нужен конкретный файл — "
            "назови его по имени, JARVIS подгрузит на следующем шаге.)\n");
    }

    context += QStringLiteral("--- Конец контекста ---\n");
    return context;
}

// ============================================================
// TTS — SAPI в отдельном QThread
// ============================================================

void Jarvis::speakAsync(const QString& text)
{
    if (text.isEmpty()) return;
    if (!m_ttsMutex.tryLock()) return;

    m_speaking.store(true);
    emit speakingChanged(true);

    QString copy = text;

    QThread* thread = QThread::create([this, copy]() {
        ComInitializer threadCom;
        if (!threadCom.ok()) {
            m_ttsMutex.unlock();
            return;
        }

        ISpVoice* voice = nullptr;
        HRESULT hr = CoCreateInstance(
            CLSID_SpVoice, nullptr, CLSCTX_ALL,
            IID_ISpVoice, reinterpret_cast<void**>(&voice)
        );

        if (SUCCEEDED(hr) && voice) {
            voice->SetRate(1);
            voice->SetVolume(100);
            std::wstring wtext = copy.toStdWString();
            voice->Speak(wtext.c_str(), SPF_DEFAULT, nullptr);
            voice->Release();
        }

        m_speaking.store(false);
        m_ttsMutex.unlock();

        QMetaObject::invokeMethod(this, [this]() {
            emit speakingChanged(false);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

// ============================================================
// Обработка команд (гибридный режим)
// ============================================================

QString Jarvis::processCommand(const QString& input)
{
    QString s = input.trimmed();
    if (s.isEmpty()) {
        return QStringLiteral("Введите команду или напишите «помощь».");
    }

    m_memory->addMessage(QStringLiteral("user"), s);

    // 1. Пробуем локальные команды
    auto result = m_registry.tryExecute(s);
    if (result.matched) {
        m_memory->addMessage(QStringLiteral("assistant"), result.response);
        m_memory->updateContext(s, result.response);

        m_predictor->recordSequence(s);
        auto suggestion = m_predictor->suggestAfter(s);
        if (suggestion.isValid() && suggestion.confidence >= 0.5) {
            emit suggestionAvailable(suggestion.description, suggestion.action);
        }

        return result.response;
    }

    // 2. Если есть API-ключ — отправляем в Claude API
    if (m_claudeApi->shouldUseApi(s)) {
        // Убедимся что code_actions знает корень проекта (на случай первого запуска)
        if (!m_indexer->projectRoot().isEmpty()) {
            m_codeActions->setProjectRoot(m_indexer->projectRoot());
        }

        // Умное обогащение запроса контекстом проекта
        const QString projectContext = buildProjectContext(s);
        const QString enrichedMessage = projectContext.isEmpty()
                                       ? s
                                       : (s + projectContext);

        m_claudeApi->sendMessage(enrichedMessage,
                                 [this, s](bool success, const QString& response) {
            if (success) {
                // Обработка файловых операций из ответа
                QString fileReport     = m_codeActions->processResponse(response);
                QString displayResponse = m_codeActions->cleanResponseForDisplay(response);

                m_memory->addMessage(QStringLiteral("assistant"), displayResponse);
                m_memory->updateContext(s, displayResponse);
                handleClaudeResponse(response);
                m_predictor->recordSequence(s);

                QString fullResponse = displayResponse;
                if (!fileReport.isEmpty()) {
                    fullResponse += QStringLiteral("\n\n") + fileReport;
                }
                emit asyncResponseReady(fullResponse);
            } else {
                emit asyncResponseError(response);
            }
        });

        return QString();
    }

    // 3. Fallback
    QString fallback = QStringLiteral(
        "Не понял команду. Напишите «помощь» для списка команд.\n"
        "Для свободного диалога установите API-ключ: apikey <ваш-ключ>"
    );
    m_memory->addMessage(QStringLiteral("assistant"), fallback);
    return fallback;
}

// ============================================================
// Обработка ответа Claude (парсинг [CMD:...])
// ============================================================

void Jarvis::handleClaudeResponse(const QString& response)
{
    static const QRegularExpression cmdPattern(
        QStringLiteral(R"(\[CMD:(.+?)\])"));

    QRegularExpressionMatchIterator it = cmdPattern.globalMatch(response);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString cmd = match.captured(1).trimmed();
        m_registry.tryExecute(cmd);
    }
}

// ============================================================
// Команды: API и память
// ============================================================

QString Jarvis::cmdSetApiKey(const QString& input)
{
    QString key = extractArg(input, {QStringLiteral("apikey "),
                                      QStringLiteral("ключ ")});
    if (key.isEmpty()) {
        if (m_claudeApi->hasApiKey()) {
            return QStringLiteral("API-ключ установлен. Для замены: apikey <новый-ключ>");
        }
        return QStringLiteral("Укажите ключ: apikey <ваш-anthropic-api-key>");
    }

    m_claudeApi->setApiKey(key);
    return QStringLiteral("API-ключ сохранён. Claude API подключён.");
}

QString Jarvis::cmdRememberFact(const QString& input)
{
    QString arg = extractArg(input, {QStringLiteral("запомни "),
                                      QStringLiteral("remember ")});
    int eqPos = arg.indexOf(QChar('='));
    if (eqPos <= 0) {
        return QStringLiteral("Формат: запомни ключ=значение\n"
                              "Пример: запомни проект=JARVIS на Unreal Engine");
    }

    QString key   = arg.left(eqPos).trimmed();
    QString value = arg.mid(eqPos + 1).trimmed();

    m_memory->rememberFact(key, value);
    return QStringLiteral("Запомнил: ") + key + QStringLiteral(" = ") + value;
}

QString Jarvis::cmdRecallFact(const QString& input)
{
    QString key = extractArg(input, {QStringLiteral("вспомни "),
                                      QStringLiteral("recall ")});
    if (key.isEmpty()) {
        return QStringLiteral("Укажите что вспомнить: вспомни <ключ>");
    }

    QString value = m_memory->recallFact(key);
    if (value.isEmpty()) {
        return QStringLiteral("Не помню ничего о «") + key + QStringLiteral("».");
    }
    return key + QStringLiteral(": ") + value;
}

QString Jarvis::cmdShowMemory(const QString&)
{
    QJsonObject facts = m_memory->allFacts();
    if (facts.isEmpty()) {
        return QStringLiteral("Память пуста. Используйте: запомни ключ=значение");
    }

    QString text = QStringLiteral("Сохранённые факты:\n");
    for (auto it = facts.begin(); it != facts.end(); ++it) {
        text += QStringLiteral("• ") + it.key() + QStringLiteral(": ")
              + it.value().toString() + QStringLiteral("\n");
    }

    text += QStringLiteral("\nСессий в памяти: ")
          + QString::number(m_memory->pastSessionSummaries().size());
    text += QStringLiteral("\nСообщений за сессию: ")
          + QString::number(m_memory->messageCount());

    return text.trimmed();
}

QString Jarvis::cmdShowStats(const QString&)
{
    QJsonObject stats = m_memory->commandStats();
    if (stats.isEmpty()) {
        return QStringLiteral("Статистика пуста — пока недостаточно данных.");
    }

    QVector<QPair<QString, int>> sorted;
    for (auto it = stats.begin(); it != stats.end(); ++it) {
        sorted.append({it.key(), it.value().toInt()});
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    QString text = QStringLiteral("Статистика команд:\n");
    int shown = 0;
    for (const auto& [cmd, count] : sorted) {
        text += QStringLiteral("• ") + cmd + QStringLiteral(": ")
              + QString::number(count) + QStringLiteral(" раз\n");
        if (++shown >= 15) break;
    }

    text += QStringLiteral("\nВсего за сессию: ")
          + QString::number(m_memory->taskContext().commandCount)
          + QStringLiteral(" команд");

    auto suggestions = m_predictor->suggest(3);
    if (!suggestions.isEmpty()) {
        text += QStringLiteral("\n\nПредложения:");
        for (const auto& s : suggestions) {
            text += QStringLiteral("\n  → ") + s.description
                  + QStringLiteral(" (") + QString::number(int(s.confidence * 100))
                  + QStringLiteral("%)");
        }
    }

    return text.trimmed();
}

// ============================================================
// Обновление
// ============================================================

QString Jarvis::cmdCheckUpdate(const QString&)
{
    m_updater->checkForUpdates(false);
    return QStringLiteral("Текущая версия: ")
           + QCoreApplication::applicationVersion()
           + QStringLiteral("\nПроверяю обновления...");
}

// ============================================================
// Индексатор проекта
// ============================================================

QString Jarvis::cmdIndexProject(const QString& input)
{
    QString path = extractArg(input, {QStringLiteral("индекс "),
                                       QStringLiteral("index "),
                                       QStringLiteral("проект "),
                                       QStringLiteral("project ")});
    if (path.isEmpty()) {
        if (m_indexer->projectRoot().isEmpty()) {
            return QStringLiteral("Укажите путь к проекту:\n"
                                  "индекс C:\\Projects\\MyGame\n\n"
                                  "После индексации доступны команды:\n"
                                  "• карта — структура проекта\n"
                                  "• найди символ <имя> — поиск функции/класса\n"
                                  "• grep <текст> — поиск в файлах");
        }
        // Переиндексация текущего проекта
        m_indexer->indexProject();
        syncProjectInfoToMemory();
        return QStringLiteral("Переиндексирую ") + m_indexer->projectRoot()
             + QStringLiteral("...\nФайлов: ") + QString::number(m_indexer->fileCount())
             + QStringLiteral(", Символов: ") + QString::number(m_indexer->symbolCount());
    }

    // Нормализуем путь
    path = path.replace(QChar('/'), QChar('\\'));

    if (!QDir(path).exists()) {
        return QStringLiteral("Папка не найдена: ") + path;
    }

    m_indexer->setProjectRoot(path);
    m_indexer->indexProject();
    m_indexer->enableFileWatcher(true);
    syncProjectInfoToMemory();

    return QStringLiteral("Проект проиндексирован: ") + path
         + QStringLiteral("\nФайлов: ") + QString::number(m_indexer->fileCount())
         + QStringLiteral(", Символов: ") + QString::number(m_indexer->symbolCount())
         + QStringLiteral("\n\nСлежение за изменениями включено.")
         + QStringLiteral("\nТеперь при вопросах к Claude я автоматически "
                          "найду нужный код и отправлю только его.");
}

QString Jarvis::cmdFindSymbol(const QString& input)
{
    QString name = extractArg(input, {QStringLiteral("найди символ "),
                                       QStringLiteral("find "),
                                       QStringLiteral("символ "),
                                       QStringLiteral("symbol ")});
    if (name.isEmpty()) {
        return QStringLiteral("Укажите имя: найди символ SpawnEnemy");
    }

    if (m_indexer->fileCount() == 0) {
        return QStringLiteral("Проект не проиндексирован. Используйте: индекс <путь>");
    }

    auto results = m_indexer->findSymbol(name);
    if (results.isEmpty()) {
        return QStringLiteral("Символ «") + name + QStringLiteral("» не найден.");
    }

    QString text = QStringLiteral("Найдено ") + QString::number(results.size())
                 + QStringLiteral(" результатов:\n\n");

    int shown = 0;
    for (const auto& sym : results) {
        text += QStringLiteral("• ") + sym.kindToString() + QStringLiteral(" ");
        if (!sym.parentClass.isEmpty()) {
            text += sym.parentClass + QStringLiteral("::");
        }
        text += sym.name;
        text += QStringLiteral("\n  Файл: ") + m_indexer->projectRoot()
              + QStringLiteral("/") + sym.filePath;
        text += QStringLiteral(", строка ") + QString::number(sym.lineStart);
        if (!sym.brief.isEmpty()) {
            text += QStringLiteral("\n  ") + sym.brief;
        }
        text += QStringLiteral("\n");

        if (++shown >= 10) {
            text += QStringLiteral("\n... и ещё ")
                  + QString::number(results.size() - 10) + QStringLiteral(" результатов");
            break;
        }
    }

    return text.trimmed();
}

QString Jarvis::cmdProjectMap(const QString&)
{
    if (m_indexer->fileCount() == 0) {
        return QStringLiteral("Проект не проиндексирован. Используйте: индекс <путь>");
    }

    QString map = m_indexer->projectMap();

    // Ограничиваем длину вывода
    if (map.length() > 3000) {
        map = map.left(3000) + QStringLiteral("\n\n... (обрезано, всего ")
            + QString::number(m_indexer->fileCount()) + QStringLiteral(" файлов, ")
            + QString::number(m_indexer->symbolCount()) + QStringLiteral(" символов)");
    }

    return map;
}

QString Jarvis::cmdGrep(const QString& input)
{
    QString pattern = extractArg(input, {QStringLiteral("grep "),
                                          QStringLiteral("поиск ")});
    if (pattern.isEmpty()) {
        return QStringLiteral("Укажите текст для поиска: grep SpawnEnemy");
    }

    if (m_indexer->fileCount() == 0) {
        return QStringLiteral("Проект не проиндексирован. Используйте: индекс <путь>");
    }

    auto results = m_indexer->grep(pattern, 20);
    if (results.isEmpty()) {
        return QStringLiteral("Не найдено: «") + pattern + QStringLiteral("»");
    }

    QString text = QStringLiteral("Найдено ") + QString::number(results.size())
                 + QStringLiteral(" совпадений:\n\n");

    for (const auto& r : results) {
        text += r.filePath + QStringLiteral(":") + QString::number(r.line)
              + QStringLiteral("  ") + r.lineText + QStringLiteral("\n");
    }

    return text.trimmed();
}

// ============================================================
// Остальные команды
// ============================================================

QString Jarvis::cmdTime(const QString&)
{
    return QStringLiteral("Сейчас ") + QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
}

QString Jarvis::cmdDate(const QString&)
{
    QDate d = QDate::currentDate();
    static const char* months[] = {
        "", "января", "февраля", "марта", "апреля", "мая", "июня",
        "июля", "августа", "сентября", "октября", "ноября", "декабря"
    };
    static const char* days[] = {
        "", "понедельник", "вторник", "среда", "четверг",
        "пятница", "суббота", "воскресенье"
    };
    return QString::fromUtf8("%1, %2 %3 %4")
        .arg(QString::fromUtf8(days[d.dayOfWeek()]))
        .arg(d.day())
        .arg(QString::fromUtf8(months[d.month()]))
        .arg(d.year());
}

QString Jarvis::cmdGreeting(const QString&)
{
    int hour = QTime::currentTime().hour();
    QString g;
    if      (hour < 6)  g = QStringLiteral("Доброй ночи");
    else if (hour < 12) g = QStringLiteral("Доброе утро");
    else if (hour < 18) g = QStringLiteral("Добрый день");
    else                g = QStringLiteral("Добрый вечер");

    QString name = cmdUserName(QString()).replace(QStringLiteral("Вы — "), QString());
    return g + QStringLiteral(", ") + name + QStringLiteral("! Чем могу помочь?");
}

QString Jarvis::cmdUserName(const QString&)
{
    wchar_t buf[UNLEN + 1];
    DWORD sz = UNLEN + 1;
    if (GetUserNameW(buf, &sz)) {
        return QStringLiteral("Вы — ") + QString::fromWCharArray(buf);
    }
    return QStringLiteral("Не удалось определить имя.");
}

QString Jarvis::cmdHelp(const QString&)
{
    QString help = m_registry.helpText();
    help += QStringLiteral("\n\n— Свободный диалог —\n"
                           "Любой вопрос → отправляется в Claude API\n"
                           "(требуется apikey)");
    if (m_indexer->fileCount() > 0) {
        help += QStringLiteral("\n\n— Проект «")
              + QFileInfo(m_indexer->projectRoot()).fileName()
              + QStringLiteral("» проиндексирован —\n")
              + QString::number(m_indexer->fileCount()) + QStringLiteral(" файлов, ")
              + QString::number(m_indexer->symbolCount()) + QStringLiteral(" символов");
    }
    return help;
}

QString Jarvis::cmdLaunchApp(const QString& app)
{
    if (app.isEmpty()) {
        return QStringLiteral("Укажите приложение для запуска.");
    }
    std::wstring wapp = app.toStdWString();
    HINSTANCE result = ShellExecuteW(
        nullptr, L"open", wapp.c_str(), nullptr, nullptr, SW_SHOWNORMAL
    );
    if (reinterpret_cast<intptr_t>(result) > 32) {
        return QStringLiteral("Запускаю: ") + app;
    }
    return QStringLiteral("Не удалось запустить: ") + app;
}

QString Jarvis::cmdWebSearch(const QString& query)
{
    if (query.isEmpty()) {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.google.com")));
        return QStringLiteral("Открываю Google.");
    }
    QString url = QStringLiteral("https://www.google.com/search?q=")
                  + QString::fromUtf8(QUrl::toPercentEncoding(query));
    QDesktopServices::openUrl(QUrl(url));
    return QStringLiteral("Ищу: ") + query;
}

QString Jarvis::cmdYoutube(const QString& query)
{
    if (query.isEmpty()) {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.youtube.com")));
        return QStringLiteral("Открываю YouTube.");
    }
    QString url = QStringLiteral("https://www.youtube.com/results?search_query=")
                  + QString::fromUtf8(QUrl::toPercentEncoding(query));
    QDesktopServices::openUrl(QUrl(url));
    return QStringLiteral("Ищу на YouTube: ") + query;
}

QString Jarvis::cmdLock(const QString&)
{
    LockWorkStation();
    return QStringLiteral("Блокирую экран.");
}

QString Jarvis::cmdBrowser(const QString&)
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.google.com")));
    return QStringLiteral("Открываю браузер.");
}

// ============================================================
// Виртуальная клавиатура
// ============================================================

QString Jarvis::cmdTypeText(const QString& input)
{
    QString text = extractArg(input, {QStringLiteral("напечатай "),
                                      QStringLiteral("type ")});
    if (text.isEmpty()) {
        return QStringLiteral("Укажите текст для набора.");
    }

    m_keyEmulator->pressCombo({VK_MENU, VK_TAB});
    QThread::msleep(300);
    m_keyEmulator->typeText(text, 30);
    return QStringLiteral("Печатаю: ") + text;
}

QString Jarvis::cmdPressKey(const QString& input)
{
    QString keyName = extractArg(input, {QStringLiteral("нажми "),
                                         QStringLiteral("press ")});
    if (keyName.isEmpty()) {
        return QStringLiteral("Укажите клавишу (enter, tab, escape...).");
    }

    WORD vk = parseVirtualKey(keyName);
    if (vk == 0) {
        return QStringLiteral("Неизвестная клавиша: ") + keyName;
    }

    m_keyEmulator->pressKey(vk);
    return QStringLiteral("Нажимаю: ") + keyName;
}

QString Jarvis::cmdCombo(const QString& input)
{
    QString comboStr = extractArg(input, {QStringLiteral("комбо "),
                                          QStringLiteral("combo ")});
    if (comboStr.isEmpty()) {
        return QStringLiteral("Укажите комбинацию (ctrl+c, alt+tab...).");
    }

    QStringList parts = comboStr.toLower().split(QStringLiteral("+"),
                                                  Qt::SkipEmptyParts);
    std::vector<WORD> keys;
    for (const auto& part : parts) {
        WORD vk = parseVirtualKey(part.trimmed());
        if (vk == 0) {
            return QStringLiteral("Неизвестная клавиша в комбинации: ") + part.trimmed();
        }
        keys.push_back(vk);
    }

    if (keys.empty()) {
        return QStringLiteral("Не удалось разобрать комбинацию.");
    }

    m_keyEmulator->pressCombo(
        std::initializer_list<WORD>(keys.data(), keys.data() + keys.size())
    );
    return QStringLiteral("Нажимаю комбинацию: ") + comboStr;
}
