#pragma once
// -------------------------------------------------------
// action_predictor.h — Action prediction + binary ethics
//
// Two systems:
// 1. Pattern-based action suggestions (IF user did A, THEN suggest B)
// 2. Binary Reinforcement Learning: ethics weights + feedback loop
//    that tracks Good/Bad per action category via user feedback.
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QPair>
#include <QJsonObject>

#include "jarvis_core_export.h"

class SessionMemory;

struct ActionSuggestion
{
    QString action;
    QString description;
    double confidence;

    bool isValid() const { return !action.isEmpty() && confidence > 0.0; }
};

// Binary ethics evaluation result
struct EthicsEvaluation
{
    double score;          // -1.0 (BAD) to +1.0 (GOOD)
    QString category;      // matched category label
    bool hardcoded;        // true = immutable rule, false = learned weight

    bool isAcceptable() const { return score > -0.5; }
};

class JARVIS_CORE_EXPORT ActionPredictor : public QObject
{
    Q_OBJECT

public:
    explicit ActionPredictor(SessionMemory* memory, QObject* parent = nullptr);

    // --- Pattern-based suggestions ---
    QVector<ActionSuggestion> suggest(int maxSuggestions = 3) const;
    ActionSuggestion suggestAfter(const QString& lastCommand) const;
    void recordSequence(const QString& command);

    void loadPatterns();
    void savePatterns();

    // --- Binary Ethics & Experience Learning ---
    EthicsEvaluation evaluateAction(const QString& action) const;

    void recordFeedback(const QString& action, bool positive);

    bool shouldAskForFeedback() const;
    void resetFeedbackCounter();

    QString buildFeedbackQuestion(bool english) const;

    void loadEthicsWeights();
    void saveEthicsWeights();

    double experienceScore(const QString& category) const;

signals:
    void suggestionReady(const ActionSuggestion& suggestion);
    void feedbackRequested(const QString& question);
    void ethicsViolation(const QString& action, double score);

private:
    struct PatternRule {
        QString trigger;
        QString suggestedAction;
        QString description;
        int hitCount = 0;
    };

    void initDefaultRules();
    void initEthicsDictionary();
    void learnFromHistory();

    SessionMemory* m_memory = nullptr;
    QVector<PatternRule> m_rules;
    QStringList m_recentCommands;

    // Ethics: hardcoded immutable rules
    struct EthicsRule {
        QString category;
        QStringList keywords;
        double weight;    // -1.0 = BAD, +1.0 = GOOD
    };
    QVector<EthicsRule> m_ethicsRules;

    // Experience: learned weights from user feedback (mutable)
    QJsonObject m_experienceWeights;
    int m_interactionsSinceFeedback = 0;
    QString m_lastEvaluatedAction;

    static constexpr int MAX_RECENT = 10;
    static constexpr double MIN_CONFIDENCE = 0.3;
    static constexpr int FEEDBACK_INTERVAL = 12;
    static constexpr double LEARNING_RATE = 0.15;
};
