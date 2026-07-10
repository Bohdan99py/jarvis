// ============================================================
// llm_cache_manager.cpp — Local-First Offline Fallback Cache
// ============================================================
#include "llm_cache_manager.h"
#include "database_manager.h"

#include <QCryptographicHash>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QSet>

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

QStringList LlmCacheManager::significantKeywords(const QString& normalized)
{
    QStringList keywords;
    for (const QString& w : normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (w.length() >= 3)
            keywords.append(w);
    }
    return keywords;
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

    if (!q.exec()) {
        qWarning() << "[LlmCache] save error:" << q.lastError().text();
        return;
    }
    qDebug() << "[LlmCache] Saved response for" << norm.left(60);

    // Keep the FTS5 similarity index in sync (delete+reinsert covers the
    // upsert-update case too). No-op if the fts5 module isn't available.
    if (ftsAvailable()) {
        QSqlQuery del(db);
        del.prepare(QStringLiteral("DELETE FROM llm_cache_fts WHERE query_hash = :h"));
        del.bindValue(QStringLiteral(":h"), hash);
        del.exec();

        QSqlQuery ins(db);
        ins.prepare(QStringLiteral(
            "INSERT INTO llm_cache_fts (query_hash, original_query) VALUES (:h, :oq)"));
        ins.bindValue(QStringLiteral(":h"),  hash);
        ins.bindValue(QStringLiteral(":oq"), norm.left(500));
        if (!ins.exec())
            qWarning() << "[LlmCache] FTS sync error:" << ins.lastError().text();
    }
}

// ============================================================
//  Layer-1 confidence router
// ============================================================

bool LlmCacheManager::ftsAvailable()
{
    static int cached = -1;  // -1 = unknown, 0 = no, 1 = yes
    if (cached != -1) return cached == 1;

    auto db = DatabaseManager::instance().isOpen()
            ? QSqlDatabase::database(QStringLiteral("jarvis_main"))
            : QSqlDatabase();
    if (!db.isOpen()) { cached = 0; return false; }

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='llm_cache_fts'"));
    cached = q.next() ? 1 : 0;

    qDebug() << "[LlmCache] FTS5 similarity index"
             << (cached == 1 ? "available" : "NOT available — falling back to LIKE scan");
    return cached == 1;
}

QList<LlmCacheManager::CaseMatch> LlmCacheManager::candidatesViaFts(const QStringList& keywords)
{
    QList<CaseMatch> out;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return out;

    QStringList terms;
    for (const QString& kw : keywords) {
        QString escaped = kw;
        escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        terms.append(QStringLiteral("\"%1\"*").arg(escaped));
    }
    const QString ftsQuery = terms.join(QStringLiteral(" OR "));

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT c.original_query, c.response_text FROM llm_cache_fts f "
        "JOIN llm_cache c ON c.query_hash = f.query_hash "
        "WHERE llm_cache_fts MATCH :mq ORDER BY rank LIMIT 5"));
    q.bindValue(QStringLiteral(":mq"), ftsQuery);

    if (!q.exec()) {
        qWarning() << "[LlmCache] FTS query error:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        CaseMatch m;
        m.matchedQuery = q.value(0).toString();
        m.response     = q.value(1).toString();
        out.append(m);
    }
    return out;
}

QList<LlmCacheManager::CaseMatch> LlmCacheManager::candidatesViaLike(const QStringList& keywords)
{
    QList<CaseMatch> out;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return out;

    // OR-based scan: any keyword may match, unlike the old strict-AND
    // approach — this lets partial overlaps surface as Similar candidates
    // instead of silently returning nothing.
    QString whereClause;
    QStringList bindKeys;
    for (int i = 0; i < keywords.size(); ++i) {
        if (i > 0) whereClause += QStringLiteral(" OR ");
        const QString key = QStringLiteral(":k%1").arg(i);
        whereClause += QStringLiteral("original_query LIKE %1").arg(key);
        bindKeys.append(key);
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT original_query, response_text FROM llm_cache WHERE %1 "
        "ORDER BY timestamp DESC LIMIT 5").arg(whereClause));
    for (int i = 0; i < keywords.size(); ++i)
        q.bindValue(bindKeys[i], QStringLiteral("%%1%").arg(keywords[i]));

    if (!q.exec()) return out;
    while (q.next()) {
        CaseMatch m;
        m.matchedQuery = q.value(0).toString();
        m.response     = q.value(1).toString();
        out.append(m);
    }
    return out;
}

LlmCacheManager::CaseMatch LlmCacheManager::route(const QString& query)
{
    static constexpr float kSimilarThreshold = 0.6f;

    CaseMatch result;
    const QString norm = normalizeQuery(query);
    if (norm.length() < 5) return result;

    // 1. Exact hash match — fastest path, highest confidence.
    const QString exact = getValidCachedResponse(query);
    if (!exact.isEmpty()) {
        result.tier         = CaseMatch::Tier::Exact;
        result.response     = exact;
        result.matchedQuery = norm;
        result.overlap      = 1.0f;
        return result;
    }

    // 2. Similarity candidates, ranked by keyword overlap against the query.
    const QStringList queryKeywords = significantKeywords(norm);
    if (queryKeywords.isEmpty()) return result;

    const QList<CaseMatch> candidates = ftsAvailable()
        ? candidatesViaFts(queryKeywords)
        : candidatesViaLike(queryKeywords);
    if (candidates.isEmpty()) return result;

    const QSet<QString> queryKeywordSet(queryKeywords.begin(), queryKeywords.end());

    CaseMatch best;
    for (const CaseMatch& c : candidates) {
        const QStringList candKeywords = significantKeywords(c.matchedQuery);
        const QSet<QString> candKeywordSet(candKeywords.begin(), candKeywords.end());

        int hits = 0;
        for (const QString& kw : queryKeywordSet)
            if (candKeywordSet.contains(kw)) ++hits;
        const float overlap = queryKeywordSet.isEmpty()
            ? 0.0f : static_cast<float>(hits) / queryKeywordSet.size();

        if (overlap > best.overlap) {
            best = c;
            best.overlap = overlap;
        }
    }

    if (best.overlap >= kSimilarThreshold) {
        best.tier = CaseMatch::Tier::Similar;
        return best;
    }
    return result;  // Tier::None — nothing close enough, escalate
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
