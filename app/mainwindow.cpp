// -------------------------------------------------------
// mainwindow.cpp — Главное окно J.A.R.V.I.S.
// -------------------------------------------------------

#include "mainwindow.h"
#include "jarvis.h"
#include "theme.h"
#include "virtual_keyboard.h"
#include "claude_api.h"
#include "auto_updater.h"
#include "project_indexer.h"
#include "session_memory.h"
#include "attachments_manager.h"
#include "lang.h"
#include "brain.h"
#include "search_router.h"
#include "fileviewer.h"
#include "ollama_api.h"
#include "gemini_api.h"
#include "learned_commands.h"
#include "screen_agent.h"
#include "bug_reporter.h"
#include "voice_input.h"
#include <QDesktopServices>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QKeyEvent>
#include <QScreen>
#include <QTime>
#include <QFont>
#include <QPropertyAnimation>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QFileDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QToolTip>
#include <QSettings>
#include <QCoreApplication>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QRegularExpression>

// ============================================================
// Конструктор
// ============================================================

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setAcceptDrops(true);

    // Загружаем язык из настроек (для UI-строк)
    QSettings cfg(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
    bool english = cfg.value(QStringLiteral("ui/english"), false).toBool();
    gUiLanguage() = english ? UiLanguage::English : UiLanguage::Russian;
    // Синхронизируем детектор языка с настройкой
    // (дефолт — русский, совпадает с конструктором LanguageDetector)

    m_jarvis = new Jarvis(this);

    connect(m_jarvis, &Jarvis::speakingChanged,
            this, &MainWindow::onSpeakingChanged);
    connect(m_jarvis->keyEmulator(), &KeyEmulator::typingStarted,
            this, &MainWindow::onTypingStarted);
    connect(m_jarvis->keyEmulator(), &KeyEmulator::typingProgress,
            this, &MainWindow::onTypingProgress);
    connect(m_jarvis->keyEmulator(), &KeyEmulator::typingFinished,
            this, &MainWindow::onTypingFinished);

    connect(m_jarvis, &Jarvis::asyncResponseReady,
            this, &MainWindow::onAsyncResponse);
    // Самообучение: после каждого ответа AI — пробуем запомнить команду
    connect(m_jarvis, &Jarvis::asyncResponseReady,
            this, [this](const QString& response) {
        if (m_learnedCmds && !m_lastUserInput.isEmpty()) {
            m_learnedCmds->learnFromApiResponse(m_lastUserInput, response, QString());
        }
    });
    connect(m_jarvis, &Jarvis::asyncResponseError,
            this, &MainWindow::onAsyncError);
    connect(m_jarvis, &Jarvis::suggestionAvailable,
            this, &MainWindow::onSuggestion);
    connect(m_jarvis, &Jarvis::agentSelected,
            this, &MainWindow::onAgentSelected);
    connect(m_jarvis, &Jarvis::ideOpened,
            this, [this](const QString& message) {
                appendLog(Str::logJarvis(), message, Theme::LogColors::system);
            });

    connect(m_jarvis->attachments(), &AttachmentsManager::changed,
            this, &MainWindow::onAttachmentsChanged);
    connect(m_jarvis, &Jarvis::attachmentsConsumed,
            this, &MainWindow::onAttachmentsConsumed);

    connect(m_jarvis->claudeApi(), &ClaudeApi::requestStarted,
            this, [this]() { setThinkingState(true); });
    connect(m_jarvis->claudeApi(), &ClaudeApi::requestFinished,
            this, [this]() { setThinkingState(false); });
    // Ollama: thinking state управляется через asyncResponseReady/asyncResponseError

    auto* updater = m_jarvis->autoUpdater();
    connect(updater, &AutoUpdater::updateAvailable,
            this, [this](const QString& newVersion, const QString&, const QUrl&) {
        showUpdateBar(newVersion);
    });
    connect(updater, &AutoUpdater::noUpdateAvailable,
            this, [this]() {
        appendLog(Str::logSystem(),
                  Str::updLatest() + QCoreApplication::applicationVersion() + QStringLiteral(")."),
                  Theme::LogColors::system);
    });
    connect(updater, &AutoUpdater::downloadProgress,
            this, [this](int percent) {
        m_status->setText(Str::statusDownload().arg(percent));
        if (m_updateProgress) {
            m_updateProgress->setValue(percent);
            m_updateProgress->setVisible(true);
        }
    });
    connect(updater, &AutoUpdater::downloadFinished,
            this, [this](const QString&) {
        appendLog(Str::logSystem(), Str::updDownloaded(), Theme::LogColors::system);
        hideUpdateBar();
    });
    connect(updater, &AutoUpdater::installerLaunched,
            this, [this]() {
        appendLog(Str::logSystem(),
                  IS_EN ? QStringLiteral("Installer launched. Closing in 3 seconds...")
                        : QStringLiteral("Установщик запущен. Закрываю через 3 секунды..."),
                  Theme::LogColors::system);
        QTimer::singleShot(3000, this, []() {
            QCoreApplication::quit();
        });
    });
    connect(updater, &AutoUpdater::updateError,
            this, [this](const QString& error) {
        appendLog(Str::logError(), error, Theme::LogColors::error);
    });

    buildUI();
    buildMenuBar();
    qApp->setStyleSheet(Theme::globalStyleSheet());

    // ── Системный трей ────────────────────────────────────
    QIcon trayIcon;
    if (!QIcon(QStringLiteral(":/jarvis.ico")).isNull())
        trayIcon = QIcon(QStringLiteral(":/jarvis.ico"));
    else if (!QIcon(QStringLiteral(":/jarvis.png")).isNull())
        trayIcon = QIcon(QStringLiteral(":/jarvis.png"));
    else
        trayIcon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);

    m_trayIcon = new QSystemTrayIcon(trayIcon, this);
    m_trayIcon->setToolTip(QStringLiteral("J.A.R.V.I.S. v")
                           + QCoreApplication::applicationVersion());

    auto* trayMenu = new QMenu(this);
    auto* actShow = trayMenu->addAction(
        IS_EN ? QStringLiteral("Show / Hide") : QStringLiteral("Показать / Скрыть"));
    connect(actShow, &QAction::triggered, this, [this]() {
        if (isVisible()) { hide(); }
        else             { show(); raise(); activateWindow(); }
    });
    trayMenu->addSeparator();
    auto* actQuit = trayMenu->addAction(
        IS_EN ? QStringLiteral("Quit") : QStringLiteral("Выход"));
    connect(actQuit, &QAction::triggered, qApp, &QApplication::quit);

    m_trayIcon->setContextMenu(trayMenu);
    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            if (isVisible()) { hide(); }
            else             { show(); raise(); activateWindow(); }
        }
    });

    // Приветствие
    int hour = QTime::currentTime().hour();
    QString g;
    if      (hour < 6)  g = Str::greetNight();
    else if (hour < 12) g = Str::greetMorning();
    else if (hour < 18) g = Str::greetDay();
    else                g = Str::greetEvening();

    appendLog(Str::logJarvis(),
              g + QStringLiteral("! ") + Str::greetReady()
              + QCoreApplication::applicationVersion(),
              Theme::LogColors::jarvis);

    if (m_jarvis->claudeApi()->hasApiKey()) {
        appendLog(Str::logJarvis(), Str::apiClaudeConnected(), Theme::LogColors::system);
    } else {
        appendLog(Str::logJarvis(), Str::apiNoKey(), Theme::LogColors::jarvis);
    }

    // Статус Gemini (встроенный fallback-ключ)
    if (m_jarvis->geminiBackup() && m_jarvis->geminiBackup()->hasApiKey()) {
        appendLog(Str::logSystem(),
                  IS_EN ? QStringLiteral("♊ Gemini ready (built-in key). "
                                         "Used as fallback when Ollama is unavailable.")
                        : QStringLiteral("♊ Gemini готов (встроенный ключ). "
                                         "Используется если Ollama недоступна."),
                  QStringLiteral("#4a9a6a"));
    }

    if (m_jarvis->projectIndexer()->fileCount() > 0) {
        appendLog(Str::logJarvis(),
                  Str::projLoaded()
                  + m_jarvis->projectIndexer()->projectRoot()
                  + QStringLiteral(" (")
                  + QString::number(m_jarvis->projectIndexer()->fileCount())
                  + Str::projFiles(),
                  Theme::LogColors::system);
        m_jarvis->syncProjectInfoToMemory();
    }

    m_input->setFocus();

    // ── Самообучение ──────────────────────────────────────
    m_learnedCmds = new LearnedCommands(this);
    connect(m_learnedCmds, &LearnedCommands::commandLearned,
            this, &MainWindow::onCommandLearned);

    // ── Зрение + управление окнами ───────────────────────
    m_screenAgent = new ScreenAgent(this);
    connect(m_screenAgent, &ScreenAgent::actionCompleted,
            this, [this](const QString& desc) {
        appendLog(Str::logJarvis(), desc, Theme::LogColors::system);
    });

    m_pulseTimer = new QTimer(this);
    connect(m_pulseTimer, &QTimer::timeout, this, [this]() {
        m_pulse = !m_pulse;
        if (m_jarvis->isSpeaking()) {
            m_dot->setStyleSheet(m_pulse
                ? QStringLiteral("color: #00ff88; font-size: 18px;")
                : QStringLiteral("color: #005533; font-size: 18px;"));
        }
    });
    m_pulseTimer->start(400);

    m_jarvis->autoUpdater()->checkForUpdates(true);
}

// ============================================================
// applyLanguage
// ============================================================

void MainWindow::applyLanguage(bool english)
{
    gUiLanguage() = english ? UiLanguage::English : UiLanguage::Russian;
    QSettings cfg(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
    cfg.setValue(QStringLiteral("ui/english"), english);

    m_input->setPlaceholderText(Str::inputPlaceholder());
    m_status->setText(IS_EN ? QStringLiteral("Online") : QStringLiteral("В сети"));

    appendLog(Str::logSystem(),
              IS_EN ? QStringLiteral("Language set to English.")
                    : QStringLiteral("Язык изменён на русский."),
              Theme::LogColors::system);
}

// ============================================================
// Drag-n-drop
// ============================================================

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e)
{
    if (!e->mimeData()->hasUrls()) return;
    QStringList paths;
    for (const QUrl& url : e->mimeData()->urls()) {
        if (url.isLocalFile()) paths.append(url.toLocalFile());
    }
    if (paths.isEmpty()) return;

    int added = m_jarvis->attachments()->addFiles(paths);
    if (added > 0) {
        appendLog(Str::logSystem(),
                  Str::statusAttached() + QString::number(added),
                  Theme::LogColors::system);
    }
    e->acceptProposedAction();
}

// ============================================================
// Меню
// ============================================================

void MainWindow::buildMenuBar()
{
    auto* menuBar = this->menuBar();
    menuBar->setStyleSheet(
        QStringLiteral("QMenuBar { background: #0a1018; color: #96c8e6; font-size: 12px; }"
                       "QMenuBar::item:selected { background: #1a3050; }"
                       "QMenu { background: #0c1828; color: #96c8e6; border: 1px solid #1a3050; }"
                       "QMenu::item:selected { background: #00243d; color: #00d4ff; }"));

    // --- Файл ---
    auto* fileMenu = menuBar->addMenu(Str::menuFile());

    auto* actAttach = fileMenu->addAction(Str::menuAttach());
    actAttach->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
    connect(actAttach, &QAction::triggered, this, &MainWindow::onAttachClicked);

    auto* actClearAttach = fileMenu->addAction(Str::menuClearAttach());
    connect(actClearAttach, &QAction::triggered, this, [this]() {
        m_jarvis->attachments()->clear();
    });

    fileMenu->addSeparator();

    auto* actClear = fileMenu->addAction(Str::menuClearLog());
    connect(actClear, &QAction::triggered, this, [this]() { m_log->clear(); });

    fileMenu->addSeparator();

    auto* actExit = fileMenu->addAction(Str::menuExit());
    connect(actExit, &QAction::triggered, this, &QWidget::close);

    // --- Настройки ---
    auto* settingsMenu = menuBar->addMenu(Str::menuSettings());

    auto* actApiKey = settingsMenu->addAction(Str::menuApiKey());
    connect(actApiKey, &QAction::triggered, this, [this]() {
        bool ok;
        QString key = QInputDialog::getText(this,
            Str::dlgApiKeyTitle(), Str::dlgApiKeyLabel(),
            QLineEdit::Password, QString(), &ok);
        if (ok && !key.trimmed().isEmpty()) {
            m_jarvis->claudeApi()->setApiKey(key.trimmed());
            appendLog(Str::logSystem(), Str::apiKeySaved(), Theme::LogColors::system);
        }
    });

    // ── Gemini API key ──────────────────────────────────
    auto* actGeminiKey = settingsMenu->addAction(
        IS_EN ? QStringLiteral("Gemini API key...") : QStringLiteral("Ключ Gemini API..."));
    connect(actGeminiKey, &QAction::triggered, this, [this]() {
        bool ok;
        const QString key = QInputDialog::getText(
            this,
            IS_EN ? QStringLiteral("Gemini API Key") : QStringLiteral("Ключ Gemini API"),
            IS_EN ? QStringLiteral("Enter your Google Gemini API key\n(free at aistudio.google.com):")
                  : QStringLiteral("Введите ключ Google Gemini API\n(бесплатно на aistudio.google.com):"),
            QLineEdit::Password,
            m_jarvis->geminiBackup() ? m_jarvis->geminiBackup()->apiKey() : QString(),
            &ok);
        if (ok && !key.trimmed().isEmpty()) {
            if (m_jarvis->geminiBackup())
                m_jarvis->geminiBackup()->setApiKey(key.trimmed());
            appendLog(Str::logSystem(),
                      IS_EN ? QStringLiteral("Gemini API key saved.")
                            : QStringLiteral("Ключ Gemini API сохранён."),
                      Theme::LogColors::system);
        }
    });

    auto* actOllamaModel = settingsMenu->addAction(
        IS_EN ? QStringLiteral("Ollama model...") : QStringLiteral("Модель Ollama..."));
    connect(actOllamaModel, &QAction::triggered, this, [this]() {
        bool ok;
        QString model = QInputDialog::getText(this,
            QStringLiteral("Ollama"),
            IS_EN ? QStringLiteral("Model name (e.g. llama3, mistral, phi3):")
                  : QStringLiteral("Имя модели (например: llama3, mistral, phi3):"),
            QLineEdit::Normal,
            m_jarvis->ollamaApi()->model(), &ok);
        if (ok && !model.trimmed().isEmpty()) {
            m_jarvis->ollamaApi()->setModel(model.trimmed());
            appendLog(Str::logSystem(),
                      IS_EN ? QStringLiteral("Ollama model set: ") + model.trimmed()
                            : QStringLiteral("Модель Ollama: ") + model.trimmed(),
                      Theme::LogColors::system);
        }
    });

    settingsMenu->addSeparator();

    auto* actAgent = settingsMenu->addAction(Str::menuAgentMode());
    actAgent->setCheckable(true);
    actAgent->setChecked(false);
    connect(actAgent, &QAction::toggled, this, [this, actAgent](bool checked) {
        if (checked) {
            // Пингуем Ollama перед включением
            appendLog(Str::logSystem(),
                      IS_EN ? QStringLiteral("Checking Ollama availability...")
                            : QStringLiteral("Проверяю доступность Ollama..."),
                      Theme::LogColors::system);

            m_jarvis->ollamaApi()->checkAvailability(
                [this, actAgent](bool available, const QString& info) {
                    if (available) {
                        m_jarvis->setMultiAgentMode(true);
                        m_agentLabel->setVisible(true);
                        m_agentLabel->setText(QStringLiteral("🦙 Ollama"));
                        appendLog(Str::logJarvis(),
                                  (IS_EN ? QStringLiteral("Agent mode ON. Code → Claude, Chat → Ollama (")
                                         : QStringLiteral("Агент мод ВКЛ. Код → Claude, Беседа → Ollama ("))
                                  + m_jarvis->ollamaApi()->model()
                                  + QStringLiteral(")\n") + info,
                                  Theme::LogColors::system);
                    } else {
                        // Ollama недоступна — откатываем чекбокс, режим не включаем
                        actAgent->setChecked(false);
                        appendLog(Str::logError(),
                                  IS_EN ? QStringLiteral(
                                      "Ollama is not running.\n"
                                      "Start it with: ollama serve\n"
                                      "Or install from: https://ollama.com\n"
                                      "Agent mode stays OFF — Claude handles everything.")
                                        : QStringLiteral(
                                      "Ollama не запущена.\n"
                                      "Запусти её: ollama serve\n"
                                      "Или скачай с: https://ollama.com\n"
                                      "Агент мод ВЫКЛ — всё обрабатывает Claude."),
                                  Theme::LogColors::error);
                    }
                });
        } else {
            m_jarvis->setMultiAgentMode(false);
            m_agentLabel->setVisible(false);
            appendLog(Str::logJarvis(), Str::agentModeOff(), Theme::LogColors::system);
        }
    });

    settingsMenu->addSeparator();

    auto* actKeepAttach = settingsMenu->addAction(Str::menuKeepAttach());
    actKeepAttach->setCheckable(true);
    actKeepAttach->setChecked(false);
    connect(actKeepAttach, &QAction::toggled, this, [this](bool checked) {
        m_jarvis->attachments()->setKeepAfterSend(checked);
        appendLog(Str::logSystem(),
                  checked ? Str::statusAttachKept() : Str::statusAttachOneShot(),
                  Theme::LogColors::system);
    });

    settingsMenu->addSeparator();

    auto* actKeyboard = settingsMenu->addAction(Str::menuKeyboard());
    connect(actKeyboard, &QAction::triggered, this, &MainWindow::toggleKeyboard);

    settingsMenu->addSeparator();

    auto* langMenu = settingsMenu->addMenu(Str::menuLanguage());

    auto* actLangRu = langMenu->addAction(Str::menuLangRu());
    actLangRu->setCheckable(true);
    actLangRu->setChecked(gUiLanguage() == UiLanguage::Russian);
    connect(actLangRu, &QAction::triggered, this, [this, actLangRu](bool) {
        applyLanguage(false);
        actLangRu->setChecked(true);
    });

    auto* actLangEn = langMenu->addAction(Str::menuLangEn());
    actLangEn->setCheckable(true);
    actLangEn->setChecked(gUiLanguage() == UiLanguage::English);
    connect(actLangEn, &QAction::triggered, this, [this, actLangEn](bool) {
        applyLanguage(true);
        actLangEn->setChecked(true);
    });

    // --- Проект ---
    auto* projectMenu = menuBar->addMenu(Str::menuProject());

    auto* actIndexProject = projectMenu->addAction(Str::menuIndexFolder());
    connect(actIndexProject, &QAction::triggered, this, [this]() {
        QString startDir = m_jarvis->projectIndexer()->projectRoot();
        if (startDir.isEmpty()) startDir = QDir::homePath();

        QString dir = QFileDialog::getExistingDirectory(this,
            Str::dlgChooseFolder(), startDir, QFileDialog::ShowDirsOnly);
        if (dir.isEmpty()) return;

        appendLog(Str::logSystem(), Str::statusIndexing() + dir + QStringLiteral("..."),
                  Theme::LogColors::system);

        m_jarvis->projectIndexer()->setProjectRoot(dir);
        m_jarvis->projectIndexer()->indexProject();
        m_jarvis->projectIndexer()->enableFileWatcher(true);
        m_jarvis->syncProjectInfoToMemory();

        appendLog(Str::logJarvis(),
                  Str::projIndexed() + QString::number(m_jarvis->projectIndexer()->fileCount())
                  + Str::projSymbols() + QString::number(m_jarvis->projectIndexer()->symbolCount()),
                  Theme::LogColors::jarvis);
    });

    auto* actReindex = projectMenu->addAction(Str::menuReindex());
    connect(actReindex, &QAction::triggered, this, [this]() {
        if (m_jarvis->projectIndexer()->projectRoot().isEmpty()) {
            appendLog(Str::logSystem(), Str::projChooseFirst(), Theme::LogColors::error);
            return;
        }
        m_jarvis->projectIndexer()->indexProject();
        m_jarvis->syncProjectInfoToMemory();
        appendLog(Str::logSystem(),
                  Str::projReindexed() + QString::number(m_jarvis->projectIndexer()->fileCount()) + Str::projFilesCount(),
                  Theme::LogColors::system);
    });

    projectMenu->addSeparator();

    auto* actProjectInfo = projectMenu->addAction(Str::menuProjectInfo());
    connect(actProjectInfo, &QAction::triggered, this, [this]() {
        auto* idx = m_jarvis->projectIndexer();
        if (idx->fileCount() == 0) {
            appendLog(Str::logSystem(), Str::projNotIndexed(), Theme::LogColors::error);
            return;
        }

        QString info = Str::projInfoLabel() + idx->projectRoot()
                     + Str::projFilesLabel() + QString::number(idx->fileCount())
                     + Str::projSymbolsLabel() + QString::number(idx->symbolCount())
                     + Str::projClassesLabel();

        for (const auto& cls : idx->allClasses()) {
            info += QStringLiteral("  • ") + cls + QStringLiteral("\n");
        }
        appendLog(Str::logJarvis(), info.trimmed(), Theme::LogColors::jarvis);
    });

    // --- Обновление ---
    auto* updateMenu = menuBar->addMenu(Str::menuUpdate());

    auto* actCheck = updateMenu->addAction(Str::menuCheckUpdate());
    connect(actCheck, &QAction::triggered, this, [this]() {
        appendLog(Str::logSystem(), Str::updChecking(), Theme::LogColors::system);
        m_jarvis->autoUpdater()->checkForUpdates(false);
    });

    updateMenu->addSeparator();

    auto* actReleases = updateMenu->addAction(Str::menuReleasePage());
    connect(actReleases, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Bohdan99py/jarvis/releases")));
    });

    // --- Помощь ---
    auto* helpMenu = menuBar->addMenu(Str::menuHelp());

    auto* actAbout = helpMenu->addAction(Str::menuAbout());
    connect(actAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, QStringLiteral("J.A.R.V.I.S."),
            Str::aboutText().arg(QCoreApplication::applicationVersion()));
    });

    helpMenu->addSeparator();

    // ── Сообщить о баге ─────────────────────────────────
    auto* actBugReport = helpMenu->addAction(
        IS_EN ? QStringLiteral("🐛 Report a Bug...") : QStringLiteral("🐛 Сообщить о баге..."));
    connect(actBugReport, &QAction::triggered, this, [this]() {
        BugReporter::showDialog(this);
    });

    // ── GitHub Issues ────────────────────────────────────
    auto* actGithubIssues = helpMenu->addAction(
        IS_EN ? QStringLiteral("GitHub Issues") : QStringLiteral("GitHub Issues"));
    connect(actGithubIssues, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/Bohdan99py/jarvis/issues")));
    });

// =============================================================================
// ВСТАВИТЬ в buildMenuBar() в mainwindow.cpp
// В меню "Помощь" — после actAbout
// =============================================================================

    // --- Что нового ---
    auto* actWhatsNew = helpMenu->addAction(
        IS_EN ? QStringLiteral("What's New") : QStringLiteral("Что нового"));
    connect(actWhatsNew, &QAction::triggered, this, [this]() {
        const QString news = IS_EN ? QStringLiteral(
R"(<b>J.A.R.V.I.S. — Latest Changes</b><br><br>

<b>🧠 Autonomous Brain (no internet needed)</b><br>
&bull; Greetings, time/date, math — answered instantly without API<br>
&bull; 35+ apps: open/close Steam, Chrome, CLion, Rider, Discord, Telegram, Blender, OBS...<br>
&bull; System: lock screen, shutdown, restart<br>
&bull; "What can you do" / "help" — local answer<br><br>

<b>📚 Self-Learning (LearnedCommands)</b><br>
&bull; After each AI response, JARVIS extracts executable steps<br>
&bull; Next time the same request comes — runs locally, no API<br>
&bull; Saved in %AppData%/Jarvis/learned_commands.json<br>
&bull; Memory shown: ✓ (from memory, N uses)<br><br>

<b>👁 Screen Vision (ScreenAgent)</b><br>
&bull; "What do you see" — screenshot → Claude Vision → description<br>
&bull; "Click on [text]" — OCR search on screen + mouse click<br>
&bull; "Open YouTube" — browser control<br>
&bull; Visual commands via Claude Vision + action execution<br><br>

<b>♊ Gemini API Key in UI</b><br>
&bull; Settings → Gemini API key... (no manual file editing)<br>
&bull; Free key from aistudio.google.com<br><br>

<b>🗑 Removed</b><br>
&bull; Vibe Coding mode — now always active (Brain handles routing)<br><br>

<b>🐛 Fixed</b><br>
&bull; brain.h: HWND/WORD without windows.h guard — fixed<br>
&bull; screen_agent: parseVirtualKey made static, emit in const fixed<br>
&bull; Brain::captureSnapshot — multiAgentMode default param added)"
        ) : QStringLiteral(
R"(<b>J.A.R.V.I.S. — Последние изменения</b><br><br>

<b>🧠 Автономный мозг (без интернета)</b><br>
&bull; Приветствия, время/дата, математика — ответ мгновенно без API<br>
&bull; 35+ приложений: открыть/закрыть Steam, Chrome, CLion, Rider, Discord, Telegram, Blender, OBS...<br>
&bull; Система: заблокировать, выключить, перезагрузить<br>
&bull; "Что ты умеешь" / "помощь" — локальный ответ<br><br>

<b>📚 Самообучение (LearnedCommands)</b><br>
&bull; После каждого ответа AI — извлекает исполняемые шаги<br>
&bull; Следующий раз тот же запрос — выполняет сам, без API<br>
&bull; Сохраняется в %AppData%/Jarvis/learned_commands.json<br>
&bull; Показывается: ✓ (из памяти, использований: N)<br><br>

<b>👁 Зрение (ScreenAgent)</b><br>
&bull; "Что видишь" — скриншот → Claude Vision → описание<br>
&bull; "Кликни на [текст]" — OCR поиск на экране + клик мышью<br>
&bull; "Открой YouTube" — управление браузером<br>
&bull; Визуальные команды через Claude Vision + исполнение<br><br>

<b>♊ Ключ Gemini API в интерфейсе</b><br>
&bull; Настройки → Ключ Gemini API... (без ручного редактирования файлов)<br>
&bull; Бесплатный ключ на aistudio.google.com<br><br>

<b>🗑 Удалено</b><br>
&bull; Режим Вайбкодинга — теперь всегда активен (Brain сам решает)<br><br>

<b>🐛 Исправлено</b><br>
&bull; screen_agent.h: HWND/WORD без guard — исправлено<br>
&bull; parseVirtualKey сделан static, emit в const-методе — исправлено<br>
&bull; Brain::captureSnapshot — добавлен дефолтный параметр)"
        );

        QMessageBox* box = new QMessageBox(this);
        box->setWindowTitle(IS_EN ? QStringLiteral("What's New in J.A.R.V.I.S.")
                                  : QStringLiteral("Что нового в J.A.R.V.I.S."));
        box->setTextFormat(Qt::RichText);
        box->setText(news);
        box->setIcon(QMessageBox::Information);
        box->setStandardButtons(QMessageBox::Ok);
        box->setStyleSheet(
            QStringLiteral("QMessageBox { background-color: #0a1018; color: #c8e0f0; }"
                           "QMessageBox QLabel { color: #c8e0f0; min-width: 500px; }"
                           "QPushButton { background-color: #0f2438; color: #00d4ff; "
                           "border: 1px solid #1a5070; padding: 6px 20px; border-radius: 4px; }"
                           "QPushButton:hover { background-color: #1a3a5c; }"));
        box->exec();
        box->deleteLater();
    });

    // --- Инструкция ---
    auto* actHelp = helpMenu->addAction(
        IS_EN ? QStringLiteral("User Guide") : QStringLiteral("Инструкция пользователя"));
    connect(actHelp, &QAction::triggered, this, [this]() {
        const QString guide = IS_EN ? QStringLiteral(
R"(<b>J.A.R.V.I.S. — User Guide</b><br><br>

<b>📋 COMMANDS (no internet needed)</b><br>
<b>Apps:</b> open Steam · open Chrome · open Notepad · open Calculator · open Explorer<br>
open CLion · open Rider · open Discord · open Telegram · open OBS · close Steam<br><br>
<b>System:</b> lock screen · shutdown · restart · volume 70 · brightness up<br><br>
<b>Info:</b> what time · what date · 2+2 · what can you do · help<br><br>
<b>Search:</b> find file readme · find document resume · find where it says "text"<br><br>

<b>👁 VISUAL COMMANDS (requires Claude API)</b><br>
what do you see · describe screen · click on [text] · find on screen [text]<br><br>

<b>🔍 SEARCH</b><br>
find [query] in project · find [query] on PC · find [query] in browser history<br>
find [query] in chat · find [query] online<br><br>

<b>💻 CODE (requires Claude API + project indexed)</b><br>
add function X · fix bug in file.cpp · refactor class Y · create file Z<br><br>

<b>⚙ SETTINGS</b><br>
Settings → Claude API key... (console.anthropic.com)<br>
Settings → Gemini API key... (aistudio.google.com — free)<br>
Settings → Ollama model... (ollama.com — local, offline)<br>
Project → Index folder... → choose your project root<br><br>

<b>📎 ATTACHMENTS</b><br>
Click 📎 or drag files into the window · Ctrl+O to open file picker<br><br>

<b>⌨ KEYBOARD SHORTCUTS</b><br>
Enter — send · Esc — close clarification · Ctrl+O — attach file)"
        ) : QStringLiteral(
R"(<b>J.A.R.V.I.S. — Инструкция пользователя</b><br><br>

<b>📋 КОМАНДЫ (без интернета)</b><br>
<b>Приложения:</b> открой Steam · открой Chrome · открой блокнот · открой калькулятор<br>
открой CLion · открой Rider · открой Discord · открой Telegram · закрой Steam<br><br>
<b>Система:</b> заблокируй · выключи · перезагрузи · громкость 70 · яркость выше<br><br>
<b>Информация:</b> который час · какая дата · 2+2 · что ты умеешь · помощь<br><br>
<b>Поиск:</b> найди файл readme · найди документ резюме · найди где написано "текст"<br><br>

<b>👁 ВИЗУАЛЬНЫЕ КОМАНДЫ (требует Claude API)</b><br>
что видишь · опиши экран · кликни на [текст] · найди на экране [текст]<br><br>

<b>🔍 ПОИСК</b><br>
найди [запрос] в проекте · найди [запрос] на компьютере · найди [запрос] в истории браузера<br>
найди [запрос] в нашем разговоре · найди [запрос] в интернете<br><br>

<b>💻 КОД (требует Claude API + проект проиндексирован)</b><br>
добавь функцию X · исправь баг в файл.cpp · перепиши класс Y · создай файл Z<br><br>

<b>⚙ НАСТРОЙКИ</b><br>
Настройки → Ключ Claude API... (console.anthropic.com)<br>
Настройки → Ключ Gemini API... (aistudio.google.com — бесплатно)<br>
Настройки → Модель Ollama... (ollama.com — локально, без интернета)<br>
Проект → Индексировать папку... → выбрать корень проекта<br><br>

<b>📎 ПРИКРЕПЛЕНИЯ</b><br>
Кнопка 📎 или перетащить файлы в окно · Ctrl+O открыть выбор файлов<br><br>

<b>⌨ ГОРЯЧИЕ КЛАВИШИ</b><br>
Enter — отправить · Esc — закрыть уточнение · Ctrl+O — прикрепить файл)"
        );

        QMessageBox* box = new QMessageBox(this);
        box->setWindowTitle(IS_EN ? QStringLiteral("J.A.R.V.I.S. User Guide")
                                  : QStringLiteral("Инструкция J.A.R.V.I.S."));
        box->setTextFormat(Qt::RichText);
        box->setText(guide);
        box->setIcon(QMessageBox::Information);
        box->setStandardButtons(QMessageBox::Ok);
        box->setStyleSheet(
            QStringLiteral("QMessageBox { background-color: #0a1018; color: #c8e0f0; }"
                           "QMessageBox QLabel { color: #c8e0f0; min-width: 520px; }"
                           "QPushButton { background-color: #0f2438; color: #00d4ff; "
                           "border: 1px solid #1a5070; padding: 6px 20px; border-radius: 4px; }"
                           "QPushButton:hover { background-color: #1a3a5c; }"));
        box->exec();
        box->deleteLater();
    });

}

// ============================================================
// Events
// ============================================================

void MainWindow::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        if (m_clarifyBar && m_clarifyBar->isVisible()) {
            hideClarification();
        } else if (m_kbVisible) {
            toggleKeyboard();
        } else {
            m_input->setFocus();
            m_input->selectAll();
        }
        return;
    }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (m_trayIcon && m_trayIcon->isVisible()) {
        e->ignore();
        hide();
        m_trayIcon->showMessage(
            QStringLiteral("J.A.R.V.I.S."),
            IS_EN ? QStringLiteral("Running in the system tray. Right-click to quit.")
                  : QStringLiteral("Работает в трее. ПКМ → Выход."),
            QSystemTrayIcon::Information, 2000);
    } else {
        e->accept();
    }
}

// ============================================================
// trySystemControl — управление системой (звук, яркость и т.д.)
// Возвращает true если команда обработана
// ============================================================

bool MainWindow::trySystemControl(const QString& userText)
{
    auto result = SystemController::tryExecuteSystemCommand(userText);
    if (!result.success || result.message.isEmpty()) {
        // success=false, message="" → не системная команда
        // success=false, message!="" → системная команда, но ошибка
        if (!result.message.isEmpty()) {
            // Была попытка, но ошибка — сообщаем и считаем обработанной
            appendLog(Str::logJarvis(), result.message, Theme::LogColors::error);
            m_jarvis->memory()->addMessage(QStringLiteral("user"), userText);
            m_jarvis->memory()->addMessage(QStringLiteral("assistant"), result.message);
            return true;
        }
        return false; // не системная команда → идёт дальше
    }
    // Успешно выполнено
    appendLog(Str::logJarvis(), result.message, Theme::LogColors::jarvis);
    m_jarvis->speakAsync(result.message);
    m_jarvis->memory()->addMessage(QStringLiteral("user"), userText);
    m_jarvis->memory()->addMessage(QStringLiteral("assistant"), result.message);
    return true;
}

// ============================================================
// tryOpenApp — умное открытие приложений через AppLauncher
// Возвращает true если приложение найдено и запущено/попытка была
// ============================================================

bool MainWindow::tryOpenApp(const QString& userText, const Intent& intent)
{
    if (intent.action != Intent::Action::Open) return false;

    // ── Спец-случай: «открой проект [в <ide>]» / «open project [in <ide>]» ──
    // Brain для "открой проект в clion" кладёт "clion" в targetApp (как
    // обычное приложение), но пользователь имеет в виду ПРОЕКТ JARVIS,
    // открытый в IDE — а не просто запуск CLion вхолостую.
    // intent.query сохраняет полную фразу без "открой "/"open " (например
    // "проект", "проект в rider", "project in vscode") — её и проверяем.
    {
        const QString q = intent.query.trimmed();
        static const QRegularExpression reProject(
            QStringLiteral(R"(^(?:проект|project)(?:\s+(?:в|in)\s+(.+))?$)"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = reProject.match(q);
        if (m.hasMatch()) {
            QString ide = m.captured(1).trimmed();
            if (ide.isEmpty()) ide = QStringLiteral("clion");

            const QString resp = m_jarvis->openProjectInIDE(ide);
            if (!resp.isEmpty()) {
                appendLog(Str::logJarvis(), resp, Theme::LogColors::jarvis);
                m_jarvis->memory()->addMessage(QStringLiteral("user"), userText);
                m_jarvis->memory()->addMessage(QStringLiteral("assistant"), resp);
            } else {
                const QString err = IS_EN
                    ? QStringLiteral("No project is open. Index one first, "
                                     "e.g. \"index C:\\Projects\\jarvis\".")
                    : QStringLiteral("Проект не открыт. Сначала проиндексируйте его: "
                                     "индекс C:\\Projects\\jarvis");
                appendLog(Str::logJarvis(), err, Theme::LogColors::error);
                m_jarvis->memory()->addMessage(QStringLiteral("user"), userText);
                m_jarvis->memory()->addMessage(QStringLiteral("assistant"), err);
            }
            return true;
        }
    }

    // Целевое имя: сначала targetApp из Brain, потом query
    const QString target = intent.targetApp.isEmpty()
        ? intent.query
        : intent.targetApp;

    if (target.isEmpty()) return false;

    // Пробуем AppLauncher — он ищет через реестр, PATH и жёсткие пути
    auto result = m_appLauncher.launch(target);
    if (result.success) {
        const QString appName = QFileInfo(result.resolvedPath).baseName();
        const QString resp = IS_EN
            ? QStringLiteral("Opening: ") + appName
            : QStringLiteral("Открываю: ") + appName;
        appendLog(Str::logJarvis(), resp, Theme::LogColors::jarvis);
        m_jarvis->memory()->addMessage(QStringLiteral("user"), userText);
        m_jarvis->memory()->addMessage(QStringLiteral("assistant"), resp);
        return true;
    }

    // AppLauncher не нашёл — пробуем напрямую через ShellExecuteW
    // (может сработать для URL, ms-settings: и прочих схем)
    {
        const std::wstring wexe = target.toStdWString();
        HINSTANCE hr = ShellExecuteW(
            nullptr, L"open", wexe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<intptr_t>(hr) > 32) {
            const QString resp = IS_EN
                ? QStringLiteral("Opening: ") + target
                : QStringLiteral("Открываю: ") + target;
            appendLog(Str::logJarvis(), resp, Theme::LogColors::jarvis);
            m_jarvis->memory()->addMessage(QStringLiteral("user"), userText);
            m_jarvis->memory()->addMessage(QStringLiteral("assistant"), resp);
            return true;
        }
    }

    // Совсем не нашли — сообщаем
    const QString resp = IS_EN
        ? QStringLiteral("Could not find application: ") + target
        : QStringLiteral("Не могу найти приложение: ") + target;
    appendLog(Str::logJarvis(), resp, Theme::LogColors::jarvis);
    m_jarvis->memory()->addMessage(QStringLiteral("user"), userText);
    m_jarvis->memory()->addMessage(QStringLiteral("assistant"), resp);
    return true; // считаем обработанной (не пускаем в Claude)
}

// ============================================================
// onSend — главная точка входа пользовательского ввода
// ============================================================

void MainWindow::onSend()
{
    QString text = m_input->text().trimmed();

    const bool hasAttach = !m_jarvis->attachments()->isEmpty();
    if (text.isEmpty() && !hasAttach) return;
    if (text.isEmpty() && hasAttach) {
        appendLog(Str::logSystem(),
                  IS_EN ? QStringLiteral("Write a request — attached files will be sent with it.")
                        : QStringLiteral("Напишите запрос — прикреплённые файлы будут отправлены вместе с ним."),
                  Theme::LogColors::error);
        return;
    }

    m_input->clear();
    hideClarification();

    // ── 1. Автоопределение языка ──────────────────────────
    // Обновляем язык по каждому сообщению пользователя.
    // Языковая инструкция инжектируется в системный промпт Claude
    // в методе buildClaudeSystemPrompt() через m_langDetector.systemInstruction().
    m_langDetector.update(text);

    // ── 0. Выученные команды (самообучение) ────────────
    // Если Джарвис уже знает как это сделать — делает сам, БЕЗ API
    if (m_learnedCmds) {
        const LearnedCommand* learned = m_learnedCmds->findMatch(text);
        if (learned) {
            const QString result = m_learnedCmds->execute(*learned);
            const QString msg = IS_EN
                ? QStringLiteral("✓ (from memory, %1 uses)\n%2").arg(learned->useCount).arg(result)
                : QStringLiteral("✓ (из памяти, использований: %1)\n%2").arg(learned->useCount).arg(result);
            appendLog(Str::logJarvis(), msg, Theme::LogColors::system);
            m_jarvis->memory()->addMessage(QStringLiteral("user"), text);
            m_jarvis->memory()->addMessage(QStringLiteral("assistant"), result);
            m_input->setFocus();
            return;
        }
    }

    // ── 1. Визуальные команды (Screen Agent) ─────────────
    // "нажми на", "кликни", "что видишь", "опиши экран"
    {
        const QString lo = text.toLower();
        static const QStringList kVisualTriggers = {
            QStringLiteral("нажми на"),          QStringLiteral("кликни на"),
            QStringLiteral("кликни по"),         QStringLiteral("нажми кнопку"),
            QStringLiteral("что видишь"),         QStringLiteral("опиши экран"),
            QStringLiteral("посмотри на экран"),  QStringLiteral("что на экране"),
            QStringLiteral("click on"),           QStringLiteral("find on screen"),
            QStringLiteral("what do you see"),    QStringLiteral("describe screen"),
            QStringLiteral("look at screen"),     QStringLiteral("найди на экране"),
        };
        bool isVisual = false;
        for (const QString& t : kVisualTriggers)
            if (lo.contains(t)) { isVisual = true; break; }
        if (isVisual) {
            handleVisualCommand(text);
            m_input->setFocus();
            return;
        }
    }

    // ── 2. Системные команды (звук, яркость, блокировка и т.д.)
    // Проверяем ДО Brain, потому что "громкость 50" — не вопрос к AI
    if (trySystemControl(text)) {
        m_input->setFocus();
        return;
    }

    const QString attachmentBlock = m_jarvis->attachments()->buildAttachmentBlock();

    QString userLog = text.left(200);
    if (hasAttach) {
        userLog += QStringLiteral("   [📎 ")
                 + QString::number(m_jarvis->attachments()->count())
                 + (IS_EN ? QStringLiteral(" file(s), ") : QStringLiteral(" файл(ов), "))
                 + m_jarvis->attachments()->totalSizeHuman()
                 + QStringLiteral("]");
    }
    appendLog(Str::logSender(), userLog, Theme::LogColors::user);

    if (m_jarvis->multiAgentMode()) {
        m_agentLabel->setText(Str::agentClaude());
    }

    // ── 3. Brain: анализируем намерение ──────────────────
    ContextSnapshot ctx = Brain::captureSnapshot(
        m_jarvis->memory()->recentCommands(8),
        m_jarvis->memory()->lastResponse(),
        m_jarvis->projectIndexer()->fileCount() > 0,
        m_jarvis->projectIndexer()->projectRoot(),
        m_jarvis->projectIndexer()->recentFiles(10),
        false,  // vibeCodingMode убран — Brain сам определяет режим
        m_jarvis->multiAgentMode()
    );

    Brain brain;
    Intent intent = brain.analyze(text, ctx);

    if (intent.needsClarification) {
        m_pendingInput = text;
        showClarification(
            IS_EN ? QStringLiteral("Where should I look?")
                  : QStringLiteral("Где искать?"),
            {
                IS_EN ? QStringLiteral("Project files") : QStringLiteral("В проекте"),
                IS_EN ? QStringLiteral("Whole PC")      : QStringLiteral("На компьютере"),
                IS_EN ? QStringLiteral("Browser history"): QStringLiteral("История браузера"),
                IS_EN ? QStringLiteral("Internet")      : QStringLiteral("В интернете"),
                IS_EN ? QStringLiteral("Chat history")  : QStringLiteral("Наш разговор"),
            }
        );
        m_input->setFocus();
        return;
    }

    // ── Debug-строка Brain ────────────────────────────────
    {
        static const auto actionStr = [](Intent::Action a) -> QString {
            switch (a) {
            case Intent::Action::Search:   return QStringLiteral("Search");
            case Intent::Action::Open:     return QStringLiteral("Open");
            case Intent::Action::Explain:  return QStringLiteral("Explain");
            case Intent::Action::Modify:   return QStringLiteral("Modify");
            case Intent::Action::Create:   return QStringLiteral("Create");
            case Intent::Action::Ask:      return QStringLiteral("Ask");
            case Intent::Action::Clarify:  return QStringLiteral("Clarify");
            case Intent::Action::SystemCmd:return QStringLiteral("SystemCmd");
            default:                       return QStringLiteral("Unknown");
            }
        };
        static const auto domainStr = [](Intent::Domain d) -> QString {
            switch (d) {
            case Intent::Domain::ProjectFiles:   return QStringLiteral("ProjectFiles");
            case Intent::Domain::Filesystem:     return QStringLiteral("Filesystem");
            case Intent::Domain::BrowserHistory: return QStringLiteral("BrowserHistory");
            case Intent::Domain::ChatHistory:    return QStringLiteral("ChatHistory");
            case Intent::Domain::UE5Logs:        return QStringLiteral("UE5Logs");
            case Intent::Domain::Web:            return QStringLiteral("Web");
            case Intent::Domain::Clipboard:      return QStringLiteral("Clipboard");
            case Intent::Domain::Memory:         return QStringLiteral("Memory");
            case Intent::Domain::Code:           return QStringLiteral("Code");
            default:                             return QStringLiteral("None");
            }
        };
        appendLog(QStringLiteral("🧠 Brain"),
                  QStringLiteral("action=") + actionStr(intent.action)
                  + QStringLiteral("  domain=") + domainStr(intent.domain)
                  + QStringLiteral("  query=\"") + intent.query + QStringLiteral("\"")
                  + QStringLiteral("  conf=") + QString::number(intent.confidence, 'f', 2),
                  QStringLiteral("#4a6080"));
    }

    // ── 4. Open: умное открытие через AppLauncher ─────────
    if (intent.action == Intent::Action::Open) {
        if (tryOpenApp(text, intent)) {
            m_input->setFocus();
            return;
        }
        // Если tryOpenApp вернул false (target пустой) — идём в Search/Claude
    }

    // ── 5. Search: поиск + FileViewer для файлов ─────────
    if (intent.action == Intent::Action::Search)
    {
        auto* router = new SearchRouter(m_jarvis->projectIndexer(),
                                        m_jarvis->memory(),
                                        this);

        // Захватываем this для FileViewer — окно показываем из основного потока
        connect(router, &SearchRouter::searchFinished,
                this, [this, router, text](const QString& result) {
            appendLog(Str::logJarvis(), result, Theme::LogColors::jarvis);
            if (result.length() <= 300) m_jarvis->speakAsync(result);
            m_jarvis->memory()->addMessage(QStringLiteral("user"), text);
            m_jarvis->memory()->addMessage(QStringLiteral("assistant"), result);
            setThinkingState(false);

            // Открываем FileViewer для результатов файловой системы
            const QStringList filePaths = router->lastFoundFilePaths();
            if (!filePaths.isEmpty()) {
                FileViewer::showFiles(filePaths, this);
            }

            router->deleteLater();
        });

        const QString searchResult = router->search(intent, ctx);

        if (!searchResult.isEmpty()) {
            appendLog(Str::logJarvis(), searchResult, Theme::LogColors::jarvis);
            if (searchResult.length() <= 300) m_jarvis->speakAsync(searchResult);
            m_jarvis->memory()->addMessage(QStringLiteral("user"), text);
            m_jarvis->memory()->addMessage(QStringLiteral("assistant"), searchResult);

            // Синхронный поиск (проект, история браузера, etc.) — показываем
            // FileViewer сразу. Асинхронный — ТОЛЬКО Filesystem (см.
            // SearchRouter::search: это единственный домен, где search()
            // сам вызывает searchAsync() и возвращает плейсхолдер
            // "Ищу на компьютере: ...", а итоговый результат придёт через
            // searchFinished). Раньше здесь проверялось
            // searchResult.contains("..."), но синхронный результат из
            // ProjectFiles может и сам содержать "..." (например, цитата
            // кода вида "...";) — тогда UI навсегда застревал в "Thinking...".
            if (intent.domain != Intent::Domain::Filesystem) {
                const QStringList filePaths = router->lastFoundFilePaths();
                if (!filePaths.isEmpty()) {
                    FileViewer::showFiles(filePaths, this);
                }
                router->deleteLater();
            } else {
                // Асинхронный ФС поиск — ждём searchFinished
                setThinkingState(true);
            }
        }

        m_input->setFocus();
        return;
    }

    // ── 6. Всё остальное → Jarvis/Claude ─────────────────
    m_lastUserInput = text;  // сохраняем для самообучения
    QString response = m_jarvis->processCommand(
        text, attachmentBlock, m_langDetector.systemInstruction());

    if (!response.isEmpty()) {
        appendLog(Str::logJarvis(), response, Theme::LogColors::jarvis);
        m_jarvis->speakAsync(response);
    }

    m_input->setFocus();
}

// ============================================================
// Панель уточнения Brain
// ============================================================

void MainWindow::showClarification(const QString& question, const QStringList& options)
{
    if (!m_clarifyBar) return;

    m_clarifyText->setText(question);

    while (QLayoutItem* item = m_clarifyBtnLay->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }

    for (int i = 0; i < options.size(); ++i) {
        auto* btn = new QPushButton(options[i], m_clarifyBar);
        btn->setStyleSheet(
            QStringLiteral("QPushButton { background-color: #001830; color: #00d4ff; "
                           "border: 1px solid #00587a; border-radius: 4px; "
                           "padding: 4px 10px; font-size: 11px; } "
                           "QPushButton:hover { background-color: #00243d; }"));
        btn->setCursor(Qt::PointingHandCursor);
        const int choice = i + 1;
        connect(btn, &QPushButton::clicked, this, [this, choice]() {
            onClarificationChoice(choice);
        });
        m_clarifyBtnLay->addWidget(btn);
    }
    m_clarifyBtnLay->addStretch(1);

    m_clarifyBar->setVisible(true);
}

void MainWindow::hideClarification()
{
    if (m_clarifyBar) m_clarifyBar->setVisible(false);
    m_pendingInput.clear();
}

void MainWindow::onClarificationChoice(int choice)
{
    if (m_pendingInput.isEmpty()) return;

    static const QStringList domainSuffixes = {
        QString(),
        QStringLiteral(" в проекте"),
        QStringLiteral(" на компьютере"),
        QStringLiteral(" в истории браузера"),
        QStringLiteral(" в интернете"),
        QStringLiteral(" в нашем разговоре"),
    };

    QString enriched = m_pendingInput;
    if (choice >= 1 && choice < domainSuffixes.size()) {
        enriched += domainSuffixes[choice];
    }

    hideClarification();
    m_input->setText(enriched);
    onSend();
}

// ============================================================
// Slots: TTS
// ============================================================

void MainWindow::onSpeakingChanged(bool speaking)
{
    if (speaking) {
        m_dot->setStyleSheet(QStringLiteral("color: #00ff88; font-size: 18px;"));
        m_status->setText(IS_EN ? QStringLiteral("Speaking...") : QStringLiteral("Говорю..."));
        m_status->setStyleSheet(QStringLiteral("color: #00ff88; font-size: 12px;"));
    } else {
        m_dot->setStyleSheet(QStringLiteral("color: #00d4ff; font-size: 18px;"));
        m_status->setText(IS_EN ? QStringLiteral("Online") : QStringLiteral("В сети"));
        m_status->setStyleSheet(QStringLiteral("color: #00d4ff; font-size: 12px;"));
    }
}

void MainWindow::onTypingStarted()
{
    m_dot->setStyleSheet(QStringLiteral("color: #ffaa00; font-size: 18px;"));
    m_status->setText(IS_EN ? QStringLiteral("Typing...") : QStringLiteral("Печатаю..."));
    m_status->setStyleSheet(QStringLiteral("color: #ffaa00; font-size: 12px;"));
}

void MainWindow::onTypingProgress(int current, int total)
{
    m_status->setText((IS_EN ? QStringLiteral("Typing... %1/%2") : QStringLiteral("Печатаю... %1/%2"))
                      .arg(current).arg(total));
}

void MainWindow::onTypingFinished()
{
    m_dot->setStyleSheet(QStringLiteral("color: #00d4ff; font-size: 18px;"));
    m_status->setText(IS_EN ? QStringLiteral("Online") : QStringLiteral("В сети"));
    m_status->setStyleSheet(QStringLiteral("color: #00d4ff; font-size: 12px;"));
}

// ============================================================
// Slots: API ответы
// ============================================================

void MainWindow::onAsyncResponse(const QString& response)
{
    appendLog(Str::logJarvis(), response, Theme::LogColors::jarvis);
    if (response.length() <= 200) {
        m_jarvis->speakAsync(response);
    }
}

void MainWindow::onAsyncError(const QString& error)
{
    appendLog(Str::logError(), error, Theme::LogColors::error);
}

void MainWindow::onSuggestion(const QString& description, const QString& action)
{
    m_pendingSuggestionAction = action;
    m_suggestionText->setText(QStringLiteral("→ ") + description);
    m_suggestionBar->setVisible(true);
}

void MainWindow::onAgentSelected(const QString& agentName)
{
    if (m_agentLabel) {
        m_agentLabel->setText(agentName);
        m_agentLabel->setVisible(true);
    }
}

// ============================================================
// Slots: прикрепления
// ============================================================

void MainWindow::onAttachClicked()
{
    QString startDir = m_jarvis->projectIndexer()->projectRoot();
    if (startDir.isEmpty()) startDir = QDir::homePath();

    QStringList files = QFileDialog::getOpenFileNames(this,
        IS_EN ? QStringLiteral("Attach files to next request")
              : QStringLiteral("Прикрепить файлы к следующему запросу"),
        startDir,
        IS_EN
            ? QStringLiteral("All files (*);;Source code (*.cpp *.h *.hpp *.c *.py *.js *.ts);;"
                             "Configs (*.json *.yaml *.yml *.toml *.ini *.cmake);;"
                             "Text (*.txt *.md *.log)")
            : QStringLiteral("Все файлы (*);;Исходный код (*.cpp *.h *.hpp *.c *.py *.js *.ts);;"
                             "Конфиги (*.json *.yaml *.yml *.toml *.ini *.cmake);;"
                             "Текст (*.txt *.md *.log)"));
    if (files.isEmpty()) return;

    int added = m_jarvis->attachments()->addFiles(files);
    if (added == 0) {
        appendLog(Str::logSystem(),
                  IS_EN ? QStringLiteral("Files already attached or limit reached.")
                        : QStringLiteral("Файлы уже прикреплены или лимит достигнут."),
                  Theme::LogColors::error);
    } else if (added < files.size()) {
        appendLog(Str::logSystem(),
                  (IS_EN ? QStringLiteral("Attached ") : QStringLiteral("Прикреплено "))
                  + QString::number(added)
                  + (IS_EN ? QStringLiteral(" of ") : QStringLiteral(" из "))
                  + QString::number(files.size())
                  + (IS_EN ? QStringLiteral(" files (others are duplicates or over limit).")
                           : QStringLiteral(" файлов (остальные — дубликаты или сверх лимита).")),
                  Theme::LogColors::system);
    }
}

void MainWindow::onAttachmentsChanged() { rebuildAttachmentsBar(); }

void MainWindow::onAttachmentsConsumed()
{
    if (!m_jarvis->attachments()->keepAfterSend()) {
        m_jarvis->attachments()->clear();
    }
}

// ============================================================
// Thinking state
// ============================================================

void MainWindow::setThinkingState(bool thinking)
{
    if (thinking) {
        m_dot->setStyleSheet(QStringLiteral("color: #aa66ff; font-size: 18px;"));
        m_status->setText(Str::statusThinking());
        m_status->setStyleSheet(QStringLiteral("color: #aa66ff; font-size: 12px;"));
        m_input->setEnabled(false);
    } else {
        m_dot->setStyleSheet(QStringLiteral("color: #00d4ff; font-size: 18px;"));
        m_status->setText(IS_EN ? QStringLiteral("Online") : QStringLiteral("В сети"));
        m_status->setStyleSheet(QStringLiteral("color: #00d4ff; font-size: 12px;"));
        m_input->setEnabled(true);
        m_input->setFocus();
    }
}

// ============================================================
// Панель обновления
// ============================================================

void MainWindow::showUpdateBar(const QString& version)
{
    m_updateLabel->setText((IS_EN ? QStringLiteral("Update available v")
                                  : QStringLiteral("Доступно обновление v")) + version);
    m_updateProgress->setValue(0);
    m_updateProgress->setVisible(false);
    m_updateBtn->setVisible(true);
    m_updateBar->setVisible(true);

    appendLog(Str::logSystem(),
              (IS_EN ? QStringLiteral("Update available v") : QStringLiteral("Доступно обновление v")) + version,
              Theme::LogColors::system);
}

void MainWindow::hideUpdateBar() { m_updateBar->setVisible(false); }

// ============================================================
// Клавиатура
// ============================================================

void MainWindow::toggleKeyboard()
{
    m_kbVisible = !m_kbVisible;

    if (m_kbAnim) {
        m_kbAnim->stop();
        m_kbAnim->deleteLater();
    }

    m_kbAnim = new QPropertyAnimation(m_kbContainer, "maximumHeight", this);
    m_kbAnim->setDuration(250);
    m_kbAnim->setEasingCurve(QEasingCurve::InOutQuad);

    if (m_kbVisible) {
        m_kbContainer->setVisible(true);
        m_kbAnim->setStartValue(0);
        m_kbAnim->setEndValue(220);
    } else {
        m_kbAnim->setStartValue(m_kbContainer->height());
        m_kbAnim->setEndValue(0);
        connect(m_kbAnim, &QPropertyAnimation::finished, this, [this]() {
            m_kbContainer->setVisible(false);
        });
    }

    m_kbAnim->start(QAbstractAnimation::DeleteWhenStopped);
    m_kbAnim = nullptr;
}

// ============================================================
// Прикрепления
// ============================================================

void MainWindow::rebuildAttachmentsBar()
{
    if (!m_attachBar || !m_attachLayout) return;

    while (QLayoutItem* item = m_attachLayout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }

    const auto* mgr = m_jarvis->attachments();

    if (mgr->isEmpty()) {
        m_attachBar->setVisible(false);
        m_attachSummary->setText(QString());
        return;
    }

    m_attachBar->setVisible(true);

    for (int i = 0; i < mgr->count(); ++i) {
        const auto& a = mgr->items()[i];

        auto* chip = new QWidget(m_attachBar);
        chip->setStyleSheet(
            QStringLiteral("background-color: #0f2438; border: 1px solid #1a4a70; "
                           "border-radius: 10px; padding: 2px 4px;"));

        auto* chipLay = new QHBoxLayout(chip);
        chipLay->setContentsMargins(8, 2, 4, 2);
        chipLay->setSpacing(4);

        QString iconEmoji;
        QString iconColor;
        if (a.isTooLarge) {
            iconEmoji = QStringLiteral("⚠");
            iconColor = QStringLiteral("#ff6b6b");
        } else if (a.isBinary) {
            iconEmoji = QStringLiteral("▣");
            iconColor = QStringLiteral("#ffaa00");
        } else {
            iconEmoji = QStringLiteral("📄");
            iconColor = QStringLiteral("#00d4ff");
        }

        auto* icon = new QLabel(iconEmoji, chip);
        icon->setStyleSheet(
            QStringLiteral("color: %1; font-size: 12px; border: none; background: transparent;")
            .arg(iconColor));

        auto* nameLabel = new QLabel(chip);
        QString displayText = a.displayName;
        if (displayText.length() > 28)
            displayText = displayText.left(25) + QStringLiteral("...");

        nameLabel->setText(displayText + QStringLiteral("  ")
                         + QStringLiteral("<span style='color:#5a7a90;'>(")
                         + AttachmentsManager::humanSize(a.sizeBytes)
                         + QStringLiteral(")</span>"));
        nameLabel->setStyleSheet(
            QStringLiteral("color: #c0dceb; font-size: 11px; border: none; background: transparent;"));
        nameLabel->setToolTip(a.filePath);

        auto* closeBtn = new QPushButton(QStringLiteral("✕"), chip);
        closeBtn->setFixedSize(18, 18);
        closeBtn->setCursor(Qt::PointingHandCursor);
        closeBtn->setStyleSheet(
            QStringLiteral("QPushButton { background: transparent; color: #5a7a90; border: none; "
                           "font-size: 12px; padding: 0; } "
                           "QPushButton:hover { color: #ff6b6b; }"));
        const int index = i;
        connect(closeBtn, &QPushButton::clicked, this, [this, index]() {
            m_jarvis->attachments()->removeAt(index);
        });

        chipLay->addWidget(icon);
        chipLay->addWidget(nameLabel);
        chipLay->addWidget(closeBtn);

        m_attachLayout->addWidget(chip);
    }

    m_attachLayout->addStretch(1);

    m_attachSummary->setText(QStringLiteral("📎 ")
                           + QString::number(mgr->count())
                           + (IS_EN
                              ? (mgr->count() == 1 ? QStringLiteral(" file, ") : QStringLiteral(" files, "))
                              : (mgr->count() == 1 ? QStringLiteral(" файл, ") : QStringLiteral(" файлов, ")))
                           + mgr->totalSizeHuman());
}

// ============================================================
// buildUI — без изменений в логике, только без мёртвого кода
// ============================================================

void MainWindow::buildUI()
{
    setWindowTitle(QStringLiteral("J.A.R.V.I.S. — Personal Assistant v")
                   + QCoreApplication::applicationVersion());
    setMinimumSize(680, 560);
    resize(760, 680);

    if (auto* scr = QApplication::primaryScreen()) {
        QRect g = scr->availableGeometry();
        move((g.width() - width()) / 2, (g.height() - height()) / 2);
    }

    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(16, 8, 16, 12);
    vbox->setSpacing(8);

    // === Заголовок ===
    auto* topBar = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("⬡  J.A.R.V.I.S."), this);
    title->setObjectName(QStringLiteral("titleLabel"));

    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_agentLabel = new QLabel(this);
    m_agentLabel->setVisible(false);
    m_agentLabel->setStyleSheet(
        QStringLiteral("color: #00d4ff; font-size: 11px; background: #0a1828; "
                       "border: 1px solid #1a3050; border-radius: 8px; padding: 2px 8px;"));

    m_dot = new QLabel(QStringLiteral("●"), this);
    m_dot->setStyleSheet(QStringLiteral("color: #00d4ff; font-size: 18px;"));

    m_status = new QLabel(IS_EN ? QStringLiteral("Online") : QStringLiteral("В сети"), this);
    m_status->setObjectName(QStringLiteral("statusText"));

    topBar->addWidget(title);
    topBar->addWidget(spacer);
    topBar->addWidget(m_agentLabel);
    topBar->addSpacing(8);
    topBar->addWidget(m_dot);
    topBar->addWidget(m_status);
    vbox->addLayout(topBar);

    auto* sep = new QLabel(this);
    sep->setObjectName(QStringLiteral("separator"));
    sep->setFixedHeight(1);
    vbox->addWidget(sep);

    // === Панель обновления ===
    m_updateBar = new QWidget(this);
    m_updateBar->setVisible(false);
    m_updateBar->setStyleSheet(
        QStringLiteral("background-color: #0c2018; border: 1px solid #00553a; "
                       "border-radius: 5px; padding: 4px 8px;"));

    auto* updateLayout = new QHBoxLayout(m_updateBar);
    updateLayout->setContentsMargins(10, 6, 10, 6);
    updateLayout->setSpacing(10);

    m_updateLabel = new QLabel(this);
    m_updateLabel->setStyleSheet(
        QStringLiteral("color: #00ff88; font-size: 13px; font-weight: bold; "
                       "border: none; background: transparent;"));

    m_updateProgress = new QProgressBar(this);
    m_updateProgress->setFixedWidth(120);
    m_updateProgress->setFixedHeight(16);
    m_updateProgress->setVisible(false);
    m_updateProgress->setStyleSheet(
        QStringLiteral("QProgressBar { background: #0a1018; border: 1px solid #1a3050; "
                       "border-radius: 3px; text-align: center; color: #96c8e6; font-size: 10px; }"
                       "QProgressBar::chunk { background: #00ff88; border-radius: 2px; }"));

    m_updateBtn = new QPushButton(IS_EN ? QStringLiteral("Update") : QStringLiteral("Обновить"), this);
    m_updateBtn->setFixedWidth(90);
    m_updateBtn->setStyleSheet(
        QStringLiteral("background-color: #003d2a; color: #00ff88; border: 1px solid #00663a; "
                       "border-radius: 4px; padding: 4px 12px; font-size: 12px; font-weight: bold;"));

    m_updateDismiss = new QPushButton(QStringLiteral("✕"), this);
    m_updateDismiss->setFixedWidth(28);
    m_updateDismiss->setStyleSheet(
        QStringLiteral("background-color: transparent; color: #3a5a70; border: none; font-size: 14px;"));

    updateLayout->addWidget(m_updateLabel, 1);
    updateLayout->addWidget(m_updateProgress);
    updateLayout->addWidget(m_updateBtn);
    updateLayout->addWidget(m_updateDismiss);
    vbox->addWidget(m_updateBar);

    connect(m_updateBtn, &QPushButton::clicked, this, [this]() {
        m_updateBtn->setVisible(false);
        m_updateProgress->setVisible(true);
        appendLog(Str::logSystem(),
                  IS_EN ? QStringLiteral("Downloading update...") : QStringLiteral("Скачиваю обновление..."),
                  Theme::LogColors::system);
        m_jarvis->autoUpdater()->downloadPendingUpdate();
    });
    connect(m_updateDismiss, &QPushButton::clicked, this, [this]() { hideUpdateBar(); });

    // === Лог ===
    m_log = new QTextEdit(this);
    m_log->setObjectName(QStringLiteral("logArea"));
    m_log->setReadOnly(true);
    m_log->setFocusPolicy(Qt::NoFocus);
    vbox->addWidget(m_log, 1);

    // === Панель предложений ===
    m_suggestionBar = new QWidget(this);
    m_suggestionBar->setVisible(false);
    m_suggestionBar->setStyleSheet(
        QStringLiteral("background-color: #0c1828; border: 1px solid #1a3050; "
                       "border-radius: 4px; padding: 4px 8px;"));

    auto* sugLayout = new QHBoxLayout(m_suggestionBar);
    sugLayout->setContentsMargins(8, 4, 8, 4);
    sugLayout->setSpacing(8);

    m_suggestionText = new QLabel(this);
    m_suggestionText->setStyleSheet(
        QStringLiteral("color: #ffcc00; font-size: 12px; border: none; background: transparent;"));

    m_suggestionBtn = new QPushButton(IS_EN ? QStringLiteral("Yes") : QStringLiteral("Да"), this);
    m_suggestionBtn->setFixedWidth(50);
    m_suggestionBtn->setStyleSheet(
        QStringLiteral("background-color: #00243d; color: #00d4ff; border: 1px solid #00587a; "
                       "border-radius: 3px; padding: 3px 8px; font-size: 11px;"));

    auto* sugDismiss = new QPushButton(QStringLiteral("✕"), this);
    sugDismiss->setFixedWidth(28);
    sugDismiss->setStyleSheet(
        QStringLiteral("background-color: transparent; color: #3a5a70; border: none; font-size: 14px;"));

    sugLayout->addWidget(m_suggestionText, 1);
    sugLayout->addWidget(m_suggestionBtn);
    sugLayout->addWidget(sugDismiss);
    vbox->addWidget(m_suggestionBar);

    connect(m_suggestionBtn, &QPushButton::clicked, this, [this]() {
        if (!m_pendingSuggestionAction.isEmpty()) {
            m_input->setText(m_pendingSuggestionAction);
            onSend();
        }
        m_suggestionBar->setVisible(false);
    });
    connect(sugDismiss, &QPushButton::clicked, this, [this]() {
        m_suggestionBar->setVisible(false);
    });

    // === Панель уточнения Brain ===
    m_clarifyBar = new QWidget(this);
    m_clarifyBar->setVisible(false);
    m_clarifyBar->setStyleSheet(
        QStringLiteral("background-color: #080f1a; border: 1px solid #003a5c; "
                       "border-radius: 4px;"));

    auto* clarifyVBox = new QVBoxLayout(m_clarifyBar);
    clarifyVBox->setContentsMargins(10, 6, 10, 6);
    clarifyVBox->setSpacing(6);

    m_clarifyText = new QLabel(this);
    m_clarifyText->setStyleSheet(
        QStringLiteral("color: #80c8e0; font-size: 12px; border: none; background: transparent;"));

    auto* clarifyBtnWidget = new QWidget(m_clarifyBar);
    clarifyBtnWidget->setStyleSheet(QStringLiteral("background: transparent;"));
    m_clarifyBtnLay = new QHBoxLayout(clarifyBtnWidget);
    m_clarifyBtnLay->setContentsMargins(0, 0, 0, 0);
    m_clarifyBtnLay->setSpacing(6);

    auto* clarifyDismiss = new QPushButton(QStringLiteral("✕"), m_clarifyBar);
    clarifyDismiss->setFixedSize(22, 22);
    clarifyDismiss->setStyleSheet(
        QStringLiteral("QPushButton { background: transparent; color: #3a5a70; "
                       "border: none; font-size: 14px; } "
                       "QPushButton:hover { color: #ff6b6b; }"));
    connect(clarifyDismiss, &QPushButton::clicked, this, [this]() { hideClarification(); });

    auto* clarifyTopRow = new QHBoxLayout();
    clarifyTopRow->addWidget(m_clarifyText, 1);
    clarifyTopRow->addWidget(clarifyDismiss);

    clarifyVBox->addLayout(clarifyTopRow);
    clarifyVBox->addWidget(clarifyBtnWidget);
    vbox->addWidget(m_clarifyBar);

    // === Панель прикреплений ===
    m_attachBar = new QWidget(this);
    m_attachBar->setVisible(false);
    m_attachBar->setStyleSheet(
        QStringLiteral("background-color: #0a1828; border: 1px solid #1a3050; border-radius: 4px;"));

    auto* attachVBox = new QVBoxLayout(m_attachBar);
    attachVBox->setContentsMargins(6, 4, 6, 4);
    attachVBox->setSpacing(4);

    auto* summaryRow = new QHBoxLayout();
    summaryRow->setSpacing(8);
    m_attachSummary = new QLabel(this);
    m_attachSummary->setStyleSheet(
        QStringLiteral("color: #80b4d0; font-size: 11px; border: none; background: transparent;"));

    auto* clearAllBtn = new QPushButton(IS_EN ? QStringLiteral("clear") : QStringLiteral("очистить"), this);
    clearAllBtn->setStyleSheet(
        QStringLiteral("QPushButton { background: transparent; color: #5a7a90; "
                       "border: none; font-size: 10px; text-decoration: underline; } "
                       "QPushButton:hover { color: #ff8080; }"));
    clearAllBtn->setCursor(Qt::PointingHandCursor);
    clearAllBtn->setFixedHeight(18);
    connect(clearAllBtn, &QPushButton::clicked, this, [this]() {
        m_jarvis->attachments()->clear();
    });

    summaryRow->addWidget(m_attachSummary);
    summaryRow->addStretch(1);
    summaryRow->addWidget(clearAllBtn);
    attachVBox->addLayout(summaryRow);

    m_attachScroll = new QScrollArea(this);
    m_attachScroll->setFrameShape(QFrame::NoFrame);
    m_attachScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_attachScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_attachScroll->setWidgetResizable(true);
    m_attachScroll->setFixedHeight(32);
    m_attachScroll->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    auto* attachInner = new QWidget(this);
    attachInner->setStyleSheet(QStringLiteral("background: transparent;"));
    m_attachLayout = new QHBoxLayout(attachInner);
    m_attachLayout->setContentsMargins(0, 0, 0, 0);
    m_attachLayout->setSpacing(6);

    m_attachScroll->setWidget(attachInner);
    attachVBox->addWidget(m_attachScroll);
    vbox->addWidget(m_attachBar);

    // === Ввод ===
    auto* inputBar = new QHBoxLayout();
    inputBar->setSpacing(8);

    m_attachBtn = new QPushButton(QStringLiteral("📎"), this);
    m_attachBtn->setFixedSize(40, 34);
    m_attachBtn->setToolTip(IS_EN
        ? QStringLiteral("Attach files (Ctrl+O) — or drag & drop into the window")
        : QStringLiteral("Прикрепить файлы (Ctrl+O) — можно также перетащить в окно"));
    m_attachBtn->setCursor(Qt::PointingHandCursor);
    m_attachBtn->setStyleSheet(
        QStringLiteral("QPushButton { background-color: #0a1828; color: #80b4d0; "
                       "border: 1px solid #1a3050; border-radius: 4px; font-size: 16px; } "
                       "QPushButton:hover { background-color: #0f2438; color: #00d4ff; }"));
    connect(m_attachBtn, &QPushButton::clicked, this, &MainWindow::onAttachClicked);

    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("inputField"));
    m_input->setPlaceholderText(Str::inputPlaceholder());

    auto* sendBtn = new QPushButton(QStringLiteral("▶"), this);
    sendBtn->setObjectName(QStringLiteral("sendBtn"));
    sendBtn->setFixedWidth(50);
    sendBtn->setToolTip(IS_EN ? QStringLiteral("Send (Enter)") : QStringLiteral("Отправить (Enter)"));

    // Кнопка микрофона
    m_micBtn = new QPushButton(QStringLiteral("🎤"), this);
    m_micBtn->setObjectName(QStringLiteral("micBtn"));
    m_micBtn->setFixedWidth(44);
    m_micBtn->setToolTip(IS_EN ? QStringLiteral("Voice input (Whisper)") : QStringLiteral("Голосовой ввод (Whisper)"));
    m_micBtn->setStyleSheet(
        QStringLiteral("QPushButton#micBtn { background-color: #0d1f2d; color: #4a7a9b; "
                       "border: 1px solid #1a3050; border-radius: 4px; font-size: 16px; } "
                       "QPushButton#micBtn:hover { background-color: #0f2438; color: #00d4ff; } "
                       "QPushButton#micBtn[active=true] { background-color: #1a0d0d; color: #ff4444; "
                       "border-color: #ff2222; }"));

    inputBar->addWidget(m_attachBtn);
    inputBar->addWidget(m_input, 1);
    inputBar->addWidget(m_micBtn);
    inputBar->addWidget(sendBtn);
    vbox->addLayout(inputBar);

    // === Нижняя панель ===
    auto* bottomBar = new QHBoxLayout();

    auto* modeLabel = new QLabel(QStringLiteral("v") + QCoreApplication::applicationVersion(), this);
    modeLabel->setStyleSheet(
        QStringLiteral("color: #2a4a60; font-size: 11px; border: none; background: transparent;"));

    auto* bSpacer = new QWidget(this);
    bSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* kbBtn = new QPushButton(QStringLiteral("⌨"), this);
    kbBtn->setObjectName(QStringLiteral("kbToggleBtn"));
    kbBtn->setFixedWidth(40);
    kbBtn->setToolTip(Str::menuKeyboard());

    bottomBar->addWidget(modeLabel);
    bottomBar->addWidget(bSpacer);
    bottomBar->addWidget(kbBtn);
    vbox->addLayout(bottomBar);

    // === Виртуальная клавиатура ===
    m_kbContainer = new QWidget(this);
    m_kbContainer->setMaximumHeight(0);
    m_kbContainer->setVisible(false);

    auto* kbLayout = new QVBoxLayout(m_kbContainer);
    kbLayout->setContentsMargins(0, 4, 0, 0);

    m_keyboard = new VirtualKeyboardWidget(m_kbContainer);
    kbLayout->addWidget(m_keyboard);
    vbox->addWidget(m_kbContainer);

    // === Подключения ===
    connect(sendBtn, &QPushButton::clicked, this, &MainWindow::onSend);
    connect(m_input, &QLineEdit::returnPressed, this, &MainWindow::onSend);
    connect(kbBtn, &QPushButton::clicked, this, &MainWindow::toggleKeyboard);
    connect(m_micBtn, &QPushButton::clicked, this, &MainWindow::onMicButtonClicked);

    // ── Инициализация голосового ввода ────────────────────
    m_voiceInput = new VoiceInput(this);

    connect(m_voiceInput, &VoiceInput::ready,
            this, &MainWindow::onVoiceReady);
    connect(m_voiceInput, &VoiceInput::textRecognized,
            this, &MainWindow::onVoiceText);
    connect(m_voiceInput, &VoiceInput::wakeWordDetected,
            this, &MainWindow::onWakeWord);
    connect(m_voiceInput, &VoiceInput::whisperModeDetected,
            this, &MainWindow::onWhisperMode);
    connect(m_voiceInput, &VoiceInput::speechDetected, this, [this]() {
        // Мигаем красным пока говорит пользователь
        m_micBtn->setStyleSheet(
            QStringLiteral("QPushButton#micBtn { background-color: #2a0d0d; color: #ff2222; "
                           "border: 1px solid #ff2222; border-radius: 4px; font-size: 16px; }"));
    });
    connect(m_voiceInput, &VoiceInput::initError, this, [this](const QString& err) {
        // initError теперь только для критических ошибок
        // скачивание идёт автоматически через downloadModelIfNeeded
        m_micBtn->setToolTip(err);
        appendLog(Str::logSystem(), QStringLiteral("🎤 ") + err, Theme::LogColors::error);
    });
    connect(m_voiceInput, &VoiceInput::modelDownloadProgress, this, [this](int pct) {
        m_micBtn->setEnabled(false);
        m_micBtn->setText(QStringLiteral("⬇"));
        m_micBtn->setToolTip(QStringLiteral("Downloading Whisper model... %1%").arg(pct));
        m_status->setText(
            IS_EN ? QStringLiteral("⬇ Downloading Whisper model %1%...").arg(pct)
                  : QStringLiteral("⬇ Скачиваю модель Whisper %1%...").arg(pct));
        if (pct == 0) {
            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("⬇ Whisper model not found. Downloading (~1.5 GB)...")
                      : QStringLiteral("⬇ Модель Whisper не найдена. Скачиваю (~1.5 ГБ)..."),
                Theme::LogColors::system);
        }
    });
    connect(m_voiceInput, &VoiceInput::modelDownloadFinished, this, [this](bool ok, const QString& err) {
        if (ok) {
            m_micBtn->setEnabled(true);
            m_micBtn->setText(QStringLiteral("🎤"));
            m_status->setText(IS_EN ? QStringLiteral("Ready") : QStringLiteral("Готов"));
            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("✅ Whisper model downloaded. Voice input ready!")
                      : QStringLiteral("✅ Модель Whisper скачана. Голосовой ввод готов!"),
                Theme::LogColors::system);
        } else {
            m_micBtn->setEnabled(false);
            m_micBtn->setText(QStringLiteral("🎤"));
            m_micBtn->setToolTip(err);
            appendLog(Str::logError(), err, Theme::LogColors::error);
        }
    });
    connect(m_voiceInput, &VoiceInput::errorOccurred, this, [this](const QString& err) {
        appendLog(Str::logError(), err, Theme::LogColors::error);
    });

    // Инициализируем Whisper (загружает модель в фоне)
    m_voiceInput->initialize();

    connect(m_keyboard, &VirtualKeyboardWidget::charPressed, this, [this](const QString& ch) {
        m_input->insert(ch);
        m_input->setFocus();
    });
    connect(m_keyboard, &VirtualKeyboardWidget::backspacePressed, this, [this]() {
        m_input->backspace();
        m_input->setFocus();
    });
    connect(m_keyboard, &VirtualKeyboardWidget::enterPressed, this, &MainWindow::onSend);
}

// ============================================================
// appendLog
// ============================================================

void MainWindow::appendLog(const QString& who, const QString& text, const QString& color)
{
    QString time = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    QString html = QStringLiteral(
        "<div style='margin-bottom:6px;'>"
        "<span style='color:%1;'>[%2]</span> "
        "<span style='color:%3;'>%4:</span> "
        "<span style='color:%5;'>%6</span></div>"
    ).arg(Theme::LogColors::timestamp, time, color, who, color,
          text.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>")));

    m_log->append(html);
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}
// ============================================================
// onCommandLearned — уведомление о выученной команде
// ============================================================

void MainWindow::onCommandLearned(const LearnedCommand& cmd)
{
    const QString msg = IS_EN
        ? QStringLiteral("✅ Learned: \"%1\" (%2 steps). Will run locally next time.")
              .arg(cmd.description).arg(cmd.steps.size())
        : QStringLiteral("✅ Запомнил: \"%1\" (%2 шага). Следующий раз выполню сам.")
              .arg(cmd.description).arg(cmd.steps.size());
    appendLog(Str::logJarvis(), msg, QStringLiteral("#4a9a6a"));
}

// ============================================================
// handleVisualCommand — зрение + управление экраном
// ============================================================

void MainWindow::handleVisualCommand(const QString& userText)
{
    if (!m_screenAgent) return;

    const QString lo = userText.toLower();

    // "что видишь" / "опиши экран" — описание без действий
    if (lo.contains(QStringLiteral("что видишь")) ||
        lo.contains(QStringLiteral("what do you see")) ||
        lo.contains(QStringLiteral("опиши экран")) ||
        lo.contains(QStringLiteral("что на экране")) ||
        lo.contains(QStringLiteral("describe screen")))
    {
        appendLog(QStringLiteral("🧠 Brain"),
                  QStringLiteral("action=Visual domain=Screen"),
                  QStringLiteral("#4a6080"));
        appendLog(Str::logJarvis(),
                  IS_EN ? QStringLiteral("📸 Capturing screen, analyzing...")
                        : QStringLiteral("📸 Делаю скриншот, анализирую..."),
                  Theme::LogColors::system);

        const QString apiKey = m_jarvis->claudeApi()->hasApiKey()
            ? m_jarvis->claudeApi()->apiKey() : QString();

        if (apiKey.isEmpty()) {
            appendLog(Str::logJarvis(),
                      IS_EN ? QStringLiteral("Vision requires Claude API key.")
                            : QStringLiteral("Зрение требует ключ Claude API."),
                      Theme::LogColors::error);
            return;
        }

        setThinkingState(true);
        m_screenAgent->describeScreen(apiKey, [this](const QString& desc) {
            setThinkingState(false);
            appendLog(Str::logJarvis(), desc, Theme::LogColors::jarvis);
            m_jarvis->memory()->addMessage(QStringLiteral("user"),
                                           QStringLiteral("[screen description request]"));
            m_jarvis->memory()->addMessage(QStringLiteral("assistant"), desc);
        });
        return;
    }

    // "найди на экране X" / "кликни на X" — поиск текста + клик
    static const QRegularExpression reFindClick(
        QStringLiteral(R"((?:найди на экране|нажми на|кликни на|кликни по|нажми кнопку|find on screen|click on)\s+(.+))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = reFindClick.match(lo);
    if (m.hasMatch()) {
        const QString searchText = m.captured(1).trimmed();
        appendLog(QStringLiteral("🧠 Brain"),
                  QStringLiteral("action=Visual domain=Screen query=\"") + searchText + QStringLiteral("\""),
                  QStringLiteral("#4a6080"));
        appendLog(Str::logJarvis(),
                  (IS_EN ? QStringLiteral("🔍 Looking for: ") : QStringLiteral("🔍 Ищу: ")) + searchText,
                  Theme::LogColors::system);

        const bool found = m_screenAgent->clickText(searchText);
        const QString resp = found
            ? (IS_EN ? QStringLiteral("✓ Found and clicked: ") : QStringLiteral("✓ Нашёл и кликнул: ")) + searchText
            : (IS_EN ? QStringLiteral("✗ Not found on screen: ") : QStringLiteral("✗ Не найдено на экране: ")) + searchText;
        appendLog(Str::logJarvis(), resp, found ? Theme::LogColors::jarvis : Theme::LogColors::error);
        m_jarvis->memory()->addMessage(QStringLiteral("user"), userText);
        m_jarvis->memory()->addMessage(QStringLiteral("assistant"), resp);
        return;
    }

    // Общая визуальная команда — скриншот + Claude Vision → инструкции → выполнение
    const QString apiKey = m_jarvis->claudeApi()->hasApiKey()
        ? m_jarvis->claudeApi()->apiKey() : QString();

    if (apiKey.isEmpty()) {
        appendLog(Str::logJarvis(),
                  IS_EN ? QStringLiteral("Visual commands require Claude API key.")
                        : QStringLiteral("Визуальные команды требуют ключ Claude API."),
                  Theme::LogColors::error);
        return;
    }

    appendLog(QStringLiteral("🧠 Brain"), QStringLiteral("action=Visual domain=Screen"), QStringLiteral("#4a6080"));
    appendLog(Str::logJarvis(),
              IS_EN ? QStringLiteral("📸 Analyzing screen and executing...")
                    : QStringLiteral("📸 Анализирую экран и выполняю..."),
              Theme::LogColors::system);
    setThinkingState(true);

    m_screenAgent->executeVisualCommand(userText, apiKey, [this, userText](const QString& result) {
        setThinkingState(false);
        appendLog(Str::logJarvis(), result, Theme::LogColors::jarvis);
        m_jarvis->memory()->addMessage(QStringLiteral("user"), userText);
        m_jarvis->memory()->addMessage(QStringLiteral("assistant"), result);
    });
}

// ============================================================
// Голосовой ввод — слоты
// ============================================================

void MainWindow::onMicButtonClicked()
{
    if (!m_voiceInput) return;

    if (!m_voiceActive) {
        // Запускаем прослушивание
        m_voiceInput->startListening();
        m_voiceActive = true;
        m_micBtn->setText(QStringLiteral("🔴"));
        m_micBtn->setProperty("active", true);
        m_micBtn->setToolTip(IS_EN ? QStringLiteral("Listening... (click to stop)")
                                   : QStringLiteral("Слушаю... (нажми чтобы остановить)"));
        m_micBtn->style()->unpolish(m_micBtn);
        m_micBtn->style()->polish(m_micBtn);
        m_status->setText(IS_EN ? QStringLiteral("🎤 Listening...")
                                : QStringLiteral("🎤 Слушаю..."));
        appendLog(Str::logJarvis(),
                  IS_EN ? QStringLiteral("🎤 Voice input started. Say 'Jarvis' to activate.")
                        : QStringLiteral("🎤 Голосовой ввод запущен. Скажите «Джарвис» для активации."),
                  Theme::LogColors::system);
    } else {
        // Останавливаем
        m_voiceInput->stopListening();
        m_voiceActive = false;
        m_micBtn->setText(QStringLiteral("🎤"));
        m_micBtn->setProperty("active", false);
        m_micBtn->setToolTip(IS_EN ? QStringLiteral("Voice input (Whisper)")
                                   : QStringLiteral("Голосовой ввод (Whisper)"));
        m_micBtn->style()->unpolish(m_micBtn);
        m_micBtn->style()->polish(m_micBtn);
        m_status->setText(IS_EN ? QStringLiteral("Ready") : QStringLiteral("Готов"));
    }
}

void MainWindow::onVoiceReady()
{
    m_micBtn->setEnabled(true);
    appendLog(Str::logSystem(),
              IS_EN ? QStringLiteral("🎤 Whisper model loaded. Voice input ready.")
                    : QStringLiteral("🎤 Модель Whisper загружена. Голосовой ввод готов."),
              Theme::LogColors::system);
}

void MainWindow::onVoiceText(const QString& text, const QString& lang)
{
    // Сбрасываем цвет кнопки — запись закончена, идёт распознавание
    m_micBtn->setStyleSheet(QString());
    m_micBtn->style()->unpolish(m_micBtn);
    m_micBtn->style()->polish(m_micBtn);

    if (text.isEmpty()) return;

    // Показываем распознанный текст в поле ввода и отправляем
    const QString prefix = (lang == QStringLiteral("ru"))
        ? QStringLiteral("🎤 ")
        : QStringLiteral("🎤 ");

    appendLog(QStringLiteral("🎤 Voice"),
              QStringLiteral("[%1] %2").arg(lang.toUpper(), text),
              QStringLiteral("#4a9a6a"));

    m_input->setText(text);
    // Автоматически отправляем голосовую команду
    onSend();
}

void MainWindow::onWakeWord(const QString& word)
{
    appendLog(Str::logJarvis(),
              QStringLiteral("👂 ") + (IS_EN ? QStringLiteral("Wake word: ") : QStringLiteral("Активация: ")) + word,
              Theme::LogColors::system);
    m_status->setText(IS_EN ? QStringLiteral("🎤 Speak now...")
                            : QStringLiteral("🎤 Говорите..."));
}

void MainWindow::onWhisperMode(bool isWhisper)
{
    if (isWhisper) {
        m_micBtn->setToolTip(IS_EN ? QStringLiteral("🤫 Whisper detected")
                                   : QStringLiteral("🤫 Обнаружен шёпот"));
        appendLog(Str::logSystem(),
                  IS_EN ? QStringLiteral("🤫 Whisper detected — low volume mode active")
                        : QStringLiteral("🤫 Обнаружен шёпот — режим тихого голоса"),
                  Theme::LogColors::system);
    }
}
