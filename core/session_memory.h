#pragma once
// -------------------------------------------------------
// session_memory.h — Контекстная память сессии
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QDateTime>

#include "jarvis_core_export.h"

struct ChatMessage
{
    QString role;
    QString content;
    QDateTime timestamp;

    QJsonObject toJson() const;
    static ChatMessage fromJson(const QJsonObject& obj);
};

struct TaskContext
{
    QString currentTask;
    QString lastTopic;
    QStringList recentApps;
    QStringList recentSearches;
    int commandCount = 0;

    QJsonObject toJson() const;
    static TaskContext fromJson(const QJsonObject& obj);
    void clear();
};

class JARVIS_CORE_EXPORT SessionMemory : public QObject
{
    Q_OBJECT

public:
    explicit SessionMemory(QObject* parent = nullptr);
    ~SessionMemory() override;

    void addMessage(const QString& role, const QString& content);
    QJsonArray recentMessagesAsJson(int maxMessages = 20) const;
    QString sessionSummary() const;

    TaskContext& taskContext() { return m_taskContext; }
    const TaskContext& taskContext() const { return m_taskContext; }

    void updateContext(const QString& userInput, const QString& response);
    int messageCount() const { return m_sessionMessages.size(); }

    void loadPersistent();
    void savePersistent();

    void rememberFact(const QString& key, const QString& value);
    QString recallFact(const QString& key) const;
    QJsonObject allFacts() const { return m_persistentFacts; }

    void recordCommandUsage(const QString& command);
    QJsonObject commandStats() const { return m_commandStats; }

    QJsonArray pastSessionSummaries() const { return m_pastSessions; }

    void setVibeMode(bool on)            { m_vibeMode = on; }
    bool vibeMode() const                { return m_vibeMode; }

    void setProjectInfo(const QString& root,
                        const QString& projectMap,
                        int fileCount,
                        int symbolCount);
    void clearProjectInfo();
    bool hasProjectInfo() const          { return !m_projectRoot.isEmpty(); }

    QString buildSystemPrompt() const;

signals:
    void memoryUpdated();

private:
    QString persistentFilePath() const;

    QVector<ChatMessage> m_sessionMessages;
    TaskContext m_taskContext;

    QJsonObject m_persistentFacts;
    QJsonObject m_commandStats;
    QJsonArray  m_pastSessions;

    bool m_vibeMode = false;

    QString m_projectRoot;
    QString m_projectMap;
    int     m_projectFileCount   = 0;
    int     m_projectSymbolCount = 0;

    static constexpr int MAX_SESSION_MESSAGES = 100;
    static constexpr int MAX_PAST_SESSIONS    = 30;
};
