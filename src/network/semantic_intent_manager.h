#pragma once
// ============================================================
// semantic_intent_manager.h — Goal-Oriented Intent Classifier
//
// Transforms natural-language phrases into executable action
// sequences. Unlike CommandDispatcherTg (which handles explicit
// /slash commands), this module listens for high-level goals
// expressed in free text:
//
//   "найди работу" → open browser → navigate to hh.ru
//   "хочу музыки"  → open music.youtube.com
//   "продуктивность" → open IDE + close social media
//
// Intent confidence model:
//   > 0.85 → execute silently + send ironic acknowledgement
//   0.50–0.85 → ask user for confirmation before acting
//   < 0.50 → pass through to LLM as conversational
//
// Thread safety: the intent map is immutable after construction;
// action execution dispatches to the main thread via signals.
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

class J2JTelegramGateway;

// ── Single executable action ────────────────────────────────

struct IntentAction
{
    enum Type {
        OpenUrl,       // open a URL in the default browser
        LaunchApp,     // launch a local application by alias
        SystemCommand, // future: lock screen, volume, etc.
    };

    Type    type;
    QString target;    // URL for OpenUrl, app alias for LaunchApp
    QString label;     // human-readable description for logs
};

// ── Classified intent result ────────────────────────────────

struct IntentMatch
{
    bool    matched    = false;
    QString goalId;             // internal key, e.g. "job_search"
    QString goalLabel;          // human-readable, e.g. "Job Search"
    double  confidence = 0.0;   // 0.0–1.0
    QVector<IntentAction> actions;
};

// ── Manager ─────────────────────────────────────────────────

class SemanticIntentManager : public QObject
{
    Q_OBJECT

public:
    explicit SemanticIntentManager(J2JTelegramGateway* gateway,
                                   QObject* parent = nullptr);

    // Classify a free-text message into a goal.
    // Returns matched=false if no goal detected.
    IntentMatch classifyIntent(const QString& text) const;

    // Execute all actions in the match. Returns a short
    // acknowledgement string for the user.
    QString executeActions(const IntentMatch& match);

    // Whether the message is purely conversational (no goal signal)
    static bool isConversational(const IntentMatch& match);

    // Check if a user query would benefit from a visual diagram
    static bool needsVisualExplanation(const QString& text);

signals:
    void intentExecuted(const QString& goalId, int actionCount);

private:
    // One entry in the goal registry
    struct GoalEntry {
        QString     goalId;
        QString     goalLabel;
        QStringList triggerPhrases;  // lowercase, checked via contains()
        QVector<IntentAction> actions;
    };

    void buildGoalRegistry();

    // Score how well text matches a goal entry (0.0–1.0)
    double scoreMatch(const QString& lower,
                      const GoalEntry& goal) const;

    QVector<GoalEntry> m_goals;
    J2JTelegramGateway* m_gateway = nullptr;
};
