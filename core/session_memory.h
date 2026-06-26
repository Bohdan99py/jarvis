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

    // Для Brain::captureSnapshot — история команд пользователя
    QStringList recentCommands(int n = 8) const;
    // Последний ответ Джарвиса (для контекста уточнений)
    QString lastResponse() const;

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

    // === Полный поисковый журнал сессий ===
    // Файл/папка, созданные или изменённые в текущей сессии (через
    // CodeActions) — попадают в "filesTouched" сводки сессии.
    void recordFileTouched(const QString& path);

    // Сохраняет/обновляет сводку ТЕКУЩЕЙ сессии в m_pastSessions и
    // пишет на диск. Вызывается после каждого сообщения (crash-safe —
    // даже при аварийном завершении на диске остаётся актуальная
    // сводка) и в деструкторе.
    void flushSessionSummary();

    // Если userQuery похож на запрос "вспомни что было ..." — парсит
    // период (сегодня/вчера/на прошлой неделе/в прошлом месяце/за N дней)
    // и/или тему, ищет совпадения по m_pastSessions (включая текущую,
    // ещё не завершённую сессию) и возвращает текстовый блок-контекст
    // для подстановки в сообщение Claude/Gemini. Если запрос не похож
    // на запрос истории — возвращает пустую строку (без накладных расходов).
    QString buildHistoryContext(const QString& userQuery) const;

    // vibeMode оставлен как stub для совместимости — Brain сам решает режим
    void setVibeMode(bool) {}  // deprecated, ничего не делает
    bool vibeMode() const { return false; }

    // Сводка профиля предпочтений (UserProfile::buildProfileSummary) —
    // обновляется перед каждым обращением к Claude, попадает в system prompt
    void setUserProfileSummary(const QString& summary) { m_userProfileSummary = summary; }
    QString userProfileSummary() const   { return m_userProfileSummary; }

    // Activity context: what the user is doing right now
    void setActivityContext(const QString& ctx) { m_activityContext = ctx; }
    // Detected user role based on activity patterns
    void setDetectedRole(const QString& role) { m_detectedRole = role; }
    // Knowledge base summary
    void setKnowledgeSummary(const QString& kb) { m_knowledgeSummary = kb; }
    // Active user name (for multi-user prompts)
    void setActiveUserName(const QString& name) { m_activeUserName = name; }

    // Core Memory Stream: top-N time-decay scored events (RAG context)
    void setMemoryStreamContext(const QString& ctx) { m_memoryStreamContext = ctx; }

    // Сознание: что JARVIS знает о накопленном опыте (для system prompt)
    void setLearningStats(int totalInteractions, int likedResponses,
                          int cachedResponses, int sessionsRecorded);

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

    // Собирает JSON-сводку текущей сессии (дата начала/конца, темы,
    // тронутые файлы, краткая выжимка диалога) — используется в
    // flushSessionSummary() и деструкторе.
    QJsonObject buildSessionSummaryObject() const;

    QVector<ChatMessage> m_sessionMessages;
    TaskContext m_taskContext;

    QJsonObject m_persistentFacts;
    QJsonObject m_commandStats;
    QJsonArray  m_pastSessions;

    QString m_userProfileSummary;
    QString m_activityContext;
    QString m_detectedRole;
    QString m_knowledgeSummary;
    QString m_activeUserName;
    QString m_memoryStreamContext;

    // Сознание: что JARVIS знает о себе
    int m_totalInteractions = 0;
    int m_likedResponses    = 0;
    int m_cachedResponses   = 0;
    int m_sessionsRecorded  = 0;

    QString m_projectRoot;
    QString m_projectMap;
    int     m_projectFileCount   = 0;
    int     m_projectSymbolCount = 0;

    // === Поисковый журнал сессий ===
    QDateTime   m_sessionStart;            // момент создания SessionMemory (начало сессии)
    QStringList m_sessionTopics;           // ключевые слова из сообщений пользователя
    QStringList m_sessionFilesTouched;     // файлы, созданные/изменённые за сессию
    int         m_currentSessionIndex = -1; // индекс записи текущей сессии в m_pastSessions

    static constexpr int MAX_SESSION_MESSAGES = 100;
    // 50 записей — достаточно для нескольких недель истории.
    // Старые сессии автоматически удаляются.
    static constexpr int MAX_PAST_SESSIONS    = 50;
    static constexpr int MAX_SESSION_TOPICS   = 20;
    static constexpr int MAX_SESSION_FILES    = 20;
};
