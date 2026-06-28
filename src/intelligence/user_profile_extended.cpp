// ============================================================
// user_profile_extended.cpp — Multi-User Identity & Preferences
// ============================================================

#include "user_profile_extended.h"
#include "database_manager.h"

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSysInfo>
#include <QCryptographicHash>
#include <QMutexLocker>
#include <QJsonArray>
#include <QDebug>

// ============================================================
//  Singleton
// ============================================================

UserProfileExtended& UserProfileExtended::instance()
{
    static UserProfileExtended inst;
    return inst;
}

UserProfileExtended::UserProfileExtended(QObject* parent)
    : QObject(parent)
    , m_userId(generateUserId())
{
}

UserProfileExtended::~UserProfileExtended() = default;

// ============================================================
//  User ID — stable crypto hash of machine + OS user
// ============================================================

QString UserProfileExtended::generateUserId()
{
    const QString raw = QSysInfo::machineHostName()
                        + QStringLiteral("|")
                        + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER", QStringLiteral("default")));

    return QString::fromLatin1(
        QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha256)
            .toHex().left(16));
}

void UserProfileExtended::setCurrentUserId(const QString& id)
{
    QMutexLocker lock(&m_mutex);
    m_userId = id;
}

// ============================================================
//  Required fields — used for calibration detection
// ============================================================

const QStringList& UserProfileExtended::requiredFields()
{
    static const QStringList fields = {
        QStringLiteral("nickname"),
        QStringLiteral("active_hours_start"),
        QStringLiteral("active_hours_end"),
        QStringLiteral("dev_style"),
        QStringLiteral("ui_accent_color"),
        QStringLiteral("mesh_role"),
    };
    return fields;
}

// ============================================================
//  Database table
// ============================================================

void UserProfileExtended::ensureTable()
{
    if (!DatabaseManager::instance().isOpen()) return;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS user_profiles ("
        "  user_id      TEXT NOT NULL,"
        "  key          TEXT NOT NULL,"
        "  value        TEXT DEFAULT '',"
        "  confidence   REAL NOT NULL DEFAULT 1.0,"
        "  source       TEXT NOT NULL DEFAULT 'user',"
        "  updated_at   TEXT NOT NULL DEFAULT (datetime('now')),"
        "  PRIMARY KEY (user_id, key)"
        ")"));

    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_uprof_user "
        "ON user_profiles(user_id)"));
}

// ============================================================
//  Thread-safe field UPSERT
// ============================================================

void UserProfileExtended::updateProfileField(const QString& userId,
                                               const QString& key,
                                               const QVariant& value,
                                               double confidence,
                                               const QString& source)
{
    QMutexLocker lock(&m_mutex);

    if (!DatabaseManager::instance().isOpen()) return;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO user_profiles (user_id, key, value, confidence, source, updated_at) "
        "VALUES (:uid, :key, :val, :conf, :src, datetime('now')) "
        "ON CONFLICT(user_id, key) DO UPDATE SET "
        "  value = :val2, confidence = :conf2, source = :src2, "
        "  updated_at = datetime('now')"));
    q.bindValue(QStringLiteral(":uid"),   userId);
    q.bindValue(QStringLiteral(":key"),   key);
    q.bindValue(QStringLiteral(":val"),   value.toString());
    q.bindValue(QStringLiteral(":conf"),  qBound(0.0, confidence, 1.0));
    q.bindValue(QStringLiteral(":src"),   source);
    q.bindValue(QStringLiteral(":val2"),  value.toString());
    q.bindValue(QStringLiteral(":conf2"), qBound(0.0, confidence, 1.0));
    q.bindValue(QStringLiteral(":src2"),  source);

    if (q.exec()) {
        lock.unlock();
        emit profileChanged(userId);
    } else {
        qWarning() << "[UserProfileExt] UPSERT failed:" << q.lastError().text();
    }
}

QVariant UserProfileExtended::profileField(const QString& userId,
                                             const QString& key) const
{
    QMutexLocker lock(&m_mutex);

    if (!DatabaseManager::instance().isOpen()) return QVariant();
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return QVariant();

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT value FROM user_profiles WHERE user_id = :uid AND key = :key"));
    q.bindValue(QStringLiteral(":uid"), userId);
    q.bindValue(QStringLiteral(":key"), key);

    if (q.exec() && q.next())
        return q.value(0);
    return QVariant();
}

ProfileField UserProfileExtended::profileFieldFull(const QString& userId,
                                                     const QString& key) const
{
    QMutexLocker lock(&m_mutex);
    ProfileField f;
    f.key = key;

    if (!DatabaseManager::instance().isOpen()) return f;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return f;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT value, confidence, source, updated_at "
        "FROM user_profiles WHERE user_id = :uid AND key = :key"));
    q.bindValue(QStringLiteral(":uid"), userId);
    q.bindValue(QStringLiteral(":key"), key);

    if (q.exec() && q.next()) {
        f.value       = q.value(0);
        f.confidence  = q.value(1).toDouble();
        f.source      = q.value(2).toString();
        f.lastUpdated = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
    }
    return f;
}

// ============================================================
//  Bulk queries
// ============================================================

QList<ProfileField> UserProfileExtended::allFields(const QString& userId) const
{
    QMutexLocker lock(&m_mutex);
    QList<ProfileField> result;

    if (!DatabaseManager::instance().isOpen()) return result;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return result;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT key, value, confidence, source, updated_at "
        "FROM user_profiles WHERE user_id = :uid ORDER BY key"));
    q.bindValue(QStringLiteral(":uid"), userId);

    if (q.exec()) {
        while (q.next()) {
            ProfileField f;
            f.key         = q.value(0).toString();
            f.value       = q.value(1);
            f.confidence  = q.value(2).toDouble();
            f.source      = q.value(3).toString();
            f.lastUpdated = QDateTime::fromString(q.value(4).toString(), Qt::ISODate);
            result.append(f);
        }
    }
    return result;
}

QList<ProfileField> UserProfileExtended::lowConfidenceFields(const QString& userId) const
{
    QMutexLocker lock(&m_mutex);
    QList<ProfileField> result;

    if (!DatabaseManager::instance().isOpen()) return result;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return result;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT key, value, confidence, source, updated_at "
        "FROM user_profiles WHERE user_id = :uid AND confidence < 0.75 "
        "ORDER BY confidence ASC"));
    q.bindValue(QStringLiteral(":uid"), userId);

    if (q.exec()) {
        while (q.next()) {
            ProfileField f;
            f.key         = q.value(0).toString();
            f.value       = q.value(1);
            f.confidence  = q.value(2).toDouble();
            f.source      = q.value(3).toString();
            f.lastUpdated = QDateTime::fromString(q.value(4).toString(), Qt::ISODate);
            result.append(f);
        }
    }
    return result;
}

QStringList UserProfileExtended::emptyRequiredFields(const QString& userId) const
{
    QStringList empty;
    for (const QString& key : requiredFields()) {
        const ProfileField f = profileFieldFull(userId, key);
        if (f.isEmpty())
            empty.append(key);
    }
    return empty;
}

// ============================================================
//  Q_PROPERTY accessors — operate on m_userId
// ============================================================

QString UserProfileExtended::nickname() const
{ return profileField(m_userId, QStringLiteral("nickname")).toString(); }
void UserProfileExtended::setNickname(const QString& v)
{ updateProfileField(m_userId, QStringLiteral("nickname"), v); }

QString UserProfileExtended::devStyle() const
{ return profileField(m_userId, QStringLiteral("dev_style")).toString(); }
void UserProfileExtended::setDevStyle(const QString& v)
{ updateProfileField(m_userId, QStringLiteral("dev_style"), v); }

QString UserProfileExtended::uiAccentColor() const
{ return profileField(m_userId, QStringLiteral("ui_accent_color")).toString(); }
void UserProfileExtended::setUiAccentColor(const QString& v)
{ updateProfileField(m_userId, QStringLiteral("ui_accent_color"), v); }

int UserProfileExtended::activeHoursStart() const
{ return profileField(m_userId, QStringLiteral("active_hours_start")).toInt(); }
void UserProfileExtended::setActiveHoursStart(int hour)
{ updateProfileField(m_userId, QStringLiteral("active_hours_start"), qBound(0, hour, 23)); }

int UserProfileExtended::activeHoursEnd() const
{ return profileField(m_userId, QStringLiteral("active_hours_end")).toInt(); }
void UserProfileExtended::setActiveHoursEnd(int hour)
{ updateProfileField(m_userId, QStringLiteral("active_hours_end"), qBound(0, hour, 23)); }

QString UserProfileExtended::meshRole() const
{ return profileField(m_userId, QStringLiteral("mesh_role")).toString(); }
void UserProfileExtended::setMeshRole(const QString& role)
{ updateProfileField(m_userId, QStringLiteral("mesh_role"), role); }

// ============================================================
//  LLM context summary
// ============================================================

QString UserProfileExtended::buildIdentitySummary(const QString& userId) const
{
    auto fields = allFields(userId);
    if (fields.isEmpty()) return QString();

    QString summary;
    summary += QStringLiteral("[User Identity]\n");

    for (const ProfileField& f : fields) {
        if (f.isEmpty()) continue;
        summary += QStringLiteral("- %1: %2").arg(f.key, f.value.toString());
        if (f.confidence < 0.75)
            summary += QStringLiteral(" (low confidence: %1)")
                           .arg(QString::number(f.confidence, 'f', 2));
        summary += QChar('\n');
    }
    return summary;
}

// ============================================================
//  Mesh sync serialization
// ============================================================

QJsonObject UserProfileExtended::toJson(const QString& userId) const
{
    QJsonObject obj;
    auto fields = allFields(userId);
    for (const ProfileField& f : fields) {
        QJsonObject entry;
        entry[QStringLiteral("value")]      = f.value.toString();
        entry[QStringLiteral("confidence")] = f.confidence;
        entry[QStringLiteral("source")]     = f.source;
        obj[f.key] = entry;
    }
    return obj;
}

void UserProfileExtended::fromJson(const QString& userId, const QJsonObject& obj)
{
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        updateProfileField(
            userId,
            it.key(),
            entry[QStringLiteral("value")].toString(),
            entry[QStringLiteral("confidence")].toDouble(0.5),
            entry[QStringLiteral("source")].toString(QStringLiteral("mesh_sync")));
    }
}
