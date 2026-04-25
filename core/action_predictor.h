#pragma once
// -------------------------------------------------------
// action_predictor.h — Предугадывание действий пользователя
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QPair>

#include "jarvis_core_export.h"

class SessionMemory;

// Предложение действия
struct ActionSuggestion
{
    QString action;
    QString description;
    double confidence;

    bool isValid() const { return !action.isEmpty() && confidence > 0.0; }
};

class JARVIS_CORE_EXPORT ActionPredictor : public QObject
{
    Q_OBJECT

public:
    explicit ActionPredictor(SessionMemory* memory, QObject* parent = nullptr);

    QVector<ActionSuggestion> suggest(int maxSuggestions = 3) const;
    ActionSuggestion suggestAfter(const QString& lastCommand) const;

    void recordSequence(const QString& command);

    void loadPatterns();
    void savePatterns();

signals:
    void suggestionReady(const ActionSuggestion& suggestion);

private:
    struct PatternRule {
        QString trigger;
        QString suggestedAction;
        QString description;
        int hitCount = 0;
    };

    void initDefaultRules();
    void learnFromHistory();

    SessionMemory* m_memory = nullptr;
    QVector<PatternRule> m_rules;
    QStringList m_recentCommands;

    static constexpr int MAX_RECENT = 10;
    static constexpr double MIN_CONFIDENCE = 0.3;
};
