// ============================================================
// telegram_access_manager.cpp — RBAC Middleware for Telegram
// ============================================================

#include "telegram_access_manager.h"
#include "database_manager.h"

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QDebug>

// ============================================================
//  Command ACL — which role can run which command
// ============================================================

QMap<QString, TelegramRole> TelegramAccessManager::buildCommandAcl()
{
    return {
        // User-level (everyone)
        { QStringLiteral("/start"),      TelegramRole::User },
        { QStringLiteral("/menu"),       TelegramRole::User },
        { QStringLiteral("/help"),       TelegramRole::User },
        { QStringLiteral("/cancel"),     TelegramRole::User },

        // User-level extras
        { QStringLiteral("/uptime"),     TelegramRole::User },
        { QStringLiteral("/note"),       TelegramRole::User },
        { QStringLiteral("/notes"),      TelegramRole::User },

        // Tester-level
        { QStringLiteral("/bug"),        TelegramRole::Tester },
        { QStringLiteral("/kanban"),     TelegramRole::Tester },
        { QStringLiteral("/telemetry"),  TelegramRole::Tester },
        { QStringLiteral("/tasks"),      TelegramRole::Tester },

        // Admin-level
        { QStringLiteral("/admin_stats"),  TelegramRole::Admin },
        { QStringLiteral("/admin_grant"),  TelegramRole::Admin },
        { QStringLiteral("/admin_revoke"), TelegramRole::Admin },
        { QStringLiteral("/admin_users"),  TelegramRole::Admin },
        { QStringLiteral("/screenshot"),   TelegramRole::Admin },
        { QStringLiteral("/sysinfo"),      TelegramRole::Admin },
        { QStringLiteral("/disk"),         TelegramRole::Admin },
    };
}

const QMap<QString, TelegramRole> TelegramAccessManager::s_commandAcl =
    TelegramAccessManager::buildCommandAcl();

// ============================================================
//  Construction
// ============================================================

TelegramAccessManager::TelegramAccessManager(QObject* parent)
    : QObject(parent)
{
    ensureTables();
    qDebug() << "[AccessManager] Initialized";
}

void TelegramAccessManager::ensureTables()
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(QSqlDatabase::database());

    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS telegram_users ("
        "  chat_id       INTEGER PRIMARY KEY,"
        "  display_name  TEXT NOT NULL DEFAULT '',"
        "  role          TEXT NOT NULL DEFAULT 'User',"
        "  registered_at TEXT NOT NULL DEFAULT (datetime('now')),"
        "  last_active   TEXT NOT NULL DEFAULT (datetime('now'))"
        ")"));

    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS activity_log_tg ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  chat_id     INTEGER NOT NULL,"
        "  action_type TEXT NOT NULL,"
        "  detail      TEXT DEFAULT '',"
        "  created_at  TEXT NOT NULL DEFAULT (datetime('now'))"
        ")"));

    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_activity_tg_chat "
        "ON activity_log_tg(chat_id, created_at)"));
}

// ============================================================
//  RBAC Core
// ============================================================

TelegramRole TelegramAccessManager::minimumRoleFor(const QString& command)
{
    const QString cmd = command.toLower().section(QLatin1Char(' '), 0, 0);
    auto it = s_commandAcl.find(cmd);
    if (it != s_commandAcl.end())
        return it.value();
    // Unknown commands default to User level (free chat goes through)
    return TelegramRole::User;
}

bool TelegramAccessManager::hasAccess(qint64 chatId, const QString& command) const
{
    TelegramRole userRole = getRole(chatId);
    TelegramRole required = minimumRoleFor(command);

    bool allowed = static_cast<int>(userRole) >= static_cast<int>(required);

    if (!allowed) {
        qDebug() << "[AccessManager] DENIED:" << chatId
                 << "tried" << command
                 << "(has:" << telegramRoleToString(userRole)
                 << ", needs:" << telegramRoleToString(required) << ")";
        emit const_cast<TelegramAccessManager*>(this)->accessDenied(chatId, command);
    }

    return allowed;
}

TelegramRole TelegramAccessManager::getRole(qint64 chatId) const
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "SELECT role FROM telegram_users WHERE chat_id = :cid"));
    q.bindValue(QStringLiteral(":cid"), chatId);

    if (q.exec() && q.next())
        return telegramRoleFromString(q.value(0).toString());

    return TelegramRole::User;
}

bool TelegramAccessManager::setRole(qint64 chatId, TelegramRole role)
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "UPDATE telegram_users SET role = :role WHERE chat_id = :cid"));
    q.bindValue(QStringLiteral(":role"), telegramRoleToString(role));
    q.bindValue(QStringLiteral(":cid"),  chatId);
    return q.exec() && q.numRowsAffected() > 0;
}

bool TelegramAccessManager::isAdmin(qint64 chatId) const
{
    return getRole(chatId) == TelegramRole::Admin;
}

void TelegramAccessManager::ensureRegistered(qint64 chatId,
                                              const QString& displayName)
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(QSqlDatabase::database());

    // Check if already registered
    q.prepare(QStringLiteral(
        "SELECT chat_id FROM telegram_users WHERE chat_id = :cid"));
    q.bindValue(QStringLiteral(":cid"), chatId);
    if (q.exec() && q.next()) {
        // Update last_active
        QSqlQuery u(QSqlDatabase::database());
        u.prepare(QStringLiteral(
            "UPDATE telegram_users SET last_active = datetime('now'), "
            "display_name = :name WHERE chat_id = :cid"));
        u.bindValue(QStringLiteral(":name"), displayName);
        u.bindValue(QStringLiteral(":cid"),  chatId);
        u.exec();
        return;
    }

    // First user ever → Admin; subsequent → User
    QSqlQuery countQ(QSqlDatabase::database());
    countQ.exec(QStringLiteral("SELECT COUNT(*) FROM telegram_users"));
    bool isFirst = (!countQ.next() || countQ.value(0).toInt() == 0);

    TelegramRole role = isFirst ? TelegramRole::Admin : TelegramRole::User;

    QSqlQuery ins(QSqlDatabase::database());
    ins.prepare(QStringLiteral(
        "INSERT INTO telegram_users (chat_id, display_name, role) "
        "VALUES (:cid, :name, :role)"));
    ins.bindValue(QStringLiteral(":cid"),  chatId);
    ins.bindValue(QStringLiteral(":name"), displayName);
    ins.bindValue(QStringLiteral(":role"), telegramRoleToString(role));
    ins.exec();

    lock.unlock();
    emit userRegistered(chatId, role);

    qDebug() << "[AccessManager] Registered:" << displayName
             << "chat_id:" << chatId
             << "role:" << telegramRoleToString(role);
}

// ============================================================
//  Activity Tracking
// ============================================================

void TelegramAccessManager::logActivity(qint64 chatId,
                                         const QString& actionType,
                                         const QString& detail)
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "INSERT INTO activity_log_tg (chat_id, action_type, detail) "
        "VALUES (:cid, :type, :detail)"));
    q.bindValue(QStringLiteral(":cid"),    chatId);
    q.bindValue(QStringLiteral(":type"),   actionType);
    q.bindValue(QStringLiteral(":detail"), detail.left(200));
    q.exec();

    // Update last_active
    QSqlQuery u(QSqlDatabase::database());
    u.prepare(QStringLiteral(
        "UPDATE telegram_users SET last_active = datetime('now') "
        "WHERE chat_id = :cid"));
    u.bindValue(QStringLiteral(":cid"), chatId);
    u.exec();
}

// ============================================================
//  Admin Stats Report
// ============================================================

QString TelegramAccessManager::buildStatsReport() const
{
    QMutexLocker lock(&m_mutex);

    QString report;
    report += QStringLiteral("📊 *Admin Stats Report*\n");
    report += QStringLiteral("━━━━━━━━━━━━━━━━━━━━━━\n\n");

    QSqlQuery q(QSqlDatabase::database());
    q.exec(QStringLiteral(
        "SELECT u.chat_id, u.display_name, u.role, u.last_active, "
        "       COALESCE(a.cmd_count, 0) AS cmd_count "
        "FROM telegram_users u "
        "LEFT JOIN ("
        "  SELECT chat_id, COUNT(*) AS cmd_count "
        "  FROM activity_log_tg "
        "  GROUP BY chat_id"
        ") a ON u.chat_id = a.chat_id "
        "ORDER BY cmd_count DESC"));

    int totalUsers = 0;
    int totalActions = 0;

    while (q.next()) {
        ++totalUsers;
        const QString name   = q.value(1).toString();
        const QString role   = q.value(2).toString();
        const QString active = q.value(3).toString();
        int count            = q.value(4).toInt();
        totalActions += count;

        QString roleIcon;
        if (role == QStringLiteral("Admin"))       roleIcon = QStringLiteral("👑");
        else if (role == QStringLiteral("Tester")) roleIcon = QStringLiteral("🔬");
        else                                       roleIcon = QStringLiteral("👤");

        report += QStringLiteral("%1 *%2* `[%3]`\n").arg(roleIcon, name, role);
        report += QStringLiteral("   Commands: %1\n").arg(count);
        report += QStringLiteral("   Last active: %1\n\n").arg(active);
    }

    report += QStringLiteral("━━━━━━━━━━━━━━━━━━━━━━\n");
    report += QStringLiteral("Total: %1 users, %2 actions\n")
        .arg(totalUsers).arg(totalActions);

    return report;
}
