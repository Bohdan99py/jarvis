// ============================================================
// case_distiller.cpp — Layer 2: distill repeated cases into heuristics
// ============================================================
#include "case_distiller.h"
#include "database_manager.h"
#include "llm_cache_manager.h"
#include "claude_api.h"
#include "curiosity_engine.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QVector>
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>

CaseDistiller::CaseDistiller(ClaudeApi* claudeApi, QObject* parent)
    : QObject(parent), m_claudeApi(claudeApi)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &CaseDistiller::onTimerTick);
}

void CaseDistiller::start()
{
    // Tick hourly; onTimerTick() itself decides (via a persisted timestamp)
    // whether it's actually been kMinHoursBetweenRuns since the last real
    // cycle, so this behaves like "nightly" regardless of how long the app
    // has been open for.
    m_timer->start(60 * 60 * 1000);
}

void CaseDistiller::stop()
{
    m_timer->stop();
}

void CaseDistiller::onTimerTick()
{
    const QDateTime last = QDateTime::fromString(
        DatabaseManager::instance().getConfig(QStringLiteral("heuristics_last_run"), QString()).toString(),
        Qt::ISODate);

    if (last.isValid() && last.secsTo(QDateTime::currentDateTime()) < kMinHoursBetweenRuns * 3600)
        return;

    runCycleNow();
}

void CaseDistiller::runCycleNow()
{
    DatabaseManager::instance().setConfig(QStringLiteral("heuristics_last_run"),
                                           QDateTime::currentDateTime().toString(Qt::ISODate));
    runCycle();
}

void CaseDistiller::runCycle()
{
    m_pendingClusters = buildClusters();
    m_createdThisCycle = 0;

    qDebug() << "[CaseDistiller] Starting cycle:" << m_pendingClusters.size() << "cluster(s) to distill.";
    processNextCluster();
}

QList<CaseDistiller::Cluster> CaseDistiller::buildClusters()
{
    QList<Cluster> allClusters;
    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return allClusters;

    QSqlQuery ownerQ(db);
    ownerQ.exec(QStringLiteral("SELECT DISTINCT owner_id FROM llm_cache WHERE heuristic_id IS NULL"));

    QList<qint64> ownerIds;
    while (ownerQ.next()) ownerIds.append(ownerQ.value(0).toLongLong());

    for (qint64 ownerId : ownerIds) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT query_hash, original_query, response_text FROM llm_cache "
            "WHERE owner_id = :oid AND heuristic_id IS NULL"));
        q.bindValue(QStringLiteral(":oid"), ownerId);
        if (!q.exec()) continue;

        QList<CaseRow> rows;
        while (q.next()) {
            CaseRow r;
            r.hash     = q.value(0).toString();
            r.query    = q.value(1).toString();
            r.response = q.value(2).toString();
            rows.append(r);
        }

        allClusters += clusterCases(ownerId, rows);
    }

    return allClusters;
}

QList<CaseDistiller::Cluster> CaseDistiller::clusterCases(qint64 ownerId, const QList<CaseRow>& rows)
{
    QList<Cluster> clusters;
    QVector<bool> used(rows.size(), false);

    for (int i = 0; i < rows.size(); ++i) {
        if (used[i]) continue;

        Cluster c;
        c.ownerId = ownerId;
        c.hashes    << rows[i].hash;
        c.queries   << rows[i].query;
        c.responses << rows[i].response;
        used[i] = true;

        for (int j = i + 1; j < rows.size(); ++j) {
            if (used[j]) continue;
            if (LlmCacheManager::keywordOverlap(rows[i].query, rows[j].query) >= kClusterOverlapThreshold) {
                c.hashes    << rows[j].hash;
                c.queries   << rows[j].query;
                c.responses << rows[j].response;
                used[j] = true;
            }
        }

        if (c.hashes.size() >= kMinClusterSize)
            clusters.append(c);
    }

    return clusters;
}

QString CaseDistiller::buildPrompt(const Cluster& c)
{
    QString prompt = QStringLiteral(
        "Ниже несколько похожих вопросов пользователя и мои прошлые ответы на них. "
        "Сформулируй ОДНО краткое общее правило (2-4 предложения), которым я мог бы "
        "руководствоваться, чтобы отвечать на подобные вопросы в будущем без обращения "
        "к тебе. Не повторяй сами вопросы. Также определи: это скорее МЕХАНИЧЕСКОЕ/"
        "фактическое правило (как что-то сделать, техническая инструкция) или "
        "МНЕНИЕ/позиция/предпочтение (оценочное суждение, которое может со временем "
        "смениться на основе новых данных)? Ответь СТРОГО в этом формате:\n"
        "TOPIC: <короткий тег темы, 2-4 слова>\n"
        "PRINCIPLE: <текст правила>\n"
        "IS_OPINION: <yes или no>\n\n");

    for (int i = 0; i < c.queries.size(); ++i) {
        prompt += QStringLiteral("[Вопрос %1] %2\n[Ответ %1] %3\n\n")
            .arg(i + 1)
            .arg(c.queries[i].left(300), c.responses[i].left(500));
    }

    return prompt;
}

void CaseDistiller::processNextCluster()
{
    if (m_pendingClusters.isEmpty()) {
        qDebug() << "[CaseDistiller] Clustering pass done. Heuristics created:" << m_createdThisCycle;
        startOpinionCheckPhase();
        return;
    }

    const Cluster c = m_pendingClusters.takeFirst();

    if (!m_claudeApi || !m_claudeApi->hasApiKey()) {
        // No API access — skip this cycle's remaining clusters rather than
        // spin; they stay un-distilled (heuristic_id IS NULL) and get
        // reconsidered on the next due cycle. Opinion checks need Claude
        // too, so there's no point starting that phase either.
        qDebug() << "[CaseDistiller] No Claude API key — skipping distillation.";
        m_pendingClusters.clear();
        emit cycleFinished(m_createdThisCycle);
        return;
    }

    const QString prompt = buildPrompt(c);
    m_claudeApi->sendMessage(prompt, [this, c](bool success, const QString& response) {
        if (success)
            storeHeuristic(c, response);
        else
            qWarning() << "[CaseDistiller] Claude call failed for cluster (owner"
                       << c.ownerId << "," << c.hashes.size() << "cases) — left un-distilled.";
        processNextCluster();
    });
}

void CaseDistiller::storeHeuristic(const Cluster& c, const QString& claudeResponse)
{
    static const QRegularExpression topicRe(QStringLiteral("TOPIC:\\s*(.+)"));
    // Non-greedy + stop at the IS_OPINION line (or end of string) — a
    // greedy match here would swallow the trailing IS_OPINION: line into
    // the principle text.
    static const QRegularExpression principleRe(
        QStringLiteral("PRINCIPLE:\\s*([\\s\\S]+?)(?:\\nIS_OPINION:|$)"));
    static const QRegularExpression isOpinionRe(
        QStringLiteral("IS_OPINION:\\s*(yes|no)"), QRegularExpression::CaseInsensitiveOption);

    QString topic     = c.queries.first().left(40);
    QString principle = claudeResponse.trimmed();
    bool    isOpinion = false;

    const auto tm = topicRe.match(claudeResponse);
    const auto pm = principleRe.match(claudeResponse);
    const auto om = isOpinionRe.match(claudeResponse);
    if (tm.hasMatch())     topic     = tm.captured(1).trimmed().left(60);
    if (pm.hasMatch())     principle = pm.captured(1).trimmed();
    if (om.hasMatch())     isOpinion = om.captured(1).toLower() == QStringLiteral("yes");

    if (principle.isEmpty()) {
        qWarning() << "[CaseDistiller] Empty principle from Claude — discarding cluster result.";
        return;
    }

    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return;

    QSqlQuery ins(db);
    ins.prepare(QStringLiteral(
        "INSERT INTO heuristics (owner_id, topic, principle, case_count) "
        "VALUES (:oid, :topic, :principle, :count)"));
    ins.bindValue(QStringLiteral(":oid"),       c.ownerId);
    ins.bindValue(QStringLiteral(":topic"),     topic);
    ins.bindValue(QStringLiteral(":principle"), principle);
    ins.bindValue(QStringLiteral(":count"),     c.hashes.size());

    if (!ins.exec()) {
        qWarning() << "[CaseDistiller] heuristics insert error:" << ins.lastError().text();
        return;
    }

    const QVariant newId = ins.lastInsertId();

    QString placeholders;
    for (int i = 0; i < c.hashes.size(); ++i) {
        if (i > 0) placeholders += QLatin1Char(',');
        placeholders += QStringLiteral(":h%1").arg(i);
    }

    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE llm_cache SET heuristic_id = :hid WHERE query_hash IN (%1)")
                    .arg(placeholders));
    upd.bindValue(QStringLiteral(":hid"), newId);
    for (int i = 0; i < c.hashes.size(); ++i)
        upd.bindValue(QStringLiteral(":h%1").arg(i), c.hashes[i]);
    upd.exec();

    ++m_createdThisCycle;
    qDebug() << "[CaseDistiller] New heuristic for owner" << c.ownerId
             << "topic:" << topic << "(" << c.hashes.size() << "cases folded in)";

    // Layer 3: a heuristic Claude judged to be a stance/preference (not a
    // purely mechanical rule) also becomes an opinion — every opinion has a
    // backing heuristic, but not every heuristic becomes an opinion.
    if (isOpinion) {
        QSqlQuery opIns(db);
        opIns.prepare(QStringLiteral(
            "INSERT INTO opinions (owner_id, topic, position, based_on_heuristic_id) "
            "VALUES (:oid, :topic, :position, :hid)"));
        opIns.bindValue(QStringLiteral(":oid"),      c.ownerId);
        opIns.bindValue(QStringLiteral(":topic"),    topic);
        opIns.bindValue(QStringLiteral(":position"), principle);
        opIns.bindValue(QStringLiteral(":hid"),      newId);

        if (!opIns.exec())
            qWarning() << "[CaseDistiller] opinions insert error:" << opIns.lastError().text();
        else
            qDebug() << "[CaseDistiller] New opinion for owner" << c.ownerId << "topic:" << topic;
    }
}

// ============================================================
//  Layer 3 — opinion contradiction checking
// ============================================================

void CaseDistiller::startOpinionCheckPhase()
{
    m_pendingOpinionChecks = buildOpinionChecks();

    if (m_pendingOpinionChecks.isEmpty()) {
        qDebug() << "[CaseDistiller] Cycle done. Heuristics created:" << m_createdThisCycle;
        emit cycleFinished(m_createdThisCycle);
        return;
    }

    qDebug() << "[CaseDistiller] Opinion check phase:" << m_pendingOpinionChecks.size()
             << "opinion(s) with new evidence.";
    processNextOpinionCheck();
}

QList<CaseDistiller::OpinionCheck> CaseDistiller::buildOpinionChecks()
{
    QList<OpinionCheck> checks;
    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return checks;

    QSqlQuery opQ(db);
    opQ.exec(QStringLiteral(
        "SELECT id, owner_id, topic, position, updated_at FROM opinions"));

    struct OpRow { qint64 id; qint64 ownerId; QString topic; QString position; QString updatedAt; };
    QList<OpRow> opinions;
    while (opQ.next()) {
        OpRow r;
        r.id        = opQ.value(0).toLongLong();
        r.ownerId   = opQ.value(1).toLongLong();
        r.topic     = opQ.value(2).toString();
        r.position  = opQ.value(3).toString();
        r.updatedAt = opQ.value(4).toString();
        opinions.append(r);
    }

    for (const auto& op : opinions) {
        // Any case for this owner added since the opinion was last touched
        // is candidate evidence — not restricted to un-clustered cases,
        // since a case can be relevant to an opinion's topic regardless of
        // whether the clustering pass above also folded it into a
        // heuristic this same cycle.
        QSqlQuery caseQ(db);
        caseQ.prepare(QStringLiteral(
            "SELECT original_query, response_text FROM llm_cache "
            "WHERE owner_id = :oid AND timestamp > :since"));
        caseQ.bindValue(QStringLiteral(":oid"),   op.ownerId);
        caseQ.bindValue(QStringLiteral(":since"), op.updatedAt);
        if (!caseQ.exec()) continue;

        QString bestQuery, bestResponse;
        float bestOverlap = 0.0f;
        while (caseQ.next()) {
            const QString q = caseQ.value(0).toString();
            const float overlap = LlmCacheManager::keywordOverlap(op.topic, q);
            if (overlap > bestOverlap) {
                bestOverlap  = overlap;
                bestQuery    = q;
                bestResponse = caseQ.value(1).toString();
            }
        }

        if (bestOverlap >= kClusterOverlapThreshold) {
            OpinionCheck check;
            check.opinionId    = op.id;
            check.ownerId      = op.ownerId;
            check.topic        = op.topic;
            check.position     = op.position;
            check.caseQuery    = bestQuery;
            check.caseResponse = bestResponse;
            checks.append(check);
        }
    }

    return checks;
}

QString CaseDistiller::buildOpinionCheckPrompt(const OpinionCheck& check)
{
    return QStringLiteral(
        "У меня есть текущая позиция по теме \"%1\": %2\n\n"
        "Новый случай:\n[Вопрос] %3\n[Ответ] %4\n\n"
        "Подтверждает этот новый случай текущую позицию, или противоречит ей? "
        "Ответь СТРОГО одним словом в первой строке — CONFIRM или CONTRADICT — "
        "затем с новой строки одно предложение объяснения.")
        .arg(check.topic, check.position,
             check.caseQuery.left(300), check.caseResponse.left(500));
}

void CaseDistiller::processNextOpinionCheck()
{
    if (m_pendingOpinionChecks.isEmpty()) {
        qDebug() << "[CaseDistiller] Cycle done. Heuristics created:" << m_createdThisCycle;
        emit cycleFinished(m_createdThisCycle);
        return;
    }

    const OpinionCheck check = m_pendingOpinionChecks.takeFirst();

    if (!m_claudeApi || !m_claudeApi->hasApiKey()) {
        qDebug() << "[CaseDistiller] No Claude API key — skipping opinion checks.";
        m_pendingOpinionChecks.clear();
        emit cycleFinished(m_createdThisCycle);
        return;
    }

    const QString prompt = buildOpinionCheckPrompt(check);
    m_claudeApi->sendMessage(prompt, [this, check](bool success, const QString& response) {
        if (success)
            applyOpinionCheckResult(check, response);
        else
            qWarning() << "[CaseDistiller] Opinion check Claude call failed for opinion"
                       << check.opinionId << "— left unchanged.";
        processNextOpinionCheck();
    });
}

void CaseDistiller::applyOpinionCheckResult(const OpinionCheck& check, const QString& claudeResponse)
{
    const QString firstLine = claudeResponse.trimmed()
                                   .section(QChar('\n'), 0, 0).trimmed().toUpper();
    const bool confirmed    = firstLine.startsWith(QStringLiteral("CONFIRM"));
    const bool contradicted = firstLine.startsWith(QStringLiteral("CONTRADICT"));

    if (!confirmed && !contradicted) {
        qWarning() << "[CaseDistiller] Opinion check: unparseable Claude reply for opinion"
                   << check.opinionId << "— left unchanged.";
        return;
    }

    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return;

    QSqlQuery cur(db);
    cur.prepare(QStringLiteral(
        "SELECT confidence, confirmations, contradictions FROM opinions WHERE id = :id"));
    cur.bindValue(QStringLiteral(":id"), check.opinionId);
    if (!cur.exec() || !cur.next()) return;

    float confidence     = cur.value(0).toFloat();
    int   confirmations  = cur.value(1).toInt();
    int   contradictions = cur.value(2).toInt();

    if (confirmed) {
        ++confirmations;
        confidence = qMin(1.0f, confidence + 0.1f);
    } else {
        ++contradictions;
        confidence = qMax(0.0f, confidence - 0.25f);
    }

    QSqlQuery upd(db);
    upd.prepare(QStringLiteral(
        "UPDATE opinions SET confidence = :c, confirmations = :cf, "
        "contradictions = :ct, updated_at = datetime('now') WHERE id = :id"));
    upd.bindValue(QStringLiteral(":c"),  confidence);
    upd.bindValue(QStringLiteral(":cf"), confirmations);
    upd.bindValue(QStringLiteral(":ct"), contradictions);
    upd.bindValue(QStringLiteral(":id"), check.opinionId);
    upd.exec();

    qDebug() << "[CaseDistiller] Opinion" << check.opinionId << (confirmed ? "CONFIRMED" : "CONTRADICTED")
             << "— confidence now" << confidence;

    // A single contradiction just nudges confidence down quietly (the
    // position might still be right, one counter-example isn't proof).
    // Enough accumulated doubt — ask the owner instead of silently
    // flipping or silently ignoring it.
    if (contradicted && (confidence < kOpinionRevisionConfidenceFloor
                          || contradictions >= kOpinionRevisionContradictionThreshold)) {
        const QString question = QStringLiteral(
            "Кажется, моё мнение о \"%1\" могло измениться. Раньше: %2\n"
            "Но недавно: %3 → %4\n"
            "Обновить позицию?")
            .arg(check.topic, check.position,
                 check.caseQuery.left(200), check.caseResponse.left(300));
        CuriosityEngine::instance().postOpinionRevisionQuestion(
            check.ownerId, check.opinionId, question);
    }
}
