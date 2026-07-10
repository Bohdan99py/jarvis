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
    // Only cases belonging to the same ownerId are ever considered — this is
    // what keeps learning individual per person.
    CaseMatch route(qint64 ownerId, const QString& query);

    // Shared with CaseDistiller (Layer 2): fraction of `a`'s significant
    // keywords (len>=3) that also appear in `b`, 0..1.
    static float keywordOverlap(const QString& a, const QString& b);

private:
    explicit LlmCacheManager(QObject* parent = nullptr);
    ~LlmCacheManager() override = default;

    static QString normalizeQuery(const QString& raw);
    static QString hashQuery(qint64 ownerId, const QString& normalized);
    static QStringList significantKeywords(const QString& normalized);

    bool ftsAvailable();
    QList<CaseMatch> candidatesViaFts(qint64 ownerId, const QStringList& keywords);
    QList<CaseMatch> candidatesViaLike(qint64 ownerId, const QStringList& keywords);
};
