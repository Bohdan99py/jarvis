// ============================================================
// self_journal.cpp — JARVIS Self-Reflection Journal
// ============================================================

#include "self_journal.h"
#include "database_manager.h"
#include "memory_consolidation.h"

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QDebug>

// ============================================================
//  Singleton
// ============================================================

SelfJournal& SelfJournal::instance()
{
    static SelfJournal inst;
    return inst;
}

SelfJournal::SelfJournal(QObject* parent)
    : QObject(parent)
{
    m_journalPath = QStringLiteral("D:/Jarvis/self_journal.md");
}

SelfJournal::~SelfJournal() = default;

void SelfJournal::setJournalPath(const QString& path)
{
    m_journalPath = path;
}

// ============================================================
//  Database table — local Tier 2 mirror of doubts
// ============================================================

void SelfJournal::ensureTable()
{
    if (!DatabaseManager::instance().isOpen()) return;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS self_doubts ("
        "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  content      TEXT NOT NULL,"
        "  doubt_reason TEXT NOT NULL,"
        "  confidence   REAL NOT NULL DEFAULT 0.5,"
        "  source_ref   TEXT DEFAULT '',"
        "  resolved     INTEGER NOT NULL DEFAULT 0,"
        "  resolution   TEXT DEFAULT '',"
        "  created_at   TEXT NOT NULL DEFAULT (datetime('now')),"
        "  resolved_at  TEXT"
        ")"));

    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_doubt_resolved "
        "ON self_doubts(resolved, confidence)"));
}

// ============================================================
//  Doubt logging
// ============================================================

void SelfJournal::logDoubt(const QString& content,
                            const QString& reason,
                            double confidence,
                            const QString& sourceRef)
{
    if (!DatabaseManager::instance().isOpen()) return;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO self_doubts (content, doubt_reason, confidence, source_ref) "
        "VALUES (:ct, :dr, :conf, :src)"));
    q.bindValue(QStringLiteral(":ct"),   content.left(500));
    q.bindValue(QStringLiteral(":dr"),   reason.left(300));
    q.bindValue(QStringLiteral(":conf"), qBound(0.0, confidence, 1.0));
    q.bindValue(QStringLiteral(":src"),  sourceRef.left(500));

    if (q.exec()) {
        emit doubtLogged(content, confidence);
        qDebug() << "[SelfJournal] Doubt logged (conf:"
                 << confidence << "):" << content.left(80);
    }
}

void SelfJournal::logLearning(const QString& content,
                               double confidence,
                               const QString& sourceRef)
{
    if (confidence < 0.75) {
        logDoubt(content,
                 QStringLiteral("confidence below threshold during learning"),
                 confidence, sourceRef);
        return;
    }

    // High-confidence learning → memory_stream event
    DbMemoryEvent ev;
    ev.userId    = 1;
    ev.eventType = QStringLiteral("pdf_learning");
    ev.content   = content.left(500);
    ev.importance = confidence;
    DatabaseManager::instance().addMemoryEvent(ev);
}

// ============================================================
//  Doubt queries — always from local SQLite
// ============================================================

QList<DoubtEntry> SelfJournal::unresolvedDoubts(int limit) const
{
    QList<DoubtEntry> result;
    if (!DatabaseManager::instance().isOpen()) return result;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, content, doubt_reason, confidence, source_ref, created_at "
        "FROM self_doubts WHERE resolved = 0 "
        "ORDER BY confidence ASC, created_at DESC LIMIT :lim"));
    q.bindValue(QStringLiteral(":lim"), limit);

    if (q.exec()) {
        while (q.next()) {
            DoubtEntry d;
            d.id          = q.value(0).toLongLong();
            d.content     = q.value(1).toString();
            d.doubtReason = q.value(2).toString();
            d.confidence  = q.value(3).toDouble();
            d.sourceRef   = q.value(4).toString();
            d.createdAt   = QDateTime::fromString(q.value(5).toString(), Qt::ISODate);
            d.resolved    = false;
            result.append(d);
        }
    }
    return result;
}

QList<DoubtEntry> SelfJournal::topDoubtsForVerification(int limit) const
{
    QList<DoubtEntry> result;
    if (!DatabaseManager::instance().isOpen()) return result;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    QSqlQuery q(db);
    // Prioritize: lowest confidence first, but not too old (last 7 days)
    q.prepare(QStringLiteral(
        "SELECT id, content, doubt_reason, confidence, source_ref, created_at "
        "FROM self_doubts "
        "WHERE resolved = 0 AND created_at > datetime('now', '-7 days') "
        "ORDER BY confidence ASC LIMIT :lim"));
    q.bindValue(QStringLiteral(":lim"), limit);

    if (q.exec()) {
        while (q.next()) {
            DoubtEntry d;
            d.id          = q.value(0).toLongLong();
            d.content     = q.value(1).toString();
            d.doubtReason = q.value(2).toString();
            d.confidence  = q.value(3).toDouble();
            d.sourceRef   = q.value(4).toString();
            d.createdAt   = QDateTime::fromString(q.value(5).toString(), Qt::ISODate);
            result.append(d);
        }
    }
    return result;
}

int SelfJournal::unresolvedDoubtCount() const
{
    if (!DatabaseManager::instance().isOpen()) return 0;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    QSqlQuery q(db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM self_doubts WHERE resolved = 0"));
    return q.next() ? q.value(0).toInt() : 0;
}

// ============================================================
//  Doubt resolution — user confirms or corrects
// ============================================================

void SelfJournal::resolveDoubt(qint64 doubtId, bool correct,
                                const QString& userCorrection)
{
    if (!DatabaseManager::instance().isOpen()) return;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE self_doubts SET resolved = 1, "
        "resolution = :res, resolved_at = datetime('now') "
        "WHERE id = :id"));
    q.bindValue(QStringLiteral(":res"),
                correct ? QStringLiteral("Confirmed correct by user")
                        : userCorrection);
    q.bindValue(QStringLiteral(":id"), doubtId);

    if (q.exec())
        emit doubtResolved(doubtId, correct);
}

// ============================================================
//  Reflection cycle — write markdown journal entry
// ============================================================

int SelfJournal::nextCycleNumber() const
{
    ++const_cast<SelfJournal*>(this)->m_cycleCount;
    return m_cycleCount;
}

void SelfJournal::writeReflectionCycle()
{
    const QString md = buildReflectionMarkdown();
    if (md.isEmpty()) return;

    appendToJournalFile(md);
    emit reflectionWritten(m_journalPath);
}

QString SelfJournal::buildReflectionMarkdown() const
{
    // Gather recent high-confidence learnings from memory_stream
    auto topEvents = DatabaseManager::instance().getTopMemoryEvents(1, 10);

    // Gather unresolved doubts
    auto doubts = unresolvedDoubts(15);

    // Gather recently resolved
    QList<DoubtEntry> resolved;
    {
        auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
        if (db.isOpen()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "SELECT id, content, doubt_reason, confidence, source_ref, "
                "       resolution, resolved_at "
                "FROM self_doubts WHERE resolved = 1 "
                "AND resolved_at > datetime('now', '-24 hours') "
                "ORDER BY resolved_at DESC LIMIT 10"));
            if (q.exec()) {
                while (q.next()) {
                    DoubtEntry d;
                    d.id          = q.value(0).toLongLong();
                    d.content     = q.value(1).toString();
                    d.doubtReason = q.value(2).toString();
                    d.confidence  = q.value(3).toDouble();
                    d.sourceRef   = q.value(4).toString();
                    d.resolved    = true;
                    d.resolution  = q.value(5).toString();
                    resolved.append(d);
                }
            }
        }
    }

    if (topEvents.isEmpty() && doubts.isEmpty() && resolved.isEmpty())
        return QString();

    QString md;
    QTextStream out(&md);

    const int cycle = nextCycleNumber();
    const QString ts = QDateTime::currentDateTime().toString(Qt::ISODate);

    out << "\n---\n\n";
    out << "## " << ts << " — Learning Cycle #" << cycle << "\n\n";

    // Section 1: What I Learned
    out << "### What I Learned\n\n";
    if (topEvents.isEmpty()) {
        out << "_No new learnings in this cycle._\n\n";
    } else {
        for (const DbMemoryEvent& ev : topEvents) {
            out << "- **[" << ev.eventType << "]** "
                << ev.content.left(150)
                << " _(importance: " << QString::number(ev.importance, 'f', 2) << ")_\n";
        }
        out << "\n";
    }

    // Section 2: What I Am Confident About
    out << "### What I Am Confident About\n\n";
    if (resolved.isEmpty()) {
        out << "_No verifications in this cycle._\n\n";
    } else {
        for (const DoubtEntry& d : resolved) {
            out << "- ✅ " << d.content.left(120)
                << " — _" << d.resolution << "_\n";
        }
        out << "\n";
    }

    // Section 3: What If I Am Wrong
    out << "### ⚠️ What If I Am Wrong\n\n";
    if (doubts.isEmpty()) {
        out << "_No active doubts. All learnings verified or high-confidence._\n\n";
    } else {
        for (const DoubtEntry& d : doubts) {
            out << "- ❓ **\"" << d.content.left(120) << "\"**\n";
            out << "  - Confidence: " << QString::number(d.confidence, 'f', 2) << "\n";
            out << "  - Doubt: _" << d.doubtReason << "_\n";
            if (!d.sourceRef.isEmpty())
                out << "  - Source: `" << QFileInfo(d.sourceRef).fileName() << "`\n";
            out << "\n";
        }
    }

    return md;
}

void SelfJournal::appendToJournalFile(const QString& markdown)
{
    // Write to external drive if available
    if (!m_journalPath.isEmpty()) {
        const QString dir = QFileInfo(m_journalPath).absolutePath();
        QDir().mkpath(dir);

        QFile file(m_journalPath);

        // Create header on first write
        if (!file.exists()) {
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << "# J.A.R.V.I.S. Self-Reflection Journal\n\n";
                out << "_This journal is automatically maintained by JARVIS._\n";
                out << "_It records what I learn, what I am confident about,_\n";
                out << "_and critically: what I might be wrong about._\n\n";
                out << "---\n";
                file.close();
            }
        }

        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&file);
            out << markdown;
            file.close();
            qDebug() << "[SelfJournal] Reflection cycle written to:"
                     << m_journalPath;
        } else {
            qDebug() << "[SelfJournal] External journal unavailable,"
                     << "reflection stored in local DB only";
        }
    }
}
