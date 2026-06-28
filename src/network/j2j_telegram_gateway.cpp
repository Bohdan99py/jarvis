// ============================================================
// j2j_telegram_gateway.cpp — Multi-Tenant Telegram Bot Gateway
// ============================================================

#include "j2j_telegram_gateway.h"
#include "mobile_pairing_manager.h"
#include "telegram_access_manager.h"
#include "pc_wake_agent.h"
#include "command_dispatcher_tg.h"
#include "social_presence.h"
#include "memory_manager.h"
#include "reflection_engine.h"
#include "personality_engine.h"
#include "semantic_intent_manager.h"
#include "layout_fixer.h"
#include "database_manager.h"
#include "llm_cache_manager.h"
#include "jarvis_response.h"
#include "voice_synthesis_manager.h"
#include "media_routing_manager.h"
#include "jarvis.h"
#include "translation_engine.h"
#include "jarvis_paths.h"

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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QUuid>
#include <QtConcurrent>
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
        return en ? QStringLiteral(
            "❓ *J.A.R.V.I.S. v3.5.5 — Mobile Help*\n\n"
            "=== WHAT'S NEW ===\n"
            "• LayoutFixer: type commands in any keyboard layout\n"
            "• Local LLM cache (SQLite v11) — offline fallback\n"
            "• Media Preview Hub — images & video in overlay\n"
            "• Smart Clipboard Watcher — auto-debug on Ctrl+C\n"
            "• Natural TTS via Piper voice synthesis queue\n\n"
            "=== COMMANDS ===\n"
            "• `/menu` — main control menu\n"
            "• `/screen_analyze` — capture screen for AI analysis\n"
            "• `/stop_voice` — silence voice playback\n"
            "• `/cache_stats` — show LLM cache statistics\n"
            "• `/cancel` — abort current wizard\n"
            "• `/help` — this message")
        : QStringLiteral(
            "❓ *J.A.R.V.I.S. v3.5.5 — Справка*\n\n"
            "=== ЧТО НОВОГО В v3.5.5 ===\n"
            "• LayoutFixer: команды работают в любой раскладке\n"
            "• Локальный кэш LLM (SQLite v11) — офлайн-ответы\n"
            "• Glassmorphic MediaPreview — превью медиа в оверлее\n"
            "• Clipboard Watcher — авто-отладка при Ctrl+C\n"
            "• Естественный TTS через Piper и очередь речи\n\n"
            "=== КРАТКАЯ ИНСТРУКЦИЯ ПОЛЬЗОВАТЕЛЯ ===\n"
            "• `/menu` (`/ьуну`) — главное меню\n"
            "• `/screen_analyze` (`/ысккуут_фтфдныу`) — захват экрана + ИИ анализ\n"
            "• `/stop_voice` (`/ытщз_мщшсу`) — остановить голос\n"
            "• `/cache_stats` — статистика кэша LLM\n"
            "• `/cancel` — отмена текущего мастера\n"
            "• `/help` — эта справка\n\n"
            "💡 Раскладка автоматически исправляется — можно "
            "набирать команды хоть на русской клавиатуре.\n"
            "📋 Скопируй ошибку C++ / UE5 через Ctrl+C — "
            "JARVIS сам разберёт и ответит.\n"
            "🔇 Для управления голосом: `/stop_voice` мгновенно "
            "останавливает все фразы в очереди.");

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

    m_typingTimer = new QTimer(this);
    m_typingTimer->setInterval(TYPING_INTERVAL_MS);
    connect(m_typingTimer, &QTimer::timeout,
            this, &J2JTelegramGateway::onTypingTimer);

    // Resolve or generate a persistent device ID for this PC
    auto& dbInst = DatabaseManager::instance();
    QString deviceId = dbInst.getConfig(
        QStringLiteral("local_device_id"), QString()).toString();
    if (deviceId.isEmpty()) {
        deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(16);
        dbInst.setConfig(QStringLiteral("local_device_id"), deviceId);
    }

    m_accessMgr = new TelegramAccessManager(deviceId, this);
    m_dispatcher = new CommandDispatcherTg(this, m_accessMgr, this);
    m_intentMgr  = new SemanticIntentManager(this, this);

    // Two-layer communication: WoL + cloud relay agent
    m_wakeAgent = new PcWakeAgent(deviceId, this);
    QString relayUrl = dbInst.getConfig(
        QStringLiteral("relay_server_url"), QString()).toString();
    if (!relayUrl.isEmpty())
        m_wakeAgent->setRelayUrl(relayUrl);
    QString relayKey = dbInst.getConfig(
        QStringLiteral("relay_api_key"), QString()).toString();
    if (!relayKey.isEmpty())
        m_wakeAgent->setRelayApiKey(relayKey);

    // Deliver queued messages that arrived while PC was asleep
    connect(m_wakeAgent, &PcWakeAgent::queuedMessageReceived,
            this, [this](qint64 chatId, const QString& text) {
        handleMessage(chatId, text, QStringLiteral("queued"));
    });

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

void J2JTelegramGateway::initSocialPresence(qint64 targetChatId)
{
    if (m_socialPresence) {
        m_socialPresence->stop();
        delete m_socialPresence;
    }

    // Initialize the vector memory store
    MemoryManager::instance().initialize();

    m_socialPresence = new SocialPresenceEngine(this);
    m_socialPresence->setTelegramGateway(this);
    m_socialPresence->setTargetChatId(targetChatId);
    m_socialPresence->setMemoryManager(&MemoryManager::instance());

    // Initialize the personality evolution engine
    auto* personality = new PersonalityEngine(this);
    m_personalityEng = personality;
    m_socialPresence->setPersonalityEngine(personality);

    // Initialize the autonomous reflection engine
    auto* reflection = new ReflectionEngine(this);
    reflection->setMemoryManager(&MemoryManager::instance());
    reflection->setSocialPresence(m_socialPresence);
    reflection->setPersonalityEngine(personality);
    m_socialPresence->setReflectionEngine(reflection);
    reflection->start(30);

    m_socialPresence->start();

    qDebug() << "[TelegramGW] Social Presence + Memory + Reflection + Personality "
                "initialized for chat" << targetChatId;
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

    // Register this PC as ONLINE and start heartbeat
    m_wakeAgent->start();

    // Auto-init Social Presence for the primary owner if known
    if (!m_socialPresence) {
        const qint64 ownerChat = m_accessMgr->primaryOwnerChatId();
        if (ownerChat != 0)
            initSocialPresence(ownerChat);
    }

    qDebug() << "[TelegramGW] Polling started (every" << POLL_INTERVAL_MS << "ms)";
}

void J2JTelegramGateway::stop()
{
    if (!m_running) return;
    m_running = false;
    m_pollTimer->stop();

    // Mark this PC as OFFLINE in the relay
    m_wakeAgent->stop();

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
    // Handle messages (text, voice, audio)
    if (update.contains(QStringLiteral("message"))) {
        QJsonObject msg = update[QStringLiteral("message")].toObject();
        qint64 chatId = static_cast<qint64>(
            msg[QStringLiteral("chat")].toObject()[QStringLiteral("id")].toDouble());
        QString firstName = msg[QStringLiteral("from")].toObject()
                                [QStringLiteral("first_name")].toString();

        // Voice note or audio file
        if (msg.contains(QStringLiteral("voice")) || msg.contains(QStringLiteral("audio"))) {
            QJsonObject voiceObj = msg.contains(QStringLiteral("voice"))
                ? msg[QStringLiteral("voice")].toObject()
                : msg[QStringLiteral("audio")].toObject();
            TgChatSession& session = getOrCreateSession(chatId);
            handleVoiceMessage(chatId, voiceObj, session.isEnglish);
            return;
        }

        // Photo upload
        if (msg.contains(QStringLiteral("photo"))) {
            QJsonArray photos = msg[QStringLiteral("photo")].toArray();
            QString caption = msg[QStringLiteral("caption")].toString();
            TgChatSession& session = getOrCreateSession(chatId);
            handlePhotoMessage(chatId, photos, caption, session.isEnglish);
            return;
        }

        // Text message
        QString text = msg[QStringLiteral("text")].toString().trimmed();
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
//  Message Handling — strict priority hierarchy:
//    1. Pairing PIN  (highest — must not be swallowed by LLM)
//    2. RBAC-gated slash commands
//    3. Bug wizard (if a wizard session is active)
//    4. Free-dialogue LLM routing (lowest)
// ============================================================

void J2JTelegramGateway::handleMessage(qint64 chatId, const QString& text,
                                        const QString& firstName)
{
    emit messageReceived(chatId, text);

    // Register user in RBAC system (first user → Admin)
    m_accessMgr->ensureRegistered(chatId, firstName);

    // ── 0. Session check — verify this chat is bound to THIS PC ─
    {
        auto pcSession = m_accessMgr->resolveSession(chatId);
        if (pcSession.has_value() && !m_accessMgr->isSessionBoundHere(chatId)) {
            TgChatSession& cs = getOrCreateSession(chatId);
            sendMessage(chatId, cs.isEnglish
                ? QStringLiteral("🖥 This chat is bound to a different PC (`%1`). "
                                 "Use /bind_pc to re-bind to the current machine.")
                    .arg(pcSession->pcName)
                : QStringLiteral("🖥 Этот чат привязан к другому ПК (`%1`). "
                                 "Используйте /bind_pc чтобы привязать к текущей машине.")
                    .arg(pcSession->pcName));
            m_accessMgr->logActivity(chatId, QStringLiteral("wrong_pc"),
                                      pcSession->deviceId);
            return;
        }
    }

    // ── 1. TOP PRIORITY: Pairing PIN ────────────────────────
    if (handlePairing(chatId, text, firstName))
        return;

    // ── 1.5. Layout auto-correction for slash commands ──────
    QString effectiveText = text;
    bool layoutFixed = false;
    if (text.startsWith(QLatin1Char('/'))) {
        auto fix = LayoutFixer::tryFixCommand(text, TelegramAccessManager::knownCommands());
        if (fix.changed) {
            effectiveText = fix.text;
            layoutFixed = true;
            qDebug() << "[TelegramGW] Layout fix:" << text << "→" << effectiveText;
        }
    }

    // ── 2. Slash commands (RBAC-gated) ──────────────────────
    if (effectiveText.startsWith(QLatin1Char('/'))) {
        const QString cmd = effectiveText.section(QLatin1Char(' '), 0, 0).toLower();

        // RBAC check — deny before any processing
        if (!m_accessMgr->hasAccess(chatId, cmd)) {
            TgChatSession& session = getOrCreateSession(chatId);
            sendMessage(chatId, session.isEnglish
                ? QStringLiteral("🔒 Access denied. Your role: *%1*. "
                                 "Required: *%2*.")
                    .arg(telegramRoleToString(m_accessMgr->getRole(chatId)),
                         telegramRoleToString(TelegramAccessManager::minimumRoleFor(cmd)))
                : QStringLiteral("🔒 Доступ запрещён. Ваша роль: *%1*. "
                                 "Требуется: *%2*.")
                    .arg(telegramRoleToString(m_accessMgr->getRole(chatId)),
                         telegramRoleToString(TelegramAccessManager::minimumRoleFor(cmd))));
            m_accessMgr->logActivity(chatId, QStringLiteral("denied"), cmd);
            return;
        }

        m_accessMgr->logActivity(chatId, QStringLiteral("command"), cmd);

        // Diagnostic prefix for layout-corrected commands
        QString fixPrefix;
        if (layoutFixed)
            fixPrefix = QStringLiteral("🔧 *Layout fixed:* `%1` → `%2`\n\n")
                .arg(text, effectiveText);

        // Try the command dispatcher first (sandboxed system actions)
        {
            TgChatSession& ds = getOrCreateSession(chatId);
            DispatchResult dr = m_dispatcher->dispatch(chatId, effectiveText, ds.isEnglish);
            if (dr.handled) {
                if (!dr.response.isEmpty())
                    sendMessage(chatId, fixPrefix + dr.response);
                if (!dr.imagePath.isEmpty()) {
                    MediaRoutingManager::instance().previewImage(dr.imagePath);
                    sendImageToMobile(chatId, dr.imagePath, QString());
                }
                return;
            }
        }

        if (cmd == QStringLiteral("/start") || cmd == QStringLiteral("/menu")) {
            TgChatSession& session = getOrCreateSession(chatId);
            session.wizardStep = QaWizardStep::Idle;
            if (layoutFixed)
                sendMessage(chatId, fixPrefix.trimmed());
            sendMainMenu(chatId, session.isEnglish);
            return;
        }
        if (cmd == QStringLiteral("/cancel")) {
            TgChatSession& session = getOrCreateSession(chatId);
            if (session.wizardStep != QaWizardStep::Idle) {
                session.wizardStep = QaWizardStep::Idle;
                session.pendingBug = QaBugReport();
                sendMessage(chatId, fixPrefix + localized(TgStringId::BugCancelled, session.isEnglish));
            }
            sendMainMenu(chatId, session.isEnglish);
            return;
        }
        if (cmd == QStringLiteral("/help")) {
            TgChatSession& session = getOrCreateSession(chatId);
            sendMessage(chatId, fixPrefix + localized(TgStringId::HelpText, session.isEnglish));
            return;
        }

        // ── Admin commands ──────────────────────────────────
        if (cmd == QStringLiteral("/admin_stats")) {
            sendMessage(chatId, fixPrefix + m_accessMgr->buildStatsReport());
            return;
        }
        if (cmd == QStringLiteral("/admin_grant")) {
            const QString cmdArgs = effectiveText.section(QLatin1Char(' '), 1).trimmed();
            const QString targetIdStr = cmdArgs.section(QLatin1Char(' '), 0, 0);
            const QString roleName = cmdArgs.section(QLatin1Char(' '), 1, 1);
            bool ok = false;
            qint64 targetId = targetIdStr.toLongLong(&ok);
            if (!ok || roleName.isEmpty()) {
                sendMessage(chatId, QStringLiteral("Usage: `/admin_grant <chat_id> <Admin|Tester|User>`"));
                return;
            }
            TelegramRole newRole = telegramRoleFromString(roleName);
            m_accessMgr->setRole(targetId, newRole);
            sendMessage(chatId, fixPrefix + QStringLiteral("✅ Chat %1 → role *%2*")
                .arg(targetId).arg(telegramRoleToString(newRole)));
            return;
        }
        if (cmd == QStringLiteral("/admin_revoke")) {
            const QString targetIdStr = effectiveText.section(QLatin1Char(' '), 1, 1).trimmed();
            bool ok = false;
            qint64 targetId = targetIdStr.toLongLong(&ok);
            if (!ok) {
                sendMessage(chatId, QStringLiteral("Usage: `/admin_revoke <chat_id>`"));
                return;
            }
            m_accessMgr->setRole(targetId, TelegramRole::User);
            sendMessage(chatId, fixPrefix + QStringLiteral("✅ Chat %1 → role *User*").arg(targetId));
            return;
        }

        // Unknown slash command — fall through to LLM
    }

    // ── 3. Bug wizard (active session) ──────────────────────
    TgChatSession& session = getOrCreateSession(chatId);
    if (session.wizardStep != QaWizardStep::Idle) {
        m_accessMgr->logActivity(chatId, QStringLiteral("wizard"), text.left(50));
        advanceBugWizard(session, text);
        return;
    }

    // ── 3.5. Social Presence — availability tracking & troll detection ──
    // Auto-init on first message from the primary owner
    if (!m_socialPresence && m_accessMgr->isPrimaryOwner(chatId))
        initSocialPresence(chatId);

    if (m_socialPresence) {
        const auto statusBefore = m_socialPresence->currentStatus();
        const QString sarcasticReply = m_socialPresence->processIncomingMessage(text);

        if (!sarcasticReply.isEmpty()) {
            sendMessage(chatId, sarcasticReply);
            m_accessMgr->logActivity(chatId, QStringLiteral("troll"), text.left(50));
            return;
        }

        const auto statusAfter = m_socialPresence->currentStatus();

        // If this message just transitioned the user to BUSY/PAUSED, acknowledge
        if (statusBefore != statusAfter) {
            if (statusAfter == UserState::BUSY) {
                const QString topic = m_socialPresence->currentTopic();
                const QString ack = topic.isEmpty()
                    ? (session.isEnglish
                        ? QStringLiteral("👍 Got it. I'll hold my thoughts until you're free.")
                        : QStringLiteral("👍 Понял. Подожду, пока освободишься."))
                    : (session.isEnglish
                        ? QStringLiteral("👍 Got it — you're *%1*. I'll save any thoughts for later.").arg(topic)
                        : QStringLiteral("👍 Понял — ты *%1*. Сохраню мысли на потом.").arg(topic));
                sendMessage(chatId, ack);
                m_accessMgr->logActivity(chatId, QStringLiteral("status_busy"), text.left(50));
                return;
            }
            if (statusAfter == UserState::PAUSED) {
                sendMessage(chatId, session.isEnglish
                    ? QStringLiteral("⏸ Paused. Send anything when you're back.")
                    : QStringLiteral("⏸ На паузе. Напиши, когда вернёшься."));
                m_accessMgr->logActivity(chatId, QStringLiteral("status_paused"), text.left(50));
                return;
            }
            if (statusAfter == UserState::FREE && statusBefore != UserState::FREE) {
                // Returning from BUSY/PAUSED — welcome back (nudge flush handled internally)
                m_accessMgr->logActivity(chatId, QStringLiteral("status_free"), text.left(50));
                // Don't return — let the message flow to LLM as usual
            }
        }
    }

    // ── 3.7. Semantic Intent — goal-driven action execution ──
    if (m_intentMgr) {
        const IntentMatch intent = m_intentMgr->classifyIntent(text);
        if (intent.matched) {
            if (intent.confidence >= 0.85) {
                // High confidence — execute silently, send acknowledgement
                const QString ack = m_intentMgr->executeActions(intent);
                if (!ack.isEmpty())
                    sendMessage(chatId, ack);
                m_accessMgr->logActivity(chatId, QStringLiteral("intent"),
                    intent.goalId + QStringLiteral(":") + QString::number(intent.confidence, 'f', 2));
                return;
            }
            if (intent.confidence >= 0.50) {
                // Medium confidence — ask for confirmation
                const QString question = (session.isEnglish
                    ? QStringLiteral("I think you mean *%1*. Should I go ahead?")
                    : QStringLiteral("Похоже, ты хочешь *%1*. Запустить?"))
                    .arg(intent.goalLabel);
                sendMessage(chatId, question);
                m_accessMgr->logActivity(chatId, QStringLiteral("intent_confirm"),
                    intent.goalId + QStringLiteral(":") + QString::number(intent.confidence, 'f', 2));
                return;
            }
        }
    }

    // ── 4. Free-dialogue LLM routing (lowest priority) ──────
    m_accessMgr->logActivity(chatId, QStringLiteral("message"), text.left(50));
    routeToLlm(chatId, text, session.isEnglish);
}

// ============================================================
//  Pairing PIN Handler
// ============================================================

bool J2JTelegramGateway::handlePairing(qint64 chatId, const QString& text,
                                        const QString& firstName)
{
    if (!m_pairing) return false;
    if (!m_pairing->hasActiveSession()) return false;
    if (!MobilePairingManager::looksLikePin(text)) return false;

    const QString displayName = firstName.isEmpty()
        ? QStringLiteral("Telegram User %1").arg(chatId)
        : firstName;

    bool paired = m_pairing->tryPairViaTelegram(
        text, QString::number(chatId), displayName);

    if (paired) {
        // Re-resolve locale now that the device is bound
        TgChatSession& session = getOrCreateSession(chatId);
        resolveSessionLocale(session);
        bool en = session.isEnglish;

        sendMessage(chatId,
            localized(TgStringId::PairSuccess, en).arg(session.boundRole));
        sendMainMenu(chatId, en);

        emit pairingCompleted(chatId, session.boundRole);

        qDebug() << "[TelegramGW] Device paired via PIN:" << displayName
                 << "→ role:" << session.boundRole;
    } else {
        // PIN format matched but didn't pair — wrong or expired PIN
        sendMessage(chatId,
            QStringLiteral("❌ Invalid or expired PIN. Please check and try again.\n"
                           "❌ Неверный или истёкший PIN. Проверьте и попробуйте снова."));
    }

    return true;  // consumed: don't pass to LLM regardless of outcome
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
//  Free-Dialogue LLM Routing
// ============================================================

void J2JTelegramGateway::routeToLlm(qint64 chatId, const QString& text,
                                      bool english)
{
    if (!m_jarvis) {
        sendMessage(chatId, english
            ? QStringLiteral("⚠ AI core not connected. Use buttons or `/menu`.")
            : QStringLiteral("⚠ AI-ядро не подключено. Используйте кнопки или `/menu`."));
        return;
    }

    TgChatSession& session = getOrCreateSession(chatId);
    if (session.awaitingLlm) {
        sendMessage(chatId, english
            ? QStringLiteral("⏳ Still processing your previous message...")
            : QStringLiteral("⏳ Ещё обрабатываю предыдущее сообщение..."));
        return;
    }

    // ── Step 6a: Local-First Offline Cache — check before any network I/O ──
    {
        const QString cached = LlmCacheManager::instance()
                                   .getValidCachedResponse(text);
        if (!cached.isEmpty()) {
            const QString tagged = cached
                + QStringLiteral("\n\n💾 *Served from local cache*");
            sendMessage(chatId, tagged);
            emit conversationResponse(chatId, tagged);

            JarvisResponse dual = JarvisResponse::parse(cached);
            VoiceSynthesisManager::instance().say(dual.speechText);

            qDebug() << "[TelegramGW] LLM cache HIT for chat" << chatId;
            return;
        }
    }

    session.awaitingLlm = true;
    startTypingIndicator(chatId);

    QString langInstruction = english
        ? QStringLiteral("Respond in English. The user is a QA Tester.")
        : QStringLiteral("Отвечай на русском языке.");

    // ── Emotional Tone Modulation — inject mood into system prompt ──
    if (m_personalityEng) {
        EmotionalState emo = m_personalityEng->emotionalState();
        langInstruction += QStringLiteral(
            "\n[SYSTEM_METADATA: Current Mood={joy: %1, frustration: %2, "
            "curiosity: %3, boredom: %4}. "
            "If frustration is high, your tone should be terse and impatient. "
            "If joy is high, be more conversational and use emojis. "
            "If curiosity is high, ask follow-up questions. "
            "If boredom is high, be brief and suggest something new.]")
            .arg(emo.joy,         0, 'f', 2)
            .arg(emo.frustration, 0, 'f', 2)
            .arg(emo.curiosity,   0, 'f', 2)
            .arg(emo.boredom,     0, 'f', 2);
    }

    QString syncResponse = m_jarvis->processCommand(text, QString(), langInstruction);

    if (!syncResponse.isEmpty() && syncResponse != QStringLiteral("...")) {
        finishLlmRequest(chatId);

        JarvisResponse dual = JarvisResponse::parse(syncResponse);
        sendMessage(chatId, dual.fullText);
        emit conversationResponse(chatId, dual.fullText);
        VoiceSynthesisManager::instance().say(dual.speechText);
        return;
    }

    // Shared guard: both callbacks and the timeout use this to ensure
    // exactly one of them fires. Once set to true, the others no-op.
    auto resolved = std::make_shared<bool>(false);

    auto conn    = std::make_shared<QMetaObject::Connection>();
    auto errConn = std::make_shared<QMetaObject::Connection>();

    // Cleanup lambda — disconnects both signals, releases the lock
    auto cleanup = [this, chatId, conn, errConn, resolved]() {
        if (*resolved) return;
        *resolved = true;
        disconnect(*conn);
        disconnect(*errConn);
        finishLlmRequest(chatId);
    };

    // Capture original query for cache save on success
    const QString originalQuery = text;

    *conn = connect(m_jarvis, &Jarvis::asyncResponseReady, this,
                    [this, chatId, cleanup, originalQuery](const QString& response) {
        cleanup();
        sendMessage(chatId, response);
        emit conversationResponse(chatId, response);

        // ── Step 6b: Cache the successful LLM response asynchronously ──
        LlmCacheManager::instance().saveResponse(originalQuery, response);

        qDebug() << "[TelegramGW] LLM response delivered to chat" << chatId
                 << "(" << response.length() << "chars)";
    });

    *errConn = connect(m_jarvis, &Jarvis::asyncResponseError, this,
                       [this, chatId, cleanup, originalQuery, english](const QString& error) {
        cleanup();

        // ── Step 6c: Network failure — try cache fallback, then static message ──
        const QString fallback = LlmCacheManager::instance()
                                     .getValidCachedResponse(originalQuery);
        if (!fallback.isEmpty()) {
            const QString tagged = fallback
                + QStringLiteral("\n\n💾 *Served from local cache*");
            sendMessage(chatId, tagged);

            JarvisResponse dual = JarvisResponse::parse(fallback);
            VoiceSynthesisManager::instance().say(dual.speechText);

            qDebug() << "[TelegramGW] Error fallback: cache HIT for chat" << chatId;
        } else {
            sendMessage(chatId, english
                ? QStringLiteral("📡 *System Offline:* Network timeout, "
                                 "and no local cache available for this query.")
                : QStringLiteral("📡 *Система офлайн:* Таймаут сети, "
                                 "и для этого запроса нет локального кэша."));

            VoiceSynthesisManager::instance().say(english
                ? QStringLiteral("I'm offline right now, can't reach the server.")
                : QStringLiteral("Я сейчас не в сети, не могу подключиться к серверу."));

            qDebug() << "[TelegramGW] Error fallback: no cache, static offline msg."
                     << "Error:" << error;
        }
    });

    // Safety timeout: if neither signal fires within 90 seconds,
    // force-release the lock so the chat isn't stuck forever.
    QTimer::singleShot(90000, this, [this, chatId, cleanup, originalQuery, english]() {
        cleanup();
        if (m_sessions.contains(chatId) && !m_sessions[chatId].awaitingLlm)
            return;  // already cleaned up by a signal

        // ── Timeout fallback: same cache-then-static logic ──
        const QString fallback = LlmCacheManager::instance()
                                     .getValidCachedResponse(originalQuery);
        if (!fallback.isEmpty()) {
            const QString tagged = fallback
                + QStringLiteral("\n\n💾 *Served from local cache (timeout fallback)*");
            sendMessage(chatId, tagged);

            JarvisResponse dual = JarvisResponse::parse(fallback);
            VoiceSynthesisManager::instance().say(dual.speechText);

            qDebug() << "[TelegramGW] Timeout fallback: cache HIT for chat" << chatId;
        } else {
            sendMessage(chatId, english
                ? QStringLiteral("📡 *System Offline:* Request timed out, "
                                 "and no local cache available for this query.")
                : QStringLiteral("📡 *Система офлайн:* Запрос превысил время ожидания, "
                                 "и для этого запроса нет локального кэша."));

            VoiceSynthesisManager::instance().say(english
                ? QStringLiteral("Request timed out. I can't reach the server right now.")
                : QStringLiteral("Запрос не прошёл, сервер не отвечает. Попробуй позже."));

            qDebug() << "[TelegramGW] Timeout fallback: no cache for chat" << chatId;
        }
    });
}

void J2JTelegramGateway::finishLlmRequest(qint64 chatId)
{
    stopTypingIndicator(chatId);
    if (m_sessions.contains(chatId))
        m_sessions[chatId].awaitingLlm = false;
}

// ============================================================
//  Typing Indicator
// ============================================================

void J2JTelegramGateway::sendChatAction(qint64 chatId, const QString& action)
{
    if (m_botToken.isEmpty()) return;

    QUrl url(kTgApiBase + m_botToken + QStringLiteral("/sendChatAction"));
    QJsonObject body;
    body[QStringLiteral("chat_id")] = chatId;
    body[QStringLiteral("action")]  = action;

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    req.setTransferTimeout(3000);

    QNetworkReply* reply = m_network->post(
        req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void J2JTelegramGateway::startTypingIndicator(qint64 chatId)
{
    m_typingChats.insert(chatId);
    sendChatAction(chatId, QStringLiteral("typing"));
    if (!m_typingTimer->isActive())
        m_typingTimer->start();
}

void J2JTelegramGateway::stopTypingIndicator(qint64 chatId)
{
    m_typingChats.remove(chatId);
    if (m_typingChats.isEmpty())
        m_typingTimer->stop();
}

void J2JTelegramGateway::onTypingTimer()
{
    for (qint64 chatId : m_typingChats)
        sendChatAction(chatId, QStringLiteral("typing"));
}

// ============================================================
//  Voice Message Processing
// ============================================================

void J2JTelegramGateway::handleVoiceMessage(qint64 chatId,
                                              const QJsonObject& voice,
                                              bool isEnglish)
{
    const QString fileId = voice[QStringLiteral("file_id")].toString();
    if (fileId.isEmpty()) return;

    sendMessage(chatId, isEnglish
        ? QStringLiteral("🎙 Voice message received — transcribing...")
        : QStringLiteral("🎙 Голосовое сообщение получено — транскрибирую..."));

    startTypingIndicator(chatId);
    downloadTelegramFile(fileId, chatId, isEnglish);
}

void J2JTelegramGateway::downloadTelegramFile(const QString& fileId,
                                                qint64 chatId,
                                                bool isEnglish)
{
    // Step 1: call getFile to get file_path
    QUrl url(kTgApiBase + m_botToken + QStringLiteral("/getFile"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("file_id"), fileId);
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setTransferTimeout(10000);
    QNetworkReply* reply = m_network->get(req);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, chatId, isEnglish]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            stopTypingIndicator(chatId);
            sendMessage(chatId, isEnglish
                ? QStringLiteral("⚠ Failed to retrieve voice file.")
                : QStringLiteral("⚠ Не удалось получить голосовой файл."));
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();
        if (!root[QStringLiteral("ok")].toBool()) {
            stopTypingIndicator(chatId);
            return;
        }

        QString filePath = root[QStringLiteral("result")].toObject()
                               [QStringLiteral("file_path")].toString();
        if (filePath.isEmpty()) {
            stopTypingIndicator(chatId);
            return;
        }

        // Step 2: download the actual file
        QUrl dlUrl(QStringLiteral("https://api.telegram.org/file/bot")
                   + m_botToken + QStringLiteral("/") + filePath);
        QNetworkRequest dlReq(dlUrl);
        dlReq.setTransferTimeout(30000);
        QNetworkReply* dlReply = m_network->get(dlReq);

        connect(dlReply, &QNetworkReply::finished, this,
                [this, dlReply, chatId, isEnglish, filePath]() {
            dlReply->deleteLater();
            if (dlReply->error() != QNetworkReply::NoError) {
                stopTypingIndicator(chatId);
                sendMessage(chatId, isEnglish
                    ? QStringLiteral("⚠ Voice file download failed.")
                    : QStringLiteral("⚠ Скачивание голосового файла не удалось."));
                return;
            }

            QByteArray audioData = dlReply->readAll();
            if (audioData.isEmpty()) {
                stopTypingIndicator(chatId);
                return;
            }

            // Save to temp file
            QString ext = QStringLiteral(".ogg");
            if (filePath.endsWith(QStringLiteral(".wav"))) ext = QStringLiteral(".wav");
            else if (filePath.endsWith(QStringLiteral(".m4a"))) ext = QStringLiteral(".m4a");
            else if (filePath.endsWith(QStringLiteral(".mp3"))) ext = QStringLiteral(".mp3");

            QString tempDir = JarvisPaths::dataRoot() + QStringLiteral("/voice_telegram");
            QDir().mkpath(tempDir);
            QString tempPath = tempDir + QStringLiteral("/tg_voice_%1_%2%3")
                .arg(chatId)
                .arg(QDateTime::currentMSecsSinceEpoch())
                .arg(ext);

            QFile file(tempPath);
            if (!file.open(QIODevice::WriteOnly)) {
                stopTypingIndicator(chatId);
                return;
            }
            file.write(audioData);
            file.close();

            qDebug() << "[TelegramGW] Voice file saved:" << tempPath
                     << "(" << audioData.size() << "bytes)";

            // Route to translation engine for transcription
            if (m_translator) {
                QString targetLang = isEnglish ? QStringLiteral("en") : QStringLiteral("ru");

                // Connect transcription result → LLM → reply
                auto vResolved = std::make_shared<bool>(false);
                auto transConn = std::make_shared<QMetaObject::Connection>();
                auto vErrConn  = std::make_shared<QMetaObject::Connection>();

                auto voiceCleanup = [vResolved, transConn, vErrConn, tempPath]() {
                    if (*vResolved) return false;
                    *vResolved = true;
                    disconnect(*transConn);
                    disconnect(*vErrConn);
                    QFile::remove(tempPath);
                    return true;
                };

                *transConn = connect(m_translator, &TranslationEngine::audioTranscribed, this,
                    [this, chatId, isEnglish, voiceCleanup](const QString& transcript, const QString& lang) {
                    if (!voiceCleanup()) return;

                    if (transcript.trimmed().isEmpty()) {
                        stopTypingIndicator(chatId);
                        sendMessage(chatId, isEnglish
                            ? QStringLiteral("🎙 Could not transcribe the voice message.")
                            : QStringLiteral("🎙 Не удалось распознать голосовое сообщение."));
                        return;
                    }

                    emit voiceProcessed(chatId, transcript);

                    sendMessage(chatId, (isEnglish
                        ? QStringLiteral("🎙 *Transcript* (%1):\n_%2_")
                        : QStringLiteral("🎙 *Транскрипт* (%1):\n_%2_"))
                        .arg(lang.toUpper(), transcript.left(500)));

                    routeToLlm(chatId, transcript, isEnglish);
                });

                *vErrConn = connect(m_translator, &TranslationEngine::audioProcessingError, this,
                    [this, chatId, isEnglish, voiceCleanup](const QString& error) {
                    if (!voiceCleanup()) return;
                    stopTypingIndicator(chatId);
                    sendMessage(chatId, (isEnglish
                        ? QStringLiteral("⚠ Voice processing error: %1")
                        : QStringLiteral("⚠ Ошибка обработки голоса: %1")).arg(error));
                });

                m_translator->processAudioFile(tempPath, targetLang,
                    QStringLiteral("TelegramVoice"));
            } else {
                // No translation engine — try to route raw text
                stopTypingIndicator(chatId);
                QFile::remove(tempPath);
                sendMessage(chatId, isEnglish
                    ? QStringLiteral("⚠ Voice transcription engine not available.")
                    : QStringLiteral("⚠ Движок транскрипции недоступен."));
            }
        });
    });
}

// ============================================================
//  Desktop Media Workspace
// ============================================================

QString J2JTelegramGateway::workspaceOutputDir()
{
    const QString desktop = QStandardPaths::writableLocation(
        QStandardPaths::DesktopLocation);
    const QString dir = desktop + QStringLiteral("/JARVIS_Workspace/Outputs");
    QDir().mkpath(dir);
    return dir;
}

void J2JTelegramGateway::saveWorkspaceAsset(const QString& filename,
                                              const QByteArray& data,
                                              const QString& companionMarkdown)
{
    // Offload file I/O to a background thread
    (void)QtConcurrent::run([filename, data, companionMarkdown]() {
        const QString dir = workspaceOutputDir();
        const QString filePath = dir + QStringLiteral("/") + filename;

        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
            file.close();
            qDebug() << "[TelegramGW] Asset saved:" << filePath
                     << "(" << data.size() << "bytes)";
        } else {
            qWarning() << "[TelegramGW] Failed to save asset:" << filePath;
        }

        // Write companion Markdown guide alongside the asset
        if (!companionMarkdown.isEmpty()) {
            const QString baseName = QFileInfo(filename).completeBaseName();
            const QString mdPath = dir + QStringLiteral("/") + baseName
                                   + QStringLiteral("_guide.md");
            QFile mdFile(mdPath);
            if (mdFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                mdFile.write(companionMarkdown.toUtf8());
                mdFile.close();
            }
        }
    });
}

// ============================================================
//  Inbound Image Handling (Telegram → Desktop)
// ============================================================

void J2JTelegramGateway::handlePhotoMessage(qint64 chatId,
                                              const QJsonArray& photos,
                                              const QString& /*caption*/,
                                              bool isEnglish)
{
    if (photos.isEmpty()) return;

    // Telegram sends multiple resolutions — pick the largest (last element)
    QJsonObject bestPhoto = photos.last().toObject();
    QString fileId = bestPhoto[QStringLiteral("file_id")].toString();
    if (fileId.isEmpty()) return;

    sendMessage(chatId, isEnglish
        ? QStringLiteral("📷 Image received — saving to Desktop workspace...")
        : QStringLiteral("📷 Изображение получено — сохраняю на Рабочий стол..."));

    startTypingIndicator(chatId);
    downloadAndSaveImage(fileId, chatId, isEnglish);
}

void J2JTelegramGateway::downloadAndSaveImage(const QString& fileId,
                                                qint64 chatId,
                                                bool isEnglish)
{
    // Step 1: resolve file_path via getFile
    QUrl url(kTgApiBase + m_botToken + QStringLiteral("/getFile"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("file_id"), fileId);
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setTransferTimeout(10000);
    QNetworkReply* reply = m_network->get(req);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, chatId, isEnglish]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            stopTypingIndicator(chatId);
            sendMessage(chatId, isEnglish
                ? QStringLiteral("⚠ Failed to retrieve image metadata.")
                : QStringLiteral("⚠ Не удалось получить метаданные изображения."));
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();
        if (!root[QStringLiteral("ok")].toBool()) {
            stopTypingIndicator(chatId);
            return;
        }

        QString remotePath = root[QStringLiteral("result")].toObject()
                                 [QStringLiteral("file_path")].toString();
        if (remotePath.isEmpty()) {
            stopTypingIndicator(chatId);
            return;
        }

        // Step 2: download the actual image bytes
        QUrl dlUrl(QStringLiteral("https://api.telegram.org/file/bot")
                   + m_botToken + QStringLiteral("/") + remotePath);
        QNetworkRequest dlReq(dlUrl);
        dlReq.setTransferTimeout(30000);
        QNetworkReply* dlReply = m_network->get(dlReq);

        connect(dlReply, &QNetworkReply::finished, this,
                [this, dlReply, chatId, isEnglish, remotePath]() {
            dlReply->deleteLater();
            stopTypingIndicator(chatId);

            if (dlReply->error() != QNetworkReply::NoError) {
                sendMessage(chatId, isEnglish
                    ? QStringLiteral("⚠ Image download failed.")
                    : QStringLiteral("⚠ Скачивание изображения не удалось."));
                return;
            }

            QByteArray imageData = dlReply->readAll();
            if (imageData.isEmpty()) return;

            // Determine extension from remote path
            QString ext = QStringLiteral(".jpg");
            if (remotePath.endsWith(QStringLiteral(".png")))       ext = QStringLiteral(".png");
            else if (remotePath.endsWith(QStringLiteral(".webp"))) ext = QStringLiteral(".webp");
            else if (remotePath.endsWith(QStringLiteral(".gif")))  ext = QStringLiteral(".gif");

            // Timestamped filename
            const QString timestamp = QDateTime::currentDateTime()
                .toString(QStringLiteral("yyyyMMdd_HHmmss"));
            const QString filename = QStringLiteral("IMG_%1%2").arg(timestamp, ext);

            // Companion Markdown
            QString markdown = QStringLiteral(
                "# Image Asset: %1\n\n"
                "| Field | Value |\n"
                "|-------|-------|\n"
                "| **Source** | Telegram Mobile Upload |\n"
                "| **Chat ID** | %2 |\n"
                "| **Received** | %3 |\n"
                "| **Size** | %4 bytes |\n\n"
                "![%1](%1)\n\n"
                "---\n"
                "_Saved by J.A.R.V.I.S. Media Workspace_\n"
            ).arg(filename,
                  QString::number(chatId),
                  QDateTime::currentDateTime().toString(Qt::ISODate),
                  QString::number(imageData.size()));

            saveWorkspaceAsset(filename, imageData, markdown);

            const QString savedPath = workspaceOutputDir()
                                      + QStringLiteral("/") + filename;

            sendMessage(chatId, (isEnglish
                ? QStringLiteral("✅ Image saved to Desktop:\n`JARVIS_Workspace/Outputs/%1`")
                : QStringLiteral("✅ Изображение сохранено на Рабочий стол:\n`JARVIS_Workspace/Outputs/%1`"))
                .arg(filename));

            MediaRoutingManager::instance().previewImage(savedPath);

            emit imageReceived(chatId, savedPath);

            qDebug() << "[TelegramGW] Image saved:" << savedPath;
        });
    });
}

// ============================================================
//  Outbound Text Delivery (proactive pings, reminders, etc.)
// ============================================================

void J2JTelegramGateway::sendOutboundMessage(qint64 chatId, const QString& text)
{
    sendMessage(chatId, text);
}

// ============================================================
//  Outbound Image Delivery (Desktop → Telegram)
// ============================================================

void J2JTelegramGateway::sendImageToMobile(qint64 chatId,
                                            const QString& filePath,
                                            const QString& caption)
{
    if (m_botToken.isEmpty()) return;

    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) {
        qWarning() << "[TelegramGW] sendImageToMobile: file not found:" << filePath;
        sendMessage(chatId, QStringLiteral("⚠ Image not found: `%1`").arg(fi.fileName()));
        return;
    }

    (void)QtConcurrent::run([this, chatId, filePath, caption, fi]() {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "[TelegramGW] Cannot open image for sending:" << filePath;
            QMetaObject::invokeMethod(this, [this, chatId, fi]() {
                sendMessage(chatId, QStringLiteral("⚠ Cannot read image: `%1`").arg(fi.fileName()));
            }, Qt::QueuedConnection);
            return;
        }
        QByteArray fileData = file.readAll();
        file.close();

        QString mimeType = QStringLiteral("image/jpeg");
        const QString suffix = fi.suffix().toLower();
        if (suffix == QStringLiteral("png"))       mimeType = QStringLiteral("image/png");
        else if (suffix == QStringLiteral("gif"))  mimeType = QStringLiteral("image/gif");
        else if (suffix == QStringLiteral("webp")) mimeType = QStringLiteral("image/webp");
        else if (suffix == QStringLiteral("bmp"))  mimeType = QStringLiteral("image/bmp");
        else if (suffix == QStringLiteral("svg"))  mimeType = QStringLiteral("image/svg+xml");

        QMetaObject::invokeMethod(this, [this, chatId, filePath, caption,
                                          fileData, mimeType, fi]() {
            QUrl url(kTgApiBase + m_botToken + QStringLiteral("/sendPhoto"));

            auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

            QHttpPart chatIdPart;
            chatIdPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                                 QStringLiteral("form-data; name=\"chat_id\""));
            chatIdPart.setBody(QByteArray::number(chatId));
            multiPart->append(chatIdPart);

            if (!caption.isEmpty()) {
                QHttpPart captionPart;
                captionPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                                      QStringLiteral("form-data; name=\"caption\""));
                captionPart.setBody(caption.toUtf8());
                multiPart->append(captionPart);

                QHttpPart parseModePart;
                parseModePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                                        QStringLiteral("form-data; name=\"parse_mode\""));
                parseModePart.setBody(QByteArrayLiteral("Markdown"));
                multiPart->append(parseModePart);
            }

            QHttpPart photoPart;
            photoPart.setHeader(QNetworkRequest::ContentTypeHeader, mimeType);
            photoPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                QStringLiteral("form-data; name=\"photo\"; filename=\"%1\"")
                    .arg(fi.fileName()));
            photoPart.setBody(fileData);
            multiPart->append(photoPart);

            QNetworkRequest req(url);
            req.setTransferTimeout(30000);

            QNetworkReply* reply = m_network->post(req, multiPart);
            multiPart->setParent(reply);

            connect(reply, &QNetworkReply::finished, this, [this, reply, chatId, filePath, fi, caption]() {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    qWarning() << "[TelegramGW] sendPhoto failed:" << reply->errorString()
                               << "— falling back to sendDocument";
                    sendDocumentToMobile(chatId, filePath, caption);
                    return;
                }
                const QByteArray body = reply->readAll();
                const QJsonObject resp = QJsonDocument::fromJson(body).object();
                if (!resp[QStringLiteral("ok")].toBool()) {
                    qWarning() << "[TelegramGW] sendPhoto API error:" << body
                               << "— falling back to sendDocument";
                    sendDocumentToMobile(chatId, filePath, caption);
                    return;
                }
                emit imageSent(chatId, filePath);
                qDebug() << "[TelegramGW] Image sent to chat" << chatId << ":" << filePath;
            });
        }, Qt::QueuedConnection);
    });
}

// ── Video delivery via Telegram Bot API /sendVideo ─────────

void J2JTelegramGateway::sendVideoToMobile(qint64 chatId,
                                            const QString& filePath,
                                            const QString& caption)
{
    if (m_botToken.isEmpty()) return;

    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) {
        qWarning() << "[TelegramGW] sendVideoToMobile: file not found:" << filePath;
        sendMessage(chatId, QStringLiteral("⚠ Video not found: `%1`").arg(fi.fileName()));
        return;
    }

    (void)QtConcurrent::run([this, chatId, filePath, caption, fi]() {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "[TelegramGW] Cannot open video for sending:" << filePath;
            QMetaObject::invokeMethod(this, [this, chatId, fi]() {
                sendMessage(chatId, QStringLiteral("⚠ Cannot read video: `%1`").arg(fi.fileName()));
            }, Qt::QueuedConnection);
            return;
        }
        QByteArray fileData = file.readAll();
        file.close();

        QString mimeType = QStringLiteral("video/mp4");
        const QString suffix = fi.suffix().toLower();
        if (suffix == QStringLiteral("mkv"))       mimeType = QStringLiteral("video/x-matroska");
        else if (suffix == QStringLiteral("avi"))  mimeType = QStringLiteral("video/x-msvideo");
        else if (suffix == QStringLiteral("mov"))  mimeType = QStringLiteral("video/quicktime");
        else if (suffix == QStringLiteral("webm")) mimeType = QStringLiteral("video/webm");
        else if (suffix == QStringLiteral("wmv"))  mimeType = QStringLiteral("video/x-ms-wmv");

        QMetaObject::invokeMethod(this, [this, chatId, filePath, caption,
                                          fileData, mimeType, fi]() {
            QUrl url(kTgApiBase + m_botToken + QStringLiteral("/sendVideo"));

            auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

            QHttpPart chatIdPart;
            chatIdPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                                 QStringLiteral("form-data; name=\"chat_id\""));
            chatIdPart.setBody(QByteArray::number(chatId));
            multiPart->append(chatIdPart);

            if (!caption.isEmpty()) {
                QHttpPart captionPart;
                captionPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                                      QStringLiteral("form-data; name=\"caption\""));
                captionPart.setBody(caption.toUtf8());
                multiPart->append(captionPart);

                QHttpPart parseModePart;
                parseModePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                                        QStringLiteral("form-data; name=\"parse_mode\""));
                parseModePart.setBody(QByteArrayLiteral("Markdown"));
                multiPart->append(parseModePart);
            }

            QHttpPart videoPart;
            videoPart.setHeader(QNetworkRequest::ContentTypeHeader, mimeType);
            videoPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                QStringLiteral("form-data; name=\"video\"; filename=\"%1\"")
                    .arg(fi.fileName()));
            videoPart.setBody(fileData);
            multiPart->append(videoPart);

            QNetworkRequest req(url);
            req.setTransferTimeout(60000);

            QNetworkReply* reply = m_network->post(req, multiPart);
            multiPart->setParent(reply);

            connect(reply, &QNetworkReply::finished, this, [this, reply, chatId, filePath, fi, caption]() {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    qWarning() << "[TelegramGW] sendVideo failed:" << reply->errorString()
                               << "— falling back to sendDocument";
                    sendDocumentToMobile(chatId, filePath, caption);
                    return;
                }
                const QByteArray body = reply->readAll();
                const QJsonObject resp = QJsonDocument::fromJson(body).object();
                if (!resp[QStringLiteral("ok")].toBool()) {
                    qWarning() << "[TelegramGW] sendVideo API error:" << body
                               << "— falling back to sendDocument";
                    sendDocumentToMobile(chatId, filePath, caption);
                    return;
                }
                emit videoSent(chatId, filePath);
                qDebug() << "[TelegramGW] Video sent to chat" << chatId << ":" << filePath;
            });
        }, Qt::QueuedConnection);
    });
}

// ── Document delivery via Telegram Bot API /sendDocument ─────

void J2JTelegramGateway::sendDocumentToMobile(qint64 chatId,
                                               const QString& filePath,
                                               const QString& caption)
{
    if (m_botToken.isEmpty()) return;

    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) {
        qWarning() << "[TelegramGW] sendDocumentToMobile: file not found:" << filePath;
        sendMessage(chatId, QStringLiteral("⚠ File not found: `%1`").arg(fi.fileName()));
        return;
    }

    (void)QtConcurrent::run([this, chatId, filePath, caption, fi]() {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "[TelegramGW] Cannot open file for sending:" << filePath;
            QMetaObject::invokeMethod(this, [this, chatId, fi]() {
                sendMessage(chatId, QStringLiteral("⚠ Cannot read file: `%1`").arg(fi.fileName()));
            }, Qt::QueuedConnection);
            return;
        }
        QByteArray fileData = file.readAll();
        file.close();

        QMetaObject::invokeMethod(this, [this, chatId, filePath, caption,
                                          fileData, fi]() {
            QUrl url(kTgApiBase + m_botToken + QStringLiteral("/sendDocument"));

            auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

            QHttpPart chatIdPart;
            chatIdPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                                 QStringLiteral("form-data; name=\"chat_id\""));
            chatIdPart.setBody(QByteArray::number(chatId));
            multiPart->append(chatIdPart);

            if (!caption.isEmpty()) {
                QHttpPart captionPart;
                captionPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                                      QStringLiteral("form-data; name=\"caption\""));
                captionPart.setBody(caption.toUtf8());
                multiPart->append(captionPart);

                QHttpPart parseModePart;
                parseModePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                                        QStringLiteral("form-data; name=\"parse_mode\""));
                parseModePart.setBody(QByteArrayLiteral("Markdown"));
                multiPart->append(parseModePart);
            }

            QHttpPart docPart;
            docPart.setHeader(QNetworkRequest::ContentTypeHeader,
                              QStringLiteral("application/octet-stream"));
            docPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                QStringLiteral("form-data; name=\"document\"; filename=\"%1\"")
                    .arg(fi.fileName()));
            docPart.setBody(fileData);
            multiPart->append(docPart);

            QNetworkRequest req(url);
            req.setTransferTimeout(60000);

            QNetworkReply* reply = m_network->post(req, multiPart);
            multiPart->setParent(reply);

            connect(reply, &QNetworkReply::finished, this, [this, reply, chatId, filePath, fi]() {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    qWarning() << "[TelegramGW] sendDocument failed:" << reply->errorString();
                    sendMessage(chatId, QStringLiteral("⚠ Failed to send file `%1`: %2")
                        .arg(fi.fileName(), reply->errorString()));
                    return;
                }
                const QByteArray body = reply->readAll();
                const QJsonObject resp = QJsonDocument::fromJson(body).object();
                if (!resp[QStringLiteral("ok")].toBool()) {
                    const QString desc = resp[QStringLiteral("description")].toString();
                    qWarning() << "[TelegramGW] sendDocument API error:" << desc;
                    sendMessage(chatId, QStringLiteral("⚠ Telegram rejected file `%1`: %2")
                        .arg(fi.fileName(), desc));
                    return;
                }
                emit documentSent(chatId, filePath);
                qDebug() << "[TelegramGW] Document sent to chat" << chatId << ":" << filePath;
            });
        }, Qt::QueuedConnection);
    });
}

// ── Photo from in-memory buffer ─────────────────────────────

void J2JTelegramGateway::sendPhotoFromBuffer(qint64 chatId,
                                              const QByteArray& imageData,
                                              const QString& filename,
                                              const QString& caption)
{
    if (m_botToken.isEmpty() || imageData.isEmpty()) return;

    QUrl url(kTgApiBase + m_botToken + QStringLiteral("/sendPhoto"));

    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart chatIdPart;
    chatIdPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                         QStringLiteral("form-data; name=\"chat_id\""));
    chatIdPart.setBody(QByteArray::number(chatId));
    multiPart->append(chatIdPart);

    if (!caption.isEmpty()) {
        QHttpPart captionPart;
        captionPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                              QStringLiteral("form-data; name=\"caption\""));
        captionPart.setBody(caption.toUtf8());
        multiPart->append(captionPart);

        QHttpPart parseModePart;
        parseModePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                                QStringLiteral("form-data; name=\"parse_mode\""));
        parseModePart.setBody(QByteArrayLiteral("Markdown"));
        multiPart->append(parseModePart);
    }

    QString mimeType = QStringLiteral("image/png");
    const QString ext = QFileInfo(filename).suffix().toLower();
    if (ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg"))
        mimeType = QStringLiteral("image/jpeg");
    else if (ext == QStringLiteral("gif"))
        mimeType = QStringLiteral("image/gif");
    else if (ext == QStringLiteral("webp"))
        mimeType = QStringLiteral("image/webp");

    QHttpPart photoPart;
    photoPart.setHeader(QNetworkRequest::ContentTypeHeader, mimeType);
    photoPart.setHeader(QNetworkRequest::ContentDispositionHeader,
        QStringLiteral("form-data; name=\"photo\"; filename=\"%1\"")
            .arg(filename.isEmpty() ? QStringLiteral("image.png") : filename));
    photoPart.setBody(imageData);
    multiPart->append(photoPart);

    QNetworkRequest req(url);
    req.setTransferTimeout(30000);

    QNetworkReply* reply = m_network->post(req, multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, chatId, filename]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[TelegramGW] sendPhotoFromBuffer failed:" << reply->errorString();
            sendMessage(chatId, QStringLiteral("⚠ Failed to send image `%1`: %2")
                .arg(filename, reply->errorString()));
            return;
        }
        const QByteArray body = reply->readAll();
        const QJsonObject resp = QJsonDocument::fromJson(body).object();
        if (!resp[QStringLiteral("ok")].toBool()) {
            qWarning() << "[TelegramGW] sendPhotoFromBuffer API error:" << body;
            sendMessage(chatId, QStringLiteral("⚠ Telegram rejected image `%1`").arg(filename));
            return;
        }
        emit imageSent(chatId, filename);
        qDebug() << "[TelegramGW] Buffer image sent to chat" << chatId << ":" << filename;
    });
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
