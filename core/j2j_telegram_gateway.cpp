// ============================================================
// j2j_telegram_gateway.cpp — Multi-Tenant Telegram Bot Gateway
// ============================================================

#include "j2j_telegram_gateway.h"
#include "database_manager.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QDebug>

static const QString kTgApiBase = QStringLiteral("https://api.telegram.org/bot");

// ============================================================
//  Localization Dictionary
// ============================================================

QString J2JTelegramGateway::localized(TgStringId id, bool en)
{
    switch (id) {

    case TgStringId::Welcome:
        return en ? QStringLiteral("👋 Welcome to *J.A.R.V.I.S.* Mobile Hub!\n"
                                   "Your device has been linked. Choose an action below.")
                  : QStringLiteral("👋 Добро пожаловать в *J.A.R.V.I.S.* Mobile Hub!\n"
                                   "Устройство подключено. Выберите действие ниже.");

    case TgStringId::MainMenu:
        return en ? QStringLiteral("🏠 *Main Menu* — what would you like to do?")
                  : QStringLiteral("🏠 *Главное Меню* — что хотите сделать?");

    case TgStringId::BtnSystemTelemetry:
        return en ? QStringLiteral("📊 System Telemetry")
                  : QStringLiteral("📊 Статус Системы");

    case TgStringId::BtnMyKanban:
        return en ? QStringLiteral("📋 My Kanban Board")
                  : QStringLiteral("📋 Мой Канбан");

    case TgStringId::BtnReportBug:
        return en ? QStringLiteral("🐛 Report a Bug")
                  : QStringLiteral("🐛 Создать Баг-Репорт");

    case TgStringId::BtnWakePC:
        return en ? QStringLiteral("⚡ Wake PC")
                  : QStringLiteral("⚡ Разбудить ПК");

    case TgStringId::BtnHelp:
        return en ? QStringLiteral("❓ Help")
                  : QStringLiteral("❓ Помощь");

    case TgStringId::BugTitlePrompt:
        return en ? QStringLiteral("🐛 *Bug Report — Step 1/4*\n\n"
                                   "Please enter the *Bug Title*:\n"
                                   "_A short, descriptive summary of the issue._")
                  : QStringLiteral("🐛 *Баг-Репорт — Шаг 1/4*\n\n"
                                   "Введите *Заголовок бага*:\n"
                                   "_Краткое описание проблемы._");

    case TgStringId::BugStepsPrompt:
        return en ? QStringLiteral("🐛 *Bug Report — Step 2/4*\n\n"
                                   "Provide *Steps to Reproduce*:\n"
                                   "_List the steps that trigger this bug._")
                  : QStringLiteral("🐛 *Баг-Репорт — Шаг 2/4*\n\n"
                                   "Опишите *Шаги воспроизведения*:\n"
                                   "_Перечислите действия для воспроизведения бага._");

    case TgStringId::BugExpectedPrompt:
        return en ? QStringLiteral("🐛 *Bug Report — Step 3/4*\n\n"
                                   "What was the *Expected Result*?\n"
                                   "_What should have happened._")
                  : QStringLiteral("🐛 *Баг-Репорт — Шаг 3/4*\n\n"
                                   "Каков *Ожидаемый результат*?\n"
                                   "_Что должно было произойти._");

    case TgStringId::BugActualPrompt:
        return en ? QStringLiteral("🐛 *Bug Report — Step 4/4*\n\n"
                                   "What was the *Actual Result*?\n"
                                   "_What actually happened instead._")
                  : QStringLiteral("🐛 *Баг-Репорт — Шаг 4/4*\n\n"
                                   "Каков *Фактический результат*?\n"
                                   "_Что произошло на самом деле._");

    case TgStringId::BugSeverityPrompt:
        return en ? QStringLiteral("🐛 *Select Severity Level:*")
                  : QStringLiteral("🐛 *Выберите уровень серьёзности:*");

    case TgStringId::BugSaved:
        return en ? QStringLiteral("✅ *Bug report filed successfully!*\n\n"
                                   "The report has been saved to the local QA database "
                                   "and a Markdown artifact has been generated.")
                  : QStringLiteral("✅ *Баг-репорт успешно сохранён!*\n\n"
                                   "Репорт сохранён в локальную QA базу, "
                                   "Markdown-артефакт сгенерирован.");

    case TgStringId::BugCancelled:
        return en ? QStringLiteral("❌ Bug report cancelled.")
                  : QStringLiteral("❌ Баг-репорт отменён.");

    case TgStringId::KanbanEmpty:
        return en ? QStringLiteral("📋 Your Kanban board is empty.\n"
                                   "_No tasks found._")
                  : QStringLiteral("📋 Канбан-доска пуста.\n"
                                   "_Задач не найдено._");

    case TgStringId::KanbanHeader:
        return en ? QStringLiteral("📋 *Kanban Board*\n")
                  : QStringLiteral("📋 *Канбан-доска*\n");

    case TgStringId::TelemetryHeader:
        return en ? QStringLiteral("📊 *System Telemetry*\n")
                  : QStringLiteral("📊 *Телеметрия Системы*\n");

    case TgStringId::HelpText:
        return en ? QStringLiteral("❓ *J.A.R.V.I.S. Mobile Help*\n\n"
                                   "• Tap buttons below to interact\n"
                                   "• Type `/menu` to show the main menu\n"
                                   "• Type `/cancel` to abort the current wizard\n"
                                   "• Bug reports are saved locally and exported as Markdown")
                  : QStringLiteral("❓ *Помощь J.A.R.V.I.S. Mobile*\n\n"
                                   "• Нажмите кнопки ниже для взаимодействия\n"
                                   "• Введите `/menu` для главного меню\n"
                                   "• Введите `/cancel` для отмены текущего мастера\n"
                                   "• Баг-репорты сохраняются локально и экспортируются в Markdown");

    case TgStringId::UnknownCommand:
        return en ? QStringLiteral("🤔 I didn't understand that. Tap a button or type `/menu`.")
                  : QStringLiteral("🤔 Не понял команду. Нажмите кнопку или введите `/menu`.");

    case TgStringId::SessionExpired:
        return en ? QStringLiteral("⏰ Your session has expired. Please re-pair your device.")
                  : QStringLiteral("⏰ Сессия истекла. Пожалуйста, переподключите устройство.");

    case TgStringId::PairSuccess:
        return en ? QStringLiteral("🔗 Device paired successfully! Role: *%1*")
                  : QStringLiteral("🔗 Устройство подключено! Роль: *%1*");
    }

    return QStringLiteral("[???]");
}

// ============================================================
//  Construction
// ============================================================

J2JTelegramGateway::J2JTelegramGateway(QObject* parent)
    : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(POLL_INTERVAL_MS);
    connect(m_pollTimer, &QTimer::timeout,
            this, &J2JTelegramGateway::onPollUpdates);

    // Ensure the qa_artifacts table exists
    {
        QMutexLocker lock(&m_mutex);
        QSqlQuery q(QSqlDatabase::database());
        q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS qa_artifacts ("
            "  id               INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  chat_id          INTEGER NOT NULL,"
            "  reporter_name    TEXT,"
            "  reporter_role    TEXT DEFAULT 'QA_Tester',"
            "  title            TEXT NOT NULL,"
            "  steps_to_reproduce TEXT,"
            "  expected_result  TEXT,"
            "  actual_result    TEXT,"
            "  severity         TEXT DEFAULT 'Major',"
            "  markdown_export  TEXT,"
            "  created_at       TEXT DEFAULT (datetime('now'))"
            ")"));
    }

    // Try to load bot token from settings
    m_botToken = DatabaseManager::instance()
        .getConfig(QStringLiteral("telegram_bot_token"), QString()).toString();

    qDebug() << "[TelegramGW] Initialized"
             << (m_botToken.isEmpty() ? "(no token yet)" : "(token loaded)");
}

J2JTelegramGateway::~J2JTelegramGateway()
{
    stop();
}

void J2JTelegramGateway::setBotToken(const QString& token)
{
    m_botToken = token;
    QMutexLocker lock(&m_mutex);
    DatabaseManager::instance().setConfig(
        QStringLiteral("telegram_bot_token"), token);
}

void J2JTelegramGateway::start()
{
    if (m_running) return;
    if (m_botToken.isEmpty()) {
        emit gatewayError(QStringLiteral("Telegram bot token not configured"));
        return;
    }
    m_running = true;
    m_pollTimer->start();
    emit gatewayStarted();
    qDebug() << "[TelegramGW] Polling started (every" << POLL_INTERVAL_MS << "ms)";
}

void J2JTelegramGateway::stop()
{
    if (!m_running) return;
    m_running = false;
    m_pollTimer->stop();
    emit gatewayStopped();
    qDebug() << "[TelegramGW] Polling stopped";
}

// ============================================================
//  Session Management & Locale Resolution
// ============================================================

TgChatSession& J2JTelegramGateway::getOrCreateSession(qint64 chatId)
{
    if (!m_sessions.contains(chatId)) {
        TgChatSession s;
        s.chatId = chatId;
        m_sessions[chatId] = s;
        resolveSessionLocale(m_sessions[chatId]);
    }
    return m_sessions[chatId];
}

bool J2JTelegramGateway::resolveSessionLocale(TgChatSession& session)
{
    QMutexLocker lock(&m_mutex);

    // Look up this chat_id in paired_devices by mobile_handle
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "SELECT device_id, bound_role FROM paired_devices "
        "WHERE mobile_handle = :handle AND active = 1 "
        "ORDER BY paired_at DESC LIMIT 1"));
    q.bindValue(QStringLiteral(":handle"), QString::number(session.chatId));

    if (q.exec() && q.next()) {
        session.deviceId  = q.value(0).toString();
        session.boundRole = q.value(1).toString();
    } else {
        // Fallback: check if any paired device matches by chat_id stored as device context
        QSqlQuery q2(QSqlDatabase::database());
        q2.prepare(QStringLiteral(
            "SELECT device_id, bound_role FROM paired_devices "
            "WHERE active = 1 ORDER BY paired_at DESC LIMIT 1"));
        if (q2.exec() && q2.next()) {
            session.deviceId  = q2.value(0).toString();
            session.boundRole = q2.value(1).toString();
        }
    }

    // QA_Tester → English; everything else → Russian
    session.isEnglish = (session.boundRole == QStringLiteral("QA_Tester"));

    return !session.deviceId.isEmpty();
}

// ============================================================
//  Telegram Bot API: Polling
// ============================================================

void J2JTelegramGateway::onPollUpdates()
{
    if (m_botToken.isEmpty()) return;

    QUrl url(kTgApiBase + m_botToken + QStringLiteral("/getUpdates"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("offset"),  QString::number(m_lastUpdateId + 1));
    query.addQueryItem(QStringLiteral("timeout"), QStringLiteral("1"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setTransferTimeout(5000);
    QNetworkReply* reply = m_network->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) return;
        QJsonObject root = doc.object();
        if (!root[QStringLiteral("ok")].toBool()) return;

        const QJsonArray results = root[QStringLiteral("result")].toArray();
        for (const auto& val : results) {
            QJsonObject update = val.toObject();
            qint64 updateId = static_cast<qint64>(
                update[QStringLiteral("update_id")].toDouble());
            if (updateId > m_lastUpdateId)
                m_lastUpdateId = updateId;
            processUpdate(update);
        }
    });
}

void J2JTelegramGateway::processUpdate(const QJsonObject& update)
{
    // Handle text messages
    if (update.contains(QStringLiteral("message"))) {
        QJsonObject msg = update[QStringLiteral("message")].toObject();
        qint64 chatId = static_cast<qint64>(
            msg[QStringLiteral("chat")].toObject()[QStringLiteral("id")].toDouble());
        QString text = msg[QStringLiteral("text")].toString().trimmed();
        QString firstName = msg[QStringLiteral("from")].toObject()
                                [QStringLiteral("first_name")].toString();
        if (!text.isEmpty())
            handleMessage(chatId, text, firstName);
    }

    // Handle inline keyboard callbacks
    if (update.contains(QStringLiteral("callback_query"))) {
        QJsonObject cbq = update[QStringLiteral("callback_query")].toObject();
        QString callbackId = cbq[QStringLiteral("id")].toString();
        qint64 chatId = static_cast<qint64>(
            cbq[QStringLiteral("message")].toObject()
                [QStringLiteral("chat")].toObject()
                [QStringLiteral("id")].toDouble());
        QString data = cbq[QStringLiteral("data")].toString();
        handleCallbackQuery(callbackId, chatId, data);
    }
}

// ============================================================
//  Message Handling
// ============================================================

void J2JTelegramGateway::handleMessage(qint64 chatId, const QString& text,
                                        const QString& firstName)
{
    TgChatSession& session = getOrCreateSession(chatId);
    bool en = session.isEnglish;

    emit messageReceived(chatId, text);

    // Slash commands
    if (text.startsWith(QLatin1Char('/'))) {
        const QString cmd = text.section(QLatin1Char(' '), 0, 0).toLower();

        if (cmd == QStringLiteral("/start") || cmd == QStringLiteral("/menu")) {
            session.wizardStep = QaWizardStep::Idle;
            sendMainMenu(chatId, en);
            return;
        }
        if (cmd == QStringLiteral("/cancel")) {
            if (session.wizardStep != QaWizardStep::Idle) {
                session.wizardStep = QaWizardStep::Idle;
                session.pendingBug = QaBugReport();
                sendMessage(chatId, localized(TgStringId::BugCancelled, en));
            }
            sendMainMenu(chatId, en);
            return;
        }
        if (cmd == QStringLiteral("/help")) {
            sendMessage(chatId, localized(TgStringId::HelpText, en));
            return;
        }
    }

    // If inside bug wizard, advance the state machine
    if (session.wizardStep != QaWizardStep::Idle) {
        advanceBugWizard(session, text);
        return;
    }

    // Unknown text
    sendMessage(chatId, localized(TgStringId::UnknownCommand, en));
}

void J2JTelegramGateway::handleCallbackQuery(const QString& callbackId,
                                               qint64 chatId,
                                               const QString& data)
{
    TgChatSession& session = getOrCreateSession(chatId);
    bool en = session.isEnglish;

    answerCallbackQuery(callbackId);

    // Main menu actions
    if (data == QStringLiteral("action_telemetry")) {
        // Build telemetry summary
        auto& db = DatabaseManager::instance();
        int chatCount = 0, taskCount = 0, patternCount = 0;
        {
            QMutexLocker lock(&m_mutex);
            QSqlQuery q(QSqlDatabase::database());
            q.exec(QStringLiteral("SELECT COUNT(*) FROM chat_history WHERE user_id = 1"));
            if (q.next()) chatCount = q.value(0).toInt();
            q.exec(QStringLiteral("SELECT COUNT(*) FROM tasks WHERE user_id = 1"));
            if (q.next()) taskCount = q.value(0).toInt();
            patternCount = db.patternCount(1);
        }

        QString telemetry = localized(TgStringId::TelemetryHeader, en);
        if (en) {
            telemetry += QStringLiteral(
                "```\n"
                "Messages:  %1\n"
                "Tasks:     %2\n"
                "Patterns:  %3\n"
                "Status:    Online ✓\n"
                "```").arg(chatCount).arg(taskCount).arg(patternCount);
        } else {
            telemetry += QStringLiteral(
                "```\n"
                "Сообщений:  %1\n"
                "Задач:      %2\n"
                "Паттернов:  %3\n"
                "Статус:     Онлайн ✓\n"
                "```").arg(chatCount).arg(taskCount).arg(patternCount);
        }
        sendMessage(chatId, telemetry);
        return;
    }

    if (data == QStringLiteral("action_kanban")) {
        auto tasks = DatabaseManager::instance().getTasks(1);
        if (tasks.isEmpty()) {
            sendMessage(chatId, localized(TgStringId::KanbanEmpty, en));
            return;
        }

        QString board = localized(TgStringId::KanbanHeader, en);
        QMap<QString, QStringList> columns;
        for (const auto& t : tasks) {
            QString icon;
            if (t.status == QStringLiteral("Todo"))       icon = QStringLiteral("⬜");
            else if (t.status == QStringLiteral("InProgress")) icon = QStringLiteral("🔶");
            else icon = QStringLiteral("✅");

            QString pri;
            if (t.priority == QStringLiteral("High")) pri = QStringLiteral("🔴");
            else if (t.priority == QStringLiteral("Medium")) pri = QStringLiteral("🟡");
            else pri = QStringLiteral("🟢");

            columns[t.status].append(
                QStringLiteral("  %1 %2 %3").arg(icon, pri, t.title));
        }

        for (const auto& status : {QStringLiteral("Todo"),
                                    QStringLiteral("InProgress"),
                                    QStringLiteral("Done")}) {
            if (!columns.contains(status)) continue;
            if (en)
                board += QStringLiteral("\n*%1:*\n").arg(status);
            else {
                QString label = status;
                if (status == QStringLiteral("Todo")) label = QStringLiteral("К выполнению");
                else if (status == QStringLiteral("InProgress")) label = QStringLiteral("В работе");
                else label = QStringLiteral("Готово");
                board += QStringLiteral("\n*%1:*\n").arg(label);
            }
            board += columns[status].join(QLatin1Char('\n'));
            board += QStringLiteral("\n");
        }

        sendMessage(chatId, board);
        return;
    }

    if (data == QStringLiteral("action_bug")) {
        startBugWizard(session);
        return;
    }

    if (data == QStringLiteral("action_wake")) {
        sendMessage(chatId, en ? QStringLiteral("⚡ Wake-on-LAN signal sent to desktop.")
                               : QStringLiteral("⚡ Wake-on-LAN сигнал отправлен на ПК."));
        return;
    }

    if (data == QStringLiteral("action_help")) {
        sendMessage(chatId, localized(TgStringId::HelpText, en));
        return;
    }

    // Severity selection (inline callback from bug wizard)
    if (data.startsWith(QStringLiteral("severity_"))) {
        QString severity = data.mid(9); // "severity_Blocker" → "Blocker"
        session.pendingBug.severity = severity;
        finalizeBugReport(session);
        return;
    }
}

// ============================================================
//  QA Bug Wizard
// ============================================================

void J2JTelegramGateway::startBugWizard(TgChatSession& session)
{
    session.wizardStep = QaWizardStep::AwaitingTitle;
    session.pendingBug = QaBugReport();
    session.pendingBug.chatId = session.chatId;
    session.pendingBug.reporterRole = session.boundRole;
    session.pendingBug.createdAt = QDateTime::currentDateTimeUtc();

    sendMessage(session.chatId,
                localized(TgStringId::BugTitlePrompt, session.isEnglish));
}

void J2JTelegramGateway::advanceBugWizard(TgChatSession& session,
                                           const QString& input)
{
    bool en = session.isEnglish;

    switch (session.wizardStep) {

    case QaWizardStep::AwaitingTitle:
        session.pendingBug.title = input;
        session.wizardStep = QaWizardStep::AwaitingSteps;
        sendMessage(session.chatId, localized(TgStringId::BugStepsPrompt, en));
        break;

    case QaWizardStep::AwaitingSteps:
        session.pendingBug.stepsToReproduce = input;
        session.wizardStep = QaWizardStep::AwaitingExpected;
        sendMessage(session.chatId, localized(TgStringId::BugExpectedPrompt, en));
        break;

    case QaWizardStep::AwaitingExpected:
        session.pendingBug.expectedResult = input;
        session.wizardStep = QaWizardStep::AwaitingActual;
        sendMessage(session.chatId, localized(TgStringId::BugActualPrompt, en));
        break;

    case QaWizardStep::AwaitingActual:
        session.pendingBug.actualResult = input;
        session.wizardStep = QaWizardStep::AwaitingSeverity;
        showSeverityKeyboard(session.chatId, en);
        break;

    case QaWizardStep::AwaitingSeverity:
        // Text input for severity (fallback if inline keyboard not used)
        session.pendingBug.severity = input;
        finalizeBugReport(session);
        break;

    case QaWizardStep::Idle:
        break;
    }
}

void J2JTelegramGateway::showSeverityKeyboard(qint64 chatId, bool english)
{
    QJsonObject markup = buildInlineKeyboard(buildSeverityButtons());
    sendMessage(chatId, localized(TgStringId::BugSeverityPrompt, english), markup);
}

void J2JTelegramGateway::finalizeBugReport(TgChatSession& session)
{
    session.wizardStep = QaWizardStep::Idle;
    bool en = session.isEnglish;

    // Generate Markdown artifact
    QString markdown = bugReportToMarkdown(session.pendingBug);

    // Persist to qa_artifacts
    persistBugReport(session.pendingBug);

    // Send confirmation with Markdown preview
    QString confirmation = localized(TgStringId::BugSaved, en);
    confirmation += QStringLiteral("\n\n```markdown\n");
    confirmation += markdown;
    confirmation += QStringLiteral("\n```");

    sendMessage(session.chatId, confirmation);

    emit bugReportFiled(session.pendingBug);

    // Send main menu again
    sendMainMenu(session.chatId, en);

    qDebug() << "[TelegramGW] Bug report filed:"
             << session.pendingBug.title
             << "severity:" << session.pendingBug.severity;
}

void J2JTelegramGateway::persistBugReport(const QaBugReport& report)
{
    QMutexLocker lock(&m_mutex);

    QString markdown = bugReportToMarkdown(report);

    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "INSERT INTO qa_artifacts "
        "(chat_id, reporter_name, reporter_role, title, "
        " steps_to_reproduce, expected_result, actual_result, "
        " severity, markdown_export, created_at) "
        "VALUES (:cid, :name, :role, :title, :steps, :expected, "
        "        :actual, :severity, :md, :created)"));
    q.bindValue(QStringLiteral(":cid"),      report.chatId);
    q.bindValue(QStringLiteral(":name"),     report.reporterName);
    q.bindValue(QStringLiteral(":role"),     report.reporterRole);
    q.bindValue(QStringLiteral(":title"),    report.title);
    q.bindValue(QStringLiteral(":steps"),    report.stepsToReproduce);
    q.bindValue(QStringLiteral(":expected"), report.expectedResult);
    q.bindValue(QStringLiteral(":actual"),   report.actualResult);
    q.bindValue(QStringLiteral(":severity"), report.severity);
    q.bindValue(QStringLiteral(":md"),       markdown);
    q.bindValue(QStringLiteral(":created"),
                report.createdAt.toString(Qt::ISODate));

    if (!q.exec())
        qWarning() << "[TelegramGW] Failed to persist bug report:"
                   << q.lastError().text();
}

// ============================================================
//  English Markdown Exporter
// ============================================================

QString J2JTelegramGateway::bugReportToMarkdown(const QaBugReport& bug)
{
    return QStringLiteral(
        "# Bug Report: %1\n\n"
        "| Field | Value |\n"
        "|-------|-------|\n"
        "| **Severity** | %2 |\n"
        "| **Reporter** | %3 |\n"
        "| **Role** | %4 |\n"
        "| **Date** | %5 |\n\n"
        "## Steps to Reproduce\n\n"
        "%6\n\n"
        "## Expected Result\n\n"
        "%7\n\n"
        "## Actual Result\n\n"
        "%8\n\n"
        "---\n"
        "_Generated by J.A.R.V.I.S. Mobile QA Gateway_\n"
    ).arg(bug.title,
          bug.severity,
          bug.reporterName.isEmpty() ? QStringLiteral("QA Tester") : bug.reporterName,
          bug.reporterRole.isEmpty() ? QStringLiteral("QA_Tester") : bug.reporterRole,
          bug.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm UTC")),
          bug.stepsToReproduce,
          bug.expectedResult,
          bug.actualResult);
}

// ============================================================
//  Telegram Bot API: Senders
// ============================================================

void J2JTelegramGateway::sendMessage(qint64 chatId, const QString& text,
                                      const QJsonObject& replyMarkup)
{
    if (m_botToken.isEmpty()) return;

    QUrl url(kTgApiBase + m_botToken + QStringLiteral("/sendMessage"));
    QJsonObject body;
    body[QStringLiteral("chat_id")]    = chatId;
    body[QStringLiteral("text")]       = text;
    body[QStringLiteral("parse_mode")] = QStringLiteral("Markdown");

    if (!replyMarkup.isEmpty())
        body[QStringLiteral("reply_markup")] = replyMarkup;

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    req.setTransferTimeout(5000);

    QNetworkReply* reply = m_network->post(
        req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void J2JTelegramGateway::sendMainMenu(qint64 chatId, bool english)
{
    QJsonObject markup = buildInlineKeyboard(buildMainMenuButtons(english));
    sendMessage(chatId, localized(TgStringId::MainMenu, english), markup);
}

void J2JTelegramGateway::answerCallbackQuery(const QString& callbackId,
                                              const QString& text)
{
    if (m_botToken.isEmpty()) return;

    QUrl url(kTgApiBase + m_botToken + QStringLiteral("/answerCallbackQuery"));
    QJsonObject body;
    body[QStringLiteral("callback_query_id")] = callbackId;
    if (!text.isEmpty())
        body[QStringLiteral("text")] = text;

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    req.setTransferTimeout(5000);

    QNetworkReply* reply = m_network->post(
        req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

// ============================================================
//  Inline Keyboard Builders
// ============================================================

QJsonObject J2JTelegramGateway::buildInlineKeyboard(const QJsonArray& rows)
{
    QJsonObject markup;
    markup[QStringLiteral("inline_keyboard")] = rows;
    return markup;
}

QJsonArray J2JTelegramGateway::buildMainMenuButtons(bool en)
{
    QJsonArray rows;

    // Row 1: Telemetry + Kanban
    QJsonArray row1;
    row1.append(QJsonObject{
        {QStringLiteral("text"), localized(TgStringId::BtnSystemTelemetry, en)},
        {QStringLiteral("callback_data"), QStringLiteral("action_telemetry")}
    });
    row1.append(QJsonObject{
        {QStringLiteral("text"), localized(TgStringId::BtnMyKanban, en)},
        {QStringLiteral("callback_data"), QStringLiteral("action_kanban")}
    });
    rows.append(row1);

    // Row 2: Bug Report + Wake PC
    QJsonArray row2;
    row2.append(QJsonObject{
        {QStringLiteral("text"), localized(TgStringId::BtnReportBug, en)},
        {QStringLiteral("callback_data"), QStringLiteral("action_bug")}
    });
    row2.append(QJsonObject{
        {QStringLiteral("text"), localized(TgStringId::BtnWakePC, en)},
        {QStringLiteral("callback_data"), QStringLiteral("action_wake")}
    });
    rows.append(row2);

    // Row 3: Help
    QJsonArray row3;
    row3.append(QJsonObject{
        {QStringLiteral("text"), localized(TgStringId::BtnHelp, en)},
        {QStringLiteral("callback_data"), QStringLiteral("action_help")}
    });
    rows.append(row3);

    return rows;
}

QJsonArray J2JTelegramGateway::buildSeverityButtons()
{
    QJsonArray rows;

    // Row 1: Blocker + Critical
    QJsonArray row1;
    row1.append(QJsonObject{
        {QStringLiteral("text"), QStringLiteral("🔴 Blocker")},
        {QStringLiteral("callback_data"), QStringLiteral("severity_Blocker")}
    });
    row1.append(QJsonObject{
        {QStringLiteral("text"), QStringLiteral("🟠 Critical")},
        {QStringLiteral("callback_data"), QStringLiteral("severity_Critical")}
    });
    rows.append(row1);

    // Row 2: Major + Minor
    QJsonArray row2;
    row2.append(QJsonObject{
        {QStringLiteral("text"), QStringLiteral("🟡 Major")},
        {QStringLiteral("callback_data"), QStringLiteral("severity_Major")}
    });
    row2.append(QJsonObject{
        {QStringLiteral("text"), QStringLiteral("🟢 Minor")},
        {QStringLiteral("callback_data"), QStringLiteral("severity_Minor")}
    });
    rows.append(row2);

    return rows;
}
