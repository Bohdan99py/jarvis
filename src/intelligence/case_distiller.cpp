// ============================================================
// case_distiller.cpp — Layer 2: distill repeated cases into heuristics
// ============================================================
#include "case_distiller.h"
#include "database_manager.h"
#include "llm_cache_manager.h"
#include "claude_api.h"

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

    if (m_pendingClusters.isEmpty()) {
        qDebug() << "[CaseDistiller] No qualifying clusters this cycle.";
        emit cycleFinished(0);
        return;
    }

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
        "к тебе. Не повторяй сами вопросы. Ответь СТРОГО в этом формате:\n"
        "TOPIC: <короткий тег темы, 2-4 слова>\n"
        "PRINCIPLE: <текст правила>\n\n");

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
        qDebug() << "[CaseDistiller] Cycle done. Heuristics created:" << m_createdThisCycle;
        emit cycleFinished(m_createdThisCycle);
        return;
    }

    const Cluster c = m_pendingClusters.takeFirst();

    if (!m_claudeApi || !m_claudeApi->hasApiKey()) {
        // No API access — skip this cycle's remaining clusters rather than
        // spin; they stay un-distilled (heuristic_id IS NULL) and get
        // reconsidered on the next due cycle.
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
    static const QRegularExpression principleRe(QStringLiteral("PRINCIPLE:\\s*([\\s\\S]+)"));

    QString topic     = c.queries.first().left(40);
    QString principle = claudeResponse.trimmed();

    const auto tm = topicRe.match(claudeResponse);
    const auto pm = principleRe.match(claudeResponse);
    if (tm.hasMatch())     topic     = tm.captured(1).trimmed().left(60);
    if (pm.hasMatch())     principle = pm.captured(1).trimmed();

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
}
