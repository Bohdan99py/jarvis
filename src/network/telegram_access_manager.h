#pragma once
// ============================================================
// telegram_access_manager.h — RBAC + Multi-PC Session Middleware
//
// Provides role-based access control for Telegram bot commands,
// AND binds each telegram chat_id to a specific PC via a unique
// session token stored in the user_sessions table.
//
// When a user writes to the bot, the manager first resolves
// which PC owns that session, then checks RBAC.
//
// Tables managed:
//   telegram_users    — roster with roles
//   user_sessions     — chat_id ↔ device_id ↔ auth_token binding
//   activity_log_tg   — per-user activity log
//
// Thread safety: all DB access serialized via QMutex.
// ============================================================

#include <QObject>
#include <QString>
#include <QMap>
#include <QSet>
#include <QMutex>
#include <QList>
#include <QDateTime>
#include <optional>

// ── Access roles (ordered by privilege) ──────────────────────
//
//   User      — free chat, /menu, /help only
//   Tester    — + /bug, /kanban (no system access)
//   Developer — + /sysinfo, /disk, /screenshot, /tasks, /screen_analyze
//                (NO admin controls, NO user analytics)
//   Admin     — full access: /admin_*, user analytics, system control
//
enum class TelegramRole {
    User      = 0,
    Tester    = 1,
    Developer = 2,
    Admin     = 3,
};

inline QString telegramRoleToString(TelegramRole r)
{
    switch (r) {
    case TelegramRole::Admin:     return QStringLiteral("Admin");
    case TelegramRole::Developer: return QStringLiteral("Developer");
    case TelegramRole::Tester:    return QStringLiteral("Tester");
    case TelegramRole::User:      return QStringLiteral("User");
    }
    return QStringLiteral("User");
}

inline TelegramRole telegramRoleFromString(const QString& s)
{
    if (s == QStringLiteral("Admin"))     return TelegramRole::Admin;
    if (s == QStringLiteral("Developer")) return TelegramRole::Developer;
    if (s == QStringLiteral("Tester")) return TelegramRole::Tester;
    return TelegramRole::User;
}

// ── Activity log entry ───────────────────────────────────────
struct TgActivityEntry {
    qint64    chatId      = 0;
    QString   actionType;    // "command", "message", "voice", "photo", "pairing"
    QString   detail;        // the command or text summary
    QDateTime timestamp;
};

// ── User session — links a Telegram chat to a specific PC ────
struct TgUserSession {
    qint64    chatId      = 0;
    QString   deviceId;       // unique PC identifier (machine-id or UUID)
    QString   authToken;      // random token for authenticating this binding
    QString   pcName;         // human-readable PC label
    QString   status;         // "active" | "expired" | "revoked"
    QDateTime createdAt;
    QDateTime lastUsed;
};

// ── Access Manager class ─────────────────────────────────────

class TelegramAccessManager : public QObject
{
    Q_OBJECT

public:
    explicit TelegramAccessManager(const QString& localDeviceId,
                                   QObject* parent = nullptr);

    // ── Session resolution (called before any command) ────────
    // Returns true if chatId is bound to THIS PC's deviceId.
    bool isSessionBoundHere(qint64 chatId) const;

    // Full session info for a chat, or nullopt if none
    std::optional<TgUserSession> resolveSession(qint64 chatId) const;

    // Bind a Telegram chat to THIS local PC. Generates auth token.
    // If a session already exists for another PC, replaces it.
    TgUserSession bindSession(qint64 chatId, const QString& pcName = QString());

    // Validate an auth token for a given chatId
    bool validateToken(qint64 chatId, const QString& token) const;

    // List all sessions (admin view)
    QList<TgUserSession> allSessions() const;

    // Revoke a session (admin action)
    bool revokeSession(qint64 chatId);

    // Auto-bind on first message — no PIN needed.
    // Calls ensureRegistered + bindSession in one step.
    void autoBindSession(qint64 chatId, const QString& displayName);

    // The device ID of THIS local PC instance
    QString localDeviceId() const { return m_localDeviceId; }

    // ── Core RBAC check — call before executing any command ───
    bool hasAccess(qint64 chatId, const QString& command) const;

    // ── Role management ──────────────────────────────────────
    TelegramRole getRole(qint64 chatId) const;
    bool         setRole(qint64 chatId, TelegramRole role);
    bool         isAdmin(qint64 chatId) const;

    // Absolute admin override — primary owner always has full access
    bool isPrimaryOwner(qint64 chatId) const;
    qint64 primaryOwnerChatId() const;

    // First user to interact becomes Admin automatically
    void ensureRegistered(qint64 chatId, const QString& displayName);

    // ── Activity tracking ────────────────────────────────────
    void logActivity(qint64 chatId, const QString& actionType,
                     const QString& detail = QString());

    // ── Admin stats report ───────────────────────────────────
    QString buildStatsReport() const;

    // ── Command → minimum role mapping ───────────────────────
    static TelegramRole minimumRoleFor(const QString& command);

    // All registered command names (for layout fixer lookups)
    static QSet<QString> knownCommands();

signals:
    void accessDenied(qint64 chatId, const QString& command);
    void userRegistered(qint64 chatId, TelegramRole role);
    void sessionBound(qint64 chatId, const QString& deviceId);
    void sessionRevoked(qint64 chatId);
    void wrongPcSession(qint64 chatId, const QString& boundDeviceId);

private:
    void ensureTables();
    void resolvePrimaryOwner();
    static QMap<QString, TelegramRole> buildCommandAcl();
    static QString generateAuthToken();

    QString        m_localDeviceId;
    mutable QMutex m_mutex;
    mutable qint64 m_primaryOwnerId = 0;
    static const QMap<QString, TelegramRole> s_commandAcl;
};
