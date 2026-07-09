#pragma once
// ============================================================
// synonym_learner.h — Self-taught synonym/paraphrase dictionary
//
// Pure local dictionary — no embedding model, no LLM call. Search
// (ContentSearch/SearchRouter) is plain substring matching, which
// misses paraphrases ("отчёт" vs "репорт"). Instead of a static
// synonym list, Jarvis asks the user when a search comes up empty
// and remembers the answer — the dictionary grows from real misses.
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>

class SynonymLearner : public QObject
{
    Q_OBJECT
public:
    static SynonymLearner& instance();

    SynonymLearner(const SynonymLearner&)            = delete;
    SynonymLearner& operator=(const SynonymLearner&) = delete;

    void ensureTable();

    // Known synonyms for term (bidirectional), lowercase, deduplicated.
    // Does not include term itself.
    QStringList expand(const QString& term) const;

    // Expands every term in the list and merges the results in
    // (original terms kept, synonyms appended) — safe to feed straight
    // into the existing QStringList-based matching functions.
    QStringList expandAll(const QStringList& terms) const;

    void learn(const QString& term, const QString& synonym, float confidence = 0.6f);

    // ── Pending clarification — reactive, not idle-timer-gated ──
    // Fires immediately off a zero-result search (see SearchRouter),
    // independent of CuriosityEngine's proactive/idle-gated questions.
    // Returns the question text and arms the pending state.
    QString askForClarification(const QString& term, bool english);
    bool    hasPendingClarification() const;
    // Treats answerText as the clarification reply; persists a synonym
    // mapping unless the user indicates they don't know either. Returns
    // true if it consumed the pending state (regardless of what was learned).
    bool    consumeClarification(const QString& answerText);

private:
    explicit SynonymLearner(QObject* parent = nullptr);

    static constexpr int PENDING_WINDOW_MINUTES = 5;

    QString   m_pendingTerm;
    QDateTime m_pendingTimestamp;
};
