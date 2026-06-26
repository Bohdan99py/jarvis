// -------------------------------------------------------
// mainwindow.cpp — Главное окно J.A.R.V.I.S.
// -------------------------------------------------------

#include "mainwindow.h"
#include "jarvis.h"
#include "jarvis_paths.h"
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
#include <QSqlQuery>
#include "local_trainer.h"
#include "task_manager_dialog.h"
#include "translation_engine.h"
#include "VoskSetupDialog.h"
#include <QClipboard>
#include "activity_tracker.h"
#include "user_profile.h"
#include "mobile_pairing_manager.h"
#include "j2j_mesh_connector.h"
#include "j2j_telegram_gateway.h"
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
#include <QGraphicsOpacityEffect>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QInputDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
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
    bool english = cfg.value(QStringLiteral("ui/english"), true).toBool();
    gUiLanguage() = english ? UiLanguage::English : UiLanguage::Russian;
    // Синхронизируем детектор языка с настройкой
    // (дефолт — русский, совпадает с конструктором LanguageDetector)

    m_jarvis = new Jarvis(this);
    m_jarvis->setUiLanguage(english);

    m_audioManager = new AudioManager(this);

    // Restore last active user
    {
        qint64 lastUserId = cfg.value(QStringLiteral("user/currentId"), 1).toLongLong();
        auto user = DatabaseManager::instance().getUser(lastUserId);
        if (user) {
            m_jarvis->setCurrentUserId(user->id);
            m_jarvis->memory()->setActiveUserName(user->name);
        }
    }

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

    connect(m_jarvis, &Jarvis::meshEvent,
            this, [this](const QString& message) {
                appendLog(QStringLiteral("J.A.R.V.I.S."), message, "#66FCF1");
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
            this, [this](const QString& path) {
        m_updateProgress->setVisible(false);

        m_updateBtn->setText(IS_EN ? QStringLiteral("Open Folder")
                                   : QStringLiteral("Открыть папку"));
        m_updateBtn->setVisible(true);
        disconnect(m_updateBtn, nullptr, nullptr, nullptr);
        connect(m_updateBtn, &QPushButton::clicked, this, [this, path]() {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        });

        const QString notes = m_jarvis->autoUpdater()->pendingNotes();
        const auto& tc = ThemeManager::colors(m_themeIndex);
        QString card = QStringLiteral(
            "<div style='background:%1; border:1px solid %2; border-radius:10px; "
            "padding:12px 16px; margin:4px 0;'>"
            "<b style='color:%3;'>Update v%4 downloaded</b><br>"
            "<span style='color:%5; font-size:12px;'>Saved to: %6</span>"
        ).arg(tc.cardBg, tc.cardBorder, tc.system,
              m_jarvis->autoUpdater()->pendingVersion(),
              tc.timestamp, path);
        if (!notes.isEmpty()) {
            QString safeNotes = notes.toHtmlEscaped()
                                     .replace(QStringLiteral("\n"), QStringLiteral("<br>"));
            card += QStringLiteral("<hr style='border:none; border-top:1px solid %1; margin:8px 0;'>"
                                   "<span style='color:%2; font-size:12px;'>%3</span>")
                        .arg(tc.cardBorder, tc.timestamp, safeNotes);
        }
        card += QStringLiteral("</div>");
        m_log->append(card);
        m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
    });
    connect(updater, &AutoUpdater::updateError,
            this, [this](const QString& error) {
        appendLog(Str::logError(), error, Theme::LogColors::error);
    });

    buildUI();
    buildMenuBar();
    menuBar()->setVisible(false);

    m_themeIndex = cfg.value(QStringLiteral("ui/theme"), 0).toInt();
    if (m_themeIndex < 0 || m_themeIndex >= ThemeManager::ThemeCount)
        m_themeIndex = 0;
    applyThemeStyleSheet(m_themeIndex);

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

    // What's New — show once per version
    {
        QSettings cfg2(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
        QString lastVer = cfg2.value(QStringLiteral("ui/last_seen_version")).toString();
        QString currentVer = QCoreApplication::applicationVersion();
        if (lastVer != currentVer) {
            appendLog(Str::logJarvis(),
                      Str::whatsNew().arg(currentVer),
                      Theme::LogColors::jarvis);
            cfg2.setValue(QStringLiteral("ui/last_seen_version"), currentVer);
        }
    }

    // ── Статус базы данных ────────────────────────────────
    // Показываем реальный путь к БД при каждом старте — это сразу
    // видно если debug/release-сборки вдруг разошлись по путям.
    {
        auto& db = DatabaseManager::instance();
        const QString dbPath = db.dbPath();
        const bool dbOk      = db.isOpen();
        if (dbOk) {
            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("💾 Database: %1").arg(dbPath)
                      : QStringLiteral("💾 База данных: %1").arg(dbPath),
                Theme::LogColors::system);
        } else {
            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("❌ Database FAILED to open: %1\nPath: %2")
                            .arg(db.lastError(), dbPath)
                      : QStringLiteral("❌ База данных НЕ открылась: %1\nПуть: %2")
                            .arg(db.lastError(), dbPath),
                QStringLiteral("#ff4444"));
        }
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

    // Random startup tip
    appendLog(Str::logJarvis(), Str::startupTip(), QStringLiteral("#7c4dff"));

    m_input->setFocus();

    // ── Самообучение ──────────────────────────────────────
    m_learnedCmds = new LearnedCommands(this);
    connect(m_learnedCmds, &LearnedCommands::commandLearned,
            this, &MainWindow::onCommandLearned);

    // ── Зрение + управление окнами ───────────────────────
    m_screenAgent = new ScreenAgent(this);

    // ── Паттерн-обучение: какие приложения используются ──────
    m_appLearner = new ScreenshotLearner(this);
    connect(m_appLearner, &ScreenshotLearner::suggestionReady,
            this, [this](const AppSuggestion& s) {
        if (s.confidence >= 0.65f && m_suggestionBar) {
            QString desc = IS_EN
                ? QStringLiteral("Usually at this time you use %1. Open it?").arg(s.appName)
                : QStringLiteral("Обычно в это время вы используете %1. Открыть?").arg(s.appName);
            m_pendingSuggestionAction = s.appName;
            onSuggestion(desc, s.appName);
        }
    });
    // Запускаем ПОСЛЕ полной инициализации окна
    QTimer::singleShot(3000, this, [this]() {
        if (m_appLearner) m_appLearner->start(2);
    });
    connect(m_screenAgent, &ScreenAgent::actionCompleted,
            this, [this](const QString& desc) {
        appendLog(Str::logJarvis(), desc, Theme::LogColors::system);
    });

    m_pulseTimer = new QTimer(this);
    connect(m_pulseTimer, &QTimer::timeout, this, [this]() {
        m_pulse = !m_pulse;
        if (m_jarvis->isSpeaking()) {
            m_dot->setStyleSheet(m_pulse
                ? QStringLiteral("color: #45A29E; font-size: 20px;")
                : QStringLiteral("color: rgba(69,162,158,0.35); font-size: 16px;"));
        } else if (!m_input->isEnabled()) {
            m_dot->setStyleSheet(m_pulse
                ? QStringLiteral("color: #66FCF1; font-size: 20px;")
                : QStringLiteral("color: rgba(102,252,241,0.30); font-size: 16px;"));
        }
    });
    m_pulseTimer->start(400);

    m_jarvis->autoUpdater()->checkForUpdates(true);

    // Startup deadline check — notify about overdue/approaching tasks
    QTimer::singleShot(3000, this, [this]() {
        QString warnings = m_jarvis->getOverdueTasksSummary();
        if (!warnings.isEmpty()) {
            appendLog(IS_EN ? QStringLiteral("J.A.R.V.I.S.") : QStringLiteral("Д.Ж.А.Р.В.И.С."),
                warnings, Theme::LogColors::error);
        }
    });
}

// ============================================================
// applyLanguage
// ============================================================

void MainWindow::applyLanguage(bool english)
{
    gUiLanguage() = english ? UiLanguage::English : UiLanguage::Russian;
    m_jarvis->setUiLanguage(english);
    QSettings cfg(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
    cfg.setValue(QStringLiteral("ui/english"), english);

    // ── Перестраиваем всё меню — все Str::* вернут новые строки ──
    // Откладываем на следующий цикл: текущий QAction ещё в стеке вызовов
    QTimer::singleShot(0, this, [this]() {
        menuBar()->clear();
        buildMenuBar();
        menuBar()->setVisible(false);
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
// applyTheme — переключение темы интерфейса
// ============================================================

void MainWindow::applyTheme(int index)
{
    auto* fadeEffect = new QGraphicsOpacityEffect(this);
    fadeEffect->setOpacity(1.0);
    setGraphicsEffect(fadeEffect);

    auto* fadeOut = new QPropertyAnimation(fadeEffect, "opacity", this);
    fadeOut->setDuration(120);
    fadeOut->setStartValue(1.0);
    fadeOut->setEndValue(0.5);

    auto* fadeIn = new QPropertyAnimation(fadeEffect, "opacity", this);
    fadeIn->setDuration(200);
    fadeIn->setStartValue(0.5);
    fadeIn->setEndValue(1.0);

    connect(fadeOut, &QPropertyAnimation::finished, this, [this, index, fadeIn, fadeEffect]() {
        applyThemeStyleSheet(index);
        fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
    });
    connect(fadeIn, &QPropertyAnimation::finished, this, [this, fadeEffect]() {
        setGraphicsEffect(nullptr);
    });
    fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::applyThemeStyleSheet(int index)
{
    Q_UNUSED(index)
    ThemeManager::applyStyleSheet(ThemeManager::Cyberpunk);
    m_themeIndex = ThemeManager::Cyberpunk;
}

// Legacy inline themes kept for reference but no longer called.
// ThemeManager::applyStyleSheet() now handles all QSS application.
#if 0 // ── Old inline themes (superseded by theme_manager.h) ──────
static void applyThemeStyleSheet_legacy(int index)
{
    switch (index) {
    case 1: {
        QString glass = QStringLiteral(
            "QMainWindow { background-color: rgba(16, 22, 42, 185); }"
            "QWidget { background-color: transparent; color: #d4e6f8; "
            "  font-family: 'Segoe UI', 'Consolas', monospace; font-size: 13px; }"
            "#titleLabel { color: #66ccff; font-size: 26px; font-weight: bold; "
            "  font-family: 'Segoe UI Semibold', 'Segoe UI', sans-serif; letter-spacing: 4px; padding: 6px 0 2px 0; }"
            "#statusText { color: rgba(102, 204, 255, 160); font-size: 11px; padding-bottom: 2px; }"
            "#separator { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "  stop:0 transparent, stop:0.15 rgba(102,204,255,50),"
            "  stop:0.35 #66ccff, stop:0.5 #9070ff, stop:0.65 #66ccff,"
            "  stop:0.85 rgba(102,204,255,50), stop:1 transparent);"
            "  min-height: 2px; max-height: 2px; }"

            "#logArea { background-color: rgba(12, 18, 36, 130); color: #d0e4f4;"
            "  border: 1px solid rgba(102,204,255,50); border-radius: 14px;"
            "  padding: 12px; font-family: 'Consolas', monospace; font-size: 13px;"
            "  selection-background-color: rgba(102,204,255,160); selection-color: #fff; }"
            "#logArea QScrollBar:vertical { background: transparent; width: 4px; border-radius: 2px; margin: 4px 1px; }"
            "#logArea QScrollBar::handle:vertical { background: rgba(102,204,255,45); border-radius: 2px; min-height: 30px; }"
            "#logArea QScrollBar::handle:vertical:hover { background: rgba(102,204,255,130); }"
            "#logArea QScrollBar::add-line:vertical, #logArea QScrollBar::sub-line:vertical { height: 0; }"
            "#logArea QScrollBar::add-page:vertical, #logArea QScrollBar::sub-page:vertical { background: transparent; }"

            "#inputField { background-color: rgba(18, 26, 50, 140); color: #e4f0ff;"
            "  border: 1px solid rgba(102,204,255,35); border-radius: 10px;"
            "  padding: 10px 14px; font-size: 14px; font-family: 'Consolas', monospace; }"
            "#inputField:focus { border: 2px solid qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "  stop:0 #66ccff, stop:1 #9070ff); background-color: rgba(18,26,50,200); padding: 9px 13px; }"
            "#inputField::placeholder { color: rgba(160,210,240,50); }"

            "#sendBtn { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "  stop:0 rgba(70,190,255,210), stop:1 rgba(140,100,255,210));"
            "  color: #fff; border: none; border-radius: 10px; padding: 9px 18px;"
            "  font-weight: bold; font-size: 14px; font-family: 'Segoe UI Semibold', sans-serif; }"
            "#sendBtn:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "  stop:0 rgba(100,210,255,230), stop:1 rgba(160,120,255,230));"
            "  border: 1px solid rgba(102,204,255,100); }"
            "#sendBtn:pressed { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "  stop:0 #9070ff, stop:1 #66ccff); }"

            "#clearBtn, #kbToggleBtn { background-color: transparent;"
            "  color: rgba(160,210,240,70); border: 1px solid rgba(102,204,255,18);"
            "  border-radius: 8px; padding: 5px 14px; font-size: 11px; }"
            "#clearBtn:hover, #kbToggleBtn:hover { color: #d0e8f8;"
            "  border-color: rgba(102,204,255,50); background-color: rgba(102,204,255,8); }"

            "#micBtn { background-color: rgba(18,30,55,160); color: rgba(100,180,220,150);"
            "  border: 1px solid rgba(102,204,255,28); border-radius: 10px; font-size: 16px; }"
            "#micBtn:hover { background-color: rgba(24,38,68,180); color: #66ccff;"
            "  border-color: rgba(102,204,255,70); }"
            "#micBtn[active=\"true\"] { background-color: rgba(60,10,10,170); color: #ff5252;"
            "  border: 2px solid rgba(255,82,82,160); }"

            "#likeBtn { background: transparent; color: rgba(0,230,118,50);"
            "  border: 1px solid rgba(0,230,118,22); border-radius: 8px; font-size: 13px; padding: 0 8px; }"
            "#likeBtn:hover { background: rgba(0,230,118,10); color: #00e676;"
            "  border-color: rgba(0,230,118,80); }"
            "#likeBtn:disabled { color: rgba(0,230,118,20); border-color: rgba(0,230,118,8); }"
            "#likeBtn[liked=\"true\"] { color: #00e676; border-color: #00e676; background: rgba(0,230,118,18); }"

            "QMenuBar { background-color: rgba(14,22,42,180); color: #d0e4f4; font-size: 12px;"
            "  border-bottom: 1px solid rgba(102,204,255,18); }"
            "QMenuBar::item { background: transparent; padding: 5px 12px; border-radius: 6px; }"
            "QMenuBar::item:selected { background-color: rgba(102,204,255,22); color: #66ccff; }"
            "QMenu { background-color: rgba(14,22,42,235); color: #d0e4f4;"
            "  border: 1px solid rgba(102,204,255,28); border-radius: 10px; padding: 4px; }"
            "QMenu::item { padding: 6px 24px 6px 12px; border-radius: 6px; }"
            "QMenu::item:selected { background-color: rgba(102,204,255,28); color: #66ccff; }"
            "QMenu::separator { height: 1px; background: rgba(102,204,255,14); margin: 4px 8px; }"

            "#keyboardPanel { background-color: rgba(14,22,42,190); border-top: 1px solid rgba(102,204,255,18); }"
            "#keyboardPanel QPushButton { background-color: rgba(20,32,58,160); color: #d0e4f4;"
            "  border: 1px solid rgba(102,204,255,22); border-radius: 8px;"
            "  font-family: 'Consolas', monospace; font-size: 13px; min-height: 34px; min-width: 32px; }"
            "#keyboardPanel QPushButton:hover { background-color: rgba(102,204,255,18);"
            "  border-color: rgba(102,204,255,50); color: #e4f0ff; }"
            "#keyboardPanel QPushButton:pressed { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "  stop:0 #66ccff, stop:1 #9070ff); color: #fff; border-color: #66ccff; }"
            "#keyboardPanel #kbSpecialKey { background-color: rgba(16,26,48,180);"
            "  color: #66ccff; font-weight: bold; border-color: rgba(102,204,255,35); }"
            "#keyboardPanel #kbSpaceBar { min-width: 200px; }"

            "QGroupBox, QFrame#suggestionPanel, QFrame#clarifyPanel,"
            "QFrame#updatePanel, QFrame#attachPanel {"
            "  background-color: rgba(14,22,42,160); border: 1px solid rgba(102,204,255,28);"
            "  border-radius: 12px; padding: 10px; }"

            "QScrollBar:vertical { background: transparent; width: 4px; border-radius: 2px; }"
            "QScrollBar::handle:vertical { background: rgba(102,204,255,35); border-radius: 2px; min-height: 24px; }"
            "QScrollBar::handle:vertical:hover { background: rgba(102,204,255,100); }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
            "QScrollBar:horizontal { background: transparent; height: 4px; border-radius: 2px; }"
            "QScrollBar::handle:horizontal { background: rgba(102,204,255,35); border-radius: 2px; min-width: 24px; }"
            "QScrollBar::handle:horizontal:hover { background: rgba(102,204,255,100); }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"

            "QToolTip { background-color: rgba(14,22,42,235); color: #e8f4ff;"
            "  border: 1px solid rgba(102,204,255,45); border-radius: 8px; padding: 6px 10px; font-size: 12px; }"

            "QDialog { background-color: rgba(14,22,42,245); color: #d0e4f4; }"
            "QLabel { background: transparent; }"
            "QCheckBox { color: #d0e4f4; spacing: 6px; }"
            "QRadioButton { color: #d0e4f4; spacing: 6px; }"
            "QComboBox { background-color: rgba(20,30,55,180); color: #d0e4f4;"
            "  border: 1px solid rgba(102,204,255,28); border-radius: 6px; padding: 4px 8px; }"
            "QComboBox QAbstractItemView { background-color: rgba(14,22,42,240); color: #d0e4f4;"
            "  border: 1px solid rgba(102,204,255,28); selection-background-color: rgba(102,204,255,28); }"
            "QSpinBox, QDoubleSpinBox { background-color: rgba(20,30,55,180); color: #d0e4f4;"
            "  border: 1px solid rgba(102,204,255,28); border-radius: 4px; padding: 2px 6px; }"
            "QLineEdit { background-color: rgba(18,26,50,160); color: #e4f0ff;"
            "  border: 1px solid rgba(102,204,255,28); border-radius: 6px; padding: 4px 8px; }"
            "QTextEdit { background-color: rgba(12,18,36,140); color: #d0e4f4;"
            "  border: 1px solid rgba(102,204,255,35); border-radius: 8px; }"
            "QPushButton { background-color: rgba(20,30,55,160); color: #d0e4f4;"
            "  border: 1px solid rgba(102,204,255,22); border-radius: 6px; padding: 5px 12px; }"
            "QPushButton:hover { background-color: rgba(102,204,255,15); color: #e4f0ff;"
            "  border-color: rgba(102,204,255,50); }"
            "QProgressBar { background: rgba(14,22,42,180); border: 1px solid rgba(102,204,255,22);"
            "  border-radius: 4px; color: #d0e4f4; font-size: 10px; text-align: center; }"
            "QProgressBar::chunk { background: rgba(102,204,255,170); border-radius: 3px; }"
            "#hamburgerBtn { background: transparent; color: rgba(160,210,240,70);"
            "  border: 1px solid transparent; border-radius: 8px; }"
            "#hamburgerBtn:hover { background: rgba(102,204,255,8); color: #66ccff;"
            "  border-color: rgba(102,204,255,20); }"
        );
        qApp->setStyleSheet(glass);
        break;
    }
    case 2: { // Light — soft warm gray, not aggressive white
        QString light = QStringLiteral(
            "QMainWindow { background-color: #e4e8ee; }"
            "QWidget { background-color: transparent; color: #2c3e50;"
            "  font-family: 'Segoe UI', 'Consolas', monospace; font-size: 13px; }"
            "#titleLabel { color: #1a6bb5; font-size: 26px; font-weight: bold;"
            "  font-family: 'Segoe UI Semibold', 'Segoe UI', sans-serif; letter-spacing: 4px; padding: 6px 0 2px 0; }"
            "#statusText { color: rgba(26, 107, 181, 150); font-size: 11px; padding-bottom: 2px; }"
            "#separator { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "  stop:0 transparent, stop:0.15 rgba(26,107,181,40),"
            "  stop:0.35 #1a6bb5, stop:0.5 #6b4dcc, stop:0.65 #1a6bb5,"
            "  stop:0.85 rgba(26,107,181,40), stop:1 transparent);"
            "  min-height: 2px; max-height: 2px; }"

            "#logArea { background-color: #f0f3f7; color: #2c3e50;"
            "  border: 1px solid #cdd5de; border-radius: 10px;"
            "  padding: 12px; font-family: 'Consolas', monospace; font-size: 13px;"
            "  selection-background-color: rgba(26,107,181,160); selection-color: #fff; }"
            "#logArea QScrollBar:vertical { background: transparent; width: 5px; border-radius: 2px; margin: 4px 1px; }"
            "#logArea QScrollBar::handle:vertical { background: rgba(26,107,181,30); border-radius: 2px; min-height: 30px; }"
            "#logArea QScrollBar::handle:vertical:hover { background: rgba(26,107,181,80); }"
            "#logArea QScrollBar::add-line:vertical, #logArea QScrollBar::sub-line:vertical { height: 0; }"
            "#logArea QScrollBar::add-page:vertical, #logArea QScrollBar::sub-page:vertical { background: transparent; }"

            "#inputField { background-color: #f0f3f7; color: #2c3e50;"
            "  border: 1px solid #c0c8d2; border-radius: 8px;"
            "  padding: 10px 14px; font-size: 14px; font-family: 'Consolas', monospace; }"
            "#inputField:focus { border: 2px solid #1a6bb5; background-color: #ffffff; padding: 9px 13px; }"
            "#inputField::placeholder { color: rgba(80,100,120,50); }"

            "#sendBtn { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "  stop:0 #1a6bb5, stop:1 #6b4dcc); color: #fff; border: none;"
            "  border-radius: 8px; padding: 9px 18px; font-weight: bold; font-size: 14px;"
            "  font-family: 'Segoe UI Semibold', sans-serif; }"
            "#sendBtn:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "  stop:0 #2080cc, stop:1 #7c60dd); border: 1px solid rgba(26,107,181,60); }"
            "#sendBtn:pressed { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "  stop:0 #6b4dcc, stop:1 #1a6bb5); }"

            "#clearBtn, #kbToggleBtn { background-color: transparent;"
            "  color: #8898a8; border: 1px solid #c8d0d8; border-radius: 6px;"
            "  padding: 5px 14px; font-size: 11px; }"
            "#clearBtn:hover, #kbToggleBtn:hover { color: #1a6bb5;"
            "  border-color: #1a6bb5; background-color: rgba(26,107,181,5); }"

            "#micBtn { background-color: #eceef2; color: #6a7a8a;"
            "  border: 1px solid #c0c8d2; border-radius: 8px; font-size: 16px; }"
            "#micBtn:hover { background-color: #e0e4ea; color: #1a6bb5; border-color: #1a6bb5; }"
            "#micBtn[active=\"true\"] { background-color: #fde8e8; color: #d83030;"
            "  border: 2px solid #d83030; }"

            "#likeBtn { background: transparent; color: rgba(0,150,80,35);"
            "  border: 1px solid rgba(0,150,80,20); border-radius: 6px; font-size: 13px; padding: 0 8px; }"
            "#likeBtn:hover { background: rgba(0,150,80,6); color: #009650; border-color: #009650; }"
            "#likeBtn:disabled { color: rgba(0,150,80,15); border-color: rgba(0,150,80,8); }"
            "#likeBtn[liked=\"true\"] { color: #009650; border-color: #009650; background: rgba(0,150,80,8); }"

            "QMenuBar { background-color: #e4e8ee; color: #2c3e50; font-size: 12px;"
            "  border-bottom: 1px solid #cdd5de; }"
            "QMenuBar::item { background: transparent; padding: 5px 12px; border-radius: 4px; }"
            "QMenuBar::item:selected { background-color: #d8e0e8; color: #1a6bb5; }"
            "QMenu { background-color: #f5f7fa; color: #2c3e50;"
            "  border: 1px solid #cdd5de; border-radius: 8px; padding: 4px; }"
            "QMenu::item { padding: 6px 24px 6px 12px; border-radius: 4px; }"
            "QMenu::item:selected { background-color: #e0e8f0; color: #1a6bb5; }"
            "QMenu::separator { height: 1px; background: #d8e0e8; margin: 4px 8px; }"

            "#keyboardPanel { background-color: #dde2e8; border-top: 1px solid #c8d0d8; }"
            "#keyboardPanel QPushButton { background-color: #f0f3f7; color: #2c3e50;"
            "  border: 1px solid #c0c8d2; border-radius: 6px;"
            "  font-family: 'Consolas', monospace; font-size: 13px; min-height: 34px; min-width: 32px; }"
            "#keyboardPanel QPushButton:hover { background-color: #e0e8f0; border-color: #1a6bb5; color: #1a6bb5; }"
            "#keyboardPanel QPushButton:pressed { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "  stop:0 #1a6bb5, stop:1 #6b4dcc); color: #fff; border-color: #1a6bb5; }"
            "#keyboardPanel #kbSpecialKey { background-color: #e4e8ee; color: #1a6bb5;"
            "  font-weight: bold; border-color: #b8c4d0; }"
            "#keyboardPanel #kbSpaceBar { min-width: 200px; }"

            "QGroupBox, QFrame#suggestionPanel, QFrame#clarifyPanel,"
            "QFrame#updatePanel, QFrame#attachPanel {"
            "  background-color: #eceef2; border: 1px solid #cdd5de; border-radius: 8px; padding: 10px; }"

            "QScrollBar:vertical { background: transparent; width: 5px; border-radius: 2px; }"
            "QScrollBar::handle:vertical { background: rgba(26,107,181,25); border-radius: 2px; min-height: 24px; }"
            "QScrollBar::handle:vertical:hover { background: rgba(26,107,181,70); }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
            "QScrollBar:horizontal { background: transparent; height: 5px; border-radius: 2px; }"
            "QScrollBar::handle:horizontal { background: rgba(26,107,181,25); border-radius: 2px; min-width: 24px; }"
            "QScrollBar::handle:horizontal:hover { background: rgba(26,107,181,70); }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"

            "QToolTip { background-color: #f5f7fa; color: #2c3e50;"
            "  border: 1px solid #cdd5de; border-radius: 6px; padding: 6px 10px; font-size: 12px; }"

            "QDialog { background-color: #eceef2; color: #2c3e50; }"
            "QLabel { background: transparent; }"
            "QCheckBox { color: #2c3e50; spacing: 6px; }"
            "QRadioButton { color: #2c3e50; spacing: 6px; }"
            "QComboBox { background-color: #f0f3f7; color: #2c3e50;"
            "  border: 1px solid #c0c8d2; border-radius: 6px; padding: 4px 8px; }"
            "QComboBox QAbstractItemView { background-color: #f5f7fa; color: #2c3e50;"
            "  border: 1px solid #cdd5de; selection-background-color: #e0e8f0; }"
            "QSpinBox, QDoubleSpinBox { background-color: #f0f3f7; color: #2c3e50;"
            "  border: 1px solid #c0c8d2; border-radius: 4px; padding: 2px 6px; }"
            "QLineEdit { background-color: #f0f3f7; color: #2c3e50;"
            "  border: 1px solid #c0c8d2; border-radius: 6px; padding: 4px 8px; }"
            "QTextEdit { background-color: #f0f3f7; color: #2c3e50;"
            "  border: 1px solid #cdd5de; border-radius: 8px; }"
            "QPushButton { background-color: #eceef2; color: #2c3e50;"
            "  border: 1px solid #c0c8d2; border-radius: 6px; padding: 5px 12px; }"
            "QPushButton:hover { background-color: #e0e8f0; color: #1a6bb5; border-color: #1a6bb5; }"
            "QProgressBar { background: #e4e8ee; border: 1px solid #c8d0d8;"
            "  border-radius: 4px; color: #2c3e50; font-size: 10px; text-align: center; }"
            "QProgressBar::chunk { background: #1a6bb5; border-radius: 3px; }"
            "#hamburgerBtn { background: transparent; color: #8898a8;"
            "  border: 1px solid transparent; border-radius: 8px; }"
            "#hamburgerBtn:hover { background: rgba(26,107,181,6); color: #1a6bb5;"
            "  border-color: rgba(26,107,181,15); }"
        );
        qApp->setStyleSheet(light);
        break;
    }
    default:
        qApp->setStyleSheet(Theme::globalStyleSheet());
        break;
    }
}
#endif // legacy themes

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

    // --- Пользователь (multi-user) ---
    {
        auto* userMenu = menuBar->addMenu(
            IS_EN ? QStringLiteral("👤 User") : QStringLiteral("👤 Пользователь"));

        // Switch user
        auto* actSwitch = userMenu->addAction(
            IS_EN ? QStringLiteral("Switch User...") : QStringLiteral("Сменить пользователя..."));
        connect(actSwitch, &QAction::triggered, this, [this]() {
            auto& db = DatabaseManager::instance();
            auto users = db.getAllUsers();

            QStringList items;
            for (const auto& u : users)
                items << QStringLiteral("%1 — %2 (%3)").arg(u.name, u.scenario, u.language);
            items << (IS_EN ? QStringLiteral("+ New user...") : QStringLiteral("+ Новый пользователь..."));

            bool ok;
            QString choice = QInputDialog::getItem(this,
                IS_EN ? QStringLiteral("Switch User") : QStringLiteral("Сменить пользователя"),
                IS_EN ? QStringLiteral("Select user:") : QStringLiteral("Выберите пользователя:"),
                items, 0, false, &ok);
            if (!ok) return;

            int idx = items.indexOf(choice);
            if (idx >= 0 && idx < users.size()) {
                // Switch to existing user
                auto& user = users[idx];
                m_jarvis->setCurrentUserId(user.id);
                m_jarvis->memory()->setActiveUserName(user.name);
                db.updateUser(user); // touch last_seen
                QSettings(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"))
                    .setValue(QStringLiteral("user/currentId"), user.id);
                appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                    (IS_EN ? QStringLiteral("Switched to user: ") : QStringLiteral("Переключено на: ")) + user.name,
                    Theme::LogColors::system);
            } else {
                // New user dialog
                QString name = QInputDialog::getText(this,
                    IS_EN ? QStringLiteral("New User") : QStringLiteral("Новый пользователь"),
                    IS_EN ? QStringLiteral("Name:") : QStringLiteral("Имя:"),
                    QLineEdit::Normal, QString(), &ok);
                if (!ok || name.trimmed().isEmpty()) return;

                QStringList roles = {
                    QStringLiteral("programmer"), QStringLiteral("artist"),
                    QStringLiteral("game_dev"), QStringLiteral("tester"),
                    QStringLiteral("designer"), QStringLiteral("student"),
                    QStringLiteral("general")
                };
                QString role = QInputDialog::getItem(this,
                    IS_EN ? QStringLiteral("User Role") : QStringLiteral("Роль"),
                    IS_EN ? QStringLiteral("Primary role:") : QStringLiteral("Основная роль:"),
                    roles, 0, false, &ok);
                if (!ok) return;

                DbUserProfile newUser;
                newUser.name     = name.trimmed();
                newUser.scenario = role;
                newUser.language = QStringLiteral("auto");
                newUser.preferences = QStringLiteral("{}");
                qint64 newId = db.addUser(newUser);
                if (newId > 0) {
                    m_jarvis->setCurrentUserId(newId);
                    m_jarvis->memory()->setActiveUserName(newUser.name);
                    QSettings(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"))
                        .setValue(QStringLiteral("user/currentId"), newId);
                    appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                        (IS_EN ? QStringLiteral("Created user: ") : QStringLiteral("Создан пользователь: ")) + newUser.name
                        + QStringLiteral(" (") + role + QStringLiteral(")"),
                        Theme::LogColors::system);
                }
            }
        });

        // Current user info
        auto* actProfile = userMenu->addAction(
            IS_EN ? QStringLiteral("My Profile") : QStringLiteral("Мой профиль"));
        connect(actProfile, &QAction::triggered, this, [this]() {
            auto& db = DatabaseManager::instance();
            auto user = db.getUser(m_jarvis->currentUserId());
            if (!user) return;

            QString role = m_jarvis->activityTracker()->detectUserRole();
            QString knowledge = m_jarvis->activityTracker()->knowledgeSummary(m_jarvis->currentUserId(), 20);
            QString activity = m_jarvis->activityTracker()->recentActivitySummary(60);

            QString info = QStringLiteral("👤 ") + user->name + QStringLiteral("\n")
                + QStringLiteral("Role: ") + user->scenario + QStringLiteral("\n")
                + QStringLiteral("Detected role: ") + role + QStringLiteral("\n")
                + QStringLiteral("Language: ") + user->language + QStringLiteral("\n\n");

            if (!activity.isEmpty())
                info += QStringLiteral("📊 Recent activity (1h):\n") + activity + QStringLiteral("\n");
            if (!knowledge.isEmpty())
                info += QStringLiteral("🧠 Knowledge base:\n") + knowledge;

            QMessageBox box(this);
            box.setWindowTitle(IS_EN ? QStringLiteral("User Profile") : QStringLiteral("Профиль пользователя"));
            box.setText(info);
            box.setStyleSheet(QStringLiteral(
                "QMessageBox { background: #0a0a1a; color: #ecf0f1; }"
                "QLabel { color: #ecf0f1; font-family: Consolas; font-size: 12px; }"
                "QPushButton { background: #0f2438; color: #00d4ff; border: 1px solid #1a5070; "
                "border-radius: 4px; padding: 5px 18px; }"));
            box.exec();
        });

        // Delete user
        auto* actDelete = userMenu->addAction(
            IS_EN ? QStringLiteral("Delete User...") : QStringLiteral("Удалить пользователя..."));
        connect(actDelete, &QAction::triggered, this, [this]() {
            auto& db = DatabaseManager::instance();
            auto users = db.getAllUsers();
            if (users.size() <= 1) {
                QMessageBox::information(this,
                    IS_EN ? QStringLiteral("Delete User") : QStringLiteral("Удаление"),
                    IS_EN ? QStringLiteral("Cannot delete the last user.")
                          : QStringLiteral("Нельзя удалить последнего пользователя."));
                return;
            }

            QStringList items;
            for (const auto& u : users)
                if (u.id != 1) items << QStringLiteral("%1 (id=%2)").arg(u.name).arg(u.id);
            if (items.isEmpty()) return;

            bool ok;
            QString choice = QInputDialog::getItem(this,
                IS_EN ? QStringLiteral("Delete User") : QStringLiteral("Удалить пользователя"),
                IS_EN ? QStringLiteral("Select user to delete:") : QStringLiteral("Выберите:"),
                items, 0, false, &ok);
            if (!ok) return;

            // Extract id from "Name (id=N)"
            static const QRegularExpression reId(QStringLiteral("id=(\\d+)"));
            auto m = reId.match(choice);
            if (m.hasMatch()) {
                qint64 delId = m.captured(1).toLongLong();
                QSqlQuery q(QSqlDatabase::database());
                q.prepare("DELETE FROM users WHERE id=:id");
                q.bindValue(":id", delId);
                q.exec();
                if (m_jarvis->currentUserId() == delId) {
                    m_jarvis->setCurrentUserId(1);
                    m_jarvis->memory()->setActiveUserName(QStringLiteral(""));
                }
                appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                    IS_EN ? QStringLiteral("User deleted.") : QStringLiteral("Пользователь удалён."),
                    Theme::LogColors::system);
            }
        });
    }

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
        int liked     = db.trainingLogCount(1, 1);  // rating>=1 — реально лайкнутые
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
        addRow("👍", IS_EN ? "Liked (priority)" : "Лайкнуто (приоритет)",
               QString::number(liked));
        addRow("🎯", IS_EN ? "Goal (export ready)" : "Цель (готово к экспорту)",
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

    // --- One-click обучение через Ollama ---
    auto* actTrainLocal = trainMenu->addAction(
        IS_EN ? QStringLiteral("🚀 Train Local Model (Ollama)...")
              : QStringLiteral("🚀 Обучить модель локально (Ollama)..."));
    connect(actTrainLocal, &QAction::triggered, this, [this]() {
        auto* trainer = new LocalTrainer(this);

        // Проверяем Ollama перед показом диалога
        if (!trainer->isOllamaAvailable()) {
            QMessageBox::warning(this,
                IS_EN ? QStringLiteral("Ollama Not Found") : QStringLiteral("Ollama не найдена"),
                IS_EN ? QStringLiteral("Ollama is required for local training.\n\n"
                           "Install from: https://ollama.com\n"
                           "Then restart JARVIS.")
                      : QStringLiteral("Для обучения нужна Ollama.\n\n"
                           "Установите: https://ollama.com\n"
                           "Затем перезапустите JARVIS."));
            delete trainer;
            return;
        }

        // Диалог настройки обучения
        QDialog dlg(this);
        dlg.setWindowTitle(IS_EN ? QStringLiteral("JARVIS — Train Personal Model")
                                 : QStringLiteral("JARVIS — Обучение персональной модели"));
        dlg.setMinimumSize(520, 420);
        dlg.setStyleSheet(QStringLiteral(
            "QDialog{background:#0a1018;color:#c8e0f0;}"
            "QLabel{color:#c8e0f0;font-size:12px;}"
            "QComboBox{background:#0f2438;color:#00d4ff;border:1px solid #1a5070;"
            "padding:4px 12px;border-radius:4px;font-size:12px;}"
            "QComboBox:drop-down{border:none;}"
            "QSpinBox{background:#0f2438;color:#00d4ff;border:1px solid #1a5070;"
            "padding:4px;border-radius:4px;}"
            "QTextEdit{background:#0d1a28;color:#95a5a6;border:1px solid #1a3050;"
            "font-family:Consolas,monospace;font-size:11px;border-radius:4px;}"
            "QPushButton{background:#0f2438;color:#00d4ff;border:1px solid #1a5070;"
            "padding:6px 20px;border-radius:4px;font-size:13px;}"
            "QPushButton:hover{background:#1a3a5c;}"
            "QPushButton#trainBtn{background:#00695c;color:#fff;border:1px solid #00897b;font-weight:bold;}"
            "QPushButton#trainBtn:hover{background:#00897b;}"
            "QPushButton#trainBtn:disabled{background:#333;color:#666;}"));

        auto* layout = new QVBoxLayout(&dlg);
        layout->setContentsMargins(20, 16, 20, 16);
        layout->setSpacing(10);

        // Заголовок
        auto* titleLabel = new QLabel(
            IS_EN ? QStringLiteral("<b style='color:#00d4ff;font-size:15px;'>"
                                   "🚀 Train personalized model</b>")
                  : QStringLiteral("<b style='color:#00d4ff;font-size:15px;'>"
                                   "🚀 Обучение персональной модели</b>"), &dlg);
        titleLabel->setTextFormat(Qt::RichText);
        layout->addWidget(titleLabel);

        auto* descLabel = new QLabel(
            IS_EN ? QStringLiteral("Creates a custom Ollama model from your liked responses.\n"
                                   "No internet, GPU, or coding required — just Ollama.")
                  : QStringLiteral("Создаёт персональную модель из лайкнутых ответов.\n"
                                   "Без интернета, GPU и программирования — только Ollama."), &dlg);
        descLabel->setWordWrap(true);
        layout->addWidget(descLabel);

        // Статистика
        auto& db = DatabaseManager::instance();
        int likedCount = db.trainingLogCount(1);

        auto* statsLabel = new QLabel(
            (IS_EN ? QStringLiteral("👍 Liked responses available: <b style='color:#00d4ff;'>%1</b>")
                   : QStringLiteral("👍 Лайкнутых ответов: <b style='color:#00d4ff;'>%1</b>"))
                .arg(likedCount), &dlg);
        statsLabel->setTextFormat(Qt::RichText);
        layout->addWidget(statsLabel);

        // Выбор базовой модели
        auto* modelRow = new QHBoxLayout();
        modelRow->addWidget(new QLabel(
            IS_EN ? QStringLiteral("Base model:") : QStringLiteral("Базовая модель:"), &dlg));
        auto* modelCombo = new QComboBox(&dlg);
        QStringList installed = trainer->installedModels();
        // Добавляем рекомендованные + установленные
        QStringList options = { QStringLiteral("llama3.2:3b"), QStringLiteral("llama3.2:1b"),
                                QStringLiteral("mistral:7b"), QStringLiteral("phi3:mini") };
        for (const QString& m : installed) {
            if (!options.contains(m)) options.prepend(m);
        }
        modelCombo->addItems(options);
        modelCombo->setCurrentText(QStringLiteral("llama3.2:3b"));
        modelRow->addWidget(modelCombo, 1);
        layout->addLayout(modelRow);

        // Макс пар
        auto* pairsRow = new QHBoxLayout();
        pairsRow->addWidget(new QLabel(
            IS_EN ? QStringLiteral("Max examples:") : QStringLiteral("Макс. примеров:"), &dlg));
        auto* pairsSpin = new QSpinBox(&dlg);
        pairsSpin->setRange(10, 500);
        pairsSpin->setValue(80);
        pairsSpin->setToolTip(IS_EN ? QStringLiteral("More = better quality but slower inference")
                                    : QStringLiteral("Больше = лучше качество, но медленнее"));
        pairsRow->addWidget(pairsSpin);
        pairsRow->addStretch();
        layout->addLayout(pairsRow);

        // Лог
        auto* logView = new QTextEdit(&dlg);
        logView->setReadOnly(true);
        logView->setMaximumHeight(140);
        layout->addWidget(logView, 1);

        // Кнопки
        auto* btnRow = new QHBoxLayout();
        auto* trainBtn = new QPushButton(
            IS_EN ? QStringLiteral("🚀 Start Training") : QStringLiteral("🚀 Начать обучение"), &dlg);
        trainBtn->setObjectName(QStringLiteral("trainBtn"));
        trainBtn->setMinimumHeight(38);
        if (likedCount == 0) {
            trainBtn->setEnabled(false);
            trainBtn->setToolTip(IS_EN ? QStringLiteral("Like some responses first (👍 button)")
                                       : QStringLiteral("Сначала лайкните ответы (кнопка 👍)"));
        }
        auto* cancelBtn = new QPushButton(
            IS_EN ? QStringLiteral("Close") : QStringLiteral("Закрыть"), &dlg);
        btnRow->addStretch();
        btnRow->addWidget(cancelBtn);
        btnRow->addWidget(trainBtn);
        layout->addLayout(btnRow);

        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

        connect(trainer, &LocalTrainer::trainingProgress, logView,
                [logView](const QString& msg) {
            logView->append(msg);
            auto* sb = logView->verticalScrollBar();
            if (sb) sb->setValue(sb->maximum());
        });

        connect(trainer, &LocalTrainer::trainingFinished, &dlg,
                [this, trainBtn, cancelBtn, logView, trainer](bool success, const QString& msg) {
            trainBtn->setEnabled(true);
            cancelBtn->setText(IS_EN ? QStringLiteral("Done") : QStringLiteral("Готово"));
            if (success) {
                logView->append(QStringLiteral("\n🎉 ") + msg);
                appendLog(Str::logSystem(),
                    IS_EN ? QStringLiteral("🎉 Personal model trained! Select '%1' in Ollama settings.")
                                .arg(trainer->outputModel())
                          : QStringLiteral("🎉 Модель обучена! Выберите '%1' в настройках Ollama.")
                                .arg(trainer->outputModel()),
                    Theme::LogColors::system);
            } else {
                logView->append(QStringLiteral("\n❌ ") + msg);
            }
        });

        connect(trainBtn, &QPushButton::clicked, &dlg,
                [trainer, trainBtn, modelCombo, pairsSpin]() {
            trainBtn->setEnabled(false);
            trainer->setBaseModel(modelCombo->currentText());
            trainer->setMaxPairs(pairsSpin->value());
            trainer->train(&DatabaseManager::instance());
        });

        dlg.exec();
        delete trainer;
    });

    trainMenu->addSeparator();

    // --- Паттерны использования ПК ---
    auto* actAppPatterns = trainMenu->addAction(
        IS_EN ? QStringLiteral("📊 App Usage Patterns...")
              : QStringLiteral("📊 Паттерны использования..."));
    connect(actAppPatterns, &QAction::triggered, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle(IS_EN ? QStringLiteral("JARVIS — App Usage Patterns")
                                 : QStringLiteral("JARVIS — Паттерны использования"));
        dlg.setMinimumSize(500, 450);
        dlg.setStyleSheet(QStringLiteral(
            "QDialog{background:#0a1018;color:#c8e0f0;}"
            "QLabel{color:#c8e0f0;}"
            "QTextEdit{background:#0d1a28;color:#95a5a6;border:1px solid #1a3050;"
            "font-family:Consolas,monospace;font-size:12px;border-radius:4px;}"
            "QPushButton{background:#0f2438;color:#00d4ff;border:1px solid #1a5070;"
            "padding:6px 16px;border-radius:4px;}"
            "QPushButton:hover{background:#1a3a5c;}"
            "QCheckBox{color:#c8e0f0;}"));

        auto* layout = new QVBoxLayout(&dlg);
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setSpacing(10);

        auto* title = new QLabel(
            IS_EN ? QStringLiteral("<b style='color:#00d4ff;font-size:14px;'>"
                                   "📊 App Usage Patterns</b>")
                  : QStringLiteral("<b style='color:#00d4ff;font-size:14px;'>"
                                   "📊 Паттерны использования ПК</b>"), &dlg);
        title->setTextFormat(Qt::RichText);
        layout->addWidget(title);

        int total = m_appLearner ? m_appLearner->totalRecords() : 0;
        auto* countLabel = new QLabel(
            (IS_EN ? QStringLiteral("Total records: <b>%1</b>")
                   : QStringLiteral("Всего записей: <b>%1</b>")).arg(total), &dlg);
        countLabel->setTextFormat(Qt::RichText);
        layout->addWidget(countLabel);

        // Предложения на сейчас
        auto* sugTitle = new QLabel(
            IS_EN ? QStringLiteral("<b style='color:#2ecc71;'>Predictions for now:</b>")
                  : QStringLiteral("<b style='color:#2ecc71;'>Предсказания на сейчас:</b>"), &dlg);
        sugTitle->setTextFormat(Qt::RichText);
        layout->addWidget(sugTitle);

        auto suggestions = m_appLearner ? m_appLearner->suggestionsForNow(5) : QVector<AppSuggestion>{};
        if (suggestions.isEmpty()) {
            layout->addWidget(new QLabel(
                IS_EN ? QStringLiteral("Not enough data yet. Keep using your PC!")
                      : QStringLiteral("Ещё мало данных. Продолжайте использовать ПК!"), &dlg));
        } else {
            for (const auto& s : suggestions) {
                QString line = QStringLiteral("  %1  — %2x  (%3%)")
                    .arg(s.appName)
                    .arg(s.frequency)
                    .arg(static_cast<int>(s.confidence * 100));
                auto* lbl = new QLabel(line, &dlg);
                lbl->setStyleSheet(s.confidence >= 0.5f
                    ? QStringLiteral("color:#2ecc71;font-size:13px;")
                    : QStringLiteral("color:#7f8c8d;font-size:12px;"));
                layout->addWidget(lbl);
            }
        }

        // Статистика за сегодня
        layout->addSpacing(8);
        auto* todayTitle = new QLabel(
            IS_EN ? QStringLiteral("<b style='color:#3498db;'>Today's usage:</b>")
                  : QStringLiteral("<b style='color:#3498db;'>Сегодня:</b>"), &dlg);
        todayTitle->setTextFormat(Qt::RichText);
        layout->addWidget(todayTitle);

        auto todayData = m_appLearner ? m_appLearner->todayStats() : QVector<AppUsageStat>{};
        auto* statsView = new QTextEdit(&dlg);
        statsView->setReadOnly(true);
        statsView->setMaximumHeight(160);
        if (todayData.isEmpty()) {
            statsView->setText(IS_EN ? QStringLiteral("No data for today")
                                     : QStringLiteral("Нет данных за сегодня"));
        } else {
            QString html;
            for (const auto& st : todayData) {
                html += QStringLiteral("<b>%1</b> — %2 min (%3 sessions)<br>")
                    .arg(st.appName).arg(st.totalMinutes).arg(st.sessionCount);
            }
            statsView->setHtml(html);
        }
        layout->addWidget(statsView);

        // Кнопки
        auto* btnRow = new QHBoxLayout();

        auto* enableCheck = new QCheckBox(
            IS_EN ? QStringLiteral("Enable learning")
                  : QStringLiteral("Включить обучение"), &dlg);
        enableCheck->setChecked(m_appLearner && m_appLearner->isEnabled());
        connect(enableCheck, &QCheckBox::toggled, this, [this](bool on) {
            if (m_appLearner) {
                m_appLearner->setEnabled(on);
                if (on && !m_appLearner->isRunning()) m_appLearner->start(2);
            }
        });
        btnRow->addWidget(enableCheck);

        btnRow->addStretch();

        auto* clearBtn = new QPushButton(
            IS_EN ? QStringLiteral("Clear data") : QStringLiteral("Очистить"), &dlg);
        connect(clearBtn, &QPushButton::clicked, this, [this, countLabel, statsView]() {
            if (m_appLearner) {
                m_appLearner->clearAllData();
                countLabel->setText(QStringLiteral("Total: <b>0</b>"));
                statsView->setText(QStringLiteral("Cleared"));
            }
        });
        btnRow->addWidget(clearBtn);

        auto* closeBtn = new QPushButton(
            IS_EN ? QStringLiteral("Close") : QStringLiteral("Закрыть"), &dlg);
        connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        btnRow->addWidget(closeBtn);

        layout->addLayout(btnRow);
        dlg.exec();
    });

    trainMenu->addSeparator();
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
            JarvisPaths::subPath(QStringLiteral("voice_dataset"))).toString();

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

    // --- Task Manager ---
    {
        auto* taskMenu = menuBar->addMenu(
            IS_EN ? QStringLiteral("📋 Tasks") : QStringLiteral("📋 Задачи"));

        auto* actOpenBoard = taskMenu->addAction(
            IS_EN ? QStringLiteral("Open Task Board...")
                  : QStringLiteral("Открыть доску задач..."));
        connect(actOpenBoard, &QAction::triggered, this, [this]() {
            TaskManagerDialog dlg(m_jarvis->currentUserId(), this);
            connect(&dlg, &TaskManagerDialog::taskChanged, this, [this]() {
                appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                    IS_EN ? QStringLiteral("Task board updated.")
                          : QStringLiteral("Доска задач обновлена."),
                    Theme::LogColors::system);
            });
            dlg.exec();
        });

        auto* actQuickAdd = taskMenu->addAction(
            IS_EN ? QStringLiteral("Quick Add Task...")
                  : QStringLiteral("Быстро добавить задачу..."));
        connect(actQuickAdd, &QAction::triggered, this, [this]() {
            bool ok;
            QString title = QInputDialog::getText(this,
                IS_EN ? QStringLiteral("New Task") : QStringLiteral("Новая задача"),
                IS_EN ? QStringLiteral("Task title:") : QStringLiteral("Название задачи:"),
                QLineEdit::Normal, QString(), &ok);
            if (!ok || title.trimmed().isEmpty()) return;
            qint64 id = m_jarvis->addTask(title.trimmed());
            if (id > 0) {
                appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                    (IS_EN ? QStringLiteral("Task created: ") : QStringLiteral("Задача создана: ")) + title.trimmed(),
                    Theme::LogColors::system);
            }
        });

        taskMenu->addSeparator();

        auto* actDeadlines = taskMenu->addAction(
            IS_EN ? QStringLiteral("Check Deadlines")
                  : QStringLiteral("Проверить дедлайны"));
        connect(actDeadlines, &QAction::triggered, this, [this]() {
            QString warnings = m_jarvis->getOverdueTasksSummary();
            if (warnings.isEmpty()) {
                appendLog(IS_EN ? QStringLiteral("J.A.R.V.I.S.") : QStringLiteral("Д.Ж.А.Р.В.И.С."),
                    IS_EN ? QStringLiteral("All clear, sir. No approaching deadlines.")
                          : QStringLiteral("Всё чисто, сэр. Дедлайнов в ближайшее время нет."),
                    Theme::LogColors::jarvis);
            } else {
                appendLog(IS_EN ? QStringLiteral("J.A.R.V.I.S.") : QStringLiteral("Д.Ж.А.Р.В.И.С."),
                    warnings, Theme::LogColors::error);
            }
        });
    }

    // --- Translation & Role ---
    {
        auto* toolsMenu = menuBar->addMenu(
            IS_EN ? QStringLiteral("🌐 Tools") : QStringLiteral("🌐 Инструменты"));

        // Translation pair toggle
        auto* transMenu = toolsMenu->addMenu(
            IS_EN ? QStringLiteral("Translation Pair") : QStringLiteral("Пара перевода"));

        struct LangPair { QString label; QString src; QString tgt; };
        const QList<LangPair> pairs = {
            {QStringLiteral("FR → EN"), QStringLiteral("fr"), QStringLiteral("en")},
            {QStringLiteral("FR → RU"), QStringLiteral("fr"), QStringLiteral("ru")},
            {QStringLiteral("EN → RU"), QStringLiteral("en"), QStringLiteral("ru")},
            {QStringLiteral("RU → EN"), QStringLiteral("ru"), QStringLiteral("en")},
            {QStringLiteral("EN → FR"), QStringLiteral("en"), QStringLiteral("fr")},
            {QStringLiteral("RU → FR"), QStringLiteral("ru"), QStringLiteral("fr")},
        };
        auto* langGroup = new QActionGroup(transMenu);
        langGroup->setExclusive(true);
        for (const auto& p : pairs) {
            auto* act = transMenu->addAction(p.label);
            act->setCheckable(true);
            if (p.src == QStringLiteral("fr") && p.tgt == QStringLiteral("en"))
                act->setChecked(true);
            langGroup->addAction(act);
            const QString src = p.src, tgt = p.tgt;
            connect(act, &QAction::triggered, this, [this, src, tgt]() {
                m_jarvis->translationEngine()->setSourceLang(src);
                m_jarvis->translationEngine()->setTargetLang(tgt);
                appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                    QStringLiteral("Translation pair: %1 → %2").arg(src.toUpper(), tgt.toUpper()),
                    Theme::LogColors::system);
            });
        }

        toolsMenu->addSeparator();

        // Quick translate clipboard
        auto* actTranslate = toolsMenu->addAction(
            IS_EN ? QStringLiteral("Translate Clipboard")
                  : QStringLiteral("Перевести буфер обмена"));
        connect(actTranslate, &QAction::triggered, this, [this]() {
            QString text = QApplication::clipboard()->text().trimmed();
            if (text.isEmpty()) {
                appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                    IS_EN ? QStringLiteral("Clipboard is empty.") : QStringLiteral("Буфер обмена пуст."),
                    Theme::LogColors::error);
                return;
            }
            appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                IS_EN ? QStringLiteral("Translating...") : QStringLiteral("Перевожу..."),
                Theme::LogColors::system);
            m_jarvis->translationEngine()->translateText(text,
                m_jarvis->translationEngine()->targetLang(),
                [this](bool ok, const QString& result) {
                    if (ok) {
                        appendLog(QStringLiteral("J.A.R.V.I.S."), result, Theme::LogColors::jarvis);
                        QApplication::clipboard()->setText(result);
                    } else {
                        appendLog(IS_EN ? QStringLiteral("Error") : QStringLiteral("Ошибка"),
                            result, Theme::LogColors::error);
                    }
                });
        });

        // Process audio file
        auto* actAudioTranslate = toolsMenu->addAction(
            IS_EN ? QStringLiteral("Process Audio File...")
                  : QStringLiteral("Обработать аудиофайл..."));
        connect(actAudioTranslate, &QAction::triggered, this, [this]() {
            QString path = QFileDialog::getOpenFileName(this,
                IS_EN ? QStringLiteral("Select Audio File") : QStringLiteral("Выберите аудиофайл"),
                QDir::homePath(),
                QStringLiteral("Audio (*.wav *.pcm *.raw);;All (*)"));
            if (path.isEmpty()) return;

            appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                (IS_EN ? QStringLiteral("Processing audio: ") : QStringLiteral("Обработка аудио: "))
                + QFileInfo(path).fileName(),
                Theme::LogColors::system);

            auto* te = m_jarvis->translationEngine();
            connect(te, &TranslationEngine::audioProcessingProgress, this,
                    [this](const QString& stage) {
                appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                    stage, Theme::LogColors::system);
            }, Qt::SingleShotConnection);
            connect(te, &TranslationEngine::audioTranscribed, this,
                    [this](const QString& transcript, const QString& lang) {
                appendLog(QStringLiteral("J.A.R.V.I.S."),
                    QStringLiteral("[Transcript %1]: %2").arg(lang.toUpper(), transcript.left(500)),
                    Theme::LogColors::jarvis);
            }, Qt::SingleShotConnection);
            connect(te, &TranslationEngine::audioSummaryReady, this,
                    [this](const QString& summary) {
                appendLog(QStringLiteral("J.A.R.V.I.S."), summary, Theme::LogColors::jarvis);
            }, Qt::SingleShotConnection);
            connect(te, &TranslationEngine::audioProcessingError, this,
                    [this](const QString& err) {
                appendLog(IS_EN ? QStringLiteral("Error") : QStringLiteral("Ошибка"),
                    err, Theme::LogColors::error);
            }, Qt::SingleShotConnection);

            te->processAudioFile(path, te->targetLang());
        });

        toolsMenu->addSeparator();

        // Role switcher
        auto* roleMenu = toolsMenu->addMenu(
            IS_EN ? QStringLiteral("Switch Role") : QStringLiteral("Сменить роль"));
        struct RoleEntry { QString label; QString role; };
        const QList<RoleEntry> roles = {
            {IS_EN ? QStringLiteral("Developer") : QStringLiteral("Разработчик"),
             QStringLiteral("Developer")},
            {IS_EN ? QStringLiteral("QA Tester") : QStringLiteral("QA Тестировщик"),
             QStringLiteral("QA_Tester")},
            {IS_EN ? QStringLiteral("Student / Academic") : QStringLiteral("Студент / Академия"),
             QStringLiteral("Student_Academic")},
        };
        auto* roleGroup = new QActionGroup(roleMenu);
        roleGroup->setExclusive(true);
        for (const auto& r : roles) {
            auto* act = roleMenu->addAction(r.label);
            act->setCheckable(true);
            if (r.role == QStringLiteral("Developer")) act->setChecked(true);
            roleGroup->addAction(act);
            const QString roleName = r.role;
            connect(act, &QAction::triggered, this, [this, roleName]() {
                auto& db = DatabaseManager::instance();
                auto user = db.getUser(m_jarvis->currentUserId());
                if (user) {
                    user->currentRole = roleName;
                    db.updateUser(*user);
                }
                appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                    (IS_EN ? QStringLiteral("Role switched to: ") : QStringLiteral("Роль: ")) + roleName,
                    Theme::LogColors::system);
            });
        }

        // --- Mobile Sync (zero-config pairing) ---
        toolsMenu->addSeparator();

        auto* actMobileSync = toolsMenu->addAction(
            IS_EN ? QStringLiteral("📱 Mobile Sync...")
                  : QStringLiteral("📱 Мобильная синхронизация..."));
        connect(actMobileSync, &QAction::triggered, this, [this]() {
            auto* mesh = m_jarvis->meshConnector();
            if (!mesh) return;
            mesh->initMobilePairing();
            auto* pairing = mesh->mobilePairing();
            if (!pairing) return;

            // Generate a fresh PIN
            PairingSession session = pairing->generatePairingPin(
                QStringLiteral("Developer"));
            pairing->startGatewayPolling();

            QString deepLink = pairing->buildDeepLinkUri(session);

            // Build the cyberpunk-themed pairing dialog
            auto* dlg = new QDialog(this);
            dlg->setWindowTitle(IS_EN ? QStringLiteral("Mobile Sync — Zero Config Pairing")
                                      : QStringLiteral("Мобильная синхронизация — Без настройки"));
            dlg->setMinimumSize(520, 480);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->setStyleSheet(QStringLiteral(
                "QDialog { background: rgba(8,10,18,245); }"
                "QLabel { color: #c0c8d8; font-size: 13px; }"
                "QComboBox { background: rgba(14,18,30,180); color: #e0e8f0; "
                "  border: 1px solid rgba(0,212,255,50); border-radius: 6px; padding: 6px; }"
                "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                "  stop:0 #00d4ff, stop:1 #7c4dff); color: white; font-weight: bold;"
                "  border: none; border-radius: 8px; padding: 10px 24px; font-size: 13px; }"
                "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                "  stop:0 #33e0ff, stop:1 #9b6dff); }"
                "QTextEdit { background: rgba(14,18,30,180); color: #66FCF1;"
                "  border: 1px solid rgba(0,212,255,35); border-radius: 8px;"
                "  font-family: 'Consolas'; font-size: 12px; padding: 8px; }"));

            auto* layout = new QVBoxLayout(dlg);
            layout->setSpacing(12);
            layout->setContentsMargins(24, 20, 24, 20);

            // Title
            auto* title = new QLabel(IS_EN ? QStringLiteral("📱 MOBILE SYNC")
                                           : QStringLiteral("📱 МОБИЛЬНАЯ СИНХРОНИЗАЦИЯ"));
            title->setStyleSheet(QStringLiteral(
                "color: #00d4ff; font-size: 20px; font-weight: bold; letter-spacing: 3px;"));
            title->setAlignment(Qt::AlignCenter);
            layout->addWidget(title);

            auto* subtitle = new QLabel(IS_EN
                ? QStringLiteral("Pair your phone — no bots, no tokens, no setup")
                : QStringLiteral("Подключите телефон — без ботов, токенов и настройки"));
            subtitle->setStyleSheet(QStringLiteral(
                "color: rgba(0,212,255,150); font-size: 12px;"));
            subtitle->setAlignment(Qt::AlignCenter);
            layout->addWidget(subtitle);

            // Separator
            auto* sep1 = new QFrame(dlg);
            sep1->setFrameShape(QFrame::HLine);
            sep1->setStyleSheet(QStringLiteral(
                "background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
                "stop:0 transparent, stop:0.2 #00d4ff, stop:0.8 #7c4dff, stop:1 transparent);"
                "max-height: 1px;"));
            layout->addWidget(sep1);

            // PIN display
            auto* pinLabel = new QLabel(IS_EN ? QStringLiteral("YOUR PAIRING PIN:")
                                              : QStringLiteral("ВАШ PIN-КОД:"));
            pinLabel->setAlignment(Qt::AlignCenter);
            pinLabel->setStyleSheet(QStringLiteral("color: #8892a4; font-size: 11px; letter-spacing: 2px;"));
            layout->addWidget(pinLabel);

            auto* pinDisplay = new QLabel(session.pin);
            pinDisplay->setAlignment(Qt::AlignCenter);
            pinDisplay->setTextInteractionFlags(Qt::TextSelectableByMouse);
            pinDisplay->setStyleSheet(QStringLiteral(
                "color: #00d4ff; font-size: 42px; font-weight: bold; font-family: 'Consolas';"
                "letter-spacing: 12px; padding: 16px;"
                "background: rgba(0,212,255,8); border: 2px solid rgba(0,212,255,60);"
                "border-radius: 12px;"));
            layout->addWidget(pinDisplay);

            // Timer countdown
            auto* timerLabel = new QLabel();
            timerLabel->setAlignment(Qt::AlignCenter);
            timerLabel->setStyleSheet(QStringLiteral("color: #ff6b6b; font-size: 11px;"));
            layout->addWidget(timerLabel);

            auto* countdownTimer = new QTimer(dlg);
            countdownTimer->setInterval(1000);
            QDateTime expires = session.expiresAt;
            connect(countdownTimer, &QTimer::timeout, dlg, [timerLabel, expires]() {
                int remaining = static_cast<int>(QDateTime::currentDateTimeUtc().secsTo(expires));
                if (remaining <= 0) {
                    timerLabel->setText(IS_EN ? QStringLiteral("⚠ PIN expired — generate a new one")
                                             : QStringLiteral("⚠ PIN истёк — сгенерируйте новый"));
                } else {
                    timerLabel->setText(
                        (IS_EN ? QStringLiteral("Expires in %1:%2")
                               : QStringLiteral("Истекает через %1:%2"))
                        .arg(remaining / 60, 2, 10, QLatin1Char('0'))
                        .arg(remaining % 60, 2, 10, QLatin1Char('0')));
                }
            });
            countdownTimer->start();
            // Trigger immediately via manual invoke
            QMetaObject::invokeMethod(countdownTimer, "timeout", Qt::QueuedConnection);

            // Deep link
            auto* linkLabel = new QLabel(
                QStringLiteral("<span style='color:#8892a4;'>%1</span><br>"
                               "<a href='%2' style='color:#7c4dff;'>%2</a>")
                .arg(IS_EN ? QStringLiteral("Deep Link:")
                           : QStringLiteral("Ссылка:"),
                     deepLink));
            linkLabel->setAlignment(Qt::AlignCenter);
            linkLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
            linkLabel->setOpenExternalLinks(false);
            layout->addWidget(linkLabel);

            // Role selector
            auto* roleRow = new QHBoxLayout();
            auto* roleLbl = new QLabel(IS_EN ? QStringLiteral("Bind to role:")
                                             : QStringLiteral("Привязать к роли:"));
            auto* roleCombo = new QComboBox(dlg);
            roleCombo->addItem(QStringLiteral("Developer"));
            roleCombo->addItem(QStringLiteral("QA_Tester"));
            roleCombo->addItem(QStringLiteral("Student_Academic"));
            roleCombo->addItem(QStringLiteral("Creative"));
            roleCombo->addItem(QStringLiteral("Hardware"));
            roleRow->addWidget(roleLbl);
            roleRow->addWidget(roleCombo, 1);
            layout->addLayout(roleRow);

            // Regenerate PIN button
            auto* regenBtn = new QPushButton(
                IS_EN ? QStringLiteral("🔄 Generate New PIN")
                      : QStringLiteral("🔄 Новый PIN"));
            connect(regenBtn, &QPushButton::clicked, dlg,
                    [this, pairing, roleCombo, pinDisplay, countdownTimer, timerLabel, dlg]() {
                QString role = roleCombo->currentText();
                PairingSession newSession = pairing->generatePairingPin(role);
                pairing->startGatewayPolling();
                pinDisplay->setText(newSession.pin);
                QDateTime exp = newSession.expiresAt;
                disconnect(countdownTimer, &QTimer::timeout, nullptr, nullptr);
                connect(countdownTimer, &QTimer::timeout, dlg, [timerLabel, exp]() {
                    int rem = static_cast<int>(QDateTime::currentDateTimeUtc().secsTo(exp));
                    if (rem <= 0)
                        timerLabel->setText(IS_EN ? QStringLiteral("⚠ PIN expired")
                                                  : QStringLiteral("⚠ PIN истёк"));
                    else
                        timerLabel->setText(
                            (IS_EN ? QStringLiteral("Expires in %1:%2")
                                   : QStringLiteral("Истекает через %1:%2"))
                            .arg(rem / 60, 2, 10, QLatin1Char('0'))
                            .arg(rem % 60, 2, 10, QLatin1Char('0')));
                });
                appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                    QStringLiteral("New PIN: %1 → role: %2").arg(newSession.pin, role),
                    Theme::LogColors::system);
            });
            layout->addWidget(regenBtn);

            // Separator
            auto* sep2 = new QFrame(dlg);
            sep2->setFrameShape(QFrame::HLine);
            sep2->setStyleSheet(sep1->styleSheet());
            layout->addWidget(sep2);

            // Paired devices list
            auto* devicesLabel = new QLabel(IS_EN ? QStringLiteral("PAIRED DEVICES:")
                                                  : QStringLiteral("ПОДКЛЮЧЁННЫЕ УСТРОЙСТВА:"));
            devicesLabel->setStyleSheet(QStringLiteral(
                "color: #8892a4; font-size: 11px; letter-spacing: 2px;"));
            layout->addWidget(devicesLabel);

            auto* devicesList = new QTextEdit(dlg);
            devicesList->setReadOnly(true);
            devicesList->setMaximumHeight(100);
            auto devices = pairing->pairedDevices();
            if (devices.isEmpty()) {
                devicesList->setPlainText(IS_EN ? QStringLiteral("No devices paired yet.")
                                                : QStringLiteral("Нет подключённых устройств."));
            } else {
                QString devText;
                for (const auto& d : devices) {
                    devText += QStringLiteral("• %1 [%2] → %3  (%4)\n")
                        .arg(d.displayName, d.platform, d.boundRole,
                             d.pairedAt.toString(QStringLiteral("yyyy-MM-dd")));
                }
                devicesList->setPlainText(devText);
            }
            layout->addWidget(devicesList);

            // Pairing success handler
            connect(pairing, &MobilePairingManager::devicePaired, dlg,
                    [this, devicesList, dlg](const QString& name, const QString& role) {
                devicesList->append(QStringLiteral("✓ %1 → %2  (just now)").arg(name, role));
                appendLog(QStringLiteral("J.A.R.V.I.S."),
                    (IS_EN ? QStringLiteral("📱 Device paired: %1 → role: %2")
                           : QStringLiteral("📱 Устройство подключено: %1 → роль: %2"))
                    .arg(name, role),
                    QStringLiteral("#66FCF1"));
            });

            // Cleanup on close
            connect(dlg, &QDialog::finished, this, [pairing]() {
                pairing->stopGatewayPolling();
            });

            dlg->show();
        });

        // Wake-on-LAN Shortcut Generator
        auto* actWol = toolsMenu->addAction(
            IS_EN ? QStringLiteral("⚡ Generate WoL Shortcut")
                  : QStringLiteral("⚡ Сгенерировать WoL ярлык"));
        connect(actWol, &QAction::triggered, this, [this]() {
            auto* mesh = m_jarvis->meshConnector();
            if (!mesh) return;
            mesh->initMobilePairing();
            auto* pairing = mesh->mobilePairing();
            if (!pairing) return;

            auto interfaces = pairing->discoverNetworkInterfaces();
            if (interfaces.isEmpty()) {
                appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                    IS_EN ? QStringLiteral("No active network interfaces found for Wake-on-LAN.")
                          : QStringLiteral("Активные сетевые интерфейсы для WoL не найдены."),
                    Theme::LogColors::error);
                return;
            }

            // Build WoL dialog
            auto* dlg = new QDialog(this);
            dlg->setWindowTitle(IS_EN ? QStringLiteral("Wake-on-LAN Shortcut Generator")
                                      : QStringLiteral("Генератор WoL ярлыков"));
            dlg->setMinimumSize(560, 440);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->setStyleSheet(QStringLiteral(
                "QDialog { background: rgba(8,10,18,245); }"
                "QLabel { color: #c0c8d8; font-size: 13px; }"
                "QComboBox { background: rgba(14,18,30,180); color: #e0e8f0; "
                "  border: 1px solid rgba(0,212,255,50); border-radius: 6px; padding: 6px; }"
                "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                "  stop:0 #00d4ff, stop:1 #7c4dff); color: white; font-weight: bold;"
                "  border: none; border-radius: 8px; padding: 10px 24px; font-size: 13px; }"
                "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                "  stop:0 #33e0ff, stop:1 #9b6dff); }"
                "QTextEdit { background: rgba(14,18,30,180); color: #66FCF1;"
                "  border: 1px solid rgba(0,212,255,35); border-radius: 8px;"
                "  font-family: 'Consolas'; font-size: 12px; padding: 8px; }"));

            auto* layout = new QVBoxLayout(dlg);
            layout->setSpacing(12);
            layout->setContentsMargins(24, 20, 24, 20);

            auto* wolTitle = new QLabel(IS_EN ? QStringLiteral("⚡ WAKE-ON-LAN EXPORTER")
                                              : QStringLiteral("⚡ ЭКСПОРТ WAKE-ON-LAN"));
            wolTitle->setStyleSheet(QStringLiteral(
                "color: #00d4ff; font-size: 20px; font-weight: bold; letter-spacing: 3px;"));
            wolTitle->setAlignment(Qt::AlignCenter);
            layout->addWidget(wolTitle);

            auto* wolSub = new QLabel(IS_EN
                ? QStringLiteral("One-click WoL shortcuts for iOS & Android")
                : QStringLiteral("WoL ярлыки для iOS и Android в один клик"));
            wolSub->setStyleSheet(QStringLiteral("color: rgba(0,212,255,150); font-size: 12px;"));
            wolSub->setAlignment(Qt::AlignCenter);
            layout->addWidget(wolSub);

            // Interface selector
            auto* ifaceRow = new QHBoxLayout();
            auto* ifaceLbl = new QLabel(IS_EN ? QStringLiteral("Network Interface:")
                                              : QStringLiteral("Сетевой интерфейс:"));
            auto* ifaceCombo = new QComboBox(dlg);
            for (const auto& iface : interfaces) {
                ifaceCombo->addItem(QStringLiteral("%1  [%2] — %3")
                    .arg(iface.interfaceName, iface.macAddress, iface.ipv4Address));
            }
            ifaceRow->addWidget(ifaceLbl);
            ifaceRow->addWidget(ifaceCombo, 1);
            layout->addLayout(ifaceRow);

            // Output text area
            auto* output = new QTextEdit(dlg);
            output->setReadOnly(true);

            // Generate button
            auto* genBtn = new QPushButton(
                IS_EN ? QStringLiteral("⚡ Generate iOS/Android Shortcut")
                      : QStringLiteral("⚡ Сгенерировать ярлык iOS/Android"));
            connect(genBtn, &QPushButton::clicked, dlg,
                    [this, pairing, ifaceCombo, output, interfaces]() {
                int idx = ifaceCombo->currentIndex();
                if (idx < 0 || idx >= interfaces.size()) return;

                const auto& iface = interfaces[idx];
                QString report = pairing->exportAllWolProfiles();
                output->setPlainText(report);

                appendLog(QStringLiteral("J.A.R.V.I.S."),
                    (IS_EN ? QStringLiteral("⚡ WoL profile generated for %1 [%2]")
                           : QStringLiteral("⚡ WoL профиль для %1 [%2]"))
                    .arg(iface.interfaceName, iface.macAddress),
                    QStringLiteral("#66FCF1"));
            });
            layout->addWidget(genBtn);
            layout->addWidget(output);

            // Copy to clipboard
            auto* copyBtn = new QPushButton(
                IS_EN ? QStringLiteral("📋 Copy to Clipboard")
                      : QStringLiteral("📋 Скопировать"));
            connect(copyBtn, &QPushButton::clicked, dlg, [output]() {
                QApplication::clipboard()->setText(output->toPlainText());
            });
            layout->addWidget(copyBtn);

            dlg->show();
        });

        // Telegram QA Gateway toggle
        toolsMenu->addSeparator();
        auto* actTelegram = toolsMenu->addAction(
            IS_EN ? QStringLiteral("🤖 Telegram QA Gateway...")
                  : QStringLiteral("🤖 Telegram QA Шлюз..."));
        connect(actTelegram, &QAction::triggered, this, [this]() {
            auto* mesh = m_jarvis->meshConnector();
            if (!mesh) return;
            mesh->initTelegramGateway();
            auto* gw = mesh->telegramGateway();
            if (!gw) return;

            auto* dlg = new QDialog(this);
            dlg->setWindowTitle(IS_EN ? QStringLiteral("Telegram QA Gateway")
                                      : QStringLiteral("Telegram QA Шлюз"));
            dlg->setMinimumSize(480, 320);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->setStyleSheet(QStringLiteral(
                "QDialog { background: rgba(8,10,18,245); }"
                "QLabel { color: #c0c8d8; font-size: 13px; }"
                "QLineEdit { background: rgba(14,18,30,180); color: #e0e8f0; "
                "  border: 1px solid rgba(0,212,255,50); border-radius: 6px; padding: 8px; }"
                "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                "  stop:0 #00d4ff, stop:1 #7c4dff); color: white; font-weight: bold;"
                "  border: none; border-radius: 8px; padding: 10px 24px; font-size: 13px; }"
                "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                "  stop:0 #33e0ff, stop:1 #9b6dff); }"));

            auto* layout = new QVBoxLayout(dlg);
            layout->setSpacing(12);
            layout->setContentsMargins(24, 20, 24, 20);

            auto* title = new QLabel(IS_EN ? QStringLiteral("🤖 TELEGRAM QA GATEWAY")
                                           : QStringLiteral("🤖 TELEGRAM QA ШЛЮЗ"));
            title->setStyleSheet(QStringLiteral(
                "color: #00d4ff; font-size: 20px; font-weight: bold; letter-spacing: 3px;"));
            title->setAlignment(Qt::AlignCenter);
            layout->addWidget(title);

            auto* desc = new QLabel(IS_EN
                ? QStringLiteral("QA_Tester devices get full English UI.\n"
                                 "Main user devices default to Russian.")
                : QStringLiteral("QA_Tester устройства получают английский интерфейс.\n"
                                 "Основной пользователь — русский."));
            desc->setAlignment(Qt::AlignCenter);
            desc->setStyleSheet(QStringLiteral("color: rgba(0,212,255,150); font-size: 12px;"));
            layout->addWidget(desc);

            // Token input
            auto* tokenLbl = new QLabel(IS_EN ? QStringLiteral("Bot Token (auto-provisioned or manual):")
                                              : QStringLiteral("Токен бота (авто или вручную):"));
            layout->addWidget(tokenLbl);

            auto* tokenInput = new QLineEdit(dlg);
            tokenInput->setPlaceholderText(QStringLiteral("123456:ABC-DEF..."));
            tokenInput->setText(gw->botToken());
            tokenInput->setEchoMode(QLineEdit::Password);
            layout->addWidget(tokenInput);

            // Status indicator
            auto* statusLbl = new QLabel();
            statusLbl->setAlignment(Qt::AlignCenter);
            auto updateStatus = [gw, statusLbl]() {
                if (gw->isRunning()) {
                    statusLbl->setText(QStringLiteral("🟢 Gateway ACTIVE — polling Telegram"));
                    statusLbl->setStyleSheet(QStringLiteral("color: #66FCF1; font-weight: bold;"));
                } else {
                    statusLbl->setText(QStringLiteral("🔴 Gateway STOPPED"));
                    statusLbl->setStyleSheet(QStringLiteral("color: #ff6b6b; font-weight: bold;"));
                }
            };
            updateStatus();
            layout->addWidget(statusLbl);

            // Start/Stop button
            auto* toggleBtn = new QPushButton(
                gw->isRunning()
                    ? (IS_EN ? QStringLiteral("⏹ Stop Gateway") : QStringLiteral("⏹ Остановить"))
                    : (IS_EN ? QStringLiteral("▶ Start Gateway") : QStringLiteral("▶ Запустить")));
            connect(toggleBtn, &QPushButton::clicked, dlg,
                    [this, gw, tokenInput, toggleBtn, updateStatus]() {
                if (gw->isRunning()) {
                    gw->stop();
                    toggleBtn->setText(IS_EN ? QStringLiteral("▶ Start Gateway")
                                            : QStringLiteral("▶ Запустить"));
                } else {
                    QString token = tokenInput->text().trimmed();
                    if (!token.isEmpty())
                        gw->setBotToken(token);
                    gw->start();
                    toggleBtn->setText(IS_EN ? QStringLiteral("⏹ Stop Gateway")
                                            : QStringLiteral("⏹ Остановить"));
                    appendLog(QStringLiteral("J.A.R.V.I.S."),
                        IS_EN ? QStringLiteral("🤖 Telegram QA Gateway started")
                              : QStringLiteral("🤖 Telegram QA Шлюз запущен"),
                        QStringLiteral("#66FCF1"));
                }
                updateStatus();
            });
            layout->addWidget(toggleBtn);

            // Bug report notifications
            connect(gw, &J2JTelegramGateway::bugReportFiled, dlg,
                    [this](const QaBugReport& report) {
                appendLog(QStringLiteral("J.A.R.V.I.S."),
                    QStringLiteral("🐛 QA Bug: [%1] %2 — %3")
                        .arg(report.severity, report.title, report.reporterRole),
                    QStringLiteral("#ff9800"));
            });

            layout->addStretch(1);
            dlg->show();
        });
    }

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
R"w(<h3>🔒 J.A.R.V.I.S. Privacy Policy</h3>
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

<p><i>Last updated: when we remembered to update it.</i></p>)w")
        : QStringLiteral(
R"w(<h3>🔒 Политика конфиденциальности J.A.R.V.I.S.</h3>
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

<p><i>Последнее обновление: когда мы вспомнили его обновить.</i></p>)w");

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
R"w(<b>🎤 Voice Input (Vosk — offline)</b><br>
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
• 30+ sites mapped automatically)w")
        : QStringLiteral(
            R"w(<b>🎤 Голосовой ввод (Vosk — офлайн)</b><br>
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
• 30+ сайтов в маппинге автоматически)w");

        // Правая колонка
        const QString col2 = IS_EN ? QStringLiteral(
R"w(<b>🎙️ Passive Voice Recording</b><br>
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
• Most Vexing Parse in QNetworkRequest)w")
        : QStringLiteral(
R"w(<b>🎙️ Пассивная запись голоса</b><br>
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
• Most Vexing Parse в QNetworkRequest)w");

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
            "QDialog { background: #0B0C10; color: #C5C6C7; }"
            "QLabel { background: transparent; }"
            "QListWidget { background: rgba(11,12,16,240); color: #C5C6C7; "
            "  border: 1px solid rgba(102,252,241,0.12); border-radius: 8px; "
            "  font-family: 'Segoe UI', sans-serif; font-size: 13px; outline: none; }"
            "QListWidget::item { padding: 11px 16px; border-bottom: 1px solid rgba(102,252,241,0.06); }"
            "QListWidget::item:selected { background: rgba(102,252,241,0.10); color: #66FCF1; "
            "  border-left: 3px solid #66FCF1; }"
            "QListWidget::item:hover { background: rgba(102,252,241,0.05); }"
            "QTextBrowser { background: rgba(11,12,16,220); color: #C5C6C7; "
            "  border: 1px solid rgba(102,252,241,0.10); border-radius: 8px; "
            "  font-family: 'Segoe UI', sans-serif; font-size: 13px; padding: 8px; }"
            "QScrollBar:vertical { background: transparent; width: 5px; }"
            "QScrollBar::handle:vertical { background: rgba(102,252,241,0.20); border-radius: 2px; min-height: 30px; }"
            "QScrollBar::handle:vertical:hover { background: rgba(102,252,241,0.45); }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
            "QPushButton { background: rgba(102,252,241,0.08); color: #66FCF1; "
            "  border: 1px solid rgba(102,252,241,0.18); padding: 7px 28px; border-radius: 6px; "
            "  font-family: 'Segoe UI Semibold', sans-serif; }"
            "QPushButton:hover { background: rgba(102,252,241,0.15); "
            "  border-color: rgba(102,252,241,0.30); }"));

        auto* mainLayout = new QVBoxLayout(dlg);
        mainLayout->setContentsMargins(16, 14, 16, 14);
        mainLayout->setSpacing(10);

        // Заголовок
        auto* title = new QLabel(
            QStringLiteral("<b style='font-size:15px; color:#66FCF1; letter-spacing:2px;'>"
                           "⬡ J.A.R.V.I.S. User Guide</b>"),
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

        // Color constants for HTML (Cyberpunk palette)
        const char* H = "#66FCF1"; // headings
        const char* S2 = "#45A29E"; // subheadings
        const char* W = "#C5C6C7"; // body text
        const char* A = "#ffaa44"; // warnings
        const char* G = "#44ff44"; // success/tips
        Q_UNUSED(W)

        QVector<Section> sections = {
        // ══════════════════════════════════════════════════════════
        //  QUICK START
        // ══════════════════════════════════════════════════════════
        {
            "🚀", "Быстрый старт", "Quick Start",
            QString(), // RU filled below
            QStringLiteral(R"w(<h3 style='color:%1;'>🚀 Quick Start</h3>
<p>No setup needed for basic commands — just launch and type.</p>
<h4 style='color:%2;'>First steps</h4>
<ol>
<li>Launch JARVIS from the Start Menu or Desktop shortcut</li>
<li>Type <b>"hello"</b> or any command in the input field</li>
<li>For AI-powered answers: <b>Settings → Claude API key...</b></li>
</ol>
<h4 style='color:%2;'>AI Backends (pick any or all)</h4>
<table border='0' cellpadding='4'>
<tr><td style='color:%3;'><b>Claude</b></td><td>Best quality for code & reasoning (~$1-3/mo) — console.anthropic.com</td></tr>
<tr><td style='color:%3;'><b>Gemini</b></td><td>Free tier, built-in key — aistudio.google.com</td></tr>
<tr><td style='color:%3;'><b>Ollama</b></td><td>100% offline local LLM — ollama.com → <code>ollama pull qwen2.5:3b</code></td></tr>
</table>
<p>Enable <b>Agent Mode</b> (Settings menu) to auto-route: casual → Ollama (free), code → Claude.</p>
<h4 style='color:%2;'>Adaptive Focus System</h4>
<p>JARVIS automatically detects your workflow from the <b>Core Memory Stream</b> — the last 10 interactions
scored by <code style='color:%3;'>importance / (1 + hours_elapsed)</code>. Based on keyword patterns it
shifts its persona between <b>Developer</b> (terse, technical), <b>Creative</b> (brainstorming),
<b>Admin</b> (step-by-step), or <b>Casual</b> (witty) — no configuration needed.</p>
<h4 style='color:%2;'>What works offline vs. online</h4>
<table border='0' cellpadding='4'>
<tr><td style='color:%3;'><b>Always offline</b></td><td>Voice input (Vosk), system commands, response cache, behavior patterns, activity tracking, virtual keyboard, PC control</td></tr>
<tr><td style='color:%3;'><b>Needs internet</b></td><td>Claude API, Gemini API, auto-updater, Screenshot Vision</td></tr>
<tr><td style='color:%3;'><b>Optional local</b></td><td>Ollama (runs on your GPU/CPU, no internet)</td></tr>
</table>
<p style='color:%4;background:rgba(68,255,68,0.06);padding:8px;border-radius:6px;'>
✅ JARVIS works without internet for all local commands, cached responses, and learned behavior patterns.</p>)w")
                .arg(QLatin1String(H), QLatin1String(S2), QLatin1String(S2), QLatin1String(G))
        },
        // ══════════════════════════════════════════════════════════
        //  COMMANDS
        // ══════════════════════════════════════════════════════════
        {
            "⌨", "Команды", "Commands",
            QString(),
            QStringLiteral(R"w(<h3 style='color:%1;'>⌨ Complete Command Reference</h3>
<h4 style='color:%2;'>System Automation</h4>
<table border='0' cellpadding='4' style='width:100%%;'>
<tr><td style='color:%1;white-space:nowrap;'><b>open &lt;app&gt;</b></td><td>Launch any installed application (Chrome, Steam, Notepad, etc.)</td></tr>
<tr><td style='color:%1;'><b>close &lt;app&gt;</b></td><td>Terminate a running process</td></tr>
<tr><td style='color:%1;'><b>lock screen</b></td><td>Lock the Windows session</td></tr>
<tr><td style='color:%1;'><b>shutdown / restart</b></td><td>Power off or reboot the PC</td></tr>
<tr><td style='color:%1;'><b>volume &lt;0-100&gt; / louder / quieter</b></td><td>System volume control</td></tr>
<tr><td style='color:%1;'><b>brightness up / down</b></td><td>Screen brightness adjustment</td></tr>
<tr><td style='color:%1;'><b>what time / what date</b></td><td>Current time and date</td></tr>
<tr><td style='color:%1;'><b>2+2 / 10*5 / sqrt(144)</b></td><td>Instant math evaluation</td></tr>
</table>
<h4 style='color:%2;'>Memory & Knowledge</h4>
<table border='0' cellpadding='4' style='width:100%%;'>
<tr><td style='color:%1;white-space:nowrap;'><b>remember</b> key=value</td><td>Permanently store a fact in the knowledge base</td></tr>
<tr><td style='color:%1;'><b>recall</b> key</td><td>Retrieve a stored fact</td></tr>
<tr><td style='color:%1;'><b>memory</b></td><td>List all stored facts</td></tr>
<tr><td style='color:%1;'><b>profile</b></td><td>Show learned work patterns, scenarios, and time-of-day preferences</td></tr>
<tr><td style='color:%1;'><b>stats</b></td><td>Display command usage frequency across sessions</td></tr>
</table>
<h4 style='color:%2;'>Project & Code Intelligence</h4>
<table border='0' cellpadding='4' style='width:100%%;'>
<tr><td style='color:%1;white-space:nowrap;'><b>index</b> &lt;path&gt;</td><td>Index a project folder — enables RAG code context in AI queries</td></tr>
<tr><td style='color:%1;'><b>symbol</b> &lt;name&gt;</td><td>Search for classes, functions, structs, or variables in the index</td></tr>
<tr><td style='color:%1;'><b>grep</b> &lt;text&gt;</td><td>Full-text search across all indexed project files</td></tr>
</table>
<h4 style='color:%2;'>Input & Keyboard Emulation</h4>
<table border='0' cellpadding='4' style='width:100%%;'>
<tr><td style='color:%1;white-space:nowrap;'><b>type</b> &lt;text&gt;</td><td>Type text into the currently active window</td></tr>
<tr><td style='color:%1;'><b>press</b> &lt;key&gt;</td><td>Press a single key (Enter, F5, Escape, etc.)</td></tr>
<tr><td style='color:%1;'><b>combo</b> &lt;keys&gt;</td><td>Press a key combination (Ctrl+S, Alt+Tab, Ctrl+Shift+P)</td></tr>
</table>
<h4 style='color:%2;'>Screen Vision & AI</h4>
<table border='0' cellpadding='4' style='width:100%%;'>
<tr><td style='color:%1;white-space:nowrap;'><b>"what do you see"</b></td><td>Capture screen and analyze via Claude Vision</td></tr>
<tr><td style='color:%1;'><b>"describe screen"</b></td><td>Full visual description of screen content</td></tr>
<tr><td style='color:%1;'><b>"click on X"</b></td><td>OCR locates the element, then clicks it</td></tr>
</table>
<h4 style='color:%2;'>Conversation & History</h4>
<table border='0' cellpadding='4' style='width:100%%;'>
<tr><td style='color:%1;white-space:nowrap;'><b>Any question</b></td><td>Routes to Claude (code/complex) or Ollama/Gemini (casual chat)</td></tr>
<tr><td style='color:%1;'><b>"recall what happened..."</b></td><td>Search session journal by date or topic keyword</td></tr>
<tr><td style='color:%1;'><b>apikey</b> &lt;key&gt;</td><td>Set or update your Claude API key</td></tr>
<tr><td style='color:%1;'><b>help</b></td><td>Show the quick reference guide in chat</td></tr>
</table>
<p style='font-size:11px;color:rgba(197,198,199,0.5);'>All commands work in both English and Russian without switching.</p>)w")
                .arg(QLatin1String(H), QLatin1String(S2))
        },
        // ══════════════════════════════════════════════════════════
        //  VOICE INPUT
        // ══════════════════════════════════════════════════════════
        {
            "🎙", "Голосовой ввод", "Voice Input",
            QString(),
            QStringLiteral(R"w(<h3 style='color:%1;'>🎙 Voice Input System</h3>
<p>Powered by <b>Vosk</b> — fully offline speech recognition. No cloud, no API keys, no internet required.</p>
<h4 style='color:%2;'>How it works</h4>
<ol>
<li><b>VoiceRecorder</b> captures raw microphone audio with real-time Voice Activity Detection (VAD) at -45 dB threshold</li>
<li>Audio is downsampled to 16 kHz mono and streamed to the <b>VoskWorker</b> in a separate thread</li>
<li>If multiple language models are loaded, both run in parallel and the highest-confidence result wins</li>
<li>Silence detection (800ms timeout) auto-stops recording; max 10 seconds per utterance</li>
</ol>
<h4 style='color:%2;'>Wake word activation</h4>
<p>Say <b>"Jarvis"</b> or <b>"Джарвис"</b> — the wake word triggers hands-free listening mode.
No button press needed. JARVIS processes your next sentence and returns to idle.</p>
<h4 style='color:%2;'>Whisper mode</h4>
<p>When ambient noise is high and you speak softly, JARVIS auto-adjusts its recognition thresholds.
The VAD adapts dynamically — you don't need to configure anything.</p>
<h4 style='color:%2;'>Language auto-detection</h4>
<p>If English and Russian models are both loaded, JARVIS runs recognition on both and picks
the result with higher confidence. The detected language is shown in the chat log as
<code>[🎤 EN]</code> or <code>[🎤 RU]</code>.</p>
<h4 style='color:%2;'>Available models</h4>
<table border='0' cellpadding='4'>
<tr><td style='color:%3;'><b>EN Small</b></td><td>~40 MB — fast, good for commands & wake word ✅</td></tr>
<tr><td style='color:%2;'><b>EN Full</b></td><td>~1.8 GB — high-quality dictation</td></tr>
<tr><td style='color:%2;'><b>RU</b></td><td>~1.8 GB — Russian language support</td></tr>
<tr><td style='color:%2;'><b>DE / FR / ZH</b></td><td>~0.5-1.0 GB — German, French, Chinese</td></tr>
</table>
<p>Manage models via <b>Settings → 🎤 Voice Models...</b></p>
<h4 style='color:%2;'>Passive recording (dataset mode)</h4>
<p>Enable via Settings menu. JARVIS continuously transcribes background speech into the
<b>voice_journal</b> database table with language and confidence scores. Entries marked
<i>"Pending processing"</i> haven't been analyzed yet — BackgroundLearner processes them
into training pairs. Raw entries are auto-deleted after 7 days.</p>
<p style='font-size:11px;color:rgba(197,198,199,0.5);'>Audio never leaves your computer. Silence is not recorded. Runs on CPU — no GPU needed.</p>)w")
                .arg(QLatin1String(H), QLatin1String(S2), QLatin1String(G))
        },
        // ══════════════════════════════════════════════════════════
        //  AI TRAINING
        // ══════════════════════════════════════════════════════════
        {
            "◈", "Обучение ИИ", "AI Training",
            QString(),
            QStringLiteral(R"w(<h3 style='color:%1;'>◈ AI Training & Data Pipeline</h3>
<p>JARVIS accumulates your interactions and learns to respond in your style over time.</p>
<h4 style='color:%2;'>Automatic data collection</h4>
<p>Every user-AI exchange is saved to the <b>training_logs</b> database table with <code>rating=0</code>.
When you click the <b>👍</b> button below a response, the rating updates to <code>1</code>, marking
it as a high-quality training pair. These liked pairs are prioritized during export.</p>
<h4 style='color:%2;'>Behavior pattern learning</h4>
<p>The <b>BackgroundLearner</b> continuously analyzes chat history and builds <b>behavior_patterns</b> —
normalized trigger→response pairs with frequency and confidence scores. When JARVIS sees a question
that matches an existing pattern with frequency ≥ 3 and confidence ≥ 0.5, it can respond
<i>instantly without any API call</i> (offline brain). This means common questions get faster over time.</p>
<h4 style='color:%2;'>Response cache (offline library)</h4>
<p>Jokes, advice, facts, motivational quotes, and poems generated by Claude are cached in the
<b>response_cache</b> table. Next time you ask for a joke, JARVIS serves it from cache —
zero latency, zero cost. The cache grows automatically with every unique request.</p>
<h4 style='color:%2;'>Voice journal (passive dataset)</h4>
<p>When passive recording is enabled, ambient speech is transcribed into <b>voice_journal</b>
with language tags and confidence scores. BackgroundLearner processes these entries into
training pairs. Unprocessed entries show as <i>"Pending processing"</i> in the stats dialog.</p>
<h4 style='color:%2;'>Export for fine-tuning</h4>
<p>Menu <b>🧠 Training → Export .jsonl...</b> creates an Alpaca/Unsloth-format dataset:</p>
<pre style='background:rgba(102,252,241,0.04);padding:8px;border-radius:6px;font-size:11px;color:%2;'>
{"instruction": "user question", "input": "", "output": "AI response", "model": "claude"}
</pre>
<p>Recommended: export when you reach <b>500+ liked entries</b>. Use with Unsloth, LLaMA-Factory,
or any Alpaca-compatible trainer to fine-tune a local model on your personal style.</p>
<h4 style='color:%2;'>Auto-cleanup</h4>
<p>BackgroundLearner automatically removes noise: messages under 5 chars, AI responses under 20 chars,
speech recognition artifacts ("hmm", "uh", "aga"), and near-duplicate pairs.</p>
<p style='color:%3;background:rgba(68,255,68,0.06);padding:8px;border-radius:6px;'>
💡 More 👍 likes = better future model. The training pipeline is fully automatic — just use JARVIS
naturally and like the responses you want it to learn from.</p>)w")
                .arg(QLatin1String(H), QLatin1String(S2), QLatin1String(G))
        },
        // ══════════════════════════════════════════════════════════
        //  SCREEN VISION
        // ══════════════════════════════════════════════════════════
        {
            "⊡", "Зрение и экран", "Screen Vision",
            QString(),
            QStringLiteral(R"w(<h3 style='color:%1;'>⊡ Screen Vision & Control</h3>
<p>JARVIS can see your screen and interact with UI elements using Claude Vision + OCR.</p>
<h4 style='color:%2;'>Vision commands</h4>
<table border='0' cellpadding='4' style='width:100%%;'>
<tr><td style='color:%1;white-space:nowrap;'><b>"what do you see"</b></td><td>Takes a screenshot and sends it to Claude Vision for analysis</td></tr>
<tr><td style='color:%1;'><b>"describe screen"</b></td><td>Full visual description of everything on screen</td></tr>
<tr><td style='color:%1;'><b>"click on OK"</b></td><td>OCR scans for the text "OK", locates it, and performs a mouse click</td></tr>
<tr><td style='color:%1;'><b>"press button X"</b></td><td>Searches for UI element by text and clicks it</td></tr>
</table>
<p style='color:%3;'>⚠️ Requires a Claude API key (Vision model is used for screen analysis).</p>
<h4 style='color:%2;'>File attachments</h4>
<p>Attach files for AI analysis — JARVIS extracts content and includes it in the prompt context.</p>
<table border='0' cellpadding='4' style='width:100%%;'>
<tr><td style='color:%1;'><b>📎 button</b></td><td>Open file picker dialog (also Ctrl+O)</td></tr>
<tr><td style='color:%1;'><b>Drag &amp; Drop</b></td><td>Drop files directly into the JARVIS window</td></tr>
</table>
<p><b>Supported formats:</b> .txt .cpp .h .py .js .ts .pdf .docx and image files (PNG, JPG, BMP).</p>
<h4 style='color:%2;'>Screenshot learning</h4>
<p>The ScreenshotLearner module can periodically capture your screen to learn app usage patterns.
This data stays local and helps JARVIS understand which tools you use at different times of day.</p>)w")
                .arg(QLatin1String(H), QLatin1String(S2), QLatin1String(A))
        },
        // ══════════════════════════════════════════════════════════
        //  SETTINGS
        // ══════════════════════════════════════════════════════════
        {
            "⚙", "Настройки", "Settings",
            QString(),
            QStringLiteral(R"w(<h3 style='color:%1;'>⚙ Settings & Configuration</h3>
<h4 style='color:%2;'>API keys</h4>
<table border='0' cellpadding='4' style='width:100%%;'>
<tr><td style='color:%1;white-space:nowrap;'><b>Claude</b></td><td>console.anthropic.com → API Keys → Create Key</td></tr>
<tr><td style='color:%1;'><b>Gemini</b></td><td>aistudio.google.com → Get API key (free tier)</td></tr>
<tr><td style='color:%1;'><b>Ollama</b></td><td>ollama.com → install → <code>ollama pull qwen2.5:3b</code></td></tr>
</table>
<h4 style='color:%2;'>Recommended Ollama models</h4>
<table border='0' cellpadding='4'>
<tr><td style='color:%3;'><b>qwen2.5:3b</b></td><td>Fast, good quality ✅ recommended</td></tr>
<tr><td><b>phi3:mini</b></td><td>Fast, Microsoft model</td></tr>
<tr><td><b>llama3.2:3b</b></td><td>Good balance of speed and quality</td></tr>
<tr><td><b>mistral:7b</b></td><td>Strong for code generation</td></tr>
</table>
<h4 style='color:%2;'>Agent Mode routing</h4>
<p><b>Settings → Agent Mode</b> enables intelligent routing:<br>
Simple questions / chitchat → <b>Ollama</b> (offline, free, instant)<br>
Code analysis / complex reasoning → <b>Claude API</b> (best quality)<br>
If Ollama is unavailable → <b>Gemini</b> (free fallback) → Claude (last resort)</p>
<h4 style='color:%2;'>Audio modes</h4>
<p>Click the speaker icon in the bottom bar to cycle:</p>
<table border='0' cellpadding='4'>
<tr><td style='color:%1;'><b>🔊 Full</b></td><td>TTS speech + UI sound effects</td></tr>
<tr><td style='color:%1;'><b>🔕 Mute Speech</b></td><td>UI sounds only — JARVIS stays silent</td></tr>
<tr><td style='color:%1;'><b>🔇 Mute All</b></td><td>Complete silence</td></tr>
</table>
<h4 style='color:%2;'>Keyboard shortcuts</h4>
<table border='0' cellpadding='4'>
<tr><td style='color:%1;'><b>Enter</b></td><td>Send message</td></tr>
<tr><td style='color:%1;'><b>Ctrl+O</b></td><td>Attach files</td></tr>
<tr><td style='color:%1;'><b>Escape</b></td><td>Close clarification panel / focus input</td></tr>
<tr><td style='color:%1;'><b>Drag &amp; Drop</b></td><td>Attach files by dropping into window</td></tr>
</table>
<h4 style='color:%2;'>Data storage</h4>
<p>All data is stored locally in <b>Documents/JARVIS/jarvis.db</b> (SQLite).
Easy to backup, easy to delete to start fresh. Session memory is in
<b>Documents/JARVIS/jarvis_memory.json</b>.</p>)w")
                .arg(QLatin1String(H), QLatin1String(S2), QLatin1String(G))
        },
        // ══════════════════════════════════════════════════════════
        //  TROUBLESHOOTING
        // ══════════════════════════════════════════════════════════
        {
            "🔧", "Устранение проблем", "Troubleshooting",
            QString(),
            QStringLiteral(R"w(<h3 style='color:%1;'>🔧 Troubleshooting</h3>
<h4 style='color:%2;'>No AI response</h4>
<ul>
<li>Check API key: <b>Settings → Claude API key...</b></li>
<li>Try <b>Settings → Agent Mode</b> to enable Ollama or Gemini fallback</li>
<li>Gemini works with a free API key as a backup</li>
<li>Check your internet connection for cloud-based models</li>
</ul>
<h4 style='color:%2;'>Application not found</h4>
<ul>
<li>Use the full name: <b>"open Google Chrome"</b> not <b>"open google"</b></li>
<li>Both English and Russian application names work</li>
<li>JARVIS searches system PATH + common install directories</li>
</ul>
<h4 style='color:%2;'>Voice input not working</h4>
<ul>
<li>Check microphone permission: <b>Windows Settings → Privacy → Microphone</b></li>
<li>Open <b>Settings → 🎤 Voice Models...</b> and download at least one model</li>
<li>The 🎤 button in the input bar only activates after a model finishes loading</li>
<li>If models are loaded but recognition fails, try the EN Small model (~40 MB) first</li>
</ul>
<h4 style='color:%2;'>Adaptive Focus stuck in wrong mode</h4>
<p>The focus system reads the last 10 memory events. If it's stuck in "Developer" mode when you
want casual chat, just have a few casual conversations — the time-decay formula will naturally
shift the focus as newer events outweigh older ones.</p>
<h4 style='color:%2;'>Antivirus / SmartScreen warnings</h4>
<p>JARVIS uses <b>SendInput</b> (virtual keyboard), <b>ShellExecuteW</b> (app launcher), and writes
to <b>Documents/JARVIS/</b>. These are legitimate operations but may trigger security software.
Add the JARVIS folder to your antivirus exclusions.</p>
<h4 style='color:%2;'>Database issues</h4>
<p>If the database becomes corrupted, delete <b>Documents/JARVIS/jarvis.db</b> — JARVIS will
recreate it on next launch. Session memory (<b>jarvis_memory.json</b>) is separate and won't be affected.</p>
<h4 style='color:%2;'>Report a bug</h4>
<p>Menu <b>Help → 🐛 Report a Bug...</b> — creates a GitHub issue with system info.</p>)w")
                .arg(QLatin1String(H), QLatin1String(S2))
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
            const auto& sec = sections[row];
            QString html = (!isEnglish && !sec.htmlRu.isEmpty()) ? sec.htmlRu : sec.htmlEn;
            content_browser->setHtml(html);
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
    if (m_audioManager->speechAllowed()) m_jarvis->speakAsync(result.message);
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
            if (result.length() <= 300) if (m_audioManager->speechAllowed()) m_jarvis->speakAsync(result);
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
            if (searchResult.length() <= 300) if (m_audioManager->speechAllowed()) m_jarvis->speakAsync(searchResult);
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
    // НЕ сбрасываем m_lastInputWasVoice здесь — он нужен в onAsyncResponse
    // для записи в voice_journal. Сбросится там после использования.
    QString response = m_jarvis->processCommand(
        text, attachmentBlock, m_langDetector.systemInstruction());

    if (!response.isEmpty()) {
        appendLog(Str::logJarvis(), response, Theme::LogColors::jarvis);
        if (m_audioManager->speechAllowed()) m_jarvis->speakAsync(response);

        // ── Сохраняем синхронный ответ в датасет (rating=0) ──
        m_lastAiResponse = response;
        m_lastAiModel    = QStringLiteral("local");
        if (m_lastSessionId.isEmpty())
            m_lastSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

        DbTrainingLog syncLog;
        syncLog.userId      = 1;
        syncLog.userMessage = text;
        syncLog.aiResponse  = response;
        syncLog.model       = m_lastAiModel;
        syncLog.sessionId   = m_lastSessionId;
        syncLog.rating      = 0;
        DatabaseManager::instance().addTrainingLog(syncLog);

        // Если это был голосовой ввод — сохраняем в voice_journal
        if (m_lastInputWasVoice && m_passiveListener) {
            m_passiveListener->addVoiceCommandPair(
                text, response, m_lastVoiceLanguage);
            m_lastInputWasVoice = false;
        }

        // Активируем кнопку 👍
        if (m_likeBtn) {
            m_likeBtn->setEnabled(true);
            m_likeBtn->setProperty("liked", false);
            m_likeBtn->setText(QStringLiteral("👍"));
            ++m_trainingCount;
        }
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

    m_clarifyBar->setMaximumHeight(0);
    m_clarifyBar->setVisible(true);
    auto* anim = new QPropertyAnimation(m_clarifyBar, "maximumHeight", this);
    anim->setDuration(250);
    anim->setStartValue(0);
    anim->setEndValue(120);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::hideClarification()
{
    if (!m_clarifyBar) return;
    auto* anim = new QPropertyAnimation(m_clarifyBar, "maximumHeight", this);
    anim->setDuration(200);
    anim->setStartValue(m_clarifyBar->height());
    anim->setEndValue(0);
    anim->setEasingCurve(QEasingCurve::InCubic);
    connect(anim, &QPropertyAnimation::finished, m_clarifyBar, [this]() {
        m_clarifyBar->setVisible(false);
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
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
    m_audioManager->playSuccess();
    appendLog(Str::logJarvis(), response, Theme::LogColors::jarvis);
    if (response.length() <= 200) {
        if (m_audioManager->speechAllowed()) m_jarvis->speakAsync(response);
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
        // Сбрасываем voice flag после использования
        m_lastInputWasVoice = false;
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
    m_audioManager->playWarning();
    appendLog(Str::logError(), error, Theme::LogColors::error);
}

void MainWindow::onSuggestion(const QString& description, const QString& action)
{
    m_pendingSuggestionAction = action;
    m_suggestionText->setText(QStringLiteral("→ ") + description);
    m_suggestionBar->setMaximumHeight(0);
    m_suggestionBar->setVisible(true);
    auto* anim = new QPropertyAnimation(m_suggestionBar, "maximumHeight", this);
    anim->setDuration(250);
    anim->setStartValue(0);
    anim->setEndValue(60);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
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
    m_updateBar->setMaximumHeight(0);
    m_updateBar->setVisible(true);
    auto* anim = new QPropertyAnimation(m_updateBar, "maximumHeight", this);
    anim->setDuration(300);
    anim->setStartValue(0);
    anim->setEndValue(60);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);

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

    auto* hamburgerBtn = new QPushButton(QStringLiteral("☰"), this);
    hamburgerBtn->setObjectName(QStringLiteral("hamburgerBtn"));
    hamburgerBtn->setFixedSize(36, 36);
    hamburgerBtn->setCursor(Qt::PointingHandCursor);
    hamburgerBtn->setToolTip(IS_EN ? QStringLiteral("Menu") : QStringLiteral("Меню"));
    hamburgerBtn->setStyleSheet(
        QStringLiteral("QPushButton { background: transparent; color: #607888; "
                       "border: 1px solid transparent; border-radius: 8px; font-size: 20px; "
                       "font-family: 'Segoe UI', sans-serif; } "
                       "QPushButton:hover { background: rgba(0,212,255,0.08); color: #00d4ff; "
                       "border-color: rgba(0,212,255,0.2); }"));

    auto* title = new QLabel(
        QStringLiteral("⬡  J.A.R.V.I.S.  <span style='font-size:11px; color:rgba(102,252,241,100); "
                       "font-weight:normal; letter-spacing:1px;'>v%1</span>")
            .arg(QCoreApplication::applicationVersion()), this);
    title->setObjectName(QStringLiteral("titleLabel"));
    title->setTextFormat(Qt::RichText);

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

    topBar->addWidget(hamburgerBtn);
    topBar->addSpacing(4);
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

    // === Quick-access toolbar ===
    auto* toolbar = new QHBoxLayout();
    toolbar->setSpacing(10);
    toolbar->setContentsMargins(4, 4, 4, 4);

    auto makeToolBtn = [this](const QString& icon, const QString& tip) -> QPushButton* {
        auto* btn = new QPushButton(icon, this);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; color: rgba(102,252,241,0.45); "
            "  border: none; padding: 4px 8px; font-size: 17px; } "
            "QPushButton:hover { color: #66FCF1; "
            "  background: rgba(102,252,241,0.08); border-radius: 8px; }"));
        return btn;
    };

    auto* tbSearch    = makeToolBtn(QStringLiteral("⌕"),
        IS_EN ? QStringLiteral("Search history")   : QStringLiteral("Поиск по чату"));
    auto* tbProject   = makeToolBtn(QStringLiteral("⬡"),
        IS_EN ? QStringLiteral("Index project")    : QStringLiteral("Открыть проект"));
    auto* tbVoice     = makeToolBtn(QStringLiteral("🎙"),
        IS_EN ? QStringLiteral("Voice models")     : QStringLiteral("Модели голоса"));
    auto* tbTrain     = makeToolBtn(QStringLiteral("◈"),
        IS_EN ? QStringLiteral("Training stats")   : QStringLiteral("Статистика обучения"));
    auto* tbCapture   = makeToolBtn(QStringLiteral("⊡"),
        IS_EN ? QStringLiteral("Screenshot + AI")  : QStringLiteral("Скриншот + AI"));
    auto* tbClear     = makeToolBtn(QStringLiteral("⌫"),
        IS_EN ? QStringLiteral("Clear chat")       : QStringLiteral("Очистить чат"));

    toolbar->addWidget(tbSearch);
    toolbar->addWidget(tbProject);
    toolbar->addWidget(tbVoice);
    toolbar->addWidget(tbTrain);
    toolbar->addWidget(tbCapture);
    toolbar->addStretch();
    toolbar->addWidget(tbClear);
    vbox->addLayout(toolbar);

    // Connect toolbar buttons
    connect(tbSearch, &QPushButton::clicked, this, [this]() {
        bool ok;
        QString query = QInputDialog::getText(this,
            IS_EN ? QStringLiteral("Search") : QStringLiteral("Поиск"),
            IS_EN ? QStringLiteral("Search chat history:") : QStringLiteral("Поиск по истории:"),
            QLineEdit::Normal, QString(), &ok);
        if (ok && !query.trimmed().isEmpty()) {
            m_input->setText(QStringLiteral("вспомни ") + query.trimmed());
            onSend();
        }
    });
    connect(tbProject, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this,
            Str::dlgChooseFolder(),
            m_jarvis->projectIndexer()->projectRoot().isEmpty()
                ? QDir::homePath() : m_jarvis->projectIndexer()->projectRoot(),
            QFileDialog::ShowDirsOnly);
        if (dir.isEmpty()) return;
        m_jarvis->projectIndexer()->setProjectRoot(dir);
        m_jarvis->projectIndexer()->indexProject();
        m_jarvis->projectIndexer()->enableFileWatcher(true);
        m_jarvis->syncProjectInfoToMemory();
        appendLog(Str::logJarvis(),
            Str::projIndexed() + QString::number(m_jarvis->projectIndexer()->fileCount())
            + Str::projSymbols() + QString::number(m_jarvis->projectIndexer()->symbolCount()),
            Theme::LogColors::jarvis);
    });
    connect(tbClear, &QPushButton::clicked, this, [this]() { m_log->clear(); });

    connect(tbVoice, &QPushButton::clicked, this, [this]() {
        for (auto* action : menuBar()->actions()) {
            auto* menu = action->menu();
            if (!menu) continue;
            for (auto* sub : menu->actions()) {
                if (sub->text().contains(QStringLiteral("🎤"))) {
                    sub->trigger();
                    return;
                }
            }
        }
    });
    connect(tbTrain, &QPushButton::clicked, this, [this]() {
        for (auto* action : menuBar()->actions()) {
            auto* menu = action->menu();
            if (!menu) continue;
            for (auto* sub : menu->actions()) {
                if (sub->text().contains(QStringLiteral("📊")) && sub->text().contains(QStringLiteral("Stat"))) {
                    sub->trigger();
                    return;
                }
            }
        }
    });
    connect(tbCapture, &QPushButton::clicked, this, [this]() {
        for (auto* action : menuBar()->actions()) {
            auto* menu = action->menu();
            if (!menu) continue;
            for (auto* sub : menu->actions()) {
                if (sub->text().contains(QStringLiteral("📸"))) {
                    sub->trigger();
                    return;
                }
            }
        }
    });

    // === Hamburger menu ===
    connect(hamburgerBtn, &QPushButton::clicked, this, [this, hamburgerBtn]() {
        auto* popup = new QMenu(this);
        popup->setStyleSheet(qApp->styleSheet());
        for (auto* action : menuBar()->actions()) {
            popup->addAction(action);
        }
        popup->exec(hamburgerBtn->mapToGlobal(QPoint(0, hamburgerBtn->height())));
        popup->deleteLater();
    });

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
    m_log->document()->setMaximumBlockCount(300);
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
    m_micBtn->setEnabled(false); // выключена до загрузки моделей Vosk
    m_micBtn->setToolTip(IS_EN ? QStringLiteral("Voice input — loading models...")
                               : QStringLiteral("Голосовой ввод — загрузка моделей..."));
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
    bottomBar->setContentsMargins(4, 2, 4, 2);
    bottomBar->setSpacing(6);

    auto* modeLabel = new QLabel(QStringLiteral("v") + QCoreApplication::applicationVersion(), this);
    modeLabel->setStyleSheet(
        QStringLiteral("color: #2a4a60; font-size: 11px; border: none; background: transparent;"));
    modeLabel->setFixedHeight(26);

    const QString barBtnStyle = QStringLiteral(
        "padding: 0px; margin: 0px; border: none; "
        "font-size: 15px; background: transparent;");

    m_audioModeBtn = new QPushButton(m_audioManager->modeLabel(), this);
    m_audioModeBtn->setObjectName(QStringLiteral("audioModeBtn"));
    m_audioModeBtn->setStyleSheet(barBtnStyle);
    m_audioModeBtn->setToolTip(m_audioManager->modeTooltip());
    connect(m_audioModeBtn, &QPushButton::clicked, this, [this]() {
        m_audioManager->cycleMode();
        m_audioModeBtn->setText(m_audioManager->modeLabel());
        m_audioModeBtn->setToolTip(m_audioManager->modeTooltip());
    });

    auto* kbBtn = new QPushButton(QStringLiteral("⌨"), this);
    kbBtn->setObjectName(QStringLiteral("kbToggleBtn"));
    kbBtn->setStyleSheet(barBtnStyle);
    kbBtn->setToolTip(Str::menuKeyboard());

    m_likeBtn->setStyleSheet(barBtnStyle);

    auto* bSpacer = new QWidget(this);
    bSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    bSpacer->setFixedHeight(26);

    bottomBar->addWidget(modeLabel);
    bottomBar->addWidget(m_likeBtn);
    bottomBar->addWidget(m_audioModeBtn);
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

    // Пути к Vosk моделям — те же что у VoiceInput (для совместимости конфига)
    passiveCfg.modelPathRu = m_voiceInput->config().modelPathRu;
    passiveCfg.modelPathEn = m_voiceInput->config().modelPathEn;

    // Шерим уже загруженные модели Vosk из VoiceInput вместо загрузки дубликатов
    // (~3.6 GB экономии). Инициализируем пассивный слушатель когда модели готовы.
    connect(m_voiceInput, &VoiceInput::ready, this, [this, passiveCfg]() {
        VoskWorker* w = m_voiceInput->worker();
        void* modelRu = w ? w->modelForLang(QStringLiteral("ru")) : nullptr;
        void* modelEn = w ? w->modelForLang(QStringLiteral("en")) : nullptr;
        m_passiveListener->initializeWithSharedModels(modelRu, modelEn, passiveCfg);
    });

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
    const QString time = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    const auto& tc = ThemeManager::colors(m_themeIndex);
    const QString escaped = text.toHtmlEscaped()
                                .replace(QStringLiteral("\n"), QStringLiteral("<br>"));
    const QString html = ThemeManager::buildMessageHtml(tc, time, who, escaped, color);

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
        m_audioManager->playListening();
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
    m_micBtn->setToolTip(IS_EN ? QStringLiteral("Voice input (Vosk)")
                               : QStringLiteral("Голосовой ввод (Vosk)"));
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

    auto& db = DatabaseManager::instance();

    // Запись уже могла быть автосохранена с rating=0 (см. onAsyncResponse /
    // синхронную ветку в onSendClicked) — сначала пробуем поднять её
    // рейтинг до 1. Если такой пары ещё нет в БД (например, автосохранение
    // не сработало из-за дублирования текста), updateTrainingLogRating
    // вернёт false и мы добавляем запись напрямую через addTrainingLog.
    const bool updated = db.updateTrainingLogRating(m_lastUserInput, m_lastAiResponse, 1);
    qint64 insertedId = -1;
    if (!updated) {
        insertedId = db.addTrainingLog(log);
    }

    // Реальный успех — это либо успешный UPDATE существующей пары,
    // либо успешный INSERT новой. Раньше здесь стояло "|| true", из-за
    // чего кнопка ВСЕГДА показывала "сохранено", даже если SQL-запрос
    // упал или INSERT OR IGNORE отбросил дубликат — то есть в БД на
    // самом деле ничего не попадало, а пользователь не мог об этом узнать.
    const bool actuallySaved = updated || insertedId > 0;

    if (actuallySaved) {
        ++m_trainingCount;
        m_likeBtn->setProperty("liked", true);
        m_likeBtn->setText(QStringLiteral("✅"));
        m_likeBtn->setEnabled(false);
        m_likeBtn->style()->unpolish(m_likeBtn);
        m_likeBtn->style()->polish(m_likeBtn);
        m_likeBtn->setToolTip(IS_EN
            ? QStringLiteral("Saved! Total: %1 responses").arg(m_trainingCount)
            : QStringLiteral("Сохранено! Всего: %1 ответов").arg(m_trainingCount));

        qDebug() << "[Training] Liked response saved, total:" << m_trainingCount;
    } else {
        // INSERT OR IGNORE отбросил вставку и UPDATE не нашёл строку —
        // значит эта именно пара уже была лайкнута ранее как дубликат,
        // либо случилась ошибка БД (см. DatabaseManager::lastError()
        // и qDebug-лог logError("addTrainingLog", ...) в консоли).
        m_likeBtn->setToolTip(IS_EN
            ? QStringLiteral("Already saved or DB error — check log: %1").arg(db.lastError())
            : QStringLiteral("Уже сохранено или ошибка БД — см. лог: %1").arg(db.lastError()));

        appendLog(Str::logSystem(),
            IS_EN ? QStringLiteral("ℹ️ Could not save like — already in dataset or DB error: %1")
                        .arg(db.lastError())
                  : QStringLiteral("ℹ️ Не удалось сохранить лайк — уже в датасете или ошибка БД: %1")
                        .arg(db.lastError()),
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