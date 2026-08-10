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

qint64 SynapseGraph::findOrCreateNode(qint64 ownerId, const QString& label,
                                      const QString& source)
{
    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return 0;

    QSqlQuery up(db);
    // source is set on INSERT only, never in the DO UPDATE branch: the column
    // answers "where did this concept come from", so the first channel to
    // teach it owns that answer. Overwriting it on every reinforcement would
    // turn origin into "whatever touched it last", which is not a fact about
    // the concept at all.
    up.prepare(QStringLiteral(
        "INSERT INTO synapse_nodes (owner_id, label, activations, source, created_at, last_activated_at) "
        "VALUES (:oid, :l, 1, :src, datetime('now'), datetime('now')) "
        "ON CONFLICT(owner_id, label) DO UPDATE SET "
        "  activations = activations + 1, "
        "  last_activated_at = datetime('now')"));
    up.bindValue(QStringLiteral(":oid"), ownerId);
    up.bindValue(QStringLiteral(":l"), label);
    up.bindValue(QStringLiteral(":src"),
                 source.isEmpty() ? QStringLiteral("dialogue") : source);
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

void SynapseGraph::reinforce(qint64 ownerId, const QString& text,
                             const QString& caseHash, const QString& source)
{
    const QStringList concepts = extractConcepts(text);
    if (concepts.isEmpty()) return;

    // Один вызов = до 10 upsert'ов узлов + до 45 рёбер + привязки кейсов,
    // то есть ~55 отдельных запросов. В autocommit каждый из них — сам себе
    // транзакция со своим fsync, и обучение упирается в диск, а не в логику.
    // Одна транзакция на весь эпизод: он и логически атомарен — либо связь
    // "эти понятия сработали вместе" записана целиком, либо не записана.
    auto db = DatabaseManager::instance().connection();
    const bool inTx = db.isOpen() && db.transaction();

    QList<qint64> nodeIds;
    nodeIds.reserve(concepts.size());
    for (const QString& c : concepts) {
        const qint64 id = findOrCreateNode(ownerId, c, source);
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

    // transaction() не удалась — значит соединение уже внутри чужой
    // транзакции; коммитить её здесь нельзя, это работа вызывающего.
    if (inTx) db.commit();
}

void SynapseGraph::weaken(qint64 ownerId, const QString& text)
{
    const QStringList concepts = extractConcepts(text);
    if (concepts.size() < 2) return;

    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return;

    QList<qint64> nodeIds;
    // Одно подготовленное выражение на все понятия вместо нового QSqlQuery
    // с повторным prepare() в каждой итерации.
    QSqlQuery lookup(db);
    lookup.prepare(QStringLiteral(
        "SELECT id FROM synapse_nodes WHERE owner_id=:oid AND label=:l"));
    for (const QString& c : concepts) {
        lookup.bindValue(QStringLiteral(":oid"), ownerId);
        lookup.bindValue(QStringLiteral(":l"), c);
        if (lookup.exec() && lookup.next())
            nodeIds.append(lookup.value(0).toLongLong());
    }
    if (nodeIds.size() < 2) return;

    const bool inTx = db.transaction();

    // Ошибка → минус вместо плюс. Только для уже существующих узлов —
    // ослаблять то, чего никогда не было, бессмысленно.
    for (int i = 0; i < nodeIds.size(); ++i)
        for (int j = i + 1; j < nodeIds.size(); ++j)
            linkEdge(ownerId, nodeIds[i], nodeIds[j], -kHebbianDecrement);

    if (inTx) db.commit();
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

SynapseGraph::GraphView SynapseGraph::graphView(qint64 ownerId, int maxNodes) const
{
    GraphView view;
    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return view;

    // Strongest concepts first — a picture of everything is a hairball, and
    // the tail of once-seen tokens carries no association worth drawing.
    QSqlQuery nq(db);
    nq.prepare(QStringLiteral(
        "SELECT id, label, activations, source FROM synapse_nodes WHERE owner_id=:oid "
        "ORDER BY activations DESC LIMIT :lim"));
    nq.bindValue(QStringLiteral(":oid"), ownerId);
    nq.bindValue(QStringLiteral(":lim"), maxNodes);
    if (!nq.exec()) return view;

    QHash<qint64, int> indexById;
    QList<ViewNode> candidates;
    while (nq.next()) {
        ViewNode n;
        n.id          = nq.value(0).toLongLong();
        n.label       = nq.value(1).toString();
        n.activations = nq.value(2).toInt();
        n.source      = nq.value(3).toString();
        if (n.source.isEmpty()) n.source = QString::fromLatin1(kSourceDialogue);
        indexById.insert(n.id, candidates.size());
        candidates.append(n);
    }
    if (candidates.isEmpty()) return view;

    // Every synapse for those nodes; the both-endpoints-present test happens
    // here rather than in SQL so the id list stays out of the query text.
    QSqlQuery eq(db);
    eq.prepare(QStringLiteral(
        "SELECT node_a, node_b, weight, co_activations FROM synapse_edges "
        "WHERE owner_id=:oid ORDER BY weight DESC"));
    eq.bindValue(QStringLiteral(":oid"), ownerId);
    if (!eq.exec()) return view;

    // Collected against candidate indices first, then compacted below.
    QList<ViewEdge> rawEdges;
    while (eq.next()) {
        const auto ia = indexById.constFind(eq.value(0).toLongLong());
        const auto ib = indexById.constFind(eq.value(1).toLongLong());
        if (ia == indexById.constEnd() || ib == indexById.constEnd()) continue;

        ViewEdge e;
        e.a             = *ia;
        e.b             = *ib;
        e.weight        = eq.value(2).toFloat();
        e.coActivations = eq.value(3).toInt();
        view.maxWeight  = qMax(view.maxWeight, e.weight);
        candidates[e.a].degree++;
        candidates[e.b].degree++;
        rawEdges.append(e);
    }

    // Drop concepts with no synapse in this view. A node the layout cannot
    // attach to anything gets pushed to the rim by pure repulsion and reads
    // as debris around the network — and it is the least informative thing
    // on screen, since a concept with no association is precisely the one
    // carrying no association to show. They stay in the database and in the
    // node count; this only decides what is worth drawing.
    QList<int> remap;
    remap.reserve(candidates.size());
    for (const ViewNode& n : candidates) {
        if (n.degree > 0) {
            remap.append(view.nodes.size());
            view.maxActivations = qMax(view.maxActivations, n.activations);
            view.nodes.append(n);
        } else {
            remap.append(-1);
        }
    }
    for (const ViewEdge& e : rawEdges) {
        const int a = remap.value(e.a, -1);
        const int b = remap.value(e.b, -1);
        if (a < 0 || b < 0) continue;   // unreachable: an edge kept both ends
        ViewEdge out = e;
        out.a = a;
        out.b = b;
        view.edges.append(out);
    }
    return view;
}

QList<SynapseGraph::TopNode> SynapseGraph::topNodes(qint64 ownerId, int limit) const
{
    QList<TopNode> out;
    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return out;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT label, activations FROM synapse_nodes WHERE owner_id=:oid "
        "ORDER BY activations DESC LIMIT :lim"));
    q.bindValue(QStringLiteral(":oid"), ownerId);
    q.bindValue(QStringLiteral(":lim"), limit);
    if (!q.exec()) return out;

    while (q.next()) {
        TopNode n;
        n.label       = q.value(0).toString();
        n.activations = q.value(1).toInt();
        out.append(n);
    }
    return out;
}

QList<SynapseGraph::TopEdge> SynapseGraph::topEdges(qint64 ownerId, int limit) const
{
    QList<TopEdge> out;
    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return out;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT na.label, nb.label, e.weight, e.co_activations "
        "FROM synapse_edges e "
        "JOIN synapse_nodes na ON na.id = e.node_a "
        "JOIN synapse_nodes nb ON nb.id = e.node_b "
        "WHERE e.owner_id=:oid ORDER BY e.weight DESC LIMIT :lim"));
    q.bindValue(QStringLiteral(":oid"), ownerId);
    q.bindValue(QStringLiteral(":lim"), limit);
    if (!q.exec()) return out;

    while (q.next()) {
        TopEdge e;
        e.labelA        = q.value(0).toString();
        e.labelB        = q.value(1).toString();
        e.weight        = q.value(2).toFloat();
        e.coActivations = q.value(3).toInt();
        out.append(e);
    }
    return out;
}

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
