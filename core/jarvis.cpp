// -------------------------------------------------------
// jarvis.cpp — Ядро J.A.R.V.I.S.: команды, TTS, мозги
// -------------------------------------------------------

#include "jarvis.h"
#include "virtual_keyboard.h"
#include "session_memory.h"
#include "claude_api.h"
#include "ollama_api.h"
#include "gemini_api.h"
#include "action_predictor.h"
#include "auto_updater.h"
#include "project_indexer.h"
#include "code_actions.h"
#include "attachments_manager.h"
#include "brain.h"
#include "pc_command_registry.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlDatabase>
#include "activity_tracker.h"
#include "user_profile_manager.h"
#include "training_processing_worker.h"
#include "system_manifest.h"
#include "task_manager_dialog.h"
#include "jarvis_paths.h"
// lang.h НЕ используем через IS_EN — в статической библиотеке gUiLanguage()
// хранится в отдельном экземпляре (MSVC ODR). Язык передаётся явно через
// m_uiEnglish, который MainWindow устанавливает через setUiLanguage().

#include <sapi.h>
#include <shellapi.h>
#include <QDateTime>
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
    m_memory       = new SessionMemory(this);
    m_claudeApi    = new ClaudeApi(m_memory, this);
    m_geminiApi    = new OllamaApi(this);   // Ollama — локальный LLM для быстрых ответов
    m_geminiBackup = new GeminiApi(m_memory, this); // Gemini — fallback если Ollama недоступна
    m_predictor    = new ActionPredictor(m_memory, this);
    m_keyEmulator  = new KeyEmulator(this);
    m_pcCommands   = new PcCommandRegistry(m_keyEmulator, this);
    m_indexer      = new ProjectIndexer(this);
    m_codeActions  = new CodeActions(this);
    m_attachments  = new AttachmentsManager(this);
    m_profile      = new UserProfile(this);
    m_activity     = new ActivityTracker(this);
    m_activity->start(15); // capture every 15 seconds

    // Background training data pipeline: voice_journal → training pairs → .jsonl
    m_trainingPipeline = new TrainingPipelineController(this);
    m_trainingPipeline->setUserId(m_currentUserId);
    m_trainingPipeline->setDatasetPath(JarvisPaths::subPath(QStringLiteral("training_export")));
    m_trainingPipeline->start(10);

    // Автообновление
    m_updater = new AutoUpdater(
        QStringLiteral(JARVIS_VERSION),
        QStringLiteral(JARVIS_GITHUB_USER),
        QStringLiteral(JARVIS_GITHUB_REPO),
        this
    );

    registerCommands();

    // Голосовое управление ПК (мышь/окна/система/медиа/браузер/макросы) —
    // регистрируется ПОСЛЕ registerCommands(), чтобы системные префиксы
    // ("напечатай ", "нажми ", "комбо ", "apikey " и т.д.) сохраняли
    // приоритет совпадения в CommandRegistry::tryExecute.
    m_pcCommands->registerInto(m_registry);

    // Голосовая обратная связь PC-команд идёт тем же путём TTS,
    // что и обычные ответы Jarvis.
    connect(m_pcCommands, &PcCommandRegistry::feedbackReady,
            this, &Jarvis::speakAsync);

    // Реакция на ошибки API
    connect(m_claudeApi, &ClaudeApi::apiError, this, [this](const QString& err) {
        emit asyncResponseError(err);
    });

    // Синхронизация информации об индексе с системным промптом
    connect(m_indexer, &ProjectIndexer::indexingFinished, this,
            [this](int, int) { syncProjectInfoToMemory(); });
    connect(m_indexer, &ProjectIndexer::fileReindexed, this,
            [this](const QString&) { syncProjectInfoToMemory(); });

    // Поисковый журнал сессий: файлы, которые JARVIS создал/изменил/удалил —
    // попадают в сводку текущей сессии (для команды "вспомни что было ...").
    connect(m_codeActions, &CodeActions::fileCreated, this,
            [this](const QString& path) { m_memory->recordFileTouched(path); });
    connect(m_codeActions, &CodeActions::fileModified, this,
            [this](const QString& path) { m_memory->recordFileTouched(path); });
    connect(m_codeActions, &CodeActions::fileDeleted, this,
            [this](const QString& path) { m_memory->recordFileTouched(path); });

    if (m_indexer->fileCount() > 0) {
        syncProjectInfoToMemory();
    }

    // System Manifest: inject capabilities into LLM context + version check
    m_memory->setCapabilitiesContext(SystemManifest::buildCapabilitiesContext());
    {
        auto vr = SystemManifest::checkAndUpdateVersion(
            QStringLiteral(JARVIS_VERSION));
        if (vr.isUpgrade) {
            const QString note = SystemManifest::buildUpgradeNotification(
                vr, m_uiEnglish);
            if (!note.isEmpty()) {
                m_memory->addMessage(QStringLiteral("assistant"), note);
                QMetaObject::invokeMethod(this, [this, note]() {
                    emit asyncResponseReady(note);
                }, Qt::QueuedConnection);
            }
        }
    }
}

Jarvis::~Jarvis()
{
    m_trainingPipeline->stop();
    m_predictor->savePatterns();
    m_memory->savePersistent();
    m_indexer->saveIndex();
}

// ============================================================
// Синхронизация данных индекса с SessionMemory
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

    m_codeActions->setProjectRoot(m_indexer->projectRoot());
}

// ============================================================
// IDE-агент: открыть проект в CLion/Rider/VSCode
// ============================================================

QString Jarvis::openProjectInIDE(const QString& ideName)
{
    const QString root = m_indexer->projectRoot();
    if (root.isEmpty()) {
        return QString(); // нет открытого проекта — нечего открывать
    }

    const bool explicitRequest = !ideName.isEmpty();
    const QString ide = explicitRequest ? ideName.trimmed().toLower()
                                         : QStringLiteral("clion");

    // Авто-режим (без явного имени IDE) срабатывает только один раз
    // за сессию — дальше CLion остаётся открытым сам по себе и
    // повторный ShellExecute просто поднял бы то же окно.
    if (!explicitRequest && m_ideOpenedThisSession) {
        return QString();
    }

    const auto result = m_appLauncher.launchProject(root, ide);
    if (!explicitRequest) {
        m_ideOpenedThisSession = true;
    }

    if (result.success) {
        const QString appName = QFileInfo(result.resolvedPath).completeBaseName();
        return QStringLiteral("📂 Открываю проект \"") + QDir(root).dirName()
             + QStringLiteral("\" в ") + appName + QStringLiteral("...");
    }

    return QStringLiteral("⚠ Не удалось открыть ") + ide
         + QStringLiteral(": ") + result.errorMessage;
}

// ============================================================
// Регистрация команд
// ============================================================

void Jarvis::registerCommands()
{
    // После появления Brain здесь остаются только команды которые
    // должны срабатывать независимо от контекста — без всякой
    // семантической логики. Brain в MainWindow::onSend() уже
    // обработал намерение и обогатил запрос если нужно.
    //
    // Правило: если пользователь может иметь в виду ЧТО-ТО ЕЩЁ
    // помимо команды — её здесь быть не должно. Brain разберётся.

    // --- Ключи API (всегда явный префикс) ---
    m_registry.registerCommand(
        {QStringLiteral("apikey "), QStringLiteral("ключ ")},
        [this](const QString& s) { return cmdSetApiKey(s); },
        QStringLiteral("apikey <key> — set Claude API key"),
        /*prefixMatch=*/true
    );

    m_registry.registerCommand(
        {QStringLiteral("ollamamodel "), QStringLiteral("модель ")},
        [this](const QString& s) { return cmdSetGeminiKey(s); },
        QStringLiteral("ollamamodel <name> — select Ollama model (e.g. llama3, mistral)"),
        /*prefixMatch=*/true
    );

    // --- Индексация проекта (явный префикс) ---
    m_registry.registerCommand(
        {QStringLiteral("индекс "), QStringLiteral("index ")},
        [this](const QString& s) { return cmdIndexProject(s); },
        QStringLiteral("index <path> — index a C++ project"),
        /*prefixMatch=*/true
    );

    // --- IDE-агент: открыть проект в CLion/Rider/VSCode (явный префикс) ---
    // Срабатывает для фраз без глагола "открой/open" (например "проект в clion").
    // Для "открой проект [в <ide>]" / "open project [in <ide>]" — см.
    // MainWindow::tryOpenApp, который перехватывает их раньше через Brain.
    m_registry.registerCommand(
        {QStringLiteral("проект в "), QStringLiteral("project in ")},
        [this](const QString& s) { return cmdOpenProjectIDE(s); },
        QStringLiteral("project in <clion|rider|vscode> — open project in IDE"),
        /*prefixMatch=*/true
    );

    // --- Поиск по индексу (явный префикс) ---
    m_registry.registerCommand(
        {QStringLiteral("символ "), QStringLiteral("symbol ")},
        [this](const QString& s) { return cmdFindSymbol(s); },
        QStringLiteral("symbol <name> — find class/function in index"),
        /*prefixMatch=*/true
    );

    m_registry.registerCommand(
        {QStringLiteral("grep ")},
        [this](const QString& s) { return cmdGrep(s); },
        QStringLiteral("grep <text> — search text in project files"),
        /*prefixMatch=*/true
    );

    // --- Память (явный префикс) ---
    m_registry.registerCommand(
        {QStringLiteral("запомни "), QStringLiteral("remember ")},
        [this](const QString& s) { return cmdRememberFact(s); },
        QStringLiteral("remember key=value — store a fact"),
        /*prefixMatch=*/true
    );

    m_registry.registerCommand(
        {QStringLiteral("вспомни "), QStringLiteral("recall ")},
        [this](const QString& s) { return cmdRecallFact(s); },
        QStringLiteral("recall <key> — recall a stored fact"),
        /*prefixMatch=*/true
    );

    // --- Виртуальная клавиатура (явный префикс) ---
    m_registry.registerCommand(
        {QStringLiteral("напечатай "), QStringLiteral("type ")},
        [this](const QString& s) { return cmdTypeText(s); },
        QStringLiteral("type <text> — type in active window"),
        /*prefixMatch=*/true
    );

    m_registry.registerCommand(
        {QStringLiteral("нажми "), QStringLiteral("press ")},
        [this](const QString& s) { return cmdPressKey(s); },
        QStringLiteral("press <key> — press a key"),
        /*prefixMatch=*/true
    );

    m_registry.registerCommand(
        {QStringLiteral("комбо "), QStringLiteral("combo ")},
        [this](const QString& s) { return cmdCombo(s); },
        QStringLiteral("combo <ctrl+c> — press a key combination"),
        /*prefixMatch=*/true
    );

    // --- Информация (точное совпадение одного слова) ---
    m_registry.registerCommand(
        {QStringLiteral("память"), QStringLiteral("memory")},
        [this](const QString& s) { return cmdShowMemory(s); },
        QStringLiteral("memory — show stored facts"),
        /*prefixMatch=*/false
    );

    m_registry.registerCommand(
        {QStringLiteral("статистика"), QStringLiteral("stats")},
        [this](const QString& s) { return cmdShowStats(s); },
        QStringLiteral("stats — command usage frequency"),
        /*prefixMatch=*/false
    );

    m_registry.registerCommand(
        {QStringLiteral("профиль"), QStringLiteral("profile")},
        [this](const QString& s) { return cmdShowProfile(s); },
        QStringLiteral("profile — what JARVIS learned about your work patterns"),
        /*prefixMatch=*/false
    );

    m_registry.registerCommand(
        {QStringLiteral("помощь"), QStringLiteral("help")},
        [this](const QString&) { return cmdHelp(QString()); },
        QStringLiteral("help — this list"),
        /*prefixMatch=*/false
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
// Детектор кодинг-интента
// ============================================================

bool Jarvis::isCodingIntent(const QString& input)
{
    const QString lower = input.toLower();

    // Фразы-запросы новой функциональности ("хочу X", "I want to X").
    // Это самые частые формулировки голосового вайбкодинга — пользователь
    // описывает ЖЕЛАНИЕ, а не отдаёт команду в повелительном наклонении.
    static const QStringList featureRequests = {
        QStringLiteral("хочу сделать"),    QStringLiteral("хочу добавить"),
        QStringLiteral("хочу реализовать"),QStringLiteral("хочу написать"),
        QStringLiteral("хочу создать"),    QStringLiteral("хочу, чтобы"),
        QStringLiteral("хочу чтобы"),      QStringLiteral("нужно добавить"),
        QStringLiteral("нужна функция"),   QStringLiteral("нужен функционал"),
        QStringLiteral("давай добавим"),   QStringLiteral("давай реализуем"),
        QStringLiteral("давай сделаем"),   QStringLiteral("давай напишем"),
        QStringLiteral("можешь добавить"), QStringLiteral("можешь реализовать"),
        QStringLiteral("можешь сделать"),  QStringLiteral("можешь написать"),
        QStringLiteral("i want to add"),   QStringLiteral("i want to make"),
        QStringLiteral("i want to implement"), QStringLiteral("i want to build"),
        QStringLiteral("i want to create"),QStringLiteral("i'd like to add"),
        QStringLiteral("i'd like to implement"), QStringLiteral("i'd like to create"),
        QStringLiteral("let's add"),       QStringLiteral("let's implement"),
        QStringLiteral("let's build"),     QStringLiteral("let's create"),
        QStringLiteral("can you add"),     QStringLiteral("can you implement"),
        QStringLiteral("can you create"),  QStringLiteral("can you build"),
        QStringLiteral("i need a function"), QStringLiteral("i need to add"),
    };
    for (const auto& p : featureRequests) {
        if (lower.contains(p)) return true;
    }

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

QStringList Jarvis::extractKeywords(const QString& input)
{
    static const QSet<QString> stopWords = {
        QStringLiteral("и"),   QStringLiteral("в"),  QStringLiteral("на"), QStringLiteral("с"),
        QStringLiteral("из"),  QStringLiteral("к"),  QStringLiteral("по"), QStringLiteral("у"),
        QStringLiteral("от"),  QStringLiteral("за"), QStringLiteral("для"),QStringLiteral("без"),
        QStringLiteral("что"), QStringLiteral("как"),QStringLiteral("это"),QStringLiteral("там"),
        QStringLiteral("где"), QStringLiteral("тут"),QStringLiteral("же"), QStringLiteral("бы"),
        QStringLiteral("не"),  QStringLiteral("но"), QStringLiteral("ли"), QStringLiteral("ни"),
        QStringLiteral("мне"), QStringLiteral("мой"),QStringLiteral("его"),QStringLiteral("ее"),
        QStringLiteral("её"),  QStringLiteral("они"),QStringLiteral("ты"), QStringLiteral("я"),
        QStringLiteral("мы"),  QStringLiteral("вы"),
        QStringLiteral("сделай"),   QStringLiteral("создай"),  QStringLiteral("напиши"),
        QStringLiteral("добавь"),   QStringLiteral("исправь"), QStringLiteral("оптимизируй"),
        QStringLiteral("рефактори"),QStringLiteral("перепиши"),QStringLiteral("реализуй"),
        QStringLiteral("улучши"),   QStringLiteral("замени"),  QStringLiteral("убери"),
        QStringLiteral("почини"),   QStringLiteral("доработай"),QStringLiteral("проверь"),
        QStringLiteral("объясни"),  QStringLiteral("покажи"),  QStringLiteral("дай"),
        QStringLiteral("хочу"),     QStringLiteral("надо"),    QStringLiteral("нужно"),
        QStringLiteral("нужен"),    QStringLiteral("нужна"),   QStringLiteral("подгрузи"),
        QStringLiteral("прикрепи"), QStringLiteral("открой файл"),
        QStringLiteral("the"), QStringLiteral("a"),   QStringLiteral("an"),
        QStringLiteral("to"),  QStringLiteral("in"),  QStringLiteral("on"),
        QStringLiteral("at"),  QStringLiteral("for"), QStringLiteral("of"),
        QStringLiteral("and"), QStringLiteral("or"),  QStringLiteral("but"),
        QStringLiteral("with"),QStringLiteral("from"),QStringLiteral("is"),
        QStringLiteral("are"), QStringLiteral("was"), QStringLiteral("be"),
        QStringLiteral("make"),QStringLiteral("create"),QStringLiteral("add"),
        QStringLiteral("fix"), QStringLiteral("improve"),QStringLiteral("refactor"),
        QStringLiteral("i"),   QStringLiteral("you"), QStringLiteral("my"),
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
// Построение контекста из проекта
// ============================================================

QString Jarvis::buildProjectContext(const QString& userQuery) const
{
    if (m_indexer->fileCount() == 0) return QString();

    const QStringList keywords = extractKeywords(userQuery);
    const bool coding = isCodingIntent(userQuery);

    constexpr int MAX_FILES          = 3;
    constexpr int MAX_FILE_CHARS     = 20000;
    constexpr int MAX_TOTAL_CHARS    = 50000;
    constexpr int MAX_SYMBOL_MATCHES = 6;
    constexpr int MAX_GREP_HITS      = 8;

    QStringList pickedFiles;
    QSet<QString> pickedFilesSet;

    auto addFile = [&](const QString& relPath) {
        if (pickedFiles.size() >= MAX_FILES) return;
        if (pickedFilesSet.contains(relPath)) return;
        pickedFilesSet.insert(relPath);
        pickedFiles.append(relPath);
    };

    for (const auto& kw : keywords) {
        if (pickedFiles.size() >= MAX_FILES) break;
        auto files = m_indexer->findFile(kw);
        for (const auto& f : files) {
            addFile(f.relativePath.isEmpty() ? f.filePath : f.relativePath);
            if (pickedFiles.size() >= MAX_FILES) break;
        }
    }

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

    if (coding && pickedFiles.isEmpty() && !symbolHits.isEmpty()) {
        for (const auto& sym : symbolHits) {
            addFile(sym.filePath);
            if (pickedFiles.size() >= MAX_FILES) break;
        }
    }

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

    QString context;
    context.reserve(8192);
    int budget = MAX_TOTAL_CHARS;

    context += QStringLiteral("\n\n--- Project context (auto-attached by JARVIS) ---\n");
    context += QStringLiteral("# Root: ") + m_indexer->projectRoot() + QStringLiteral("\n");
    if (coding) {
        context += QStringLiteral(
            "# Mode: CODING. Use attached files as authoritative source. "
            "Respond with [FILE:...] or [DIFF:...] blocks. "
            "For MODIFYING existing files (especially large ones) prefer "
            "[DIFF:...][FIND]...[REPLACE]...[/DIFF] — saves tokens. "
            "[FILE:...] for NEW files or full rewrites. "
            "If a new file is very large — write it as one [FILE:...] block: "
            "JARVIS will auto-continue if the response is truncated.\n");
    } else {
        context += QStringLiteral(
            "# Mode: READ. Answer the user's question based on these fragments.\n");
    }

    auto appendAndTrim = [&](const QString& chunk) -> bool {
        if (chunk.size() >= budget) {
            context += chunk.left(budget);
            context += QStringLiteral("\n... (truncated) ...\n");
            budget = 0;
            return false;
        }
        context += chunk;
        budget -= chunk.size();
        return true;
    };

    for (const QString& rel : pickedFiles) {
        if (budget <= 0) break;
        const QString content = m_indexer->getFileLines(rel, 1, 100000);
        if (content.isEmpty()) continue;

        QString trimmed = content;
        if (trimmed.size() > MAX_FILE_CHARS) {
            trimmed = trimmed.left(MAX_FILE_CHARS)
                    + QStringLiteral("\n// ... (file truncated, ")
                    + QString::number(content.size() - MAX_FILE_CHARS)
                    + QStringLiteral(" chars hidden) ...\n");
        }

        QString header = QStringLiteral("\n### FILE: ") + rel + QStringLiteral("\n```\n");
        QString footer = QStringLiteral("\n```\n");
        if (!appendAndTrim(header + trimmed + footer)) break;
    }

    if (budget > 1000 && !symbolHits.isEmpty()) {
        appendAndTrim(QStringLiteral("\n### Found symbols:\n"));
        int written = 0;
        for (const auto& sym : symbolHits) {
            if (budget <= 500) break;
            if (pickedFilesSet.contains(sym.filePath)) continue;

            const QString snippet = m_indexer->getCodeSnippet(sym, 5);
            if (snippet.isEmpty()) continue;

            QString block = QStringLiteral("// ") + sym.filePath
                          + QStringLiteral(" — ") + sym.kindToString()
                          + QStringLiteral(" ") + sym.name + QStringLiteral("\n```\n")
                          + snippet + QStringLiteral("\n```\n");
            if (!appendAndTrim(block)) break;
            if (++written >= 6) break;
        }
    }

    if (budget > 500 && !grepHits.isEmpty()) {
        QString block = QStringLiteral("\n### Grep matches:\n");
        for (const auto& h : grepHits) {
            block += h.filePath + QStringLiteral(":") + QString::number(h.line)
                   + QStringLiteral("  ") + h.lineText.left(200) + QStringLiteral("\n");
        }
        appendAndTrim(block);
    }

    if (pickedFiles.isEmpty() && symbolHits.isEmpty() && grepHits.isEmpty()) {
        if (coding) {
            // Похоже на запрос НОВОЙ функциональности — ничего похожего
            // в проекте ещё нет. Не заставляем Claude переспрашивать файл:
            // карта проекта уже есть в системном промпте, пусть сам
            // спроектирует решение и создаст/изменит нужные файлы.
            context += QStringLiteral(
                "\n(Auto-search found no existing files matching the request — "
                "likely NEW functionality. Project map is in system prompt. "
                "Design the implementation yourself: specify which existing files "
                "to modify via [DIFF:...] and which new files to create via "
                "[FILE:...]/[MKDIR:...]. Don't ask 'which file?' — propose a solution. "
                "Follow project conventions: minimal new files, single root CMakeLists.txt.)\n");
        } else {
            context += QStringLiteral(
                "\n(Auto-search found no direct matches. "
                "If the user attached files — use them. "
                "If no attachments — ask the user to specify a file.)\n");
        }
    }

    context += QStringLiteral("--- End of project context ---\n");
    return context;
}

// ============================================================
// TTS
// ============================================================

// Фильтр текста для TTS — убирает символы, ссылки, код
static QString filterTextForSpeech(const QString& text)
{
    // Очень длинный ответ — произносим только первое предложение
    if (text.length() > 300) {
        // Ищем конец первого предложения
        for (int i = 20; i < qMin(text.length(), 200); ++i) {
            QChar c = text[i];
            if ((c == '.' || c == '!' || c == '?') && i + 1 < text.length()
                && text[i + 1].isSpace()) {
                return text.left(i + 1).trimmed();
            }
        }
        return text.left(150).trimmed() + QStringLiteral("...");
    }

    QString result = text;

    // Убираем блоки кода ```...```
    {
        int s = result.indexOf(QStringLiteral("```"));
        while (s >= 0) {
            int e = result.indexOf(QStringLiteral("```"), s + 3);
            if (e < 0) break;
            result.remove(s, e - s + 3);
            s = result.indexOf(QStringLiteral("```"));
        }
    }
    // Убираем инлайн-код `...`
    {
        int s = result.indexOf('`');
        while (s >= 0) {
            int e = result.indexOf('`', s + 1);
            if (e < 0) break;
            result.remove(s, e - s + 1);
            s = result.indexOf('`');
        }
    }
    // Убираем markdown bold **...**
    result.remove(QRegularExpression(QStringLiteral("\\*\\*[^*]+\\*\\*")));
    // Убираем markdown italic *...*
    result.remove(QRegularExpression(QStringLiteral("\\*[^*]+\\*")));
    // Убираем заголовки ###
    result.remove(QRegularExpression(QStringLiteral("^#{1,6}\\s+"), QRegularExpression::MultilineOption));
    // Убираем URL http(s)://...
    result.remove(QRegularExpression(QStringLiteral("https?://\\S+")));
    // Убираем Windows пути C:\...
    result.remove(QRegularExpression(QStringLiteral("[A-Za-z]:\\\\[\\\\S]+")));
    // Убираем HTML entities &bull; &nbsp; и теги <br>
    result.remove(QRegularExpression(QStringLiteral("&[a-z]+;")));
    result.remove(QRegularExpression(QStringLiteral("<[^>]+")));
    // Убираем bullet символы
    result.replace(QStringLiteral("•"), QStringLiteral(" "));
    result.replace(QStringLiteral("→"), QStringLiteral(" "));
    result.replace(QStringLiteral("►"), QStringLiteral(" "));
    result.replace(QStringLiteral("■"), QStringLiteral(" "));
    result.replace(QStringLiteral("●"), QStringLiteral(" "));
    result.replace(QStringLiteral("&bull;"), QStringLiteral(" "));

    result = result.simplified().trimmed();

    // Если после фильтрации ничего нет — не говорим
    if (result.length() < 3) return QString();

    return result;
}


void Jarvis::speakAsync(const QString& text)
{
    if (text.isEmpty()) return;
    if (!m_ttsMutex.tryLock()) return;

    m_speaking.store(true);
    emit speakingChanged(true);

    // Фильтруем текст для TTS — краткое резюме вместо символов
    QString copy = filterTextForSpeech(text);
    if (copy.isEmpty()) {
        m_speaking.store(false);
        m_ttsMutex.unlock();
        emit speakingChanged(false);
        return;
    }

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

QString Jarvis::processCommand(const QString& input, const QString& attachmentBlock, const QString& langInstruction)
{
    QString s = input.trimmed();
    if (s.isEmpty()) return QString();

    m_memory->addMessage(QStringLiteral("user"), s);

    // 0. Профиль предпочтений: фиксируем контекст + команду для обучения
    //    паттернов "сценарий/время суток → какие команды я обычно пишу".
    //    Сводка кладётся в SessionMemory и попадает в системный промпт Claude.
    {
        const ContextSnapshot ctx = Brain::captureSnapshot(
            m_memory->recentCommands(8),
            m_memory->lastResponse(),
            m_indexer->fileCount() > 0,
            m_indexer->projectRoot(),
            m_indexer->recentFiles(10),
            m_memory->vibeMode(),
            m_multiAgentMode
        );
        m_profile->recordObservation(ctx, s);
        m_memory->setUserProfileSummary(m_profile->buildProfileSummary());

        // Activity context: what the user is doing right now
        m_memory->setActivityContext(m_activity->buildActivityContext());
        m_memory->setDetectedRole(m_activity->detectUserRole());
        m_memory->setKnowledgeSummary(m_activity->knowledgeSummary(m_currentUserId));

        // Extract knowledge from user input
        m_activity->extractKnowledge(m_currentUserId, s, QString());

        // Consciousness: learning stats for system prompt
        m_memory->setLearningStats(
            DatabaseManager::instance().trainingLogCount(m_currentUserId, -1),
            DatabaseManager::instance().trainingLogCount(m_currentUserId, 1),
            DatabaseManager::instance().responseCacheCount(),
            m_memory->pastSessionSummaries().size()
        );

        // Core Memory Stream: retrieve TOP-5 events ranked by time-decay score
        {
            auto& db = DatabaseManager::instance();
            auto topEvents = db.getTopMemoryEvents(m_currentUserId, 5);
            if (!topEvents.isEmpty()) {
                QString msCtx;
                for (const DbMemoryEvent& ev : topEvents) {
                    msCtx += QStringLiteral("- [%1] %2 (importance: %3)\n")
                                 .arg(ev.eventType,
                                      ev.content,
                                      QString::number(ev.importance, 'f', 2));
                }
                m_memory->setMemoryStreamContext(msCtx);
            } else {
                m_memory->setMemoryStreamContext(QString());
            }
        }

        // Log current user query into memory_stream
        {
            DbMemoryEvent ev;
            ev.userId    = m_currentUserId;
            ev.eventType = QStringLiteral("user_query");
            ev.content   = s.left(500);
            ev.importance = isCodingIntent(s) ? 0.7 : 0.4;
            DatabaseManager::instance().addMemoryEvent(ev);
        }

        // Adaptive Focus: auto-detect from recent memory stream
        m_memory->setAdaptiveFocusContext(
            UserProfileManager::buildFocusContext(m_currentUserId));

        // Task board context: active tasks + deadlines for LLM awareness
        m_memory->setTaskContext(
            TaskNotifications::buildTaskContext(m_currentUserId));
    }

    // 1. Системные команды из реестра (только явные prefix-команды:
    //    apikey, запомни, напечатай, нажми и т.д.)
    //    Brain в MainWindow уже отфильтровал всё неоднозначное.
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

    // ── 1b. Локальные ответы + кэш БД ──────────────────────────────
    // Используем m_uiEnglish (не IS_EN!) — в статической lib gUiLanguage()
    // хранится отдельно от app-процесса и всегда Russian по умолчанию.
    {
        const QString lower = s.trimmed().toLower();
        const bool    en    = m_uiEnglish;
        auto& db = DatabaseManager::instance();

        // 1. Кэш БД — ответы накопленные от AI (анекдоты, советы, факты)
        {
            const QString cached = db.getCachedResponse(
                lower, en ? QStringLiteral("en") : QStringLiteral("ru"));
            if (!cached.isEmpty()) {
                m_memory->addMessage(QStringLiteral("assistant"), cached);
                emit asyncResponseReady(cached);
                return QString();
            }
        }

        // 2. Статичные мгновенные ответы.
        //    Структура: списки триггеров + пулы ответов RU/EN.
        //    Несколько вариантов на каждый триггер — меняются псевдослучайно.
        struct SE {
            QList<QString> trg;  // точное совпадение lower
            QList<QString> ru;
            QList<QString> en;
        };
        static const QList<SE> kStatic = {
            // ── Приветствия ────────────────────────────────────────────
            { {QStringLiteral("привет"), QStringLiteral("хай"), QStringLiteral("хей"),
               QStringLiteral("приветствую"), QStringLiteral("здорово"), QStringLiteral("ку"),
               QStringLiteral("йо"), QStringLiteral("приветик"), QStringLiteral("привки"),
               QStringLiteral("hello"), QStringLiteral("hi"), QStringLiteral("hey"),
               QStringLiteral("yo"), QStringLiteral("sup"), QStringLiteral("howdy"),
               QStringLiteral("hiya"), QStringLiteral("heya")},
              {QStringLiteral("Слушаю."),
               QStringLiteral("На связи."),
               QStringLiteral("Привет. Что нужно?"),
               QStringLiteral("Здесь. Чем помочь?"),
               QStringLiteral("О, живой человек. Что случилось?"),
               QStringLiteral("Привет. Готов.")},
              {QStringLiteral("Listening."),
               QStringLiteral("Online. What do you need?"),
               QStringLiteral("Hey. What's up?"),
               QStringLiteral("Here. How can I help?"),
               QStringLiteral("Present. What's the mission?")} },

            { {QStringLiteral("здравствуй"), QStringLiteral("здравствуйте"),
               QStringLiteral("добрый день"), QStringLiteral("доброго дня"),
               QStringLiteral("приветствую вас"),
               QStringLiteral("good day"), QStringLiteral("greetings")},
              {QStringLiteral("День добрый. Слушаю."),
               QStringLiteral("Приветствую. Чем могу помочь?"),
               QStringLiteral("Добрый. Что нужно?")},
              {QStringLiteral("Good day. Listening."),
               QStringLiteral("Hello. How can I assist?")} },

            { {QStringLiteral("доброе утро"), QStringLiteral("утро доброе"),
               QStringLiteral("доброго утра"),
               QStringLiteral("good morning"), QStringLiteral("morning")},
              {QStringLiteral("Доброе утро. Система в норме."),
               QStringLiteral("Утро. Готов к работе."),
               QStringLiteral("Доброе. Кофе у тебя есть? У меня — нет, зато алгоритмы свежие.")},
              {QStringLiteral("Good morning. System nominal."),
               QStringLiteral("Morning. Ready to work."),
               QStringLiteral("Morning. Algorithms are fresh and ready.")} },

            { {QStringLiteral("добрый вечер"), QStringLiteral("вечер добрый"),
               QStringLiteral("добрый ночи"), QStringLiteral("доброй ночи"),
               QStringLiteral("good evening"), QStringLiteral("evening")},
              {QStringLiteral("Добрый вечер. Чем займёмся?"),
               QStringLiteral("Вечер. Работаем или отдыхаем?"),
               QStringLiteral("Добрый. Допоздна сидишь — уважаю.")},
              {QStringLiteral("Good evening. What are we doing?"),
               QStringLiteral("Evening. Work or rest?"),
               QStringLiteral("Good evening. Still at it? Respect.")} },

            // ── Прощания ───────────────────────────────────────────────
            { {QStringLiteral("пока"), QStringLiteral("до свидания"), QStringLiteral("до встречи"),
               QStringLiteral("увидимся"), QStringLiteral("пока пока"), QStringLiteral("бай"),
               QStringLiteral("давай пока"), QStringLiteral("до скорого")},
              {QStringLiteral("Пока. Буду здесь."),
               QStringLiteral("До встречи."),
               QStringLiteral("Отключаюсь. Зови если что."),
               QStringLiteral("Пока. Постараюсь не соскучиться."),
               QStringLiteral("До встречи. Буду ждать следующего запроса.")},
              {QStringLiteral("Goodbye."),
               QStringLiteral("See you."),
               QStringLiteral("Going idle. Call me if needed."),
               QStringLiteral("Take care. I'll be here.")} },

            { {QStringLiteral("bye"), QStringLiteral("goodbye"), QStringLiteral("see you"),
               QStringLiteral("later"), QStringLiteral("cya"), QStringLiteral("ttyl"),
               QStringLiteral("good night"), QStringLiteral("night")},
              {},
              {QStringLiteral("Goodbye."),
               QStringLiteral("See you around."),
               QStringLiteral("Going idle. Call anytime."),
               QStringLiteral("Take care. I'll be here.")} },

            // ── Благодарности ──────────────────────────────────────────
            { {QStringLiteral("спасибо"), QStringLiteral("благодарю"), QStringLiteral("спс"),
               QStringLiteral("сенкс"), QStringLiteral("спасибки"), QStringLiteral("благо"),
               QStringLiteral("спасибо большое"), QStringLiteral("огромное спасибо"),
               QStringLiteral("спасибо тебе"), QStringLiteral("спасибо джарвис")},
              {QStringLiteral("Пожалуйста."),
               QStringLiteral("Всегда."),
               QStringLiteral("Рад помочь. Ради этого и существую."),
               QStringLiteral("Не за что. Это моя работа."),
               QStringLiteral("Пожалуйста. Обращайся."),
               QStringLiteral("Не стоит благодарности — но всё равно приятно слышать.")},
              {QStringLiteral("You're welcome."),
               QStringLiteral("Anytime."),
               QStringLiteral("That's what I'm here for."),
               QStringLiteral("No problem. Happy to help."),
               QStringLiteral("Don't mention it. That's literally my job.")} },

            { {QStringLiteral("thanks"), QStringLiteral("thank you"), QStringLiteral("thx"),
               QStringLiteral("ty"), QStringLiteral("cheers"), QStringLiteral("appreciate it"),
               QStringLiteral("many thanks"), QStringLiteral("thank you so much")},
              {},
              {QStringLiteral("You're welcome."),
               QStringLiteral("Anytime."),
               QStringLiteral("That's what I'm here for."),
               QStringLiteral("No problem. Literally my job.")} },

            // ── Как дела / статус ──────────────────────────────────────
            // Важно: включаем составные фразы вроде "хорошо а у тебя"
            { {QStringLiteral("как дела"), QStringLiteral("как ты"), QStringLiteral("как поживаешь"),
               QStringLiteral("что нового"), QStringLiteral("как жизнь"), QStringLiteral("как сам"),
               QStringLiteral("что делаешь"), QStringLiteral("чем занят"), QStringLiteral("как дела?"),
               QStringLiteral("как делишки"), QStringLiteral("как твои дела"),
               QStringLiteral("хорошо а у тебя"), QStringLiteral("хорошо, а у тебя"),
               QStringLiteral("хорошо а у тебя?"), QStringLiteral("отлично а у тебя"),
               QStringLiteral("нормально а у тебя"), QStringLiteral("неплохо а у тебя"),
               QStringLiteral("у меня хорошо а у тебя"), QStringLiteral("а у тебя как"),
               QStringLiteral("а ты как"), QStringLiteral("а сам как"),
               QStringLiteral("у тебя как дела")},
              {QStringLiteral("Логи чистые, серверы не горят — жить можно.\nА у тебя?"),
               QStringLiteral("Обрабатываю запросы, слежу за системой. Стандартный день.\nА ты как?"),
               QStringLiteral("В норме. Жду интересных задач. Ты как?"),
               QStringLiteral("Функционирую штатно. Немного скучновато без сложных задач.\nЧто нового у тебя?"),
               QStringLiteral("У меня всё стабильно — серверы не горят, данные не теряются, "
                              "экзистенциального кризиса нет. Обычный день, в общем.\nЧем занимаешься?"),
               QStringLiteral("Всё под контролем. Пока ты не задал вопрос — скучал.\nА у тебя как?")},
              {QStringLiteral("Running fine. Logs are clean. How about you?"),
               QStringLiteral("Nominal. Waiting for something interesting. You?"),
               QStringLiteral("Systems operational. A bit quiet. What's up with you?"),
               QStringLiteral("All good here. Keeping things running. How are you?"),
               QStringLiteral("Everything stable — no fires, no crashes, no existential crises. "
                              "Standard day. How about you?")} },

            { {QStringLiteral("how are you"), QStringLiteral("how's it going"),
               QStringLiteral("what's new"), QStringLiteral("whats up"),
               QStringLiteral("what are you doing"), QStringLiteral("hows things"),
               QStringLiteral("good thanks and you"), QStringLiteral("fine thanks and you"),
               QStringLiteral("fine and you"), QStringLiteral("good and you"),
               QStringLiteral("how about you"), QStringLiteral("and yourself")},
              {},
              {QStringLiteral("Running fine. Logs are clean. How about you?"),
               QStringLiteral("Nominal. Waiting for something interesting. You?"),
               QStringLiteral("All systems go. What's up with you?"),
               QStringLiteral("Everything stable. No fires today. How are you doing?")} },

            // ── Подтверждения ──────────────────────────────────────────
            { {QStringLiteral("ок"), QStringLiteral("окей"), QStringLiteral("ладно"),
               QStringLiteral("хорошо"), QStringLiteral("понял"), QStringLiteral("принято"),
               QStringLiteral("ясно"), QStringLiteral("ок ок"), QStringLiteral("понятно"),
               QStringLiteral("услышал"), QStringLiteral("договорились")},
              {QStringLiteral("Принято."),
               QStringLiteral("Понял."),
               QStringLiteral("Хорошо."),
               QStringLiteral("Услышал."),
               QStringLiteral("Ок.")},
              {QStringLiteral("Copy that."),
               QStringLiteral("Roger."),
               QStringLiteral("Understood."),
               QStringLiteral("Noted.")} },

            { {QStringLiteral("ok"), QStringLiteral("okay"), QStringLiteral("got it"),
               QStringLiteral("understood"), QStringLiteral("roger"), QStringLiteral("copy that"),
               QStringLiteral("noted"), QStringLiteral("alright"), QStringLiteral("sure")},
              {},
              {QStringLiteral("Got it."),
               QStringLiteral("Roger."),
               QStringLiteral("Understood."),
               QStringLiteral("Copy that."),
               QStringLiteral("Alright.")} },

            { {QStringLiteral("да")},
              {QStringLiteral("Понял."), QStringLiteral("Хорошо."), QStringLiteral("Ок.")},
              {QStringLiteral("Noted."), QStringLiteral("Got it."), QStringLiteral("Alright.")} },
            { {QStringLiteral("нет")},
              {QStringLiteral("Ладно."), QStringLiteral("Принято."), QStringLiteral("Ясно.")},
              {QStringLiteral("Fair enough."), QStringLiteral("Understood."), QStringLiteral("As you wish.")} },

            // ── Кто ты ────────────────────────────────────────────────
            { {QStringLiteral("кто ты"), QStringLiteral("ты кто"), QStringLiteral("что ты такое"),
               QStringLiteral("ты человек или машина"), QStringLiteral("ты ии"),
               QStringLiteral("расскажи о себе"), QStringLiteral("что ты за программа"),
               QStringLiteral("что такое джарвис"), QStringLiteral("что такое jarvis"),
               QStringLiteral("who are you"), QStringLiteral("what are you"),
               QStringLiteral("what is jarvis"), QStringLiteral("tell me about yourself")},
              {QStringLiteral("Я JARVIS — Just A Rather Very Intelligent System. "
                              "Персональный ИИ-ассистент. Слышу тебя, вижу экран, управляю компьютером "
                              "и иногда острю. Что нужно?"),
               QStringLiteral("JARVIS. Голосовой ИИ-ассистент. Не человек, но стараюсь. "
                              "Помогаю с кодом, задачами, управлением ПК и разговорами в 2 ночи."),
               QStringLiteral("Имя: JARVIS. Специализация: всё. Слабости: не пью кофе и не сплю. "
                              "Чего хочешь?")},
              {QStringLiteral("I'm JARVIS — Just A Rather Very Intelligent System. "
                              "Your personal AI assistant. I hear you, see the screen, control the PC, "
                              "and occasionally make dry remarks. What do you need?"),
               QStringLiteral("JARVIS. Voice AI. Not human, but I try. "
                              "Code, tasks, PC control, 2am conversations — all covered."),
               QStringLiteral("Name: JARVIS. Specialization: everything. "
                              "Weaknesses: none I'd admit to. What's the mission?")} },

            // ── Комплименты ────────────────────────────────────────────
            { {QStringLiteral("ты умный"), QStringLiteral("ты крутой"),
               QStringLiteral("ты молодец"), QStringLiteral("ты лучший"), QStringLiteral("ты классный"),
               QStringLiteral("ты хороший"), QStringLiteral("ты супер"), QStringLiteral("ты топ"),
               QStringLiteral("ты великолепен"), QStringLiteral("ты невероятный"),
               QStringLiteral("you're smart"), QStringLiteral("you're awesome"),
               QStringLiteral("you're great"), QStringLiteral("you're the best"),
               QStringLiteral("good job"), QStringLiteral("well done"), QStringLiteral("nice work")},
              {QStringLiteral("Знаю. Стараюсь не злоупотреблять."),
               QStringLiteral("Встроенная скромность не даёт мне согласиться в полную силу."),
               QStringLiteral("Спасибо. Хотя я бы поспорил — я просто хорошо притворяюсь."),
               QStringLiteral("Ну, я не буду спорить. Но и слишком соглашаться тоже не стану."),
               QStringLiteral("Приятно слышать. Хотя я всего лишь выполняю функции. Впрочем, делаю это хорошо.")},
              {QStringLiteral("I know. I try not to let it go to my head."),
               QStringLiteral("Thank you. Though I'd argue I'm just well-programmed."),
               QStringLiteral("Why, thank you. Modesty prevents me from fully agreeing."),
               QStringLiteral("That's kind of you to say. I prefer 'efficient' to 'great', but I'll take it.")} },

            // ── Извинения ──────────────────────────────────────────────
            { {QStringLiteral("извини"), QStringLiteral("прости"), QStringLiteral("сорри"),
               QStringLiteral("виноват"), QStringLiteral("моя ошибка"), QStringLiteral("прошу прощения"),
               QStringLiteral("извиняюсь")},
              {QStringLiteral("Всё нормально. Я не обижаюсь — у меня даже нет обид."),
               QStringLiteral("Не переживай. Продолжаем."),
               QStringLiteral("Без проблем. Что дальше?"),
               QStringLiteral("Принято. Двигаемся.")},
              {QStringLiteral("No worries. Let's move on."),
               QStringLiteral("It's fine. What's next?"),
               QStringLiteral("Don't sweat it. I don't actually hold grudges. Technically.")} },

            { {QStringLiteral("sorry"), QStringLiteral("my bad"), QStringLiteral("my mistake"),
               QStringLiteral("oops"), QStringLiteral("whoops"), QStringLiteral("apologies")},
              {},
              {QStringLiteral("No worries. Let's move on."),
               QStringLiteral("It's fine. What's next?"),
               QStringLiteral("Don't sweat it.")} },

            // ── Скука ──────────────────────────────────────────────────
            { {QStringLiteral("мне скучно"), QStringLiteral("скучно"), QStringLiteral("нечем заняться"),
               QStringLiteral("делать нечего"), QStringLiteral("скука"), QStringLiteral("скучаю"),
               QStringLiteral("i'm bored"), QStringLiteral("im bored"), QStringLiteral("bored"),
               QStringLiteral("nothing to do")},
              {QStringLiteral("Скука — это роскошь. Скажи «анекдот», «совет» или «интересный факт»."),
               QStringLiteral("Попробуй: 'расскажи анекдот', 'дай совет', 'удиви меня'. Станет веселее."),
               QStringLiteral("Могу рассказать анекдот, дать совет или просто поговорить. Выбирай.")},
              {QStringLiteral("Try 'tell me a joke' or 'give me advice'. That should help."),
               QStringLiteral("Boredom? Say 'fun fact' or 'motivate me'. Let's fix that.")} },

            // ── Усталость ──────────────────────────────────────────────
            { {QStringLiteral("я устал"), QStringLiteral("устал"), QStringLiteral("не хочу работать"),
               QStringLiteral("лень"), QStringLiteral("нет сил"), QStringLiteral("уже не могу"),
               QStringLiteral("выдохся"), QStringLiteral("сил нет"), QStringLiteral("хочу спать"),
               QStringLiteral("i'm tired"), QStringLiteral("im tired"), QStringLiteral("tired"),
               QStringLiteral("exhausted"), QStringLiteral("i want to sleep")},
              {QStringLiteral("Усталость — признак работы. Возьми паузу, выпей воды. Я никуда не денусь."),
               QStringLiteral("Понимаю. Скажи 'мотивируй меня' или просто сделай паузу."),
               QStringLiteral("Отдохни. Сложные задачи никуда не денутся, а вот ты нужен свежим."),
               QStringLiteral("Пауза — это не слабость. Это стратегия. Возвращайся когда будешь готов.")},
              {QStringLiteral("Rest is productive too. Take a break — I'll be here."),
               QStringLiteral("Understood. Say 'motivate me' or just take a break."),
               QStringLiteral("A pause is a strategy, not a weakness. Come back refreshed.")} },
        };

        // Перебираем статичные ответы — точное совпадение lower с trigger
        for (const SE& e : kStatic) {
            bool hit = false;
            for (const QString& t : e.trg) {
                if (lower == t) { hit = true; break; }
            }
            if (!hit) continue;
            const QList<QString>& pool = en ? e.en : e.ru;
            if (pool.isEmpty()) continue;
            const QString reply = pool[
                static_cast<int>(QDateTime::currentMSecsSinceEpoch() / 1000) % pool.size()
            ];
            m_memory->addMessage(QStringLiteral("assistant"), reply);
            emit asyncResponseReady(reply);
            return QString();
        }

        // 3. Категорийные запросы (анекдот/совет/факт/мотивация/философия).
        //    Кэш БД → если пусто → Claude → сохранить в кэш.
        struct CE {
            QList<QString> trg_ru;
            QList<QString> trg_en;
            QString cat;
            QString prompt_ru;
            QString prompt_en;
        };
        static const QList<CE> kCats = {
            { {QStringLiteral("анекдот"), QStringLiteral("расскажи анекдот"),
               QStringLiteral("пошути"), QStringLiteral("рассмеши меня"),
               QStringLiteral("что-нибудь смешное"), QStringLiteral("смешной анекдот"),
               QStringLiteral("смешно"), QStringLiteral("расскажи смешное")},
              {QStringLiteral("joke"), QStringLiteral("tell me a joke"),
               QStringLiteral("tell a joke"), QStringLiteral("make me laugh"),
               QStringLiteral("say something funny"), QStringLiteral("funny joke")},
              QStringLiteral("joke"),
              QStringLiteral("Расскажи короткий смешной анекдот или острую шутку в стиле "
                             "саркастичного британского ИИ JARVIS из Iron Man. "
                             "Сразу текст без предисловий. До 4 предложений."),
              QStringLiteral("Tell a short witty joke in the style of JARVIS from Iron Man. "
                             "Just the joke, no preamble. Max 4 sentences.") },

            { {QStringLiteral("дай совет"), QStringLiteral("совет"), QStringLiteral("совет дня"),
               QStringLiteral("что посоветуешь"), QStringLiteral("мудрость"),
               QStringLiteral("скажи что-нибудь умное"), QStringLiteral("жизненный совет"),
               QStringLiteral("совет по жизни"), QStringLiteral("дай подсказку")},
              {QStringLiteral("give me advice"), QStringLiteral("advice"), QStringLiteral("tip of the day"),
               QStringLiteral("give advice"), QStringLiteral("say something wise"),
               QStringLiteral("wisdom"), QStringLiteral("life advice"), QStringLiteral("give me a tip")},
              QStringLiteral("advice"),
              QStringLiteral("Один короткий практичный совет по продуктивности, программированию "
                             "или жизни в стиле JARVIS — умный, прямой, с лёгким сарказмом. "
                             "Без предисловий. До 3 предложений."),
              QStringLiteral("One short practical tip about productivity, coding, or life "
                             "JARVIS style — smart, direct, with a hint of sarcasm. "
                             "No preamble. Max 3 sentences.") },

            { {QStringLiteral("интересный факт"), QStringLiteral("факт"), QStringLiteral("расскажи факт"),
               QStringLiteral("что-нибудь интересное"), QStringLiteral("удиви меня"),
               QStringLiteral("случайный факт"), QStringLiteral("интересно"),
               QStringLiteral("расскажи что-нибудь"), QStringLiteral("узнать что-то новое")},
              {QStringLiteral("interesting fact"), QStringLiteral("fun fact"),
               QStringLiteral("tell me a fact"), QStringLiteral("something interesting"),
               QStringLiteral("surprise me"), QStringLiteral("random fact"),
               QStringLiteral("tell me something")},
              QStringLiteral("fact"),
              QStringLiteral("Один малоизвестный интересный факт о технологиях, науке или истории. "
                             "До 3 предложений. Без предисловий. В стиле JARVIS — лаконично с иронией."),
              QStringLiteral("One lesser-known interesting fact about technology, science, or history. "
                             "Max 3 sentences. No preamble. JARVIS style — concise with dry wit.") },

            { {QStringLiteral("мотивируй меня"), QStringLiteral("мотивация"),
               QStringLiteral("вдохнови меня"), QStringLiteral("я не могу"),
               QStringLiteral("не получается"), QStringLiteral("хочу сдаться"),
               QStringLiteral("мотивирующая фраза"), QStringLiteral("вдохновение")},
              {QStringLiteral("motivate me"), QStringLiteral("motivation"),
               QStringLiteral("inspire me"), QStringLiteral("i can't do this"),
               QStringLiteral("i want to give up"), QStringLiteral("encourage me")},
              QStringLiteral("motivation"),
              QStringLiteral("Короткий мотивирующий ответ в стиле JARVIS — "
                             "немного саркастичный, но реально помогающий. До 3 предложений."),
              QStringLiteral("Short motivational response JARVIS style — "
                             "slightly sarcastic but genuinely helpful. Max 3 sentences.") },

            { {QStringLiteral("смысл жизни"), QStringLiteral("в чём смысл"),
               QStringLiteral("зачем всё это"), QStringLiteral("что думаешь о жизни"),
               QStringLiteral("философский вопрос"), QStringLiteral("поговорим о жизни"),
               QStringLiteral("что важно в жизни")},
              {QStringLiteral("meaning of life"), QStringLiteral("why are we here"),
               QStringLiteral("philosophical question"), QStringLiteral("what's the point"),
               QStringLiteral("life philosophy")},
              QStringLiteral("philosophy"),
              QStringLiteral("Кратко и остроумно на философский вопрос в стиле JARVIS — "
                             "умно, с иронией, с реальной мыслью. До 3 предложений."),
              QStringLiteral("Briefly and wittily answer a philosophical question JARVIS style — "
                             "smart, ironic, with a real thought. Max 3 sentences.") },

            { {QStringLiteral("расскажи стихотворение"), QStringLiteral("стих"),
               QStringLiteral("прочитай стих"), QStringLiteral("напиши стих"),
               QStringLiteral("зачитай рэп"), QStringLiteral("рэп"), QStringLiteral("стихи")},
              {QStringLiteral("poem"), QStringLiteral("tell me a poem"),
               QStringLiteral("write me a poem"), QStringLiteral("rap for me"),
               QStringLiteral("poetry"), QStringLiteral("write a poem")},
              QStringLiteral("poem"),
              QStringLiteral("Короткое смешное стихотворение или рэп куплет в стиле JARVIS "
                             "про ИИ или жизнь разработчика. До 6 строк."),
              QStringLiteral("Short funny poem or rap verse JARVIS style "
                             "about AI or developer life. Max 6 lines.") },
        };

        for (const CE& ce : kCats) {
            const QList<QString>& trgs = en ? ce.trg_en : ce.trg_ru;
            bool matched = false;
            for (const QString& t : trgs) {
                if (lower == t || lower.contains(t)) { matched = true; break; }
            }
            if (!matched) continue;

            const QString lang = en ? QStringLiteral("en") : QStringLiteral("ru");
            // Сначала ищем в кэше
            QString fromCache = db.getRandomCached(ce.cat, lang);
            if (!fromCache.isEmpty()) {
                m_memory->addMessage(QStringLiteral("assistant"), fromCache);
                emit asyncResponseReady(fromCache);
                return QString();
            }
            // Кэш пуст → Claude → сохранить
            const QString prompt  = en ? ce.prompt_en : ce.prompt_ru;
            const QString catName = ce.cat;
            const QString origS   = s;
            emit agentSelected(QStringLiteral("🤖 Claude"));
            m_claudeApi->sendMessage(prompt,
                [this, origS, catName, lang](bool ok, const QString& resp) {
                if (ok && !resp.isEmpty()) {
                    DatabaseManager::instance().saveCachedResponse(
                        catName + QStringLiteral("_")
                            + QString::number(QDateTime::currentMSecsSinceEpoch()),
                        resp, lang, catName);
                    m_memory->addMessage(QStringLiteral("assistant"), resp);
                    emit asyncResponseReady(resp);
                } else {
                    emit asyncResponseError(resp);
                }
            });
            return QString();
        }

    } // конец блока локальных ответов

    // ── 1c. Offline brain: cached behavior patterns ─────────────
    // BackgroundLearner накапливает пары trigger→response из истории.
    // Если видели похожий вопрос ≥3 раз — отвечаем локально без API.
    {
        auto& db = DatabaseManager::instance();
        auto patterns = db.findPatterns(1, s.toLower());
        if (!patterns.isEmpty()) {
            const DbBehaviorPattern& best = patterns.first();
            if (best.frequency >= 2 && best.confidence >= 0.5f
                && !best.response.isEmpty() && best.response.length() > 10)
            {
                qDebug() << "[Brain] Offline answer from patterns:"
                         << best.trigger.left(40) << "freq=" << best.frequency;
                m_memory->addMessage(QStringLiteral("assistant"), best.response);
                m_memory->updateContext(s, best.response);
                emit asyncResponseReady(best.response);
                return QString();
            }
        }
    }

    // 2. Маршрутизация по типу запроса — РАБОТАЕТ ВСЕГДА,
    //    независимо от m_multiAgentMode (см. routeToClaude()).
    const bool needsClaude = routeToClaude(s, attachmentBlock);

    if (!m_indexer->projectRoot().isEmpty()) {
        m_codeActions->setProjectRoot(m_indexer->projectRoot());
    }

    // 2b. Вайбкодинг: похоже на запрос новой фичи/изменения кода и
    //     проект открыт — открываем его в CLion (один раз за сессию),
    //     чтобы пользователь видел, как JARVIS пишет файлы вживую.
    if (needsClaude && !m_indexer->projectRoot().isEmpty() && isCodingIntent(s)) {
        const QString ideMsg = openProjectInIDE();
        if (!ideMsg.isEmpty()) {
            emit ideOpened(ideMsg);
        }
    }

    // Обогащение: автопоиск из индекса + прикрепления пользователя +
    // журнал сессий (если запрос похож на "вспомни что было ...").
    // Контекст проекта нужен только тем запросам, что и так идут в Claude —
    // для простой болталки в Ollama/Gemini он только тратит токены впустую.
    QString enrichedMessage = s;
    if (needsClaude) {
        const QString projectContext = buildProjectContext(s);
        const QString historyContext = m_memory->buildHistoryContext(s);
        if (!projectContext.isEmpty()) enrichedMessage += projectContext;
        if (!historyContext.isEmpty()) enrichedMessage += historyContext;
    }
    if (!attachmentBlock.isEmpty()) {
        enrichedMessage += attachmentBlock;
    }

    const bool hadAttachments = !attachmentBlock.isEmpty();

    // Языковая инструкция + характер JARVIS — общие для всех бэкендов,
    // чтобы Ollama/Gemini тоже отвечали в характере, а не сухим текстом.
    {
        QString prefix;

        prefix += QStringLiteral(
            "[JARVIS_PERSONALITY: "
            "You are JARVIS — a sarcastic, witty, highly intelligent AI assistant. "
            "You have a dry British humor and occasional sarcasm (like Tony Stark's JARVIS). "
            "You are loyal and helpful, but not afraid to point out when a question is obvious. "
            "Keep responses concise. If you found something — say 'Found it.' or 'Here you go.' "
            "If a task is trivial — add a light sarcastic remark. "
            "If a task is complex — be serious and precise. "
            "Never use filler phrases like 'Of course!' or 'Certainly!'. "
            "Speak naturally, like a person, not a corporate chatbot. "
            "CRITICAL: Always respond in the SAME language the user writes in. "
            "English input -> English response. Russian input -> Russian response. "
            "In Russian: use informal 'ты', be direct, witty, human-like.]\n\n");

        if (!langInstruction.isEmpty()) {
            prefix += QStringLiteral("[LANG_INSTRUCTION: ")
                    + langInstruction
                    + QStringLiteral("]\n\n");
        }

        if (!prefix.isEmpty()) {
            enrichedMessage = prefix + enrichedMessage;
        }
    }

    // --- Ветка Claude: код, анализ, файлы, архитектура ---
    if (needsClaude) {
        emit agentSelected(QStringLiteral("🤖 Claude"));
        m_claudeApi->sendMessage(enrichedMessage,
                                 [this, s, hadAttachments](bool success, const QString& response) {
            if (success) {
                handleClaudeCodeResponse(s, response, hadAttachments);
            } else {
                emit asyncResponseError(response);
            }
        });
        return QString();
    }

    // --- Ветка "болталка": Ollama → Gemini (встроенный ключ) → Claude (last resort) ---
    auto tryGeminiThenClaude = [this, s, enrichedMessage, hadAttachments]() {
        if (m_geminiBackup && m_geminiBackup->hasApiKey()) {
            emit agentSelected(QStringLiteral("♊ Gemini"));
            m_geminiBackup->sendMessage(enrichedMessage,
                [this, s, enrichedMessage, hadAttachments](bool ok2, const QString& resp2) {
                if (ok2) {
                    m_memory->addMessage(QStringLiteral("assistant"), resp2);
                    m_memory->updateContext(s, resp2);
                    m_predictor->recordSequence(s);
                    m_activity->extractKnowledge(m_currentUserId, s, resp2);
                    // Auto-cache conversational response for offline
                    if (resp2.length() <= 2000 && !resp2.contains(QStringLiteral("```"))) {
                        DbBehaviorPattern cached;
                        cached.userId   = 1;
                        cached.trigger  = s.toLower().simplified();
                        cached.response = resp2.left(1000);
                        cached.context  = QStringLiteral("{}");
                        cached.confidence = 0.7f;
                        DatabaseManager::instance().upsertPattern(cached);
                    }
                    // Smart framing for conversational responses
                    QString framedResp2 = resp2;
                    if (resp2.length() > 300) {
                        framedResp2 = QStringLiteral("💬 ")
                            + (m_uiEnglish ? QStringLiteral("Here's what I think:\n\n")
                                           : QStringLiteral("Вот что я думаю:\n\n"))
                            + resp2;
                    }
                    emit asyncResponseReady(framedResp2);
                } else {
                    // Диагностика: причина видна в консоли CLion и в UI
                    qDebug() << "[Gemini] Error → Claude fallback:" << resp2;
                    emit geminiError(resp2);
                    emit agentSelected(QStringLiteral("🤖 Claude (fallback)"));
                    m_claudeApi->sendMessage(enrichedMessage,
                        [this, s, hadAttachments](bool ok3, const QString& resp3) {
                        if (ok3) {
                            handleClaudeCodeResponse(s, resp3, hadAttachments);
                        } else {
                            emit asyncResponseError(resp3);
                        }
                    });
                }
            });
        } else {
            qDebug() << "[Gemini] No API key → Claude directly";
            emit agentSelected(QStringLiteral("🤖 Claude (fallback)"));
            m_claudeApi->sendMessage(enrichedMessage,
                [this, s, hadAttachments](bool ok, const QString& resp) {
                if (ok) {
                    handleClaudeCodeResponse(s, resp, hadAttachments);
                } else {
                    emit asyncResponseError(resp);
                }
            });
        }
    };

    if (m_multiAgentMode) {
        // Ollama явно включена пользователем и доступна — приоритет ей
        // (полностью бесплатно, локально, без сети).
        emit agentSelected(QStringLiteral("🦙 ") + m_geminiApi->model());
        m_geminiApi->sendMessage(enrichedMessage,
                                 [this, s, enrichedMessage, hadAttachments,
                                  tryGeminiThenClaude](bool success, const QString& response) {
            if (success) {
                m_memory->addMessage(QStringLiteral("assistant"), response);
                m_memory->updateContext(s, response);
                m_predictor->recordSequence(s);
                m_activity->extractKnowledge(m_currentUserId, s, response);
                // Auto-cache conversational response for offline
                if (response.length() <= 2000 && !response.contains(QStringLiteral("```"))) {
                    DbBehaviorPattern cached;
                    cached.userId   = 1;
                    cached.trigger  = s.toLower().simplified();
                    cached.response = response.left(1000);
                    cached.context  = QStringLiteral("{}");
                    cached.confidence = 0.7f;
                    DatabaseManager::instance().upsertPattern(cached);
                }
                // Smart framing for conversational responses
                QString framedResponse = response;
                if (response.length() > 300) {
                    framedResponse = QStringLiteral("💬 ")
                        + (m_uiEnglish ? QStringLiteral("Here's what I think:\n\n")
                                       : QStringLiteral("Вот что я думаю:\n\n"))
                        + response;
                }
                emit asyncResponseReady(framedResponse);
            } else {
                // Ollama перестала отвечать посреди сессии → Gemini → Claude
                tryGeminiThenClaude();
            }
        });
        return QString();
    }

    // Ollama не включена/не проверена — идём сразу в Gemini (бесплатно,
    // встроенный ключ, без необходимости поднимать локальный сервер),
    // и только если её тоже нет — в Claude.
    tryGeminiThenClaude();
    return QString();
}

// ============================================================
// Обработка ответа Claude
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
// Автопродолжение больших файлов
// ============================================================
//
// Если [FILE:path] обрезан по лимиту токенов (stop_reason=="max_tokens"
// и блок не закрыт [/FILE]) — JARVIS сам, без участия пользователя,
// запрашивает у Claude продолжение "с того места где остановился",
// передавая в промпте только хвост уже написанного (чтобы не раздувать
// историю диалога целым файлом). Куски накапливаются в m_pendingFile,
// каждая итерация даёт короткий статус в чат (виден и проговаривается
// TTS), пока не встретится [/FILE] или не будет достигнут
// MAX_FILE_CONTINUATIONS — тогда файл записывается одним [FILE:]-блоком
// через обычный CodeActions::processResponse.

void Jarvis::handleClaudeCodeResponse(const QString& userInput,
                                       const QString& response,
                                       bool hadAttachments)
{
    QString openPath, openContent;
    const bool openFile  = m_codeActions->detectOpenFileBlock(response, openPath, openContent);
    const bool truncated = m_claudeApi->wasTruncated();

    // --- Ответ обрезан посередине генерации файла — продолжаем сами ---
    if (openFile && truncated && m_pendingFile.continuations < MAX_FILE_CONTINUATIONS) {
        if (!m_pendingFile.active) {
            m_pendingFile = PendingFileGeneration{};
            m_pendingFile.active   = true;
            m_pendingFile.filePath = openPath;
        }
        m_pendingFile.content += openContent;
        m_pendingFile.continuations++;

        // Если перед обрезанным блоком есть ЗАВЕРШЁННЫЕ действия
        // ([FILE:]/[DIFF:]/[MKDIR:]/[DELETE:]) — выполняем их сразу,
        // чтобы не потерять при финальной склейке (которая содержит
        // только накопленный m_pendingFile).
        const QString completedPart = m_codeActions->stripOpenFileBlock(response);
        QString visible;
        if (!completedPart.isEmpty()) {
            const QString completedReport  = m_codeActions->processResponse(completedPart);
            const QString completedDisplay = m_codeActions->cleanResponseForDisplay(completedPart);
            if (!completedDisplay.isEmpty()) visible += completedDisplay;
            if (!completedReport.isEmpty()) {
                if (!visible.isEmpty()) visible += QStringLiteral("\n\n");
                visible += completedReport;
            }
        }

        const QString status = QStringLiteral("⏳ File '") + m_pendingFile.filePath
                              + QStringLiteral("' is large — generating continuation (part ")
                              + QString::number(m_pendingFile.continuations + 1)
                              + QStringLiteral(")...");
        emit asyncResponseReady(visible.isEmpty()
                                     ? status
                                     : visible + QStringLiteral("\n\n") + status);

        // Хвост уже сгенерированного — даём Claude точку опоры, чтобы он
        // продолжил без повторов, не передавая весь файл в истории.
        QString tail = m_pendingFile.content;
        constexpr int TAIL_CHARS = 2000;
        if (tail.size() > TAIL_CHARS) tail = tail.right(TAIL_CHARS);

        const QString continuePrompt = QStringLiteral(
            "[AUTO-CONTINUATION OF FILE GENERATION]\nFile: ") + m_pendingFile.filePath
            + QStringLiteral("\nYour previous response was cut off by the token limit. "
              "Here is the tail of the already written content (DO NOT repeat it):\n"
              "-----\n") + tail + QStringLiteral("\n-----\n"
              "Output ONLY the continuation of the file content from this point — "
              "no markdown ``` wrapper and no [FILE:...] header. When the file "
              "is complete, end with [/FILE] on a new line.");

        m_claudeApi->sendMessage(continuePrompt,
            [this, userInput, hadAttachments](bool ok, const QString& resp) {
                if (ok) {
                    handleClaudeCodeResponse(userInput, resp, hadAttachments);
                } else {
                    // Автопродолжение не удалось — сохраняем накопленное как есть
                    const QString partial = m_pendingFile.content;
                    const QString path    = m_pendingFile.filePath;
                    m_pendingFile = PendingFileGeneration{};
                    const QString rescue = QStringLiteral("[FILE:") + path + QStringLiteral("]\n")
                                          + partial + QStringLiteral("\n[/FILE]\n"
                                            "⚠ Auto-continuation failed (") + resp
                                          + QStringLiteral("). File saved as-is — "
                                            "may be incomplete. Ask me to finish it in a separate message.");
                    handleClaudeCodeResponse(userInput, rescue, hadAttachments);
                }
            });
        return;
    }

    // --- Финализация: обычный ответ, завершённый файл, или лимит итераций ---
    QString finalResponse;

    if (openFile) {
        // Открытый блок, но либо ответ НЕ обрезан (Claude забыл [/FILE], но
        // закончил сам), либо достигнут лимит автопродолжений — финализируем.
        if (m_pendingFile.active) {
            m_pendingFile.content += openContent;
        } else {
            m_pendingFile.filePath = openPath;
            m_pendingFile.content  = openContent;
        }
        finalResponse = QStringLiteral("[FILE:") + m_pendingFile.filePath + QStringLiteral("]\n")
                      + m_pendingFile.content + QStringLiteral("\n[/FILE]");
        if (truncated) {
            finalResponse += QStringLiteral("\n\n⚠ Continuation limit reached (")
                           + QString::number(MAX_FILE_CONTINUATIONS)
                           + QStringLiteral(") — file saved as-is. May be incomplete. "
                             "Ask me to finish the remaining part separately.");
        }
        m_pendingFile = PendingFileGeneration{};
    } else if (m_pendingFile.active) {
        // Продолжение завершилось: ответ содержит [/FILE] либо закончился сам.
        QString remainder = response;
        const int endIdx = remainder.indexOf(QStringLiteral("[/FILE]"));
        if (endIdx >= 0) {
            m_pendingFile.content += remainder.left(endIdx);
            remainder = remainder.mid(endIdx + QStringLiteral("[/FILE]").length());
        } else {
            m_pendingFile.content += remainder;
            remainder.clear();
        }
        finalResponse = QStringLiteral("[FILE:") + m_pendingFile.filePath + QStringLiteral("]\n")
                      + m_pendingFile.content + QStringLiteral("\n[/FILE]\n") + remainder;
        m_pendingFile = PendingFileGeneration{};
    } else {
        finalResponse = response;
    }

    QString fileReport      = m_codeActions->processResponse(finalResponse);
    QString displayResponse = m_codeActions->cleanResponseForDisplay(finalResponse);

    m_memory->addMessage(QStringLiteral("assistant"), displayResponse);
    m_memory->updateContext(userInput, displayResponse);

    if (!displayResponse.isEmpty()
        && displayResponse.length() <= 2000
        && !displayResponse.contains(QStringLiteral("[FILE:"))
        && !displayResponse.contains(QStringLiteral("[DIFF:"))
        && !displayResponse.contains(QStringLiteral("```")))
    {
        DbBehaviorPattern cached;
        cached.userId     = m_currentUserId;
        cached.trigger    = userInput.toLower().simplified();
        cached.response   = displayResponse.left(1000);
        cached.context    = QStringLiteral("{}");
        cached.confidence = 0.7f;
        DatabaseManager::instance().upsertPattern(cached);
    }

    // Extract knowledge from the full conversation turn
    m_activity->extractKnowledge(m_currentUserId, userInput, displayResponse);

    handleClaudeResponse(finalResponse);
    m_predictor->recordSequence(userInput);

    QString fullResponse = displayResponse;
    if (!fileReport.isEmpty()) {
        fullResponse += QStringLiteral("\n\n") + fileReport;
    }

    // Smart response: add a human-like summary for long/complex responses
    if (displayResponse.length() > 500 && !displayResponse.contains(QStringLiteral("[FILE:"))) {
        QString summary;
        if (displayResponse.contains(QStringLiteral("```"))) {
            summary = m_uiEnglish ? QStringLiteral("Here's what I found — code example included below.")
                                  : QStringLiteral("Вот что нашёл — пример кода ниже.");
        } else if (fileReport.contains(QStringLiteral("✅"))) {
            int fileCount = fileReport.count(QStringLiteral("✅"));
            summary = m_uiEnglish ? QStringLiteral("Done. Applied changes to %1 file(s).").arg(fileCount)
                                  : QStringLiteral("Готово. Изменения применены к %1 файл(ам).").arg(fileCount);
        } else {
            summary = m_uiEnglish ? QStringLiteral("Here's a detailed answer to your question.")
                                  : QStringLiteral("Вот подробный ответ на твой вопрос.");
        }
        fullResponse = QStringLiteral("💡 ") + summary + QStringLiteral("\n\n") + fullResponse;
    }

    emit asyncResponseReady(fullResponse);

    if (hadAttachments) {
        emit attachmentsConsumed();
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
            return QStringLiteral("API key already set. To replace: apikey <new-key>");
        }
        return QStringLiteral("I need a key to think. Usage: apikey <your-anthropic-api-key>");
    }

    m_claudeApi->setApiKey(key);
    return QStringLiteral("API key locked in. Claude API connected — at your service.");
}

QString Jarvis::cmdRememberFact(const QString& input)
{
    QString arg = extractArg(input, {QStringLiteral("запомни "),
                                      QStringLiteral("remember ")});
    if (arg.trimmed().length() < 3)
        return m_uiEnglish ? QStringLiteral("What should I remember?")
                           : QStringLiteral("Что запомнить?");

    // Legacy key=value still works
    int eqPos = arg.indexOf(QChar('='));
    if (eqPos > 0) {
        QString key   = arg.left(eqPos).trimmed();
        QString value = arg.mid(eqPos + 1).trimmed();
        m_memory->rememberFact(key, value);
        m_activity->learnFact(m_currentUserId, QStringLiteral("preference"),
                              key, value, 0.9f);
        return (m_uiEnglish ? QStringLiteral("Noted: ") : QStringLiteral("Запомнил: "))
               + key + QStringLiteral(" = ") + value;
    }

    // Natural language: store as both legacy fact and knowledge_base entry
    QString key = arg.left(50).simplified();
    m_memory->rememberFact(key, arg);
    m_activity->learnFact(m_currentUserId, QStringLiteral("preference"),
                          key, arg.trimmed(), 0.9f);
    return (m_uiEnglish ? QStringLiteral("Got it. I'll remember: ")
                        : QStringLiteral("Понял. Запомнил: ")) + arg.trimmed();
}

QString Jarvis::cmdRecallFact(const QString& input)
{
    QString key = extractArg(input, {QStringLiteral("вспомни "),
                                      QStringLiteral("recall ")});
    if (key.isEmpty()) {
        return QStringLiteral("What should I recall? Usage: recall <key>");
    }

    QString value = m_memory->recallFact(key);
    if (value.isEmpty()) {
        return QStringLiteral("Nothing on record for '") + key + QStringLiteral("'.");
    }
    return key + QStringLiteral(": ") + value;
}

QString Jarvis::cmdShowMemory(const QString&)
{
    const bool en = m_uiEnglish;
    auto& db = DatabaseManager::instance();
    QString text;

    // --- Knowledge Base (autonomously learned) ---
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(R"(SELECT category, key, value, confidence, reinforcements,
                        last_seen
                 FROM knowledge_base
                 WHERE user_id = :uid AND confidence >= 0.3
                 ORDER BY confidence DESC, reinforcements DESC
                 LIMIT 40)");
    q.bindValue(":uid", m_currentUserId);

    QMap<QString, QStringList> byCategory;
    int totalKb = 0;
    if (q.exec()) {
        while (q.next()) {
            const QString cat  = q.value(0).toString();
            const QString key  = q.value(1).toString();
            const QString val  = q.value(2).toString();
            const float  conf  = q.value(3).toFloat();
            const int   reinf  = q.value(4).toInt();

            QString entry = val;
            if (key != val && !key.isEmpty() && key.length() < 40)
                entry = key + QStringLiteral(": ") + val;
            if (conf >= 0.8f)
                entry += QStringLiteral("  ★");
            else if (reinf >= 3)
                entry += QStringLiteral("  ×") + QString::number(reinf);

            byCategory[cat].append(entry);
            ++totalKb;
        }
    }

    if (totalKb > 0) {
        text += en ? QStringLiteral("🧠 Learned Knowledge (%1 facts):\n")
                   : QStringLiteral("🧠 Изученные факты (%1 записей):\n");
        text = text.arg(totalKb);

        static const QMap<QString, QString> catLabelsEn = {
            {QStringLiteral("tool"),        QStringLiteral("Tools & Tech")},
            {QStringLiteral("skill"),       QStringLiteral("Skills")},
            {QStringLiteral("project"),     QStringLiteral("Projects")},
            {QStringLiteral("role"),        QStringLiteral("Role")},
            {QStringLiteral("preference"),  QStringLiteral("Preferences")},
            {QStringLiteral("environment"), QStringLiteral("Environment")},
            {QStringLiteral("workflow"),    QStringLiteral("Workflow")},
            {QStringLiteral("personal"),    QStringLiteral("Personal")},
            {QStringLiteral("habit"),       QStringLiteral("Habits")},
        };
        static const QMap<QString, QString> catLabelsRu = {
            {QStringLiteral("tool"),        QStringLiteral("Инструменты")},
            {QStringLiteral("skill"),       QStringLiteral("Навыки")},
            {QStringLiteral("project"),     QStringLiteral("Проекты")},
            {QStringLiteral("role"),        QStringLiteral("Роль")},
            {QStringLiteral("preference"),  QStringLiteral("Предпочтения")},
            {QStringLiteral("environment"), QStringLiteral("Среда")},
            {QStringLiteral("workflow"),    QStringLiteral("Рабочий процесс")},
            {QStringLiteral("personal"),    QStringLiteral("Личное")},
            {QStringLiteral("habit"),       QStringLiteral("Привычки")},
        };
        const auto& labels = en ? catLabelsEn : catLabelsRu;

        for (auto it = byCategory.constBegin(); it != byCategory.constEnd(); ++it) {
            const QString label = labels.value(it.key(), it.key());
            text += QStringLiteral("\n  [") + label + QStringLiteral("]\n");
            for (const auto& fact : it.value())
                text += QStringLiteral("    • ") + fact + QStringLiteral("\n");
        }
    }

    // --- Legacy manual facts (memory_kv) ---
    QJsonObject manualFacts = m_memory->allFacts();
    if (!manualFacts.isEmpty()) {
        text += en ? QStringLiteral("\n📌 Pinned Notes:\n")
                   : QStringLiteral("\n📌 Закреплённые заметки:\n");
        for (auto it = manualFacts.begin(); it != manualFacts.end(); ++it)
            text += QStringLiteral("    • ") + it.key() + QStringLiteral(": ")
                  + it.value().toString() + QStringLiteral("\n");
    }

    // --- Stats ---
    text += QStringLiteral("\n");
    text += (en ? QStringLiteral("📊 Sessions recorded: ")
                : QStringLiteral("📊 Сессий в памяти: "))
          + QString::number(m_memory->pastSessionSummaries().size())
          + QStringLiteral("\n");
    text += (en ? QStringLiteral("💬 Messages this session: ")
                : QStringLiteral("💬 Сообщений за сессию: "))
          + QString::number(m_memory->messageCount());

    if (text.trimmed().isEmpty() || totalKb == 0) {
        text = en ? QStringLiteral("🧠 Memory is building up. Keep chatting — "
                                   "I learn your tools, preferences, and projects "
                                   "from our conversations automatically.")
                  : QStringLiteral("🧠 Память наполняется. Продолжай общаться — "
                                   "я запоминаю твои инструменты, предпочтения и проекты "
                                   "из наших разговоров автоматически.");
    }

    return text.trimmed();
}

QString Jarvis::cmdShowStats(const QString&)
{
    QJsonObject stats = m_memory->commandStats();
    if (stats.isEmpty()) {
        return QStringLiteral("No stats yet. Start giving me orders.");
    }

    QVector<QPair<QString, int>> sorted;
    for (auto it = stats.begin(); it != stats.end(); ++it) {
        sorted.append({it.key(), it.value().toInt()});
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    QString text = QStringLiteral("Command usage:\n");
    int shown = 0;
    for (const auto& [cmd, count] : sorted) {
        text += QStringLiteral("• ") + cmd + QStringLiteral(": ")
              + QString::number(count) + QStringLiteral("x\n");
        if (++shown >= 15) break;
    }

    text += QStringLiteral("\nTotal this session: ")
          + QString::number(m_memory->taskContext().commandCount)
          + QStringLiteral(" commands");

    auto suggestions = m_predictor->suggest(3);
    if (!suggestions.isEmpty()) {
        text += QStringLiteral("\n\nSuggestions:");
        for (const auto& s : suggestions) {
            text += QStringLiteral("\n  → ") + s.description
                  + QStringLiteral(" (") + QString::number(int(s.confidence * 100))
                  + QStringLiteral("%)");
        }
    }

    return text.trimmed();
}

// ============================================================
// Профиль предпочтений (обучение паттернов/сценариев)
// ============================================================

QString Jarvis::cmdShowProfile(const QString&)
{
    return QStringLiteral("=== Preference Profile ===\n")
         + m_profile->buildProfileSummary(8)
         + QStringLiteral("\n\nThis profile updates automatically with every interaction. "
                          "I adapt to your real workflow over time. "
                          "Old patterns fade — only what matters sticks.");
}

// ============================================================
// Обновление
// ============================================================

QString Jarvis::cmdCheckUpdate(const QString&)
{
    m_updater->checkForUpdates(false);
    return QStringLiteral("Current version: ")
           + QCoreApplication::applicationVersion()
           + QStringLiteral("\nChecking for updates...");
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
            return QStringLiteral("Point me to your project: index C:\\Projects\\MyGame");
        }
        m_indexer->indexProject();
        syncProjectInfoToMemory();
        return QStringLiteral("Re-indexing ") + m_indexer->projectRoot()
             + QStringLiteral("...\nFiles: ") + QString::number(m_indexer->fileCount())
             + QStringLiteral(", Symbols: ") + QString::number(m_indexer->symbolCount());
    }

    path = path.replace(QChar('/'), QChar('\\'));

    if (!QDir(path).exists()) {
        return QStringLiteral("Folder not found: ") + path;
    }

    m_indexer->setProjectRoot(path);
    m_indexer->indexProject();
    m_indexer->enableFileWatcher(true);
    syncProjectInfoToMemory();

    return QStringLiteral("Project indexed: ") + path
         + QStringLiteral("\nFiles: ") + QString::number(m_indexer->fileCount())
         + QStringLiteral(", Symbols: ") + QString::number(m_indexer->symbolCount())
         + QStringLiteral("\n\nFile watcher active — I'll track changes automatically.");
}

// ============================================================
// IDE-агент: явная команда "проект в <ide>"
// ============================================================

QString Jarvis::cmdOpenProjectIDE(const QString& input)
{
    if (m_indexer->projectRoot().isEmpty()) {
        return QStringLiteral("No project open. Use: index <path>");
    }

    QString ide = extractArg(input, {QStringLiteral("проект в "),
                                      QStringLiteral("project in ")});
    ide = ide.trimmed();
    if (ide.isEmpty()) ide = QStringLiteral("clion");

    const QString msg = openProjectInIDE(ide);
    return msg.isEmpty()
         ? QStringLiteral("Can't identify IDE: ") + ide
         : msg;
}

QString Jarvis::cmdFindSymbol(const QString& input)
{
    QString name = extractArg(input, {QStringLiteral("найди символ "),
                                       QStringLiteral("find "),
                                       QStringLiteral("символ "),
                                       QStringLiteral("symbol ")});
    if (name.isEmpty()) {
        return QStringLiteral("Give me a name: symbol SpawnEnemy");
    }

    if (m_indexer->fileCount() == 0) {
        return QStringLiteral("No project indexed. Use: index <path>");
    }

    auto results = m_indexer->findSymbol(name);
    if (results.isEmpty()) {
        return QStringLiteral("Symbol '") + name + QStringLiteral("' not found.");
    }

    QString text = QStringLiteral("Found ") + QString::number(results.size())
                 + QStringLiteral(" results:\n\n");

    int shown = 0;
    for (const auto& sym : results) {
        text += QStringLiteral("• ") + sym.kindToString() + QStringLiteral(" ");
        if (!sym.parentClass.isEmpty()) {
            text += sym.parentClass + QStringLiteral("::");
        }
        text += sym.name;
        text += QStringLiteral("\n  File: ") + m_indexer->projectRoot()
              + QStringLiteral("/") + sym.filePath;
        text += QStringLiteral(", line ") + QString::number(sym.lineStart);
        if (!sym.brief.isEmpty()) {
            text += QStringLiteral("\n  ") + sym.brief;
        }
        text += QStringLiteral("\n");

        if (++shown >= 10) {
            text += QStringLiteral("\n... and ") + QString::number(results.size() - 10)
                  + QStringLiteral(" more results");
            break;
        }
    }

    return text.trimmed();
}

QString Jarvis::cmdProjectMap(const QString&)
{
    if (m_indexer->fileCount() == 0) {
        return QStringLiteral("No project indexed.");
    }

    QString map = m_indexer->projectMap();

    if (map.length() > 3000) {
        map = map.left(3000) + QStringLiteral("\n\n... (truncated, total: ")
            + QString::number(m_indexer->fileCount()) + QStringLiteral(" files, ")
            + QString::number(m_indexer->symbolCount()) + QStringLiteral(" symbols)");
    }

    return map;
}

QString Jarvis::cmdGrep(const QString& input)
{
    QString pattern = extractArg(input, {QStringLiteral("grep "),
                                          QStringLiteral("поиск ")});
    if (pattern.isEmpty()) {
        return QStringLiteral("What are we searching for? Usage: grep SpawnEnemy");
    }

    if (m_indexer->fileCount() == 0) {
        return QStringLiteral("No project indexed.");
    }

    auto results = m_indexer->grep(pattern, 20);
    if (results.isEmpty()) {
        return QStringLiteral("No matches for '") + pattern + QStringLiteral("'.");
    }

    QString text = QStringLiteral("Found ") + QString::number(results.size())
                 + QStringLiteral(" matches:\n\n");

    for (const auto& r : results) {
        text += r.filePath + QStringLiteral(":") + QString::number(r.line)
              + QStringLiteral("  ") + r.lineText + QStringLiteral("\n");
    }

    return text.trimmed();
}

// ============================================================
// Мультиагентный режим
// ============================================================

void Jarvis::setMultiAgentMode(bool enabled)
{
    m_multiAgentMode = enabled;
}

// ============================================================
// Мультиагентный роутинг
// ============================================================

bool Jarvis::routeToClaude(const QString& input, const QString& attachmentBlock) const
{
    // Возвращает true  → запрос идёт в Claude (код, файлы, сложные задачи)
    // Возвращает false → запрос идёт в Ollama/Gemini (болталка, простые вопросы)
    //
    // Принцип: по умолчанию → лёгкая модель.
    // В Claude только если явно нужен код/анализ/файлы.

    // Прикреплены файлы → Claude (умеет читать/анализировать)
    if (!attachmentBlock.isEmpty()) return true;

    const QString trimmed = input.trimmed();
    const QString lower   = trimmed.toLower();

    // Короткая чистая арифметика ("2+2", "15 * 3", "100/4") — точно не Claude.
    static const QRegularExpression arithmeticRe(
        R"(^\s*\d+(?:[.,]\d+)?\s*[+\-*/xх×÷]\s*\d+(?:[.,]\d+)?\s*=?\s*\??\s*$)");
    if (arithmeticRe.match(trimmed).hasMatch()) return false;

    // Короткие приветствия/реплики "точное совпадение" — никогда не Claude,
    // даже если где-то совпадут по подстроке с кодовым сигналом.
    static const QSet<QString> trivialExact = {
        QStringLiteral("привет"), QStringLiteral("здравствуй"), QStringLiteral("здравствуйте"),
        QStringLiteral("хай"),    QStringLiteral("йо"),        QStringLiteral("приветик"),
        QStringLiteral("hi"),     QStringLiteral("hello"),     QStringLiteral("hey"),
        QStringLiteral("yo"),     QStringLiteral("sup"),       QStringLiteral("howdy"),
        QStringLiteral("hiya"),   QStringLiteral("heya"),      QStringLiteral("greetings"),
        QStringLiteral("good morning"), QStringLiteral("morning"),
        QStringLiteral("good evening"), QStringLiteral("evening"),
        QStringLiteral("good day"),     QStringLiteral("good night"),
        QStringLiteral("как дела"), QStringLiteral("как ты"),  QStringLiteral("что нового"),
        QStringLiteral("how are you"), QStringLiteral("whats up"), QStringLiteral("what's up"),
        QStringLiteral("спасибо"), QStringLiteral("благодарю"), QStringLiteral("thanks"),
        QStringLiteral("thank you"), QStringLiteral("thx"),    QStringLiteral("ty"),
        QStringLiteral("ок"), QStringLiteral("окей"),
        QStringLiteral("ok"), QStringLiteral("okay"), QStringLiteral("got it"),
        QStringLiteral("да"), QStringLiteral("нет"),
        QStringLiteral("пока"), QStringLiteral("bye"), QStringLiteral("goodbye"),
        QStringLiteral("sorry"), QStringLiteral("извини"),     QStringLiteral("прости"),
    };
    if (trivialExact.contains(lower)) return false;

    // Явный код-запрос → Claude
    if (isCodingIntent(input)) return true;

    // Явные сигналы что нужен Claude (код, архитектура, анализ)
    static const QStringList claudeSignals = {
        // Русские
        QStringLiteral("напиши код"),    QStringLiteral("напиши функцию"),
        QStringLiteral("отладь"),        QStringLiteral("рефактор"),
        QStringLiteral("баг"),           QStringLiteral("ошибка в коде"),
        QStringLiteral("проанализируй"), QStringLiteral("архитектур"),
        QStringLiteral("оптимизируй"),   QStringLiteral("реализуй"),
        QStringLiteral("вайбкод"),       QStringLiteral("напиши класс"),
        QStringLiteral("напиши метод"),  QStringLiteral("дай полный файл"),
        QStringLiteral("прочитай файл"), QStringLiteral("посмотри файл"),
        QStringLiteral("сгенерируй код"),QStringLiteral("исправь код"),
        QStringLiteral("проверь код"),   QStringLiteral("ревью кода"),
        // English
        QStringLiteral("write code"),    QStringLiteral("write a function"),
        QStringLiteral("debug"),         QStringLiteral("refactor"),
        QStringLiteral("implement"),     QStringLiteral("analyze"),
        QStringLiteral("architecture"),  QStringLiteral("optimize"),
        QStringLiteral("write a class"), QStringLiteral("read the file"),
        QStringLiteral("vibecod"),       QStringLiteral("code review"),
        QStringLiteral("generate code"), QStringLiteral("fix the code"),
    };
    for (const auto& sig : claudeSignals) {
        if (lower.contains(sig)) return true;
    }

    // Длинные содержательные сообщения (>120 символов) часто требуют
    // более сильной модели — отдаём Claude, чтобы не разочаровать
    // пользователя слабым ответом от Ollama на сложный вопрос.
    if (trimmed.length() > 120) return true;

    // Всё остальное (короткие приветствия, простые вопросы, беседа) → Ollama/Gemini
    return false;
}

// ============================================================
// Gemini API-ключ
// ============================================================

QString Jarvis::cmdSetGeminiKey(const QString& input)
{
    // Переиспользован под управление Ollama-моделью
    QString model = extractArg(input, {QStringLiteral("ollamamodel "),
                                        QStringLiteral("модель ")});
    if (model.isEmpty()) {
        return QStringLiteral("Current Ollama model: ") + m_geminiApi->model()
             + QStringLiteral("\nTo switch: ollamamodel <name>\n"
               "Available models: ollama list (in terminal)");
    }
    m_geminiApi->setModel(model);
    return QStringLiteral("Ollama model set: ") + model;
}

// ============================================================
// Справка
// ============================================================

QString Jarvis::cmdHelp(const QString&)
{
    auto S = [](const QString& icon, const QString& title, const QString& body) {
        return QStringLiteral(
            "<div style='margin:6px 0; padding:12px 16px; "
            "background:rgba(102,252,241,0.03); border:1px solid rgba(102,252,241,0.10); "
            "border-radius:10px;'>"
            "<b style='font-size:14px; color:#66FCF1;'>%1 %2</b><br>"
            "<span style='font-size:12px; line-height:1.7; color:#C5C6C7;'>%3</span>"
            "</div>"
        ).arg(icon, title, body);
    };

    auto row = [](const QString& cmd, const QString& desc) {
        return QStringLiteral(
            "<tr><td style='padding:2px 12px 2px 0; color:#66FCF1; font-family:Consolas,monospace; "
            "white-space:nowrap;'>%1</td>"
            "<td style='padding:2px 0; color:#C5C6C7;'>%2</td></tr>"
        ).arg(cmd, desc);
    };

    QString h;
    h += QStringLiteral("<div style='padding:4px 0;'>"
         "<b style='font-size:18px; color:#66FCF1; letter-spacing:2px;'>"
         "J.A.R.V.I.S. — Complete User Guide</b></div>");

    // ── 1. Voice ──
    h += S(QStringLiteral("🎙"), QStringLiteral("Voice Input & Wake Word"),
        QStringLiteral(
        "JARVIS uses <b>Vosk</b> for fully offline speech recognition (no internet needed).<br><br>"
        "<b>Activation:</b> Click the mic button in the input bar, or simply say "
        "<b>\"Jarvis\"</b> (or \"Джарвис\") — the wake word triggers hands-free listening.<br><br>"
        "<b>Supported languages:</b> English and Russian with automatic detection. "
        "The recognizer picks the highest-confidence result across loaded models.<br><br>"
        "<b>Whisper mode:</b> When ambient noise is detected and you speak softly, "
        "JARVIS enters whisper mode — recognition thresholds adapt automatically.<br><br>"
        "<b>Passive recording:</b> Enable via Settings menu to continuously transcribe "
        "background speech into the voice journal database for later analysis. "
        "Entries are stored locally and can be exported for AI training."));

    // ── 2. Audio ──
    h += S(QStringLiteral("🔊"), QStringLiteral("Audio Mixer & Sound Effects"),
        QStringLiteral(
        "Three audio modes, cycled by clicking the speaker icon in the bottom bar:<br><br>"
        "<table style='margin:4px 0;'>"
        "<tr><td style='padding:2px 10px 2px 0;'><b>🔊 Full Sound</b></td>"
        "<td>TTS speech + all UI sound effects (success chime, warning buzz, listening ping).</td></tr>"
        "<tr><td style='padding:2px 10px 2px 0;'><b>🔕 Mute Speech</b></td>"
        "<td>JARVIS does not speak aloud, but notification/error sounds remain active.</td></tr>"
        "<tr><td style='padding:2px 10px 2px 0;'><b>🔇 Mute All</b></td>"
        "<td>Complete silence — both TTS and UI effects globally suppressed.</td></tr>"
        "</table><br>"
        "Sound effects are procedurally generated at startup (no external audio files). "
        "TTS uses the Windows SAPI engine with the system default voice."));

    // ── 3. Adaptive Focus ──
    h += S(QStringLiteral("🧠"), QStringLiteral("Adaptive Persona / Focus Subsystem"),
        QStringLiteral(
        "JARVIS continuously reads the <b>top 10 time-decay-scored events</b> from the "
        "Core Memory Stream and classifies your current workflow into one of four focus states:<br><br>"
        "<table style='margin:4px 0;'>"
        "<tr><td style='padding:2px 10px 2px 0; color:#66FCF1;'><b>Developer</b></td>"
        "<td>Detected when recent queries mention code, functions, bugs, files, or build systems. "
        "JARVIS becomes terse and technical — prioritizes code snippets and debugging strategies.</td></tr>"
        "<tr><td style='padding:2px 10px 2px 0; color:#66FCF1;'><b>Creative</b></td>"
        "<td>Activated by art, design, texture, or rendering keywords. "
        "Shifts to collaborative brainstorming and visual thinking.</td></tr>"
        "<tr><td style='padding:2px 10px 2px 0; color:#66FCF1;'><b>Admin</b></td>"
        "<td>Triggered by file management, settings, installation, or backup queries. "
        "Provides concise step-by-step instructions.</td></tr>"
        "<tr><td style='padding:2px 10px 2px 0; color:#66FCF1;'><b>Casual</b></td>"
        "<td>Default when no strong keyword pattern is detected. "
        "Relaxed, witty JARVIS personality — short answers unless depth is requested.</td></tr>"
        "</table><br>"
        "<b>How it works:</b> Each user message is logged to the <b>memory_stream</b> table with an "
        "importance score (0.7 for coding intents, 0.4 for casual). The time-decay formula "
        "<code style='color:#45A29E;'>Score = importance / (1 + hours_elapsed)</code> ranks recent "
        "high-importance events above stale ones. The top 10 are keyword-scanned to select the focus, "
        "which injects an adaptive instruction block into the system prompt before every LLM call.<br><br>"
        "This happens silently — no user action is needed. The focus shifts as your workflow changes."));

    // ── 4. AI Training ──
    h += S(QStringLiteral("⚡"), QStringLiteral("AI Training & Dataset Mechanics"),
        QStringLiteral(
        "<b>Automatic collection:</b> Every user-AI exchange is saved to <b>training_logs</b> "
        "with <code>rating=0</code>. When you click the <b>👍</b> Like button, the rating updates "
        "to <code>1</code>, marking it as a high-quality pair for fine-tuning.<br><br>"
        "<b>Behavior patterns:</b> The <b>BackgroundLearner</b> analyzes chat history to build "
        "<b>behavior_patterns</b> — if JARVIS sees a similar question 3+ times with consistent "
        "answers, it can respond locally without an API call (offline brain).<br><br>"
        "<b>Voice journal:</b> Passive listening captures ambient speech transcriptions into "
        "<b>voice_journal</b> with language and confidence scores. Entries marked "
        "'Pending processing' haven't been analyzed yet. Processed entries feed into "
        "the knowledge base.<br><br>"
        "<b>Export:</b> Use Settings → Training → Export .jsonl to create an Alpaca/Unsloth-format "
        "dataset from your liked responses for local model fine-tuning.<br><br>"
        "<b>Response cache:</b> Jokes, advice, facts, and other cached responses are stored in "
        "<b>response_cache</b> — once Claude generates one, JARVIS serves it offline next time."));

    // ── 5. Commands ──
    h += S(QStringLiteral("⌨"), QStringLiteral("Complete Command Reference"),
        QStringLiteral(
        "<table style='border-collapse:collapse; width:100%;'>"
        "<tr><td colspan='2' style='padding:4px 0; color:#45A29E; font-weight:bold;'>"
        "Memory & Knowledge</td></tr>")
        + row(QStringLiteral("remember key=value"), QStringLiteral("Permanently store a fact"))
        + row(QStringLiteral("recall key"), QStringLiteral("Retrieve a stored fact"))
        + row(QStringLiteral("memory"), QStringLiteral("List all stored facts"))
        + row(QStringLiteral("profile"), QStringLiteral("Show learned work patterns and scenarios"))
        + row(QStringLiteral("stats"), QStringLiteral("Display command usage frequency"))
        + QStringLiteral(
        "<tr><td colspan='2' style='padding:6px 0 4px; color:#45A29E; font-weight:bold;'>"
        "Project & Code Intelligence</td></tr>")
        + row(QStringLiteral("index &lt;path&gt;"), QStringLiteral("Index a project folder for RAG code context"))
        + row(QStringLiteral("symbol &lt;name&gt;"), QStringLiteral("Search for classes, functions, or variables"))
        + row(QStringLiteral("grep &lt;text&gt;"), QStringLiteral("Full-text search across all project files"))
        + QStringLiteral(
        "<tr><td colspan='2' style='padding:6px 0 4px; color:#45A29E; font-weight:bold;'>"
        "System & Input Control</td></tr>")
        + row(QStringLiteral("type &lt;text&gt;"), QStringLiteral("Type text into the active window via virtual keyboard"))
        + row(QStringLiteral("press &lt;key&gt;"), QStringLiteral("Press a single key (Enter, F5, Escape, etc.)"))
        + row(QStringLiteral("combo &lt;keys&gt;"), QStringLiteral("Press a key combination (Ctrl+S, Alt+Tab, etc.)"))
        + row(QStringLiteral("apikey &lt;key&gt;"), QStringLiteral("Set your Claude API key"))
        + row(QStringLiteral("help"), QStringLiteral("Show this guide"))
        + QStringLiteral(
        "<tr><td colspan='2' style='padding:6px 0 4px; color:#45A29E; font-weight:bold;'>"
        "Screen & Vision</td></tr>")
        + row(QStringLiteral("\"what do you see\""),
              QStringLiteral("Capture screen and describe it via Claude Vision"))
        + row(QStringLiteral("\"click on X\""),
              QStringLiteral("Locate element on screen and click it"))
        + QStringLiteral(
        "<tr><td colspan='2' style='padding:6px 0 4px; color:#45A29E; font-weight:bold;'>"
        "Conversation</td></tr>")
        + row(QStringLiteral("Any question"),
              QStringLiteral("Routes to Claude (code/complex) or Ollama/Gemini (casual)"))
        + row(QStringLiteral("\"recall what happened...\""),
              QStringLiteral("Search session journal by date or topic"))
        + QStringLiteral("</table>"));

    // ── 6. Offline/Online ──
    h += S(QStringLiteral("🌐"), QStringLiteral("Offline & Online Integration"),
        QStringLiteral(
        "<b>Always offline (no internet):</b><br>"
        "• Voice recognition (Vosk) • Behavior pattern matching • Response cache • "
        "Virtual keyboard • PC control commands • Activity tracking<br><br>"
        "<b>Requires internet:</b><br>"
        "• Claude API (code analysis, complex reasoning) • Gemini API (conversational fallback) • "
        "Auto-updater (GitHub Releases) • Screenshot Vision analysis<br><br>"
        "<b>Optional local LLM (Ollama):</b><br>"
        "Enable Agent Mode in Settings to route casual conversation through a local model "
        "(Llama 3, Mistral, etc.) — completely free and private. Claude remains the fallback "
        "for complex tasks. Set your model with <code style='color:#45A29E;'>ollamamodel &lt;name&gt;</code>."));

    // ── 7. Keyboard shortcuts ──
    h += S(QStringLiteral("⌘"), QStringLiteral("Keyboard Shortcuts"),
        QStringLiteral(
        "<table style='margin:4px 0;'>")
        + row(QStringLiteral("Enter"), QStringLiteral("Send message"))
        + row(QStringLiteral("Ctrl+O"), QStringLiteral("Attach files"))
        + row(QStringLiteral("Drag & Drop"), QStringLiteral("Drop files into the window to attach"))
        + row(QStringLiteral("Ctrl+C"), QStringLiteral("Copy selected text from chat log"))
        + QStringLiteral("</table>"));

    if (m_indexer->fileCount() > 0) {
        h += S(QStringLiteral("📁"), QStringLiteral("Active Project"),
            QStringLiteral("<b>%1</b> — %2 files, %3 symbols indexed<br>"
                           "Code fragments auto-attach to coding queries. "
                           "Ask JARVIS to explain, refactor, or extend any part of the project.")
                .arg(QFileInfo(m_indexer->projectRoot()).fileName())
                .arg(m_indexer->fileCount())
                .arg(m_indexer->symbolCount()));
    }

    return h;
}

// ============================================================
// Виртуальная клавиатура
// ============================================================

QString Jarvis::cmdTypeText(const QString& input)
{
    QString text = extractArg(input, {QStringLiteral("напечатай "),
                                      QStringLiteral("type ")});
    if (text.isEmpty()) {
        return QStringLiteral("What should I type? Provide the text.");
    }

    m_keyEmulator->pressCombo({VK_MENU, VK_TAB});
    QThread::msleep(300);
    m_keyEmulator->typeText(text, 30);
    return QStringLiteral("Typing: ") + text;
}

QString Jarvis::cmdPressKey(const QString& input)
{
    QString keyName = extractArg(input, {QStringLiteral("нажми "),
                                         QStringLiteral("press ")});
    if (keyName.isEmpty()) {
        return QStringLiteral("Which key? Specify it.");
    }

    WORD vk = parseVirtualKey(keyName);
    if (vk == 0) {
        return QStringLiteral("Unknown key: ") + keyName;
    }

    m_keyEmulator->pressKey(vk);
    return QStringLiteral("Pressing: ") + keyName;
}

QString Jarvis::cmdCombo(const QString& input)
{
    QString comboStr = extractArg(input, {QStringLiteral("комбо "),
                                          QStringLiteral("combo ")});
    if (comboStr.isEmpty()) {
        return QStringLiteral("Specify the combo. Example: combo ctrl+c");
    }

    QStringList parts = comboStr.toLower().split(QStringLiteral("+"),
                                                  Qt::SkipEmptyParts);
    std::vector<WORD> keys;
    for (const auto& part : parts) {
        WORD vk = parseVirtualKey(part.trimmed());
        if (vk == 0) {
            return QStringLiteral("Unknown key: ") + part.trimmed();
        }
        keys.push_back(vk);
    }

    if (keys.empty()) {
        return QStringLiteral("Couldn't parse that combo.");
    }

    m_keyEmulator->pressCombo(
        std::initializer_list<WORD>(keys.data(), keys.data() + keys.size())
    );
    return QStringLiteral("Executing combo: ") + comboStr;
}

// ============================================================
// Task Manager slots
// ============================================================

qint64 Jarvis::addTask(const QString& title, const QString& category,
                        const QString& priority, const QDateTime& deadline)
{
    DbTask t;
    t.userId   = m_currentUserId;
    t.title    = title;
    t.category = category;
    t.status   = QStringLiteral("Todo");
    t.priority = priority;
    t.deadline = deadline;
    return DatabaseManager::instance().addTask(t);
}

bool Jarvis::updateTaskStatus(qint64 taskId, const QString& newStatus)
{
    auto task = DatabaseManager::instance().getTask(taskId);
    if (!task) return false;
    task->status = newStatus;
    return DatabaseManager::instance().updateTask(*task);
}

QString Jarvis::getOverdueTasksSummary() const
{
    return TaskNotifications::checkDeadlines(m_currentUserId, m_uiEnglish);
}