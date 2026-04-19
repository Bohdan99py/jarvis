#pragma once
// -------------------------------------------------------
// session_memory.h — Контекстная память сессии и
//                    постоянное хранилище (JSON)
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QDateTime>

// Одна запись в истории диалога
struct ChatMessage
{
    QString role;       // "user" или "assistant"
    QString content;
    QDateTime timestamp;

    QJsonObject toJson() const;
    static ChatMessage fromJson(const QJsonObject& obj);
};

// Контекст текущей задачи
struct TaskContext
{
    QString currentTask;           // Что сейчас делает пользователь
    QString lastTopic;             // Последняя тема разговора
    QStringList recentApps;        // Недавно запущенные приложения
    QStringList recentSearches;    // Недавние поиски
    int commandCount = 0;          // Число команд за сессию

    QJsonObject toJson() const;
    static TaskContext fromJson(const QJsonObject& obj);
    void clear();
};

class SessionMemory : public QObject
{
    Q_OBJECT

public:
    explicit SessionMemory(QObject* parent = nullptr);
    ~SessionMemory() override;

    // --- Текущая сессия ---

    // Добавить сообщение в историю
    void addMessage(const QString& role, const QString& content);

    // Получить последние N сообщений для контекста API
    QJsonArray recentMessagesAsJson(int maxMessages = 20) const;

    // Получить всю историю сессии как текст (для локального fallback)
    QString sessionSummary() const;

    // Контекст задачи
    TaskContext& taskContext() { return m_taskContext; }
    const TaskContext& taskContext() const { return m_taskContext; }

    // Обновить контекст на основе команды пользователя
    void updateContext(const QString& userInput, const QString& response);

    // Количество сообщений в сессии
    int messageCount() const { return m_sessionMessages.size(); }

    // --- Постоянная память (между сессиями) ---

    // Загрузить / сохранить в JSON-файл
    void loadPersistent();
    void savePersistent();

    // Добавить факт для долгосрочной памяти
    void rememberFact(const QString& key, const QString& value);
    QString recallFact(const QString& key) const;
    QJsonObject allFacts() const { return m_persistentFacts; }

    // Статистика использования команд (для предугадывания)
    void recordCommandUsage(const QString& command);
    QJsonObject commandStats() const { return m_commandStats; }

    // История прошлых сессий (краткие summary)
    QJsonArray pastSessionSummaries() const { return m_pastSessions; }

    // Системный промпт с контекстом для Claude API
    QString buildSystemPrompt() const;

signals:
    void memoryUpdated();

private:
    QString persistentFilePath() const;

    // Сессия
    QVector<ChatMessage> m_sessionMessages;
    TaskContext m_taskContext;

    // Постоянная память
    QJsonObject m_persistentFacts;    // {"имя_пользователя": "...", "предпочтения": "..."}
    QJsonObject m_commandStats;       // {"блокнот": 15, "найди": 42, ...}
    QJsonArray  m_pastSessions;       // [{date, summary, commandCount}, ...]

    static constexpr int MAX_SESSION_MESSAGES = 100;
    static constexpr int MAX_PAST_SESSIONS = 30;
};