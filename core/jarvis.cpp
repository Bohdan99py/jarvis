// -------------------------------------------------------
// jarvis.cpp — Ядро J.A.R.V.I.S.: команды, TTS, мозги
// -------------------------------------------------------

#include "jarvis.h"
#include "virtual_keyboard.h"
#include "session_memory.h"
#include "claude_api.h"
#include "ollama_api.h"
#include "action_predictor.h"
#include "auto_updater.h"
#include "project_indexer.h"
#include "code_actions.h"
#include "attachments_manager.h"

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
    m_predictor    = new ActionPredictor(m_memory, this);
    m_keyEmulator  = new KeyEmulator(this);
    m_indexer      = new ProjectIndexer(this);
    m_codeActions  = new CodeActions(this);
    m_attachments  = new AttachmentsManager(this);

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
        QStringLiteral("apikey <ключ> — установить Claude API-ключ"),
        /*prefixMatch=*/true
    );

    m_registry.registerCommand(
        {QStringLiteral("ollamamodel "), QStringLiteral("модель ")},
        [this](const QString& s) { return cmdSetGeminiKey(s); },
        QStringLiteral("ollamamodel <имя> — выбрать модель Ollama (например: llama3, mistral)"),
        /*prefixMatch=*/true
    );

    // --- Индексация проекта (явный префикс) ---
    m_registry.registerCommand(
        {QStringLiteral("индекс "), QStringLiteral("index ")},
        [this](const QString& s) { return cmdIndexProject(s); },
        QStringLiteral("индекс <путь> — индексировать C++ проект"),
        /*prefixMatch=*/true
    );

    // --- Поиск по индексу (явный префикс) ---
    m_registry.registerCommand(
        {QStringLiteral("символ "), QStringLiteral("symbol ")},
        [this](const QString& s) { return cmdFindSymbol(s); },
        QStringLiteral("символ <имя> — найти класс/функцию в индексе"),
        /*prefixMatch=*/true
    );

    m_registry.registerCommand(
        {QStringLiteral("grep ")},
        [this](const QString& s) { return cmdGrep(s); },
        QStringLiteral("grep <текст> — поиск текста в файлах проекта"),
        /*prefixMatch=*/true
    );

    // --- Память (явный префикс) ---
    m_registry.registerCommand(
        {QStringLiteral("запомни "), QStringLiteral("remember ")},
        [this](const QString& s) { return cmdRememberFact(s); },
        QStringLiteral("запомни ключ=значение — сохранить факт"),
        /*prefixMatch=*/true
    );

    m_registry.registerCommand(
        {QStringLiteral("вспомни "), QStringLiteral("recall ")},
        [this](const QString& s) { return cmdRecallFact(s); },
        QStringLiteral("вспомни <ключ> — вспомнить факт"),
        /*prefixMatch=*/true
    );

    // --- Виртуальная клавиатура (явный префикс) ---
    m_registry.registerCommand(
        {QStringLiteral("напечатай "), QStringLiteral("type ")},
        [this](const QString& s) { return cmdTypeText(s); },
        QStringLiteral("напечатай <текст> — набрать в активном окне"),
        /*prefixMatch=*/true
    );

    m_registry.registerCommand(
        {QStringLiteral("нажми "), QStringLiteral("press ")},
        [this](const QString& s) { return cmdPressKey(s); },
        QStringLiteral("нажми <клавиша> — нажать клавишу"),
        /*prefixMatch=*/true
    );

    m_registry.registerCommand(
        {QStringLiteral("комбо "), QStringLiteral("combo ")},
        [this](const QString& s) { return cmdCombo(s); },
        QStringLiteral("комбо <ctrl+c> — нажать комбинацию клавиш"),
        /*prefixMatch=*/true
    );

    // --- Информация (точное совпадение одного слова) ---
    m_registry.registerCommand(
        {QStringLiteral("память"), QStringLiteral("memory")},
        [this](const QString& s) { return cmdShowMemory(s); },
        QStringLiteral("память — показать сохранённые факты"),
        /*prefixMatch=*/false
    );

    m_registry.registerCommand(
        {QStringLiteral("статистика"), QStringLiteral("stats")},
        [this](const QString& s) { return cmdShowStats(s); },
        QStringLiteral("статистика — частота использования команд"),
        /*prefixMatch=*/false
    );

    m_registry.registerCommand(
        {QStringLiteral("помощь"), QStringLiteral("help")},
        [this](const QString&) { return cmdHelp(QString()); },
        QStringLiteral("помощь — этот список"),
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
    constexpr int MAX_FILE_CHARS     = 14000;
    constexpr int MAX_TOTAL_CHARS    = 32000;
    constexpr int MAX_SYMBOL_MATCHES = 8;
    constexpr int MAX_GREP_HITS      = 10;

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

    for (const QString& rel : pickedFiles) {
        if (budget <= 0) break;
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

    if (budget > 1000 && !symbolHits.isEmpty()) {
        appendAndTrim(QStringLiteral("\n### Найденные символы:\n"));
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
        QString block = QStringLiteral("\n### Совпадения grep:\n");
        for (const auto& h : grepHits) {
            block += h.filePath + QStringLiteral(":") + QString::number(h.line)
                   + QStringLiteral("  ") + h.lineText.left(200) + QStringLiteral("\n");
        }
        appendAndTrim(block);
    }

    if (pickedFiles.isEmpty() && symbolHits.isEmpty() && grepHits.isEmpty()) {
        context += QStringLiteral(
            "\n(Автопоиск не нашёл прямых совпадений. "
            "Если пользователь прикрепил файлы «скрепкой» — опирайся на них. "
            "Если и прикреплений нет — попроси пользователя указать конкретный файл.)\n");
    }

    context += QStringLiteral("--- Конец контекста из проекта ---\n");
    return context;
}

// ============================================================
// TTS
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

QString Jarvis::processCommand(const QString& input, const QString& attachmentBlock, const QString& langInstruction)
{
    QString s = input.trimmed();
    if (s.isEmpty()) return QString();

    m_memory->addMessage(QStringLiteral("user"), s);

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

    // 2. Claude API — дефолт для всего остального.
    //    Brain уже определил намерение и при необходимости обогатил
    //    запрос суффиксом домена (например " в проекте").
    //    Здесь просто отправляем в API.
    if (!m_indexer->projectRoot().isEmpty()) {
        m_codeActions->setProjectRoot(m_indexer->projectRoot());
    }

    // Обогащение: автопоиск из индекса + прикрепления пользователя
    const QString projectContext = buildProjectContext(s);

    QString enrichedMessage = s;
    if (!projectContext.isEmpty()) {
        enrichedMessage += projectContext;
    }
    if (!attachmentBlock.isEmpty()) {
        enrichedMessage += attachmentBlock;
    }

    const bool hadAttachments = !attachmentBlock.isEmpty();

    // Языковая инструкция — добавляем в начало сообщения как скрытую системную заметку
    // Claude читает её и отвечает на нужном языке
    if (!langInstruction.isEmpty()) {
        enrichedMessage = QStringLiteral("[LANG_INSTRUCTION: ")
                        + langInstruction
                        + QStringLiteral("]\n\n")
                        + enrichedMessage;
    }

    // Мультиагентный роутинг
    if (m_multiAgentMode && !routeToClaude(s, attachmentBlock)) {
        emit agentSelected(QStringLiteral("🦙 Ollama (") + m_geminiApi->model() + QStringLiteral(")"));
        m_geminiApi->sendMessage(enrichedMessage,
                                 [this, s, enrichedMessage, hadAttachments](bool success, const QString& response) {
            if (success) {
                m_memory->addMessage(QStringLiteral("assistant"), response);
                m_memory->updateContext(s, response);
                m_predictor->recordSequence(s);
                emit asyncResponseReady(response);
            } else {
                // Ollama упала или недоступна — fallback на Claude
                emit agentSelected(QStringLiteral("🤖 Claude (fallback)"));
                m_claudeApi->sendMessage(enrichedMessage,
                                         [this, s, hadAttachments](bool ok, const QString& resp) {
                    if (ok) {
                        QString fileReport      = m_codeActions->processResponse(resp);
                        QString displayResponse = m_codeActions->cleanResponseForDisplay(resp);
                        m_memory->addMessage(QStringLiteral("assistant"), displayResponse);
                        m_memory->updateContext(s, displayResponse);
                        m_predictor->recordSequence(s);
                        QString full = displayResponse;
                        if (!fileReport.isEmpty()) full += QStringLiteral("\n\n") + fileReport;
                        emit asyncResponseReady(full);
                        if (hadAttachments) emit attachmentsConsumed();
                    } else {
                        emit asyncResponseError(resp);
                    }
                });
            }
        });
        return QString();
    }

    emit agentSelected(QStringLiteral("🤖 Claude"));
    m_claudeApi->sendMessage(enrichedMessage,
                             [this, s, hadAttachments](bool success, const QString& response) {
        if (success) {
            QString fileReport      = m_codeActions->processResponse(response);
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

            if (hadAttachments) {
                emit attachmentsConsumed();
            }
        } else {
            emit asyncResponseError(response);
        }
    });

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
        return QStringLiteral("Формат: запомни ключ=значение");
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
        return QStringLiteral("Статистика пуста.");
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
            return QStringLiteral("Укажите путь к проекту: индекс C:\\Projects\\MyGame");
        }
        m_indexer->indexProject();
        syncProjectInfoToMemory();
        return QStringLiteral("Переиндексирую ") + m_indexer->projectRoot()
             + QStringLiteral("...\nФайлов: ") + QString::number(m_indexer->fileCount())
             + QStringLiteral(", Символов: ") + QString::number(m_indexer->symbolCount());
    }

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
         + QStringLiteral("\n\nСлежение за изменениями включено.");
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
        return QStringLiteral("Проект не проиндексирован.");
    }

    QString map = m_indexer->projectMap();

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
        return QStringLiteral("Проект не проиндексирован.");
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
    if (!attachmentBlock.isEmpty()) return true;
    if (isCodingIntent(input))      return true;

    const QString lower = input.toLower();
    static const QStringList chatSignals = {
        QStringLiteral("расскажи"), QStringLiteral("что такое"),
        QStringLiteral("объясни"),  QStringLiteral("почему"),
        QStringLiteral("как дела"), QStringLiteral("привет"),
        QStringLiteral("what is"),  QStringLiteral("tell me"),
        QStringLiteral("explain"),  QStringLiteral("why "),
    };
    for (const auto& sig : chatSignals) {
        if (lower.contains(sig)) return false;
    }
    return true;
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
        return QStringLiteral("Текущая модель Ollama: ") + m_geminiApi->model()
             + QStringLiteral("\nДля смены: ollamamodel <имя>\n"
               "Доступные модели: ollama list (в терминале)");
    }
    m_geminiApi->setModel(model);
    return QStringLiteral("Модель Ollama установлена: ") + model;
}

// ============================================================
// Справка
// ============================================================

QString Jarvis::cmdHelp(const QString&)
{
    QString help = m_registry.helpText();
    help += QStringLiteral("\n\n— Свободный диалог —\n"
                           "Любой вопрос, задача или запрос → Claude API.\n"
                           "«Найди X», «открой Y», «объясни Z» — Brain сам разберётся.");
    help += QStringLiteral("\n\n— Прикрепление файлов —\n"
                           "Кнопка 📎 или перетащи файлы в окно.");
    if (m_indexer->fileCount() > 0) {
        help += QStringLiteral("\n\n— Проект «")
              + QFileInfo(m_indexer->projectRoot()).fileName()
              + QStringLiteral("» проиндексирован —\n")
              + QString::number(m_indexer->fileCount()) + QStringLiteral(" файлов, ")
              + QString::number(m_indexer->symbolCount()) + QStringLiteral(" символов");
    }
    return help;
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
        return QStringLiteral("Укажите клавишу.");
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
        return QStringLiteral("Укажите комбинацию.");
    }

    QStringList parts = comboStr.toLower().split(QStringLiteral("+"),
                                                  Qt::SkipEmptyParts);
    std::vector<WORD> keys;
    for (const auto& part : parts) {
        WORD vk = parseVirtualKey(part.trimmed());
        if (vk == 0) {
            return QStringLiteral("Неизвестная клавиша: ") + part.trimmed();
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