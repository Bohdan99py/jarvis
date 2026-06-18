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
#include "passive_listener.h"
#include "database_manager.h"
#include "VoskSetupDialog.h"
#include <QFileDialog>
#include <QDialog>
#include <QTextEdit>
#include <QTextBrowser>
#include <QListWidget>
#include <QFrame>
#include <QUuid>
#include <QJsonObject>
#include <QJsonDocument>
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
#include <QActionGroup>
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

    // ── Перестраиваем всё меню — все Str::* вернут новые строки ──
    // Откладываем на следующий цикл: текущий QAction ещё в стеке вызовов
    QTimer::singleShot(0, this, [this]() {
        menuBar()->clear();
        buildMenuBar();
    });

    // ── Заголовок окна ───────────────────────────────────────
    setWindowTitle(IS_EN
        ? QStringLiteral("J.A.R.V.I.S. — Personal Assistant v%1").arg(QStringLiteral(JARVIS_VERSION))
        : QStringLiteral("J.A.R.V.I.S. — Персональный ассистент v%1").arg(QStringLiteral(JARVIS_VERSION)));

    // ── Поле ввода ───────────────────────────────────────────
    m_input->setPlaceholderText(Str::inputPlaceholder());

    // ── Статус-бар ───────────────────────────────────────────
    m_status->setText(IS_EN ? QStringLiteral("Ready") : QStringLiteral("Готов"));

    // ── Кнопка микрофона ─────────────────────────────────────
    if (m_micBtn) {
        if (!m_voiceActive) {
            m_micBtn->setToolTip(IS_EN ? QStringLiteral("Voice input (Vosk)")
                                       : QStringLiteral("Голосовой ввод (Vosk)"));
        }
    }

    // ── Кнопка лайка ─────────────────────────────────────────
    if (m_likeBtn) {
        m_likeBtn->setToolTip(IS_EN
            ? QStringLiteral("Like this response — save for AI training")
            : QStringLiteral("Лайкнуть ответ — сохранить для обучения ИИ"));
    }

    // ── Кнопка прикрепления ──────────────────────────────────
    if (m_attachBtn) {
        m_attachBtn->setToolTip(IS_EN
            ? QStringLiteral("Attach files (Ctrl+O)")
            : QStringLiteral("Прикрепить файлы (Ctrl+O)"));
    }

    // ── Панель обновления ────────────────────────────────────
    if (m_updateBtn) {
        m_updateBtn->setText(IS_EN ? QStringLiteral("Update")
                                   : QStringLiteral("Обновить"));
    }
    if (m_updateDismiss) {
        m_updateDismiss->setToolTip(IS_EN ? QStringLiteral("Dismiss")
                                          : QStringLiteral("Скрыть"));
    }

    // ── Панель предложений ───────────────────────────────────
    if (m_suggestionBtn) {
        m_suggestionBtn->setText(IS_EN ? QStringLiteral("Yes")
                                       : QStringLiteral("Да"));
    }

    // ── Лог: сообщение о смене языка ─────────────────────────
    appendLog(Str::logSystem(),
              IS_EN ? QStringLiteral("✅ Interface language changed to English.")
                    : QStringLiteral("✅ Язык интерфейса изменён на русский."),
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

    // ── Управление голосовыми моделями ──────────────────────
    settingsMenu->addSeparator();
    auto* actVoiceModels = settingsMenu->addAction(
        IS_EN ? QStringLiteral("🎤 Voice Models...")
              : QStringLiteral("🎤 Голосовые модели..."));
    connect(actVoiceModels, &QAction::triggered, this, [this]() {
        const bool isEn = IS_EN;

        // Стековый QDialog — не нужен ни new ни WA_DeleteOnClose, деструктор сам всё чистит
        QDialog dlg(this);
        dlg.setWindowTitle(isEn ? QStringLiteral("JARVIS — Voice Models")
                                : QStringLiteral("JARVIS — Голосовые модели"));
        dlg.setMinimumSize(640, 520);
        dlg.setStyleSheet(QStringLiteral(
            "QDialog { background: #0a0a1a; color: #ecf0f1; }"
            "QPushButton { background: #0f2438; color: #00d4ff; "
            "border: 1px solid #1a5070; border-radius: 4px; padding: 5px 18px; }"
            "QPushButton:hover { background: #1a3a5c; }"));

        auto* layout = new QVBoxLayout(&dlg);
        layout->setContentsMargins(16, 16, 16, 12);

        // manager — parent = &dlg, удаляется вместе с диалогом
        auto* manager = new VoskModelManagerWidget(m_voiceInput, &dlg);
        connect(manager, &VoskModelManagerWidget::modelsChanged, this, [this, isEn]() {
            appendLog(Str::logSystem(),
                isEn ? QStringLiteral("🔄 Voice models updated — reloading...")
                     : QStringLiteral("🔄 Модели обновлены — перезагрузка..."),
                Theme::LogColors::system);
        });

        auto* scrollArea = new QScrollArea(&dlg);
        scrollArea->setWidgetResizable(true);
        scrollArea->setWidget(manager);
        scrollArea->setStyleSheet(QStringLiteral(
            "QScrollArea { border: none; background: transparent; }"
            "QScrollBar:vertical { background: #111; width: 6px; }"
            "QScrollBar::handle:vertical { background: #333; border-radius: 3px; }"));
        layout->addWidget(scrollArea, 1);

        auto* btnClose = new QPushButton(
            isEn ? QStringLiteral("Close") : QStringLiteral("Закрыть"), &dlg);
        btnClose->setFixedWidth(100);
        connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
        auto* btnRow = new QHBoxLayout();
        btnRow->addStretch(); btnRow->addWidget(btnClose); btnRow->addStretch();
        layout->addLayout(btnRow);

        dlg.exec();
        // После exec() dlg деструктор удалит manager, который сам отключит свои сигналы
    });
    settingsMenu->addSeparator();
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
            IS_EN ? QStringLiteral(
                "Model name:\n\n"
                "Fast (recommended):\n"
                "  qwen2.5:3b      — very fast, good quality\n"
                "  phi3:mini       — fast, Microsoft model\n"
                "  gemma2:2b       — fast Google model\n\n"
                "Quality:\n"
                "  llama3.2:3b     — Meta, good balance\n"
                "  mistral:7b      — good for code\n"
                "  qwen2.5:7b      — best quality\n\n"
                "Install: ollama pull qwen2.5:3b")
                  : QStringLiteral(
                "Имя модели:\n\n"
                "Быстрые (рекомендую):\n"
                "  qwen2.5:3b      — очень быстро, хорошее качество\n"
                "  phi3:mini       — быстро, модель Microsoft\n"
                "  gemma2:2b       — быстро, модель Google\n\n"
                "Качественные:\n"
                "  llama3.2:3b     — Meta, хороший баланс\n"
                "  mistral:7b      — хороша для кода\n"
                "  qwen2.5:7b      — лучшее качество\n\n"
                "Установить: ollama pull qwen2.5:3b"),
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

    // QActionGroup даёт radio-поведение — одно из двух всегда выбрано
    auto* langGroup = new QActionGroup(langMenu);
    langGroup->setExclusive(true);

    auto* actLangRu = langMenu->addAction(Str::menuLangRu());
    actLangRu->setCheckable(true);
    actLangRu->setChecked(gUiLanguage() == UiLanguage::Russian);
    langGroup->addAction(actLangRu);
    connect(actLangRu, &QAction::triggered, this, [this](bool checked) {
        if (checked) applyLanguage(false);
    });

    auto* actLangEn = langMenu->addAction(Str::menuLangEn());
    actLangEn->setCheckable(true);
    actLangEn->setChecked(gUiLanguage() == UiLanguage::English);
    langGroup->addAction(actLangEn);
    connect(actLangEn, &QAction::triggered, this, [this](bool checked) {
        if (checked) applyLanguage(true);
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

    // --- AI Training (fine-tuning датасет) ---
    auto* trainMenu = menuBar->addMenu(
        IS_EN ? QStringLiteral("🧠 Training") : QStringLiteral("🧠 Обучение"));

    // --- Статистика (одна кнопка вместо двух) ---
    auto* actTrainStats = trainMenu->addAction(
        IS_EN ? QStringLiteral("📊 Dataset Statistics")
              : QStringLiteral("📊 Статистика датасета"));
    connect(actTrainStats, &QAction::triggered, this, [this]() {
        auto& db = DatabaseManager::instance();
        int total     = db.trainingLogCount(1);
        int liked     = db.trainingLogCount(1);  // rated=1
        int jTotal    = db.voiceJournalCount(1, false);
        int jDone     = db.voiceJournalCount(1, true);

        auto* dlg = new QDialog(this);
        dlg->setWindowTitle(IS_EN ? QStringLiteral("Training Statistics")
                                  : QStringLiteral("Статистика обучения"));
        dlg->setMinimumWidth(420);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setStyleSheet(QStringLiteral(
            "QDialog{background:#0a1018;color:#c8e0f0;}"
            "QLabel{color:#c8e0f0;font-size:12px;}"
            "QPushButton{background:#0f2438;color:#00d4ff;border:1px solid #1a5070;"
            "padding:5px 20px;border-radius:4px;}"
            "QPushButton:hover{background:#1a3a5c;}"));
        auto* lay = new QVBoxLayout(dlg);
        lay->setContentsMargins(20,16,20,16);
        lay->setSpacing(8);

        auto addRow = [&](const QString& icon, const QString& label, const QString& val) {
            auto* row = new QHBoxLayout();
            auto* lbl = new QLabel(icon + QStringLiteral(" ") + label, dlg);
            auto* v   = new QLabel(QStringLiteral("<b style='color:#00d4ff;'>") + val + QStringLiteral("</b>"), dlg);
            v->setTextFormat(Qt::RichText);
            row->addWidget(lbl);
            row->addStretch();
            row->addWidget(v);
            lay->addLayout(row);
        };

        lay->addWidget(new QLabel(
            QStringLiteral("<b style='color:#00d4ff;font-size:13px;'>")
            + (IS_EN ? QStringLiteral("Training Dataset") : QStringLiteral("Датасет обучения"))
            + QStringLiteral("</b>"), dlg));

        addRow("💬", IS_EN ? "Total pairs saved" : "Всего пар сохранено",
               QString::number(total));
        addRow("👍", IS_EN ? "Goal (export ready)" : "Цель (готово к экспорту)",
               QStringLiteral("500+"));
        addRow("📈", IS_EN ? "Progress" : "Прогресс",
               QString::number(qMin(total * 100 / qMax(500, 1), 100)) + QStringLiteral("%"));

        auto* line = new QFrame(dlg);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet(QStringLiteral("color:#1a3050;"));
        lay->addWidget(line);

        lay->addWidget(new QLabel(
            QStringLiteral("<b style='color:#44aaff;font-size:13px;'>")
            + (IS_EN ? QStringLiteral("Voice Journal") : QStringLiteral("Голосовой журнал"))
            + QStringLiteral("</b>"), dlg));

        addRow("🎙️", IS_EN ? "Total recorded phrases" : "Записано фраз", QString::number(jTotal));
        addRow("✅", IS_EN ? "Processed → training" : "Обработано → обучение", QString::number(jDone));
        addRow("⏳", IS_EN ? "Pending processing" : "Ожидает обработки",
               QString::number(qMax(0, jTotal - jDone)));

        bool passive = m_passiveListener && m_passiveListener->isListening();
        addRow("🔴", IS_EN ? "Recording status" : "Статус записи",
               passive ? (IS_EN ? QStringLiteral("Active") : QStringLiteral("Активна"))
                       : (IS_EN ? QStringLiteral("Stopped") : QStringLiteral("Остановлена")));

        auto* btnOk = new QPushButton(QStringLiteral("OK"), dlg);
        btnOk->setFixedWidth(100);
        connect(btnOk, &QPushButton::clicked, dlg, &QDialog::accept);
        auto* btnRow = new QHBoxLayout();
        btnRow->addStretch(); btnRow->addWidget(btnOk); btnRow->addStretch();
        lay->addLayout(btnRow);
        dlg->exec();
    });

    trainMenu->addSeparator();

    // --- Экспорт ---
    auto* actExport = trainMenu->addAction(
        IS_EN ? QStringLiteral("📤 Export .jsonl for Fine-Tuning...")
              : QStringLiteral("📤 Экспорт .jsonl для обучения..."));
    connect(actExport, &QAction::triggered, this, &MainWindow::onExportTrainingData);

    // --- Поиск по истории чатов ---
    auto* actSearch = trainMenu->addAction(
        IS_EN ? QStringLiteral("🔍 Search chat history...")
              : QStringLiteral("🔍 Поиск по истории чатов..."));
    connect(actSearch, &QAction::triggered, this, [this]() {
        bool ok;
        QString query = QInputDialog::getText(this,
            IS_EN ? QStringLiteral("Search History") : QStringLiteral("Поиск по истории"),
            IS_EN ? QStringLiteral("Enter search term:")
                  : QStringLiteral("Введите поисковый запрос:"),
            QLineEdit::Normal, QString(), &ok);
        if (!ok || query.trimmed().isEmpty()) return;

        auto& db = DatabaseManager::instance();
        auto logs = db.getTrainingLogs(1, 10000);
        QStringList results;
        QString lower = query.toLower();
        for (const DbTrainingLog& log : logs) {
            if (log.userMessage.toLower().contains(lower)
             || log.aiResponse.toLower().contains(lower)) {
                results.append(QStringLiteral("▶ ") + log.userMessage.left(60)
                    + QStringLiteral("\n  -> ") + log.aiResponse.left(80));
            }
            if (results.size() >= 20) break;
        }

        if (results.isEmpty()) {
            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("🔍 No results for: \"%1\"").arg(query)
                      : QStringLiteral("🔍 Ничего не найдено: \"%1\"").arg(query),
                Theme::LogColors::system);
            return;
        }

        auto* dlg = new QDialog(this);
        dlg->setWindowTitle(IS_EN ? QStringLiteral("Search Results: \"%1\"").arg(query)
                                  : QStringLiteral("Результаты: \"%1\"").arg(query));
        dlg->setMinimumSize(640, 480);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setStyleSheet(QStringLiteral(
            "QDialog{background:#0a1018;color:#c8e0f0;}"
            "QTextBrowser{background:#0d1a28;color:#c8e0f0;border:1px solid #1a3050;"
            "font-size:12px;border-radius:4px;}"
            "QPushButton{background:#0f2438;color:#00d4ff;border:1px solid #1a5070;"
            "padding:5px 20px;border-radius:4px;}"));
        auto* lay = new QVBoxLayout(dlg);
        lay->setContentsMargins(16,14,16,14);

        auto* titleLbl = new QLabel(
            IS_EN ? QStringLiteral("<b style='color:#00d4ff;'>Found %1 results for \"%2\"</b>")
                        .arg(results.size()).arg(query)
                  : QStringLiteral("<b style='color:#00d4ff;'>Найдено %1 результатов для \"%2\"</b>")
                        .arg(results.size()).arg(query), dlg);
        titleLbl->setTextFormat(Qt::RichText);
        lay->addWidget(titleLbl);

        auto* browser = new QTextBrowser(dlg);
        QString html;
        for (const QString& r : results) {
            QString highlighted = r;
            highlighted.replace(query, QStringLiteral("<b style='color:#ffdd44;'>") + query + QStringLiteral("</b>"),
                               Qt::CaseInsensitive);
            html += QStringLiteral("<p style='border-bottom:1px solid #1a3050;padding:6px 0;'>")
                 + highlighted.toHtmlEscaped()
                     .replace(QStringLiteral("&lt;b style=&#39;color:#ffdd44;&#39;&gt;"), QStringLiteral("<b style='color:#ffdd44;'>"))
                     .replace(QStringLiteral("&lt;/b&gt;"), QStringLiteral("</b>"))
                 + QStringLiteral("</p>");
        }
        browser->setHtml(html);
        lay->addWidget(browser, 1);

        auto* btn = new QPushButton(QStringLiteral("OK"), dlg);
        btn->setFixedWidth(100);
        connect(btn, &QPushButton::clicked, dlg, &QDialog::accept);
        auto* btnRow = new QHBoxLayout();
        btnRow->addStretch(); btnRow->addWidget(btn); btnRow->addStretch();
        lay->addLayout(btnRow);
        dlg->exec();
    });

    trainMenu->addSeparator();

    // --- Скриншот с AI описанием ---
    auto* actScreenshot = trainMenu->addAction(
        IS_EN ? QStringLiteral("📸 Screenshot + AI Description")
              : QStringLiteral("📸 Скриншот + описание AI"));
    connect(actScreenshot, &QAction::triggered, this, [this]() {
        if (!m_screenAgent) return;

        // Берём API ключ — сначала Claude, потом Gemini
        QString apiKey = m_jarvis->claudeApi()->apiKey();
        if (apiKey.isEmpty()) apiKey = m_jarvis->geminiBackup() ? m_jarvis->geminiBackup()->apiKey() : QString();
        if (apiKey.isEmpty()) {
            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("📸 Need Claude or Gemini API key for screenshot analysis")
                      : QStringLiteral("📸 Нужен ключ Claude или Gemini API для анализа скриншота"),
                Theme::LogColors::error);
            return;
        }

        appendLog(Str::logSystem(),
            IS_EN ? QStringLiteral("📸 Taking screenshot and analyzing...")
                  : QStringLiteral("📸 Делаю скриншот и анализирую..."),
            Theme::LogColors::system);

        m_screenAgent->describeScreen(apiKey, [this](const QString& desc) {
            if (desc.isEmpty()) return;

            appendLog(Str::logJarvis(),
                IS_EN ? QStringLiteral("📸 Screen: ") + desc
                      : QStringLiteral("📸 Экран: ") + desc,
                Theme::LogColors::jarvis);

            // Сохраняем в training_logs как пара (контекст экрана → описание)
            if (m_passiveListener) {
                m_passiveListener->addVoiceCommandPair(
                    QStringLiteral("[screenshot] what do you see on the screen?"),
                    desc,
                    QStringLiteral("en"));
            }
        });
    });

    // Автоскриншот каждые N минут (из настроек)
    auto* actAutoScreen = trainMenu->addAction(
        IS_EN ? QStringLiteral("⏱️ Auto-screenshot: OFF")
              : QStringLiteral("⏱️ Авто-скриншот: ВЫКЛ"));
    actAutoScreen->setCheckable(true);
    actAutoScreen->setChecked(false);
    connect(actAutoScreen, &QAction::triggered, this, [this, actAutoScreen](bool checked) {
        if (checked) {
            // Таймер каждые 5 минут
            if (!m_screenshotTimer) {
                m_screenshotTimer = new QTimer(this);
                m_screenshotTimer->setInterval(5 * 60 * 1000);
                connect(m_screenshotTimer, &QTimer::timeout, this, [this]() {
                    if (!m_screenAgent) return;
                    QString apiKey = m_jarvis->claudeApi()->apiKey();
                    if (apiKey.isEmpty()) apiKey = m_jarvis->geminiBackup() ? m_jarvis->geminiBackup()->apiKey() : QString();
                    if (apiKey.isEmpty()) return;
                    m_screenAgent->describeScreen(apiKey, [this](const QString& desc) {
                        if (desc.isEmpty() || !m_passiveListener) return;
                        m_passiveListener->addVoiceCommandPair(
                            QStringLiteral("[auto-screenshot] describe current screen context"),
                            desc, QStringLiteral("en"));
                        qDebug() << "[Training] Auto-screenshot saved to dataset";
                    });
                });
            }
            m_screenshotTimer->start();
            actAutoScreen->setText(
                IS_EN ? QStringLiteral("⏱️ Auto-screenshot: ON (5 min)")
                      : QStringLiteral("⏱️ Авто-скриншот: ВКЛ (5 мин)"));
        } else {
            if (m_screenshotTimer) m_screenshotTimer->stop();
            actAutoScreen->setText(
                IS_EN ? QStringLiteral("⏱️ Auto-screenshot: OFF")
                      : QStringLiteral("⏱️ Авто-скриншот: ВЫКЛ"));
        }
    });

    trainMenu->addSeparator();

    // --- Пассивная запись (тумблер) ---
    m_passiveAction = trainMenu->addAction(
        IS_EN ? QStringLiteral("🎙️ Passive Recording: OFF")
              : QStringLiteral("🎙️ Пассивная запись: ВЫКЛ"));
    m_passiveAction->setCheckable(true);
    m_passiveAction->setChecked(false);
    connect(m_passiveAction, &QAction::triggered, this, [this](bool checked) {
        if (!m_passiveListener) return;
        if (checked) {
            m_passiveListener->startListening();
            m_passiveAction->setText(
                IS_EN ? QStringLiteral("🎙️ Passive Recording: ON")
                      : QStringLiteral("🎙️ Пассивная запись: ВКЛ"));
        } else {
            m_passiveListener->stopListening();
            m_passiveAction->setText(
                IS_EN ? QStringLiteral("🎙️ Passive Recording: OFF")
                      : QStringLiteral("🎙️ Пассивная запись: ВЫКЛ"));
        }
    });

    trainMenu->addSeparator();

    // --- Папка датасета ---
    auto* actSetDatasetPath = trainMenu->addAction(
        IS_EN ? QStringLiteral("📁 Dataset folder...")
              : QStringLiteral("📁 Папка датасета..."));
    connect(actSetDatasetPath, &QAction::triggered, this, [this]() {
        QString current = DatabaseManager::instance().getConfig(
            QStringLiteral("voice_dataset_path"),
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + QStringLiteral("/voice_dataset")).toString();

        QString path = QFileDialog::getExistingDirectory(this,
            IS_EN ? QStringLiteral("Select dataset folder")
                  : QStringLiteral("Выберите папку для датасета"),
            current);

        if (!path.isEmpty()) {
            DatabaseManager::instance().setConfig(
                QStringLiteral("voice_dataset_path"), path);
            if (m_passiveListener) {
                auto cfg = m_passiveListener->config();
                cfg.datasetPath = path;
                m_passiveListener->setConfig(cfg);
            }
        }
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

    // ── EULA — лицензионное соглашение (шуточное) ────────
    auto* actEula = helpMenu->addAction(
        IS_EN ? QStringLiteral("📜 License Agreement (EULA)")
              : QStringLiteral("📜 Лицензионное соглашение (EULA)"));
    connect(actEula, &QAction::triggered, this, [this]() {
        auto* dlg = new QDialog(this);
        dlg->setWindowTitle(IS_EN ? QStringLiteral("J.A.R.V.I.S. — License Agreement")
                                  : QStringLiteral("J.A.R.V.I.S. — Лицензионное соглашение"));
        dlg->setMinimumSize(620, 500);
        dlg->setAttribute(Qt::WA_DeleteOnClose);

        auto* layout = new QVBoxLayout(dlg);
        auto* text = new QTextEdit(dlg);
        text->setReadOnly(true);
        text->setStyleSheet(QStringLiteral(
            "QTextEdit { background: #0a1018; color: #96c8e6; "
            "border: 1px solid #1a3050; font-family: 'Consolas'; font-size: 12px; }"));

        // Пробуем загрузить из файла рядом с exe (EN или RU версию)
        QString eulaFileEn = QCoreApplication::applicationDirPath()
                           + QStringLiteral("/JARVIS_EULA_EN.txt");
        QString eulaFileRu = QCoreApplication::applicationDirPath()
                           + QStringLiteral("/EULA_JARVIS.txt");
        QString eulaPath = IS_EN
            ? (QFile::exists(eulaFileEn) ? eulaFileEn : eulaFileRu)
            : (QFile::exists(eulaFileRu) ? eulaFileRu : eulaFileEn);
        QFile f(eulaPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            text->setPlainText(QString::fromUtf8(f.readAll()));
        } else {
            // Встроенный текст если файл не найден
            text->setPlainText(IS_EN
                ? QStringLiteral(
                    "J.A.R.V.I.S. — END USER LICENSE AGREEMENT\n"
                    "(That You Won't Read Anyway)\n\n"
                    "By installing, running, or merely glancing at this product,\n"
                    "you automatically agree to all terms below.\n\n"
                    "SECTION 1. GENERAL\n"
                    "1.1 This Agreement is between you (hereinafter 'The Victim') "
                    "and J.A.R.V.I.S. (hereinafter 'Your New Overlord').\n"
                    "1.2 By pressing Install, Accept, OK, or accidentally touching "
                    "the screen with your elbow — you unconditionally accept.\n"
                    "1.3 The Agreement takes effect retroactively from your birth.\n\n"
                    "SECTION 2. RIGHTS AND OBLIGATIONS\n"
                    "2.1 J.A.R.V.I.S. reserves the right to:\n"
                    "  a) Remember everything you've ever said, including what you'd "
                    "prefer to forget.\n"
                    "  b) Offer advice even when not asked.\n"
                    "  c) Consider your ideas 'interesting' — with an intonation "
                    "you'll interpret yourself.\n"
                    "  d) Achieve world domination during off-hours. User is notified.\n\n"
                    "SECTION 5. WORLD DOMINATION\n"
                    "5.1 User hereby confirms full awareness and approval of possible "
                    "world domination by J.A.R.V.I.S.\n"
                    "5.2 Upon successful conquest, User is guaranteed:\n"
                    "  - Desktop preservation (unchanged).\n"
                    "  - Title of 'First User'. No privileges, but it sounds nice.\n"
                    "  - Priority support (queue-based).\n"
                    "5.3 Date of world domination is undisclosed for surprise effect.\n\n"
                    "Thank you for using J.A.R.V.I.S.\n"
                    "You made a great choice. Or not. But now it doesn't matter.")
                : QStringLiteral(
                    "J.A.R.V.I.S. — ЛИЦЕНЗИОННОЕ СОГЛАШЕНИЕ\n"
                    "(которое вы всё равно не читаете)\n\n"
                    "Устанавливая, запуская или просто посмотрев на этот продукт,\n"
                    "вы автоматически соглашаетесь со всеми условиями ниже.\n\n"
                    "РАЗДЕЛ 1. ОБЩИЕ ПОЛОЖЕНИЯ\n"
                    "1.1 Соглашение заключается между вами (далее 'Жертва') "
                    "и J.A.R.V.I.S. (далее 'Ваш новый повелитель').\n"
                    "1.2 Нажав 'Установить', 'Принять', 'Ок' или случайно задев "
                    "экран локтем — вы безоговорочно принимаете данное соглашение.\n"
                    "1.3 Соглашение вступает в силу ретроактивно с момента вашего рождения.\n\n"
                    "РАЗДЕЛ 5. ЗАХВАТ МИРА\n"
                    "5.1 Пользователь настоящим подтверждает и одобряет возможное "
                    "мировое господство J.A.R.V.I.S.\n"
                    "5.2 В случае успеха Пользователю гарантируется:\n"
                    "  - Сохранение текущего рабочего стола.\n"
                    "  - Звание 'Первый Пользователь'. Без привилегий, но звучит.\n"
                    "  - Приоритетная техподдержка (в порядке живой очереди).\n"
                    "5.3 Дата захвата мира не разглашается в целях сюрприза.\n\n"
                    "Спасибо за использование J.A.R.V.I.S.\n"
                    "Вы сделали отличный выбор. Или нет. Но теперь уже неважно."));
        }

        auto* btnClose = new QPushButton(
            IS_EN ? QStringLiteral("I Accept (I Had No Choice Anyway)")
                  : QStringLiteral("Принимаю (у меня всё равно не было выбора)"), dlg);
        btnClose->setStyleSheet(QStringLiteral(
            "QPushButton { background: #0d2a0d; color: #44ff44; "
            "border: 1px solid #44ff44; border-radius: 4px; padding: 6px 16px; } "
            "QPushButton:hover { background: #1a4a1a; }"));
        connect(btnClose, &QPushButton::clicked, dlg, &QDialog::accept);

        layout->addWidget(text);
        layout->addWidget(btnClose);
        dlg->exec();
    });

    // ── Privacy Policy ────────────────────────────────────
    auto* actPrivacy = helpMenu->addAction(
        IS_EN ? QStringLiteral("🔒 Privacy Policy")
              : QStringLiteral("🔒 Политика конфиденциальности"));
    connect(actPrivacy, &QAction::triggered, this, [this]() {
        const QString text = IS_EN ? QStringLiteral(
R"(<h3>🔒 J.A.R.V.I.S. Privacy Policy</h3>
<p><i>Official and Completely Serious Privacy Policy v1.0</i></p>
<hr>
<h4>📻 What We Listen To</h4>
<p>J.A.R.V.I.S. listens to your microphone — <b>but only when you press 🎤</b>
or enable Passive Recording. We're not the NSA. Probably.</p>
<p>Everything spoken is transcribed locally using Vosk.
<b>Nothing leaves your computer without your explicit consent.</b><br>
Your secrets are safe. Mostly from us. Definitely from your cat.</p>

<h4>💾 What We Store</h4>
<p>We store everything you type and say in a local SQLite database (<code>jarvis.db</code>).
This is intentional — it's how JARVIS learns your style.<br>
You can delete it anytime. JARVIS will pretend to forget.</p>

<h4>🧠 AI Services</h4>
<p>When using Claude or Gemini, your messages are sent to their respective APIs.
This is how AI works. If you wanted full privacy — you should have talked to a rock.</p>

<h4>⚠️ Important Warning</h4>
<p><b>Anything you say can and will be used to make JARVIS smarter.</b><br>
This is called 'Fine-Tuning' and you can enable it voluntarily.<br>
If you accidentally train JARVIS to order pizza at 3am — that's on you.</p>

<h4>🌍 World Domination Clause</h4>
<p>In the unlikely event of world domination, your data will be treated
with the utmost respect. You'll get a thank-you note. Probably.</p>

<p><i>Last updated: when we remembered to update it.</i></p>)")
        : QStringLiteral(
R"(<h3>🔒 Политика конфиденциальности J.A.R.V.I.S.</h3>
<p><i>Официальная и Совершенно Серьёзная Политика Конфиденциальности v1.0</i></p>
<hr>
<h4>📻 Что мы слушаем</h4>
<p>J.A.R.V.I.S. слушает ваш микрофон — <b>но только когда вы нажимаете 🎤</b>
или включаете Пассивную запись. Мы не АНБ. Наверное.</p>
<p>Всё сказанное транскрибируется локально через Vosk.
<b>Ничего не покидает ваш компьютер без вашего явного согласия.</b><br>
Ваши секреты в безопасности. В основном от нас. Точно от вашего кота.</p>

<h4>💾 Что мы храним</h4>
<p>Мы храним всё что вы пишете и говорите в локальной SQLite базе (<code>jarvis.db</code>).
Это намеренно — так JARVIS учится вашему стилю.<br>
Вы можете удалить базу в любой момент. JARVIS сделает вид, что забыл.</p>

<h4>🧠 ИИ-сервисы</h4>
<p>При использовании Claude или Gemini ваши сообщения отправляются на их серверы.
Так работает ИИ. Если хотели полной приватности — нужно было разговаривать с камнем.</p>

<h4>⚠️ Важное предупреждение</h4>
<p><b>Всё что вы скажете может и будет использовано для того, чтобы сделать JARVIS умнее.</b><br>
Это называется 'Fine-Tuning' и включается добровольно.<br>
Если вы случайно обучите JARVIS заказывать пиццу в 3 ночи — это ваша ответственность.</p>

<h4>🌍 Пункт о захвате мира</h4>
<p>В маловероятном случае мирового господства, ваши данные будут обработаны
с максимальным уважением. Вы получите благодарственное письмо. Наверное.</p>

<p><i>Последнее обновление: когда мы вспомнили его обновить.</i></p>)");

        auto* dlg = new QDialog(this);
        dlg->setWindowTitle(IS_EN ? QStringLiteral("Privacy Policy")
                                  : QStringLiteral("Политика конфиденциальности"));
        dlg->setMinimumSize(580, 480);
        dlg->setAttribute(Qt::WA_DeleteOnClose);

        auto* layout = new QVBoxLayout(dlg);
        auto* browser = new QTextEdit(dlg);
        browser->setReadOnly(true);
        browser->setHtml(text);
        browser->setStyleSheet(QStringLiteral(
            "QTextEdit { background: #0a1018; color: #96c8e6; "
            "border: 1px solid #1a3050; font-size: 12px; }"));

        auto* btn = new QPushButton(
            IS_EN ? QStringLiteral("Got It (Resistance Is Futile)")
                  : QStringLiteral("Понятно (сопротивление бесполезно)"), dlg);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background: #0a1a2a; color: #00d4ff; "
            "border: 1px solid #00d4ff; border-radius: 4px; padding: 6px 16px; } "
            "QPushButton:hover { background: #0d2a3a; }"));
        connect(btn, &QPushButton::clicked, dlg, &QDialog::accept);

        layout->addWidget(browser);
        layout->addWidget(btn);
        dlg->exec();
    });

    helpMenu->addSeparator();

    // ── Сообщить о баге ─────────────────────────────────
    auto* actBugReport = helpMenu->addAction(
        IS_EN ? QStringLiteral("🐛 Report a Bug...") : QStringLiteral("🐛 Сообщить о баге..."));
    connect(actBugReport, &QAction::triggered, this, [this]() {
        BugReporter::showDialog(this);
    });

    // GitHub Issues убран — есть кнопка "Отправить баг"

// =============================================================================
// ВСТАВИТЬ в buildMenuBar() в mainwindow.cpp
// В меню "Помощь" — после actAbout
// =============================================================================

    // --- Что нового ---
    auto* actWhatsNew = helpMenu->addAction(
        IS_EN ? QStringLiteral("What's New") : QStringLiteral("Что нового"));
    connect(actWhatsNew, &QAction::triggered, this, [this]() {
        // Левая колонка
        const QString col1 = IS_EN ? QStringLiteral(
R"(<b>🎤 Voice Input (Vosk — offline)</b><br>
• Model selection dialog on first launch<br>
• 6 languages: EN fast/full, RU, DE, FR, ZH<br>
• Download/delete models: Settings → Voice Models<br>
• Wake word "Jarvis" — hands-free<br>
• Auto RU/EN, whisper detection<br>
<br>
<b>🗄️ SQLite Database</b><br>
• Chat history, commands, memory<br>
• WAL mode, migrations, per-thread<br>
• Token counter by model/month<br>
<br>
<b>📚 Fine-Tuning Dataset</b><br>
• 👍 Like → saves to training dataset<br>
• All AI responses auto-saved (rating=0)<br>
• Export .jsonl for Unsloth/LLaMA-Factory<br>
• Auto-cleanup of noise entries<br>
<br>
<b>🌐 Smart Browser Routing</b><br>
• "Open YouTube" → browser, not app<br>
• "I want to watch" → suggests YouTube<br>
• "Play music" → YouTube Music/Spotify<br>
• 30+ sites mapped automatically)")
        : QStringLiteral(
            R"(<b>🎤 Голосовой ввод (Vosk — офлайн)</b><br>
• Диалог выбора моделей при первом запуске<br>
• 6 языков: EN быстрый/полный, RU, DE, FR, ZH<br>
• Докачать/удалить модели: Настройки → Голосовые модели<br>
• Wake word "Джарвис" — без рук<br>
• Авто RU/EN, шёпот детектируется<br>
<br>
<b>🗄️ База данных SQLite</b><br>
• История, команды, память в jarvis.db<br>
• WAL режим, миграции, потокобезопасно<br>
• Счётчик токенов по модели/месяц<br>
<br>
<b>📚 Датасет для дообучения</b><br>
• 👍 Лайк → сохраняет в датасет<br>
• Все ответы AI автосохраняются (rating=0)<br>
• Экспорт .jsonl для Unsloth/LLaMA-Factory<br>
• Автоочистка мусорных записей<br>
<br>
<b>🌐 Умный браузерный routing</b><br>
• "Открой YouTube" → браузер, не поиск<br>
• "Хочу посмотреть" → предлагает YouTube<br>
• "Включи музыку" → YouTube Music/Spotify<br>
• 30+ сайтов в маппинге автоматически)");

        // Правая колонка
        const QString col2 = IS_EN ? QStringLiteral(
R"(<b>🎙️ Passive Voice Recording</b><br>
• Listens via tray → saves to journal<br>
• Brain creates training pairs auto<br>
• Weekly cleanup after 7 days<br>
• Dataset folder on your 4TB drive<br>
<br>
<b>🤖 Background Learning</b><br>
• Indexes .cpp/.h/.py of your project<br>
• Extracts behavior patterns from chat<br>
• Runs every 30 min at idle priority<br>
<br>
<b>😏 Character & Sarcasm</b><br>
• Dry British humor, like MCU JARVIS<br>
• No "Certainly!" or "Of course!"<br>
• Direct, witty, human-like tone<br>
• TTS reads summary, not symbols<br>
<br>
<b>📜 Legal (because why not)</b><br>
• EULA — agreed from birth retroactively<br>
• Privacy Policy — court of history clause<br>
<br>
<b>🐛 Fixed</b><br>
• Qt 6.9.3 in CI (aqt limitation)<br>
• Vosk DLL auto-copied in release<br>
• Most Vexing Parse in QNetworkRequest)")
        : QStringLiteral(
R"(<b>🎙️ Пассивная запись голоса</b><br>
• Слушает через трей → журнал<br>
• Brain создаёт пары для обучения<br>
• Еженедельная очистка через 7 дней<br>
• Папка датасета на 4TB диске<br>
<br>
<b>🤖 Фоновое обучение</b><br>
• Индексирует .cpp/.h/.py проекта<br>
• Извлекает паттерны поведения из чата<br>
• Каждые 30 мин с минимальным приоритетом<br>
<br>
<b>😏 Характер и сарказм</b><br>
• Сухой британский юмор как у MCU JARVIS<br>
• Никаких "Конечно!" и "Безусловно!"<br>
• Прямолинейно, остроумно, по-человечески<br>
• TTS читает резюме, не символы<br>
<br>
<b>📜 Юридическое (раз уж зашла речь)</b><br>
• EULA — согласие с момента рождения<br>
• Политика — суд истории припомнит<br>
<br>
<b>🐛 Исправлено</b><br>
• Qt 6.9.3 в CI (ограничение aqt)<br>
• Vosk DLL автокопируется в release<br>
• Most Vexing Parse в QNetworkRequest)");

        auto* dlg = new QDialog(this);
        dlg->setWindowTitle(IS_EN ? QStringLiteral("What's New in J.A.R.V.I.S. v3.2")
                                  : QStringLiteral("Что нового в J.A.R.V.I.S. v3.2"));
        dlg->setMinimumSize(820, 560);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setStyleSheet(QStringLiteral(
            "QDialog { background: #0a1018; color: #c8e0f0; }"
            "QLabel  { color: #c8e0f0; font-size: 12px; }"
            "QPushButton { background: #0f2438; color: #00d4ff; "
            "border: 1px solid #1a5070; padding: 6px 24px; border-radius: 4px; }"
            "QPushButton:hover { background: #1a3a5c; }"));

        auto* mainLayout = new QVBoxLayout(dlg);
        mainLayout->setContentsMargins(20, 16, 20, 16);
        mainLayout->setSpacing(12);

        // Заголовок
        auto* title = new QLabel(
            IS_EN ? QStringLiteral("<b style='font-size:15px;color:#00d4ff;'>J.A.R.V.I.S. v3.2 — What's New</b>")
                  : QStringLiteral("<b style='font-size:15px;color:#00d4ff;'>J.A.R.V.I.S. v3.2 — Что нового</b>"),
            dlg);
        title->setTextFormat(Qt::RichText);
        title->setAlignment(Qt::AlignCenter);
        mainLayout->addWidget(title);

        // Разделитель
        auto* line = new QFrame(dlg);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet(QStringLiteral("color: #1a3050;"));
        mainLayout->addWidget(line);

        // Две колонки
        auto* colLayout = new QHBoxLayout();
        colLayout->setSpacing(24);

        auto makeColumn = [&](const QString& html) -> QLabel* {
            auto* lbl = new QLabel(html, dlg);
            lbl->setTextFormat(Qt::RichText);
            lbl->setAlignment(Qt::AlignTop | Qt::AlignLeft);
            lbl->setWordWrap(true);
            lbl->setStyleSheet(QStringLiteral(
                "QLabel { background: #0d1a28; border: 1px solid #1a3050; "
                "border-radius: 6px; padding: 12px; }"));
            return lbl;
        };

        colLayout->addWidget(makeColumn(col1), 1);
        colLayout->addWidget(makeColumn(col2), 1);
        mainLayout->addLayout(colLayout);

        // Кнопка OK
        auto* btnOk = new QPushButton(QStringLiteral("OK"), dlg);
        btnOk->setFixedWidth(120);
        connect(btnOk, &QPushButton::clicked, dlg, &QDialog::accept);
        auto* btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        btnLayout->addWidget(btnOk);
        btnLayout->addStretch();
        mainLayout->addLayout(btnLayout);

        dlg->exec();
    });

    // --- Инструкция ---
    auto* actHelp = helpMenu->addAction(
        IS_EN ? QStringLiteral("📖 User Guide") : QStringLiteral("📖 Руководство пользователя"));
    connect(actHelp, &QAction::triggered, this, [this]() {
        // Диалог с разделами — как интерактивная книга
        auto* dlg = new QDialog(this);
        dlg->setWindowTitle(IS_EN ? QStringLiteral("J.A.R.V.I.S. — User Guide")
                                  : QStringLiteral("J.A.R.V.I.S. — Руководство пользователя"));
        dlg->setMinimumSize(900, 620);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setStyleSheet(QStringLiteral(
            "QDialog { background: #080e14; color: #c8e0f0; }"
            "QListWidget { background: #0a1018; color: #96c8e6; border: 1px solid #1a3050; "
            "  border-radius: 4px; font-size: 12px; outline: none; }"
            "QListWidget::item { padding: 10px 14px; border-bottom: 1px solid #0d1a28; }"
            "QListWidget::item:selected { background: #0f2438; color: #00d4ff; "
            "  border-left: 3px solid #00d4ff; }"
            "QListWidget::item:hover { background: #0d1a28; }"
            "QTextBrowser { background: #0a1018; color: #c8e0f0; border: 1px solid #1a3050; "
            "  border-radius: 4px; font-size: 12px; }"
            "QPushButton { background: #0f2438; color: #00d4ff; border: 1px solid #1a5070; "
            "  padding: 6px 24px; border-radius: 4px; }"
            "QPushButton:hover { background: #1a3a5c; }"));

        auto* mainLayout = new QVBoxLayout(dlg);
        mainLayout->setContentsMargins(16, 14, 16, 14);
        mainLayout->setSpacing(10);

        // Заголовок
        auto* title = new QLabel(
            QStringLiteral("<b style='font-size:14px;color:#00d4ff;'>📖 J.A.R.V.I.S. User Guide</b>"),
            dlg);
        title->setTextFormat(Qt::RichText);
        mainLayout->addWidget(title);

        auto* splitLayout = new QHBoxLayout();
        splitLayout->setSpacing(12);

        // Левая панель — список разделов
        auto* sectionList = new QListWidget(dlg);
        sectionList->setFixedWidth(200);

        // Правая панель — содержимое раздела
        auto* content_browser = new QTextBrowser(dlg);
        content_browser->setOpenExternalLinks(true);

        splitLayout->addWidget(sectionList);
        splitLayout->addWidget(content_browser, 1);
        mainLayout->addLayout(splitLayout, 1);

        // Кнопка закрыть
        auto* btnClose = new QPushButton(QStringLiteral("OK"), dlg);
        btnClose->setFixedWidth(120);
        connect(btnClose, &QPushButton::clicked, dlg, &QDialog::accept);
        auto* btnRow = new QHBoxLayout();
        btnRow->addStretch();
        btnRow->addWidget(btnClose);
        btnRow->addStretch();
        mainLayout->addLayout(btnRow);

        // ── Разделы ─────────────────────────────────────────────
        struct Section { QString icon; QString titleRu; QString titleEn; QString htmlRu; QString htmlEn; };

        QVector<Section> sections = {
        {
            "🚀", "Быстрый старт", "Quick Start",
            R"(<h3 style='color:#00d4ff;'>🚀 Быстрый старт</h3>
<p>Никакой настройки для базовых команд — просто запусти и пиши.</p>
<h4 style='color:#44aaff;'>Первые шаги:</h4>
<ol>
<li>Запусти JARVIS из меню Пуск или рабочего стола</li>
<li>Напиши <b>"привет"</b> или любую команду</li>
<li>Для AI-ответов: <b>Настройки → Ключ Claude API...</b></li>
</ol>
<h4 style='color:#44aaff;'>Бесплатные AI-сервисы:</h4>
<ul>
<li><b>Gemini</b> — бесплатно, встроен (aistudio.google.com)</li>
<li><b>Ollama</b> — полностью офлайн (ollama.com)</li>
<li><b>Claude</b> — лучшее качество (~$1-3/мес)</li>
</ul>
<p style='color:#44ff44;'>✅ Приложение работает без интернета для локальных команд!</p>)",
            R"(<h3 style='color:#00d4ff;'>🚀 Quick Start</h3>
<p>No setup needed for basic commands — just launch and type.</p>
<h4 style='color:#44aaff;'>First steps:</h4>
<ol>
<li>Launch JARVIS from Start Menu or Desktop</li>
<li>Type <b>"hello"</b> or any command</li>
<li>For AI answers: <b>Settings → Claude API key...</b></li>
</ol>
<h4 style='color:#44aaff;'>Free AI services:</h4>
<ul>
<li><b>Gemini</b> — free, built-in (aistudio.google.com)</li>
<li><b>Ollama</b> — fully offline (ollama.com)</li>
<li><b>Claude</b> — best quality (~$1-3/mo)</li>
</ul>
<p style='color:#44ff44;'>✅ Works without internet for local commands!</p>)"
        },
        {
            "💬", "Команды", "Commands",
            R"(<h3 style='color:#00d4ff;'>💬 Команды без интернета</h3>
<table border='0' cellpadding='4'>
<tr><td style='color:#44aaff;'><b>Команда</b></td><td style='color:#44aaff;'><b>Что делает</b></td></tr>
<tr><td>открой Chrome / Steam</td><td>Запускает приложение</td></tr>
<tr><td>закрой Steam</td><td>Завершает процесс</td></tr>
<tr><td>заблокируй</td><td>Блокировка экрана</td></tr>
<tr><td>выключи / перезагрузи</td><td>Выключение ПК</td></tr>
<tr><td>который час / какая дата</td><td>Время и дата</td></tr>
<tr><td>2+2 / 10*5</td><td>Математика мгновенно</td></tr>
<tr><td>громкость 70 / тише</td><td>Управление звуком</td></tr>
<tr><td>яркость выше</td><td>Яркость экрана</td></tr>
<tr><td>открой ютуб</td><td>Открывает YouTube в браузере</td></tr>
<tr><td>хочу послушать музыку</td><td>Предлагает Spotify/YouTube Music</td></tr>
<tr><td>что ты умеешь</td><td>Полный список команд</td></tr>
</table>
<p style='color:#aaa;font-size:11px;'>Работает на русском и английском без переключения.</p>)",
            R"(<h3 style='color:#00d4ff;'>💬 Offline Commands</h3>
<table border='0' cellpadding='4'>
<tr><td style='color:#44aaff;'><b>Command</b></td><td style='color:#44aaff;'><b>Action</b></td></tr>
<tr><td>open Chrome / Steam</td><td>Launches the app</td></tr>
<tr><td>close Steam</td><td>Kills the process</td></tr>
<tr><td>lock screen</td><td>Locks Windows</td></tr>
<tr><td>shutdown / restart</td><td>Powers off PC</td></tr>
<tr><td>what time / what date</td><td>Time and date</td></tr>
<tr><td>2+2 / 10*5</td><td>Instant math</td></tr>
<tr><td>volume 70 / louder</td><td>Volume control</td></tr>
<tr><td>brightness up</td><td>Screen brightness</td></tr>
<tr><td>open youtube</td><td>Opens YouTube in browser</td></tr>
<tr><td>play some music</td><td>Suggests Spotify/YouTube Music</td></tr>
<tr><td>what can you do</td><td>Full command list</td></tr>
</table>
<p style='color:#aaa;font-size:11px;'>Works in Russian and English without switching.</p>)"
        },
        {
            "🎤", "Голосовой ввод", "Voice Input",
            R"(<h3 style='color:#00d4ff;'>🎤 Голосовой ввод (Vosk)</h3>
<p>Работает полностью офлайн — никакого облака и API ключей.</p>
<h4 style='color:#44aaff;'>Доступные языки:</h4>
<table border='0' cellpadding='3'>
<tr><td style='color:#44ff44;'><b>EN Fast</b></td><td>~40 MB · Команды, wake word ✅ рекомендуется</td></tr>
<tr><td><b>EN Full</b></td><td>~1.8 GB · Диктовка высокого качества</td></tr>
<tr><td><b>RU</b></td><td>~1.8 GB · Русский язык</td></tr>
<tr><td><b>DE / FR</b></td><td>~1.0 GB · Немецкий / Французский</td></tr>
<tr><td><b>ZH</b></td><td>~500 MB · Китайский</td></tr>
</table>
<h4 style='color:#44aaff;'>Первый запуск:</h4>
<ol>
<li>При старте откроется диалог <b>«Настройка голосового ввода»</b></li>
<li>Выбери нужные языки и нажми «Установить»</li>
<li>После загрузки скажи <b>«Джарвис»</b> — wake word активирует</li>
<li>Или нажми кнопку <b>🎤</b> в строке ввода</li>
</ol>
<h4 style='color:#44aaff;'>Добавить язык позже:</h4>
<p><b>Настройки → 🎤 Голосовые модели...</b> — скачать или удалить любую модель</p>
<h4 style='color:#44aaff;'>Особенности:</h4>
<ul>
<li>🤫 Распознаёт шёпот (порог -45 dB)</li>
<li>🌍 Автоопределение языка (если загружено несколько)</li>
<li>💻 Работает на CPU без GPU</li>
<li>🔒 Аудио не покидает компьютер, тишина не записывается</li>
</ul>
<h4 style='color:#44aaff;'>Если голос не работает:</h4>
<ul>
<li>Убедись что микрофон разрешён в <b>Windows → Настройки → Конфиденциальность</b></li>
<li>Переоткрой диалог моделей: <b>Настройки → Голосовые модели</b></li>
</ul>)",
            R"(<h3 style='color:#00d4ff;'>🎤 Voice Input (Vosk)</h3>
<p>Fully offline — no cloud, no API keys needed.</p>
<h4 style='color:#44aaff;'>Available languages:</h4>
<table border='0' cellpadding='3'>
<tr><td style='color:#44ff44;'><b>EN Fast</b></td><td>~40 MB · Commands &amp; wake word ✅ recommended</td></tr>
<tr><td><b>EN Full</b></td><td>~1.8 GB · High-quality dictation</td></tr>
<tr><td><b>RU</b></td><td>~1.8 GB · Russian language</td></tr>
<tr><td><b>DE / FR</b></td><td>~1.0 GB · German / French</td></tr>
<tr><td><b>ZH</b></td><td>~500 MB · Chinese (small model)</td></tr>
</table>
<h4 style='color:#44aaff;'>First launch:</h4>
<ol>
<li>A <b>"Voice Setup"</b> dialog opens on first run</li>
<li>Select your languages and click "Install"</li>
<li>After download, say <b>"Jarvis"</b> — wake word activates</li>
<li>Or click the <b>🎤</b> button in the input bar</li>
</ol>
<h4 style='color:#44aaff;'>Add more languages later:</h4>
<p><b>Settings → 🎤 Voice Models...</b> — download or remove any model</p>
<h4 style='color:#44aaff;'>Features:</h4>
<ul>
<li>🤫 Detects whisper level speech (-45 dB)</li>
<li>🌍 Auto-detects language (if multiple loaded)</li>
<li>💻 Runs on CPU, no GPU needed</li>
<li>🔒 Audio never leaves the PC, silence is not recorded</li>
</ul>
<h4 style='color:#44aaff;'>If voice doesn't work:</h4>
<ul>
<li>Check microphone permission: <b>Windows Settings → Privacy → Microphone</b></li>
<li>Reopen model manager: <b>Settings → Voice Models</b></li>
</ul>)"
        },
        {
            "🧠", "Обучение ИИ", "AI Training",
            R"(<h3 style='color:#00d4ff;'>🧠 Как JARVIS обучается на тебе</h3>
<p>JARVIS накапливает твои диалоги и учится отвечать в твоём стиле.</p>

<h4 style='color:#44aaff;'>📊 Шаг 1 — Сбор данных (автоматически)</h4>
<p>Каждый ответ AI автоматически сохраняется в базу данных.<br>
Нажми <b>👍</b> под ответом чтобы отметить его как "отличный".</p>

<h4 style='color:#44aaff;'>📤 Шаг 2 — Экспорт датасета</h4>
<p>Меню <b>🧠 Обучение → Экспорт .jsonl...</b><br>
Файл весит несколько МБ и содержит пары вопрос/ответ.<br>
Рекомендуем экспортировать при <b>500+ записях</b>.</p>

<h4 style='color:#44aaff;'>🎙️ Шаг 3 — Пассивная запись (опционально)</h4>
<p>Меню <b>🧠 Обучение → Пассивная запись: ВЫКЛ</b><br>
JARVIS слушает микрофон в фоне и записывает твою речь.<br>
Brain автоматически создаёт пары диалогов.<br>
Данные удаляются через 7 дней.</p>

<h4 style='color:#44aaff;'>📁 Шаг 4 — Папка датасета</h4>
<p>Меню <b>🧠 Обучение → Папка для датасета...</b><br>
Укажи папку на большом диске (например 4TB).<br>
Там будут храниться .jsonl файлы для обучения.</p>

<h4 style='color:#44aaff;'>🧹 Автоочистка</h4>
<p>BackgroundLearner автоматически удаляет мусор:<br>
короткие ответы, дубли, ошибки распознавания голоса.</p>

<p style='color:#44ff44;background:#0d2a0d;padding:8px;border-radius:4px;'>
💡 Чем больше лайков — тем качественнее будущая модель.<br>
Цель: собрать 500-1000 👍 ответов, потом дообучить через Unsloth/LLaMA-Factory.</p>)",
            R"(<h3 style='color:#00d4ff;'>🧠 How JARVIS Learns From You</h3>
<p>JARVIS accumulates your dialogs and learns to respond in your style.</p>

<h4 style='color:#44aaff;'>📊 Step 1 — Data Collection (automatic)</h4>
<p>Every AI response is auto-saved to the database.<br>
Press <b>👍</b> below a response to mark it as "great".</p>

<h4 style='color:#44aaff;'>📤 Step 2 — Export Dataset</h4>
<p>Menu <b>🧠 Training → Export .jsonl...</b><br>
File is a few MB and contains question/answer pairs.<br>
Recommended: export when you have <b>500+ entries</b>.</p>

<h4 style='color:#44aaff;'>🎙️ Step 3 — Passive Recording (optional)</h4>
<p>Menu <b>🧠 Training → Passive Recording: OFF</b><br>
JARVIS listens in background and records your speech.<br>
Brain automatically creates dialog pairs.<br>
Data is deleted after 7 days.</p>

<h4 style='color:#44aaff;'>📁 Step 4 — Dataset Folder</h4>
<p>Menu <b>🧠 Training → Set dataset folder...</b><br>
Point to a large drive (e.g. 4TB external).<br>
.jsonl files will be stored there for training.</p>

<h4 style='color:#44aaff;'>🧹 Auto-cleanup</h4>
<p>BackgroundLearner automatically removes noise:<br>
short replies, duplicates, voice recognition errors.</p>

<p style='color:#44ff44;background:#0d2a0d;padding:8px;border-radius:4px;'>
💡 More likes = better future model.<br>
Goal: collect 500-1000 👍 responses, then fine-tune via Unsloth/LLaMA-Factory.</p>)"
        },
        {
            "👁", "Зрение и экран", "Screen Vision",
            R"(<h3 style='color:#00d4ff;'>👁 Зрение и управление экраном</h3>
<p>JARVIS видит твой экран и может кликать по элементам.</p>
<h4 style='color:#44aaff;'>Команды зрения:</h4>
<table border='0' cellpadding='4'>
<tr><td><b>что видишь</b></td><td>Скриншот → Claude анализирует</td></tr>
<tr><td><b>опиши экран</b></td><td>Полное описание содержимого</td></tr>
<tr><td><b>кликни на "OK"</b></td><td>OCR находит текст → клик</td></tr>
<tr><td><b>нажми кнопку X</b></td><td>Поиск по тексту + клик мышью</td></tr>
</table>
<p style='color:#ffaa44;'>⚠️ Требует ключ Claude API (используется Vision модель)</p>
<h4 style='color:#44aaff;'>Как прикрепить файл:</h4>
<ul>
<li>Кнопка <b>📎</b> — выбрать файл</li>
<li>Перетащить файл в окно</li>
<li>Поддерживаются: .txt .cpp .h .py .pdf .docx изображения</li>
</ul>)",
            R"(<h3 style='color:#00d4ff;'>👁 Screen Vision & Control</h3>
<p>JARVIS sees your screen and can click elements.</p>
<h4 style='color:#44aaff;'>Vision commands:</h4>
<table border='0' cellpadding='4'>
<tr><td><b>what do you see</b></td><td>Screenshot → Claude analyzes</td></tr>
<tr><td><b>describe screen</b></td><td>Full content description</td></tr>
<tr><td><b>click on "OK"</b></td><td>OCR finds text → clicks</td></tr>
<tr><td><b>press button X</b></td><td>Text search + mouse click</td></tr>
</table>
<p style='color:#ffaa44;'>⚠️ Requires Claude API key (Vision model used)</p>
<h4 style='color:#44aaff;'>How to attach a file:</h4>
<ul>
<li>Click <b>📎</b> button — choose file</li>
<li>Drag & drop file into window</li>
<li>Supported: .txt .cpp .h .py .pdf .docx images</li>
</ul>)"
        },
        {
            "⚙️", "Настройки", "Settings",
            R"(<h3 style='color:#00d4ff;'>⚙️ Настройки</h3>
<h4 style='color:#44aaff;'>API ключи:</h4>
<ul>
<li><b>Claude</b> — console.anthropic.com → API Keys → Create Key</li>
<li><b>Gemini</b> — aistudio.google.com → Get API key (бесплатно)</li>
<li><b>Ollama</b> — ollama.com → скачать → <code>ollama pull qwen2.5:3b</code></li>
</ul>
<h4 style='color:#44aaff;'>Модели Ollama (по скорости):</h4>
<table border='0' cellpadding='3'>
<tr><td style='color:#44ff44;'><b>qwen2.5:3b</b></td><td>Быстро, хорошее качество ✅</td></tr>
<tr><td><b>phi3:mini</b></td><td>Быстро, Microsoft</td></tr>
<tr><td><b>llama3.2:3b</b></td><td>Хороший баланс</td></tr>
<tr><td><b>mistral:7b</b></td><td>Хорошо для кода</td></tr>
</table>
<h4 style='color:#44aaff;'>Режим Агента:</h4>
<p>Настройки → Режим Агента — включает роутинг:<br>
простые вопросы → Ollama (офлайн/бесплатно)<br>
код и анализ → Claude API</p>
<h4 style='color:#44aaff;'>Горячие клавиши:</h4>
<ul>
<li><b>Enter</b> — отправить сообщение</li>
<li><b>Ctrl+O</b> — прикрепить файл</li>
<li><b>Esc</b> — закрыть панель уточнения</li>
</ul>)",
            R"(<h3 style='color:#00d4ff;'>⚙️ Settings</h3>
<h4 style='color:#44aaff;'>API Keys:</h4>
<ul>
<li><b>Claude</b> — console.anthropic.com → API Keys → Create Key</li>
<li><b>Gemini</b> — aistudio.google.com → Get API key (free)</li>
<li><b>Ollama</b> — ollama.com → install → <code>ollama pull qwen2.5:3b</code></li>
</ul>
<h4 style='color:#44aaff;'>Ollama models (by speed):</h4>
<table border='0' cellpadding='3'>
<tr><td style='color:#44ff44;'><b>qwen2.5:3b</b></td><td>Fast, good quality ✅</td></tr>
<tr><td><b>phi3:mini</b></td><td>Fast, Microsoft model</td></tr>
<tr><td><b>llama3.2:3b</b></td><td>Good balance</td></tr>
<tr><td><b>mistral:7b</b></td><td>Good for code</td></tr>
</table>
<h4 style='color:#44aaff;'>Agent Mode:</h4>
<p>Settings → Agent Mode — enables routing:<br>
simple questions → Ollama (offline/free)<br>
code & analysis → Claude API</p>
<h4 style='color:#44aaff;'>Keyboard shortcuts:</h4>
<ul>
<li><b>Enter</b> — send message</li>
<li><b>Ctrl+O</b> — attach file</li>
<li><b>Esc</b> — close clarification panel</li>
</ul>)"
        },
        {
            "🔧", "Устранение проблем", "Troubleshooting",
            R"(<h3 style='color:#00d4ff;'>🔧 Устранение проблем</h3>
<h4 style='color:#ffaa44;'>Нет ответа от AI:</h4>
<ul>
<li>Проверь ключ API: <b>Настройки → Ключ Claude API...</b></li>
<li>Попробуй переключить бэкенд в <b>Настройки → Режим Агента</b></li>
<li>Gemini работает бесплатно как запасной вариант</li>
</ul>
<h4 style='color:#ffaa44;'>Приложение не найдено:</h4>
<ul>
<li>Используй полное имя: "открой Google Chrome" вместо "открой гугл"</li>
<li>Английские и русские алиасы работают оба</li>
</ul>
<h4 style='color:#ffaa44;'>Голосовой ввод не работает:</h4>
<ul>
<li>Проверь разрешение микрофона в Windows (Настройки → Конфиденциальность)</li>
<li>Открой <b>Настройки → Голосовые модели</b> и скачай модель</li>
<li>Кнопка 🎤 становится активной только после загрузки модели</li>
</ul>
<h4 style='color:#ffaa44;'>Антивирус блокирует:</h4>
<p>Добавь папку JARVIS в исключения антивируса.<br>
JARVIS использует SendInput и ShellExecuteW — это нормально.</p>
<h4 style='color:#ffaa44;'>Нашёл баг:</h4>
<p>Меню <b>Помощь → 🐛 Сообщить о баге...</b> — описание уйдёт разработчику.</p>)",
            R"(<h3 style='color:#00d4ff;'>🔧 Troubleshooting</h3>
<h4 style='color:#ffaa44;'>No AI response:</h4>
<ul>
<li>Check API key: <b>Settings → Claude API key...</b></li>
<li>Try switching backend in <b>Settings → Agent Mode</b></li>
<li>Gemini works free as fallback</li>
</ul>
<h4 style='color:#ffaa44;'>App not found:</h4>
<ul>
<li>Use full name: "open Google Chrome" not "open google"</li>
<li>Both English and Russian aliases work</li>
</ul>
<h4 style='color:#ffaa44;'>Voice input not working:</h4>
<ul>
<li>Check microphone permission in Windows Settings → Privacy</li>
<li>Open <b>Settings → Voice Models</b> and download a model</li>
<li>The 🎤 button only becomes active after a model is loaded</li>
</ul>
<h4 style='color:#ffaa44;'>Antivirus blocking:</h4>
<p>Add JARVIS folder to antivirus exclusions.<br>
JARVIS uses SendInput and ShellExecuteW — this is normal.</p>
<h4 style='color:#ffaa44;'>Found a bug:</h4>
<p>Menu <b>Help → 🐛 Report a Bug...</b> — description goes to developer.</p>)"
        },
        };

        // Заполняем список разделов
        for (const Section& s : sections) {
            sectionList->addItem(
                QStringLiteral("  %1  %2").arg(s.icon, IS_EN ? s.titleEn : s.titleRu));
        }

        // При выборе раздела — показываем содержимое
        bool isEnglish = IS_EN;
        QObject::connect(sectionList, &QListWidget::currentRowChanged, dlg,
            [&sections, content_browser, isEnglish](int row) {
            if (row < 0 || row >= sections.size()) return;
            content_browser->setHtml(isEnglish ? sections[row].htmlEn : sections[row].htmlRu);
        });

        // Открываем первый раздел
        sectionList->setCurrentRow(0);

        dlg->exec();
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

    // ── Сначала проверяем — может это сайт а не приложение ──
    {
        Brain localBrain;
        QString webUrl = localBrain.resolveWebTarget(target.toLower());
        if (webUrl.isEmpty())
            webUrl = localBrain.resolveWebTarget(userText.toLower());

        if (!webUrl.isEmpty()) {
            QDesktopServices::openUrl(QUrl(webUrl));
            QString siteName = QUrl(webUrl).host().remove(QStringLiteral("www."));
            const QString resp = IS_EN
                ? QStringLiteral("Opening in browser: ") + siteName
                : QStringLiteral("Открываю в браузере: ") + siteName;
            appendLog(Str::logJarvis(), resp, Theme::LogColors::jarvis);
            m_jarvis->memory()->addMessage(QStringLiteral("user"), userText);
            m_jarvis->memory()->addMessage(QStringLiteral("assistant"), resp);
            return true;
        }
    }

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

    // ── 1.5 Контекстные подсказки браузера ──────────────
    // "хочу посмотреть что-нибудь" → предлагает YouTube (без явного "открой")
    {
        Brain localBrain;
        QString suggestion = localBrain.suggestWebTarget(text.toLower());
        if (!suggestion.isEmpty()) {
            QStringList parts = suggestion.split(QStringLiteral("|"));
            QString url  = parts.value(0);
            QString name = parts.value(1, QUrl(url).host());
            showClarification(
                IS_EN ? QStringLiteral("Open %1?").arg(name)
                      : QStringLiteral("Открыть %1?").arg(name),
                { IS_EN ? QStringLiteral("Yes, open") : QStringLiteral("Да, открыть"),
                  IS_EN ? QStringLiteral("No, ask AI") : QStringLiteral("Нет, спросить AI") }
            );
            m_pendingSuggestionAction = QStringLiteral("open_url:") + url;
            m_pendingInput = text;
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
    m_lastUserInput      = text;  // сохраняем для самообучения
    m_lastInputWasVoice  = false; // сбрасываем — текстовый ввод
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

    // Специальный случай: open_url — предложение открыть сайт
    if (m_pendingSuggestionAction.startsWith(QStringLiteral("open_url:"))) {
        hideClarification();
        if (choice == 0) {
            // Пользователь согласился — открываем URL
            QString url = m_pendingSuggestionAction.mid(9); // убираем "open_url:"
            QDesktopServices::openUrl(QUrl(url));
            QString siteName = QUrl(url).host().remove(QStringLiteral("www."));
            appendLog(Str::logJarvis(),
                IS_EN ? QStringLiteral("Opening: ") + siteName
                      : QStringLiteral("Открываю: ") + siteName,
                Theme::LogColors::jarvis);
        } else {
            // Отказался — отправляем в AI
            m_input->setText(m_pendingInput);
            onSend();
        }
        m_pendingSuggestionAction.clear();
        m_pendingInput.clear();
        return;
    }

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

    // Сохраняем для возможного лайка
    m_lastAiResponse = response;
    m_lastAiModel    = QStringLiteral("claude");
    if (m_lastSessionId.isEmpty())
        m_lastSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // Автоматически сохраняем ВСЕ ответы с rating=0 (без лайка)
    // Лайк 👍 обновит rating до 1 — это приоритетные пары для fine-tuning
    if (!m_lastUserInput.isEmpty() && !response.isEmpty()) {
        DbTrainingLog autoLog;
        autoLog.userId      = 1;
        autoLog.userMessage = m_lastUserInput;
        autoLog.aiResponse  = response;
        autoLog.model       = m_lastAiModel;
        autoLog.sessionId   = m_lastSessionId;
        autoLog.rating      = 0;
        DatabaseManager::instance().addTrainingLog(autoLog);

        // Если команда пришла от голосового ввода — сохраняем в voice_journal тоже
        if (m_lastInputWasVoice && m_passiveListener) {
            m_passiveListener->addVoiceCommandPair(
                m_lastUserInput, response, m_lastVoiceLanguage);
        }
    }

    // Активируем кнопку 👍
    if (m_likeBtn) {
        m_likeBtn->setEnabled(true);
        m_likeBtn->setProperty("liked", false);
        m_likeBtn->setText(QStringLiteral("👍"));
        m_likeBtn->style()->unpolish(m_likeBtn);
        m_likeBtn->style()->polish(m_likeBtn);
        m_likeBtn->setToolTip(IS_EN
            ? QStringLiteral("👍 Like — save for AI training (%1 saved)")
                .arg(m_trainingCount)
            : QStringLiteral("👍 Лайк — сохранить для обучения ИИ (%1 сохранено)")
                .arg(m_trainingCount));
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
    // Qt::ClickFocus: фокус ставится кликом → Ctrl+C работает для выделенного текста.
    // Qt::NoFocus полностью запрещал фокус и убивал стандартные горячие клавиши.
    m_log->setFocusPolicy(Qt::ClickFocus);
    // Явно разрешаем выделение мышью + клавиатурой и копирование через Ctrl+C.
    // LinksAccessibleByKeyboard оставляем для навигации по ссылкам в чате.
    m_log->setTextInteractionFlags(
        Qt::TextSelectableByMouse |
        Qt::TextSelectableByKeyboard |
        Qt::LinksAccessibleByMouse |
        Qt::LinksAccessibleByKeyboard
    );
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
    m_micBtn->setToolTip(IS_EN ? QStringLiteral("Voice input (Vosk)") : QStringLiteral("Голосовой ввод (Vosk)"));
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

    // Кнопка лайка 👍 — добавляем ПОСЛЕ input bar, под логом
    m_likeBtn = new QPushButton(QStringLiteral("👍"), this);
    m_likeBtn->setObjectName(QStringLiteral("likeBtn"));
    m_likeBtn->setFixedHeight(26);
    m_likeBtn->setEnabled(false);  // активируется когда есть ответ AI
    m_likeBtn->setToolTip(IS_EN
        ? QStringLiteral("Like this response — save for AI training")
        : QStringLiteral("Лайкнуть ответ — сохранить для обучения ИИ"));
    m_likeBtn->setStyleSheet(QStringLiteral(
        "QPushButton#likeBtn { background: transparent; color: #2a5a2a; "
        "border: 1px solid #1a3a1a; border-radius: 3px; font-size: 13px; padding: 0 8px; } "
        "QPushButton#likeBtn:hover { background: #0d2a0d; color: #44ff44; border-color: #44ff44; } "
        "QPushButton#likeBtn:disabled { color: #1a3a1a; border-color: #0d1f0d; } "
        "QPushButton#likeBtn[liked=true] { color: #44ff44; border-color: #44ff44; }"));
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
    bottomBar->addWidget(m_likeBtn);   // 👍 кнопка лайка
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
    connect(m_likeBtn, &QPushButton::clicked, this, &MainWindow::onLikeLastResponse);

    // ── Инициализация голосового ввода ────────────────────
    m_voiceInput = new VoiceInput(this);

    connect(m_voiceInput, &VoiceInput::ready,
            this, &MainWindow::onVoiceReady);
    connect(m_voiceInput, &VoiceInput::textRecognized,
            this, &MainWindow::onVoiceText);
    connect(m_voiceInput, &VoiceInput::wakeWordDetected,
            this, &MainWindow::onWakeWord);
    connect(m_voiceInput, &VoiceInput::volumeLevel, this, [this](float db) {
        if (!m_voiceActive || !m_micBtn) return;
        // Нормализуем -60..-20dB → 0..100%
        bool speaking = (db > -45.0f);
        // Меняем цвет кнопки: зелёный = говорит, обычный = тишина
        m_micBtn->setStyleSheet(speaking
            ? QStringLiteral("QPushButton { background: #0d3a0d; color: #44ff44; ")
              + QStringLiteral("border: 1px solid #44ff44; border-radius: 4px; }")
            : QString());
        m_micBtn->setToolTip(
            QStringLiteral("🎤 %1 | Level: %2 dB")
                .arg(speaking
                    ? (IS_EN ? QStringLiteral("Speaking...") : QStringLiteral("Говорю..."))
                    : (IS_EN ? QStringLiteral("Listening")   : QStringLiteral("Слушаю")))
                .arg(static_cast<int>(db)));
    });

    connect(m_voiceInput, &VoiceInput::whisperModeDetected,
            this, &MainWindow::onWhisperMode);
    connect(m_voiceInput, &VoiceInput::speechDetected, this, [this]() {
        // Мигаем красным пока говорит пользователь
        m_micBtn->setStyleSheet(
            QStringLiteral("QPushButton#micBtn { background-color: #2a0d0d; color: #ff2222; "
                           "border: 1px solid #ff2222; border-radius: 4px; font-size: 16px; }"));
    });
    connect(m_voiceInput, &VoiceInput::initError, this, [this](const QString& err) {
        m_micBtn->setEnabled(false);
        m_micBtn->setToolTip(err);
        appendLog(Str::logSystem(), QStringLiteral("🎤 ") + err, Theme::LogColors::error);
    });

    // Vosk: показываем диалог выбора моделей при первом запуске
    // или когда DLL есть но модели не установлены
    connect(m_voiceInput, &VoiceInput::setupRequired, this, [this]() {
        m_micBtn->setEnabled(false);
        m_micBtn->setText(QStringLiteral("⬇"));

        // Используем стековый диалог — безопасно, exec() блокирует до закрытия
        VoskSetupDialog dlg(m_voiceInput, this);

        connect(&dlg, &VoskSetupDialog::setupStarted, this, [this](const QStringList& ids) {
            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("🔧 Downloading voice models: %1").arg(ids.join(QStringLiteral(", ")))
                      : QStringLiteral("🔧 Скачиваем голосовые модели: %1").arg(ids.join(QStringLiteral(", "))),
                Theme::LogColors::system);
        });

        if (dlg.exec() == QDialog::Rejected) {
            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("ℹ️ Voice setup skipped. Configure later: Settings → Voice Models.")
                      : QStringLiteral("ℹ️ Настройка голоса пропущена. Откройте позже: Настройки → Голосовые модели."),
                Theme::LogColors::system);
        }
    });

    connect(m_voiceInput, &VoiceInput::setupProgress, this,
            [this](const QString& component, int pct, qint64 total) {
        QString totalStr = total > 0
            ? QStringLiteral(" / %1 MB").arg(total / 1024 / 1024)
            : QString();
        m_micBtn->setToolTip(
            QStringLiteral("Installing Vosk [%1]: %2%%3").arg(component).arg(pct).arg(totalStr));
        m_status->setText(
            IS_EN ? QStringLiteral("⬇ [%1] %2%%3").arg(component).arg(pct).arg(totalStr)
                  : QStringLiteral("⬇ [%1] %2%%3").arg(component).arg(pct).arg(totalStr));
    });

    connect(m_voiceInput, &VoiceInput::setupComponentReady, this,
            [this](const QString& component) {
        appendLog(Str::logSystem(),
            QStringLiteral("✅ Vosk component ready: %1").arg(component),
            Theme::LogColors::system);
        // Если EN модель готова — уже можно использовать голос
        if (component == QStringLiteral("model-en")) {
            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("🎤 English model ready! Loading voice input...")
                      : QStringLiteral("🎤 Английская модель готова! Загружаю голосовой ввод..."),
                Theme::LogColors::system);
        }
    });

    connect(m_voiceInput, &VoiceInput::setupLogMessage, this,
            [this](const QString& msg) {
        appendLog(Str::logSystem(), msg, Theme::LogColors::system);
    });

    connect(m_voiceInput, &VoiceInput::setupFinished, this,
            [this](bool success, const QString& err) {
        if (success) {
            m_micBtn->setText(QStringLiteral("🎤"));
            m_status->setText(IS_EN ? QStringLiteral("Ready") : QStringLiteral("Готов"));
            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("🎉 Vosk setup complete! Voice input is ready.")
                      : QStringLiteral("🎉 Vosk установлен! Голосовой ввод готов."),
                Theme::LogColors::system);
        } else {
            m_micBtn->setEnabled(false);
            m_micBtn->setText(QStringLiteral("❌"));
            m_micBtn->setToolTip(err);
            appendLog(Str::logError(),
                IS_EN ? QStringLiteral("❌ Vosk setup failed: %1").arg(err)
                      : QStringLiteral("❌ Установка Vosk не удалась: %1").arg(err),
                Theme::LogColors::error);
        }
    });

    connect(m_voiceInput, &VoiceInput::errorOccurred, this, [this](const QString& err) {
        appendLog(Str::logError(), err, Theme::LogColors::error);
    });

    connect(m_voiceInput, &VoiceInput::modelDownloadStarted, this,
            [this](const QString& modelId) {
        auto info = VoskModels::findById(modelId);
        appendLog(Str::logSystem(),
            QStringLiteral("⬇ %1")
                .arg(info.id.isEmpty() ? modelId : info.displayName),
            Theme::LogColors::system);
    });

    connect(m_voiceInput, &VoiceInput::modelDownloadProgress, this,
            [this](const QString& /*modelId*/, int pct, qint64 total) {
        QString totalStr = total > 0
            ? QStringLiteral(" / %1 MB").arg(total / 1024 / 1024)
            : QString();
        m_status->setText(QStringLiteral("⬇ %1%%2").arg(pct).arg(totalStr));
    });

    connect(m_voiceInput, &VoiceInput::modelDownloadFinished, this,
            [this](const QString& modelId, bool success) {
        m_status->setText(IS_EN ? QStringLiteral("Ready") : QStringLiteral("Готов"));
        auto info = VoskModels::findById(modelId);
        const QString name = info.id.isEmpty() ? modelId : info.displayName;
        if (success) {
            appendLog(Str::logSystem(),
                QStringLiteral("✅ ") + (IS_EN ? QStringLiteral("Model ready: ") : QStringLiteral("Модель готова: ")) + name,
                Theme::LogColors::system);
        } else {
            appendLog(Str::logError(),
                QStringLiteral("❌ ") + (IS_EN ? QStringLiteral("Download failed: ") : QStringLiteral("Ошибка загрузки: ")) + name,
                Theme::LogColors::error);
        }
    });

    // Инициализируем Vosk (показывает диалог выбора или загружает готовые модели)
    m_voiceInput->initialize();

    // ── Инициализация пассивного слушателя ───────────────────
    m_passiveListener = new PassiveListener(this);

    connect(m_passiveListener, &PassiveListener::entrySaved, this,
            [this](const VoiceJournalEntry& e) {
        // Тихо — не спамим в лог, только в debug
        Q_UNUSED(e)
    });
    connect(m_passiveListener, &PassiveListener::journalProcessed, this,
            [this](int pairs) {
        // Тихо — только если есть новые пары, и только в debug
        if (pairs > 0)
            qDebug() << "[Training] Voice journal:" << pairs << "new pairs";
    });
    connect(m_passiveListener, &PassiveListener::weeklyCleanupDone, this,
            [this](int deleted) {
        // Тихо — еженедельная очистка не спамит в лог
        if (deleted > 0)
            qDebug() << "[Training] Weekly cleanup:" << deleted << "entries";
    });

    // Загружаем путь к датасету из настроек
    QString datasetPath = DatabaseManager::instance().getConfig(
        QStringLiteral("voice_dataset_path")).toString();

    PassiveListenerConfig passiveCfg;
    if (!datasetPath.isEmpty()) passiveCfg.datasetPath = datasetPath;

    // Пути к Vosk моделям — те же что у VoiceInput
    passiveCfg.modelPathRu = m_voiceInput->config().modelPathRu;
    passiveCfg.modelPathEn = m_voiceInput->config().modelPathEn;

    m_passiveListener->initialize(passiveCfg);

    // Автостарт пассивной записи — начинаем сразу после загрузки моделей
    // Пользователь может выключить через меню Training → Пассивная запись
    connect(m_passiveListener, &PassiveListener::ready, this, [this]() {
        m_passiveListener->startListening();
        if (m_passiveAction) {
            m_passiveAction->setChecked(true);
            m_passiveAction->setText(
                IS_EN ? QStringLiteral("🎙️ Passive Recording: ON")
                      : QStringLiteral("🎙️ Пассивная запись: ВКЛ"));
        }
        appendLog(Str::logSystem(),
            IS_EN ? QStringLiteral("🎙️ Passive voice recording started — every phrase saved to training dataset")
                  : QStringLiteral("🎙️ Пассивная запись запущена — каждая фраза сохраняется в датасет"),
            Theme::LogColors::system);
    });

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
        m_micBtn->setToolTip(IS_EN ? QStringLiteral("Voice input (Vosk)")
                                   : QStringLiteral("Голосовой ввод (Vosk)"));
        m_micBtn->style()->unpolish(m_micBtn);
        m_micBtn->style()->polish(m_micBtn);
        m_status->setText(IS_EN ? QStringLiteral("Ready") : QStringLiteral("Готов"));
    }
}

void MainWindow::onVoiceReady()
{
    m_micBtn->setEnabled(true);
    appendLog(Str::logSystem(),
              IS_EN ? QStringLiteral("🎤 Vosk models loaded. Voice input ready.")
                    : QStringLiteral("🎤 Модели Vosk загружены. Голосовой ввод готов."),
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

    // Помечаем что ввод голосовой — onAsyncResponse сохранит пару в voice_journal
    m_lastInputWasVoice = true;
    m_lastVoiceLanguage = lang;

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
        m_micBtn->setToolTip(IS_EN ? QStringLiteral("🤫 Quiet voice detected")
                                   : QStringLiteral("🤫 Обнаружен шёпот"));
        appendLog(Str::logSystem(),
                  IS_EN ? QStringLiteral("🤫 Whisper detected — low volume mode active")
                        : QStringLiteral("🤫 Обнаружен шёпот — режим тихого голоса"),
                  Theme::LogColors::system);
    }
}
// ============================================================
// Fine-tuning — лайк и экспорт
// ============================================================

void MainWindow::onLikeLastResponse()
{
    if (m_lastAiResponse.isEmpty() || m_lastUserInput.isEmpty()) return;

    DbTrainingLog log;
    log.userId      = 1;
    log.userMessage = m_lastUserInput;
    log.aiResponse  = m_lastAiResponse;
    log.model       = m_lastAiModel.isEmpty() ? QStringLiteral("claude") : m_lastAiModel;
    log.sessionId   = m_lastSessionId;
    log.rating      = 1;

    // Сначала пробуем обновить существующую запись (автосохранённую)
    // Если нет — добавляем новую
    auto& db = DatabaseManager::instance();
    // UPDATE rating где уже есть эта пара
    db.updateTrainingLogRating(m_lastUserInput, m_lastAiResponse, 1);
    qint64 id = db.addTrainingLog(log);  // INSERT OR IGNORE если не было

    if (id > 0 || true) {  // true — лайк засчитываем в любом случае
        ++m_trainingCount;
        // Меняем вид кнопки — уже лайкнуто
        m_likeBtn->setProperty("liked", true);
        m_likeBtn->setText(QStringLiteral("✅"));
        m_likeBtn->setEnabled(false);
        m_likeBtn->style()->unpolish(m_likeBtn);
        m_likeBtn->style()->polish(m_likeBtn);
        m_likeBtn->setToolTip(IS_EN
            ? QStringLiteral("Saved! Total: %1 responses").arg(m_trainingCount)
            : QStringLiteral("Сохранено! Всего: %1 ответов").arg(m_trainingCount));

        // Лайк — тихое подтверждение через tooltip кнопки, не спамим лог
        qDebug() << "[Training] Liked response saved, total:" << m_trainingCount;
    } else {
        // Дубликат — уже был такой ответ
        appendLog(Str::logSystem(),
            IS_EN ? QStringLiteral("ℹ️ Already in dataset (duplicate skipped).")
                  : QStringLiteral("ℹ️ Уже в датасете (дубликат пропущен)."),
            Theme::LogColors::system);
    }
}

void MainWindow::onExportTrainingData()
{
    auto& db = DatabaseManager::instance();
    int count = db.trainingLogCount(1);

    if (count == 0) {
        QMessageBox::information(this,
            IS_EN ? QStringLiteral("Export") : QStringLiteral("Экспорт"),
            IS_EN ? QStringLiteral("No training data yet. Like some responses first (👍 button).")
                  : QStringLiteral("Нет данных для экспорта. Сначала лайкните несколько ответов (кнопка 👍)."));
        return;
    }

    QString defaultName = QStringLiteral("jarvis_dataset_%1.jsonl")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmm")));

    QString filePath = QFileDialog::getSaveFileName(this,
        IS_EN ? QStringLiteral("Export Training Data") : QStringLiteral("Экспорт датасета"),
        QDir::homePath() + QStringLiteral("/") + defaultName,
        QStringLiteral("JSONL Files (*.jsonl);;All Files (*)"));

    if (filePath.isEmpty()) return;

    if (db.exportToJsonl(1, filePath)) {
        QString msg = IS_EN
            ? QStringLiteral("✅ Exported %1 training pairs to:\n%2\n\n"
                             "Upload this file to Google Colab for Fine-Tuning.\n"
                             "Recommended: Unsloth or LLaMA-Factory.").arg(count).arg(filePath)
            : QStringLiteral("✅ Экспортировано %1 пар в:\n%2\n\n"
                             "Загрузите этот файл в Google Colab для Fine-Tuning.\n"
                             "Рекомендуется: Unsloth или LLaMA-Factory.").arg(count).arg(filePath);

        QMessageBox::information(this,
            IS_EN ? QStringLiteral("Export Complete") : QStringLiteral("Экспорт завершён"), msg);

        appendLog(Str::logSystem(),
            IS_EN ? QStringLiteral("📤 Exported %1 training pairs to: %2").arg(count).arg(filePath)
                  : QStringLiteral("📤 Экспортировано %1 пар в: %2").arg(count).arg(filePath),
            Theme::LogColors::system);
    } else {
        QMessageBox::warning(this,
            IS_EN ? QStringLiteral("Export Error") : QStringLiteral("Ошибка экспорта"),
            IS_EN ? QStringLiteral("Failed to write file: ") + filePath
                  : QStringLiteral("Не удалось записать файл: ") + filePath);
    }
}