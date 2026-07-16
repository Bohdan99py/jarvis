// -------------------------------------------------------
// mainwindow.cpp — Главное окно J.A.R.V.I.S.
// -------------------------------------------------------

#include "mainwindow.h"
#include "jarvis.h"
#include "jarvis_paths.h"
#include "notification_manager.h"
#include "theme.h"
#include "virtual_keyboard.h"
#include "claude_api.h"
#include "auto_updater.h"
#include "project_indexer.h"
#include "session_memory.h"
#include "attachments_manager.h"
#include "lang.h"
#include "brain.h"
#include "code_actions.h"
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
#include "task_manager_dialog.h"
#include "training_center_dialog.h"
#include "chat_history_dialog.h"
#include "translation_engine.h"
#include "VoskSetupDialog.h"
#include <QClipboard>
#include "activity_tracker.h"
#include "user_profile.h"
#include "curiosity_engine.h"
#include "proactive_reminder_manager.h"
#include "user_profile_extended.h"
#include "memory_consolidation.h"
#include "self_journal.h"
#include "pdf_distiller.h"
#include "self_update_reflector.h"
#include "mobile_pairing_manager.h"
#include "j2j_mesh_connector.h"
#include "j2j_telegram_gateway.h"
#include "telegram_access_manager.h"
#include "jarvis_response.h"
#include "security_camera.h"
#include "face_registry.h"
#include "dependency_manager_dialog.h"
#include "profile_setup_dialog.h"
#include "skills_dialog.h"
#include "voice_synthesis_manager.h"
#include "llm_cache_manager.h"
#include "file_organizer.h"
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
#include <QDateTime>
#include <QTime>
#include <QDate>
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
#include <QStandardPaths>
#include <QCoreApplication>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QRegularExpression>

namespace {
// TaskNotifications::checkDeadlines() returns one "[TASK WARNING]: ..." /
// "[ЗАДАЧА]: ..." line per approaching/overdue task, newline-separated —
// split it into individual notification toasts instead of one wall of text.
void notifyDeadlineWarnings(const QString& warnings)
{
    qDebug() << "[MainWindow] notifyDeadlineWarnings called with:" << warnings;
    const QString overdueMarker = IS_EN ? QStringLiteral("OVERDUE") : QStringLiteral("ПРОСРОЧЕНО");
    for (const QString& line : warnings.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        NotificationManager::instance().showNotification(
            IS_EN ? QStringLiteral("Task deadline") : QStringLiteral("Дедлайн задачи"),
            line,
            line.contains(overdueMarker) ? NotificationManager::Level::Error
                                          : NotificationManager::Level::Warning);
    }
}
}

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
    // Дефолт — русский, пока пользователь явно не переключит язык в настройках.

    m_jarvis = new Jarvis(this);
    m_jarvis->setUiLanguage(english);
    CuriosityEngine::instance().setUiEnglish(english);

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

    // Первый запуск на новом ПК: спрашиваем имя и роль пользователя,
    // а не используем дефолтное имя из БД. Отложенно через singleShot,
    // чтобы окно успело создаться.
    if (!cfg.value(QStringLiteral("user/profileSetupDone"), false).toBool()) {
        QTimer::singleShot(0, this, [this]() { runFirstRunProfileSetup(); });
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

    // CuriosityEngine::questionPosted is now handled entirely via the
    // NotificationManager::askQuestion toast wired up further below
    // (right after the file-operation notifications), which replaced the
    // old clarify-bar-based prompt.

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
        NotificationManager::instance().showNotification(
            QStringLiteral("System Update"),
            IS_EN ? QStringLiteral("Version %1 is available").arg(newVersion)
                  : QStringLiteral("Доступна версия %1").arg(newVersion));
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

        NotificationManager::instance().showNotification(
            QStringLiteral("System Update"),
            IS_EN ? QStringLiteral("Update v%1 downloaded successfully")
                        .arg(m_jarvis->autoUpdater()->pendingVersion())
                  : QStringLiteral("Обновление v%1 успешно загружено")
                        .arg(m_jarvis->autoUpdater()->pendingVersion()));

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
        NotificationManager::instance().showNotification(
            QStringLiteral("System Update"), error, NotificationManager::Level::Error);
    });

    buildUI();
    buildMenuBar();
    menuBar()->setVisible(false);

    // Generated KiCad schematics join the attachments panel (so they're
    // reachable again after the chat scrolls past) and get an inline
    // "Open Folder" line right where they were created, instead of
    // leaving the user to go hunting for where the file landed.
    connect(m_jarvis->codeActions(), &CodeActions::kicadSchematicCreated,
            this, [this](const QString& path) {
        if (m_visualInsights)
            m_visualInsights->showFileRef(path, QFileInfo(path).fileName());

        const QString folderLine = IS_EN
            ? QStringLiteral("📁 Saved to: %1").arg(path)
            : QStringLiteral("📁 Сохранено: %1").arg(path);
        appendLog(Str::logSystem(), folderLine, Theme::LogColors::system);
    });

    // ── Неоновые toast-уведомления о файловых операциях ──
    connect(m_jarvis->codeActions(), &CodeActions::fileCreated,
            this, [](const QString& path) {
        NotificationManager::instance().showNotification(
            IS_EN ? QStringLiteral("File created") : QStringLiteral("Файл создан"),
            QFileInfo(path).fileName(),
            NotificationManager::Level::Success);
    });
    connect(m_jarvis->codeActions(), &CodeActions::fileModified,
            this, [](const QString& path) {
        NotificationManager::instance().showNotification(
            IS_EN ? QStringLiteral("File modified") : QStringLiteral("Файл изменён"),
            QFileInfo(path).fileName(),
            NotificationManager::Level::Success);
    });
    connect(m_jarvis->codeActions(), &CodeActions::kicadSchematicCreated,
            this, [](const QString& path) {
        NotificationManager::instance().showNotification(
            IS_EN ? QStringLiteral("KiCad schematic ready")
                  : QStringLiteral("Схема KiCad готова"),
            QFileInfo(path).fileName(),
            NotificationManager::Level::Success);
    });
    connect(m_jarvis->codeActions(), &CodeActions::actionError,
            this, [](const QString& path, const QString& error) {
        NotificationManager::instance().showNotification(
            IS_EN ? QStringLiteral("File operation failed")
                  : QStringLiteral("Ошибка файловой операции"),
            path.isEmpty() ? error
                           : QFileInfo(path).fileName() + QStringLiteral("\n") + error,
            NotificationManager::Level::Error);
    });

    // ── CuriosityEngine proactive questions → answerable notification ──
    // Previously this only ever reached the user via Telegram (it required
    // a gateway + target chat id that nothing in the codebase actually set,
    // so it never fired at all in practice). Now it's desktop-native: a
    // toast with the question, Yes/No quick-reply pills, and a free-text
    // field pops up; whatever the user types or taps is fed straight back
    // into CuriosityEngine::consumeAnswer so it's remembered exactly like a
    // Telegram reply would be. chatId 0 = desktop, which consumeAnswer
    // always matches regardless of which chat the question was pending for.
    connect(&CuriosityEngine::instance(), &CuriosityEngine::questionPosted,
            this, [](const QString& question, const QStringList& options) {
        NotificationManager::instance().askQuestion(
            QStringLiteral("J.A.R.V.I.S."), question,
            [](const QString& answer) {
                CuriosityEngine::instance().consumeAnswer(0, answer);
            },
            options);
    });

    // ── Task deadlines & reminders → notification ──
    connect(&ProactiveReminderManager::instance(), &ProactiveReminderManager::reminderFired,
            this, [](qint64, const QString& text) {
        NotificationManager::instance().showNotification(
            QStringLiteral("⏰ ") + (IS_EN ? QStringLiteral("Reminder") : QStringLiteral("Напоминание")),
            text, NotificationManager::Level::Warning);
    });

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

    // Dynamic welcome dashboard — replaces all hardcoded greeting strings
    showWelcomeDashboard();

    // Sync project info if indexed
    if (m_jarvis->projectIndexer()->fileCount() > 0)
        m_jarvis->syncProjectInfoToMemory();

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
    // Wire ScreenshotLearner → CuriosityEngine for visual context
    CuriosityEngine::instance().setScreenshotLearner(m_appLearner);

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
        qDebug() << "[MainWindow] startup deadline check: userId=" << m_jarvis->currentUserId()
                 << "warnings=" << warnings;
        if (!warnings.isEmpty()) {
            notifyDeadlineWarnings(warnings);
            appendLog(IS_EN ? QStringLiteral("J.A.R.V.I.S.") : QStringLiteral("Д.Ж.А.Р.В.И.С."),
                warnings, Theme::LogColors::error);
        }
        // The one-shot summary above already covered whatever's overdue
        // right now — seed the dedup set so the periodic timer below only
        // announces tasks that cross into "overdue" *after* startup.
        for (const auto& t : m_jarvis->getOverdueTasks())
            m_notifiedDeadlineTaskIds.insert(t.id);
    });

    // Periodic deadline re-check — unlike the one-shot startup check above,
    // this keeps running for the life of the session so a task that
    // becomes overdue at 3pm gets flagged even if JARVIS was opened at
    // 9am. Notifies once per task (by id) so a still-overdue task doesn't
    // re-spam a toast every cycle.
    m_deadlineTimer = new QTimer(this);
    connect(m_deadlineTimer, &QTimer::timeout, this, [this]() {
        for (const auto& t : m_jarvis->getOverdueTasks()) {
            if (m_notifiedDeadlineTaskIds.contains(t.id)) continue;
            m_notifiedDeadlineTaskIds.insert(t.id);

            const qint64 secsTo = QDateTime::currentDateTime().secsTo(t.deadline);
            const bool overdue = secsTo < 0;
            const QString timeStr = overdue
                ? (IS_EN ? QStringLiteral("OVERDUE by %1h").arg(qMax<qint64>(-secsTo / 3600, 1))
                         : QStringLiteral("ПРОСРОЧЕНО на %1ч").arg(qMax<qint64>(-secsTo / 3600, 1)))
                : (secsTo < 3600
                    ? (IS_EN ? QStringLiteral("due in %1 min").arg(secsTo / 60)
                             : QStringLiteral("через %1 мин").arg(secsTo / 60))
                    : (IS_EN ? QStringLiteral("due in %1h").arg(secsTo / 3600)
                             : QStringLiteral("через %1 ч").arg(secsTo / 3600)));

            NotificationManager::instance().showNotification(
                IS_EN ? QStringLiteral("Task deadline") : QStringLiteral("Дедлайн задачи"),
                QStringLiteral("%1 (%2) — %3").arg(t.title, t.category, timeStr),
                overdue ? NotificationManager::Level::Error
                        : NotificationManager::Level::Warning);
        }
    });
    m_deadlineTimer->start(20 * 60 * 1000); // every 20 minutes
}

// ============================================================
// applyLanguage
// ============================================================

void MainWindow::applyLanguage(bool english)
{
    gUiLanguage() = english ? UiLanguage::English : UiLanguage::Russian;
    m_jarvis->setUiLanguage(english);
    CuriosityEngine::instance().setUiEnglish(english);
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

        // Edit profile (name, role, language)
        auto* actEdit = userMenu->addAction(
            IS_EN ? QStringLiteral("Edit Profile...") : QStringLiteral("Редактировать профиль..."));
        connect(actEdit, &QAction::triggered, this, [this]() {
            editCurrentUserProfile();
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

    // --- Models & Intelligence ---
    auto* settingsMenu = menuBar->addMenu(
        IS_EN ? QStringLiteral("🤖 Models && Intelligence")
              : QStringLiteral("🤖 Модели и ИИ"));

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

    // ── Модульные скиллы (лего-блоки знаний) ─────────────────
    settingsMenu->addSeparator();
    auto* actSkills = settingsMenu->addAction(
        IS_EN ? QStringLiteral("🧩 JARVIS Skills...")
              : QStringLiteral("🧩 Скиллы JARVIS..."));
    connect(actSkills, &QAction::triggered, this, [this]() {
        SkillsDialog dlg(m_jarvis->skillManager(), this);
        dlg.exec();
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

    // Смена роли перенесена в «Редактировать профиль…» (User menu) —
    // отдельный пункт "Switch Role" дублировал тот же выбор роли и
    // расходился с ним по значению (scenario vs currentRole).

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

    // --- Training Center (QML: stats, app usage, local training, history) ---
    auto* actTrainStats = trainMenu->addAction(
        IS_EN ? QStringLiteral("📊 Dataset Statistics")
              : QStringLiteral("📊 Статистика датасета"));
    connect(actTrainStats, &QAction::triggered, this, [this]() {
        TrainingCenterDialog dlg(m_jarvis->currentUserId(), m_passiveListener, m_appLearner, this, 0);
        dlg.exec();
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
        TrainingCenterDialog dlg(m_jarvis->currentUserId(), m_passiveListener, m_appLearner, this, 2);
        dlg.exec();
    });

    trainMenu->addSeparator();

    // --- Паттерны использования ПК ---
    auto* actAppPatterns = trainMenu->addAction(
        IS_EN ? QStringLiteral("📊 App Usage Patterns...")
              : QStringLiteral("📊 Паттерны использования..."));
    connect(actAppPatterns, &QAction::triggered, this, [this]() {
        TrainingCenterDialog dlg(m_jarvis->currentUserId(), m_passiveListener, m_appLearner, this, 1);
        dlg.exec();
    });

    trainMenu->addSeparator();
    auto* actSearch = trainMenu->addAction(
        IS_EN ? QStringLiteral("🔍 Search chat history...")
              : QStringLiteral("🔍 Поиск по истории чатов..."));
    connect(actSearch, &QAction::triggered, this, [this]() {
        TrainingCenterDialog dlg(m_jarvis->currentUserId(), m_passiveListener, m_appLearner, this, 3);
        dlg.exec();
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

        auto* actChatHistory = taskMenu->addAction(
            IS_EN ? QStringLiteral("Chat History...")
                  : QStringLiteral("История чатов..."));
        connect(actChatHistory, &QAction::triggered, this, [this]() {
            ChatHistoryDialog dlg(m_jarvis->currentUserId(), IS_EN, this);
            dlg.exec();
        });

        auto* actOrganize = taskMenu->addAction(
            IS_EN ? QStringLiteral("🗂 Organize Folder...")
                  : QStringLiteral("🗂 Организовать папку..."));
        connect(actOrganize, &QAction::triggered, this, [this]() {
            const QString startDir =
                QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
            QString folder = QFileDialog::getExistingDirectory(this,
                IS_EN ? QStringLiteral("Choose a folder to organize")
                      : QStringLiteral("Выберите папку для организации"),
                startDir);
            if (folder.isEmpty()) return;

            if (!Jarvis::organizePathAllowed(folder)) {
                appendLog(Str::logJarvis(),
                    IS_EN ? QStringLiteral("❌ This folder isn't in the allowed roots "
                                           "(Downloads/Desktop/Documents/Pictures).")
                          : QStringLiteral("❌ Эта папка вне разрешённых корней "
                                           "(Загрузки/Рабочий стол/Документы/Изображения)."),
                    Theme::LogColors::error);
                return;
            }

            appendLog(Str::logJarvis(),
                IS_EN ? QStringLiteral("🔍 Scanning folder — this may take a moment for ambiguous files...")
                      : QStringLiteral("🔍 Сканирую папку — для неоднозначных файлов это может занять время..."),
                Theme::LogColors::system);

            FileOrganizer::instance().setLlmApi(m_jarvis->claudeApi());
            FileOrganizer::instance().buildPlan(folder, [this](const OrganizePlan& plan) {
                showOrganizePlanDialog(plan);
            });
        });

        auto* actUndoOrganize = taskMenu->addAction(
            IS_EN ? QStringLiteral("↩ Undo Last Organize")
                  : QStringLiteral("↩ Отменить последнюю организацию"));
        connect(actUndoOrganize, &QAction::triggered, this, [this]() {
            const bool ok = m_jarvis->organizeUndoLast();
            appendLog(Str::logJarvis(),
                ok ? (IS_EN ? QStringLiteral("↩ Last organize batch undone.")
                            : QStringLiteral("↩ Последняя организация отменена."))
                   : (IS_EN ? QStringLiteral("Nothing to undo.")
                            : QStringLiteral("Нечего отменять.")),
                Theme::LogColors::system);
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
                NotificationManager::instance().showNotification(
                    IS_EN ? QStringLiteral("Task created") : QStringLiteral("Задача создана"),
                    title.trimmed(), NotificationManager::Level::Success);
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
                notifyDeadlineWarnings(warnings);
                appendLog(IS_EN ? QStringLiteral("J.A.R.V.I.S.") : QStringLiteral("Д.Ж.А.Р.В.И.С."),
                    warnings, Theme::LogColors::error);
            }
        });
    }

    // --- Phone & Server ---
    {
        auto* phoneMenu = menuBar->addMenu(
            IS_EN ? QStringLiteral("📱 Phone && Server")
                  : QStringLiteral("📱 Телефон и Сервер"));

        // --- Mobile Sync (zero-config pairing) ---
        auto* actMobileSync = phoneMenu->addAction(
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
        phoneMenu->addSeparator();
        auto* actWol = phoneMenu->addAction(
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
        phoneMenu->addSeparator();
        auto* actTelegram = phoneMenu->addAction(
            IS_EN ? QStringLiteral("🤖 Telegram QA Gateway...")
                  : QStringLiteral("🤖 Telegram QA Шлюз..."));
        connect(actTelegram, &QAction::triggered, this, [this]() {
            auto* mesh = m_jarvis->meshConnector();
            if (!mesh) return;
            mesh->initTelegramGateway();
            auto* gw = mesh->telegramGateway();
            if (!gw) return;

            // Bind Jarvis core + translation engine for free dialogue and voice
            gw->setJarvisCore(m_jarvis);
            gw->setTranslationEngine(m_jarvis->translationEngine());
            mesh->initMobilePairing();
            gw->setPairingManager(mesh->mobilePairing());

            // Connect diagram pipeline to desktop dashboard
            connect(gw, &J2JTelegramGateway::diagramGenerated, this,
                    [this](const QImage& img) {
                m_visualInsights->showDiagram(img);
            }, Qt::UniqueConnection);

            // Sync Telegram conversation into the desktop log —
            // so the user sees both channels in one place and all
            // messages feed into the same learning pipeline.
            connect(gw, &J2JTelegramGateway::messageReceived, this,
                    [this](qint64 /*chatId*/, const QString& text) {
                appendLog(QStringLiteral("📱 TG User"),
                          text.left(500),
                          QStringLiteral("#2ea6c7"));
            }, Qt::UniqueConnection);

            connect(gw, &J2JTelegramGateway::conversationResponse, this,
                    [this](qint64 /*chatId*/, const QString& response) {
                appendLog(QStringLiteral("📱 JARVIS→TG"),
                          response.left(500),
                          Theme::LogColors::jarvis);
            }, Qt::UniqueConnection);

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

            // Roles — lets whoever is sitting at THIS PC fix a chat that
            // isn't Admin (e.g. it wasn't the very first chat to ever
            // message this PC's bot). Only reachable from the desktop app.
            auto* rolesBtn = new QPushButton(
                IS_EN ? QStringLiteral("👑 Manage Roles...")
                      : QStringLiteral("👑 Роли пользователей..."));
            connect(rolesBtn, &QPushButton::clicked, dlg, [this, gw]() {
                auto* accessMgr = gw->accessManager();
                if (!accessMgr) return;

                auto* rdlg = new QDialog(this);
                rdlg->setWindowTitle(IS_EN ? QStringLiteral("Telegram Roles")
                                          : QStringLiteral("Роли Telegram"));
                rdlg->setAttribute(Qt::WA_DeleteOnClose);
                rdlg->setMinimumSize(420, 300);
                auto* rlayout = new QVBoxLayout(rdlg);

                const auto users = accessMgr->allUsers();
                if (users.isEmpty()) {
                    rlayout->addWidget(new QLabel(IS_EN
                        ? QStringLiteral("No chats have messaged this bot yet.")
                        : QStringLiteral("Пока ни один чат не писал этому боту.")));
                }
                for (const auto& u : users) {
                    auto* row = new QWidget(rdlg);
                    auto* rowLay = new QHBoxLayout(row);
                    rowLay->setContentsMargins(0, 0, 0, 0);
                    const QString name = u.displayName.isEmpty()
                        ? QString::number(u.chatId) : u.displayName;
                    auto* lbl = new QLabel(QStringLiteral("%1  —  %2")
                        .arg(name, telegramRoleToString(u.role)));
                    lbl->setStyleSheet(QStringLiteral("color:#c0c8d8;"));
                    rowLay->addWidget(lbl, 1);

                    auto* makeAdminBtn = new QPushButton(
                        IS_EN ? QStringLiteral("Make Admin") : QStringLiteral("Сделать Admin"));
                    makeAdminBtn->setEnabled(u.role != TelegramRole::Admin);
                    const qint64 cid = u.chatId;
                    connect(makeAdminBtn, &QPushButton::clicked, rdlg,
                            [this, accessMgr, cid, makeAdminBtn]() {
                        accessMgr->setRole(cid, TelegramRole::Admin);
                        makeAdminBtn->setEnabled(false);
                        appendLog(QStringLiteral("J.A.R.V.I.S."),
                            IS_EN ? QStringLiteral("👑 Chat %1 promoted to Admin.").arg(cid)
                                  : QStringLiteral("👑 Чат %1 повышен до Admin.").arg(cid),
                            QStringLiteral("#66FCF1"));
                    });
                    rowLay->addWidget(makeAdminBtn);
                    rlayout->addWidget(row);
                }
                rlayout->addStretch(1);
                rdlg->show();
            });
            layout->addWidget(rolesBtn);

            // Bug report notifications
            connect(gw, &J2JTelegramGateway::bugReportFiled, dlg,
                    [this](const QaBugReport& report) {
                appendLog(QStringLiteral("J.A.R.V.I.S."),
                    QStringLiteral("🐛 QA Bug: [%1] %2 — %3")
                        .arg(report.severity, report.title, report.reporterRole),
                    QStringLiteral("#ff9800"));
            });

            // Pairing success → update status in UI
            connect(gw, &J2JTelegramGateway::pairingCompleted, dlg,
                    [this, statusLbl](qint64 chatId, const QString& role) {
                statusLbl->setText(QStringLiteral("🟢 PAIRED — chat %1 → %2").arg(chatId).arg(role));
                statusLbl->setStyleSheet(QStringLiteral("color: #66FCF1; font-weight: bold;"));
                appendLog(QStringLiteral("J.A.R.V.I.S."),
                    (IS_EN ? QStringLiteral("📱 Mobile paired via Telegram → role: %1")
                           : QStringLiteral("📱 Мобильный подключён через Telegram → роль: %1"))
                    .arg(role),
                    QStringLiteral("#66FCF1"));
            });

            layout->addStretch(1);
            dlg->show();
        });
    }

    // --- System ---
    {
        auto* sysMenu = menuBar->addMenu(
            IS_EN ? QStringLiteral("⚙ System") : QStringLiteral("⚙ Система"));

        auto* actKeepAttach = sysMenu->addAction(Str::menuKeepAttach());
        actKeepAttach->setCheckable(true);
        actKeepAttach->setChecked(false);
        connect(actKeepAttach, &QAction::toggled, this, [this](bool checked) {
            m_jarvis->attachments()->setKeepAfterSend(checked);
            appendLog(Str::logSystem(),
                      checked ? Str::statusAttachKept() : Str::statusAttachOneShot(),
                      Theme::LogColors::system);
        });

        auto* actKeyboard = sysMenu->addAction(Str::menuKeyboard());
        connect(actKeyboard, &QAction::triggered, this, &MainWindow::toggleKeyboard);

        sysMenu->addSeparator();

        // Translation pair
        auto* transMenu = sysMenu->addMenu(
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
        auto* transGroup = new QActionGroup(transMenu);
        transGroup->setExclusive(true);
        for (const auto& p : pairs) {
            auto* act = transMenu->addAction(p.label);
            act->setCheckable(true);
            if (p.src == QStringLiteral("fr") && p.tgt == QStringLiteral("en"))
                act->setChecked(true);
            transGroup->addAction(act);
            const QString src = p.src, tgt = p.tgt;
            connect(act, &QAction::triggered, this, [this, src, tgt]() {
                m_jarvis->translationEngine()->setSourceLang(src);
                m_jarvis->translationEngine()->setTargetLang(tgt);
                appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                    QStringLiteral("Translation pair: %1 → %2").arg(src.toUpper(), tgt.toUpper()),
                    Theme::LogColors::system);
            });
        }

        auto* actTranslate = sysMenu->addAction(
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

        auto* actAudioTranslate = sysMenu->addAction(
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

        sysMenu->addSeparator();

        // Analytics
        auto* actAnalytics = sysMenu->addAction(
            IS_EN ? QStringLiteral("📊 User Analytics")
                  : QStringLiteral("📊 Аналитика"));
        connect(actAnalytics, &QAction::triggered, this, [this]() {
            auto dbConn = QSqlDatabase::database(QStringLiteral("jarvis_main"));
            if (!dbConn.isOpen()) return;
            QSqlQuery q(dbConn);
            q.exec(QStringLiteral(
                "SELECT chat_id, action_type, COUNT(*) as cnt "
                "FROM activity_log_tg GROUP BY chat_id, action_type "
                "ORDER BY chat_id, cnt DESC"));
            QString report = IS_EN
                ? QStringLiteral("📊 <b>User Analytics</b><br><br>")
                : QStringLiteral("📊 <b>Аналитика</b><br><br>");
            QMap<qint64, QMap<QString, int>> userData;
            while (q.next())
                userData[q.value(0).toLongLong()][q.value(1).toString()] = q.value(2).toInt();
            if (userData.isEmpty()) {
                report += IS_EN ? QStringLiteral("<i>No data yet.</i>")
                                : QStringLiteral("<i>Данных пока нет.</i>");
            } else {
                for (auto it = userData.begin(); it != userData.end(); ++it) {
                    report += QStringLiteral("<b>Chat %1</b><br>").arg(it.key());
                    for (auto jt = it.value().begin(); jt != it.value().end(); ++jt)
                        report += QStringLiteral("  • %1: %2<br>").arg(jt.key()).arg(jt.value());
                    report += QStringLiteral("<br>");
                }
            }
            auto* dlg = new QDialog(this);
            dlg->setWindowTitle(IS_EN ? QStringLiteral("Analytics") : QStringLiteral("Аналитика"));
            dlg->setMinimumSize(480, 360);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            auto* lay = new QVBoxLayout(dlg);
            auto* browser = new QTextEdit(dlg);
            browser->setReadOnly(true);
            browser->setHtml(report);
            lay->addWidget(browser);
            auto* closeBtn = new QPushButton(QStringLiteral("OK"), dlg);
            connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::accept);
            lay->addWidget(closeBtn);
            dlg->show();
        });

        sysMenu->addSeparator();

        // Updates (moved from standalone menu)
        auto* actCheck = sysMenu->addAction(Str::menuCheckUpdate());
        connect(actCheck, &QAction::triggered, this, [this]() {
            appendLog(Str::logSystem(), Str::updChecking(), Theme::LogColors::system);
            m_jarvis->autoUpdater()->checkForUpdates(false);
        });

        auto* actReleases = sysMenu->addAction(Str::menuReleasePage());
        connect(actReleases, &QAction::triggered, this, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Bohdan99py/jarvis/releases")));
        });
    }

    // --- Помощь ---
    // ── Camera & Security ──────────────────────────────────
    {
        // Shared instance owned by Jarvis — the Telegram /security command
        // arms the SAME object, so a real motion event can't produce two
        // independent alerts from two cameras watching the same webcam.
        m_securityCam = m_jarvis->securityCamera();

        auto* camMenu = menuBar->addMenu(
            IS_EN ? QStringLiteral("📷 Camera") : QStringLiteral("📷 Камера"));

#ifdef JARVIS_HAS_OPENCV
        // ── Face enrollment ────────────────────────────────
        auto* actEnroll = camMenu->addAction(
            IS_EN ? QStringLiteral("👤 Enroll Owner Face")
                  : QStringLiteral("👤 Обучить лицо владельца"));
        connect(actEnroll, &QAction::triggered, this, [this]() {
            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("📸 Starting face enrollment... Look at the webcam.")
                      : QStringLiteral("📸 Обучение лица... Смотрите в камеру."),
                Theme::LogColors::system);

            auto* sec = new SecurityCamera(this);

            // Идентичность владельца для FaceRegistry: имя из профиля,
            // возраст опционально, статус = "владелец".
            {
                auto user = DatabaseManager::instance().getUser(m_jarvis->currentUserId());
                const QString ownerName = user ? user->name : QString();
                bool ok = false;
                const int age = QInputDialog::getInt(this,
                    IS_EN ? QStringLiteral("Age (optional)") : QStringLiteral("Возраст (необязательно)"),
                    IS_EN ? QStringLiteral("Your age (0 = skip):") : QStringLiteral("Ваш возраст (0 = пропустить):"),
                    0, 0, 120, 1, &ok);
                sec->setOwnerIdentity(ownerName, ok ? age : 0,
                    IS_EN ? QStringLiteral("owner") : QStringLiteral("владелец"));
            }

            connect(sec, &SecurityCamera::enrollmentProgress, this,
                    [this](int cur, int total) {
                appendLog(Str::logSystem(),
                    QStringLiteral("📸 %1/%2").arg(cur).arg(total),
                    Theme::LogColors::system);
            });
            connect(sec, &SecurityCamera::enrollmentComplete, this,
                    [this, sec](int samples) {
                appendLog(Str::logJarvis(),
                    (IS_EN ? QStringLiteral("✅ Face enrolled! %1 samples. "
                                            "I can now recognize you.")
                           : QStringLiteral("✅ Лицо обучено! %1 образцов. "
                                            "Теперь я вас узнаю.")).arg(samples),
                    Theme::LogColors::jarvis);
                // Делимся профилем лица с другими узлами JARVIS (P2P):
                // их камеры тоже будут узнавать владельца этого ПК.
                if (auto* mesh = m_jarvis->meshConnector())
                    mesh->broadcastFaceProfiles();
                sec->deleteLater();
            });
            connect(sec, &SecurityCamera::alertMessage, this,
                    [this, sec](const QString& msg) {
                appendLog(Str::logSystem(), msg, Theme::LogColors::error);
                sec->deleteLater();
            });
            sec->enrollOwnerFace(15);
        });

        auto* actUpdateFace = camMenu->addAction(
            IS_EN ? QStringLiteral("🔄 Update Face (new look)")
                  : QStringLiteral("🔄 Обновить лицо (новый вид)"));
        connect(actUpdateFace, &QAction::triggered, this, [this]() {
            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("📸 Re-enrolling face with new appearance...")
                      : QStringLiteral("📸 Переобучение лица с новым видом..."),
                Theme::LogColors::system);

            auto* sec = new SecurityCamera(this);
            connect(sec, &SecurityCamera::enrollmentComplete, this,
                    [this, sec](int samples) {
                appendLog(Str::logJarvis(),
                    (IS_EN ? QStringLiteral("✅ Face updated! %1 new samples.")
                           : QStringLiteral("✅ Лицо обновлено! %1 новых образцов.")).arg(samples),
                    Theme::LogColors::jarvis);
                sec->deleteLater();
            });
            connect(sec, &SecurityCamera::alertMessage, this,
                    [this, sec](const QString& msg) {
                appendLog(Str::logSystem(), msg, Theme::LogColors::error);
                sec->deleteLater();
            });
            sec->enrollOwnerFace(10);
        });

        // ── Face enrollment from uploaded photos ───────────
        auto* actEnrollFromPhoto = camMenu->addAction(
            IS_EN ? QStringLiteral("🖼 Learn Face from Photo(s)...")
                  : QStringLiteral("🖼 Обучить лицо по фото..."));
        connect(actEnrollFromPhoto, &QAction::triggered, this, [this]() {
            const QStringList files = QFileDialog::getOpenFileNames(this,
                IS_EN ? QStringLiteral("Select photo(s) with a face")
                      : QStringLiteral("Выберите фото с лицом"),
                QDir::homePath(),
                QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp)"));
            if (files.isEmpty()) return;

            bool ok = false;
            const QString name = QInputDialog::getText(this,
                IS_EN ? QStringLiteral("Name") : QStringLiteral("Имя"),
                IS_EN ? QStringLiteral("Whose face is this?")
                      : QStringLiteral("Чьё это лицо?"),
                QLineEdit::Normal, QString(), &ok);
            if (!ok || name.trimmed().isEmpty()) return;

            const int age = QInputDialog::getInt(this,
                IS_EN ? QStringLiteral("Age (optional)") : QStringLiteral("Возраст (необязательно)"),
                IS_EN ? QStringLiteral("Age (0 = skip):") : QStringLiteral("Возраст (0 = пропустить):"),
                0, 0, 120, 1, &ok);

            const bool isOwner = QMessageBox::question(this,
                IS_EN ? QStringLiteral("Owner?") : QStringLiteral("Владелец?"),
                IS_EN ? QStringLiteral("Is this the PC owner (enables auto-lock/unlock)?")
                      : QStringLiteral("Это владелец ПК (включает авто-блокировку по лицу)?"),
                QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;

            const QString status = isOwner
                ? (IS_EN ? QStringLiteral("owner") : QStringLiteral("владелец"))
                : (IS_EN ? QStringLiteral("known") : QStringLiteral("знакомый"));

            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("🖼 Learning face from %1 photo(s)...").arg(files.size())
                      : QStringLiteral("🖼 Обучаю лицо по %1 фото...").arg(files.size()),
                Theme::LogColors::system);

            auto* sec = new SecurityCamera(this);
            connect(sec, &SecurityCamera::enrollmentComplete, this,
                    [this, sec](int samples) {
                appendLog(Str::logJarvis(),
                    (IS_EN ? QStringLiteral("✅ Learned from %1 photo(s).")
                           : QStringLiteral("✅ Обучено по %1 фото.")).arg(samples),
                    Theme::LogColors::jarvis);
                if (auto* mesh = m_jarvis->meshConnector())
                    mesh->broadcastFaceProfiles();
                sec->deleteLater();
            });
            connect(sec, &SecurityCamera::alertMessage, this,
                    [this, sec](const QString& msg) {
                appendLog(Str::logSystem(), msg, Theme::LogColors::error);
                sec->deleteLater();
            });
            sec->enrollFaceFromImages(files, name.trimmed(), age, status, isOwner);
        });

        camMenu->addSeparator();

        // ── Live View: кто перед камерой ───────────────────
        auto* actWhoIsThere = camMenu->addAction(
            IS_EN ? QStringLiteral("👁 Who's on Camera?")
                  : QStringLiteral("👁 Кто перед камерой?"));
        connect(actWhoIsThere, &QAction::triggered, this, [this]() {
            SecurityCamera* sec = m_securityCam
                ? m_securityCam : new SecurityCamera(this);
            const bool temporary = (sec != m_securityCam);

            const QImage frame = sec->snapshotFullRes();
            if (frame.isNull()) {
                appendLog(Str::logSystem(),
                    IS_EN ? QStringLiteral("📷 Camera unavailable.")
                          : QStringLiteral("📷 Камера недоступна."),
                    Theme::LogColors::error);
                if (temporary) sec->deleteLater();
                return;
            }

            const auto faces = sec->identifyFaces(frame);
            const QImage annotated = SecurityCamera::annotateFaces(frame, faces);

            for (const auto& obs : faces) {
                appendLog(QStringLiteral("📷 Камера"),
                    (obs.known ? QStringLiteral("👤 ") : QStringLiteral("❓ "))
                    + obs.label(),
                    obs.known ? Theme::LogColors::jarvis : Theme::LogColors::error);
            }
            if (faces.isEmpty()) {
                appendLog(QStringLiteral("📷 Камера"),
                    IS_EN ? QStringLiteral("No faces in frame.")
                          : QStringLiteral("Лиц в кадре нет."),
                    Theme::LogColors::system);
            }

            // Показать аннотированный кадр
            auto* dlg = new QDialog(this);
            dlg->setWindowTitle(IS_EN ? QStringLiteral("Camera — Live View")
                                      : QStringLiteral("Камера — кто в кадре"));
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            auto* lay = new QVBoxLayout(dlg);
            auto* imgLabel = new QLabel(dlg);
            imgLabel->setPixmap(QPixmap::fromImage(
                annotated.scaledToWidth(qMin(900, annotated.width()),
                                        Qt::SmoothTransformation)));
            lay->addWidget(imgLabel);
            dlg->show();

            if (temporary) sec->deleteLater();
        });

        camMenu->addSeparator();

        // ── Security guard ON / OFF ────────────────────────
        auto* actGuardOn = camMenu->addAction(
            IS_EN ? QStringLiteral("🛡 Start Security Guard")
                  : QStringLiteral("🛡 Включить охрану"));
        auto* actGuardOff = camMenu->addAction(
            IS_EN ? QStringLiteral("🔴 Stop Security Guard")
                  : QStringLiteral("🔴 Выключить охрану"));
        actGuardOff->setEnabled(false);

        // ── Lock / Unlock manually ─────────────────────────
        camMenu->addSeparator();
        auto* actLockNow = camMenu->addAction(
            IS_EN ? QStringLiteral("🔒 Lock Screen Now")
                  : QStringLiteral("🔒 Заблокировать экран"));
        actLockNow->setEnabled(false);

        auto* actUnlockNow = camMenu->addAction(
            IS_EN ? QStringLiteral("🔓 Unlock Screen")
                  : QStringLiteral("🔓 Разблокировать экран"));
        actUnlockNow->setEnabled(false);

        // ── Toggle options ─────────────────────────────────
        camMenu->addSeparator();
        auto* actAutoLock = camMenu->addAction(
            IS_EN ? QStringLiteral("Auto-lock when owner leaves")
                  : QStringLiteral("Авто-блокировка при уходе"));
        actAutoLock->setCheckable(true);
        actAutoLock->setChecked(true);

        auto* actAutoUnlock = camMenu->addAction(
            IS_EN ? QStringLiteral("Auto-unlock on owner face")
                  : QStringLiteral("Авто-разблокировка по лицу"));
        actAutoUnlock->setCheckable(true);
        actAutoUnlock->setChecked(true);

        auto* actMotionAlert = camMenu->addAction(
            IS_EN ? QStringLiteral("Motion alerts + video")
                  : QStringLiteral("Оповещения о движении + видео"));
        actMotionAlert->setCheckable(true);
        actMotionAlert->setChecked(true);

        auto* actUnknownAlert = camMenu->addAction(
            IS_EN ? QStringLiteral("Alert on unknown faces")
                  : QStringLiteral("Оповещение о чужих лицах"));
        actUnknownAlert->setCheckable(true);
        actUnknownAlert->setChecked(true);

        // ── Check now ──────────────────────────────────────
        camMenu->addSeparator();
        auto* actCheckNow = camMenu->addAction(
            IS_EN ? QStringLiteral("👁 Check Now")
                  : QStringLiteral("👁 Проверить сейчас"));
        actCheckNow->setEnabled(false);

        // ── Helper: create lock overlay once ───────────────
        // Blocks Alt+Tab, Alt+F4, Win key, Ctrl+Esc, Task Manager.
        // Emergency unlock: Escape 5 times within 2 seconds (secret).
        class LockOverlayWidget : public QWidget {
        public:
            using QWidget::QWidget;
            void activate() {
                showFullScreen();
                raise();
                activateWindow();
                setFocus();
                grabKeyboard();
                grabMouse();
                // Re-grab periodically — OS can steal focus
                if (!m_refocusTimer) {
                    m_refocusTimer = new QTimer(this);
                    connect(m_refocusTimer, &QTimer::timeout, this, [this]() {
                        if (!isVisible()) return;
                        raise();
                        activateWindow();
                        setFocus();
                        grabKeyboard();
                        grabMouse();
                    });
                }
                m_refocusTimer->start(500);
            }
            void deactivate() {
                if (m_refocusTimer) m_refocusTimer->stop();
                releaseKeyboard();
                releaseMouse();
                hide();
            }
        protected:
            void keyPressEvent(QKeyEvent* e) override {
                if (e->key() == Qt::Key_Escape) {
                    const qint64 now = QDateTime::currentMSecsSinceEpoch();
                    m_escTimes.append(now);
                    while (!m_escTimes.isEmpty() && now - m_escTimes.first() > 2000)
                        m_escTimes.removeFirst();
                    if (m_escTimes.size() >= 5) {
                        m_escTimes.clear();
                        deactivate();
                        return;
                    }
                }
                // Swallow ALL keys — no Alt+Tab, Alt+F4, Ctrl+Esc
                e->accept();
            }
            void closeEvent(QCloseEvent* e) override {
                e->ignore(); // prevent Alt+F4
            }
            bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override {
                Q_UNUSED(eventType)
                // Block Win key (WM_HOTKEY from RegisterHotKey not needed,
                // grabKeyboard already captures most keys)
                auto* msg = static_cast<MSG*>(message);
                if (msg->message == WM_SYSCOMMAND) {
                    *result = 0;
                    return true; // block system commands (Alt+Space, etc.)
                }
                return false;
            }
        private:
            QList<qint64> m_escTimes;
            QTimer* m_refocusTimer = nullptr;
        };

        auto* overlay = new LockOverlayWidget(nullptr);
        overlay->setWindowFlags(
            Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
            | Qt::X11BypassWindowManagerHint);
        overlay->setStyleSheet(QStringLiteral("background: black;"));
        auto* lockLayout = new QVBoxLayout(overlay);
        lockLayout->setAlignment(Qt::AlignCenter);
        auto* lockIcon = new QLabel(QStringLiteral("🔒"), overlay);
        lockIcon->setAlignment(Qt::AlignCenter);
        lockIcon->setStyleSheet(QStringLiteral("font-size: 72px; background: transparent;"));
        auto* lockTitle = new QLabel(QStringLiteral("J.A.R.V.I.S. LOCKED"), overlay);
        lockTitle->setAlignment(Qt::AlignCenter);
        lockTitle->setStyleSheet(QStringLiteral(
            "color: #00ffcc; font-size: 36px; font-weight: bold; background: transparent;"));
        lockLayout->addWidget(lockIcon);
        lockLayout->addWidget(lockTitle);
        overlay->hide();
        m_lockOverlay = overlay;

        // ── Start guard ────────────────────────────────────
        connect(actGuardOn, &QAction::triggered, this,
                [this, actGuardOn, actGuardOff, actLockNow, actUnlockNow, actCheckNow]() {
            if (m_securityCam->isMonitoring()) return;

            // m_securityCam is a persistent shared instance (owned by
            // Jarvis, also armable via Telegram's /security) — wire these
            // desktop-only UI connections only once, ever, so re-arming
            // via Guard On doesn't stack duplicate signal connections
            // (which previously caused e.g. the motion-clip video being
            // sent to Telegram twice per event).
            if (!m_guardUiWired) {
            m_guardUiWired = true;

            connect(m_securityCam, &SecurityCamera::requestLockOverlay, this, [this]() {
                static_cast<LockOverlayWidget*>(m_lockOverlay)->activate();
            });

            connect(m_securityCam, &SecurityCamera::requestUnlockOverlay, this, [this]() {
                static_cast<LockOverlayWidget*>(m_lockOverlay)->deactivate();
            });

            connect(m_securityCam, &SecurityCamera::alertMessage, this,
                    [this](const QString& msg) {
                appendLog(QStringLiteral("🛡 Security"), msg, Theme::LogColors::error);
            });

            connect(m_securityCam, &SecurityCamera::ownerRecognized, this,
                    [this](const QImage&) {
                appendLog(QStringLiteral("🛡 Security"),
                    IS_EN ? QStringLiteral("👤 Owner recognized")
                          : QStringLiteral("👤 Владелец распознан"),
                    Theme::LogColors::jarvis);
            });

            // Идентифицированные лица (включая профили с других узлов P2P)
            connect(m_securityCam, &SecurityCamera::facesIdentified, this,
                    [this](const QImage&, const QList<FaceObservation>& faces) {
                for (const auto& obs : faces) {
                    if (!obs.known) continue;
                    appendLog(QStringLiteral("🛡 Security"),
                        QStringLiteral("👤 ") + obs.label(),
                        Theme::LogColors::jarvis);
                }
            });

            connect(m_securityCam, &SecurityCamera::motionVideoReady, this,
                    [this](const QString& videoPath) {
                appendLog(QStringLiteral("🛡 Security"),
                    QStringLiteral("📹 Motion clip: %1").arg(videoPath),
                    Theme::LogColors::system);
                auto* mesh = m_jarvis->meshConnector();
                if (!mesh) return;
                auto* gw = mesh->telegramGateway();
                if (!gw) return;
                auto* accessMgr = gw->accessManager();
                if (!accessMgr) return;
                const qint64 ownerChat = accessMgr->primaryOwnerChatId();
                if (ownerChat == 0) return;
                gw->sendVideoToMobile(ownerChat, videoPath,
                    QStringLiteral("🚨 Motion detected! 20s security clip"));
                appendLog(QStringLiteral("🛡 Security"),
                    QStringLiteral("📤 Video sent to Telegram"),
                    Theme::LogColors::jarvis);
            });
            } // !m_guardUiWired

            m_securityCam->startMonitoring(60);

            actGuardOn->setEnabled(false);
            actGuardOff->setEnabled(true);
            actLockNow->setEnabled(true);
            actUnlockNow->setEnabled(true);
            actCheckNow->setEnabled(true);

            appendLog(Str::logJarvis(),
                IS_EN ? QStringLiteral("🛡 Security guard active (1 min interval). "
                                       "Auto-lock/unlock by face. Video alerts → Telegram.")
                      : QStringLiteral("🛡 Охрана включена (интервал 1 мин). "
                                       "Авто-блокировка/разблокировка по лицу. Видео → Telegram."),
                Theme::LogColors::jarvis);
        });

        // ── Stop guard ─────────────────────────────────────
        connect(actGuardOff, &QAction::triggered, this,
                [this, actGuardOn, actGuardOff, actLockNow, actUnlockNow, actCheckNow]() {
            if (!m_securityCam->isMonitoring()) return;
            // Stop, but don't destroy — m_securityCam is the shared
            // instance owned by Jarvis; Telegram's /security may still
            // need it, and Guard On re-arms the same object next time.
            m_securityCam->stopMonitoring();
            static_cast<LockOverlayWidget*>(m_lockOverlay)->deactivate();

            actGuardOn->setEnabled(true);
            actGuardOff->setEnabled(false);
            actLockNow->setEnabled(false);
            actUnlockNow->setEnabled(false);
            actCheckNow->setEnabled(false);

            appendLog(Str::logJarvis(),
                IS_EN ? QStringLiteral("🔴 Security guard stopped.")
                      : QStringLiteral("🔴 Охрана выключена."),
                Theme::LogColors::jarvis);
        });

        // ── Manual lock / unlock ───────────────────────────
        connect(actLockNow, &QAction::triggered, this, [this]() {
            if (m_securityCam) m_securityCam->lockScreen();
        });
        connect(actUnlockNow, &QAction::triggered, this, [this]() {
            if (!m_securityCam) return;
            static_cast<LockOverlayWidget*>(m_lockOverlay)->deactivate();
            appendLog(QStringLiteral("🛡 Security"),
                IS_EN ? QStringLiteral("🔓 Screen unlocked manually")
                      : QStringLiteral("🔓 Экран разблокирован вручную"),
                Theme::LogColors::jarvis);
        });

        // ── Toggle settings ────────────────────────────────
        connect(actAutoLock, &QAction::toggled, this, [this](bool v) {
            if (m_securityCam) m_securityCam->setAutoLockOnThreat(v);
        });
        connect(actAutoUnlock, &QAction::toggled, this, [this](bool v) {
            if (m_securityCam) m_securityCam->setAutoUnlock(v);
        });
        connect(actMotionAlert, &QAction::toggled, this, [this](bool v) {
            if (m_securityCam) m_securityCam->setAlertOnMotion(v);
        });
        connect(actUnknownAlert, &QAction::toggled, this, [this](bool v) {
            if (m_securityCam) m_securityCam->setAlertOnUnknownFace(v);
        });

        // ── Check now ──────────────────────────────────────
        connect(actCheckNow, &QAction::triggered, this, [this]() {
            if (m_securityCam) {
                m_securityCam->checkNow();
                appendLog(QStringLiteral("🛡 Security"),
                    IS_EN ? QStringLiteral("👁 Manual check triggered")
                          : QStringLiteral("👁 Ручная проверка запущена"),
                    Theme::LogColors::system);
            }
        });
#else
        auto* actNoCV = camMenu->addAction(
            IS_EN ? QStringLiteral("⚠ OpenCV not installed")
                  : QStringLiteral("⚠ OpenCV не установлен"));
        actNoCV->setEnabled(false);
#endif

        camMenu->addSeparator();

        auto* actScreenshot = camMenu->addAction(
            IS_EN ? QStringLiteral("📸 Take Screenshot")
                  : QStringLiteral("📸 Сделать скриншот"));
        connect(actScreenshot, &QAction::triggered, this, [this]() {
            QScreen* screen = QApplication::primaryScreen();
            if (!screen) return;
            QPixmap shot = screen->grabWindow(0);
            const QString dir = JarvisPaths::subPath(QStringLiteral("screenshots"));
            QDir().mkpath(dir);
            const QString path = dir + QStringLiteral("/screenshot_%1.png")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
            shot.save(path, "PNG");
            appendLog(Str::logJarvis(),
                (IS_EN ? QStringLiteral("📸 Screenshot saved: ") : QStringLiteral("📸 Скриншот сохранён: ")) + path,
                Theme::LogColors::jarvis);
        });
    }

    auto* helpMenu = menuBar->addMenu(Str::menuHelp());

    auto* actComponents = helpMenu->addAction(
        IS_EN ? QStringLiteral("📦 Component Manager")
              : QStringLiteral("📦 Менеджер компонентов"));
    connect(actComponents, &QAction::triggered, this, [this]() {
        auto* dlg = new DependencyManagerDialog(this);
        dlg->show();
    });

    helpMenu->addSeparator();

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

    helpMenu->addSeparator();

    // --- Инструкция (auto-generated from active modules) ---
    auto* actHelp = helpMenu->addAction(
        IS_EN ? QStringLiteral("📖 User Guide") : QStringLiteral("📖 Руководство пользователя"));
    connect(actHelp, &QAction::triggered, this, [this]() {
        auto* dlg = new QDialog(this);
        dlg->setWindowTitle(IS_EN ? QStringLiteral("J.A.R.V.I.S. — System Manual")
                                  : QStringLiteral("J.A.R.V.I.S. — Системное руководство"));
        dlg->setMinimumSize(780, 560);
        dlg->setAttribute(Qt::WA_DeleteOnClose);

        // DYNAMIC MANUAL — replaces 400 lines of hardcoded sections
        dlg->setStyleSheet(QStringLiteral(
            "QDialog { background: #0B0C10; color: #C5C6C7; }"
            "QTextBrowser { background: rgba(11,12,16,220); color: #C5C6C7; "
            "  border: 1px solid rgba(102,252,241,0.10); border-radius: 8px; padding: 12px; "
            "  font-family: 'Segoe UI', sans-serif; font-size: 13px; }"
            "QPushButton { background: rgba(102,252,241,0.08); color: #66FCF1; "
            "  border: 1px solid rgba(102,252,241,0.18); padding: 7px 28px; border-radius: 6px; }"
            "QPushButton:hover { background: rgba(102,252,241,0.15); }"));

        auto* layout = new QVBoxLayout(dlg);
        layout->setContentsMargins(16, 14, 16, 14);

        auto* browser = new QTextBrowser(dlg);
        browser->setOpenExternalLinks(true);
        browser->setHtml(
            SelfUpdateReflector::instance().buildDynamicManualHtml(IS_EN));
        layout->addWidget(browser, 1);

        auto* btnClose = new QPushButton(QStringLiteral("OK"), dlg);
        btnClose->setFixedWidth(120);
        connect(btnClose, &QPushButton::clicked, dlg, &QDialog::accept);
        auto* btnRow = new QHBoxLayout();
        btnRow->addStretch();
        btnRow->addWidget(btnClose);
        btnRow->addStretch();
        layout->addLayout(btnRow);

        dlg->exec();
    });

}

// ============================================================
// LEGACY_MANUAL_REMOVED — the following static content has been
// replaced by SelfUpdateReflector::buildDynamicManualHtml()
// which auto-generates the manual from active module introspection.
// ============================================================

#if 0  // DEAD CODE — kept for reference during transition
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
#endif

// ============================================================
// Events
// ============================================================

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_input && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && !(ke->modifiers() & Qt::ShiftModifier)) {
            onSend();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

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
    QString text = m_input->toPlainText().trimmed();

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

    // ── 0.2 Praise/scold: typed confirm/deny for the last uncertain
    // answer ───────────────────────────────────────────────────────
    // Safety net alongside the clarify-bar buttons (see
    // onClarificationChoice, "doubt_feedback:" prefix) for when the
    // user just talks instead of clicking. Only fires on a short reply
    // within a few minutes of the doubt being raised, so it can't
    // swallow an unrelated later message.
    if (m_pendingDoubtId != 0
        && m_pendingDoubtSetAt.secsTo(QDateTime::currentDateTime()) <= 15 * 60)
    {
        static const QStringList praiseMarkers = {
            QStringLiteral("да"),        QStringLiteral("верно"),
            QStringLiteral("точно"),     QStringLiteral("правильно"),
            QStringLiteral("молодец"),   QStringLiteral("так и есть"),
            QStringLiteral("именно"),    QStringLiteral("yes"),
            QStringLiteral("correct"),   QStringLiteral("right"),
            QStringLiteral("exactly"),
        };
        static const QStringList scoldMarkers = {
            QStringLiteral("нет"),         QStringLiteral("неверно"),
            QStringLiteral("не верно"),    QStringLiteral("не так"),
            QStringLiteral("неправильно"), QStringLiteral("ошибся"),
            QStringLiteral("ошиблась"),    QStringLiteral("мимо"),
            QStringLiteral("no"),          QStringLiteral("wrong"),
            QStringLiteral("incorrect"),
        };

        const QString lo = text.toLower();
        const int wordCount = lo.split(QRegularExpression(QStringLiteral("\\s+")),
                                        Qt::SkipEmptyParts).size();
        if (wordCount <= 4) {
            bool isPraise = false, isScold = false;
            for (const auto& m : praiseMarkers)
                if (lo.startsWith(m)) { isPraise = true; break; }
            if (!isPraise)
                for (const auto& m : scoldMarkers)
                    if (lo.startsWith(m)) { isScold = true; break; }

            if (isPraise || isScold) {
                SelfJournal::instance().resolveDoubt(m_pendingDoubtId, isPraise);
                m_pendingDoubtId = 0;
                appendLog(Str::logSender(), text, Theme::LogColors::user);
                appendLog(Str::logJarvis(),
                    isPraise
                        ? (IS_EN ? QStringLiteral("Good to know — I'll trust that one more next time.")
                                 : QStringLiteral("Понял, учту — в следующий раз буду увереннее."))
                        : (IS_EN ? QStringLiteral("Thanks for the correction — I'll be more careful with that one.")
                                 : QStringLiteral("Спасибо, что поправил — учту это на будущее.")),
                    Theme::LogColors::jarvis);
                hideClarification();
                m_input->setFocus();
                return;
            }
        }
    }

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
    // Only unambiguous screen-interaction phrases — NOT "найди" alone
    // (which could mean "найди работу", "найди файл", etc.)
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
        for (const QString& t : kVisualTriggers) {
            if (!lo.contains(t)) continue;
            // "найди на экране" must start at position 0 or after whitespace —
            // don't match "найди" inside "найди работу" or "найди файл"
            if (t == QStringLiteral("найди на экране")) {
                const int pos = lo.indexOf(t);
                if (pos == 0 || (pos > 0 && lo[pos - 1].isSpace()))
                    isVisual = true;
            } else {
                isVisual = true;
            }
            if (isVisual) break;
        }
        if (isVisual) {
            handleVisualCommand(text);
            m_input->setFocus();
            return;
        }
    }

    // ── 1.2 Организация файлов ("организуй загрузки") ────
    // Content-aware auto-sort — always shows a plan for confirmation
    // (FileOrganizer::buildPlan never touches disk by itself).
    {
        const QString lo = text.toLower();
        static const QStringList kOrganizeTriggers = {
            QStringLiteral("организуй"), QStringLiteral("рассортируй"),
            QStringLiteral("разбери"),   QStringLiteral("organize"), QStringLiteral("sort out"),
        };
        bool isOrganize = false;
        for (const QString& t : kOrganizeTriggers) {
            if (lo.contains(t)) { isOrganize = true; break; }
        }
        if (isOrganize) {
            QStandardPaths::StandardLocation loc = QStandardPaths::DownloadLocation;
            if (lo.contains(QStringLiteral("рабочий стол")) || lo.contains(QStringLiteral("desktop")))
                loc = QStandardPaths::DesktopLocation;
            else if (lo.contains(QStringLiteral("документ")) || lo.contains(QStringLiteral("document")))
                loc = QStandardPaths::DocumentsLocation;
            else if (lo.contains(QStringLiteral("изображени")) || lo.contains(QStringLiteral("picture")))
                loc = QStandardPaths::PicturesLocation;

            const QString folder = QStandardPaths::writableLocation(loc);
            appendLog(Str::logSender(), text, Theme::LogColors::user);
            appendLog(Str::logJarvis(),
                IS_EN ? QStringLiteral("🔍 Scanning %1...").arg(folder)
                      : QStringLiteral("🔍 Сканирую %1...").arg(folder),
                Theme::LogColors::system);

            FileOrganizer::instance().setLlmApi(m_jarvis->claudeApi());
            FileOrganizer::instance().buildPlan(folder, [this](const OrganizePlan& plan) {
                showOrganizePlanDialog(plan);
            });
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

    // ── Sync PC message → Telegram owner chat ─────────────
    if (auto* mesh = m_jarvis->meshConnector()) {
        if (auto* gw = mesh->telegramGateway())
            gw->forwardDesktopUserMessage(text);
    }

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
            case Intent::Domain::Code:               return QStringLiteral("Code");
            case Intent::Domain::Electronics:        return QStringLiteral("Electronics");
            case Intent::Domain::Philosophy_Chitchat: return QStringLiteral("Philosophy");
            default:                                 return QStringLiteral("None");
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
                previewIfSingleImage(filePaths);
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
                    previewIfSingleImage(filePaths);
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

    // ── 5.5 Локальный ответ роутера (Layer 1) — минуем Claude ─
    if (intent.hasLocalAnswer()) {
        appendLog(Str::logJarvis(), intent.localResponse, Theme::LogColors::jarvis);
        if (intent.localResponse.length() <= 300 && m_audioManager->speechAllowed())
            m_jarvis->speakAsync(intent.localResponse);
        m_jarvis->memory()->addMessage(QStringLiteral("user"), text);
        m_jarvis->memory()->addMessage(QStringLiteral("assistant"), intent.localResponse);

        // Uncertain cached answer — offer a quick way to confirm/correct it.
        // (Redundant with the typed confirm/deny phrases checked at the top
        // of onSend — whichever the user reaches for first wins.)
        if (intent.doubtId != 0) {
            m_pendingDoubtId    = intent.doubtId;
            m_pendingDoubtSetAt = QDateTime::currentDateTime();
            m_pendingInput      = text;
            m_pendingSuggestionAction = QStringLiteral("doubt_feedback:")
                                      + QString::number(intent.doubtId);
            showClarification(
                IS_EN ? QStringLiteral("Not fully sure about that one — was I right?")
                      : QStringLiteral("Не совсем уверен в этом ответе — я прав?"),
                { IS_EN ? QStringLiteral("\U0001F44D Correct") : QStringLiteral("\U0001F44D Верно"),
                  IS_EN ? QStringLiteral("\U0001F44E Wrong")   : QStringLiteral("\U0001F44E Неверно") }
            );
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
        // Check synchronous response for diagrams too
        auto syncDr = Jarvis::tryRenderDiagram(response);
        if (syncDr.hasDiagram && (!syncDr.svgData.isEmpty() || !syncDr.image.isNull())) {
            appendLog(Str::logJarvis(), syncDr.textWithoutDiagram, Theme::LogColors::jarvis);
            if (m_visualInsights) {
                if (!syncDr.mermaidSource.isEmpty())
                    m_visualInsights->showMermaid(syncDr.mermaidSource);
                else
                    m_visualInsights->showDiagram(syncDr.image);
            }
            if (m_audioManager->speechAllowed()) m_jarvis->speakAsync(syncDr.textWithoutDiagram);
        } else {
            appendLog(Str::logJarvis(), response, Theme::LogColors::jarvis);
            if (m_audioManager->speechAllowed()) m_jarvis->speakAsync(response);
        }

        // ── Sync PC response → Telegram owner chat ──
        if (auto* mesh = m_jarvis->meshConnector()) {
            if (auto* gw = mesh->telegramGateway())
                gw->forwardDesktopAiResponse(response);
        }

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
// FileOrganizer — план организации папки, требует подтверждения
// ============================================================

void MainWindow::showOrganizePlanDialog(const OrganizePlan& plan)
{
    if (plan.items.isEmpty()) {
        appendLog(Str::logJarvis(),
            IS_EN ? QStringLiteral("Folder is already empty — nothing to organize.")
                  : QStringLiteral("Папка пуста — организовывать нечего."),
            Theme::LogColors::system);
        return;
    }

    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(IS_EN ? QStringLiteral("Organize: %1").arg(plan.targetFolder)
                              : QStringLiteral("Организация: %1").arg(plan.targetFolder));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setMinimumSize(440, 340);
    auto* layout = new QVBoxLayout(dlg);

    auto* title = new QLabel(IS_EN
        ? QStringLiteral("📦 Proposed organization (%1 files):").arg(plan.items.size())
        : QStringLiteral("📦 Предложенная организация (%1 файлов):").arg(plan.items.size()),
        dlg);
    layout->addWidget(title);

    for (const auto& pair : plan.categoryCounts()) {
        const QString& category = pair.first;
        const int count = pair.second;
        const QString icon = (category == QStringLiteral("Нераспознано"))
            ? QStringLiteral("❓") : QStringLiteral("📁");
        auto* row = new QLabel(QStringLiteral("%1 %2  —  %3")
            .arg(icon, category,
                 IS_EN ? QStringLiteral("%1 file(s)").arg(count)
                       : QStringLiteral("%1 файл(ов)").arg(count)), dlg);
        layout->addWidget(row);
    }

    auto* note = new QLabel(IS_EN
        ? QStringLiteral("\"Нераспознано\" items are left untouched — only confidently "
                         "classified files are moved. Every move is logged and can be undone.")
        : QStringLiteral("Файлы категории «Нераспознано» не трогаются — перемещаются только "
                         "уверенно классифицированные. Каждое перемещение можно отменить."),
        dlg);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    layout->addWidget(note);
    layout->addStretch(1);

    auto* btnRow = new QHBoxLayout();
    auto* applyBtn  = new QPushButton(IS_EN ? QStringLiteral("✅ Apply") : QStringLiteral("✅ Применить"), dlg);
    auto* cancelBtn = new QPushButton(IS_EN ? QStringLiteral("❌ Cancel") : QStringLiteral("❌ Отмена"), dlg);
    btnRow->addWidget(applyBtn);
    btnRow->addWidget(cancelBtn);
    layout->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::close);
    connect(applyBtn, &QPushButton::clicked, dlg, [this, plan, dlg]() {
        const QString batchId = m_jarvis->organizeApplyPlan(plan);
        appendLog(Str::logJarvis(),
            !batchId.isEmpty()
                ? (IS_EN ? QStringLiteral("✅ Organized. Undo with \"Undo Last Organize\" if needed.")
                         : QStringLiteral("✅ Организовано. При необходимости — «Отменить последнюю организацию»."))
                : (IS_EN ? QStringLiteral("Nothing was moved.")
                         : QStringLiteral("Ничего не было перемещено.")),
            Theme::LogColors::system);
        dlg->close();
    });

    dlg->show();
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
    m_pendingDoubtId = 0;
}

void MainWindow::onClarificationChoice(int choice)
{
    if (m_pendingInput.isEmpty()) return;

    // Praise/scold buttons for an uncertain cached answer (see the
    // "5.5 Локальный ответ роутера" branch in onSend, which sets this up).
    if (m_pendingSuggestionAction.startsWith(QStringLiteral("doubt_feedback:"))) {
        const qint64 doubtId = m_pendingSuggestionAction.mid(15).toLongLong();
        const bool correct = (choice == 1); // button order: [Correct, Wrong]
        hideClarification();
        if (doubtId != 0) {
            SelfJournal::instance().resolveDoubt(doubtId, correct);
            appendLog(Str::logJarvis(),
                correct
                    ? (IS_EN ? QStringLiteral("Good to know — I'll trust that one more next time.")
                             : QStringLiteral("Понял, учту — в следующий раз буду увереннее."))
                    : (IS_EN ? QStringLiteral("Thanks for the correction — I'll be more careful with that one.")
                             : QStringLiteral("Спасибо, что поправил — учту это на будущее.")),
                Theme::LogColors::jarvis);
        }
        m_pendingSuggestionAction.clear();
        m_pendingInput.clear();
        return;
    }

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
            m_input->setPlainText(m_pendingInput);
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
    m_input->setPlainText(enriched);
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
    // Skip GUI display for Telegram-origin responses — they appear
    // via the conversationResponse signal with a 📱 prefix instead.
    if (m_jarvis->isTelegramOrigin())
        return;

    m_audioManager->playSuccess();

    // ── Sync PC async response → Telegram owner chat ──
    if (auto* mesh = m_jarvis->meshConnector()) {
        if (auto* gw = mesh->telegramGateway())
            gw->forwardDesktopAiResponse(response);
    }

    // Check if the LLM response contains a diagram — render it to the
    // visual dashboard. Uses the same 3-path pipeline as Telegram.
    auto dr = Jarvis::tryRenderDiagram(response);
    if (dr.hasDiagram && (!dr.svgData.isEmpty() || !dr.image.isNull())) {
        appendLog(Str::logJarvis(), dr.textWithoutDiagram, Theme::LogColors::jarvis);
        if (m_visualInsights) {
            if (!dr.mermaidSource.isEmpty())
                m_visualInsights->showMermaid(dr.mermaidSource);
            else
                m_visualInsights->showDiagram(dr.image);
        }
    } else {
        appendLog(Str::logJarvis(), response, Theme::LogColors::jarvis);
    }

    // Dual-response TTS
    if (m_audioManager->speechAllowed()) {
        const QString& ttsSource = dr.hasDiagram ? dr.textWithoutDiagram : response;
        JarvisResponse dual = JarvisResponse::parse(ttsSource);
        VoiceSynthesisManager::instance().say(dual.speechText);
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

    // Top-level: horizontal layout [main chat | diagram side panel]
    auto* hroot = new QHBoxLayout(central);
    hroot->setContentsMargins(0, 0, 0, 0);
    hroot->setSpacing(0);

    auto* chatContainer = new QWidget(central);
    auto* vbox = new QVBoxLayout(chatContainer);
    vbox->setContentsMargins(16, 8, 16, 12);
    vbox->setSpacing(8);

    hroot->addWidget(chatContainer, 1);

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
    auto* tbAttachments = makeToolBtn(QStringLiteral("📎"),
        IS_EN ? QStringLiteral("Attachments (diagrams, files, photos)")
              : QStringLiteral("Вложения (схемы, файлы, фото)"));
    auto* tbClear     = makeToolBtn(QStringLiteral("⌫"),
        IS_EN ? QStringLiteral("Clear chat")       : QStringLiteral("Очистить чат"));

    toolbar->addWidget(tbSearch);
    toolbar->addWidget(tbProject);
    toolbar->addWidget(tbVoice);
    toolbar->addWidget(tbTrain);
    toolbar->addWidget(tbCapture);
    toolbar->addWidget(tbAttachments);
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
            m_input->setPlainText(QStringLiteral("вспомни ") + query.trimmed());
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

    connect(tbAttachments, &QPushButton::clicked, this, [this]() {
        if (!m_visualInsights) return;
        if (!m_visualInsights->hasHistory()) {
            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("No attachments yet this session.")
                      : QStringLiteral("Пока нет вложений за эту сессию."),
                Theme::LogColors::system);
            return;
        }
        m_visualInsights->reopen();
    });

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

    // === Visual Insights — full-height side panel (right of chat) ===
    m_visualInsights = new VisualInsightsWidget(central);
    m_visualInsights->setVisible(false);
    hroot->addWidget(m_visualInsights, 0);

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
            // Try to launch as an app directly, not as chat text
            auto result = m_appLauncher.launch(m_pendingSuggestionAction);
            if (result.success) {
                appendLog(Str::logJarvis(),
                    (IS_EN ? QStringLiteral("Launching: ") : QStringLiteral("Запускаю: "))
                        + m_pendingSuggestionAction,
                    Theme::LogColors::jarvis);
            } else {
                // Fallback: send as chat input
                m_input->setPlainText(m_pendingSuggestionAction);
                onSend();
            }
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

    m_input = new QTextEdit(this);
    m_input->setObjectName(QStringLiteral("inputField"));
    m_input->setPlaceholderText(Str::inputPlaceholder());
    m_input->setAcceptRichText(false);
    m_input->setTabChangesFocus(true);
    m_input->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_input->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_input->setFixedHeight(38);
    m_input->document()->setDocumentMargin(0);
    connect(m_input->document(), &QTextDocument::contentsChanged, this, [this]() {
        const int docHeight = static_cast<int>(m_input->document()->size().height());
        const int padding = 18;
        const int minH = 38;
        const int maxH = 160;
        const int target = qBound(minH, docHeight + padding, maxH);
        m_input->setFixedHeight(target);
        m_input->setVerticalScrollBarPolicy(
            target >= maxH ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    });

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
    m_input->installEventFilter(this);
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
        m_input->insertPlainText(ch);
        m_input->setFocus();
    });
    connect(m_keyboard, &VirtualKeyboardWidget::backspacePressed, this, [this]() {
        QTextCursor tc = m_input->textCursor();
        if (!tc.atStart()) {
            tc.deletePreviousChar();
            m_input->setTextCursor(tc);
        }
        m_input->setFocus();
    });
    connect(m_keyboard, &VirtualKeyboardWidget::enterPressed, this, &MainWindow::onSend);
}

// ============================================================
// appendLog
// ============================================================

void MainWindow::previewIfSingleImage(const QStringList& filePaths)
{
    if (filePaths.size() != 1 || !m_visualInsights) return;

    static const QStringList imgExts = {
        QStringLiteral("png"),  QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("bmp"),  QStringLiteral("gif"), QStringLiteral("webp"),
        QStringLiteral("tiff"), QStringLiteral("ico"),
    };
    const QString ext = QFileInfo(filePaths.first()).suffix().toLower();
    if (imgExts.contains(ext))
        m_visualInsights->showImageFile(filePaths.first());
}

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
// showWelcomeDashboard — dynamic signal-driven greeting panel
// ============================================================

void MainWindow::showWelcomeDashboard()
{
    // Render the initial welcome panel
    m_log->setHtml(buildWelcomeHtml());

    // Live signal connections — re-render on state changes
    connect(&MemoryConsolidation::instance(),
            &MemoryConsolidation::driveStatusChanged,
            this, [this](bool) {
        m_log->setHtml(buildWelcomeHtml());
    });

    connect(&UserProfileExtended::instance(),
            &UserProfileExtended::profileChanged,
            this, [this](const QString&) {
        m_log->setHtml(buildWelcomeHtml());
    });
}

QString MainWindow::buildWelcomeHtml() const
{
    const auto& tc = ThemeManager::colors(m_themeIndex);
    const int hour = QTime::currentTime().hour();

    // ── Identity greeting ─────────────────────────────────
    const QString nickname = UserProfileExtended::instance().nickname();
    QString greeting;
    if (nickname.isEmpty()) {
        greeting = IS_EN
            ? QStringLiteral("System Initialized. Awaiting identity calibration...")
            : QStringLiteral("Система инициализирована. Ожидание калибровки идентичности...");
    } else {
        QString timeGreet;
        if      (hour < 6)  timeGreet = IS_EN ? QStringLiteral("Still up, %1?")   : QStringLiteral("Не спишь, %1?");
        else if (hour < 12) timeGreet = IS_EN ? QStringLiteral("Morning, %1.")     : QStringLiteral("Доброе утро, %1.");
        else if (hour < 18) timeGreet = IS_EN ? QStringLiteral("Afternoon, %1.")   : QStringLiteral("Добрый день, %1.");
        else                timeGreet = IS_EN ? QStringLiteral("Evening, %1.")     : QStringLiteral("Добрый вечер, %1.");
        greeting = timeGreet.arg(nickname);
    }

    // ── Version ───────────────────────────────────────────
    const QString version = QCoreApplication::applicationVersion();

    // ── Storage status ────────────────────────────────────
    const auto driveStatus = MemoryConsolidation::instance().checkDriveStatus();
    QString storageHtml;
    if (driveStatus.connected) {
        storageHtml = QStringLiteral(
            "<span style='color:#66FCF1;'>&#9679;</span> "
            "External Core [%1 GB] Connected — %2 GB free")
            .arg(QString::number(driveStatus.totalGb(), 'f', 0),
                 QString::number(driveStatus.freeGb(), 'f', 1));
    } else {
        storageHtml = QStringLiteral(
            "<span style='color:#FFA726;'>&#9679;</span> "
            "Running Autonomous (Local SSD Cache)");
    }

    // ── LLM status ────────────────────────────────────────
    const bool claudeOk = m_jarvis->claudeApi() && m_jarvis->claudeApi()->hasApiKey();
    const bool geminiOk = m_jarvis->geminiBackup() && m_jarvis->geminiBackup()->hasApiKey();
    QString llmLine;
    if (claudeOk)
        llmLine = QStringLiteral("<span style='color:#66FCF1;'>&#9679;</span> Claude API Online");
    else
        llmLine = QStringLiteral("<span style='color:#ff4444;'>&#9679;</span> Claude API — no key");
    if (geminiOk)
        llmLine += QStringLiteral(" &nbsp;|&nbsp; <span style='color:#4a9a6a;'>&#9679;</span> Gemini Fallback Ready");

    // ── Database status ───────────────────────────────────
    const auto& db = DatabaseManager::instance();
    QString dbLine;
    if (db.isOpen())
        dbLine = QStringLiteral("<span style='color:#66FCF1;'>&#9679;</span> Database Online");
    else
        dbLine = QStringLiteral("<span style='color:#ff4444;'>&#9679;</span> Database OFFLINE");

    // ── Independence metric (Layer 4) ─────────────────────
    const auto indep = LlmCacheManager::instance()
        .independenceStats(LlmCacheManager::kDesktopOwnerId, 7);
    QString indepLine;
    if (indep.total > 0) {
        const QString text = IS_EN
            ? QStringLiteral("<span style='color:#66FCF1;'>&#9679;</span> "
                             "Independence (7d): %1% (%2/%3 answered locally)")
                  .arg(QString::number(indep.pct(), 'f', 0))
                  .arg(indep.local).arg(indep.total)
            : QStringLiteral("<span style='color:#66FCF1;'>&#9679;</span> "
                             "Самостоятельность (7д): %1% (%2/%3 локально)")
                  .arg(QString::number(indep.pct(), 'f', 0))
                  .arg(indep.local).arg(indep.total);
        indepLine = QStringLiteral("<div style='color:%1; font-size:12px; "
                                   "padding:3px 0; font-family:Consolas,monospace;'>"
                                   "%2</div>")
                        .arg(QLatin1String(tc.user), text);
    }

    // ── Project status ────────────────────────────────────
    QString projLine;
    if (m_jarvis->projectIndexer()->fileCount() > 0) {
        projLine = QStringLiteral("<span style='color:#66FCF1;'>&#9679;</span> Project: %1 (%2 files)")
                       .arg(m_jarvis->projectIndexer()->projectRoot()
                                .section(QChar('/'), -1),
                            QString::number(m_jarvis->projectIndexer()->fileCount()));
    }

    // ── Current thought / reflection digest ───────────────
    const auto doubts = SelfJournal::instance().topDoubtsForVerification(1);
    const int doubtCount = SelfJournal::instance().unresolvedDoubtCount();
    const int pdfChunks  = PdfDistiller::instance().totalChunks();
    const int pdfDoubts  = PdfDistiller::instance().doubtCount();

    QString thoughtLine;
    if (!doubts.isEmpty()) {
        const auto& d = doubts.first();
        thoughtLine = QStringLiteral("Reflecting on: <em>\"%1\"</em> (confidence: %2)")
                          .arg(d.content.left(80).toHtmlEscaped(),
                               QString::number(d.confidence, 'f', 2));
    } else if (pdfChunks > 0) {
        thoughtLine = IS_EN
            ? QStringLiteral("Knowledge base: %1 chunks distilled").arg(pdfChunks)
            : QStringLiteral("База знаний: %1 фрагментов извлечено").arg(pdfChunks);
    } else {
        thoughtLine = IS_EN
            ? QStringLiteral("Idle — awaiting new data to learn from")
            : QStringLiteral("Ожидание — готов к обучению");
    }

    QString doubtBadge;
    if (doubtCount > 0 || pdfDoubts > 0) {
        const int total = doubtCount + pdfDoubts;
        doubtBadge = QStringLiteral(
            " &nbsp;<span style='background:#ff572233; color:#FF5722; "
            "padding:2px 8px; border-radius:8px; font-size:11px;'>"
            "&#10067; %1 unverified</span>").arg(total);
    }

    // ── Build the complete dashboard HTML ─────────────────
    return QStringLiteral(
        // Outer container
        "<div style='margin:12px 8px; font-family:Segoe UI,sans-serif;'>"

        // Header — JARVIS title + version
        "<div style='text-align:center; padding:16px 0 8px 0;'>"
        "<span style='color:%1; font-size:28px; font-weight:bold; "
        "letter-spacing:6px; font-family:Segoe UI Semibold,sans-serif;'>"
        "J.A.R.V.I.S.</span><br>"
        "<span style='color:%2; font-size:11px; letter-spacing:2px;'>"
        "v%3 &nbsp;|&nbsp; %4</span>"
        "</div>"

        // Greeting
        "<div style='text-align:center; padding:8px 0 16px 0;'>"
        "<span style='color:%5; font-size:15px;'>%6</span>"
        "</div>"

        // Status grid
        "<div style='background:%7; border:1px solid %8; border-radius:10px; "
        "padding:14px 18px; margin:4px 0;'>"

        // Row: Storage
        "<div style='color:%9; font-size:12px; padding:3px 0; "
        "font-family:Consolas,monospace;'>%10</div>"

        // Row: LLM
        "<div style='color:%9; font-size:12px; padding:3px 0; "
        "font-family:Consolas,monospace;'>%11</div>"

        // Row: Database
        "<div style='color:%9; font-size:12px; padding:3px 0; "
        "font-family:Consolas,monospace;'>%12</div>"

        // Row: Independence (if any router activity yet)
        "%17"

        // Row: Project (if any)
        "%13"

        "</div>"

        // Current thought
        "<div style='background:%7; border:1px solid %8; border-radius:10px; "
        "padding:12px 18px; margin:6px 0;'>"
        "<span style='color:%14; font-size:11px; letter-spacing:1px;'>"
        "CURRENT THOUGHT</span>%15<br>"
        "<span style='color:%9; font-size:12px; font-family:Consolas,monospace;'>"
        "%16</span>"
        "</div>"

        "</div>"
    )
    .arg(
        /* %1  title color */   QStringLiteral("#66FCF1"),
        /* %2  subtitle color*/ QStringLiteral("rgba(102,252,241,0.6)"),
        /* %3  version */       version,
        /* %4  date */          QDate::currentDate().toString(QStringLiteral("dd MMM yyyy")),
        /* %5  greeting color*/ QLatin1String(tc.jarvis),
        /* %6  greeting text */ greeting,
        /* %7  card bg */       QLatin1String(tc.cardBg),
        /* %8  card border */   QLatin1String(tc.cardBorder),
        /* %9  text color */    QLatin1String(tc.user)
    )
    .arg(
        /* %10 storage */       storageHtml,
        /* %11 llm */           llmLine,
        /* %12 db */            dbLine,
        /* %13 project */       projLine.isEmpty() ? QString()
                                    : QStringLiteral("<div style='color:%1; font-size:12px; "
                                                     "padding:3px 0; font-family:Consolas,monospace;'>"
                                                     "%2</div>")
                                          .arg(QLatin1String(tc.user),
                                               projLine),
        /* %14 label color */   QStringLiteral("#66FCF1"),
        /* %15 doubt badge */   doubtBadge,
        /* %16 thought */       thoughtLine,
        /* %17 independence */  indepLine
    );
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
            ? (IS_EN ? QStringLiteral("✓ Found and activated: ") : QStringLiteral("✓ Нашёл и активировал: ")) + searchText
            : (IS_EN ? QStringLiteral("✗ Could not find: ") : QStringLiteral("✗ Не удалось найти: ")) + searchText
              + (IS_EN ? QStringLiteral(" (checked windows + screen OCR)") : QStringLiteral(" (проверил окна + OCR экрана)"));
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

    m_input->setPlainText(text);

    // Помечаем что ввод голосовой — onAsyncResponse сохранит пару в voice_journal
    m_lastInputWasVoice = true;
    m_lastVoiceLanguage = lang;

    // Speech that doesn't look like Russian or English text at all (not
    // just short/ambiguous) — try to look it up and remember it, so an
    // unfamiliar language becomes recognizable over time.
    if (LanguageDetector::detect(text) == LanguageDetector::Language::Unknown
        && text.length() >= 6 && m_jarvis->translationEngine()) {
        m_jarvis->translationEngine()->learnUnknownLanguageSnippet(text);
    }

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

// ============================================================
// Профиль пользователя: первый запуск и редактирование
// ============================================================

void MainWindow::runFirstRunProfileSetup()
{
    auto& db = DatabaseManager::instance();
    auto user = db.getUser(m_jarvis->currentUserId());
    if (!user) {
        qint64 id = db.getOrCreateDefaultUser();
        user = db.getUser(id);
        if (!user) return;
        m_jarvis->setCurrentUserId(id);
    }

    ProfileSetupDialog dlg(ProfileSetupDialog::Mode::FirstRun, IS_EN,
                           m_jarvis->skillManager(), this);
    dlg.setProfile(*user);
    dlg.exec(); // FirstRun: закрыть можно только заполнив имя (кнопка Start)

    DbUserProfile updated = dlg.profile();
    if (!updated.name.isEmpty()) {
        db.updateUser(updated);
        m_jarvis->memory()->setActiveUserName(updated.name);
        appendLog(Str::logSystem(),
            (IS_EN ? QStringLiteral("Nice to meet you, ") : QStringLiteral("Приятно познакомиться, "))
            + updated.name + QStringLiteral("!"),
            Theme::LogColors::system);
    }

    QSettings(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"))
        .setValue(QStringLiteral("user/profileSetupDone"), true);
}

void MainWindow::editCurrentUserProfile()
{
    auto& db = DatabaseManager::instance();
    auto user = db.getUser(m_jarvis->currentUserId());
    if (!user) return;

    ProfileSetupDialog dlg(ProfileSetupDialog::Mode::Edit, IS_EN, nullptr, this);
    dlg.setProfile(*user);
    if (dlg.exec() != QDialog::Accepted) return;

    DbUserProfile updated = dlg.profile();
    if (updated.name.isEmpty()) return;

    db.updateUser(updated);
    m_jarvis->memory()->setActiveUserName(updated.name);
    appendLog(Str::logSystem(),
        (IS_EN ? QStringLiteral("Profile updated: ") : QStringLiteral("Профиль обновлён: "))
        + updated.name + QStringLiteral(" (") + updated.scenario + QStringLiteral(")"),
        Theme::LogColors::system);
}