// -------------------------------------------------------
// mainwindow.cpp — Главное окно J.A.R.V.I.S.
// -------------------------------------------------------

#include "mainwindow.h"
#include "jarvis.h"
#include "command_palette.h"
#include "system_monitor_dialog.h"
#include "notifications_dialog.h"
#include "device_hub_dialog.h"
#include "dashboard_dialog.h"
#include "action_registry.h"
#include "event_feed.h"
#include "workflow_manager.h"
#include "agent_loop.h"
#include "permission_gate.h"
#include "tool_registry.h"
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
#include "learned_commands.h"
#include "screen_agent.h"
#include "bug_reporter.h"
#include "voice_input.h"
#include "health_center.h"
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
#include "system_manifest.h"
#include "vision_center_dialog.h"
#include "camera_view_dialog.h"
#include "artifacts_dialog.h"
#include "artifact_registry.h"
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
#include "user_center_dialog.h"
#include "skills_dialog.h"
#include "modes_dialog.h"
#include "voice_synthesis_manager.h"
#include "elevenlabs_provider.h"
#include "llm_cache_manager.h"
#include "file_organizer.h"
#include "organize_plan_dialog.h"
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
#include <QPushButton>
#include <QAbstractButton>
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

#include "chat_model.h"
#include "chat_controller.h"
#include "notice_controller.h"
#include "attachment_model.h"
#include "welcome_controller.h"
#include "visual_insights_controller.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include "jarvis_theme.h"

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

// Роль сообщения раньше выражалась цветом: appendLog() принимал
// hex-строку, и «системный зелёный» существовал ровно потому, что
// его вписали в 59 мест. Менять сигнатуру и все 142 вызова — лишний
// риск на ровном месте, поэтому цвет здесь читается обратно в роль,
// а дальше по приложению ходит уже роль. Цвет выдаёт тема.
ChatModel::Kind kindFromLogColor(const QString& color)
{
    if (color.compare(QLatin1String(Theme::LogColors::error),
                      Qt::CaseInsensitive) == 0) return ChatModel::Error;
    if (color.compare(QLatin1String(Theme::LogColors::system),
                      Qt::CaseInsensitive) == 0) return ChatModel::System;
    if (color.compare(QLatin1String(Theme::LogColors::user),
                      Qt::CaseInsensitive) == 0) return ChatModel::User;
    return ChatModel::Jarvis;
}
}

// ============================================================
// Конструктор
// ============================================================

MainWindow::MainWindow(Jarvis* core, ActionRegistry* actions, QWidget* parent)
    : QMainWindow(parent)
    , m_jarvis(core)
    , m_actions(actions)
{
    Q_ASSERT_X(m_jarvis,  "MainWindow", "core must not be null");
    Q_ASSERT_X(m_actions, "MainWindow", "action registry must not be null");

    setAcceptDrops(true);

    // Загружаем язык из настроек (для UI-строк)
    QSettings cfg(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
    bool english = cfg.value(QStringLiteral("ui/english"), false).toBool();
    gUiLanguage() = english ? UiLanguage::English : UiLanguage::Russian;
    // Дефолт — русский, пока пользователь явно не переключит язык в настройках.

    // Модель ленты создаётся ДО buildUI() и до первых appendLog():
    // приветственные сообщения пишутся уже в конструкторе.
    m_chat    = new ChatModel(this);
    m_chatCtl = new ChatController(this);
    m_chatCtl->setEnglish(english);
    m_noticeCtl   = new NoticeController(this);
    m_attachModel = new AttachmentModel(this);

    // Ядро уже создано в main() — здесь только досылаем ему язык,
    // выбранный пользователем: до этого момента оно живёт с русским
    // по умолчанию (см. jarvis.h, m_uiEnglish).
    m_jarvis->setUiLanguage(english);
    CuriosityEngine::instance().setUiEnglish(english);

    // Приветственному экрану нужен собранный Jarvis: он читает
    // индексатор проекта и клиента Claude.
    m_welcomeCtl = new WelcomeController(m_jarvis, this);
    m_visualCtl  = new VisualInsightsController(this);

    // Звуки интерфейса и режим «можно ли говорить» — тоже ядра: решение
    // молчать должно действовать и когда окно закрыто.
    m_audioManager = m_jarvis->audio();

    // Non-blocking: scans models/tts for Piper voices and falls back
    // to SAPI silently if piper.exe or no models are found.
    VoiceSynthesisManager::instance().loadModelsAsync();

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

    // Фоновый советник по проекту: применённые сам правки видно в логе —
    // молча менять файлы пользователя нельзя, даже если правка мелкая.
    connect(m_jarvis, &Jarvis::advisorMessage,
            this, [this](const QString& message) {
                appendLog(Str::logJarvis(), message, Theme::LogColors::system);
                NotificationManager::instance().showNotification(
                    IS_EN ? QStringLiteral("Project advisor")
                          : QStringLiteral("Советник по проекту"),
                    message);
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

    setupAgentUi();

    // Реестр создан в main() и переживёт окно; здесь его наполняют.
    // Модель для QML — уже окна: она умирает вместе с ним.
    m_actionModel = new ActionModel(m_actions, this);
    registerAppActions();

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
        m_chatCtl->setStatusText(Str::statusDownload().arg(percent));
        if (m_noticeCtl) {
            m_noticeCtl->setUpdateBusy(true);
            m_noticeCtl->setUpdateProgress(percent);
        }
    });
    connect(updater, &AutoUpdater::downloadFinished,
            this, [this](const QString& path) {
        if (m_noticeCtl) m_noticeCtl->setUpdateBusy(false);

        NotificationManager::instance().showNotification(
            QStringLiteral("System Update"),
            IS_EN ? QStringLiteral("Update v%1 downloaded successfully")
                        .arg(m_jarvis->autoUpdater()->pendingVersion())
                  : QStringLiteral("Обновление v%1 успешно загружено")
                        .arg(m_jarvis->autoUpdater()->pendingVersion()));

        // Кнопка не переписывается на лету (setText + disconnect всех
        // обработчиков): меняется только подпись, а куда вести —
        // решает обработчик по m_downloadedUpdatePath.
        m_downloadedUpdatePath = path;
        if (m_noticeCtl) {
            m_noticeCtl->setUpdateActionLabel(IS_EN ? QStringLiteral("Open Folder")
                                                    : QStringLiteral("Открыть папку"));
        }

        // Заметки к релизу идут обычным сообщением ленты. Раньше это
        // была отдельная HTML-карточка, которую дописывали прямо в
        // QTextDocument мимо appendLog() — единственное сообщение в
        // приложении со своей вёрсткой.
        QString text = (IS_EN ? QStringLiteral("Update v%1 downloaded.\nSaved to: %2")
                              : QStringLiteral("Обновление v%1 загружено.\nСохранено в: %2"))
                           .arg(m_jarvis->autoUpdater()->pendingVersion(), path);

        const QString notes = m_jarvis->autoUpdater()->pendingNotes();
        if (!notes.isEmpty())
            text += QStringLiteral("\n\n") + notes;

        appendLog(Str::logSystem(), text, Theme::LogColors::system);
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
        if (m_visualCtl)
            m_visualCtl->showFileRef(path, QFileInfo(path).fileName());

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
            this, [this](const QString& question, const QStringList& options) {
        NotificationManager::instance().askQuestion(
            QStringLiteral("J.A.R.V.I.S."), question,
            [this](const QString& answer) {
                CuriosityEngine::instance().consumeAnswer(0, answer, 0, /*explicitReply=*/true);
                hideAnswerPrompt();
            },
            options);

        // The toast is transient; the answer bar is not. A toast the user
        // scrolls past or ignores for half an hour used to leave no way to
        // answer at all — the bar keeps the question answerable in the chat
        // itself until they either reply or dismiss it.
        showAnswerPrompt(question);
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

    // Трея здесь больше нет: он живёт рядом с окном и говорит с ядром
    // напрямую (см. tray_presence.h, создаётся в main.cpp). Окно узнаёт
    // о нём ровно одно — есть ли куда прятаться при закрытии.

    // Dynamic welcome dashboard — replaces all hardcoded greeting strings
    showWelcomeDashboard();

    // Sync project info if indexed
    if (m_jarvis->projectIndexer()->fileCount() > 0)
        m_jarvis->syncProjectInfoToMemory();

    m_chatCtl->requestFocus();

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
        if (s.confidence >= 0.65f && m_noticeCtl) {
            QString desc = IS_EN
                ? QStringLiteral("Usually at this time you use %1. Open it?").arg(s.appName)
                : QStringLiteral("Обычно в это время вы используете %1. Открыть?").arg(s.appName);
            m_pendingSuggestionAction = s.appName;
            onSuggestion(desc, s.appName);
        }
    });
    // Wire ScreenshotLearner → CuriosityEngine for visual context
    CuriosityEngine::instance().setScreenshotLearner(m_appLearner);

    // (Watched work — app, window title, category — feeds SynapseGraph from
    // ScreenshotLearner::onCapture, where all three are available together;
    // the app name alone would create a node with nothing to link it to.)

    // Запускаем ПОСЛЕ полной инициализации окна
    QTimer::singleShot(3000, this, [this]() {
        if (m_appLearner) m_appLearner->start(2);
    });
    connect(m_screenAgent, &ScreenAgent::actionCompleted,
            this, [this](const QString& desc) {
        appendLog(Str::logJarvis(), desc, Theme::LogColors::system);
    });

    // Мигание точки состояния больше не крутится здесь. Раньше QTimer
    // на 400 мс дважды в секунду подменял таблицу стилей QLabel — то
    // есть анимация шла мимо всякой системы анимаций и не знала про
    // «уменьшить движение». Теперь пульсирует JarvisStatusDot, и он
    // этот флаг уважает.

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
    m_chatCtl->setPlaceholder(Str::inputPlaceholder());

    // ── Статус-бар ───────────────────────────────────────────
    m_chatCtl->setStatusText(IS_EN ? QStringLiteral("Ready") : QStringLiteral("Готов"));

    // ── Кнопка микрофона ─────────────────────────────────────
    if (m_chatCtl) {
        if (!m_voiceActive) {
            m_chatCtl->setMicTooltip(IS_EN ? QStringLiteral("Voice input (Vosk)")
                                       : QStringLiteral("Голосовой ввод (Vosk)"));
        }
    }

    // ── Кнопка лайка ─────────────────────────────────────────
    if (m_likeBtn) {
        m_likeBtn->setToolTip(IS_EN
            ? QStringLiteral("Like this response — save for AI training")
            : QStringLiteral("Лайкнуть ответ — сохранить для обучения ИИ"));
    }


    // ── Панель обновления ────────────────────────────────────

    // ── Панель предложений ───────────────────────────────────

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
    // Пункты — из ActionRegistry (см. registerAppActions): один список
    // на меню, поиск Ctrl+K и модель для QML.
    auto* fileMenu = menuBar->addMenu(Str::menuFile());
    connect(fileMenu, &QMenu::aboutToShow, this, [this, fileMenu]() {
        fileMenu->clear();
        m_actions->populateMenu(fileMenu, QStringLiteral("file"));
    });


    // --- Пользователь (multi-user) ---
    {
        auto* userMenu = menuBar->addMenu(
            IS_EN ? QStringLiteral("👤 User") : QStringLiteral("👤 Пользователь"));
        connect(userMenu, &QMenu::aboutToShow, this, [this, userMenu]() {
            userMenu->clear();
            m_actions->populateMenu(userMenu, QStringLiteral("user"));
        });
    }


    // --- Models & Intelligence ---
    auto* settingsMenu = menuBar->addMenu(
        IS_EN ? QStringLiteral("🤖 Models && Intelligence")
              : QStringLiteral("🤖 Модели и ИИ"));
    connect(settingsMenu, &QMenu::aboutToShow, this, [this, settingsMenu]() {
        settingsMenu->clear();
        m_actions->populateMenu(settingsMenu, QStringLiteral("models"));
        settingsMenu->addSeparator();
        buildLanguageMenu(settingsMenu);
    });

    // --- Проект ---
    auto* projectMenu = menuBar->addMenu(Str::menuProject());
    connect(projectMenu, &QMenu::aboutToShow, this, [this, projectMenu]() {
        projectMenu->clear();
        m_actions->populateMenu(projectMenu, QStringLiteral("project"));
    });


    // --- Панели ---
    // Пункты не пишутся здесь по одному: они лежат в ActionRegistry
    // (см. registerAppActions) и оттуда же попадают в поиск Ctrl+K и
    // в ActionModel для QML. Меню пересобирается на каждый показ,
    // потому что заголовки живые — например, счётчик непрочитанного.
    auto* panelsMenu = menuBar->addMenu(
        IS_EN ? QStringLiteral("▦ Panels") : QStringLiteral("▦ Панели"));
    connect(panelsMenu, &QMenu::aboutToShow, this, [this, panelsMenu]() {
        panelsMenu->clear();
        m_actions->populateMenu(panelsMenu, QStringLiteral("view"));
    });

    // --- Профиль (режим + уровень доверия) ---
    // Профиль и есть режим: он несёт и набор скиллов с тоном ответов,
    // и системную часть — разрешения, уведомления, громкость. Отдельной
    // сущности "Profile" нет намеренно, иначе их пришлось бы
    // синхронизировать между собой.
    auto* profileMenu = menuBar->addMenu(
        IS_EN ? QStringLiteral("👤 Profile") : QStringLiteral("👤 Профиль"));
    connect(profileMenu, &QMenu::aboutToShow, this, [this, profileMenu]() {
        profileMenu->clear();

        ModeManager* modes = m_jarvis->modeManager();
        if (modes) {
            auto* group = new QActionGroup(profileMenu);
            group->setExclusive(true);

            for (const ModeInfo& mode : modes->modes()) {
                const QString label = (mode.icon.isEmpty() ? QString()
                                                           : mode.icon + QChar(' '))
                                      + mode.displayName(IS_EN);
                QAction* act = profileMenu->addAction(label);
                act->setCheckable(true);
                act->setChecked(mode.id == modes->activeId());
                act->setActionGroup(group);

                QString tip = mode.description(IS_EN);
                const QString sys = mode.system.summary(IS_EN);
                if (!sys.isEmpty())
                    tip += QStringLiteral("\n\n") + sys;
                act->setToolTip(tip);

                const QString id = mode.id;
                connect(act, &QAction::triggered, this, [this, id]() {
                    m_jarvis->modeManager()->activate(id);
                });
            }

            profileMenu->addSeparator();
        }

        QAction* actModes = profileMenu->addAction(
            IS_EN ? QStringLiteral("Configure modes…")
                  : QStringLiteral("Настроить режимы…"));
        connect(actModes, &QAction::triggered, this, [this]() {
            ModesDialog dlg(m_jarvis, IS_EN, this);
            dlg.exec();
        });

        profileMenu->addSeparator();

        // --- Уровень доверия ---
        // Ослабить разрешения можно только отсюда: инструмент
        // set_permission_mode умеет лишь ужесточать, чтобы модель не
        // могла уговорить сама себя на большее.
        PermissionGate* gate = m_jarvis->permissions();
        auto* permMenu = profileMenu->addMenu(
            IS_EN ? QStringLiteral("🔐 Permissions") : QStringLiteral("🔐 Разрешения"));
        if (gate) {
            auto* permGroup = new QActionGroup(permMenu);
            permGroup->setExclusive(true);

            struct Entry { PermissionMode mode; const char* ru; const char* en; };
            static const Entry kEntries[] = {
                { PermissionMode::Paranoid, "Спрашивать про всё",           "Ask about everything" },
                { PermissionMode::Balanced, "Спрашивать перед изменениями", "Ask before changes" },
                { PermissionMode::Trusted,  "Только про необратимое",       "Only before destructive" },
            };

            for (const Entry& e : kEntries) {
                QAction* act = permMenu->addAction(
                    IS_EN ? QString::fromUtf8(e.en) : QString::fromUtf8(e.ru));
                act->setCheckable(true);
                act->setChecked(gate->mode() == e.mode);
                act->setActionGroup(permGroup);
                act->setToolTip(permissionModeDescription(e.mode, IS_EN));

                const PermissionMode target = e.mode;
                connect(act, &QAction::triggered, this, [this, target]() {
                    m_jarvis->permissions()->setMode(target);
                    appendLog(Str::logJarvis(),
                              (IS_EN ? QStringLiteral("Permission level: %1 — %2")
                                     : QStringLiteral("Уровень доверия: %1 — %2"))
                                  .arg(permissionModeName(target),
                                       permissionModeDescription(target, IS_EN)),
                              Theme::LogColors::system);
                });
            }

            permMenu->addSeparator();

            const QStringList grants = gate->sessionGrants();
            QAction* actReset = permMenu->addAction(
                (IS_EN ? QStringLiteral("Forget session approvals (%1)")
                       : QStringLiteral("Забыть разрешения сессии (%1)"))
                    .arg(grants.size()));
            actReset->setEnabled(!grants.isEmpty());
            actReset->setToolTip(grants.join(QStringLiteral(", ")));
            connect(actReset, &QAction::triggered, this, [this]() {
                m_jarvis->permissions()->clearSessionGrants();
                appendLog(Str::logJarvis(),
                          IS_EN ? QStringLiteral("Session approvals cleared.")
                                : QStringLiteral("Разрешения, выданные до перезапуска, сброшены."),
                          Theme::LogColors::system);
            });
        }
    });

    // --- Сценарии (workflows) ---
    // Собирается заново при каждом открытии: список меняется из чата
    // (save_workflow), и статическое меню устаревало бы молча.
    auto* wfMenu = menuBar->addMenu(
        IS_EN ? QStringLiteral("⚙ Workflows") : QStringLiteral("⚙ Сценарии"));
    connect(wfMenu, &QMenu::aboutToShow, this, [this, wfMenu]() {
        wfMenu->clear();

        WorkflowManager* wm = m_jarvis->workflows();
        if (!wm || wm->count() == 0) {
            QAction* empty = wfMenu->addAction(
                IS_EN ? QStringLiteral("No workflows yet")
                      : QStringLiteral("Сценариев пока нет"));
            empty->setEnabled(false);
        } else {
            for (const Workflow& wf : wm->all()) {
                const QString label = (wf.icon.isEmpty() ? QStringLiteral("▶") : wf.icon)
                                      + QChar(' ') + wf.name;
                QAction* act = wfMenu->addAction(label);
                act->setToolTip(wf.description);
                const QString name = wf.name;
                connect(act, &QAction::triggered, this, [this, name]() {
                    m_jarvis->workflows()->run(name);
                });
            }
        }

        wfMenu->addSeparator();
        QAction* hint = wfMenu->addAction(
            IS_EN ? QStringLiteral("＋ Create one by asking…")
                  : QStringLiteral("＋ Создать словами…"));
        connect(hint, &QAction::triggered, this, [this]() {
            m_chatCtl->setDraft(
                IS_EN ? QStringLiteral("remember this as a workflow called Gaming: "
                                       "launch Steam, launch Discord, set volume to 70")
                      : QStringLiteral("запомни как сценарий Gaming: запусти Steam, "
                                       "запусти Discord, поставь громкость 70"));
        });
    });

    // --- AI Training (fine-tuning датасет) ---
    auto* trainMenu = menuBar->addMenu(
        IS_EN ? QStringLiteral("🧠 Training") : QStringLiteral("🧠 Обучение"));
    connect(trainMenu, &QMenu::aboutToShow, this, [this, trainMenu]() {
        trainMenu->clear();
        m_actions->populateMenu(trainMenu, QStringLiteral("training"));
    });


    // --- Задачи ---
    {
        auto* taskMenu = menuBar->addMenu(
            IS_EN ? QStringLiteral("📋 Tasks") : QStringLiteral("📋 Задачи"));
        connect(taskMenu, &QMenu::aboutToShow, this, [this, taskMenu]() {
            taskMenu->clear();
            m_actions->populateMenu(taskMenu, QStringLiteral("tasks"));
        });
    }


    // --- Телефон и Сервер ---
    {
        auto* phoneMenu = menuBar->addMenu(
            IS_EN ? QStringLiteral("📱 Phone && Server")
                  : QStringLiteral("📱 Телефон и Сервер"));
        connect(phoneMenu, &QMenu::aboutToShow, this, [this, phoneMenu]() {
            phoneMenu->clear();
            m_actions->populateMenu(phoneMenu, QStringLiteral("phone"));
        });
    }


    // --- Система ---
    {
        auto* sysMenu = menuBar->addMenu(
            IS_EN ? QStringLiteral("⚙ System") : QStringLiteral("⚙ Система"));
        connect(sysMenu, &QMenu::aboutToShow, this, [this, sysMenu]() {
            sysMenu->clear();
            m_actions->populateMenu(sysMenu, QStringLiteral("system"));
            sysMenu->addSeparator();
            buildTranslationPairMenu(sysMenu);
        });
    }


    // --- Камера и охрана ---
    {
        // Общий экземпляр, которым владеет Jarvis: команда /security в
        // Telegram взводит ЭТОТ же объект, иначе две камеры смотрели бы
        // в один вебкам и слали два оповещения на одно событие.
        m_securityCam = m_jarvis->securityCamera();

        // Подписки на камеру ставятся здесь, а не при первом включении
        // охраны: до этого события камеры (тревоги, распознанные лица,
        // блокировка сеанса) уходили в пустоту, если охрану ни разу не
        // включали, и в ленте о них не было ни строчки.
        wireSecurityCameraUi();

        // Камера — ОДИН пункт меню, а не список. Всё, чем здесь
        // пользуются, живёт в Центре зрения, где рядом видно состояние:
        // есть ли вебкамера, идёт ли охрана, заперт ли экран. Плоский
        // список из восьми команд этого показать не мог.
        //
        // Сами команды из реестра не удалены — они остаются доступны
        // через палитру (Ctrl+K) и голосом.
        auto* camAction = menuBar->addAction(
            IS_EN ? QStringLiteral("Camera") : QStringLiteral("Камера"));
        connect(camAction, &QAction::triggered, this, [this]() {
            openVisionCenter(1);   // сразу вкладка «Камера»
        });
    }

    auto* helpMenu = menuBar->addMenu(Str::menuHelp());
    connect(helpMenu, &QMenu::aboutToShow, this, [this, helpMenu]() {
        helpMenu->clear();
        m_actions->populateMenu(helpMenu, QStringLiteral("help"));
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
<tr><td style='color:%3;'><b>Needs internet</b></td><td>Claude API, auto-updater, Screenshot Vision</td></tr>
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
<tr><td style='color:%1;white-space:nowrap;'><b>Any question</b></td><td>Routes to Claude (code/complex) or Ollama (casual chat)</td></tr>
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
If Ollama is unavailable → Claude (last resort)</p>
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
<li>Try <b>Settings → Agent Mode</b> to enable the Ollama fallback</li>
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

// Enter/Shift+Enter в поле ввода теперь разбирает сам ChatComposer.qml:
// фильтр событий на QTextEdit больше не нужен и не на что вешаться.
// Метод оставлен как точка расширения — базовая реализация ничего не
// перехватывает.
bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        if (m_noticeCtl && m_noticeCtl->clarifyOpen()) {
            hideClarification();
        } else if (m_kbVisible) {
            toggleKeyboard();
        } else {
            // Esc в пустоте — «верни меня в поле». Выделять черновик
            // при этом нельзя: Esc часто жмут, чтобы закрыть панель,
            // и следующий же символ стирал бы набранное.
            m_chatCtl->requestFocus();
        }
        return;
    }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::showAndRaise()
{
    show();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    // Закрыть окно ≠ закрыть JARVIS: ядро создано в main() и продолжает
    // работать. Но если трея в системе нет, прятаться некуда — тогда
    // закрытие означает выход, иначе процесс останется невидимым.
    if (!m_hideOnClose) {
        e->accept();
        return;
    }

    e->ignore();
    hide();
    NotificationManager::instance().showNotification(
        QStringLiteral("J.A.R.V.I.S."),
        IS_EN ? QStringLiteral("Running in the system tray. Right-click to quit.")
              : QStringLiteral("Работает в трее. ПКМ → Выход."),
        NotificationManager::Level::Info);
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
    QString text = m_chatCtl->draft().trimmed();

    const bool hasAttach = !m_jarvis->attachments()->isEmpty();
    if (text.isEmpty() && !hasAttach) return;
    if (text.isEmpty() && hasAttach) {
        appendLog(Str::logSystem(),
                  IS_EN ? QStringLiteral("Write a request — attached files will be sent with it.")
                        : QStringLiteral("Напишите запрос — прикреплённые файлы будут отправлены вместе с ним."),
                  Theme::LogColors::error);
        return;
    }

    m_chatCtl->setDraft(QString());

    // ── 0.1 Ответ на вопрос, который задал сам Джарвис ────────────
    // Панель ответа на экране означает, что вопрос назван и ждёт
    // ответа. Но НЕ означает, что следующая реплика — этот ответ:
    // человек может параллельно попросить что-то ещё. Раньше проверки
    // здесь не было вовсе, и «открой калькулятор» записывалось в
    // CuriosityEngine как ответ на «какой стиль разработки тебе
    // ближе?» — то есть портило данные, на которых Джарвис учится.
    //
    // Проверка — та же самая, что и для вопроса в хвосте реплики
    // (см. 0.1b): раньше она была только там, и два пути расходились.
    //
    // Само сообщение не перехватываем: пусть идёт обычным путём и
    // Джарвис ответит по смыслу. Вопрос уже лежит в памяти сессии,
    // поэтому модель видит ветку «вопрос → ответ», а не реплику из
    // ниоткуда.
    const bool answeringQuestion =
           !m_answerQuestion.isEmpty()
        && DialogCues::looksLikeAnswer(text);

    if (answeringQuestion) {
        // Ядру нужно знать, на что именно человек отвечает: consumeAnswer
        // гасит pending-состояние здесь, и без явной подсказки processCommand
        // разберёт реплику как новую задачу (см. noteAnsweringQuestion).
        m_jarvis->noteAnsweringQuestion(m_answerQuestion);
        CuriosityEngine::instance().consumeAnswer(0, text, 0, /*explicitReply=*/true);
        hideAnswerPrompt();
    }
    // Если реплика ответом не была — панель остаётся на экране.
    // Вопрос действительно не отвечен, и убирать напоминание только
    // потому, что человек написал что-то другое, неправильно: закрыть
    // его можно крестиком.

    // ── 0.1b Ответ на вопрос, которым Джарвис закончил реплику ────
    // Это не вопрос CuriosityEngine и панели ответа под ним нет — модель
    // просто спросила в конце своего ответа («а тебе для чего схема?»).
    // Раньше следующая реплика человека шла как новый запрос: её искали в
    // памяти (и находили не то), а до модели она доходила без всякой
    // связи с вопросом — отсюда «уточню пару деталей» в ответ на
    // развёрнутое объяснение. Признаём её ответом и НЕ ищем в памяти:
    // ответ по определению не является вопросом, который где-то уже
    // задавали.
    const QString jarvisQuestion = m_lastJarvisQuestion;
    const bool replyingToJarvisQuestion =
           !answeringQuestion
        && !jarvisQuestion.isEmpty()
        && m_lastJarvisQuestionAt.isValid()
        && m_lastJarvisQuestionAt.secsTo(QDateTime::currentDateTime()) <= 15 * 60
        && DialogCues::looksLikeAnswer(text);

    // Одноразовый в любом случае: либо на вопрос ответили, либо человек
    // сменил тему и возвращаться к нему уже незачем.
    m_lastJarvisQuestion.clear();

    // ── 0.2 Оценка последнего неуверенного ответа словами ─────────
    // Дублирует кнопки панели (см. onClarificationChoice, префикс
    // "doubt_feedback:") для тех, кто просто пишет, а не жмёт. Срабатывает
    // только в пределах нескольких минут после сомнения, поэтому не
    // проглотит неродственное сообщение.
    // answeringQuestion главнее: «да» в адрес вопроса самого Джарвиса не
    // должно читаться как похвала за какой-то прошлый ответ.
    //
    // Кнопка «Не то — объясню» уже нажата: вся реплика целиком и есть
    // объяснение, разбирать её на маркеры не нужно.
    if (m_awaitingDoubtExplanation) {
        m_awaitingDoubtExplanation = false;
        const qint64 doubtId = m_pendingDoubtId;
        const QString query  = m_doubtQuery;
        m_doubtQuery.clear();
        appendLog(Str::logSender(), text, Theme::LogColors::user);
        applyDoubtVerdict(doubtId, query, DialogCues::Verdict::Wrong, text.trimmed());
        return;
    }

    if (!answeringQuestion
        && m_pendingDoubtId != 0
        && m_pendingDoubtSetAt.secsTo(QDateTime::currentDateTime()) <= 15 * 60)
    {
        DialogCues::Verdict verdict = DialogCues::Verdict::Correct;
        QString explanation;
        if (DialogCues::parseVerdict(text, verdict, explanation)) {
            const qint64 doubtId = m_pendingDoubtId;
            const QString query  = m_pendingInput;
            appendLog(Str::logSender(), text, Theme::LogColors::user);
            hideClarification();   // чистит m_pendingInput / m_pendingDoubtId
            applyDoubtVerdict(doubtId, query, verdict, explanation);
            m_chatCtl->requestFocus();
            return;
        }
    }

    hideClarification();

    // ── 1. Автоопределение языка ──────────────────────────
    // Обновляем язык по каждому сообщению пользователя.
    // Языковая инструкция инжектируется в системный промпт Claude
    // в методе buildClaudeSystemPrompt() через m_langDetector.systemInstruction().
    m_langDetector.update(text);

    // Канные реплики ядра (врезки над ответом, «Понял, спасибо!»,
    // проактивные вопросы CuriosityEngine) идут на языке РАЗГОВОРА, а не
    // на языке интерфейса: при English в настройках и переписке на русском
    // Джарвис задавал свои вопросы по-английски, а отвечал по-русски.
    // gUiLanguage() (подписи меню и кнопок) намеренно не трогаем — это
    // отдельный, явно выбранный пользователем переключатель.
    if (m_langDetector.current() != LanguageDetector::Language::Unknown) {
        const bool convEnglish =
            m_langDetector.current() == LanguageDetector::Language::English;
        m_jarvis->setUiLanguage(convEnglish);
        CuriosityEngine::instance().setUiEnglish(convEnglish);
    }

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
            m_chatCtl->requestFocus();
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
            m_chatCtl->requestFocus();
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
            m_chatCtl->requestFocus();
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
            m_chatCtl->requestFocus();
            return;
        }
    }

    // ── 2. Системные команды (звук, яркость, блокировка и т.д.)
    // Проверяем ДО Brain, потому что "громкость 50" — не вопрос к AI
    if (trySystemControl(text)) {
        m_chatCtl->requestFocus();
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
        m_chatCtl->setAgentName(Str::agentClaude());
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
        m_chatCtl->requestFocus();
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
            m_chatCtl->requestFocus();
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

        m_chatCtl->requestFocus();
        return;
    }

    // ── 5.5 Локальный ответ роутера (Layer 1) — минуем Claude ─
    // m_bypassCacheOnce взводится кнопкой «Неверно»: пользователь уже
    // сказал, что запомненный ответ не годится, и показывать ему тот же
    // ответ второй раз — издевательство. Флаг одноразовый: следующий
    // вопрос снова может отвечаться из памяти.
    if (m_bypassCacheOnce) {
        m_bypassCacheOnce = false;
        qDebug() << "[MainWindow] Cache bypassed after 'Wrong' feedback";
    } else if (intent.hasLocalAnswer() && !replyingToJarvisQuestion) {
        // Ответ из памяти — такой же ответ модели, просто сохранённый, и
        // блок <diagram> в нём тоже есть. Раньше эта ветка печатала его
        // как есть: пользователь получал в чат сырой исходник mermaid
        // вместе с закрывающим тегом, а панель не открывалась вовсе.
        auto localDr = Jarvis::tryRenderDiagram(intent.localResponse);
        const QString shown = localDr.hasDiagram ? localDr.textWithoutDiagram
                                                  : intent.localResponse;

        appendLog(Str::logJarvis(), shown, Theme::LogColors::jarvis);
        if (localDr.hasDiagram && m_visualCtl) {
            if (!localDr.mermaidSource.isEmpty())
                m_visualCtl->showMermaid(localDr.mermaidSource);
            else if (!localDr.image.isNull())
                m_visualCtl->showDiagram(localDr.image);
        }
        // Озвучиваем тоже очищенный текст — читать вслух исходник схемы
        // бессмысленно.
        if (shown.length() <= 300 && m_audioManager->speechAllowed())
            m_jarvis->speakAsync(shown);
        noteJarvisReply(shown);
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
            // Три варианта, а не два. «Верно/Неверно» не покрывали самый
            // частый случай: ответ по теме, но не на тот вопрос — схема
            // есть, но нужна другая; фильм назван, но спрашивали про
            // комедию. «Почти» отправляет вопрос модели заново, сохранив
            // прошлый ответ как черновик, а «Не то» даёт сказать словами,
            // ЧЕМ именно не то, — и это объяснение уходит и в запрос, и в
            // журнал сомнений.
            showClarification(
                IS_EN ? QStringLiteral("Not fully sure about that one — was I right?")
                      : QStringLiteral("Не совсем уверен в этом ответе — я прав?"),
                { IS_EN ? QStringLiteral("\U0001F44D Correct")   : QStringLiteral("\U0001F44D Верно"),
                  IS_EN ? QStringLiteral("\U0001F914 Almost")    : QStringLiteral("\U0001F914 Почти"),
                  IS_EN ? QStringLiteral("\U0001F44E Not it \342\200\224 I'll explain")
                        : QStringLiteral("\U0001F44E Не то \342\200\224 объясню") }
            );
        }

        m_chatCtl->requestFocus();
        return;
    }

    // ── 6. Всё остальное → Jarvis/Claude ─────────────────
    m_lastUserInput      = text;  // сохраняем для самообучения

    // Говорим модели прямо, что это ответ на её же вопрос: сам вопрос
    // лежит в истории сессии, но по истории модель этого не считывает и
    // разбирает реплику как новую задачу. Ставим флаг вплотную к вызову —
    // он живёт ровно один processCommand и не должен протечь в следующий
    // ход, если сюда не дошли.
    if (replyingToJarvisQuestion)
        m_jarvis->noteAnsweringQuestion(jarvisQuestion);

    // НЕ сбрасываем m_lastInputWasVoice здесь — он нужен в onAsyncResponse
    // для записи в voice_journal. Сбросится там после использования.
    QString response = m_jarvis->processCommand(
        text, attachmentBlock, m_langDetector.systemInstruction());

    if (!response.isEmpty()) {
        // Check synchronous response for diagrams too
        auto syncDr = Jarvis::tryRenderDiagram(response);
        if (syncDr.hasDiagram && (!syncDr.svgData.isEmpty() || !syncDr.image.isNull())) {
            appendLog(Str::logJarvis(), syncDr.textWithoutDiagram, Theme::LogColors::jarvis);
            if (m_visualCtl) {
                if (!syncDr.mermaidSource.isEmpty())
                    m_visualCtl->showMermaid(syncDr.mermaidSource);
                else
                    m_visualCtl->showDiagram(syncDr.image);
            }
            if (m_audioManager->speechAllowed()) m_jarvis->speakAsync(syncDr.textWithoutDiagram);
            noteJarvisReply(syncDr.textWithoutDiagram);
        } else {
            appendLog(Str::logJarvis(), response, Theme::LogColors::jarvis);
            if (m_audioManager->speechAllowed()) m_jarvis->speakAsync(response);
            noteJarvisReply(response);
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

    m_chatCtl->requestFocus();
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

    OrganizePlanDialog dlg(m_jarvis, plan, this);
    dlg.exec();
}

// ============================================================
// Панель уточнения Brain
// ============================================================

void MainWindow::showClarification(const QString& question, const QStringList& options)
{
    if (!m_noticeCtl) return;
    m_noticeCtl->showClarify(question, options);
}

void MainWindow::hideClarification()
{
    if (!m_noticeCtl) return;
    m_noticeCtl->hideClarify();
    m_pendingInput.clear();
    m_pendingDoubtId = 0;
}

// ============================================================
// Панель "ты отвечаешь на вопрос Джарвиса"
// ============================================================

void MainWindow::showAnswerPrompt(const QString& question)
{
    if (!m_noticeCtl || question.isEmpty()) return;

    m_answerQuestion = question;

    // Пользователю нужно УЗНАТЬ свой вопрос, а не перечитать его
    // целиком, поэтому длинный обрезаем. Полный остаётся в
    // подсказке самой полосы.
    const QString shown = question.length() > 90
        ? question.left(88) + QStringLiteral("…")
        : question;
    m_noticeCtl->showAnswer(
        (IS_EN ? QStringLiteral("Answering: ") : QStringLiteral("Отвечаешь на: "))
        + shown);

    // Пока полоса на экране — вопрос не должен истечь по таймеру.
    CuriosityEngine::instance().setAnswerHold(true);
}

void MainWindow::hideAnswerPrompt()
{
    if (!m_noticeCtl) return;

    m_answerQuestion.clear();
    // Снятие удержания возвращает обычное истечение по таймеру: вопрос,
    // который пользователь закрыл не ответив, не должен висеть вечно.
    CuriosityEngine::instance().setAnswerHold(false);

    m_noticeCtl->hideAnswer();
}

// ============================================================
//  Оценка неуверенного ответа из памяти
// ============================================================

void MainWindow::applyDoubtVerdict(qint64 doubtId, const QString& query,
                                    DialogCues::Verdict verdict,
                                    const QString& explanation)
{
    const bool correct = (verdict == DialogCues::Verdict::Correct);

    if (doubtId != 0) {
        // Объяснение сохраняем как есть — в журнале сомнений оно ценнее
        // отметки «неверно»: по нему видно, ЧЕМ именно ответ не подошёл.
        SelfJournal::instance().resolveDoubt(
            doubtId, correct,
            !explanation.isEmpty()
                ? explanation
                : (verdict == DialogCues::Verdict::Almost
                       ? QStringLiteral("Close, but not the answer to this question")
                       : QStringLiteral("Rejected by user")));
        // Замыкаем хеббовскую петлю: подтверждённый или отвергнутый ответ
        // усиливает/ослабляет связи, по которым SynapseGraph его нашёл.
        // «Почти» — это тоже промах: вопрос был другой.
        if (!query.isEmpty())
            LlmCacheManager::instance().reportOutcome(
                LlmCacheManager::kDesktopOwnerId, query, correct);
    }

    appendLog(Str::logJarvis(),
        correct
            ? (IS_EN ? QStringLiteral("Good to know — I'll trust that one more next time.")
                     : QStringLiteral("Понял, учту — в следующий раз буду увереннее."))
            : verdict == DialogCues::Verdict::Almost
                ? (IS_EN ? QStringLiteral("Close but not it — asking properly this time.")
                         : QStringLiteral("Близко, но не то — спрашиваю заново, как следует."))
                : (IS_EN ? QStringLiteral("Got it — thanks for explaining. Asking again with that in mind.")
                         : QStringLiteral("Понял, спасибо за пояснение — перезадаю с учётом этого.")),
        Theme::LogColors::jarvis);

    m_pendingDoubtId = 0;
    if (correct || query.isEmpty()) return;

    // Промах — это ещё и незакрытый вопрос: без перезапроса человек
    // остался бы с оценкой вместо ответа. Пояснение уходит прямо в текст
    // запроса, чтобы модель видела, чем прошлый ответ не подошёл, — и
    // чтобы в кэш это легло под собственным ключом, а не поверх старого.
    QString reask = query;
    if (!explanation.isEmpty()) {
        reask += IS_EN
            ? QStringLiteral("\n\n(My previous answer missed the point: %1. "
                             "Answer this question, not that one.)").arg(explanation)
            : QStringLiteral("\n\n(Прошлый ответ был не о том: %1. "
                             "Ответь именно на этот вопрос.)").arg(explanation);
    } else {
        reask += IS_EN
            ? QStringLiteral("\n\n(A remembered answer was close but not right — "
                             "answer this question from scratch.)")
            : QStringLiteral("\n\n(Ответ из памяти был близок, но не точен — "
                             "ответь на этот вопрос заново.)");
    }

    m_bypassCacheOnce = true;
    m_chatCtl->setDraft(reask);
    onSend();
}

void MainWindow::noteJarvisReply(const QString& reply)
{
    m_lastJarvisQuestion = DialogCues::trailingQuestion(reply);
    m_lastJarvisQuestionAt = m_lastJarvisQuestion.isEmpty()
        ? QDateTime() : QDateTime::currentDateTime();
}

void MainWindow::onClarificationChoice(int choice)
{
    if (m_pendingInput.isEmpty()) return;

    // Praise/scold buttons for an uncertain cached answer (see the
    // "5.5 Локальный ответ роутера" branch in onSend, which sets this up).
    if (m_pendingSuggestionAction.startsWith(QStringLiteral("doubt_feedback:"))) {
        const qint64 doubtId = m_pendingSuggestionAction.mid(15).toLongLong();
        // Порядок кнопок: [Верно, Почти, Не то — объясню]
        const DialogCues::Verdict verdict = choice == 1 ? DialogCues::Verdict::Correct
                                   : choice == 2 ? DialogCues::Verdict::Almost
                                                 : DialogCues::Verdict::Wrong;
        const QString feedbackQuery = m_pendingInput;  // hideClarification() чистит его ниже
        hideClarification();

        // «Не то» — единственный вариант, который не решает вопрос сразу:
        // пользователь хочет сказать словами, чем именно не то. Держим
        // сомнение открытым и ждём следующей реплики как объяснения.
        if (verdict == DialogCues::Verdict::Wrong) {
            m_awaitingDoubtExplanation = true;
            m_pendingDoubtId           = doubtId;
            m_pendingDoubtSetAt        = QDateTime::currentDateTime();
            m_doubtQuery               = feedbackQuery;
            m_pendingSuggestionAction.clear();
            m_pendingInput.clear();
            appendLog(Str::logJarvis(),
                IS_EN ? QStringLiteral("What did I get wrong? Tell me in your own words — "
                                       "I'll ask again with that in mind.")
                      : QStringLiteral("Что именно не то? Скажи своими словами — "
                                       "перезадам вопрос с учётом этого."),
                Theme::LogColors::jarvis);
            m_chatCtl->requestFocus();
            return;
        }

        applyDoubtVerdict(doubtId, feedbackQuery, verdict, QString());
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
            m_chatCtl->setDraft(m_pendingInput);
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
    m_chatCtl->setDraft(enriched);
    onSend();
}

// ============================================================
// Slots: TTS
// ============================================================

void MainWindow::onSpeakingChanged(bool speaking)
{
    if (speaking) {
        m_chatCtl->setStatus(IS_EN ? QStringLiteral("Speaking…") : QStringLiteral("Говорю…"),
                             QStringLiteral("speaking"));
    } else {
        m_chatCtl->setStatus(IS_EN ? QStringLiteral("Online") : QStringLiteral("В сети"),
                             QStringLiteral("online"));
    }
}

void MainWindow::onTypingStarted()
{
    m_chatCtl->setStatus(IS_EN ? QStringLiteral("Typing…") : QStringLiteral("Печатаю…"),
                         QStringLiteral("typing"));
}

void MainWindow::onTypingProgress(int current, int total)
{
    m_chatCtl->setStatusText((IS_EN ? QStringLiteral("Typing... %1/%2") : QStringLiteral("Печатаю... %1/%2"))
                      .arg(current).arg(total));
}

void MainWindow::onTypingFinished()
{
    m_chatCtl->setStatus(IS_EN ? QStringLiteral("Online") : QStringLiteral("В сети"),
                         QStringLiteral("online"));
}

// ============================================================
// Slots: API ответы
// ============================================================

void MainWindow::onAsyncResponse(const QString& response, const QString& speechText)
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
    // Вопрос, которым модель закончила реплику, — чтобы следующий ответ
    // человека был прочитан как ответ, а не как новый запрос (см. 0.1b в
    // onSend).
    noteJarvisReply(dr.hasDiagram ? dr.textWithoutDiagram : response);
    if (dr.hasDiagram && (!dr.svgData.isEmpty() || !dr.image.isNull())) {
        appendLog(Str::logJarvis(), dr.textWithoutDiagram, Theme::LogColors::jarvis);
        if (m_visualCtl) {
            if (!dr.mermaidSource.isEmpty())
                m_visualCtl->showMermaid(dr.mermaidSource);
            else
                m_visualCtl->showDiagram(dr.image);
        }

        // Кладём диаграмму на диск и в реестр. До сих пор она жила только
        // в панели просмотра: следующий ответ её затирал, и вернуться к
        // ней было нельзя — при том что нарисована она была полностью.
        //
        // Источник картинки — сама панель: она уже отрисовала mermaid
        // через WebEngine, и её SVG заведомо совпадает с тем, что видит
        // пользователь. Внешний mmdc для этого не нужен; без него
        // MermaidRenderer возвращал PNG с ИСХОДНЫМ ТЕКСТОМ диаграммы —
        // именно эта заглушка и попадала в «Файлы».
        {
            const QString stamp = QDateTime::currentDateTime()
                                      .toString(QStringLiteral("yyyyMMdd_HHmmss"));
            const QString prompt = m_lastUserInput;

            // Фолбэк на то, что отдал MermaidRenderer: используется, когда
            // панели нет, когда диаграмма не mermaid (ASCII-арт), или когда
            // рендер в панели не удался.
            auto saveFallback = [this, stamp, prompt](const QByteArray& svgData,
                                                       const QImage& image) {
                const QString dir = JarvisPaths::subPath(QStringLiteral("diagrams"));
                QDir().mkpath(dir);
                QString saved;
                if (!svgData.isEmpty()) {
                    saved = dir + QStringLiteral("/diagram_%1.svg").arg(stamp);
                    QFile f(saved);
                    if (f.open(QIODevice::WriteOnly)) { f.write(svgData); f.close(); }
                    else saved.clear();
                } else if (!image.isNull()) {
                    saved = dir + QStringLiteral("/diagram_%1.png").arg(stamp);
                    if (!image.save(saved, "PNG")) saved.clear();
                }
                if (!saved.isEmpty()) {
                    ArtifactRegistry::instance().record(
                        saved, QString::fromLatin1(ArtifactRegistry::kDiagram),
                        IS_EN ? QStringLiteral("Diagram") : QStringLiteral("Диаграмма"),
                        prompt);
                }
            };

            if (m_visualCtl && !dr.mermaidSource.isEmpty()) {
                const QByteArray fbSvg   = dr.svgData;
                const QImage     fbImage = dr.image;
                m_visualCtl->exportRendered(
                    [this, stamp, prompt, fbSvg, fbImage, saveFallback]
                    (const QByteArray& svg, const QImage& raster) {
                        if (svg.isEmpty() && raster.isNull()) {
                            saveFallback(fbSvg, fbImage);
                            return;
                        }
                        const QString dir = JarvisPaths::subPath(QStringLiteral("diagrams"));
                        QDir().mkpath(dir);
                        const QString stem = dir + QStringLiteral("/diagram_") + stamp;

                        // Обе формы рядом: SVG масштабируется без потерь и
                        // правится в Inkscape, PNG открывается чем угодно и
                        // им же рисуется превью в окне «Файлы».
                        QString svgPath;
                        if (!svg.isEmpty()) {
                            svgPath = stem + QStringLiteral(".svg");
                            QFile f(svgPath);
                            if (f.open(QIODevice::WriteOnly)) { f.write(svg); f.close(); }
                            else {
                                qWarning() << "[MainWindow] cannot write" << svgPath
                                           << f.errorString();
                                svgPath.clear();
                            }
                        }
                        QString pngPath;
                        if (!raster.isNull()) {
                            pngPath = stem + QStringLiteral(".png");
                            if (!raster.save(pngPath, "PNG")) {
                                qWarning() << "[MainWindow] cannot write" << pngPath;
                                pngPath.clear();
                            }
                        }

                        // В реестр — растр: он и превью показывает как надо,
                        // и открывается любым просмотрщиком. Вектор лежит
                        // рядом, «Показать в папке» его находит.
                        const QString saved = pngPath.isEmpty() ? svgPath : pngPath;
                        if (saved.isEmpty()) { saveFallback(fbSvg, fbImage); return; }
                        ArtifactRegistry::instance().record(
                            saved, QString::fromLatin1(ArtifactRegistry::kDiagram),
                            IS_EN ? QStringLiteral("Diagram") : QStringLiteral("Диаграмма"),
                            prompt);
                    });
            } else {
                saveFallback(dr.svgData, dr.image);
            }
        }
    } else {
        appendLog(Str::logJarvis(), response, Theme::LogColors::jarvis);
    }

    // Dual-response TTS — единственная точка озвучки ответа.
    // Готовую реплику Jarvis присылает вместе с ответом; выводим свою
    // только когда её нет (эмиттеры, которые [SPEECH:] не разбирают).
    // Разбирать response повторно нельзя: маркер из него уже снят, и
    // fallback возвращал первое предложение — оно и звучало вторым.
    if (m_audioManager->speechAllowed()) {
        QString speech = speechText.trimmed();
        if (speech.isEmpty()) {
            const QString& ttsSource = dr.hasDiagram ? dr.textWithoutDiagram : response;
            speech = JarvisResponse::parse(ttsSource).speechText;
        }
        VoiceSynthesisManager::instance().say(speech);
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
    if (m_noticeCtl)
        m_noticeCtl->showSuggestion(QStringLiteral("→ ") + description);
}

void MainWindow::onAgentSelected(const QString& agentName)
{
    if (m_chatCtl) {
        m_chatCtl->setAgentName(agentName);
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
    // Спиннер сам запускает и останавливает свою анимацию по показу/скрытию,
    // поэтому здесь достаточно видимости.
    // Спиннер больше не отдельный виджет: «занят» — это состояние
    // контроллера, а показывает его точка в шапке.
    if (m_chatCtl) m_chatCtl->setBusy(thinking);

    if (thinking) {
        m_chatCtl->setStatus(Str::statusThinking(), QStringLiteral("thinking"));
        m_chatCtl->setInputEnabled(false);
    } else {
        m_chatCtl->setStatus(IS_EN ? QStringLiteral("Online") : QStringLiteral("В сети"),
                             QStringLiteral("online"));
        m_chatCtl->setInputEnabled(true);
        m_chatCtl->requestFocus();
    }
}

// ============================================================
// Панель обновления
// ============================================================

void MainWindow::showUpdateBar(const QString& version)
{
    if (!m_noticeCtl) return;

    m_noticeCtl->showUpdate((IS_EN ? QStringLiteral("Update available v")
                                   : QStringLiteral("Доступно обновление v")) + version);

    appendLog(Str::logSystem(),
              (IS_EN ? QStringLiteral("Update available v") : QStringLiteral("Доступно обновление v")) + version,
              Theme::LogColors::system);
}

void MainWindow::hideUpdateBar()
{
    if (m_noticeCtl) m_noticeCtl->hideUpdate();
}

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
    // Никакой перестройки больше нет: модель сообщает, что
    // изменилось, а ListView сам переиспользует делегаты. Имя метода
    // оставлено — на него завязаны onAttachmentsChanged() и
    // onAttachmentsConsumed().
    if (!m_attachModel) return;
    m_attachModel->setItems(m_jarvis->attachments()->items());
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


    // === Главный экран (QML) ===
    // Шапка, лента и нижняя область — один QQuickWidget с
    // MainScreen.qml. Раньше их было два: лента и композер жили
    // отдельно, и высоту нижней области приходилось синхронизировать
    // с виджетом руками. Внутри одного экрана её считает layout.
    m_screen = new QQuickWidget(this);
    m_screen->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_screen->setClearColor(JarvisTheme::instance().bg());
    JarvisTheme::prepareEngine(m_screen->engine());

    QQmlContext* sc = m_screen->rootContext();
    sc->setContextProperty(QStringLiteral("chatModel"),   m_chat);
    sc->setContextProperty(QStringLiteral("chatCtl"),     m_chatCtl);
    sc->setContextProperty(QStringLiteral("noticeCtl"),   m_noticeCtl);
    sc->setContextProperty(QStringLiteral("attachModel"), m_attachModel);
    sc->setContextProperty(QStringLiteral("welcomeCtl"),  m_welcomeCtl);
    sc->setContextProperty(QStringLiteral("visualCtl"),   m_visualCtl);
    // Команды приложения — сюда же. Пока меню рисует QMenuBar, но модель
    // уже доступна: именно из неё QML-панель команд будет строиться,
    // когда до неё дойдут руки. Обработчики при этом не переезжают.
    sc->setContextProperty(QStringLiteral("actionModel"), m_actionModel);

    m_screen->setSource(QUrl(QStringLiteral("qrc:/qml/MainScreen.qml")));

    // Молча пустое окно — худший из возможных отказов: приложение
    // выглядит рабочим, но интерфейса в нём нет. Ошибку показываем
    // текстом прямо на месте экрана.
    if (m_screen->status() == QQuickWidget::Error) {
        QStringList lines;
        for (const QQmlError& e : m_screen->errors())
            lines << e.toString();
        const QString text = lines.join(QStringLiteral("\n"));
        qWarning() << "[MainWindow] MainScreen.qml failed:" << text;

        auto* err = new QLabel(this);
        err->setWordWrap(true);
        err->setTextInteractionFlags(Qt::TextSelectableByMouse);
        err->setContentsMargins(16, 16, 16, 16);
        err->setText((IS_EN ? QStringLiteral("The main screen failed to load:\n\n")
                            : QStringLiteral("Главный экран не загрузился:\n\n")) + text);
        vbox->addWidget(err, 1);
        m_screen->deleteLater();
        m_screen = nullptr;
    } else {
        vbox->addWidget(m_screen, 1);
    }

    // Намерения из композера. Без этих трёх строк поле ввода
    // выглядит рабочим, но Enter и кнопка отправки не делают ничего:
    // send() исправно эмитит сигнал, а слушать его некому.
    connect(m_chatCtl, &ChatController::sendRequested,
            this, [this](const QString&) { onSend(); });
    connect(m_chatCtl, &ChatController::micToggleRequested,
            this, &MainWindow::onMicButtonClicked);
    connect(m_chatCtl, &ChatController::attachRequested,
            this, &MainWindow::onAttachClicked);

    connect(m_chatCtl, &ChatController::menuRequested, this, [this]() {
        auto* popup = new QMenu(this);
        popup->setStyleSheet(qApp->styleSheet());
        for (auto* action : menuBar()->actions())
            popup->addAction(action);
        popup->exec(mapToGlobal(QPoint(16, 56)));
        popup->deleteLater();
    });

    // === Visual Insights — full-height side panel (right of chat) ===
    // Боковая панель переехала в MainScreen.qml как правая колонка:
    // отдельного виджета в hroot больше нет, раскрытием управляет
    // сама панель через visualCtl.open.
    connect(m_visualCtl, &VisualInsightsController::saveRequested,
            this, &MainWindow::onSaveInsight);

    // === Полосы уведомлений и вложения ===
    // Пять QWidget-полос (подсказка, уточнение, ответ, обновление,
    // вложения) заменены на NoticeController + AttachmentModel:
    // «показана ли полоса» стало данными, а раскрытие ведёт QML —
    // только он знает настоящую высоту содержимого. Раньше каждая
    // полоса на каждый показ и на каждое скрытие заводила свой
    // QPropertyAnimation по maximumHeight.
    connect(m_noticeCtl, &NoticeController::suggestionAccepted, this, [this]() {
        if (m_pendingSuggestionAction.isEmpty()) return;
        // Сначала пробуем запустить как приложение, и только если не
        // вышло — отправляем текстом в чат.
        auto result = m_appLauncher.launch(m_pendingSuggestionAction);
        if (result.success) {
            appendLog(Str::logJarvis(),
                (IS_EN ? QStringLiteral("Launching: ") : QStringLiteral("Запускаю: "))
                    + m_pendingSuggestionAction,
                Theme::LogColors::jarvis);
        } else {
            m_chatCtl->setDraft(m_pendingSuggestionAction);
            onSend();
        }
    });

    // Отклонённая подсказка не должна оставлять за собой действие:
    // иначе «Да» на следующей подсказке могло бы запустить прошлое.
    connect(m_noticeCtl, &NoticeController::suggestionDismissed,
            this, [this]() { m_pendingSuggestionAction.clear(); });

    connect(m_noticeCtl, &NoticeController::clarifyChosen,
            this, &MainWindow::onClarificationChoice);
    connect(m_noticeCtl, &NoticeController::clarifyDismissed,
            this, &MainWindow::hideClarification);
    connect(m_noticeCtl, &NoticeController::answerDismissed,
            this, &MainWindow::hideAnswerPrompt);

    connect(m_noticeCtl, &NoticeController::updateAccepted, this, [this]() {
        // Куда ведёт кнопка, решает состояние загрузки, а не то, что
        // кто-то успел переписать её обработчик.
        if (!m_downloadedUpdatePath.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(
                QFileInfo(m_downloadedUpdatePath).absolutePath()));
            return;
        }
        m_jarvis->autoUpdater()->downloadPendingUpdate();
    });

    connect(m_attachModel, &AttachmentModel::removeRequested, this, [this](int row) {
        m_jarvis->attachments()->removeAt(row);
    });


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
    connect(kbBtn, &QPushButton::clicked, this, &MainWindow::toggleKeyboard);
    connect(m_likeBtn, &QPushButton::clicked, this, &MainWindow::onLikeLastResponse);

    // ── Голосовой ввод ────────────────────────────────────
    // Микрофон принадлежит ядру (см. Jarvis::voiceInput). Окно на него
    // подписывается: показывает уровень сигнала, диалоги установки
    // моделей и отправляет распознанный текст. Указатель не владеющий —
    // удалять его вместе с окном нельзя.
    m_voiceInput = m_jarvis->voiceInput();

    connect(m_voiceInput, &VoiceInput::ready,
            this, &MainWindow::onVoiceReady);

    // К textRecognized окно не подключается: речь входит в ядро, и уже
    // оно решает, кому её отдать (Jarvis::submitVoiceCommand). Окно
    // объявляет себя интерактивной сессией — тем, у кого есть диалог,
    // панели вопросов и прикреплённые файлы.
    m_jarvis->setVoiceHandler([this](const QString& text, const QString& lang) {
        onVoiceText(text, lang);
        return true;
    });

    connect(m_voiceInput, &VoiceInput::wakeWordDetected,
            this, &MainWindow::onWakeWord);
    connect(m_voiceInput, &VoiceInput::volumeLevel, this, [this](float db) {
        // Раньше индикатора: перебивание не зависит от того, открыт ли
        // чат, и решается по уровню, а не по факту распознавания —
        // ждать текст от Vosk означало бы обрываться через секунду.
        maybeBargeIn(db);

        if (!m_voiceActive || !m_chatCtl) return;
        // Нормализуем -60..-20dB → 0..100%
        bool speaking = (db > -45.0f);
        // Меняем цвет кнопки: зелёный = говорит, обычный = тишина
        m_chatCtl->setMicSpeaking(speaking);
        m_chatCtl->setMicTooltip(
            QStringLiteral("🎤 %1 | Level: %2 dB")
                .arg(speaking
                    ? (IS_EN ? QStringLiteral("Speaking...") : QStringLiteral("Говорю..."))
                    : (IS_EN ? QStringLiteral("Listening")   : QStringLiteral("Слушаю")))
                .arg(static_cast<int>(db)));
    });

    connect(m_voiceInput, &VoiceInput::whisperModeDetected,
            this, &MainWindow::onWhisperMode);
    connect(m_voiceInput, &VoiceInput::speechDetected, this, [this]() {
        m_chatCtl->setMicSpeaking(true);
    });

    // Политика голоса промолчала — но событие от этого не исчезло.
    // Notify означает «покажи глазами»: иначе тактичность превращается
    // в потерю информации.
    connect(&VoiceSynthesisManager::instance(),
            &VoiceSynthesisManager::speechSuppressed, this,
            [this](const SpeechRequest& req, VoiceDecision decision) {
        if (decision != VoiceDecision::Notify)
            return;   // Silent — значит и показывать не просили

        const bool alarming = (req.style == SpeechStyle::Warning
                               || req.style == SpeechStyle::Critical);

        NotificationManager::instance().showNotification(
            QStringLiteral("JARVIS"),
            req.text,
            alarming ? NotificationManager::Level::Warning
                     : NotificationManager::Level::Info);
    });
    connect(m_voiceInput, &VoiceInput::initError, this, [this](const QString& err) {
        m_chatCtl->setMicEnabled(false);
        m_chatCtl->setMicTooltip(err);
        appendLog(Str::logSystem(), QStringLiteral("🎤 ") + err, Theme::LogColors::error);
    });

    // Vosk: показываем диалог выбора моделей при первом запуске
    // или когда DLL есть но модели не установлены
    connect(m_voiceInput, &VoiceInput::setupRequired, this, [this]() {
        m_chatCtl->setMicEnabled(false);
        m_chatCtl->setMicGlyph(QStringLiteral("⬇"));

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
        m_chatCtl->setMicTooltip(
            QStringLiteral("Installing Vosk [%1]: %2%%3").arg(component).arg(pct).arg(totalStr));
        m_chatCtl->setStatusText(
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
            m_chatCtl->setMicGlyph(QStringLiteral("🎤"));
            m_chatCtl->setStatusText(IS_EN ? QStringLiteral("Ready") : QStringLiteral("Готов"));
            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("🎉 Vosk setup complete! Voice input is ready.")
                      : QStringLiteral("🎉 Vosk установлен! Голосовой ввод готов."),
                Theme::LogColors::system);
        } else {
            m_chatCtl->setMicEnabled(false);
            m_chatCtl->setMicGlyph(QStringLiteral("❌"));
            m_chatCtl->setMicTooltip(err);
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
        m_chatCtl->setStatusText(QStringLiteral("⬇ %1%%2").arg(pct).arg(totalStr));
    });

    connect(m_voiceInput, &VoiceInput::modelDownloadFinished, this,
            [this](const QString& modelId, bool success) {
        m_chatCtl->setStatusText(IS_EN ? QStringLiteral("Ready") : QStringLiteral("Готов"));
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

    // Vosk запускает не окно, а ядро — из Jarvis::startVoice(), после
    // того как эти connect'ы уже стоят (см. main.cpp).

    // ── Пассивный слушатель ──────────────────────────────────
    // Тоже принадлежит ядру: инициализация на общих моделях Vosk и
    // автостарт записи живут в Jarvis. Окну остаётся строка в ленте.
    m_passiveListener = m_jarvis->passiveListener();

    connect(m_passiveListener, &PassiveListener::ready, this, [this]() {
        // Галочку в меню обновлять не нужно: пункт спрашивает состояние
        // у самого слушателя (см. registerAppActions), а не хранит своё.
        appendLog(Str::logSystem(),
            IS_EN ? QStringLiteral("🎙️ Passive voice recording started — every phrase saved to training dataset")
                  : QStringLiteral("🎙️ Пассивная запись запущена — каждая фраза сохраняется в датасет"),
            Theme::LogColors::system);
    });

    connect(m_keyboard, &VirtualKeyboardWidget::charPressed,
            m_chatCtl, &ChatController::insertText);
    connect(m_keyboard, &VirtualKeyboardWidget::backspacePressed,
            m_chatCtl, &ChatController::backspace);
    connect(m_keyboard, &VirtualKeyboardWidget::enterPressed, this, &MainWindow::onSend);
}

// ============================================================
// appendLog
// ============================================================

void MainWindow::previewIfSingleImage(const QStringList& filePaths)
{
    if (filePaths.size() != 1 || !m_visualCtl) return;

    static const QStringList imgExts = {
        QStringLiteral("png"),  QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("bmp"),  QStringLiteral("gif"), QStringLiteral("webp"),
        QStringLiteral("tiff"), QStringLiteral("ico"),
    };
    const QString ext = QFileInfo(filePaths.first()).suffix().toLower();
    if (imgExts.contains(ext))
        m_visualCtl->showImageFile(filePaths.first());
}

// ============================================================
//  Команды приложения
// ============================================================

// ============================================================
//  Юридические тексты
// ============================================================
//
// Оба диалога жили лямбдами внутри buildMenuBar и занимали там
// без малого двести строк. К сборке меню они отношения не имеют:
// пункт меню только зовёт их по имени.

void MainWindow::showEulaDialog()
{
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
}

void MainWindow::showPrivacyDialog()
{
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
<p>When using Claude, your messages are sent to its API.
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
<p>При использовании Claude ваши сообщения отправляются на его серверы.
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
}

// ============================================================
//  Части меню, которые не выражаются списком команд
// ============================================================
//
// Выбор языка — радиогруппа: «включить» тут нельзя, можно только
// «выбрать один из». Плоский список команд такого не выражает,
// поэтому подменю строится руками и живёт рядом с реестром.

void MainWindow::buildLanguageMenu(QMenu* parent)
{
    auto* langMenu = parent->addMenu(Str::menuLanguage());

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
}

// Диалог голосовых моделей: 44 строки сборки виджетов, которым
// в обработчике пункта меню делать нечего.
void MainWindow::showVoiceModelsDialog()
{
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
}

// Пара перевода — снова радиогруппа: выбирается одно направление
// из шести, командой это не выражается.
void MainWindow::buildTranslationPairMenu(QMenu* parent)
{
    auto* transMenu = parent->addMenu(
        IS_EN ? QStringLiteral("Translation Pair") : QStringLiteral("Пара перевода"));

    struct LangPair { QString label; QString src; QString tgt; };
    const QList<LangPair> pairs = {
        {QStringLiteral("FR -> EN"), QStringLiteral("fr"), QStringLiteral("en")},
        {QStringLiteral("FR -> RU"), QStringLiteral("fr"), QStringLiteral("ru")},
        {QStringLiteral("EN -> RU"), QStringLiteral("en"), QStringLiteral("ru")},
        {QStringLiteral("RU -> EN"), QStringLiteral("ru"), QStringLiteral("en")},
        {QStringLiteral("EN -> FR"), QStringLiteral("en"), QStringLiteral("fr")},
        {QStringLiteral("RU -> FR"), QStringLiteral("ru"), QStringLiteral("fr")},
    };

    auto* transGroup = new QActionGroup(transMenu);
    transGroup->setExclusive(true);

    auto* engine = m_jarvis->translationEngine();
    for (const auto& p : pairs) {
        auto* act = transMenu->addAction(p.label);
        act->setCheckable(true);
        // Отметку берём у движка, а не хардкодим FR->EN: меню
        // пересобирается на каждый показ и должно показывать правду.
        act->setChecked(engine && engine->sourceLang() == p.src
                        && engine->targetLang() == p.tgt);
        transGroup->addAction(act);

        const QString src = p.src, tgt = p.tgt;
        connect(act, &QAction::triggered, this, [this, src, tgt]() {
            m_jarvis->translationEngine()->setSourceLang(src);
            m_jarvis->translationEngine()->setTargetLang(tgt);
            appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                QStringLiteral("Translation pair: %1 -> %2").arg(src.toUpper(), tgt.toUpper()),
                Theme::LogColors::system);
        });
    }
}

void MainWindow::showAudioFileDialog()
{
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
}

void MainWindow::showAnalyticsDialog()
{
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
}

// ============================================================
//  Телефон и сервер
// ============================================================
//
// Три диалога на 215, 103 и 203 строки. В сборке меню им было
// делать нечего: пункт только называет их по имени.

void MainWindow::showMobileSyncDialog()
{
    auto* mesh = m_jarvis->meshConnector();
    if (!mesh) return;
    mesh->initMobilePairing();
    auto* pairing = mesh->mobilePairing();
    if (!pairing) return;

    // Generate a fresh PIN
    PairingSession session = pairing->generatePairingPin(
        QStringLiteral("Developer"));
    pairing->startGatewayPolling();
    // Reported by the code that actually starts the polling — until
    // this runs, mobile sync is genuinely not available and the
    // manifest says so.
    SystemManifest::setRuntimeState(QStringLiteral("mobile_sync"), true,
        QStringLiteral("gateway polling active, awaiting pairing"));

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
}

void MainWindow::showWakeOnLanDialog()
{
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
}

void MainWindow::showTelegramDialog()
{
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
        m_visualCtl->showDiagram(img);
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
}

// ============================================================
//  Камера: вспомогательные операции
// ============================================================

// Центр зрения открывается из нескольких мест (пункт меню «Камера»,
// команда в палитре, подсказки). Диалогу нужны две вещи, которых у него
// нет: обучение лица по файлам и скриншот — их умеет только окно.
void MainWindow::openVisionCenter(int initialTab)
{
    VisionCenterDialog dlg(m_securityCam, this, initialTab);

    connect(&dlg, &VisionCenterDialog::enrollFromPhotoRequested,
            this, [this]() { enrollFaceFromPhoto(); });
    connect(&dlg, &VisionCenterDialog::screenshotRequested,
            this, [this]() { takeScreenshotToArtifacts(); });
    connect(&dlg, &VisionCenterDialog::liveViewRequested, this, [this, &dlg]() {
        CameraViewDialog view(m_securityCam, &dlg);
        view.exec();
    });

    dlg.exec();
}

// Подписки на сигналы камеры делаются РОВНО ОДИН РАЗ за жизнь
// приложения: камера — общий объект, и повторное взведение
// охраны раньше добавляло вторую копию каждого обработчика
// (из-за чего видео уходило в Telegram дважды на одно событие).
void MainWindow::wireSecurityCameraUi()
{
    // Раньше сюда попадали только из-под проверки «камера есть»; теперь
    // подписки ставятся при построении меню, до всяких проверок, и
    // защита нужна здесь, а не у каждого вызывающего. Без неё связывание
    // с нулевым отправителем — два десятка предупреждений Qt в лог и
    // взведённый m_guardUiWired, из-за которого настоящие подписки уже
    // никогда не поставятся.
    if (m_guardUiWired || !m_securityCam)
        return;
    m_guardUiWired = true;


// Сеанс запирает Windows, показывать нечего — но записать в ленту есть что.
connect(m_securityCam, &SecurityCamera::screenLocked, this, [this]() {
    appendLog(QStringLiteral("🛡 Security"),
        IS_EN ? QStringLiteral("🔒 Session locked")
              : QStringLiteral("🔒 Сеанс заблокирован"),
        Theme::LogColors::jarvis);
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
}

void MainWindow::enrollFaceFromPhoto()
{
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

    // Обучение по файлам камеру не открывает, так что здесь
    // отдельный объект безопасен и живёт ровно до конца разбора.
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
}

void MainWindow::takeScreenshotToArtifacts()
{
    QScreen* screen = QApplication::primaryScreen();
    if (!screen) return;
    QPixmap shot = screen->grabWindow(0);
    const QString dir = JarvisPaths::subPath(QStringLiteral("screenshots"));
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/screenshot_%1.png")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    shot.save(path, "PNG");
    ArtifactRegistry::instance().record(
        path, QString::fromLatin1(ArtifactRegistry::kScreenshot),
        IS_EN ? QStringLiteral("Screen capture") : QStringLiteral("Снимок экрана"));
    appendLog(Str::logJarvis(),
        (IS_EN ? QStringLiteral("📸 Screenshot saved: ") : QStringLiteral("📸 Скриншот сохранён: ")) + path,
        Theme::LogColors::jarvis);
}

void MainWindow::registerAppActions()
{
    // ---------------------------------------------------------
    //  Файл
    // ---------------------------------------------------------
    addOwnedAction({
        QStringLiteral("file.attach"), Str::menuAttach(),
        QStringLiteral("📎"), QStringLiteral("Ctrl+O"), QStringLiteral("file"),
        IS_EN ? QStringLiteral("Attach a file to the conversation")
              : QStringLiteral("Приложить файл к разговору"),
        [this]() { onAttachClicked(); }, nullptr
    });

    addOwnedAction({
        QStringLiteral("file.clearAttach"), Str::menuClearAttach(),
        QStringLiteral("✕"), QString(), QStringLiteral("file"),
        IS_EN ? QStringLiteral("Drop everything attached")
              : QStringLiteral("Убрать все вложения"),
        [this]() { m_jarvis->attachments()->clear(); },
        // Нечего убирать — пункт неактивен: раньше он был всегда кликабелен
        // и молча ничего не делал.
        [this]() { return m_jarvis->attachments()->count() > 0; }
    });

    addOwnedAction({
        QStringLiteral("file.searchHistory"),
        IS_EN ? QStringLiteral("Search chat history") : QStringLiteral("Поиск по истории"),
        QStringLiteral("⌕"), QString(), QStringLiteral("file"),
        IS_EN ? QStringLiteral("Ask JARVIS to recall a past conversation")
              : QStringLiteral("Попросить JARVIS вспомнить прошлый разговор"),
        [this]() {
            bool ok = false;
            const QString query = QInputDialog::getText(this,
                IS_EN ? QStringLiteral("Search") : QStringLiteral("Поиск"),
                IS_EN ? QStringLiteral("Search chat history:")
                      : QStringLiteral("Поиск по истории:"),
                QLineEdit::Normal, QString(), &ok);
            if (!ok || query.trimmed().isEmpty())
                return;
            m_chatCtl->setDraft(QStringLiteral("вспомни ") + query.trimmed());
            onSend();
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("file.reopenPanel"),
        IS_EN ? QStringLiteral("Reopen attachments panel")
              : QStringLiteral("Открыть панель вложений"),
        QStringLiteral("🖼"), QString(), QStringLiteral("file"),
        IS_EN ? QStringLiteral("Show attachments from this session again")
              : QStringLiteral("Показать вложения этой сессии снова"),
        [this]() {
            if (!m_visualCtl)
                return;
            if (!m_visualCtl->hasHistory()) {
                appendLog(Str::logSystem(),
                    IS_EN ? QStringLiteral("No attachments yet this session.")
                          : QStringLiteral("Пока нет вложений за эту сессию."),
                    Theme::LogColors::system);
                return;
            }
            m_visualCtl->reopen();
        },
        [this]() { return m_visualCtl && m_visualCtl->hasHistory(); }
    });

    addOwnedAction({
        QStringLiteral("file.clearLog"), Str::menuClearLog(),
        QStringLiteral("🧹"), QString(), QStringLiteral("file"),
        IS_EN ? QStringLiteral("Clear the visible conversation")
              : QStringLiteral("Очистить видимый разговор"),
        [this]() { m_chat->clear(); }, nullptr
    });

    addOwnedAction({
        QStringLiteral("file.exit"), Str::menuExit(),
        QStringLiteral("⏻"), QString(), QStringLiteral("file"),
        IS_EN ? QStringLiteral("Close JARVIS") : QStringLiteral("Закрыть JARVIS"),
        [this]() { close(); }, nullptr
    });


    // ---------------------------------------------------------
    //  Камера и охрана
    // ---------------------------------------------------------
    //
    // Меню было плоским списком из двенадцати пунктов со значком у
    // каждого. Значки в списке одного смысла не различают, а только
    // шумят, поэтому здесь их нет. Обучение лиц не дублируется: ему
    // место в Центре зрения, куда и отсылает подпись под камерой.
    addOwnedAction({
        QStringLiteral("camera.visionCenter"),
        IS_EN ? QStringLiteral("Vision Center") : QStringLiteral("Центр зрения"),
        QString(), QString(), QStringLiteral("camera"),
        IS_EN ? QStringLiteral("Known faces, teaching, camera and OCR state")
              : QStringLiteral("Известные лица, обучение, состояние камеры и OCR"),
        [this]() { openVisionCenter(0); }, nullptr
    });

#ifdef JARVIS_HAS_OPENCV
    // Обучение по фотографиям — единственный способ добавить ЧУЖОЕ
    // лицо: Центр зрения умеет учить только владельца, с камеры.
    addOwnedAction({
        QStringLiteral("camera.enrollPhoto"),
        IS_EN ? QStringLiteral("Teach a face from photos")
              : QStringLiteral("Обучить лицо по фотографиям"),
        QString(), QString(), QStringLiteral("camera"),
        IS_EN ? QStringLiteral("Add someone else from image files")
              : QStringLiteral("Добавить другого человека из файлов изображений"),
        [this]() { enrollFaceFromPhoto(); }, nullptr
    });

    addOwnedAction({
        QStringLiteral("camera.whoIsThere"),
        IS_EN ? QStringLiteral("Who's on camera") : QStringLiteral("Кто перед камерой"),
        QString(), QString(), QStringLiteral("camera"),
        IS_EN ? QStringLiteral("Live view with face boxes and confidence")
              : QStringLiteral("Живой вид с рамками лиц и уверенностью"),
        [this]() {
            CameraViewDialog dlg(m_securityCam, this);
            dlg.exec();
        }, nullptr
    });

    // Охрана одним переключателем вместо пары «включить / выключить».
    // Состояние спрашивается у камеры, поэтому /security из Telegram и
    // этот пункт больше не могут разойтись.
    {
        AppAction guard;
        guard.id        = QStringLiteral("camera.guard");
        guard.title     = IS_EN ? QStringLiteral("Security guard")
                                : QStringLiteral("Охрана");
        guard.group     = QStringLiteral("camera");
        guard.hint      = IS_EN ? QStringLiteral("Check every minute, lock and unlock by face")
                                : QStringLiteral("Проверка раз в минуту, блокировка и разблокировка по лицу");
        guard.checkable = true;
        guard.enabled   = [this]() { return m_securityCam != nullptr; };
        guard.checked   = [this]() { return m_securityCam && m_securityCam->isMonitoring(); };
        guard.run       = [this]() {
            if (!m_securityCam)
                return;

            if (m_securityCam->isMonitoring()) {
                // Останавливаем, но не разрушаем: объект общий, /security
                // в Telegram может взвести его снова. Запертый сеанс при
                // этом остаётся запертым — выключение охраны не должно
                // (и не может) впускать кого-то обратно за машину.
                m_securityCam->stopMonitoring();
                appendLog(Str::logJarvis(),
                    IS_EN ? QStringLiteral("Security guard stopped.")
                          : QStringLiteral("Охрана выключена."),
                    Theme::LogColors::jarvis);
                return;
            }

            wireSecurityCameraUi();
            m_securityCam->startMonitoring(60);
            appendLog(Str::logJarvis(),
                IS_EN ? QStringLiteral("Security guard active (1 min interval). "
                                       "Auto-lock/unlock by face. Video alerts to Telegram.")
                      : QStringLiteral("Охрана включена (интервал 1 мин). "
                                       "Блокировка и разблокировка по лицу. Видео в Telegram."),
                Theme::LogColors::jarvis);
        };
        addOwnedAction(guard);
    }

    addOwnedAction({
        QStringLiteral("camera.lockNow"),
        IS_EN ? QStringLiteral("Lock screen now") : QStringLiteral("Заблокировать экран"),
        QString(), QString(), QStringLiteral("camera"),
        QString(),
        [this]() { if (m_securityCam) m_securityCam->lockScreen(); },
        [this]() { return m_securityCam && !m_securityCam->isScreenLocked(); }
    });


    // Флажки охраны. «Разблокировка по моему лицу» отсюда убрана вместе
    // с оверлеем: сеанс запирает Windows, а снять его блокировку из
    // программы нельзя — узнавание лица на входе делает Windows Hello.
    {
        struct Toggle {
            const char* id;
            QString     titleEn;
            QString     titleRu;
            QString     hintEn;
            QString     hintRu;
            bool (SecurityCamera::*get)() const;
            void (SecurityCamera::*set)(bool);
        };
        const QVector<Toggle> toggles = {
            { "camera.autoLock",
              QStringLiteral("Lock when I leave"),
              QStringLiteral("Блокировать, когда ухожу"),
              QStringLiteral("A stranger or an empty chair locks the screen"),
              QStringLiteral("Чужой в кадре или пустое кресло — экран блокируется"),
              &SecurityCamera::autoLockOnThreat, &SecurityCamera::setAutoLockOnThreat },
            { "camera.motionAlert",
              QStringLiteral("Motion alerts with video"),
              QStringLiteral("Оповещения о движении с видео"),
              QStringLiteral("A 20-second clip goes to Telegram"),
              QStringLiteral("Клип на 20 секунд уходит в Telegram"),
              &SecurityCamera::alertOnMotion, &SecurityCamera::setAlertOnMotion },
        };

        for (const Toggle& t : toggles) {
            AppAction a;
            a.id        = QString::fromLatin1(t.id);
            a.title     = IS_EN ? t.titleEn : t.titleRu;
            a.hint      = IS_EN ? t.hintEn  : t.hintRu;
            a.group     = QStringLiteral("camera.guard");
            a.checkable = true;
            a.enabled   = [this]() { return m_securityCam != nullptr; };

            auto get = t.get;
            auto set = t.set;
            a.checked = [this, get]() { return m_securityCam && (m_securityCam->*get)(); };
            a.run     = [this, get, set]() {
                if (m_securityCam)
                    (m_securityCam->*set)(!(m_securityCam->*get)());
            };
            addOwnedAction(a);
        }
    }
#endif

    addOwnedAction({
        QStringLiteral("camera.screenshot"),
        IS_EN ? QStringLiteral("Take screenshot") : QStringLiteral("Сделать скриншот"),
        QString(), QString(), QStringLiteral("camera"),
        QString(), [this]() { takeScreenshotToArtifacts(); }, nullptr
    });



    // ---------------------------------------------------------
    //  Пользователь
    // ---------------------------------------------------------
    addOwnedAction({
        QStringLiteral("user.center"),
        IS_EN ? QStringLiteral("User Center") : QStringLiteral("Управление пользователями"),
        QStringLiteral("👤"), QString(), QStringLiteral("user"),
        IS_EN ? QStringLiteral("Switch, edit and delete user profiles")
              : QStringLiteral("Переключить, изменить и удалить профили"),
        [this]() {
            UserCenterDialog dlg(m_jarvis, IS_EN, this);
            connect(&dlg, &UserCenterDialog::userSwitched, this, [this]() {
                appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                    IS_EN ? QStringLiteral("User profile updated.")
                          : QStringLiteral("Профиль пользователя обновлён."),
                    Theme::LogColors::system);
            });
            dlg.exec();
        }, nullptr
    });

    // ---------------------------------------------------------
    //  Телефон и Сервер
    // ---------------------------------------------------------
    addOwnedAction({
        QStringLiteral("phone.mobileSync"),
        IS_EN ? QStringLiteral("Mobile Sync") : QStringLiteral("Мобильная синхронизация"),
        QStringLiteral("📱"), QString(), QStringLiteral("phone"),
        IS_EN ? QStringLiteral("Pair a phone with a PIN, no configuration")
              : QStringLiteral("Спарить телефон по PIN, без настройки"),
        [this]() { showMobileSyncDialog(); }, nullptr
    });

    addOwnedAction({
        QStringLiteral("phone.wol"),
        IS_EN ? QStringLiteral("Wake-on-LAN shortcut")
              : QStringLiteral("Ярлык Wake-on-LAN"),
        QStringLiteral("⏻"), QString(), QStringLiteral("phone"),
        IS_EN ? QStringLiteral("Build a shortcut that wakes this PC from the phone")
              : QStringLiteral("Собрать ярлык, будящий этот ПК с телефона"),
        [this]() { showWakeOnLanDialog(); }, nullptr
    });

    addOwnedAction({
        QStringLiteral("phone.telegram"),
        IS_EN ? QStringLiteral("Telegram gateway") : QStringLiteral("Шлюз Telegram"),
        QStringLiteral("✈"), QString(), QStringLiteral("phone"),
        IS_EN ? QStringLiteral("Talk to JARVIS from anywhere through Telegram")
              : QStringLiteral("Общаться с JARVIS откуда угодно через Telegram"),
        [this]() { showTelegramDialog(); }, nullptr
    });

    // ---------------------------------------------------------
    //  Система
    // ---------------------------------------------------------
    {
        AppAction keep;
        keep.id        = QStringLiteral("system.keepAttachments");
        keep.title     = Str::menuKeepAttach();
        keep.icon      = QStringLiteral("📎");
        keep.group     = QStringLiteral("system");
        keep.checkable = true;
        keep.checked   = [this]() { return m_jarvis->attachments()->keepAfterSend(); };
        keep.run       = [this]() {
            const bool next = !m_jarvis->attachments()->keepAfterSend();
            m_jarvis->attachments()->setKeepAfterSend(next);
            appendLog(Str::logSystem(),
                      next ? Str::statusAttachKept() : Str::statusAttachOneShot(),
                      Theme::LogColors::system);
        };
        addOwnedAction(keep);
    }

    addOwnedAction({
        QStringLiteral("system.keyboard"), Str::menuKeyboard(),
        QStringLiteral("⌨"), QString(), QStringLiteral("system"),
        QString(), [this]() { toggleKeyboard(); }, nullptr
    });

    addOwnedAction({
        QStringLiteral("system.translateClipboard"),
        IS_EN ? QStringLiteral("Translate clipboard")
              : QStringLiteral("Перевести буфер обмена"),
        QStringLiteral("🌐"), QString(), QStringLiteral("system"),
        IS_EN ? QStringLiteral("Translate and put the result back into the clipboard")
              : QStringLiteral("Перевести и положить результат обратно в буфер"),
        [this]() {
            const QString text = QApplication::clipboard()->text().trimmed();
            if (text.isEmpty()) {
                appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                    IS_EN ? QStringLiteral("Clipboard is empty.")
                          : QStringLiteral("Буфер обмена пуст."),
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
                        appendLog(QStringLiteral("J.A.R.V.I.S."), result,
                                  Theme::LogColors::jarvis);
                        QApplication::clipboard()->setText(result);
                    } else {
                        appendLog(IS_EN ? QStringLiteral("Error") : QStringLiteral("Ошибка"),
                                  result, Theme::LogColors::error);
                    }
                });
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("system.audioFile"),
        IS_EN ? QStringLiteral("Process audio file") : QStringLiteral("Обработать аудиофайл"),
        QStringLiteral("🎧"), QString(), QStringLiteral("system"),
        IS_EN ? QStringLiteral("Transcribe and summarise a recording")
              : QStringLiteral("Расшифровать запись и сделать выжимку"),
        [this]() { showAudioFileDialog(); }, nullptr
    });

    addOwnedAction({
        QStringLiteral("system.analytics"),
        IS_EN ? QStringLiteral("User analytics") : QStringLiteral("Аналитика"),
        QStringLiteral("📊"), QString(), QStringLiteral("system"),
        QString(), [this]() { showAnalyticsDialog(); }, nullptr
    });

    addOwnedAction({
        QStringLiteral("system.checkUpdate"), Str::menuCheckUpdate(),
        QStringLiteral("⭳"), QString(), QStringLiteral("system"),
        QString(),
        [this]() {
            appendLog(Str::logSystem(), Str::updChecking(), Theme::LogColors::system);
            m_jarvis->autoUpdater()->checkForUpdates(false);
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("system.releases"), Str::menuReleasePage(),
        QStringLiteral("🌍"), QString(), QStringLiteral("system"),
        QString(),
        []() {
            QDesktopServices::openUrl(
                QUrl(QStringLiteral("https://github.com/Bohdan99py/jarvis/releases")));
        }, nullptr
    });

    // ---------------------------------------------------------
    //  Модели и ИИ
    // ---------------------------------------------------------
    addOwnedAction({
        QStringLiteral("models.apiKey"), Str::menuApiKey(),
        QStringLiteral("🔑"), QString(), QStringLiteral("models"),
        IS_EN ? QStringLiteral("Set the Anthropic API key")
              : QStringLiteral("Задать ключ Anthropic API"),
        [this]() {
            bool ok = false;
            const QString key = QInputDialog::getText(this,
                Str::dlgApiKeyTitle(), Str::dlgApiKeyLabel(),
                QLineEdit::Password, QString(), &ok);
            if (ok && !key.trimmed().isEmpty()) {
                m_jarvis->claudeApi()->setApiKey(key.trimmed());
                appendLog(Str::logSystem(), Str::apiKeySaved(), Theme::LogColors::system);
            }
        }, nullptr
    });

    // Ключ ElevenLabs вводится так же, как ключ Anthropic, и так же
    // ложится отдельным файлом в AppData. Пустая строка — стереть ключ
    // и вернуться на офлайн-голос.
    addOwnedAction({
        QStringLiteral("models.elevenLabsKey"),
        IS_EN ? QStringLiteral("ElevenLabs voice key") : QStringLiteral("Ключ голоса ElevenLabs"),
        QStringLiteral("🔊"), QString(), QStringLiteral("models"),
        IS_EN ? QStringLiteral("Set the ElevenLabs API key (empty clears it)")
              : QStringLiteral("Задать ключ ElevenLabs (пусто — стереть)"),
        [this]() {
            bool ok = false;
            const QString key = QInputDialog::getText(this,
                IS_EN ? QStringLiteral("ElevenLabs") : QStringLiteral("ElevenLabs"),
                IS_EN ? QStringLiteral("API key:") : QStringLiteral("API-ключ:"),
                QLineEdit::Password, QString(), &ok);
            if (!ok)
                return;

            ElevenLabsProvider::setApiKey(key);

            const bool has = ElevenLabsProvider::hasApiKey();
            appendLog(Str::logSystem(),
                has ? (IS_EN ? QStringLiteral("🔊 ElevenLabs key saved — voice: %1")
                                   .arg(VoiceSynthesisManager::instance().activeProviderName())
                             : QStringLiteral("🔊 Ключ ElevenLabs сохранён — голос: %1")
                                   .arg(VoiceSynthesisManager::instance().activeProviderName()))
                    : (IS_EN ? QStringLiteral("🔊 ElevenLabs key cleared — offline voice")
                             : QStringLiteral("🔊 Ключ ElevenLabs стёрт — офлайн-голос")),
                Theme::LogColors::system);
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("models.skills"),
        IS_EN ? QStringLiteral("JARVIS Skills") : QStringLiteral("Скиллы JARVIS"),
        QStringLiteral("🧩"), QString(), QStringLiteral("models"),
        IS_EN ? QStringLiteral("Modular knowledge blocks")
              : QStringLiteral("Модульные блоки знаний"),
        [this]() {
            SkillsDialog dlg(m_jarvis->skillManager(), this);
            dlg.exec();
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("models.modes"),
        IS_EN ? QStringLiteral("Work Modes") : QStringLiteral("Режимы работы"),
        QStringLiteral("🎛"), QString(), QStringLiteral("models"),
        QString(),
        [this]() {
            ModesDialog dlg(m_jarvis, IS_EN, this);
            dlg.exec();
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("models.voice"),
        IS_EN ? QStringLiteral("Voice Models") : QStringLiteral("Голосовые модели"),
        QStringLiteral("🎤"), QString(), QStringLiteral("models"),
        IS_EN ? QStringLiteral("Download and manage Vosk recognition models")
              : QStringLiteral("Скачать и настроить модели распознавания Vosk"),
        [this]() { showVoiceModelsDialog(); }, nullptr
    });

    addOwnedAction({
        QStringLiteral("models.ollama"),
        IS_EN ? QStringLiteral("Ollama model") : QStringLiteral("Модель Ollama"),
        QStringLiteral("🦙"), QString(), QStringLiteral("models"),
        QString(),
        [this]() {
            bool ok = false;
            const QString model = QInputDialog::getText(this,
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
                QLineEdit::Normal, m_jarvis->ollamaApi()->model(), &ok);
            if (ok && !model.trimmed().isEmpty()) {
                m_jarvis->ollamaApi()->setModel(model.trimmed());
                appendLog(Str::logSystem(),
                          (IS_EN ? QStringLiteral("Ollama model set: ")
                                 : QStringLiteral("Модель Ollama: ")) + model.trimmed(),
                          Theme::LogColors::system);
            }
        }, nullptr
    });

    {
        // Мультиагентный режим. Состояние спрашивается у ядра, поэтому
        // откатывать галочку при недоступной Ollama больше не нужно:
        // режим просто не включится, и пункт это покажет сам.
        AppAction agent;
        agent.id        = QStringLiteral("models.agentMode");
        agent.title     = Str::menuAgentMode();
        agent.icon      = QStringLiteral("🤝");
        agent.group     = QStringLiteral("models");
        agent.hint      = IS_EN ? QStringLiteral("Code → Claude, chat → local Ollama")
                                : QStringLiteral("Код → Claude, беседа → локальная Ollama");
        agent.checkable = true;
        agent.checked   = [this]() { return m_jarvis->multiAgentMode(); };
        agent.run       = [this]() {
            if (m_jarvis->multiAgentMode()) {
                m_jarvis->setMultiAgentMode(false);
                m_chatCtl->setAgentName(QString());
                appendLog(Str::logJarvis(), Str::agentModeOff(), Theme::LogColors::system);
                return;
            }

            appendLog(Str::logSystem(),
                      IS_EN ? QStringLiteral("Checking Ollama availability...")
                            : QStringLiteral("Проверяю доступность Ollama..."),
                      Theme::LogColors::system);

            m_jarvis->ollamaApi()->checkAvailability(
                [this](bool available, const QString& info) {
                    if (!available) {
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
                        return;
                    }
                    m_jarvis->setMultiAgentMode(true);
                    m_chatCtl->setAgentName(QStringLiteral("🦙 Ollama"));
                    appendLog(Str::logJarvis(),
                              (IS_EN ? QStringLiteral("Agent mode ON. Code → Claude, Chat → Ollama (")
                                     : QStringLiteral("Агент мод ВКЛ. Код → Claude, Беседа → Ollama ("))
                                  + m_jarvis->ollamaApi()->model()
                                  + QStringLiteral(")\n") + info,
                              Theme::LogColors::system);
                });
        };
        addOwnedAction(agent);
    }

    // ---------------------------------------------------------
    //  Задачи
    // ---------------------------------------------------------
    addOwnedAction({
        QStringLiteral("tasks.board"),
        IS_EN ? QStringLiteral("Task board") : QStringLiteral("Доска задач"),
        QStringLiteral("📋"), QString(), QStringLiteral("tasks"),
        QString(),
        [this]() {
            TaskManagerDialog dlg(m_jarvis->currentUserId(), this);
            connect(&dlg, &TaskManagerDialog::taskChanged, this, [this]() {
                appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                    IS_EN ? QStringLiteral("Task board updated.")
                          : QStringLiteral("Доска задач обновлена."),
                    Theme::LogColors::system);
            });
            dlg.exec();
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("tasks.chatHistory"),
        IS_EN ? QStringLiteral("Chat history") : QStringLiteral("История чатов"),
        QStringLiteral("💬"), QString(), QStringLiteral("tasks"),
        QString(),
        [this]() {
            ChatHistoryDialog dlg(m_jarvis->currentUserId(), IS_EN, this);
            dlg.exec();
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("tasks.organize"),
        IS_EN ? QStringLiteral("Organize folder") : QStringLiteral("Организовать папку"),
        QStringLiteral("🗂"), QString(), QStringLiteral("tasks"),
        IS_EN ? QStringLiteral("Sort a folder into categories, with a plan to approve")
              : QStringLiteral("Разложить папку по категориям, с планом на утверждение"),
        [this]() {
            const QString startDir =
                QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
            const QString folder = QFileDialog::getExistingDirectory(this,
                IS_EN ? QStringLiteral("Choose a folder to organize")
                      : QStringLiteral("Выберите папку для организации"),
                startDir);
            if (folder.isEmpty())
                return;

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
                IS_EN ? QStringLiteral("🔍 Scanning folder — this may take a moment for "
                                       "ambiguous files...")
                      : QStringLiteral("🔍 Сканирую папку — для неоднозначных файлов это "
                                       "может занять время..."),
                Theme::LogColors::system);

            FileOrganizer::instance().setLlmApi(m_jarvis->claudeApi());
            FileOrganizer::instance().buildPlan(folder, [this](const OrganizePlan& plan) {
                showOrganizePlanDialog(plan);
            });
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("tasks.organizeRules"),
        IS_EN ? QStringLiteral("Configure categories") : QStringLiteral("Настроить категории"),
        QStringLiteral("⚙"), QString(), QStringLiteral("tasks"),
        QString(),
        [this]() {
            OrganizePlanDialog dlg(m_jarvis, OrganizePlan{}, this, /*initialTab=*/1);
            dlg.exec();
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("tasks.organizeUndo"),
        IS_EN ? QStringLiteral("Undo last organize")
              : QStringLiteral("Отменить последнюю организацию"),
        QStringLiteral("↩"), QString(), QStringLiteral("tasks"),
        QString(),
        [this]() {
            const bool ok = m_jarvis->organizeUndoLast();
            appendLog(Str::logJarvis(),
                ok ? (IS_EN ? QStringLiteral("↩ Last organize batch undone.")
                            : QStringLiteral("↩ Последняя организация отменена."))
                   : (IS_EN ? QStringLiteral("Nothing to undo.")
                            : QStringLiteral("Нечего отменять.")),
                Theme::LogColors::system);
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("tasks.quickAdd"),
        IS_EN ? QStringLiteral("Quick add task") : QStringLiteral("Быстро добавить задачу"),
        QStringLiteral("＋"), QString(), QStringLiteral("tasks"),
        QString(),
        [this]() {
            bool ok = false;
            const QString title = QInputDialog::getText(this,
                IS_EN ? QStringLiteral("New Task") : QStringLiteral("Новая задача"),
                IS_EN ? QStringLiteral("Task title:") : QStringLiteral("Название задачи:"),
                QLineEdit::Normal, QString(), &ok);
            if (!ok || title.trimmed().isEmpty())
                return;

            if (m_jarvis->addTask(title.trimmed()) > 0) {
                appendLog(IS_EN ? QStringLiteral("System") : QStringLiteral("Система"),
                    (IS_EN ? QStringLiteral("Task created: ")
                           : QStringLiteral("Задача создана: ")) + title.trimmed(),
                    Theme::LogColors::system);
                NotificationManager::instance().showNotification(
                    IS_EN ? QStringLiteral("Task created") : QStringLiteral("Задача создана"),
                    title.trimmed(), NotificationManager::Level::Success);
            }
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("tasks.deadlines"),
        IS_EN ? QStringLiteral("Check deadlines") : QStringLiteral("Проверить дедлайны"),
        QStringLiteral("⏰"), QString(), QStringLiteral("tasks"),
        QString(),
        [this]() {
            const QString warnings = m_jarvis->getOverdueTasksSummary();
            if (warnings.isEmpty()) {
                appendLog(IS_EN ? QStringLiteral("J.A.R.V.I.S.")
                                : QStringLiteral("Д.Ж.А.Р.В.И.С."),
                    IS_EN ? QStringLiteral("All clear, sir. No approaching deadlines.")
                          : QStringLiteral("Всё чисто, сэр. Дедлайнов в ближайшее время нет."),
                    Theme::LogColors::jarvis);
                return;
            }
            notifyDeadlineWarnings(warnings);
            appendLog(IS_EN ? QStringLiteral("J.A.R.V.I.S.") : QStringLiteral("Д.Ж.А.Р.В.И.С."),
                      warnings, Theme::LogColors::error);
        }, nullptr
    });

    // ---------------------------------------------------------
    //  Обучение
    // ---------------------------------------------------------
    addOwnedAction({
        QStringLiteral("training.center"),
        IS_EN ? QStringLiteral("Training Center") : QStringLiteral("Центр обучения"),
        QStringLiteral("🧠"), QString(), QStringLiteral("training"),
        IS_EN ? QStringLiteral("Dataset, local training, app usage, synapse graph")
              : QStringLiteral("Датасет, локальное обучение, статистика, граф синапсов"),
        [this]() {
            TrainingCenterDialog dlg(m_jarvis->currentUserId(), m_passiveListener,
                                     m_appLearner, this, 0);
            dlg.exec();
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("training.vision"),
        IS_EN ? QStringLiteral("Vision Center") : QStringLiteral("Центр зрения"),
        QStringLiteral("👁"), QString(), QStringLiteral("training"),
        IS_EN ? QStringLiteral("Who JARVIS recognises and what it sees")
              : QStringLiteral("Кого JARVIS узнаёт и что видит"),
        [this]() { openVisionCenter(0); }, nullptr
    });

    addOwnedAction({
        QStringLiteral("training.export"),
        IS_EN ? QStringLiteral("Export .jsonl for fine-tuning")
              : QStringLiteral("Экспорт .jsonl для обучения"),
        QStringLiteral("📤"), QString(), QStringLiteral("training"),
        QString(), [this]() { onExportTrainingData(); }, nullptr
    });

    addOwnedAction({
        QStringLiteral("training.screenshot"),
        IS_EN ? QStringLiteral("Screenshot + AI description")
              : QStringLiteral("Скриншот + описание AI"),
        QStringLiteral("📸"), QString(), QStringLiteral("training"),
        IS_EN ? QStringLiteral("Describe the screen and save the pair to the dataset")
              : QStringLiteral("Описать экран и сохранить пару в датасет"),
        [this]() {
            const QString apiKey = m_jarvis->claudeApi()->apiKey();
            if (apiKey.isEmpty()) {
                appendLog(Str::logSystem(),
                    IS_EN ? QStringLiteral("📸 Need a Claude API key for screenshot analysis")
                          : QStringLiteral("📸 Нужен ключ Claude API для анализа скриншота"),
                    Theme::LogColors::error);
                return;
            }

            appendLog(Str::logSystem(),
                IS_EN ? QStringLiteral("📸 Taking screenshot and analyzing...")
                      : QStringLiteral("📸 Делаю скриншот и анализирую..."),
                Theme::LogColors::system);

            m_screenAgent->describeScreen(apiKey, [this](const QString& desc) {
                if (desc.isEmpty())
                    return;
                appendLog(Str::logJarvis(),
                    (IS_EN ? QStringLiteral("📸 Screen: ") : QStringLiteral("📸 Экран: ")) + desc,
                    Theme::LogColors::jarvis);
                if (m_passiveListener) {
                    m_passiveListener->addVoiceCommandPair(
                        QStringLiteral("[screenshot] what do you see on the screen?"),
                        desc, QStringLiteral("en"));
                }
            });
        },
        [this]() { return m_screenAgent != nullptr; }
    });

    {
        // Переключатель: состояние спрашивается у таймера, а не хранится
        // в тексте пункта. Раньше пункт сам себе переписывал заголовок на
        // «ВКЛ/ВЫКЛ» — и это было единственным местом, где состояние жило.
        AppAction autoShot;
        autoShot.id       = QStringLiteral("training.autoScreenshot");
        autoShot.title    = IS_EN ? QStringLiteral("Auto-screenshot every 5 min")
                                  : QStringLiteral("Авто-скриншот каждые 5 мин");
        autoShot.icon     = QStringLiteral("⏱");
        autoShot.group    = QStringLiteral("training");
        autoShot.checkable = true;
        autoShot.checked  = [this]() { return m_screenshotTimer && m_screenshotTimer->isActive(); };
        autoShot.enabled  = [this]() { return m_screenAgent != nullptr; };
        autoShot.run      = [this]() {
            if (m_screenshotTimer && m_screenshotTimer->isActive()) {
                m_screenshotTimer->stop();
                return;
            }
            if (!m_screenshotTimer) {
                m_screenshotTimer = new QTimer(this);
                m_screenshotTimer->setInterval(5 * 60 * 1000);
                connect(m_screenshotTimer, &QTimer::timeout, this, [this]() {
                    if (!m_screenAgent)
                        return;
                    const QString apiKey = m_jarvis->claudeApi()->apiKey();
                    if (apiKey.isEmpty())
                        return;
                    m_screenAgent->describeScreen(apiKey, [this](const QString& desc) {
                        if (desc.isEmpty() || !m_passiveListener)
                            return;
                        m_passiveListener->addVoiceCommandPair(
                            QStringLiteral("[auto-screenshot] describe current screen context"),
                            desc, QStringLiteral("en"));
                        qDebug() << "[Training] Auto-screenshot saved to dataset";
                    });
                });
            }
            m_screenshotTimer->start();
        };
        addOwnedAction(autoShot);
    }

    {
        AppAction passive;
        passive.id        = QStringLiteral("training.passive");
        passive.title     = IS_EN ? QStringLiteral("Passive voice recording")
                                  : QStringLiteral("Пассивная запись голоса");
        passive.icon      = QStringLiteral("🎙");
        passive.group     = QStringLiteral("training");
        passive.hint      = IS_EN ? QStringLiteral("Every phrase heard goes into the dataset")
                                  : QStringLiteral("Каждая услышанная фраза идёт в датасет");
        passive.checkable = true;
        passive.checked   = [this]() { return m_passiveListener && m_passiveListener->isListening(); };
        passive.enabled   = [this]() { return m_passiveListener != nullptr; };
        passive.run       = [this]() {
            if (!m_passiveListener)
                return;
            if (m_passiveListener->isListening())
                m_passiveListener->stopListening();
            else
                m_passiveListener->startListening();
        };
        addOwnedAction(passive);
    }

    addOwnedAction({
        QStringLiteral("training.datasetPath"),
        IS_EN ? QStringLiteral("Dataset folder") : QStringLiteral("Папка датасета"),
        QStringLiteral("📁"), QString(), QStringLiteral("training"),
        QString(),
        [this]() {
            const QString current = DatabaseManager::instance().getConfig(
                QStringLiteral("voice_dataset_path"),
                JarvisPaths::subPath(QStringLiteral("voice_dataset"))).toString();

            const QString path = QFileDialog::getExistingDirectory(this,
                IS_EN ? QStringLiteral("Select dataset folder")
                      : QStringLiteral("Выберите папку для датасета"),
                current);
            if (path.isEmpty())
                return;

            DatabaseManager::instance().setConfig(
                QStringLiteral("voice_dataset_path"), path);
            if (m_passiveListener) {
                auto cfg = m_passiveListener->config();
                cfg.datasetPath = path;
                m_passiveListener->setConfig(cfg);
            }
        }, nullptr
    });

    // ---------------------------------------------------------
    //  Помощь
    // ---------------------------------------------------------
    addOwnedAction({
        QStringLiteral("help.components"),
        IS_EN ? QStringLiteral("Component Manager") : QStringLiteral("Менеджер компонентов"),
        QStringLiteral("📦"), QString(), QStringLiteral("help"),
        IS_EN ? QStringLiteral("Optional components: Vosk, Poppler, Tesseract")
              : QStringLiteral("Дополнительные компоненты: Vosk, Poppler, Tesseract"),
        [this]() { (new DependencyManagerDialog(this))->show(); }, nullptr
    });

    addOwnedAction({
        QStringLiteral("help.about"), Str::menuAbout(),
        QStringLiteral("ℹ"), QString(), QStringLiteral("help"),
        IS_EN ? QStringLiteral("Version and credits") : QStringLiteral("Версия и авторы"),
        [this]() {
            QMessageBox::about(this, QStringLiteral("J.A.R.V.I.S."),
                Str::aboutText().arg(QCoreApplication::applicationVersion()));
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("help.eula"),
        IS_EN ? QStringLiteral("License Agreement (EULA)")
              : QStringLiteral("Лицензионное соглашение (EULA)"),
        QStringLiteral("📜"), QString(), QStringLiteral("help"),
        QString(), [this]() { showEulaDialog(); }, nullptr
    });

    addOwnedAction({
        QStringLiteral("help.privacy"),
        IS_EN ? QStringLiteral("Privacy Policy") : QStringLiteral("Политика конфиденциальности"),
        QStringLiteral("🔒"), QString(), QStringLiteral("help"),
        QString(), [this]() { showPrivacyDialog(); }, nullptr
    });

    // ---------------------------------------------------------
    //  Проект
    // ---------------------------------------------------------
    addOwnedAction({
        QStringLiteral("project.index"), Str::menuIndexFolder(),
        QStringLiteral("📁"), QString(), QStringLiteral("project"),
        IS_EN ? QStringLiteral("Pick a folder and index it for code questions")
              : QStringLiteral("Выбрать папку и проиндексировать для вопросов по коду"),
        [this]() {
            QString startDir = m_jarvis->projectIndexer()->projectRoot();
            if (startDir.isEmpty())
                startDir = QDir::homePath();

            const QString dir = QFileDialog::getExistingDirectory(this,
                Str::dlgChooseFolder(), startDir, QFileDialog::ShowDirsOnly);
            if (dir.isEmpty())
                return;

            appendLog(Str::logSystem(), Str::statusIndexing() + dir + QStringLiteral("..."),
                      Theme::LogColors::system);

            m_jarvis->projectIndexer()->setProjectRoot(dir);
            m_jarvis->projectIndexer()->indexProject();
            m_jarvis->projectIndexer()->enableFileWatcher(true);
            m_jarvis->syncProjectInfoToMemory();

            appendLog(Str::logJarvis(),
                      Str::projIndexed()
                          + QString::number(m_jarvis->projectIndexer()->fileCount())
                          + Str::projSymbols()
                          + QString::number(m_jarvis->projectIndexer()->symbolCount()),
                      Theme::LogColors::jarvis);
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("project.reindex"), Str::menuReindex(),
        QStringLiteral("↻"), QString(), QStringLiteral("project"),
        IS_EN ? QStringLiteral("Re-scan the current project")
              : QStringLiteral("Пересканировать текущий проект"),
        [this]() {
            m_jarvis->projectIndexer()->indexProject();
            m_jarvis->syncProjectInfoToMemory();
            appendLog(Str::logSystem(),
                      Str::projReindexed()
                          + QString::number(m_jarvis->projectIndexer()->fileCount())
                          + Str::projFilesCount(),
                      Theme::LogColors::system);
        },
        // Раньше пункт был всегда доступен и на непроиндексированном
        // проекте отвечал строкой в лог. Недоступность честнее.
        [this]() { return !m_jarvis->projectIndexer()->projectRoot().isEmpty(); }
    });

    addOwnedAction({
        QStringLiteral("project.info"), Str::menuProjectInfo(),
        QStringLiteral("ℹ"), QString(), QStringLiteral("project"),
        IS_EN ? QStringLiteral("Root, file and symbol counts, classes")
              : QStringLiteral("Корень, число файлов и символов, классы"),
        [this]() {
            auto* idx = m_jarvis->projectIndexer();
            QString info = Str::projInfoLabel() + idx->projectRoot()
                         + Str::projFilesLabel() + QString::number(idx->fileCount())
                         + Str::projSymbolsLabel() + QString::number(idx->symbolCount())
                         + Str::projClassesLabel();
            for (const auto& cls : idx->allClasses())
                info += QStringLiteral("  • ") + cls + QStringLiteral("\n");
            appendLog(Str::logJarvis(), info.trimmed(), Theme::LogColors::jarvis);
        },
        [this]() { return m_jarvis->projectIndexer()->fileCount() > 0; }
    });

    // ---------------------------------------------------------
    //  Панели
    // ---------------------------------------------------------
    // Ленивое создание диалогов вынесено в один помощник: иначе каждая
    // команда повторяла бы «если нет — создай, показать, поднять».
    auto openDialog = [this](QDialog*& slot, std::function<QDialog*()> make) {
        if (!slot)
            slot = make();
        slot->show();
        slot->raise();
        slot->activateWindow();
    };

    addOwnedAction({
        QStringLiteral("view.dashboard"),
        IS_EN ? QStringLiteral("Dashboard") : QStringLiteral("Дашборд"),
        QStringLiteral("▦"), QStringLiteral("Ctrl+Shift+D"), QStringLiteral("view"),
        IS_EN ? QStringLiteral("Customizable board of widgets")
              : QStringLiteral("Настраиваемая доска виджетов"),
        [this, openDialog]() {
            openDialog(m_dashboardDialog,
                       [this]() -> QDialog* { return new DashboardDialog(m_jarvis, IS_EN, this); });
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("view.monitor"),
        IS_EN ? QStringLiteral("System monitor") : QStringLiteral("Состояние системы"),
        QStringLiteral("📊"), QStringLiteral("Ctrl+Shift+M"), QStringLiteral("view"),
        IS_EN ? QStringLiteral("CPU, memory, network, processes")
              : QStringLiteral("Процессор, память, сеть, процессы"),
        [this, openDialog]() {
            openDialog(m_monitorDialog,
                       [this]() -> QDialog* {
                           return new SystemMonitorDialog(m_jarvis->systemMonitor(), IS_EN, this);
                       });
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("view.devices"),
        IS_EN ? QStringLiteral("Devices") : QStringLiteral("Устройства"),
        QStringLiteral("🛰"), QString(), QStringLiteral("view"),
        IS_EN ? QStringLiteral("This PC, ESP32, mesh peers, Bluetooth")
              : QStringLiteral("Этот ПК, ESP32, соседи по mesh, Bluetooth"),
        [this, openDialog]() {
            openDialog(m_devicesDialog,
                       [this]() -> QDialog* { return new DeviceHubDialog(m_jarvis, IS_EN, this); });
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("view.notifications"),
        IS_EN ? QStringLiteral("Notifications") : QStringLiteral("Уведомления"),
        QStringLiteral("🔔"), QString(), QStringLiteral("view"),
        IS_EN ? QStringLiteral("Everything that happened while you looked away")
              : QStringLiteral("Всё, что случилось, пока ты смотрел в другое окно"),
        [this, openDialog]() {
            openDialog(m_notificationsDialog,
                       [this]() -> QDialog* { return new NotificationsDialog(IS_EN, this); });
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("view.modes"),
        IS_EN ? QStringLiteral("Configure modes") : QStringLiteral("Настроить режимы"),
        QStringLiteral("👤"), QString(), QStringLiteral("view"),
        IS_EN ? QStringLiteral("Skills, tone, permissions and volume per profile")
              : QStringLiteral("Скиллы, тон, разрешения и громкость профиля"),
        [this]() {
            ModesDialog dlg(m_jarvis, IS_EN, this);
            dlg.exec();
        }, nullptr
    });

    addOwnedAction({
        QStringLiteral("view.artifacts"),
        IS_EN ? QStringLiteral("Files Jarvis made") : QStringLiteral("Файлы от Джарвиса"),
        QStringLiteral("🗂"), QString(), QStringLiteral("view"),
        IS_EN ? QStringLiteral("Everything JARVIS created or edited")
              : QStringLiteral("Всё, что JARVIS создал или изменил"),
        [this]() {
            ArtifactsDialog dlg(this);
            dlg.exec();
        }, nullptr
    });

    // Счётчик непрочитанного живёт в заголовке команды: центр
    // уведомлений, о котором надо помнить, что он есть, бесполезен.
    // Перерегистрация по тому же id заменяет запись и обновляет модель.
    connect(&EventFeed::instance(), &EventFeed::changed, this, [this]() {
        const AppAction* existing = m_actions->find(QStringLiteral("view.notifications"));
        if (!existing)
            return;

        AppAction updated = *existing;
        const int unread = EventFeed::instance().unread();
        updated.title = unread > 0
            ? (IS_EN ? QStringLiteral("Notifications (%1)")
                     : QStringLiteral("Уведомления (%1)")).arg(unread)
            : (IS_EN ? QStringLiteral("Notifications") : QStringLiteral("Уведомления"));
        addOwnedAction(updated);
    });

    m_actions->installShortcuts(this);

    // Провайдер команд для Ctrl+K и запуск найденного живут в main(): они
    // работают с реестром, а не с окном, и должны пережить его закрытие.
}

// ============================================================
//  Палитра команд: Ctrl+Space из любого места системы
// ============================================================

// Палитра, её горячие клавиши и сам реестр команд собираются в main():
// они переживают это окно. Здесь осталось только наполнение реестра
// (registerAppActions) — команды окна умеют то, что умеет окно.

void MainWindow::addOwnedAction(const AppAction& action)
{
    m_actions->add(action, this);
}

// ============================================================
//  Слой действий: шаги агента и подтверждения
// ============================================================

void MainWindow::setupAgentUi()
{
    AgentLoop*      agent = m_jarvis->agent();
    PermissionGate* gate  = m_jarvis->permissions();
    if (!agent || !gate)
        return;

    // Десктопное окно — единственное место, где есть кому подтверждать.
    // Без этого флага гейт отклоняет всё, что рискованнее чтения.
    gate->setInteractive(true);

    connect(agent, &AgentLoop::narration, this, [this](const QString& text) {
        appendLog(Str::logJarvis(), text, Theme::LogColors::jarvis);
    });

    connect(agent, &AgentLoop::toolStarted, this,
            [this](const QString&, const QString& summary) {
        appendLog(Str::logJarvis(), QStringLiteral("⚙ ") + summary,
                  Theme::LogColors::system);
    });

    connect(agent, &AgentLoop::toolFinished, this,
            [this](const QString&, bool ok, const QString& summary) {
        appendLog(Str::logJarvis(),
                  (ok ? QStringLiteral("✓ ") : QStringLiteral("✕ ")) + summary,
                  ok ? Theme::LogColors::system : Theme::LogColors::error);
    });

    connect(agent, &AgentLoop::toolDenied, this,
            [this](const QString&, const QString& reason) {
        appendLog(Str::logJarvis(), QStringLiteral("⛔ ") + reason,
                  Theme::LogColors::error);
    });

    // Сценарии печатают свои шаги тем же способом — из меню их запускают
    // без всякого агента, и без этого прогон был бы молчаливым.
    if (WorkflowManager* wm = m_jarvis->workflows()) {
        connect(wm, &WorkflowManager::workflowStarted, this,
                [this](const QString& name, int stepCount) {
            appendLog(Str::logJarvis(),
                      QStringLiteral("▶ %1 — %2").arg(name).arg(
                          IS_EN ? QStringLiteral("%1 steps").arg(stepCount)
                                : QStringLiteral("шагов: %1").arg(stepCount)),
                      Theme::LogColors::jarvis);
        });

        connect(wm, &WorkflowManager::stepFinished, this,
                [this](const QString&, int index, bool ok, const QString& summary) {
            appendLog(Str::logJarvis(),
                      QStringLiteral("  %1 %2. %3")
                          .arg(ok ? QStringLiteral("✓") : QStringLiteral("✕"))
                          .arg(index + 1)
                          .arg(summary),
                      ok ? Theme::LogColors::system : Theme::LogColors::error);
        });
    }

    connect(gate, &PermissionGate::confirmationRequired, this,
            [this, gate](quint64 id, const QString& toolName,
                         const QString& summary, int risk) {

        const bool dangerous = (risk >= static_cast<int>(ToolRisk::Dangerous));

        QMessageBox box(this);
        box.setWindowTitle(IS_EN ? QStringLiteral("Confirm action")
                                 : QStringLiteral("Подтверждение действия"));
        box.setIcon(dangerous ? QMessageBox::Warning : QMessageBox::Question);
        box.setText(dangerous
                        ? (IS_EN ? QStringLiteral("This action may be irreversible.")
                                 : QStringLiteral("Это действие может быть необратимым."))
                        : (IS_EN ? QStringLiteral("JARVIS wants to do this:")
                                 : QStringLiteral("JARVIS хочет выполнить:")));
        box.setInformativeText(summary + QStringLiteral("\n\n[") + toolName + QStringLiteral("]"));

        QAbstractButton* allow = box.addButton(
            IS_EN ? QStringLiteral("Allow") : QStringLiteral("Разрешить"),
            QMessageBox::AcceptRole);

        // «Разрешать всегда» намеренно недоступно для Dangerous:
        // удаление и выключение спрашиваются каждый раз.
        QAbstractButton* always = nullptr;
        if (!dangerous) {
            always = box.addButton(
                IS_EN ? QStringLiteral("Allow for this session")
                      : QStringLiteral("Разрешать до перезапуска"),
                QMessageBox::YesRole);
        }
        QAbstractButton* deny = box.addButton(
            IS_EN ? QStringLiteral("Deny") : QStringLiteral("Отклонить"),
            QMessageBox::RejectRole);
        box.setDefaultButton(qobject_cast<QPushButton*>(deny));
        box.exec();

        const bool allowed  = (box.clickedButton() == allow)
                              || (always && box.clickedButton() == always);
        const bool remember = always && (box.clickedButton() == always);

        // Ответ отдаём следующим тиком: resolve() синхронно запускает
        // инструмент, а тот может сразу попросить следующее подтверждение —
        // разворачиваем стек, чтобы диалоги не вкладывались друг в друга.
        QTimer::singleShot(0, this, [gate, id, allowed, remember]() {
            gate->resolve(id, allowed, remember);
        });
    });
}

void MainWindow::appendLog(const QString& who, const QString& text, const QString& color)
{
    // Единственная точка входа ленты — как и была до порта, поэтому
    // все 142 вызова остались нетронутыми. Изменилось только то, что
    // сообщение уходит в модель, а не склеивается в HTML-строку.
    //
    // Цвет здесь читается обратно в роль (kindFromLogColor) и дальше
    // не используется: цвет роли выдаёт тема.
    if (m_chat)
        m_chat->append(kindFromLogColor(color), who, text);
}

// ============================================================
// showWelcomeDashboard — dynamic signal-driven greeting panel
// ============================================================

void MainWindow::showWelcomeDashboard()
{
    if (!m_welcomeCtl) return;

    // Экран сам решает, когда себя показывать: он висит в ленте под
    // условием «сообщений нет», и сам же включает опрос состояния,
    // пока виден.
    //
    // Сигналы ниже — не замена опросу, а дополнение к нему: они
    // приходят в момент события, а опрос ловит то, о чём никто не
    // сообщает (вынули накопитель, отвалилась база).
    m_welcomeCtl->refresh();

    connect(&MemoryConsolidation::instance(),
            &MemoryConsolidation::driveStatusChanged,
            this, [this](bool) { m_welcomeCtl->refresh(); });

    connect(&UserProfileExtended::instance(),
            &UserProfileExtended::profileChanged,
            this, [this](const QString&) { m_welcomeCtl->refresh(); });

    if (auto* indexer = m_jarvis->projectIndexer()) {
        connect(indexer, &ProjectIndexer::indexingFinished,
                this, [this](int, int) { m_welcomeCtl->refresh(); });
    }
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
        m_chatCtl->setMicGlyph(QStringLiteral("🔴"));
        m_chatCtl->setListening(true);
        m_chatCtl->setMicTooltip(IS_EN ? QStringLiteral("Listening... (click to stop)")
                                   : QStringLiteral("Слушаю… (нажми чтобы остановить)"));
        m_chatCtl->setStatus(IS_EN ? QStringLiteral("🎤 Listening…")
                                   : QStringLiteral("🎤 Слушаю…"),
                             QStringLiteral("speaking"));
        appendLog(Str::logJarvis(),
                  IS_EN ? QStringLiteral("🎤 Voice input started. Say 'Jarvis' to activate.")
                        : QStringLiteral("🎤 Голосовой ввод запущен. Скажите «Джарвис» для активации."),
                  Theme::LogColors::system);
    } else {
        // Останавливаем
        m_voiceInput->stopListening();
        m_voiceActive = false;
        m_chatCtl->setMicGlyph(QStringLiteral("🎤"));
        m_chatCtl->setListening(false);
        m_chatCtl->setMicTooltip(IS_EN ? QStringLiteral("Voice input (Vosk)")
                                   : QStringLiteral("Голосовой ввод (Vosk)"));
        m_chatCtl->setStatus(IS_EN ? QStringLiteral("Ready") : QStringLiteral("Готов"),
                             QStringLiteral("online"));
    }
}

void MainWindow::onVoiceReady()
{
    m_chatCtl->setMicEnabled(true);
    m_chatCtl->setMicTooltip(IS_EN ? QStringLiteral("Voice input (Vosk)")
                               : QStringLiteral("Голосовой ввод (Vosk)"));
    appendLog(Str::logSystem(),
              IS_EN ? QStringLiteral("🎤 Vosk models loaded. Voice input ready.")
                    : QStringLiteral("🎤 Модели Vosk загружены. Голосовой ввод готов."),
              Theme::LogColors::system);
}

void MainWindow::onVoiceText(const QString& text, const QString& lang)
{
    // Сбрасываем цвет кнопки — запись закончена, идёт распознавание
    m_chatCtl->setMicSpeaking(false);
    if (text.isEmpty()) return;

    // Показываем распознанный текст в поле ввода и отправляем
    const QString prefix = (lang == QStringLiteral("ru"))
        ? QStringLiteral("🎤 ")
        : QStringLiteral("🎤 ");

    appendLog(QStringLiteral("🎤 Voice"),
              QStringLiteral("[%1] %2").arg(lang.toUpper(), text),
              QStringLiteral("#4a9a6a"));

    m_chatCtl->setDraft(text);

    // Помечаем что ввод голосовой — onAsyncResponse сохранит пару в voice_journal
    m_lastInputWasVoice = true;
    m_lastVoiceLanguage = lang;

    // Незнакомый язык распознаёт и запоминает ядро — до того, как речь
    // дойдёт сюда (см. Jarvis::submitVoiceCommand).

    // Автоматически отправляем голосовую команду
    onSend();
}

void MainWindow::onWakeWord(const QString& word)
{
    appendLog(Str::logJarvis(),
              QStringLiteral("👂 ") + (IS_EN ? QStringLiteral("Wake word: ") : QStringLiteral("Активация: ")) + word,
              Theme::LogColors::system);
    m_chatCtl->setStatusText(IS_EN ? QStringLiteral("🎤 Speak now...")
                            : QStringLiteral("🎤 Говорите..."));
}

void MainWindow::onWhisperMode(bool isWhisper)
{
    if (isWhisper) {
        m_chatCtl->setMicTooltip(IS_EN ? QStringLiteral("🤫 Quiet voice detected")
                                   : QStringLiteral("🤫 Обнаружен шёпот"));
        appendLog(Str::logSystem(),
                  IS_EN ? QStringLiteral("🤫 Whisper detected — low volume mode active")
                        : QStringLiteral("🤫 Обнаружен шёпот — режим тихого голоса"),
                  Theme::LogColors::system);
    }
}

// Пользователь заговорил, пока JARVIS говорит сам. Реплика обрывается
// сразу — «Я обнаружил проблему в вашем—», а не дочитывается до конца:
// договорённая до точки фраза после «стоп» мгновенно выдаёт программу.
void MainWindow::maybeBargeIn(float micDb)
{
    VoiceSynthesisManager& tts = VoiceSynthesisManager::instance();

    // Критическую реплику (перегрев, падение процесса) не перебивает
    // никто: она помечена interruptible = false.
    if (!tts.isSpeaking() || !tts.currentIsInterruptible()) {
        m_bargeInFrames = 0;
        return;
    }

    if (tts.currentSpeechElapsedMs() < kBargeInGraceMs || micDb < kBargeInDb) {
        m_bargeInFrames = 0;
        return;
    }

    if (++m_bargeInFrames < kBargeInMinFrames)
        return;

    m_bargeInFrames = 0;
    tts.cancelCurrentSpeech(CancelReason::UserBargeIn);

    appendLog(Str::logSystem(),
              IS_EN ? QStringLiteral("🎤 Interrupted — listening")
                    : QStringLiteral("🎤 Перебил — слушаю"),
              Theme::LogColors::system);
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

// ============================================================
// onSaveInsight — сохранение того, что показано в боковой панели
//
// Диалог сохранения живёт здесь, а не в контроллере: QFileDialog
// нужен родитель-виджет, а панель после переезда в QML его не имеет.
// ============================================================

void MainWindow::onSaveInsight(const QString&)
{
    if (!m_visualCtl) return;

    // Уже настоящий файл на диске — сохранять его заново незачем,
    // просто показываем, где он лежит.
    const QString existing = m_visualCtl->currentFilePath();
    const QString mermaid  = m_visualCtl->currentMermaid();
    const QImage  image    = m_visualCtl->currentImage();
    const QByteArray svgData = m_visualCtl->currentSvgData();

    if (!existing.isEmpty() && mermaid.isEmpty() && image.isNull()) {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QFileInfo(existing).absolutePath()));
        return;
    }

    const QString ts = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd_HHmmss"));
    const QString base = JarvisPaths::subPath(QStringLiteral("visuals"))
        + QStringLiteral("/diagram_") + ts;

    if (!mermaid.isEmpty()) {
        // Сохраняем НАРИСОВАННУЮ диаграмму, а не её исходник. Пункт
        // .mmd в списке типов остаётся — текст в одном клике.
        m_visualCtl->exportRendered(
            [this, base, mermaid](const QByteArray& svg, const QImage& raster) {
                QStringList filters;
                QString defaultSuffix;
                if (!svg.isEmpty()) {
                    filters << QStringLiteral("SVG image (*.svg)");
                    defaultSuffix = QStringLiteral(".svg");
                }
                if (!raster.isNull()) {
                    filters << QStringLiteral("PNG image (*.png)");
                    if (defaultSuffix.isEmpty()) defaultSuffix = QStringLiteral(".png");
                }
                filters << QStringLiteral("Mermaid source (*.mmd)")
                        << QStringLiteral("All (*)");
                if (defaultSuffix.isEmpty()) defaultSuffix = QStringLiteral(".mmd");

                const QString path = QFileDialog::getSaveFileName(
                    this, QStringLiteral("Save Diagram"),
                    base + defaultSuffix, filters.join(QStringLiteral(";;")));
                if (path.isEmpty()) return;

                // Что писать, решает выбранное расширение: выбрав в
                // списке «PNG», нельзя получить SVG с именем .png.
                if (path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)
                    && !raster.isNull()) {
                    if (!raster.save(path, "PNG"))
                        qWarning() << "[VisualInsights] cannot write" << path;
                    return;
                }

                QFile f(path);
                if (!f.open(QIODevice::WriteOnly)) {
                    qWarning() << "[VisualInsights] cannot write" << path
                               << f.errorString();
                    return;
                }
                if (!svg.isEmpty()
                    && !path.endsWith(QStringLiteral(".mmd"), Qt::CaseInsensitive))
                    f.write(svg);
                else
                    f.write(mermaid.toUtf8());
                f.close();
            });
        return;
    }

    if (!image.isNull()) {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save Diagram"),
            base + QStringLiteral(".png"),
            QStringLiteral("PNG (*.png);;All (*)"));
        if (!path.isEmpty())
            image.save(path);
        return;
    }

    if (!svgData.isEmpty()) {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save Diagram"),
            base + QStringLiteral(".svg"),
            QStringLiteral("SVG (*.svg);;All (*)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(svgData);
            f.close();
        }
    }
}
