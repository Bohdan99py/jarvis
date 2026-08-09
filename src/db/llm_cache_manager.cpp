// ============================================================
// llm_cache_manager.cpp — Local-First Offline Fallback Cache
// ============================================================
#include "llm_cache_manager.h"
#include "database_manager.h"
#include "synapse_graph.h"

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

QString LlmCacheManager::hashQuery(qint64 ownerId, const QString& normalized)
{
    const QString salted = QString::number(ownerId) + QLatin1Char('|') + normalized;
    return QString::fromLatin1(
        QCryptographicHash::hash(salted.toUtf8(),
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

float LlmCacheManager::keywordOverlap(const QString& a, const QString& b)
{
    const QStringList aKeywords = significantKeywords(normalizeQuery(a));
    if (aKeywords.isEmpty()) return 0.0f;
    const QSet<QString> aSet(aKeywords.begin(), aKeywords.end());

    const QStringList bKeywords = significantKeywords(normalizeQuery(b));
    const QSet<QString> bSet(bKeywords.begin(), bKeywords.end());

    int hits = 0;
    for (const QString& kw : aSet)
        if (bSet.contains(kw)) ++hits;
    return static_cast<float>(hits) / aSet.size();
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

QString LlmCacheManager::getValidCachedResponse(qint64 ownerId, const QString& query)
{
    const QString norm = normalizeQuery(query);
    if (norm.isEmpty()) return {};

    const QString hash = hashQuery(ownerId, norm);

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

void LlmCacheManager::saveResponse(qint64 ownerId, const QString& query, const QString& response)
{
    const QString norm = normalizeQuery(query);
    if (norm.isEmpty() || response.isEmpty()) return;

    const QString hash = hashQuery(ownerId, norm);

    auto db = DatabaseManager::instance().isOpen()
            ? QSqlDatabase::database(QStringLiteral("jarvis_main"))
            : QSqlDatabase();
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO llm_cache (query_hash, owner_id, original_query, response_text, timestamp) "
        "VALUES (:h, :oid, :oq, :rt, datetime('now')) "
        "ON CONFLICT(query_hash) DO UPDATE SET "
        "  response_text = excluded.response_text, "
        "  timestamp     = excluded.timestamp"));
    q.bindValue(QStringLiteral(":h"),   hash);
    q.bindValue(QStringLiteral(":oid"), ownerId);
    q.bindValue(QStringLiteral(":oq"),  norm.left(500));
    q.bindValue(QStringLiteral(":rt"),  response);

    if (!q.exec()) {
        qWarning() << "[LlmCache] save error:" << q.lastError().text();
        return;
    }
    qDebug() << "[LlmCache] Saved response for owner" << ownerId << ":" << norm.left(60);

    // Keep the FTS5 similarity index in sync (delete+reinsert covers the
    // upsert-update case too). No-op if the fts5 module isn't available.
    if (ftsAvailable()) {
        QSqlQuery del(db);
        del.prepare(QStringLiteral("DELETE FROM llm_cache_fts WHERE query_hash = :h"));
        del.bindValue(QStringLiteral(":h"), hash);
        del.exec();

        QSqlQuery ins(db);
        ins.prepare(QStringLiteral(
            "INSERT INTO llm_cache_fts (query_hash, owner_id, original_query) VALUES (:h, :oid, :oq)"));
        ins.bindValue(QStringLiteral(":h"),   hash);
        ins.bindValue(QStringLiteral(":oid"), ownerId);
        ins.bindValue(QStringLiteral(":oq"),  norm.left(500));
        if (!ins.exec())
            qWarning() << "[LlmCache] FTS sync error:" << ins.lastError().text();
    }

    // Every stored case seeds the associative layer: its concepts become/
    // reinforce graph nodes, get pairwise-linked to each other (Hebbian:
    // "fired together in this query"), and are tied to this case's hash so
    // spreading activation can surface it later for a differently-worded
    // but conceptually related query (see SynapseGraph::assemble).
    SynapseGraph::instance().reinforce(ownerId, norm, hash);
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

QList<LlmCacheManager::CaseMatch> LlmCacheManager::candidatesViaFts(qint64 ownerId, const QStringList& keywords)
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
        "WHERE llm_cache_fts MATCH :mq AND f.owner_id = :oid ORDER BY rank LIMIT 5"));
    q.bindValue(QStringLiteral(":mq"),  ftsQuery);
    q.bindValue(QStringLiteral(":oid"), ownerId);

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

QList<LlmCacheManager::CaseMatch> LlmCacheManager::candidatesViaLike(qint64 ownerId, const QStringList& keywords)
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
        "SELECT original_query, response_text FROM llm_cache WHERE owner_id = :oid AND (%1) "
        "ORDER BY timestamp DESC LIMIT 5").arg(whereClause));
    q.bindValue(QStringLiteral(":oid"), ownerId);
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

void LlmCacheManager::logRouterDecision(qint64 ownerId, CaseMatch::Tier tier)
{
    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return;

    const char* tierStr = tier == CaseMatch::Tier::Exact       ? "Exact"
                        : tier == CaseMatch::Tier::Similar     ? "Similar"
                        : tier == CaseMatch::Tier::Associative ? "Associative"
                                                                : "None";
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO router_log (owner_id, tier) VALUES (:oid, :tier)"));
    q.bindValue(QStringLiteral(":oid"),  ownerId);
    q.bindValue(QStringLiteral(":tier"), QString::fromLatin1(tierStr));
    q.exec();  // best-effort — a missed log row isn't worth surfacing an error for
}

LlmCacheManager::CaseMatch LlmCacheManager::route(qint64 ownerId, const QString& query)
{
    static constexpr float kSimilarThreshold = 0.6f;

    // Every real call is logged (Layer-4 independence metric) — the only
    // path that skips it is the "too short to be a real query" guard below,
    // which callers (e.g. Brain::tryLocalAnswer) already filter out before
    // ever reaching route(), so it's not a meaningful routing decision.
    auto finish = [this, ownerId](CaseMatch r) {
        logRouterDecision(ownerId, r.tier);
        return r;
    };

    CaseMatch result;
    const QString norm = normalizeQuery(query);
    if (norm.length() < 5) return result;

    // 1. Exact hash match — fastest path, highest confidence.
    const QString exact = getValidCachedResponse(ownerId, query);
    if (!exact.isEmpty()) {
        result.tier         = CaseMatch::Tier::Exact;
        result.response     = exact;
        result.matchedQuery = norm;
        result.overlap      = 1.0f;
        SynapseGraph::instance().reinforce(ownerId, norm);
        return finish(result);
    }

    // 2. Similarity candidates (same owner only), ranked by keyword overlap.
    const QStringList queryKeywords = significantKeywords(norm);
    if (queryKeywords.isEmpty()) return finish(result);

    const QList<CaseMatch> candidates = ftsAvailable()
        ? candidatesViaFts(ownerId, queryKeywords)
        : candidatesViaLike(ownerId, queryKeywords);
    if (candidates.isEmpty()) return finish(result);

    CaseMatch best;
    for (const CaseMatch& c : candidates) {
        const float overlap = keywordOverlap(norm, c.matchedQuery);
        if (overlap > best.overlap) {
            best = c;
            best.overlap = overlap;
        }
    }

    if (best.overlap >= kSimilarThreshold) {
        best.tier = CaseMatch::Tier::Similar;
        SynapseGraph::instance().reinforce(ownerId, norm);
        return finish(best);
    }

    // 3. Nothing shares enough literal words — try assembling an answer out
    // of concept-linked fragments instead (spreading activation over
    // SynapseGraph). This is the "puzzle" fallback: a query that textually
    // matches nothing can still be conceptually made of pieces seen before.
    static constexpr float kAssociativeCompletenessFloor = 0.5f;
    const SynapseGraph::Assembly assembly = SynapseGraph::instance().assemble(ownerId, norm);
    if (!assembly.fragments.isEmpty() && assembly.completeness >= kAssociativeCompletenessFloor) {
        const SynapseGraph::Fragment& top = assembly.fragments.first();
        CaseMatch am;
        am.tier            = CaseMatch::Tier::Associative;
        am.response        = top.response;
        am.matchedQuery    = top.query;
        am.overlap         = assembly.completeness;
        am.missingConcepts = assembly.missingConcepts;
        SynapseGraph::instance().reinforce(ownerId, norm);
        return finish(am);
    }

    return finish(result);  // Tier::None — nothing close enough, escalate
}

void LlmCacheManager::reportOutcome(qint64 ownerId, const QString& query, bool wasCorrect)
{
    const QString norm = normalizeQuery(query);
    if (norm.isEmpty()) return;

    if (wasCorrect) SynapseGraph::instance().reinforce(ownerId, norm);
    else            SynapseGraph::instance().weaken(ownerId, norm);
}

LlmCacheManager::IndependenceStats LlmCacheManager::independenceStats(qint64 ownerId, int days)
{
    IndependenceStats stats;
    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return stats;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*), COUNT(CASE WHEN tier != 'None' THEN 1 END) "
        "FROM router_log WHERE owner_id = :oid "
        "AND created_at >= datetime('now', :window)"));
    q.bindValue(QStringLiteral(":oid"), ownerId);
    q.bindValue(QStringLiteral(":window"), QStringLiteral("-%1 days").arg(days));

    if (q.exec() && q.next()) {
        stats.total = q.value(0).toInt();
        stats.local = q.value(1).toInt();
    }
    return stats;
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
