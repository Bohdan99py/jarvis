#pragma once
// ============================================================
// activity_tracker.h — Deep context awareness for J.A.R.V.I.S.
//
// Continuously monitors what the user is doing:
//   - Active window title + process name
//   - Window text via OCR (optional, configurable)
//   - Running applications
//   - Activity transitions (switched from X to Y)
//
// Builds a rich "activity context" string that goes into
// Claude's system prompt so JARVIS truly understands what
// the user is working on right now and can give proactive advice.
//
// Also maintains a knowledge_base table — facts extracted
// from user interactions that persist across sessions.
// ============================================================

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

class J2JMeshConnector;

// ============================================================
//  ActivityEntry — one snapshot of user activity
// ============================================================

struct ActivityEntry {
    QDateTime timestamp;
    QString   appName;
    QString   windowTitle;
    QString   category;       // "coding", "art", "browsing", "gaming", "communication", "office", "other"
    int       durationSec = 0;
};

// ============================================================
//  KnowledgeEntry — a fact JARVIS learned about the user
// ============================================================

struct KnowledgeEntry {
    qint64    id = 0;
    qint64    userId = 1;
    QString   category;       // "skill", "preference", "project", "habit", "tool", "workflow"
    QString   key;            // short identifier
    QString   value;          // the knowledge itself
    float     confidence = 0.5f;
    int       reinforcements = 1;
    QDateTime learnedAt;
    QDateTime lastSeen;
};

// ============================================================
//  ActivityTracker
// ============================================================

class ActivityTracker : public QObject
{
    Q_OBJECT
public:
    explicit ActivityTracker(QObject* parent = nullptr);
    ~ActivityTracker() override;

    void start(int intervalSeconds = 15);
    void stop();
    bool isRunning() const { return m_running; }

    // Current activity context for system prompt (compact text)
    QString buildActivityContext() const;

    // What the user has been doing in the last N minutes
    QString recentActivitySummary(int minutes = 30) const;

    // Current detected activity category
    QString currentCategory() const { return m_currentCategory; }

    // Time spent in current activity (seconds)
    int currentActivityDuration() const;

    // Lets newly learned facts propagate to other JARVIS instances on
    // the mesh (e.g. laptop + desktop sharing what they learn about the
    // same user). Optional — facts stay local if never set.
    void setMeshConnector(J2JMeshConnector* mesh) { m_mesh = mesh; }

    // --- Knowledge Base ---
    void learnFact(qint64 userId, const QString& category,
                   const QString& key, const QString& value,
                   float confidence = 0.6f);
    void reinforceFact(qint64 userId, const QString& key);
    QString knowledgeSummary(qint64 userId, int maxFacts = 15) const;

    // Extract knowledge from a conversation turn
    void extractKnowledge(qint64 userId, const QString& userInput,
                          const QString& aiResponse);

    // --- User role detection ---
    QString detectUserRole() const;

    // App category classification (used by ScreenshotLearner + CuriosityEngine)
    static QString categorizeApp(const QString& processName, const QString& windowTitle);

signals:
    void activityChanged(const QString& newApp, const QString& category);
    void knowledgeLearned(const QString& key, const QString& value);

private slots:
    void onCapture();

private:
    void ensureTables();
    void recordActivity(const QString& appName, const QString& windowTitle);
    static QString cleanAppName(const QString& processName);

    // Windows API
    QString getActiveWindowTitle() const;
    QString getActiveProcessName() const;

    QTimer*     m_timer = nullptr;
    bool        m_running = false;
    J2JMeshConnector* m_mesh = nullptr;

    // Current activity tracking
    QString     m_currentApp;
    QString     m_currentTitle;
    QString     m_currentCategory;
    QDateTime   m_activityStart;

    // Recent activity ring buffer (last ~30 minutes)
    struct RecentEntry {
        QDateTime timestamp;
        QString   app;
        QString   title;
        QString   category;
        int       durationSec = 0;
    };
    QVector<RecentEntry> m_recentActivity;
    static constexpr int MAX_RECENT = 120; // 30min at 15s intervals

    // Activity transition tracking
    struct Transition {
        QString fromApp;
        QString toApp;
        QDateTime when;
    };
    QVector<Transition> m_transitions;
    static constexpr int MAX_TRANSITIONS = 20;

    static constexpr int MIN_DURATION_SEC = 3;
};
