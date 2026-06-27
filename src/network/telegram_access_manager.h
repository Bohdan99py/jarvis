#pragma once
// ============================================================
// telegram_access_manager.h — RBAC Middleware for Telegram
//
// Provides role-based access control for Telegram bot commands.
// Every incoming command passes through hasAccess() before
// execution. Roles: Admin, Tester, User.
//
// Also tracks all Telegram activity in the activity_log_tg
// table for /admin_stats aggregation.
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

// ── Access roles (ordered by privilege) ──────────────────────
enum class TelegramRole {
    User   = 0,   // basic: /menu, /help, free chat
    Tester = 1,   // + /bug, /kanban, /telemetry
    Admin  = 2,   // + /admin_stats, /admin_grant, /admin_revoke
};

inline QString telegramRoleToString(TelegramRole r)
{
    switch (r) {
    case TelegramRole::Admin:  return QStringLiteral("Admin");
    case TelegramRole::Tester: return QStringLiteral("Tester");
    case TelegramRole::User:   return QStringLiteral("User");
    }
    return QStringLiteral("User");
}

inline TelegramRole telegramRoleFromString(const QString& s)
{
    if (s == QStringLiteral("Admin"))  return TelegramRole::Admin;
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

// ── Access Manager class ─────────────────────────────────────

class TelegramAccessManager : public QObject
{
    Q_OBJECT

public:
    explicit TelegramAccessManager(QObject* parent = nullptr);

    // Core RBAC check — call before executing any command
    bool hasAccess(qint64 chatId, const QString& command) const;

    // Role management
    TelegramRole getRole(qint64 chatId) const;
    bool         setRole(qint64 chatId, TelegramRole role);
    bool         isAdmin(qint64 chatId) const;

    // Absolute admin override — primary owner always has full access
    bool isPrimaryOwner(qint64 chatId) const;

    // First user to interact becomes Admin automatically
    void ensureRegistered(qint64 chatId, const QString& displayName);

    // Activity tracking
    void logActivity(qint64 chatId, const QString& actionType,
                     const QString& detail = QString());

    // Admin stats report
    QString buildStatsReport() const;

    // Command → minimum role mapping
    static TelegramRole minimumRoleFor(const QString& command);

    // All registered command names (for layout fixer lookups)
    static QSet<QString> knownCommands();

signals:
    void accessDenied(qint64 chatId, const QString& command);
    void userRegistered(qint64 chatId, TelegramRole role);

private:
    void ensureTables();
    void resolvePrimaryOwner();
    static QMap<QString, TelegramRole> buildCommandAcl();

    mutable QMutex m_mutex;
    mutable qint64 m_primaryOwnerId = 0;
    static const QMap<QString, TelegramRole> s_commandAcl;
};
