// ============================================================
// telegram_access_manager.cpp — RBAC + Multi-PC Session Middleware
// ============================================================

#include "telegram_access_manager.h"
#include "database_manager.h"

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QUuid>
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
        { QStringLiteral("/uptime"),         TelegramRole::User },
        { QStringLiteral("/note"),           TelegramRole::User },
        { QStringLiteral("/notes"),          TelegramRole::User },
        { QStringLiteral("/screen_analyze"), TelegramRole::User },
        { QStringLiteral("/stop_voice"),     TelegramRole::User },
        { QStringLiteral("/cache_stats"),    TelegramRole::User },
        { QStringLiteral("/fridge"),         TelegramRole::User },
        { QStringLiteral("/summarize"),      TelegramRole::User },
        { QStringLiteral("/remind"),         TelegramRole::User },

        // Session management — Admin only
        { QStringLiteral("/sessions"),       TelegramRole::Admin },
        { QStringLiteral("/bind_pc"),        TelegramRole::Admin },
        { QStringLiteral("/revoke_session"), TelegramRole::Admin },

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

QSet<QString> TelegramAccessManager::knownCommands()
{
    const auto& acl = s_commandAcl;
    QSet<QString> cmds;
    for (auto it = acl.begin(); it != acl.end(); ++it)
        cmds.insert(it.key());
    return cmds;
}

// ============================================================
//  Auth token generator — 32-char hex
// ============================================================

QString TelegramAccessManager::generateAuthToken()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-'));
}

// ============================================================
//  Construction
// ============================================================

TelegramAccessManager::TelegramAccessManager(const QString& localDeviceId,
                                             QObject* parent)
    : QObject(parent)
    , m_localDeviceId(localDeviceId)
{
    ensureTables();
    resolvePrimaryOwner();
    qDebug() << "[AccessManager] Initialized, primaryOwner:" << m_primaryOwnerId
             << "localDevice:" << m_localDeviceId;
}

void TelegramAccessManager::resolvePrimaryOwner()
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(QSqlDatabase::database());
    q.exec(QStringLiteral(
        "SELECT chat_id FROM telegram_users ORDER BY registered_at ASC LIMIT 1"));
    if (q.next())
        m_primaryOwnerId = q.value(0).toLongLong();
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
        "CREATE TABLE IF NOT EXISTS user_sessions ("
        "  chat_id    INTEGER PRIMARY KEY,"
        "  device_id  TEXT NOT NULL,"
        "  auth_token TEXT NOT NULL,"
        "  pc_name    TEXT NOT NULL DEFAULT '',"
        "  status     TEXT NOT NULL DEFAULT 'active',"
        "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
        "  last_used  TEXT NOT NULL DEFAULT (datetime('now')),"
        "  FOREIGN KEY (chat_id) REFERENCES telegram_users(chat_id)"
        ")"));

    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_sessions_device "
        "ON user_sessions(device_id)"));

    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_sessions_token "
        "ON user_sessions(auth_token)"));

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
//  Session Management — Multi-PC binding
// ============================================================

bool TelegramAccessManager::isSessionBoundHere(qint64 chatId) const
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "SELECT device_id FROM user_sessions "
        "WHERE chat_id = :cid AND status = 'active'"));
    q.bindValue(QStringLiteral(":cid"), chatId);

    if (q.exec() && q.next()) {
        const QString boundDevice = q.value(0).toString();
        if (boundDevice == m_localDeviceId)
            return true;

        emit const_cast<TelegramAccessManager*>(this)
            ->wrongPcSession(chatId, boundDevice);
        return false;
    }

    // No session at all — not bound anywhere
    return false;
}

std::optional<TgUserSession> TelegramAccessManager::resolveSession(qint64 chatId) const
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "SELECT chat_id, device_id, auth_token, pc_name, status, "
        "       created_at, last_used "
        "FROM user_sessions WHERE chat_id = :cid"));
    q.bindValue(QStringLiteral(":cid"), chatId);

    if (!q.exec() || !q.next())
        return std::nullopt;

    TgUserSession s;
    s.chatId    = q.value(0).toLongLong();
    s.deviceId  = q.value(1).toString();
    s.authToken = q.value(2).toString();
    s.pcName    = q.value(3).toString();
    s.status    = q.value(4).toString();
    s.createdAt = QDateTime::fromString(q.value(5).toString(), Qt::ISODate);
    s.lastUsed  = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
    return s;
}

TgUserSession TelegramAccessManager::bindSession(qint64 chatId, const QString& pcName)
{
    TgUserSession session;
    session.chatId    = chatId;
    session.deviceId  = m_localDeviceId;
    session.authToken = generateAuthToken();
    session.pcName    = pcName.isEmpty()
                            ? QStringLiteral("PC-%1").arg(m_localDeviceId.left(8))
                            : pcName;
    session.status    = QStringLiteral("active");
    session.createdAt = QDateTime::currentDateTimeUtc();
    session.lastUsed  = session.createdAt;

    {
        QMutexLocker lock(&m_mutex);
        QSqlQuery q(QSqlDatabase::database());
        q.prepare(QStringLiteral(
            "INSERT INTO user_sessions "
            "  (chat_id, device_id, auth_token, pc_name, status) "
            "VALUES (:cid, :did, :tok, :name, 'active') "
            "ON CONFLICT(chat_id) DO UPDATE SET "
            "  device_id  = excluded.device_id, "
            "  auth_token = excluded.auth_token, "
            "  pc_name    = excluded.pc_name, "
            "  status     = 'active', "
            "  last_used  = datetime('now')"));
        q.bindValue(QStringLiteral(":cid"),  chatId);
        q.bindValue(QStringLiteral(":did"),  session.deviceId);
        q.bindValue(QStringLiteral(":tok"),  session.authToken);
        q.bindValue(QStringLiteral(":name"), session.pcName);
        q.exec();
    }

    emit sessionBound(chatId, m_localDeviceId);

    qDebug() << "[AccessManager] Session bound: chat" << chatId
             << "→ device" << m_localDeviceId
             << "pc:" << session.pcName;
    return session;
}

bool TelegramAccessManager::validateToken(qint64 chatId, const QString& token) const
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "SELECT 1 FROM user_sessions "
        "WHERE chat_id = :cid AND auth_token = :tok AND status = 'active'"));
    q.bindValue(QStringLiteral(":cid"), chatId);
    q.bindValue(QStringLiteral(":tok"), token);
    return q.exec() && q.next();
}

QList<TgUserSession> TelegramAccessManager::allSessions() const
{
    QMutexLocker lock(&m_mutex);
    QList<TgUserSession> result;

    QSqlQuery q(QSqlDatabase::database());
    q.exec(QStringLiteral(
        "SELECT chat_id, device_id, auth_token, pc_name, status, "
        "       created_at, last_used "
        "FROM user_sessions ORDER BY last_used DESC"));

    while (q.next()) {
        TgUserSession s;
        s.chatId    = q.value(0).toLongLong();
        s.deviceId  = q.value(1).toString();
        s.authToken = q.value(2).toString();
        s.pcName    = q.value(3).toString();
        s.status    = q.value(4).toString();
        s.createdAt = QDateTime::fromString(q.value(5).toString(), Qt::ISODate);
        s.lastUsed  = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
        result.append(s);
    }
    return result;
}

bool TelegramAccessManager::revokeSession(qint64 chatId)
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "UPDATE user_sessions SET status = 'revoked' WHERE chat_id = :cid"));
    q.bindValue(QStringLiteral(":cid"), chatId);
    if (!q.exec() || q.numRowsAffected() == 0)
        return false;

    lock.unlock();
    emit sessionRevoked(chatId);

    qDebug() << "[AccessManager] Session revoked for chat:" << chatId;
    return true;
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
    return TelegramRole::User;
}

bool TelegramAccessManager::isPrimaryOwner(qint64 chatId) const
{
    if (m_primaryOwnerId != 0)
        return chatId == m_primaryOwnerId;

    QMutexLocker lock(&m_mutex);
    QSqlQuery q(QSqlDatabase::database());
    q.exec(QStringLiteral(
        "SELECT chat_id FROM telegram_users ORDER BY registered_at ASC LIMIT 1"));
    if (q.next()) {
        m_primaryOwnerId = q.value(0).toLongLong();
        return chatId == m_primaryOwnerId;
    }
    return false;
}

qint64 TelegramAccessManager::primaryOwnerChatId() const
{
    if (m_primaryOwnerId != 0)
        return m_primaryOwnerId;

    QMutexLocker lock(&m_mutex);
    QSqlQuery q(QSqlDatabase::database());
    q.exec(QStringLiteral(
        "SELECT chat_id FROM telegram_users ORDER BY registered_at ASC LIMIT 1"));
    if (q.next()) {
        m_primaryOwnerId = q.value(0).toLongLong();
        return m_primaryOwnerId;
    }
    return 0;
}

bool TelegramAccessManager::hasAccess(qint64 chatId, const QString& command) const
{
    if (isPrimaryOwner(chatId))
        return true;

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

        // Force-assign Admin to the primary owner on every registration
        if (m_primaryOwnerId != 0 && chatId == m_primaryOwnerId) {
            QSqlQuery fix(QSqlDatabase::database());
            fix.prepare(QStringLiteral(
                "UPDATE telegram_users SET role = 'Admin' WHERE chat_id = :cid"));
            fix.bindValue(QStringLiteral(":cid"), chatId);
            fix.exec();
        }

        // Auto-bind session to this PC if no active session exists
        lock.unlock();
        auto existing = resolveSession(chatId);
        if (!existing.has_value() || existing->status != QStringLiteral("active")) {
            bindSession(chatId);
        }
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

    if (isFirst)
        m_primaryOwnerId = chatId;

    lock.unlock();

    // Auto-bind first session to this PC
    bindSession(chatId);

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

    // Update session last_used timestamp
    QSqlQuery s(QSqlDatabase::database());
    s.prepare(QStringLiteral(
        "UPDATE user_sessions SET last_used = datetime('now') "
        "WHERE chat_id = :cid AND status = 'active'"));
    s.bindValue(QStringLiteral(":cid"), chatId);
    s.exec();
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
        "       COALESCE(a.cmd_count, 0) AS cmd_count, "
        "       s.device_id, s.pc_name, s.status AS session_status "
        "FROM telegram_users u "
        "LEFT JOIN ("
        "  SELECT chat_id, COUNT(*) AS cmd_count "
        "  FROM activity_log_tg "
        "  GROUP BY chat_id"
        ") a ON u.chat_id = a.chat_id "
        "LEFT JOIN user_sessions s ON u.chat_id = s.chat_id "
        "ORDER BY cmd_count DESC"));

    int totalUsers = 0;
    int totalActions = 0;

    while (q.next()) {
        ++totalUsers;
        const QString name          = q.value(1).toString();
        const QString role          = q.value(2).toString();
        const QString active        = q.value(3).toString();
        int count                   = q.value(4).toInt();
        const QString deviceId      = q.value(5).toString();
        const QString pcName        = q.value(6).toString();
        const QString sessionStatus = q.value(7).toString();
        totalActions += count;

        QString roleIcon;
        if (role == QStringLiteral("Admin"))       roleIcon = QStringLiteral("👑");
        else if (role == QStringLiteral("Tester")) roleIcon = QStringLiteral("🔬");
        else                                       roleIcon = QStringLiteral("👤");

        report += QStringLiteral("%1 *%2* `[%3]`\n").arg(roleIcon, name, role);
        report += QStringLiteral("   Commands: %1\n").arg(count);
        report += QStringLiteral("   Last active: %1\n").arg(active);

        if (!deviceId.isEmpty()) {
            QString pcLabel = pcName.isEmpty() ? deviceId.left(8) : pcName;
            report += QStringLiteral("   🖥 PC: %1 [%2]\n").arg(pcLabel, sessionStatus);
        } else {
            report += QStringLiteral("   🖥 PC: _not bound_\n");
        }
        report += QStringLiteral("\n");
    }

    report += QStringLiteral("━━━━━━━━━━━━━━━━━━━━━━\n");
    report += QStringLiteral("Total: %1 users, %2 actions\n")
        .arg(totalUsers).arg(totalActions);
    report += QStringLiteral("This PC: `%1`\n").arg(m_localDeviceId.left(12));

    return report;
}
