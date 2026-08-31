#pragma once
// ============================================================
// llm_cache_manager.h — Local-First Offline Fallback Cache
//
// SHA-256 hash-based LLM response cache backed by SQLite.
// Provides instant offline responses for repeated queries
// when the network is unavailable or the API times out.
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>

class LlmCacheManager : public QObject
{
    Q_OBJECT

public:
    static LlmCacheManager& instance();

    LlmCacheManager(const LlmCacheManager&)            = delete;
    LlmCacheManager& operator=(const LlmCacheManager&) = delete;

    // Scoping key for per-person learning: 0 = desktop install, otherwise a
    // Telegram chat_id. Mirrors the existing chatId==0 "desktop" convention
    // already used by Jarvis::processCommand.
    static constexpr qint64 kDesktopOwnerId = 0;

    void ensureTable();

    QString getValidCachedResponse(qint64 ownerId, const QString& query);
    void    saveResponse(qint64 ownerId, const QString& query, const QString& response);

    int     cacheEntryCount();

    // ── Layer-1 confidence router ──────────────────────────
    struct CaseMatch {
        // Associative sits below Similar: it fires only when no cached case
        // shares enough literal words with the query, but SynapseGraph's
        // spreading activation still assembled a confident-enough answer
        // out of concept-linked cases (see synapse_graph.h) — the "puzzle"
        // tier, for queries phrased nothing like anything seen before.
        enum class Tier { Exact, Similar, Associative, None };
        Tier    tier = Tier::None;
        QString response;
        QString matchedQuery;
        float   overlap = 0.0f;  // 0..1 keyword-overlap (Similar) or puzzle completeness (Associative)
        // Only populated for Tier::Associative: query concepts that no
        // activated node/case covered — the "hole in the puzzle". A caller
        // that wants to spend fewer tokens can ask Claude about just these
        // instead of the whole query.
        QStringList missingConcepts;
    };

    // Exact hash match -> Tier::Exact. Otherwise ranks candidates (FTS5 if
    // available, LIKE scan as fallback) by keyword overlap against the
    // query; overlap >= kSimilarThreshold -> Tier::Similar. If that also
    // comes up empty, falls back to SynapseGraph::assemble() — spreading
    // activation over the concept graph — which can surface a case that
    // shares no words with the query at all (Tier::Associative). No
    // candidate anywhere -> Tier::None (caller should escalate to Claude).
    // Only cases belonging to the same ownerId are ever considered — this is
    // what keeps learning individual per person. Every hit also feeds
    // SynapseGraph::reinforce() so the concept graph keeps growing from use,
    // not just from new cases being saved.
    CaseMatch route(qint64 ownerId, const QString& query);

    // Feedback hook for the associative layer: call after a user confirms
    // or corrects a locally-answered query (see SelfJournal::resolveDoubt
    // call sites) to close the Hebbian loop — wasCorrect=true strengthens
    // the concept links this query touched, false weakens them, so a wrong
    // guess actually becomes less likely to resurface the same way.
    void reportOutcome(qint64 ownerId, const QString& query, bool wasCorrect);

    // Shared with CaseDistiller (Layer 2): fraction of `a`'s significant
    // keywords (len>=3) that also appear in `b`, 0..1.
    // Доля значимых слов запроса `a`, которые нашлись в `b`. Через
    // sharedOut отдаётся ещё и их абсолютное число: одной доли мало для
    // решения — у короткого запроса одно случайно общее слово даёт целых
    // 50%, и по такому «совпадению» отдавать готовый ответ нельзя.
    static float keywordOverlap(const QString& a, const QString& b,
                                int* sharedOut = nullptr);

    // ── Layer-4 independence metric ─────────────────────────
    struct IndependenceStats {
        int total = 0;  // all route() decisions in the window
        int local = 0;  // Exact + Similar (resolved without Claude)
        float pct() const { return total > 0 ? 100.0f * local / total : 0.0f; }
    };

    // % of route() decisions resolved locally (Exact/Similar, not None) for
    // ownerId over the last `days` days. Backed by router_log, populated by
    // route() itself on every call.
    IndependenceStats independenceStats(qint64 ownerId, int days);

private:
    explicit LlmCacheManager(QObject* parent = nullptr);
    ~LlmCacheManager() override = default;

    static QString normalizeQuery(const QString& raw);
    static QString hashQuery(qint64 ownerId, const QString& normalized);
    static QStringList significantKeywords(const QString& normalized);

    bool ftsAvailable();
    QList<CaseMatch> candidatesViaFts(qint64 ownerId, const QStringList& keywords);
    QList<CaseMatch> candidatesViaLike(qint64 ownerId, const QStringList& keywords);
    void logRouterDecision(qint64 ownerId, CaseMatch::Tier tier);
};
