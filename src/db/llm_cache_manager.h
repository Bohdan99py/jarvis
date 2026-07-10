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

    void ensureTable();

    QString getValidCachedResponse(const QString& query);
    void    saveResponse(const QString& query, const QString& response);

    int     cacheEntryCount();

    // ── Layer-1 confidence router ──────────────────────────
    struct CaseMatch {
        enum class Tier { Exact, Similar, None };
        Tier    tier = Tier::None;
        QString response;
        QString matchedQuery;
        float   overlap = 0.0f;  // 0..1 keyword-overlap, for logging/debugging
    };

    // Exact hash match -> Tier::Exact. Otherwise ranks candidates (FTS5 if
    // available, LIKE scan as fallback) by keyword overlap against the
    // query; overlap >= kSimilarThreshold -> Tier::Similar. No candidate
    // clears the bar -> Tier::None (caller should escalate to Claude).
    CaseMatch route(const QString& query);

private:
    explicit LlmCacheManager(QObject* parent = nullptr);
    ~LlmCacheManager() override = default;

    static QString normalizeQuery(const QString& raw);
    static QString hashQuery(const QString& normalized);
    static QStringList significantKeywords(const QString& normalized);

    bool ftsAvailable();
    QList<CaseMatch> candidatesViaFts(const QStringList& keywords);
    QList<CaseMatch> candidatesViaLike(const QStringList& keywords);
};
