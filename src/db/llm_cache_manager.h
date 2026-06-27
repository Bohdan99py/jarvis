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

    QString findLocalLearnedResponse(const QString& query);

    int     cacheEntryCount();

private:
    explicit LlmCacheManager(QObject* parent = nullptr);
    ~LlmCacheManager() override = default;

    static QString normalizeQuery(const QString& raw);
    static QString hashQuery(const QString& normalized);
};
