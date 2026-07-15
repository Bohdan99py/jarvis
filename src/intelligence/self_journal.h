#pragma once
// ============================================================
// self_journal.h — JARVIS Self-Reflection Journal
//
// Markdown journal where JARVIS records what he learned,
// what he is confident about, and critically:
// "What I am confused about / What if I misunderstood."
//
// Written to D:/Jarvis/self_journal.md (external drive)
// with a local SQLite mirror of doubt entries in Tier 2
// so doubts survive drive disconnection.
//
// Structure of each journal entry:
//   ## [timestamp] Learning Cycle #N
//   ### What I Learned
//   - [confident items with scores]
//   ### What I Am Confident About
//   - [verified facts]
//   ### What If I Am Wrong
//   - [doubt items with reasons and source references]
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>

// ============================================================
//  DoubtEntry — a single self-doubt record
// ============================================================

struct DoubtEntry
{
    qint64    id = 0;
    QString   content;         // what JARVIS thinks he understood
    QString   doubtReason;     // why he's not sure
    double    confidence = 0.5;
    QString   sourceRef;       // file/PDF that generated the doubt
    QDateTime createdAt;
    bool      resolved = false;
    QString   resolution;      // user's correction or confirmation
};

// ============================================================
//  SelfJournal
// ============================================================

class SelfJournal : public QObject
{
    Q_OBJECT

public:
    static SelfJournal& instance();

    SelfJournal(const SelfJournal&)            = delete;
    SelfJournal& operator=(const SelfJournal&) = delete;

    // --- Configuration ---
    void setJournalPath(const QString& path);
    QString journalPath() const { return m_journalPath; }

    // --- Doubt logging ---
    // Returns the new row's id (0 on failure) so callers can hold onto it
    // and resolve it later via resolveDoubt() once the user gives feedback.
    qint64 logDoubt(const QString& content,
                     const QString& reason,
                     double confidence,
                     const QString& sourceRef = QString());

    void logLearning(const QString& content,
                     double confidence,
                     const QString& sourceRef = QString());

    // --- Doubt queries (from local SQLite, always available) ---
    QList<DoubtEntry> unresolvedDoubts(int limit = 10) const;
    QList<DoubtEntry> topDoubtsForVerification(int limit = 3) const;
    int unresolvedDoubtCount() const;

    // --- Resolution ---
    void resolveDoubt(qint64 doubtId, bool correct,
                      const QString& userCorrection = QString());

    // --- Journal writing ---
    // Writes a full reflection cycle to the markdown journal
    void writeReflectionCycle();

    void ensureTable();

signals:
    void doubtLogged(const QString& content, double confidence);
    void doubtResolved(qint64 id, bool wasCorrect);
    void reflectionWritten(const QString& path);

private:
    explicit SelfJournal(QObject* parent = nullptr);
    ~SelfJournal() override;

    // Markdown generation
    QString buildReflectionMarkdown() const;
    void appendToJournalFile(const QString& markdown);

    // Cycle counter
    int nextCycleNumber() const;

    QString m_journalPath;  // e.g. "D:/Jarvis/self_journal.md"
    int     m_cycleCount = 0;
};
