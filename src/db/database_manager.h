#pragma once
// ============================================================
// database_manager.h — Центральная БД J.A.R.V.I.S.
// ============================================================
// ВАЖНО: ChatMessage, UserProfile, IndexedFile уже объявлены
// в session_memory.h, user_profile.h, project_indexer.h.
// Здесь мы объявляем ТОЛЬКО новые структуры с префиксом Db,
// чтобы избежать повторных объявлений.
// ============================================================

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QString>
#include <QVariant>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QMutex>
#include <optional>

// ============================================================
//  Структуры специфичные для БД
// ============================================================

struct DbChatMessage {
    qint64    id          = 0;
    qint64    userId      = 1;        // FK → users.id
    QString   role;                   // "user" | "assistant" | "system"
    QString   content;
    QString   model;                  // "claude" | "gemini" | "groq" | "local"
    QString   sessionId;              // UUID группирует разговор
    QDateTime createdAt;
    int       tokensUsed  = 0;
};

struct DbUserProfile {
    qint64    id          = 0;
    QString   name;
    QString   scenario;               // "game_dev" | "gamer" | "qa" | "artist"
    QString   language;               // "ru" | "en" | "auto"
    QString   preferences;            // JSON доп. настроек
    QString   currentRole;            // "Developer" | "QA_Tester" | "Student_Academic" | ...
    QDateTime createdAt;
    QDateTime lastSeen;
};

struct DbIndexedFile {
    qint64    id             = 0;
    QString   filePath;
    QString   fileName;
    QString   extension;              // ".cpp" | ".h" | ".py" и т.д.
    QString   contentHash;            // MD5 — детект изменений
    QString   symbols;                // JSON: функции, классы, переменные
    QString   summary;                // краткое описание файла
    qint64    sizeBytes      = 0;
    QDateTime indexedAt;
    QDateTime fileModifiedAt;
};

struct DbUserCommand {
    qint64    id          = 0;
    qint64    userId      = 1;
    QString   trigger;                // ключевое слово / фраза
    QString   action;                 // JSON описание действия
    QString   type;                   // "system" | "macro" | "app" | "ai"
    QString   scenario;               // "" = все сценарии
    bool      isEnabled   = true;
    int       usageCount  = 0;
    QDateTime createdAt;
};

struct DbBehaviorPattern {
    qint64    id          = 0;
    qint64    userId      = 1;
    QString   trigger;                // что сказал пользователь (нормализовано)
    QString   response;               // первые 500 символов ответа JARVIS
    QString   context;                // JSON: время, модель, сценарий
    int       frequency   = 1;        // сколько раз встречался
    float     confidence  = 0.0f;     // 0..1
    QDateTime lastSeen;
};

// Core Memory Stream — значимые события с приоритетом для RAG/time-decay
struct DbMemoryEvent {
    qint64    id         = 0;
    qint64    userId     = 1;
    QString   eventType;               // "user_query" | "insight" | "decision" | "milestone" | ...
    QString   content;                 // краткое описание события
    double    importance = 0.5;        // 0.0 .. 1.0
    QDateTime createdAt;
};

// Task Manager — Kanban tasks with deadlines
struct DbTask {
    qint64    id         = 0;
    qint64    userId     = 1;
    QString   title;
    QString   category;              // "UE5" | "KiCad" | "Blender" | "General"
    QString   status;                // "Todo" | "InProgress" | "Done"
    QString   priority;              // "Low" | "Medium" | "High"
    QDateTime deadline;              // nullable
    QDateTime updatedAt;
    QDateTime createdAt;
};

// Запись для fine-tuning датасета (лайкнутые диалоги)
struct DbTrainingLog {
    qint64    id           = 0;
    qint64    userId       = 1;
    QString   userMessage;             // вопрос пользователя
    QString   aiResponse;              // полный ответ JARVIS
    QString   model;                   // "claude" | "gemini" | "local"
    QString   sessionId;
    int       rating       = 1;        // 1 = 👍, в будущем можно 1-5
    QDateTime createdAt;
};

// ============================================================
//  DatabaseManager — синглтон
// ============================================================

class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    static DatabaseManager& instance();

    DatabaseManager(const DatabaseManager&)            = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    // ── Инициализация ──────────────────────────────────────
    bool    open(const QString& dbPath = QString());
    void    close();
    bool    isOpen() const;
    QString lastError() const;
    QString dbPath() const { return m_dbPath; }

    // ── users ──────────────────────────────────────────────
    qint64                       addUser(const DbUserProfile& p);
    bool                         updateUser(const DbUserProfile& p);
    std::optional<DbUserProfile> getUser(qint64 id);
    QList<DbUserProfile>         getAllUsers();
    qint64                       getOrCreateDefaultUser();

    // ── chat_history ───────────────────────────────────────
    qint64               addMessage(const DbChatMessage& msg);
    QList<DbChatMessage> getSession(const QString& sessionId);
    QList<DbChatMessage> getRecentMessages(qint64 userId, int limit = 50);
    QList<DbChatMessage> searchMessages(qint64 userId, const QString& query);
    bool                 deleteSession(const QString& sessionId);
    bool                 clearHistory(qint64 userId);
    int                  monthlyTokens(qint64 userId, const QString& model);
    QList<QMap<QString,QString>> getSessions(qint64 userId, int limit = 100);

    // ── commands / macros ──────────────────────────────────
    qint64               addCommand(const DbUserCommand& cmd);
    bool                 updateCommand(const DbUserCommand& cmd);
    bool                 deleteCommand(qint64 id);
    QList<DbUserCommand> getCommands(qint64 userId,
                                     const QString& scenario = QString(),
                                     const QString& type     = QString());
    std::optional<DbUserCommand> findCommand(qint64 userId, const QString& trigger);
    bool                 incrementUsage(qint64 commandId);

    // ── memory_kv (запомни / вспомни) ─────────────────────
    bool                  memSet(qint64 userId, const QString& key, const QString& value);
    QString               memGet(qint64 userId, const QString& key,
                                 const QString& def = QString());
    bool                  memDelete(qint64 userId, const QString& key);
    QMap<QString,QString> memGetAll(qint64 userId);

    // ── settings ───────────────────────────────────────────
    bool     setConfig(const QString& key, const QVariant& value);
    QVariant getConfig(const QString& key, const QVariant& def = QVariant());
    bool     deleteConfig(const QString& key);

    // ── indexed_files (RAG / фоновое обучение) ────────────
    qint64                       upsertFile(const DbIndexedFile& f);
    bool                         deleteFileIndex(const QString& filePath);
    std::optional<DbIndexedFile> getFileIndex(const QString& filePath);
    QList<DbIndexedFile>         getIndexedFiles(const QString& ext = QString());
    QList<QString>               getStaleFiles(const QStringList& allPaths);
    bool                         clearFileIndex();
    int                          indexedFileCount();

    // ── behavior_patterns (обучение из истории) ───────────
    bool                     upsertPattern(const DbBehaviorPattern& p);
    QList<DbBehaviorPattern> getTopPatterns(qint64 userId, int limit = 50);
    QList<DbBehaviorPattern> findPatterns(qint64 userId, const QString& triggerHint);
    int                      patternCount(qint64 userId);
    bool                     clearPatterns(qint64 userId);

    // ── training_logs (👍 лайкнутые диалоги для fine-tuning) ─
    qint64                   addTrainingLog(const DbTrainingLog& log);
    bool                     deleteTrainingLog(qint64 id);
    QList<DbTrainingLog>     getTrainingLogs(qint64 userId, int limit = 10000);
    int                      trainingLogCount(qint64 userId, int minRating = -1);
    // Экспорт в .jsonl формат для Unsloth/LLaMA-Factory
    bool                     exportToJsonl(qint64 userId, const QString& filePath);
    // Очистка мусора — дубли, короткие ответы, ошибки распознавания
    int                      cleanupTrainingLogs(qint64 userId);
    // Обновляет рейтинг существующей пары (для лайка 👍)
    bool                     updateTrainingLogRating(const QString& userMsg,
                                                     const QString& aiResp, int rating);

    // ── memory_stream (Core Memory с time-decay) ───────────
    qint64               addMemoryEvent(const DbMemoryEvent& ev);
    // TOP-N событий, ранжированных по Score = importance / (1 + Δhours)
    QList<DbMemoryEvent> getTopMemoryEvents(qint64 userId, int limit = 5);

    // ── voice_journal (пассивная запись голоса) ────────────
    qint64 addVoiceJournalEntry(const QString& transcript, const QString& language,
                                float confidence, const QDateTime& capturedAt);
    bool   markJournalEntryProcessed(qint64 id);
    QList<QMap<QString,QVariant>> getUnprocessedJournalEntries(qint64 userId, int limit = 200);
    int    voiceJournalCount(qint64 userId, bool processedOnly = false);
    int    cleanupOldJournalEntries(qint64 userId, int olderThanDays = 7);

    // ── tasks (Kanban task manager) ──────────────────────────
    qint64           addTask(const DbTask& t);
    bool             updateTask(const DbTask& t);
    bool             deleteTask(qint64 id);
    QList<DbTask>    getTasks(qint64 userId, const QString& status = QString());
    std::optional<DbTask> getTask(qint64 id);
    QList<DbTask>    getOverdueTasks(qint64 userId, int withinHours = 24);

    // ── response_cache (кэш AI-ответов для offline) ────────
    // JARVIS один раз спрашивает Claude → кэширует → дальше offline.
    // Накапливается библиотека анекдотов, советов, фактов без доп. затрат.
    QString getCachedResponse(const QString& trigger,
                              const QString& language = QStringLiteral("ru"),
                              const QString& category = QString());
    bool    saveCachedResponse(const QString& trigger,
                               const QString& response,
                               const QString& language = QStringLiteral("ru"),
                               const QString& category = QStringLiteral("chitchat"));
    QString getRandomCached(const QString& category,
                            const QString& language = QStringLiteral("ru"));
    int     responseCacheCount();

signals:
    void databaseError(const QString& error);
    void messageSaved(qint64 messageId);
    void fileIndexed(const QString& filePath);

    QSqlDatabase connection();

private:
    explicit DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager() override;

    bool createTables();
    bool runMigrations();
    bool execQuery(const QString& sql);
    void logError(const QString& context, const QSqlError& err);

    QSqlDatabase   m_db;
    QString        m_dbPath;
    QString        m_lastError;
    mutable QMutex m_mutex;

    static constexpr int k_schemaVersion = 13;
};
