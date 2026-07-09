// ============================================================
// synonym_learner.cpp — Self-taught synonym/paraphrase dictionary
// ============================================================

#include "synonym_learner.h"
#include "database_manager.h"

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QDebug>

SynonymLearner& SynonymLearner::instance()
{
    static SynonymLearner inst;
    return inst;
}

SynonymLearner::SynonymLearner(QObject* parent) : QObject(parent)
{
    ensureTable();
}

void SynonymLearner::ensureTable()
{
    if (!DatabaseManager::instance().isOpen()) return;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS learned_synonyms ("
        "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  term       TEXT NOT NULL,"
        "  synonym    TEXT NOT NULL,"
        "  confidence REAL NOT NULL DEFAULT 0.6,"
        "  learned_at TEXT NOT NULL DEFAULT (datetime('now')),"
        "  UNIQUE(term, synonym)"
        ")"));
    if (q.lastError().isValid())
        qWarning() << "[SynonymLearner] Table creation error:" << q.lastError().text();
}

QStringList SynonymLearner::expand(const QString& term) const
{
    QStringList result;
    if (!DatabaseManager::instance().isOpen()) return result;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return result;

    const QString lower = term.trimmed().toLower();
    if (lower.isEmpty()) return result;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT term, synonym FROM learned_synonyms "
        "WHERE LOWER(term) = :t OR LOWER(synonym) = :t"));
    q.bindValue(QStringLiteral(":t"), lower);
    if (!q.exec()) return result;

    while (q.next()) {
        const QString t = q.value(0).toString().toLower();
        const QString s = q.value(1).toString().toLower();
        const QString other = (t == lower) ? s : t;
        if (!other.isEmpty() && other != lower && !result.contains(other))
            result.append(other);
    }
    return result;
}

QStringList SynonymLearner::expandAll(const QStringList& terms) const
{
    QStringList result = terms;
    for (const QString& term : terms) {
        for (const QString& syn : expand(term)) {
            if (!result.contains(syn, Qt::CaseInsensitive))
                result.append(syn);
        }
    }
    return result;
}

void SynonymLearner::learn(const QString& term, const QString& synonym, float confidence)
{
    const QString t = term.trimmed().toLower();
    const QString s = synonym.trimmed().toLower();
    if (t.isEmpty() || s.isEmpty() || t == s) return;

    if (!DatabaseManager::instance().isOpen()) return;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO learned_synonyms (term, synonym, confidence) "
        "VALUES (:t, :s, :c) "
        "ON CONFLICT(term, synonym) DO UPDATE SET "
        "  confidence = MIN(1.0, confidence + 0.1)"));
    q.bindValue(QStringLiteral(":t"), t);
    q.bindValue(QStringLiteral(":s"), s);
    q.bindValue(QStringLiteral(":c"), confidence);
    if (!q.exec())
        qWarning() << "[SynonymLearner] learn() failed:" << q.lastError().text();
    else
        qDebug() << "[SynonymLearner] Learned:" << t << "<->" << s;
}

// ============================================================
//  Pending clarification
// ============================================================

QString SynonymLearner::askForClarification(const QString& term, bool english)
{
    m_pendingTerm      = term.trimmed().toLower();
    m_pendingTimestamp = QDateTime::currentDateTime();

    return english
        ? QStringLiteral("I didn't find anything for \"%1\". Is that another way of "
                         "saying something we've already talked about? Tell me what "
                         "it means (or just \"не знаю\"/\"don't know\") and I'll remember.")
              .arg(term)
        : QStringLiteral("Не нашёл ничего по «%1». Это то же самое, что мы уже "
                         "обсуждали, просто другими словами? Объясни в двух словах "
                         "(или напиши «не знаю») — запомню на будущее.")
              .arg(term);
}

bool SynonymLearner::hasPendingClarification() const
{
    if (m_pendingTerm.isEmpty()) return false;
    return m_pendingTimestamp.secsTo(QDateTime::currentDateTime())
        <= PENDING_WINDOW_MINUTES * 60;
}

bool SynonymLearner::consumeClarification(const QString& answerText)
{
    if (!hasPendingClarification()) {
        m_pendingTerm.clear();
        return false;
    }

    const QString term = m_pendingTerm;
    m_pendingTerm.clear();

    const QString lower = answerText.trimmed().toLower();
    static const QStringList kDontKnow = {
        QStringLiteral("не знаю"), QStringLiteral("незнаю"), QStringLiteral("не помню"),
        QStringLiteral("don't know"), QStringLiteral("dont know"), QStringLiteral("no idea"),
    };
    for (const QString& phrase : kDontKnow) {
        if (lower.contains(phrase)) return true; // consumed, nothing learned
    }

    learn(term, answerText.trimmed());
    return true;
}
