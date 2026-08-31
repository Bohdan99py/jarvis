// ============================================================
// sentence_composer.cpp — собственный словарь фраз
// ============================================================

#include "sentence_composer.h"
#include "database_manager.h"
#include "synapse_graph.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QRegularExpression>
#include <QSet>
#include <QDebug>

#include <algorithm>

SentenceComposer& SentenceComposer::instance()
{
    static SentenceComposer inst;
    return inst;
}

SentenceComposer::SentenceComposer(QObject* parent) : QObject(parent) {}

// ── Разбиение на предложения ────────────────────────────────────────
QStringList SentenceComposer::splitSentences(const QString& text)
{
    // Режем по .!? с последующим пробелом/концом, а также по переводам
    // строк: в ответах модели пункты списка часто не заканчиваются
    // точкой, но являются самостоятельными фразами.
    static const QRegularExpression sep(
        QStringLiteral(R"((?<=[.!?])\s+|\n+)"));

    QStringList out;
    for (QString s : text.split(sep, Qt::SkipEmptyParts)) {
        s = s.trimmed();
        // Маркеры списков и заголовков — мусор внутри фразы.
        s.remove(QRegularExpression(QStringLiteral(R"(^[\-\*•\d\.\)\s]{1,6})")));
        s = s.trimmed();
        if (s.length() >= kMinSentenceChars && s.length() <= kMaxSentenceChars)
            out << s;
    }
    return out;
}

// ── Пригодность фразы к переиспользованию ───────────────────────────
bool SentenceComposer::isSelfContained(const QString& sentence)
{
    const QString lower = sentence.trimmed().toLower();

    // Фраза, начинающаяся со связки или местоимения, опирается на
    // предыдущую. Вырванная из контекста, она отвечает не на тот вопрос,
    // а иногда прямо противоположное ("Но это не так").
    static const QStringList danglingStarts = {
        QStringLiteral("но "),      QStringLiteral("однако "),
        QStringLiteral("поэтому "), QStringLiteral("значит "),
        QStringLiteral("это "),     QStringLiteral("этот "),
        QStringLiteral("эта "),     QStringLiteral("эти "),
        QStringLiteral("он "),      QStringLiteral("она "),
        QStringLiteral("они "),     QStringLiteral("там "),
        QStringLiteral("тогда "),   QStringLiteral("тоже "),
        QStringLiteral("также "),   QStringLiteral("кроме того"),
        QStringLiteral("во-первых"),QStringLiteral("во-вторых"),
        QStringLiteral("but "),     QStringLiteral("however "),
        QStringLiteral("therefore "),QStringLiteral("this "),
        QStringLiteral("that "),    QStringLiteral("it "),
        QStringLiteral("they "),    QStringLiteral("also "),
        QStringLiteral("first,"),   QStringLiteral("second,"),
    };
    for (const QString& d : danglingStarts)
        if (lower.startsWith(d)) return false;

    // Вопрос в ответе — это переспрос модели, а не знание.
    if (lower.endsWith(QLatin1Char('?'))) return false;

    // Куски кода/разметки читаются как шум в собранном тексте.
    if (sentence.contains(QStringLiteral("```")) ||
        sentence.contains(QStringLiteral("[CMD:")) ||
        sentence.contains(QStringLiteral("[KICAD_SCH")))
        return false;

    return true;
}

// ── Обучение ────────────────────────────────────────────────────────
void SentenceComposer::learn(qint64 ownerId, const QString& response,
                             const QString& sourceHash)
{
    if (response.trimmed().isEmpty()) return;

    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return;

    const QStringList sentences = splitSentences(response);
    if (sentences.isEmpty()) return;

    // Одна транзакция на ответ: десяток фраз — это десяток upsert'ов, и
    // в autocommit каждый был бы отдельным коммитом с fsync.
    const bool inTx = db.transaction();

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO answer_sentences (owner_id, sentence, concepts, source_hash) "
        "VALUES (:oid, :s, :c, :h) "
        "ON CONFLICT(owner_id, sentence) DO UPDATE SET "
        "  uses = answer_sentences.uses + 1"));

    for (const QString& s : sentences) {
        if (!isSelfContained(s)) continue;
        const QStringList concepts = SynapseGraph::extractConcepts(s);
        if (concepts.isEmpty()) continue;

        q.bindValue(QStringLiteral(":oid"), ownerId);
        q.bindValue(QStringLiteral(":s"),   s);
        q.bindValue(QStringLiteral(":c"),   concepts.join(QLatin1Char(' ')));
        q.bindValue(QStringLiteral(":h"),   sourceHash);
        if (!q.exec())
            qWarning() << "[SentenceComposer] insert failed:" << q.lastError().text();
    }

    if (inTx) db.commit();
}

// ── Сборка ──────────────────────────────────────────────────────────
SentenceComposer::Composition SentenceComposer::compose(qint64 ownerId,
                                                         const QString& query,
                                                         int maxSentences) const
{
    Composition result;

    const QStringList queryConcepts = SynapseGraph::extractConcepts(query);
    if (queryConcepts.isEmpty()) return result;

    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return result;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT sentence, concepts FROM answer_sentences WHERE owner_id=:oid"));
    q.bindValue(QStringLiteral(":oid"), ownerId);
    if (!q.exec()) return result;

    const QSet<QString> wanted(queryConcepts.begin(), queryConcepts.end());

    struct Scored {
        QString         sentence;
        QSet<QString>   hits;
        float           score = 0.0f;
    };
    QList<Scored> ranked;

    while (q.next()) {
        const QString sentence = q.value(0).toString();
        const QStringList cs = q.value(1).toString()
                                   .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QSet<QString> hits;
        for (const QString& c : cs)
            if (wanted.contains(c)) hits.insert(c);
        if (hits.isEmpty()) continue;

        Scored s;
        s.sentence = sentence;
        s.hits     = hits;
        // Доля понятий вопроса, закрытых фразой, слегка штрафуемая за
        // многословие: короткая фраза, отвечающая по делу, лучше
        // абзаца, задевшего тему по касательной.
        s.score = float(hits.size()) / float(wanted.size())
                * (1.0f - qMin(0.4f, sentence.length() / 2000.0f));
        ranked.append(s);
    }
    if (ranked.isEmpty()) return result;

    std::sort(ranked.begin(), ranked.end(),
              [](const Scored& a, const Scored& b) { return a.score > b.score; });

    // Жадный отбор по НОВЫМ понятиям: две фразы об одном и том же не
    // складываются в ответ, они просто повторяются разными словами.
    QSet<QString> covered;
    QStringList   picked;
    for (const Scored& s : ranked) {
        if (picked.size() >= maxSentences) break;
        const QSet<QString> fresh = s.hits - covered;
        if (fresh.isEmpty()) continue;
        picked << s.sentence;
        covered += s.hits;
    }
    if (picked.isEmpty()) return result;

    result.coverage = float(covered.size()) / float(wanted.size());
    if (result.coverage < kMinCoverage) return result;  // лучше молчать

    result.usedSentences = picked;
    result.text = picked.join(QStringLiteral(" "));
    return result;
}

int SentenceComposer::sentenceCount(qint64 ownerId) const
{
    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return 0;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM answer_sentences WHERE owner_id=:oid"));
    q.bindValue(QStringLiteral(":oid"), ownerId);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}
