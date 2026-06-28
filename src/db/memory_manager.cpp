// ============================================================
// memory_manager.cpp — Vectorized Core Memory Manager
// ============================================================

#include "memory_manager.h"
#include "database_manager.h"
#include "jarvis_paths.h"

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QFile>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QtMath>
#include <QDebug>

#include <algorithm>
#include <cmath>

// ============================================================
//  Singleton
// ============================================================

MemoryManager& MemoryManager::instance()
{
    static MemoryManager inst;
    return inst;
}

MemoryManager::MemoryManager(QObject* parent)
    : QObject(parent)
{
}

MemoryManager::~MemoryManager() = default;

// ============================================================
//  Initialization
// ============================================================

void MemoryManager::initialize()
{
    ensureTable();
    loadIdfVocabulary();
    qDebug() << "[MemoryManager] Initialized. IDF vocab size:"
             << m_idf.size() << "total docs:" << m_totalDocuments;
}

void MemoryManager::ensureTable()
{
    if (!DatabaseManager::instance().isOpen()) return;

    QMutexLocker lock(&m_mutex);
    QSqlQuery q(QSqlDatabase::database());

    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS memory_vectors ("
        "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  tag             TEXT NOT NULL,"
        "  content         TEXT NOT NULL,"
        "  importance      REAL DEFAULT 0.5,"
        "  reinforcements  INTEGER DEFAULT 0,"
        "  vector_data     BLOB,"
        "  created_at      TEXT NOT NULL DEFAULT (datetime('now')),"
        "  last_accessed   TEXT DEFAULT (datetime('now'))"
        ")"));

    if (q.lastError().isValid())
        qWarning() << "[MemoryManager] Table creation error:" << q.lastError().text();

    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_memvec_tag "
        "ON memory_vectors(tag)"));

    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_memvec_importance "
        "ON memory_vectors(importance DESC, created_at DESC)"));
}

// ============================================================
//  IDF Vocabulary Persistence
// ============================================================

void MemoryManager::loadIdfVocabulary()
{
    const QString path = JarvisPaths::subPath(QStringLiteral("memory/idf_vocab.json"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
    m_totalDocuments = root[QStringLiteral("total_docs")].toInt(0);

    QJsonObject vocab = root[QStringLiteral("idf")].toObject();
    m_idf.clear();
    m_idf.reserve(vocab.size());
    for (auto it = vocab.begin(); it != vocab.end(); ++it)
        m_idf[it.key()] = static_cast<float>(it.value().toDouble());
}

void MemoryManager::saveIdfVocabulary()
{
    QJsonObject vocab;
    for (auto it = m_idf.begin(); it != m_idf.end(); ++it)
        vocab[it.key()] = static_cast<double>(it.value());

    QJsonObject root;
    root[QStringLiteral("total_docs")] = m_totalDocuments;
    root[QStringLiteral("idf")]        = vocab;

    const QString path = JarvisPaths::subPath(QStringLiteral("memory/idf_vocab.json"));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.close();
    }
}

// ============================================================
//  Tokenization & Embedding
// ============================================================

QStringList MemoryManager::tokenize(const QString& text)
{
    // Lowercase + split on non-alphanumeric (handles EN + RU + digits)
    const QString lower = text.toLower();
    static const QRegularExpression splitter(
        QStringLiteral("[^\\p{L}\\p{N}]+"));

    QStringList tokens = lower.split(splitter, Qt::SkipEmptyParts);

    // Remove very short tokens (< 2 chars) and stopwords
    static const QSet<QString> stopwords = {
        // English
        QStringLiteral("the"), QStringLiteral("a"), QStringLiteral("an"),
        QStringLiteral("is"), QStringLiteral("are"), QStringLiteral("was"),
        QStringLiteral("were"), QStringLiteral("be"), QStringLiteral("been"),
        QStringLiteral("am"), QStringLiteral("do"), QStringLiteral("does"),
        QStringLiteral("did"), QStringLiteral("have"), QStringLiteral("has"),
        QStringLiteral("had"), QStringLiteral("it"), QStringLiteral("its"),
        QStringLiteral("in"), QStringLiteral("on"), QStringLiteral("at"),
        QStringLiteral("to"), QStringLiteral("of"), QStringLiteral("for"),
        QStringLiteral("and"), QStringLiteral("or"), QStringLiteral("but"),
        QStringLiteral("not"), QStringLiteral("no"), QStringLiteral("if"),
        QStringLiteral("so"), QStringLiteral("as"), QStringLiteral("by"),
        QStringLiteral("with"), QStringLiteral("this"), QStringLiteral("that"),
        QStringLiteral("from"), QStringLiteral("can"), QStringLiteral("will"),
        QStringLiteral("just"), QStringLiteral("than"), QStringLiteral("then"),
        QStringLiteral("my"), QStringLiteral("me"), QStringLiteral("you"),
        QStringLiteral("your"), QStringLiteral("we"), QStringLiteral("he"),
        QStringLiteral("she"), QStringLiteral("they"), QStringLiteral("them"),
        // Russian
        QStringLiteral("и"), QStringLiteral("в"), QStringLiteral("на"),
        QStringLiteral("не"), QStringLiteral("что"), QStringLiteral("он"),
        QStringLiteral("она"), QStringLiteral("как"), QStringLiteral("это"),
        QStringLiteral("по"), QStringLiteral("но"), QStringLiteral("из"),
        QStringLiteral("за"), QStringLiteral("то"), QStringLiteral("же"),
        QStringLiteral("всё"), QStringLiteral("от"), QStringLiteral("так"),
        QStringLiteral("ещё"), QStringLiteral("бы"), QStringLiteral("ты"),
        QStringLiteral("мы"), QStringLiteral("вы"), QStringLiteral("да"),
        QStringLiteral("нет"),
    };

    QStringList filtered;
    filtered.reserve(tokens.size());
    for (const QString& t : tokens) {
        if (t.length() >= 2 && !stopwords.contains(t))
            filtered.append(t);
    }
    return filtered;
}

QMap<QString, float> MemoryManager::computeTf(const QStringList& tokens)
{
    QMap<QString, float> tf;
    if (tokens.isEmpty()) return tf;

    for (const QString& t : tokens)
        tf[t] += 1.0f;

    const float norm = static_cast<float>(tokens.size());
    for (auto it = tf.begin(); it != tf.end(); ++it)
        it.value() /= norm;

    return tf;
}

QMap<QString, float> MemoryManager::embed(const QString& text) const
{
    const QStringList tokens = tokenize(text);
    QMap<QString, float> tf = computeTf(tokens);

    // Apply IDF weighting
    QMap<QString, float> tfidf;
    for (auto it = tf.begin(); it != tf.end(); ++it) {
        float idfWeight = 1.0f;
        auto idfIt = m_idf.find(it.key());
        if (idfIt != m_idf.end())
            idfWeight = idfIt.value();
        else if (m_totalDocuments > 0)
            idfWeight = std::log(static_cast<float>(m_totalDocuments + 1));

        tfidf[it.key()] = it.value() * idfWeight;
    }

    return tfidf;
}

// ============================================================
//  Cosine Similarity
// ============================================================

double MemoryManager::cosineSimilarity(const QMap<QString, float>& a,
                                        const QMap<QString, float>& b)
{
    if (a.isEmpty() || b.isEmpty()) return 0.0;

    double dot = 0.0, normA = 0.0, normB = 0.0;

    // Iterate over the smaller map for efficiency
    const auto& smaller = (a.size() <= b.size()) ? a : b;
    const auto& larger  = (a.size() <= b.size()) ? b : a;

    for (auto it = smaller.constBegin(); it != smaller.constEnd(); ++it) {
        auto found = larger.find(it.key());
        if (found != larger.constEnd())
            dot += static_cast<double>(it.value()) * found.value();
    }

    for (auto it = a.constBegin(); it != a.constEnd(); ++it)
        normA += static_cast<double>(it.value()) * it.value();

    for (auto it = b.constBegin(); it != b.constEnd(); ++it)
        normB += static_cast<double>(it.value()) * it.value();

    const double denom = std::sqrt(normA) * std::sqrt(normB);
    return (denom > 1e-9) ? (dot / denom) : 0.0;
}

// ============================================================
//  Time-Decay with Reinforcement Preservation
// ============================================================

double MemoryManager::timeDecay(const QDateTime& created,
                                 int reinforcements)
{
    const double hoursSince = created.secsTo(QDateTime::currentDateTimeUtc())
                              / 3600.0;
    if (hoursSince < 0.0) return 1.0;

    // Exponential decay with half-life, boosted by reinforcements
    const double effectiveHalfLife = DECAY_HALF_LIFE_HOURS
        + (reinforcements * REINFORCEMENT_BONUS * DECAY_HALF_LIFE_HOURS);

    return std::exp(-0.693 * hoursSince / effectiveHalfLife);
}

// ============================================================
//  Vector Serialization
// ============================================================

QByteArray MemoryManager::serializeVector(const QMap<QString, float>& vec)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << static_cast<qint32>(vec.size());
    for (auto it = vec.constBegin(); it != vec.constEnd(); ++it) {
        stream << it.key() << it.value();
    }
    return data;
}

QMap<QString, float> MemoryManager::deserializeVector(const QByteArray& data)
{
    QMap<QString, float> vec;
    if (data.isEmpty()) return vec;

    QDataStream stream(data);
    stream.setVersion(QDataStream::Qt_6_0);
    qint32 count = 0;
    stream >> count;
    for (qint32 i = 0; i < count && !stream.atEnd(); ++i) {
        QString key;
        float value;
        stream >> key >> value;
        vec[key] = value;
    }
    return vec;
}

// ============================================================
//  Store — ingest a new memory chunk
// ============================================================

qint64 MemoryManager::store(const QString& tag,
                             const QString& content,
                             double importance)
{
    if (content.trimmed().isEmpty()) return -1;

    QMutexLocker lock(&m_mutex);

    const QMap<QString, float> vec = embed(content);
    const QByteArray vecData = serializeVector(vec);

    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "INSERT INTO memory_vectors "
        "(tag, content, importance, reinforcements, vector_data, created_at, last_accessed) "
        "VALUES (:tag, :content, :imp, 0, :vec, datetime('now'), datetime('now'))"));
    q.bindValue(QStringLiteral(":tag"),     tag);
    q.bindValue(QStringLiteral(":content"), content.left(2000));
    q.bindValue(QStringLiteral(":imp"),     importance);
    q.bindValue(QStringLiteral(":vec"),     vecData);

    if (!q.exec()) {
        qWarning() << "[MemoryManager] Store failed:" << q.lastError().text();
        return -1;
    }

    const qint64 id = q.lastInsertId().toLongLong();
    ++m_totalDocuments;

    // Rebuild IDF periodically
    if (m_totalDocuments % IDF_REBUILD_THRESHOLD == 0)
        rebuildIdf();

    emit memoryStored(id, tag);
    return id;
}

// ============================================================
//  Reinforce — boost an existing memory
// ============================================================

void MemoryManager::reinforce(qint64 chunkId)
{
    QMutexLocker lock(&m_mutex);

    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "UPDATE memory_vectors SET "
        "  reinforcements = reinforcements + 1, "
        "  importance = MIN(1.0, importance + 0.05), "
        "  last_accessed = datetime('now') "
        "WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), chunkId);
    q.exec();
}

// ============================================================
//  Semantic Search
// ============================================================

QList<MemorySearchResult> MemoryManager::search(const QString& query,
                                                  int topK,
                                                  const QString& tagFilter) const
{
    QMutexLocker lock(&m_mutex);

    const QMap<QString, float> queryVec = embed(query);
    if (queryVec.isEmpty()) return {};

    // Load candidate chunks from DB
    QString sql = QStringLiteral(
        "SELECT id, tag, content, importance, reinforcements, "
        "       vector_data, created_at, last_accessed "
        "FROM memory_vectors ");
    if (!tagFilter.isEmpty())
        sql += QStringLiteral("WHERE tag = :tag ");
    sql += QStringLiteral("ORDER BY created_at DESC LIMIT 500");

    QSqlQuery q(QSqlDatabase::database());
    q.prepare(sql);
    if (!tagFilter.isEmpty())
        q.bindValue(QStringLiteral(":tag"), tagFilter);

    if (!q.exec()) return {};

    QList<MemorySearchResult> results;
    results.reserve(500);

    while (q.next()) {
        MemoryChunk chunk;
        chunk.id             = q.value(0).toLongLong();
        chunk.tag            = q.value(1).toString();
        chunk.content        = q.value(2).toString();
        chunk.importance     = q.value(3).toDouble();
        chunk.reinforcements = q.value(4).toInt();
        chunk.vector         = deserializeVector(q.value(5).toByteArray());
        chunk.createdAt      = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
        chunk.lastAccessed   = QDateTime::fromString(q.value(7).toString(), Qt::ISODate);

        const double cosine = cosineSimilarity(queryVec, chunk.vector);
        if (cosine < 0.01) continue; // skip irrelevant

        const double decay = timeDecay(chunk.createdAt, chunk.reinforcements);
        const double score = cosine * decay * chunk.importance;

        results.append({chunk, score});
    }

    // Sort by combined score descending
    std::sort(results.begin(), results.end(),
              [](const MemorySearchResult& a, const MemorySearchResult& b) {
        return a.score > b.score;
    });

    // Trim to topK
    if (results.size() > topK)
        results.resize(topK);

    // Update last_accessed for returned results
    for (const auto& r : results) {
        QSqlQuery update(QSqlDatabase::database());
        update.prepare(QStringLiteral(
            "UPDATE memory_vectors SET last_accessed = datetime('now') "
            "WHERE id = :id"));
        update.bindValue(QStringLiteral(":id"), r.chunk.id);
        update.exec();
    }

    return results;
}

// ============================================================
//  Recent by Tag
// ============================================================

QList<MemoryChunk> MemoryManager::recentByTag(const QString& tag, int limit) const
{
    QMutexLocker lock(&m_mutex);

    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "SELECT id, tag, content, importance, reinforcements, "
        "       created_at, last_accessed "
        "FROM memory_vectors "
        "WHERE tag = :tag "
        "ORDER BY created_at DESC LIMIT :lim"));
    q.bindValue(QStringLiteral(":tag"), tag);
    q.bindValue(QStringLiteral(":lim"), limit);

    QList<MemoryChunk> chunks;
    if (!q.exec()) return chunks;

    while (q.next()) {
        MemoryChunk c;
        c.id             = q.value(0).toLongLong();
        c.tag            = q.value(1).toString();
        c.content        = q.value(2).toString();
        c.importance     = q.value(3).toDouble();
        c.reinforcements = q.value(4).toInt();
        c.createdAt      = QDateTime::fromString(q.value(5).toString(), Qt::ISODate);
        c.lastAccessed   = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
        chunks.append(c);
    }
    return chunks;
}

// ============================================================
//  Build Semantic Context (for LLM / Nudge injection)
// ============================================================

QString MemoryManager::buildSemanticContext(const QString& query,
                                             int maxChunks) const
{
    const auto results = search(query, maxChunks);
    if (results.isEmpty()) return QString();

    QString context;
    context += QStringLiteral("[Relevant Memory]\n");
    for (const auto& r : results) {
        context += QStringLiteral("- [%1] (score: %2) %3\n")
            .arg(r.chunk.tag,
                 QString::number(r.score, 'f', 3),
                 r.chunk.content.left(300));
    }
    return context;
}

// ============================================================
//  Pruning
// ============================================================

int MemoryManager::prune(int maxAge_days, double minImportance)
{
    QMutexLocker lock(&m_mutex);

    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "DELETE FROM memory_vectors "
        "WHERE importance < :minImp "
        "  AND reinforcements = 0 "
        "  AND julianday('now') - julianday(created_at) > :maxDays"));
    q.bindValue(QStringLiteral(":minImp"),  minImportance);
    q.bindValue(QStringLiteral(":maxDays"), maxAge_days);

    if (!q.exec()) return 0;

    const int removed = q.numRowsAffected();
    if (removed > 0) {
        m_totalDocuments = qMax(0, m_totalDocuments - removed);
        emit memoryPruned(removed);
        qDebug() << "[MemoryManager] Pruned" << removed << "stale chunks.";
    }
    return removed;
}

int MemoryManager::count() const
{
    QMutexLocker lock(&m_mutex);

    QSqlQuery q(QSqlDatabase::database());
    q.exec(QStringLiteral("SELECT COUNT(*) FROM memory_vectors"));
    return q.next() ? q.value(0).toInt() : 0;
}

// ============================================================
//  IDF Rebuild — recompute from corpus
// ============================================================

void MemoryManager::rebuildIdf()
{
    // Count document frequency for each token
    QHash<QString, int> docFreq;
    int docCount = 0;

    QSqlQuery q(QSqlDatabase::database());
    q.exec(QStringLiteral("SELECT content FROM memory_vectors"));

    while (q.next()) {
        ++docCount;
        const QStringList tokens = tokenize(q.value(0).toString());
        const QSet<QString> unique(tokens.begin(), tokens.end());
        for (const QString& t : unique)
            docFreq[t] += 1;
    }

    m_totalDocuments = docCount;
    m_idf.clear();
    m_idf.reserve(docFreq.size());

    for (auto it = docFreq.constBegin(); it != docFreq.constEnd(); ++it) {
        // IDF = log((N + 1) / (df + 1)) + 1  (smoothed)
        m_idf[it.key()] = std::log(
            static_cast<float>(docCount + 1) / (it.value() + 1)) + 1.0f;
    }

    saveIdfVocabulary();

    qDebug() << "[MemoryManager] IDF rebuilt. Vocab:" << m_idf.size()
             << "docs:" << docCount;
}
