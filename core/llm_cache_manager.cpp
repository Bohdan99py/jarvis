// ============================================================
// llm_cache_manager.cpp — Local-First Offline Fallback Cache
// ============================================================
#include "llm_cache_manager.h"
#include "database_manager.h"

#include <QCryptographicHash>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

// ============================================================
//  Singleton
// ============================================================

LlmCacheManager& LlmCacheManager::instance()
{
    static LlmCacheManager inst;
    return inst;
}

LlmCacheManager::LlmCacheManager(QObject* parent)
    : QObject(parent)
{}

// ============================================================
//  Query normalization & hashing
// ============================================================

QString LlmCacheManager::normalizeQuery(const QString& raw)
{
    return raw.trimmed().toLower().simplified();
}

QString LlmCacheManager::hashQuery(const QString& normalized)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(normalized.toUtf8(),
                                 QCryptographicHash::Sha256).toHex());
}

// ============================================================
//  Schema — called from DatabaseManager::createTables()
// ============================================================

void LlmCacheManager::ensureTable()
{
    auto db = DatabaseManager::instance().isOpen()
            ? QSqlDatabase::database(QStringLiteral("jarvis_main"))
            : QSqlDatabase();

    if (!db.isOpen()) {
        qWarning() << "[LlmCache] DB not open, cannot create table";
        return;
    }

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS llm_cache ("
        "  query_hash     TEXT PRIMARY KEY,"
        "  original_query TEXT NOT NULL,"
        "  response_text  TEXT NOT NULL,"
        "  timestamp      TEXT NOT NULL DEFAULT (datetime('now'))"
        ")"));

    if (q.lastError().isValid())
        qWarning() << "[LlmCache] table creation error:" << q.lastError().text();
}

// ============================================================
//  Lookup
// ============================================================

QString LlmCacheManager::getValidCachedResponse(const QString& query)
{
    const QString norm = normalizeQuery(query);
    if (norm.isEmpty()) return {};

    const QString hash = hashQuery(norm);

    auto db = DatabaseManager::instance().isOpen()
            ? QSqlDatabase::database(QStringLiteral("jarvis_main"))
            : QSqlDatabase();
    if (!db.isOpen()) return {};

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT response_text FROM llm_cache WHERE query_hash = :h"));
    q.bindValue(QStringLiteral(":h"), hash);

    if (!q.exec() || !q.next()) return {};

    qDebug() << "[LlmCache] HIT for" << norm.left(60);
    return q.value(0).toString();
}

// ============================================================
//  Save / update
// ============================================================

void LlmCacheManager::saveResponse(const QString& query, const QString& response)
{
    const QString norm = normalizeQuery(query);
    if (norm.isEmpty() || response.isEmpty()) return;

    const QString hash = hashQuery(norm);

    auto db = DatabaseManager::instance().isOpen()
            ? QSqlDatabase::database(QStringLiteral("jarvis_main"))
            : QSqlDatabase();
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO llm_cache (query_hash, original_query, response_text, timestamp) "
        "VALUES (:h, :oq, :rt, datetime('now')) "
        "ON CONFLICT(query_hash) DO UPDATE SET "
        "  response_text = excluded.response_text, "
        "  timestamp     = excluded.timestamp"));
    q.bindValue(QStringLiteral(":h"),  hash);
    q.bindValue(QStringLiteral(":oq"), norm.left(500));
    q.bindValue(QStringLiteral(":rt"), response);

    if (!q.exec())
        qWarning() << "[LlmCache] save error:" << q.lastError().text();
    else
        qDebug() << "[LlmCache] Saved response for" << norm.left(60);
}

// ============================================================
//  Stats
// ============================================================

int LlmCacheManager::cacheEntryCount()
{
    auto db = DatabaseManager::instance().isOpen()
            ? QSqlDatabase::database(QStringLiteral("jarvis_main"))
            : QSqlDatabase();
    if (!db.isOpen()) return 0;

    QSqlQuery q(db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM llm_cache"));
    return q.next() ? q.value(0).toInt() : 0;
}
