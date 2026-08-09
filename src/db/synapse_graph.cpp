// ============================================================
// synapse_graph.cpp — ассоциативная память: см. synapse_graph.h
// ============================================================
#include "synapse_graph.h"
#include "database_manager.h"
#include "memory_manager.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QSet>
#include <QHash>
#include <QPair>
#include <algorithm>

// ============================================================
//  Синглтон
// ============================================================

SynapseGraph& SynapseGraph::instance()
{
    static SynapseGraph inst;
    return inst;
}

SynapseGraph::SynapseGraph(QObject* parent)
    : QObject(parent)
{}

// ============================================================
//  Понятия — переиспользуем токенайзер MemoryManager (RU+EN,
//  стоп-слова уже вычищены), только дедупликация + потолок сверху.
// ============================================================

QStringList SynapseGraph::extractConcepts(const QString& text)
{
    const QStringList tokens = MemoryManager::tokenize(text);

    QStringList concepts;
    QSet<QString> seen;
    for (const QString& t : tokens) {
        if (seen.contains(t)) continue;
        seen.insert(t);
        concepts.append(t);
        if (concepts.size() >= kMaxConceptsPerQuery) break;
    }
    return concepts;
}

// ============================================================
//  Узлы и рёбра — низкоуровневые SQL-примитивы
// ============================================================

qint64 SynapseGraph::findOrCreateNode(qint64 ownerId, const QString& label)
{
    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return 0;

    QSqlQuery up(db);
    up.prepare(QStringLiteral(
        "INSERT INTO synapse_nodes (owner_id, label, activations, created_at, last_activated_at) "
        "VALUES (:oid, :l, 1, datetime('now'), datetime('now')) "
        "ON CONFLICT(owner_id, label) DO UPDATE SET "
        "  activations = activations + 1, "
        "  last_activated_at = datetime('now')"));
    up.bindValue(QStringLiteral(":oid"), ownerId);
    up.bindValue(QStringLiteral(":l"), label);
    if (!up.exec()) {
        qWarning() << "[SynapseGraph] node upsert error:" << up.lastError().text();
        return 0;
    }

    QSqlQuery sel(db);
    sel.prepare(QStringLiteral(
        "SELECT id FROM synapse_nodes WHERE owner_id=:oid AND label=:l"));
    sel.bindValue(QStringLiteral(":oid"), ownerId);
    sel.bindValue(QStringLiteral(":l"), label);
    if (sel.exec() && sel.next()) return sel.value(0).toLongLong();
    return 0;
}

void SynapseGraph::linkEdge(qint64 ownerId, qint64 nodeA, qint64 nodeB, float delta)
{
    if (nodeA == nodeB || nodeA == 0 || nodeB == 0) return;

    // Рёбра неориентированные — канонизируем порядок узлов, чтобы A-B
    // и B-A не заводили две разных строки под UNIQUE(owner_id, node_a, node_b).
    const qint64 a = qMin(nodeA, nodeB);
    const qint64 b = qMax(nodeA, nodeB);
    const float  initWeight = qBound(0.0f, delta, kEdgeWeightMax);

    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO synapse_edges (owner_id, node_a, node_b, weight, co_activations, updated_at) "
        "VALUES (:oid, :a, :b, :initw, 1, datetime('now')) "
        "ON CONFLICT(owner_id, node_a, node_b) DO UPDATE SET "
        "  weight = MAX(0.0, MIN(:maxw, synapse_edges.weight + :delta)), "
        "  co_activations = synapse_edges.co_activations + 1, "
        "  updated_at = datetime('now')"));
    q.bindValue(QStringLiteral(":oid"),   ownerId);
    q.bindValue(QStringLiteral(":a"),     a);
    q.bindValue(QStringLiteral(":b"),     b);
    q.bindValue(QStringLiteral(":initw"), initWeight);
    q.bindValue(QStringLiteral(":maxw"),  kEdgeWeightMax);
    q.bindValue(QStringLiteral(":delta"), delta);
    if (!q.exec())
        qWarning() << "[SynapseGraph] edge upsert error:" << q.lastError().text();
}

void SynapseGraph::linkCase(qint64 nodeId, const QString& caseHash, float weight)
{
    if (nodeId == 0 || caseHash.isEmpty()) return;

    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO synapse_case_links (node_id, case_hash, weight) "
        "VALUES (:nid, :h, :w) "
        "ON CONFLICT(node_id, case_hash) DO UPDATE SET "
        "  weight = MIN(%1, weight + %2)")
        .arg(kCaseLinkMax).arg(kCaseLinkIncrement));
    q.bindValue(QStringLiteral(":nid"), nodeId);
    q.bindValue(QStringLiteral(":h"),   caseHash);
    q.bindValue(QStringLiteral(":w"),   weight);
    if (!q.exec())
        qWarning() << "[SynapseGraph] case link upsert error:" << q.lastError().text();
}

// ============================================================
//  Хеббовское обучение — reinforce() / weaken()
// ============================================================

void SynapseGraph::reinforce(qint64 ownerId, const QString& text, const QString& caseHash)
{
    const QStringList concepts = extractConcepts(text);
    if (concepts.isEmpty()) return;

    QList<qint64> nodeIds;
    nodeIds.reserve(concepts.size());
    for (const QString& c : concepts) {
        const qint64 id = findOrCreateNode(ownerId, c);
        if (id == 0) continue;
        nodeIds.append(id);
        if (!caseHash.isEmpty()) linkCase(id, caseHash, 1.0f);
    }

    // "Нейроны, активирующиеся вместе, связываются сильнее" — каждая пара
    // понятий, упомянутых в одном запросе/кейсе, получает более крепкую
    // связь. При kMaxConceptsPerQuery=10 это максимум 45 рёбер за вызов.
    for (int i = 0; i < nodeIds.size(); ++i)
        for (int j = i + 1; j < nodeIds.size(); ++j)
            linkEdge(ownerId, nodeIds[i], nodeIds[j], kHebbianIncrement);
}

void SynapseGraph::weaken(qint64 ownerId, const QString& text)
{
    const QStringList concepts = extractConcepts(text);
    if (concepts.size() < 2) return;

    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return;

    QList<qint64> nodeIds;
    for (const QString& c : concepts) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT id FROM synapse_nodes WHERE owner_id=:oid AND label=:l"));
        q.bindValue(QStringLiteral(":oid"), ownerId);
        q.bindValue(QStringLiteral(":l"), c);
        if (q.exec() && q.next())
            nodeIds.append(q.value(0).toLongLong());
    }

    // Ошибка → минус вместо плюс. Только для уже существующих узлов —
    // ослаблять то, чего никогда не было, бессмысленно.
    for (int i = 0; i < nodeIds.size(); ++i)
        for (int j = i + 1; j < nodeIds.size(); ++j)
            linkEdge(ownerId, nodeIds[i], nodeIds[j], -kHebbianDecrement);
}

// ============================================================
//  Spreading activation
// ============================================================

QList<SynapseGraph::ActivatedNode> SynapseGraph::spreadActivation(
    qint64 ownerId, const QStringList& seedConcepts) const
{
    QHash<qint64, ActivatedNode> visited;   // nodeId -> лучшая активация
    QList<ActivatedNode> frontier;

    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return {};

    // Сиды: только уже существующие узлы — активация растекается по
    // накопленному опыту, новых узлов здесь не создаём.
    for (const QString& concept : seedConcepts) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT id FROM synapse_nodes WHERE owner_id=:oid AND label=:l"));
        q.bindValue(QStringLiteral(":oid"), ownerId);
        q.bindValue(QStringLiteral(":l"), concept);
        if (!q.exec() || !q.next()) continue;

        ActivatedNode n;
        n.nodeId     = q.value(0).toLongLong();
        n.label      = concept;
        n.activation = 1.0f;
        n.hops       = 0;
        visited.insert(n.nodeId, n);
        frontier.append(n);
    }

    for (int hop = 1; hop <= kMaxHops; ++hop) {
        QList<ActivatedNode> next;
        for (const ActivatedNode& src : frontier) {
            if (src.activation < kActivationFloor) continue;

            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "SELECT node_a, node_b, weight FROM synapse_edges "
                "WHERE owner_id=:oid AND (node_a=:id OR node_b=:id) AND weight > 0"));
            q.bindValue(QStringLiteral(":oid"), ownerId);
            q.bindValue(QStringLiteral(":id"), src.nodeId);
            if (!q.exec()) continue;

            while (q.next()) {
                const qint64 a      = q.value(0).toLongLong();
                const qint64 b      = q.value(1).toLongLong();
                const float  weight = q.value(2).toFloat();
                const qint64 neighbor = (a == src.nodeId) ? b : a;

                const float normWeight = qMin(1.0f, weight / kEdgeWeightMax);
                const float activation = src.activation * normWeight * kHopDecay;
                if (activation < kActivationFloor) continue;

                auto it = visited.find(neighbor);
                if (it != visited.end() && it.value().activation >= activation)
                    continue;  // уже достигнут с активацией не хуже этой

                ActivatedNode n;
                n.nodeId     = neighbor;
                n.activation = activation;
                n.hops       = hop;
                visited.insert(neighbor, n);
                next.append(n);
            }
        }
        if (next.isEmpty()) break;
        frontier = next;
    }

    return visited.values();
}

// ============================================================
//  assemble() — сборка пазла из фрагментов
// ============================================================

SynapseGraph::Assembly SynapseGraph::assemble(
    qint64 ownerId, const QString& query, int maxFragments) const
{
    Assembly result;
    const QStringList concepts = extractConcepts(query);
    if (concepts.isEmpty()) return result;

    const QList<ActivatedNode> activated = spreadActivation(ownerId, concepts);
    if (activated.isEmpty()) {
        result.missingConcepts = concepts;
        return result;
    }

    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return result;

    // Суммируем score по кейсу = activation узла * вес привязки узел→кейс,
    // по всем засветившимся узлам — кейс, к которому ведёт несколько
    // связанных понятий сразу, набирает больше очков, чем случайное
    // совпадение по одному узлу.
    QHash<QString, float> caseScore;
    for (const ActivatedNode& n : activated) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT case_hash, weight FROM synapse_case_links WHERE node_id=:id"));
        q.bindValue(QStringLiteral(":id"), n.nodeId);
        if (!q.exec()) continue;
        while (q.next())
            caseScore[q.value(0).toString()] += n.activation * q.value(1).toFloat();
    }
    if (caseScore.isEmpty()) {
        result.missingConcepts = concepts;
        return result;
    }

    QList<QPair<QString, float>> ranked;
    ranked.reserve(caseScore.size());
    for (auto it = caseScore.constBegin(); it != caseScore.constEnd(); ++it)
        ranked.append({it.key(), it.value()});
    std::sort(ranked.begin(), ranked.end(),
              [](const QPair<QString, float>& a, const QPair<QString, float>& b) {
                  return a.second > b.second;
              });

    QSet<QString> covered;
    for (int i = 0; i < ranked.size() && result.fragments.size() < maxFragments; ++i) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT original_query, response_text FROM llm_cache WHERE query_hash=:h"));
        q.bindValue(QStringLiteral(":h"), ranked[i].first);
        if (!q.exec() || !q.next()) continue;

        Fragment frag;
        frag.caseHash   = ranked[i].first;
        frag.query      = q.value(0).toString();
        frag.response   = q.value(1).toString();
        frag.activation = ranked[i].second;

        int hits = 0;
        for (const QString& c : concepts) {
            if (frag.query.contains(c, Qt::CaseInsensitive)) {
                ++hits;
                covered.insert(c);
            }
        }
        frag.coverage = float(hits) / concepts.size();

        result.fragments.append(frag);
    }

    if (result.fragments.isEmpty()) {
        result.missingConcepts = concepts;
        return result;
    }

    for (const QString& c : concepts) {
        if (covered.contains(c)) result.coveredConcepts.append(c);
        else                     result.missingConcepts.append(c);
    }
    // "Уверенность = полнота сборки" — доля понятий запроса, для которых
    // нашёлся хоть один кусочек пазла.
    result.completeness = float(result.coveredConcepts.size()) / concepts.size();

    std::sort(result.fragments.begin(), result.fragments.end(),
              [](const Fragment& a, const Fragment& b) { return a.activation > b.activation; });

    return result;
}

// ============================================================
//  Introspection
// ============================================================

SynapseGraph::GraphStats SynapseGraph::stats(qint64 ownerId) const
{
    GraphStats s;
    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return s;

    QSqlQuery qn(db);
    qn.prepare(QStringLiteral("SELECT COUNT(*) FROM synapse_nodes WHERE owner_id=:oid"));
    qn.bindValue(QStringLiteral(":oid"), ownerId);
    if (qn.exec() && qn.next()) s.nodeCount = qn.value(0).toInt();

    QSqlQuery qe(db);
    qe.prepare(QStringLiteral("SELECT COUNT(*) FROM synapse_edges WHERE owner_id=:oid"));
    qe.bindValue(QStringLiteral(":oid"), ownerId);
    if (qe.exec() && qe.next()) s.edgeCount = qe.value(0).toInt();

    return s;
}
