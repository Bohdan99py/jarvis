#pragma once
// ============================================================
// memory_manager.h — Vectorized Core Memory Manager
//
// Lightweight semantic memory store with TF-IDF vector
// embeddings and cosine similarity search. No external
// dependencies (no FAISS) — all computation in-process.
//
// Features:
//   1. Embeds text into sparse TF-IDF vectors (token → weight)
//   2. Cosine similarity search for semantic retrieval
//   3. Time-decay weighting: newer = higher score,
//      but frequently reinforced concepts are preserved
//   4. SQLite-backed storage (memory_vectors table)
//   5. Thread-safe via QMutex
//
// Storage format: semantically tagged chunks with metadata:
//   { id, tag, content, importance, reinforcements,
//     created_at, last_accessed, vector_data (binary) }
//
// Integration: SocialPresenceEngine queries this before
// forming nudges. ReflectionEngine writes analysis results.
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QVector>
#include <QMap>
#include <QMutex>
#include <QHash>
#include <QPair>

// ── Memory chunk — one semantic unit ────────────────────────

struct MemoryChunk
{
    qint64    id             = 0;
    QString   tag;                    // "dialogue", "insight", "emotion",
                                      // "preference", "reflection", "skill"
    QString   content;                // the actual text
    double    importance     = 0.5;   // 0.0–1.0
    int       reinforcements = 0;     // how many times reinforced
    QDateTime createdAt;
    QDateTime lastAccessed;

    // Sparse vector: token → TF-IDF weight
    QMap<QString, float> vector;
};

// ── Search result with relevance score ──────────────────────

struct MemorySearchResult
{
    MemoryChunk chunk;
    double      score = 0.0;         // combined: cosine * time_decay * importance
};

// ── Memory Manager ──────────────────────────────────────────

class MemoryManager : public QObject
{
    Q_OBJECT

public:
    static MemoryManager& instance();

    MemoryManager(const MemoryManager&)            = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

    void initialize();

    // ── Ingestion ──────────────────────────────────────────
    // Store a new semantic chunk with automatic embedding
    qint64 store(const QString& tag,
                 const QString& content,
                 double importance = 0.5);

    // Reinforce an existing memory (boosts importance + decay resistance)
    void reinforce(qint64 chunkId);

    // ── Retrieval ──────────────────────────────────────────
    // Semantic search: returns top-K chunks most relevant to query
    QList<MemorySearchResult> search(const QString& query,
                                     int topK = 5,
                                     const QString& tagFilter = QString()) const;

    // Retrieve recent memories by tag
    QList<MemoryChunk> recentByTag(const QString& tag, int limit = 10) const;

    // Build a context string from top semantic matches (for LLM injection)
    QString buildSemanticContext(const QString& query,
                                int maxChunks = 3) const;

    // ── Maintenance ────────────────────────────────────────
    // Prune low-importance, old, unreinforced memories
    int prune(int maxAge_days = 90, double minImportance = 0.1);

    // Total chunk count
    int count() const;

    // ── IDF vocabulary ─────────────────────────────────────
    // Rebuild IDF weights from all stored documents
    void rebuildIdf();

    // Tokenize text into normalized terms (public for cross-module use)
    static QStringList tokenize(const QString& text);

signals:
    void memoryStored(qint64 id, const QString& tag);
    void memoryPruned(int removedCount);

private:
    explicit MemoryManager(QObject* parent = nullptr);
    ~MemoryManager() override;

    void ensureTable();
    void loadIdfVocabulary();
    void saveIdfVocabulary();

    // ── Embedding engine ───────────────────────────────────
    // Compute TF (term frequency) for a token list
    static QMap<QString, float> computeTf(const QStringList& tokens);

    // Compute full TF-IDF vector using current IDF weights
    QMap<QString, float> embed(const QString& text) const;

    // Cosine similarity between two sparse vectors
    static double cosineSimilarity(const QMap<QString, float>& a,
                                   const QMap<QString, float>& b);

    // Time-decay factor: newer = higher, reinforced = preserved
    static double timeDecay(const QDateTime& created,
                            int reinforcements);

    // Serialize/deserialize sparse vector to/from binary
    static QByteArray serializeVector(const QMap<QString, float>& vec);
    static QMap<QString, float> deserializeVector(const QByteArray& data);

    // IDF vocabulary: token → inverse document frequency weight
    QHash<QString, float> m_idf;
    int                   m_totalDocuments = 0;

    mutable QMutex m_mutex;

    static constexpr int    IDF_REBUILD_THRESHOLD = 50;
    static constexpr double DECAY_HALF_LIFE_HOURS = 168.0; // 7 days
    static constexpr double REINFORCEMENT_BONUS   = 0.15;  // per reinforcement
};
