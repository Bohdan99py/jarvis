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
#include <QTimer>
#include <QMutex>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

class QNetworkAccessManager;
class QNetworkReply;

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
    QaBugReport   pendingBug;
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

    // Localization dictionary
    static QString localized(TgStringId id, bool english);

    // Markdown export
    static QString bugReportToMarkdown(const QaBugReport& bug);

    static constexpr int POLL_INTERVAL_MS = 2000;

signals:
    void messageReceived(qint64 chatId, const QString& text);
    void bugReportFiled(const QaBugReport& report);
    void gatewayStarted();
    void gatewayStopped();
    void gatewayError(const QString& message);

private slots:
    void onPollUpdates();

private:
    void processUpdate(const QJsonObject& update);
    void handleMessage(qint64 chatId, const QString& text,
                       const QString& firstName);
    void handleCallbackQuery(const QString& callbackId,
                             qint64 chatId, const QString& data);

    // Chat session management
    TgChatSession& getOrCreateSession(qint64 chatId);
    bool           resolveSessionLocale(TgChatSession& session);

    // Telegram Bot API helpers
    void sendMessage(qint64 chatId, const QString& text,
                     const QJsonObject& replyMarkup = QJsonObject());
    void sendMainMenu(qint64 chatId, bool english);
    void answerCallbackQuery(const QString& callbackId,
                             const QString& text = QString());

    // QA wizard flow
    void startBugWizard(TgChatSession& session);
    void advanceBugWizard(TgChatSession& session, const QString& input);
    void showSeverityKeyboard(qint64 chatId, bool english);
    void finalizeBugReport(TgChatSession& session);
    void persistBugReport(const QaBugReport& report);

    // Inline keyboard builders
    static QJsonObject buildInlineKeyboard(const QJsonArray& rows);
    static QJsonArray  buildMainMenuButtons(bool english);
    static QJsonArray  buildSeverityButtons();

    QNetworkAccessManager* m_network    = nullptr;
    QTimer*                m_pollTimer  = nullptr;
    QString                m_botToken;
    qint64                 m_lastUpdateId = 0;
    bool                   m_running    = false;

    QMap<qint64, TgChatSession> m_sessions;

    mutable QMutex m_mutex;
};
