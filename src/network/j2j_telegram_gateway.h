#pragma once
// ============================================================
// j2j_telegram_gateway.h — Multi-Tenant Telegram Bot Gateway
//
// Layers on top of MobilePairingManager to provide a Telegram
// Bot API bridge with per-chat localization. Language is driven
// by the paired device's role:
//   • QA_Tester  → EN (English)
//   • All others → RU (Russian)
//
// Features:
//   1. Role-aware localization dictionary for all payloads
//   2. Interactive QA wizard (bug report flow in EN)
//   3. English Markdown exporter for Jira/GitHub
//   4. Async polling loop via QNetworkAccessManager
//   5. Thread-safe SQLite writes under QMutex
// ============================================================

#include <QObject>
#include <QString>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QMutex>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QImage>
#include <memory>
#include <functional>

// Forward-declared, not included: OrganizePlan lives in src/intelligence
// (file_organizer.h). Pulling that header in here would leak an
// intelligence-module dependency into every consumer of this public
// network header — including modules (media, modules) that don't
// otherwise need it and don't have that include path. See
// TgChatSession::pendingOrganizePlan below (shared_ptr, not by-value).
struct OrganizePlan;

class QNetworkAccessManager;
class QNetworkReply;
class Jarvis;
class TranslationEngine;
class J2JMeshConnector;
class MobilePairingManager;
class TelegramAccessManager;
class CommandDispatcherTg;
class SocialPresenceEngine;
class SemanticIntentManager;
class PcWakeAgent;
class PersonalityEngine;

// ── Localization keys ────────────────────────────────────────
enum class TgStringId {
    Welcome,
    MainMenu,
    BtnSystemTelemetry,
    BtnMyKanban,
    BtnReportBug,
    BtnWakePC,
    BtnHelp,
    BugTitlePrompt,
    BugStepsPrompt,
    BugExpectedPrompt,
    BugActualPrompt,
    BugSeverityPrompt,
    BugSaved,
    BugCancelled,
    KanbanEmpty,
    KanbanHeader,
    TelemetryHeader,
    HelpText,
    UnknownCommand,
    SessionExpired,
    PairSuccess,
};

// ── QA Bug Report wizard state machine ───────────────────────
enum class QaWizardStep {
    Idle,
    AwaitingTitle,
    AwaitingSteps,
    AwaitingExpected,
    AwaitingActual,
    AwaitingSeverity,
};

struct QaBugReport {
    qint64    chatId       = 0;
    QString   title;
    QString   stepsToReproduce;
    QString   expectedResult;
    QString   actualResult;
    QString   severity;       // Blocker | Critical | Major | Minor
    QString   reporterName;
    QString   reporterRole;
    QDateTime createdAt;
};

struct TgChatSession {
    qint64        chatId    = 0;
    QString       deviceId;         // FK → paired_devices.device_id
    QString       boundRole;        // cached from paired_devices
    bool          isEnglish = false; // derived from role
    QaWizardStep  wizardStep = QaWizardStep::Idle;
    QDateTime     wizardTouchedAt;   // последний шаг визарда — см. handleMessage
    QaBugReport   pendingBug;
    bool          awaitingLlm = false; // true while LLM is generating
    bool          awaitingCompanionAnswer = false; // waiting for ok/no re: security

    // /browse filesystem navigation state (Admin-only) — browseEntries
    // holds the absolute paths currently on screen so callback_data can
    // reference them by short index instead of embedding full paths
    // (Telegram callback_data is capped at 64 bytes).
    QString       browsePath;      // empty = showing the drive list
    QStringList   browseEntries;

    // /history session list state — same short-index trick as browseEntries,
    // since callback_data can't hold a full session_id list.
    QStringList   historySessionIds;

    // /organize — plan awaiting confirmation (Apply/Cancel buttons).
    // shared_ptr (not by-value) so this header only needs a forward
    // declaration of OrganizePlan.
    std::shared_ptr<OrganizePlan> pendingOrganizePlan;
    bool          hasPendingOrganizePlan = false;

    // Сообщение, по которому SemanticIntentManager предложил действие со
    // средней уверенностью и ждёт «Да/Нет». Держим сам ТЕКСТ, а не разбор:
    // по «Нет» его надо не выбросить, а отправить в LLM как обычную реплику
    // (раньше вопрос «Похоже, ты хочешь X. Запустить?» был тупиком —
    // подтверждать было нечем, и сообщение терялось).
    QString       pendingIntentText;
};

// ── Gateway class ────────────────────────────────────────────

class J2JTelegramGateway : public QObject
{
    Q_OBJECT

public:
    explicit J2JTelegramGateway(QObject* parent = nullptr);
    ~J2JTelegramGateway() override;

    void start();
    void stop();
    bool isRunning() const { return m_running; }

    // Bot token is read from DB settings key "telegram_bot_token"
    // or auto-provisioned via the shared gateway — no manual setup.
    void setBotToken(const QString& token);
    QString botToken() const { return m_botToken; }

    // Jarvis core binding (for free-dialogue routing)
    void setJarvisCore(Jarvis* jarvis);
    Jarvis* jarvisCore() const { return m_jarvis; }
    void setTranslationEngine(TranslationEngine* te) { m_translator = te; }
    void setPairingManager(MobilePairingManager* pm) { m_pairing = pm; }
    void setMeshConnector(J2JMeshConnector* mesh) { m_mesh = mesh; }

    // Входящие меш-пакеты Telegram-маршрутизации (вызывает J2JMeshConnector):
    // onMeshRelay   — сообщение/привязка, пересланная с другого ПК;
    // onMeshBinding — оповещение "чат N привязан к устройству X".
    void onMeshRelay(const QJsonObject& data);
    void onMeshBinding(const QJsonObject& data);
    TelegramAccessManager* accessManager() const { return m_accessMgr; }
    PcWakeAgent*           wakeAgent()     const { return m_wakeAgent; }

    // Social Presence — proactive nudge engine
    SocialPresenceEngine* socialPresence() const { return m_socialPresence; }
    void initSocialPresence(qint64 targetChatId);

    // Localization dictionary
    static QString localized(TgStringId id, bool english);

    // Markdown export
    static QString bugReportToMarkdown(const QaBugReport& bug);

    // Desktop workspace for media assets
    static QString workspaceOutputDir();

    // Outbound message delivery (proactive pings, reminders, curiosity prompts)
    void sendOutboundMessage(qint64 chatId, const QString& text);

    // Send message with inline keyboard buttons
    void sendOutboundWithButtons(qint64 chatId, const QString& text,
                                 const QJsonObject& replyMarkup);

    // Send a CuriosityEngine proactive question with Да/Нет (Yes/No)
    // inline buttons attached — used so the reply can be recognized as
    // an answer via a callback as well as free text.
    void sendProactiveQuestion(qint64 chatId, const QString& text, bool english);

    // Mark a chat as waiting for a companion answer (да/нет/ок)
    void markAwaitingCompanionAnswer(qint64 chatId);

    // Forward PC desktop chat to the owner's Telegram (bidirectional sync)
    void forwardDesktopUserMessage(const QString& text);
    void forwardDesktopAiResponse(const QString& text);

    // Outbound media delivery to mobile
    void sendImageToMobile(qint64 chatId, const QString& filePath,
                           const QString& caption = QString());
    void sendVideoToMobile(qint64 chatId, const QString& filePath,
                           const QString& caption = QString());
    void sendDocumentToMobile(qint64 chatId, const QString& filePath,
                              const QString& caption = QString());

    // Send photo from an in-memory buffer (e.g. rendered schematic)
    void sendPhotoFromBuffer(qint64 chatId, const QByteArray& imageData,
                             const QString& filename,
                             const QString& caption = QString());

    static constexpr int POLL_INTERVAL_MS    = 2000;
    static constexpr int TYPING_INTERVAL_MS  = 3000;
    // Столько молчания — и незаконченный баг-репорт считается брошенным:
    // иначе он молча съедает весь обычный текст (см. handleMessage).
    static constexpr int WIZARD_IDLE_MINUTES = 20;

signals:
    void messageReceived(qint64 chatId, const QString& text);
    void bugReportFiled(const QaBugReport& report);
    void conversationResponse(qint64 chatId, const QString& response);
    void voiceProcessed(qint64 chatId, const QString& transcript);
    void imageReceived(qint64 chatId, const QString& savedPath);
    void imageSent(qint64 chatId, const QString& filePath);
    void videoSent(qint64 chatId, const QString& filePath);
    void documentSent(qint64 chatId, const QString& filePath);
    void pairingCompleted(qint64 chatId, const QString& role);
    void diagramGenerated(const QImage& image);
    void gatewayStarted();
    void gatewayStopped();
    void gatewayError(const QString& message);

private slots:
    void onPollUpdates();
    void onTypingTimer();

private:
    void processUpdate(const QJsonObject& update);
    // fromRelay=true — сообщение пришло по мешу с другого ПК-поллера;
    // в этом случае оно выполняется здесь безусловно (без повторной
    // пересылки), т.к. отправитель уже определил владельца.
    // replyToMessageId: the incoming message's reply_to_message.message_id
    // (0 if the user didn't use Telegram's "Reply" on a specific message) —
    // lets a delayed reply to a proactive question be recognized no matter
    // how much time passed. See CuriosityEngine::consumeAnswer.
    void handleMessage(qint64 chatId, const QString& text,
                       const QString& firstName, bool fromRelay = false,
                       qint64 replyToMessageId = 0);

    // Multi-PC: клавиатура выбора "какой ПК ваш" + привязка
    void sendPcSelectionKeyboard(qint64 chatId, bool english);
    void bindChatToLocalPc(qint64 chatId, bool announce = true);
    void handleCallbackQuery(const QString& callbackId,
                             qint64 chatId, const QString& data);
    void handleVoiceMessage(qint64 chatId, const QJsonObject& voice,
                            bool isEnglish);

    // Pairing PIN handler — returns true if text was consumed as a PIN
    bool handlePairing(qint64 chatId, const QString& text,
                       const QString& firstName);

    // Free-dialogue LLM routing
    void routeToLlm(qint64 chatId, const QString& text, bool english,
                    qint64 replyToMessageId = 0);
    void deliverLlmResponse(qint64 chatId, const QString& response);
    void finishLlmRequest(qint64 chatId);
    void sendChatAction(qint64 chatId, const QString& action);
    void startTypingIndicator(qint64 chatId);
    void stopTypingIndicator(qint64 chatId);

    // Voice file download + transcription pipeline
    void downloadTelegramFile(const QString& fileId, qint64 chatId,
                              bool isEnglish);

    // Image handling
    void handlePhotoMessage(qint64 chatId, const QJsonArray& photos,
                            const QString& caption, bool isEnglish);
    void downloadAndSaveImage(const QString& fileId, qint64 chatId,
                              bool isEnglish);
    void saveWorkspaceAsset(const QString& filename, const QByteArray& data,
                            const QString& companionMarkdown = QString());

    // Chat session management
    TgChatSession& getOrCreateSession(qint64 chatId);
    bool           resolveSessionLocale(TgChatSession& session);

    // Telegram Bot API helpers
    void sendMessage(qint64 chatId, const QString& text,
                     const QJsonObject& replyMarkup = QJsonObject());
    // Actual HTTP call behind sendMessage(). allowMarkdown=true tries
    // parse_mode=Markdown first; on a Telegram-side parse failure (unescaped
    // _/*/`/[ in LLM output routinely trips this) it retries itself once
    // with allowMarkdown=false (plain text, always accepted) instead of
    // silently dropping the reply.
    // onSent (optional) is invoked with the Telegram-assigned message_id on
    // successful delivery — used by sendProactiveQuestion so a later
    // reply_to_message can be matched back to this specific message,
    // regardless of how much time has passed.
    void sendMessageRaw(qint64 chatId, const QString& text,
                        const QJsonObject& replyMarkup, bool allowMarkdown,
                        std::function<void(qint64 messageId)> onSent);
    void sendMainMenu(qint64 chatId, bool english);
    void answerCallbackQuery(const QString& callbackId,
                             const QString& text = QString());

    // QA wizard flow
    void startBugWizard(TgChatSession& session);
    void advanceBugWizard(TgChatSession& session, const QString& input);
    void showSeverityKeyboard(qint64 chatId, bool english);
    void finalizeBugReport(TgChatSession& session);
    void persistBugReport(const QaBugReport& report);

    // Persistent reply keyboard (shown above the input field)
    void sendWithPersistentKeyboard(qint64 chatId, const QString& text,
                                     bool english);
    static QJsonObject buildPersistentKeyboard(bool english);

    // Persistent button text → handler dispatch
    bool handlePersistentButton(qint64 chatId, const QString& text,
                                bool english);

    // Sub-menu for Settings
    void sendSettingsSubMenu(qint64 chatId, bool english);

    // /browse — full filesystem navigation (Admin-only, this-PC-only).
    // path.isEmpty() shows the drive list.
    void sendFsListing(qint64 chatId, const QString& path, bool english);

    // /history — chronological chat history browser (complements keyword
    // search: no need to remember what Jarvis "remembered").
    void sendHistorySessions(qint64 chatId, bool english);
    void sendHistoryTranscript(qint64 chatId, const QString& sessionId, bool english);

    // Inline context buttons (e.g., Yes/No confirmations)
    static QJsonObject buildConfirmButtons(const QString& yesData,
                                            const QString& noData,
                                            bool english);

    // Inline keyboard builders
    static QJsonObject buildInlineKeyboard(const QJsonArray& rows);
    static QJsonArray  buildMainMenuButtons(bool english);
    static QJsonArray  buildSeverityButtons();

    QNetworkAccessManager* m_network    = nullptr;
    QTimer*                m_pollTimer  = nullptr;
    QTimer*                m_typingTimer = nullptr;
    QString                m_botToken;
    qint64                 m_lastUpdateId = 0;
    bool                   m_running    = false;

    Jarvis*                m_jarvis         = nullptr;
    TranslationEngine*     m_translator     = nullptr;
    J2JMeshConnector*      m_mesh           = nullptr;
    MobilePairingManager*  m_pairing        = nullptr;
    TelegramAccessManager* m_accessMgr      = nullptr;
    CommandDispatcherTg*   m_dispatcher     = nullptr;
    SocialPresenceEngine*  m_socialPresence = nullptr;
    SemanticIntentManager* m_intentMgr      = nullptr;
    PcWakeAgent*           m_wakeAgent      = nullptr;
    PersonalityEngine*     m_personalityEng = nullptr;

    QMap<qint64, TgChatSession> m_sessions;
    QSet<qint64>                m_typingChats;  // chats with active typing indicator

    mutable QMutex m_mutex;
};
