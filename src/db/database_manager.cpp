// ============================================================
// database_manager.cpp — J.A.R.V.I.S. SQLite storage
// ============================================================
#include "database_manager.h"
#include "llm_cache_manager.h"
#include "jarvis_paths.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QUuid>
#include <QThread>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTextStream>
#include <QFile>

// ============================================================
//  Синглтон
// ============================================================

DatabaseManager& DatabaseManager::instance()
{
    static DatabaseManager inst;
    return inst;
}

DatabaseManager::DatabaseManager(QObject* parent) : QObject(parent) {}
DatabaseManager::~DatabaseManager() { close(); }

// ============================================================
//  Thread-safe соединение — каждый поток получает своё
// ============================================================

QSqlDatabase DatabaseManager::connection()
{
    const QString connName = QStringLiteral("jarvis_db_%1")
        .arg(reinterpret_cast<quintptr>(QThread::currentThread()));

    if (QSqlDatabase::contains(connName)) {
        auto db = QSqlDatabase::database(connName);
        if (db.isOpen()) return db;
    }

    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    db.setDatabaseName(m_dbPath);
    // QSQLITE_BUSY_TIMEOUT — передаём в драйвер ДО open(), иначе таймаут
    // на первое открытие соединения не применяется.
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!db.open()) logError("connection(thread)", db.lastError());

    QSqlQuery q(db);
    // busy_timeout=5000 — если БД на секунду-другую заблокирована другим
    // процессом/потоком (второй запущенный экземпляр JARVIS, параллельная
    // запись из BackgroundLearner и т.п.), запрос ЖДЁТ до 5 секунд вместо
    // немедленного SQLITE_BUSY. Без этого q.exec() в trainingLogCount()/
    // voiceJournalCount() молча проваливался при конкурентном доступе и
    // функция тихо возвращала 0 — выглядело так, будто данных нет вообще,
    // хотя они благополучно лежали в файле.
    q.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA foreign_keys=ON");
    q.exec("PRAGMA synchronous=NORMAL");
    q.exec("PRAGMA cache_size=-2000");
    return db;
}

// ============================================================
//  Открытие БД
// ============================================================

bool DatabaseManager::open(const QString& dbPath)
{
    QMutexLocker lock(&m_mutex);
    if (m_db.isOpen()) return true;

    if (dbPath.isEmpty()) {
        // Раньше: скрытая папка AppData, разная видимость в зависимости
        // от сборки/конкуренции процессов. Теперь: один предсказуемый
        // путь в "Документах" пользователя — виден глазами, легко
        // бэкапится, легко стирается вручную чтобы начать обучение
        // с нуля, и одинаков для debug-сборки из CLion и установленного
        // релиза (оба используют один и тот же JarvisPaths::subPath).
        m_dbPath = JarvisPaths::subPath(QStringLiteral("jarvis.db"));
    } else {
        m_dbPath = dbPath;
    }

    m_db = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"), QStringLiteral("jarvis_main"));
    m_db.setDatabaseName(m_dbPath);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));

    if (!m_db.open()) { logError("open", m_db.lastError()); return false; }

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA foreign_keys=ON");
    q.exec("PRAGMA synchronous=NORMAL");
    q.exec("PRAGMA cache_size=-2000");

    if (!createTables())  return false;
    if (!runMigrations()) return false;

    getOrCreateDefaultUser();
    qDebug() << "[DB] Opened:" << m_dbPath;
    return true;
}

void DatabaseManager::close()
{
    if (m_db.isOpen()) m_db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("jarvis_main"));
}

bool    DatabaseManager::isOpen()    const { return m_db.isOpen(); }
QString DatabaseManager::lastError() const { return m_lastError; }

// ============================================================
//  DDL — создание таблиц
// ============================================================

bool DatabaseManager::createTables()
{
    if (!execQuery(R"(CREATE TABLE IF NOT EXISTS schema_version (
        version INTEGER NOT NULL DEFAULT 0))")) return false;
    {
        QSqlQuery q(m_db);
        q.exec("SELECT COUNT(*) FROM schema_version");
        if (q.next() && q.value(0).toInt() == 0)
            execQuery("INSERT INTO schema_version (version) VALUES (0)");
    }

    if (!execQuery(R"(CREATE TABLE IF NOT EXISTS users (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        name        TEXT    NOT NULL DEFAULT 'User',
        scenario    TEXT    NOT NULL DEFAULT 'general',
        language    TEXT    NOT NULL DEFAULT 'auto',
        preferences TEXT             DEFAULT '{}',
        created_at  TEXT    NOT NULL DEFAULT (datetime('now')),
        last_seen   TEXT    NOT NULL DEFAULT (datetime('now'))
    ))")) return false;

    if (!execQuery(R"(CREATE TABLE IF NOT EXISTS chat_history (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id     INTEGER NOT NULL DEFAULT 1
                    REFERENCES users(id) ON DELETE CASCADE,
        role        TEXT    NOT NULL CHECK(role IN ('user','assistant','system')),
        content     TEXT    NOT NULL,
        model       TEXT    NOT NULL DEFAULT 'claude',
        session_id  TEXT    NOT NULL,
        tokens_used INTEGER NOT NULL DEFAULT 0,
        created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
    ))")) return false;
    execQuery("CREATE INDEX IF NOT EXISTS idx_chat_session ON chat_history(session_id)");
    execQuery("CREATE INDEX IF NOT EXISTS idx_chat_user    ON chat_history(user_id, created_at DESC)");
    execQuery("CREATE INDEX IF NOT EXISTS idx_chat_model   ON chat_history(user_id, model, created_at)");

    if (!execQuery(R"(CREATE TABLE IF NOT EXISTS commands (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id     INTEGER NOT NULL DEFAULT 1
                    REFERENCES users(id) ON DELETE CASCADE,
        trigger     TEXT    NOT NULL,
        action      TEXT    NOT NULL,
        type        TEXT    NOT NULL DEFAULT 'system'
                    CHECK(type IN ('system','macro','app','ai')),
        scenario    TEXT    NOT NULL DEFAULT '',
        is_enabled  INTEGER NOT NULL DEFAULT 1,
        usage_count INTEGER NOT NULL DEFAULT 0,
        created_at  TEXT    NOT NULL DEFAULT (datetime('now')),
        UNIQUE(user_id, trigger)
    ))")) return false;
    execQuery("CREATE INDEX IF NOT EXISTS idx_cmd_user    ON commands(user_id, is_enabled)");
    execQuery("CREATE INDEX IF NOT EXISTS idx_cmd_trigger ON commands(user_id, trigger)");

    if (!execQuery(R"(CREATE TABLE IF NOT EXISTS memory_kv (
        user_id    INTEGER NOT NULL DEFAULT 1
                   REFERENCES users(id) ON DELETE CASCADE,
        key        TEXT    NOT NULL,
        value      TEXT    NOT NULL DEFAULT '',
        updated_at TEXT    NOT NULL DEFAULT (datetime('now')),
        PRIMARY KEY (user_id, key)
    ))")) return false;

    if (!execQuery(R"(CREATE TABLE IF NOT EXISTS settings (
        key        TEXT PRIMARY KEY,
        value      TEXT NOT NULL DEFAULT '',
        updated_at TEXT NOT NULL DEFAULT (datetime('now'))
    ))")) return false;

    if (!execQuery(R"(CREATE TABLE IF NOT EXISTS indexed_files (
        id               INTEGER PRIMARY KEY AUTOINCREMENT,
        file_path        TEXT    NOT NULL UNIQUE,
        file_name        TEXT    NOT NULL,
        extension        TEXT    NOT NULL DEFAULT '',
        content_hash     TEXT    NOT NULL DEFAULT '',
        symbols          TEXT    NOT NULL DEFAULT '[]',
        summary          TEXT    NOT NULL DEFAULT '',
        size_bytes       INTEGER NOT NULL DEFAULT 0,
        indexed_at       TEXT    NOT NULL DEFAULT (datetime('now')),
        file_modified_at TEXT    NOT NULL DEFAULT (datetime('now'))
    ))")) return false;
    execQuery("CREATE INDEX IF NOT EXISTS idx_file_ext  ON indexed_files(extension)");
    execQuery("CREATE INDEX IF NOT EXISTS idx_file_hash ON indexed_files(content_hash)");

    if (!execQuery(R"(CREATE TABLE IF NOT EXISTS behavior_patterns (
        id         INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id    INTEGER NOT NULL DEFAULT 1
                   REFERENCES users(id) ON DELETE CASCADE,
        trigger    TEXT    NOT NULL,
        response   TEXT    NOT NULL,
        context    TEXT    NOT NULL DEFAULT '{}',
        frequency  INTEGER NOT NULL DEFAULT 1,
        confidence REAL    NOT NULL DEFAULT 0.0,
        last_seen  TEXT    NOT NULL DEFAULT (datetime('now')),
        UNIQUE(user_id, trigger, response)
    ))")) return false;
    execQuery("CREATE INDEX IF NOT EXISTS idx_pattern_user ON behavior_patterns(user_id, frequency DESC)");
    execQuery("CREATE INDEX IF NOT EXISTS idx_pattern_trig ON behavior_patterns(user_id, trigger)");

    // training_logs — лайкнутые диалоги для fine-tuning
    if (!execQuery(R"(CREATE TABLE IF NOT EXISTS training_logs (
        id           INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id      INTEGER NOT NULL DEFAULT 1
                     REFERENCES users(id) ON DELETE CASCADE,
        user_message TEXT    NOT NULL,
        ai_response  TEXT    NOT NULL,
        model        TEXT    NOT NULL DEFAULT 'claude',
        session_id   TEXT    NOT NULL DEFAULT '',
        rating       INTEGER NOT NULL DEFAULT 1,
        created_at   TEXT    NOT NULL DEFAULT (datetime('now'))
    ))")) return false;
    execQuery("CREATE INDEX IF NOT EXISTS idx_train_user ON training_logs(user_id, rating DESC)");
    execQuery("CREATE UNIQUE INDEX IF NOT EXISTS idx_train_dedup ON training_logs(user_id, user_message, ai_response)");

    // voice_journal
    if (!execQuery(R"(CREATE TABLE IF NOT EXISTS voice_journal (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id     INTEGER NOT NULL DEFAULT 1
                    REFERENCES users(id) ON DELETE CASCADE,
        transcript  TEXT    NOT NULL,
        language    TEXT    NOT NULL DEFAULT 'auto',
        confidence  REAL    NOT NULL DEFAULT 0.0,
        processed   INTEGER NOT NULL DEFAULT 0,
        created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
    ))")) return false;
    execQuery("CREATE INDEX IF NOT EXISTS idx_journal_user ON voice_journal(user_id, processed, created_at)");

    // memory_stream — Core Memory с time-decay scoring
    if (!execQuery(R"(CREATE TABLE IF NOT EXISTS memory_stream (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id     INTEGER NOT NULL DEFAULT 1
                    REFERENCES users(id) ON DELETE CASCADE,
        event_type  TEXT    NOT NULL DEFAULT 'user_query',
        content     TEXT    NOT NULL,
        importance  REAL    NOT NULL DEFAULT 0.5
                    CHECK(importance >= 0.0 AND importance <= 1.0),
        created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
    ))")) return false;
    execQuery("CREATE INDEX IF NOT EXISTS idx_memory_user ON memory_stream(user_id, created_at DESC)");
    execQuery("CREATE INDEX IF NOT EXISTS idx_memory_type ON memory_stream(user_id, event_type)");

    // response_cache — кэш AI-ответов для offline (анекдоты, советы, факты)
    if (!execQuery(R"(CREATE TABLE IF NOT EXISTS response_cache (
        id         INTEGER PRIMARY KEY AUTOINCREMENT,
        trigger    TEXT    NOT NULL,
        category   TEXT    NOT NULL DEFAULT 'chitchat',
        response   TEXT    NOT NULL,
        language   TEXT    NOT NULL DEFAULT 'ru',
        use_count  INTEGER NOT NULL DEFAULT 0,
        created_at TEXT    NOT NULL DEFAULT (datetime('now')),
        last_used  TEXT    NOT NULL DEFAULT (datetime('now'))
    ))")) return false;
    execQuery("CREATE UNIQUE INDEX IF NOT EXISTS idx_cache_trigger  ON response_cache(trigger, language)");
    execQuery("CREATE INDEX       IF NOT EXISTS idx_cache_category ON response_cache(category, language)");

    // tasks — Kanban task manager with deadlines
    if (!execQuery(R"(CREATE TABLE IF NOT EXISTS tasks (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id     INTEGER NOT NULL DEFAULT 1
                    REFERENCES users(id) ON DELETE CASCADE,
        title       TEXT    NOT NULL,
        category    TEXT    NOT NULL DEFAULT 'General',
        status      TEXT    NOT NULL DEFAULT 'Todo'
                    CHECK(status IN ('Todo','InProgress','Done')),
        priority    TEXT    NOT NULL DEFAULT 'Medium'
                    CHECK(priority IN ('Low','Medium','High')),
        deadline    TEXT,
        updated_at  TEXT    NOT NULL DEFAULT (datetime('now')),
        created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
    ))")) return false;
    execQuery("CREATE INDEX IF NOT EXISTS idx_task_user   ON tasks(user_id, status)");
    execQuery("CREATE INDEX IF NOT EXISTS idx_task_dead   ON tasks(user_id, deadline)");

    // llm_cache — Local-First Offline Fallback Cache for LLM responses
    if (!execQuery(R"(CREATE TABLE IF NOT EXISTS llm_cache (
        query_hash     TEXT PRIMARY KEY,
        original_query TEXT NOT NULL,
        response_text  TEXT NOT NULL,
        timestamp      TEXT NOT NULL DEFAULT (datetime('now'))
    ))")) return false;

    // user_personality_matrix — CuriosityEngine personality profiling
    if (!execQuery(R"(CREATE TABLE IF NOT EXISTS user_personality_matrix (
        id         INTEGER PRIMARY KEY AUTOINCREMENT,
        chat_id    INTEGER NOT NULL,
        question   TEXT NOT NULL,
        answer     TEXT NOT NULL,
        created_at TEXT NOT NULL DEFAULT (datetime('now'))
    ))")) return false;
    execQuery("CREATE INDEX IF NOT EXISTS idx_personality_chat ON user_personality_matrix(chat_id)");

    return true;
}

bool DatabaseManager::runMigrations()
{
    QSqlQuery q(m_db);
    q.exec("SELECT version FROM schema_version");
    int ver = 0;
    if (q.next()) ver = q.value(0).toInt();
    if (ver < 1) { execQuery("UPDATE schema_version SET version=1"); ver = 1; }
    if (ver < 2) { execQuery("UPDATE schema_version SET version=2"); ver = 2; }
    if (ver < 3) {
        execQuery(R"(CREATE TABLE IF NOT EXISTS training_logs (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id      INTEGER NOT NULL DEFAULT 1,
            user_message TEXT    NOT NULL,
            ai_response  TEXT    NOT NULL,
            model        TEXT    NOT NULL DEFAULT 'claude',
            session_id   TEXT    NOT NULL DEFAULT '',
            rating       INTEGER NOT NULL DEFAULT 1,
            created_at   TEXT    NOT NULL DEFAULT (datetime('now'))
        ))");
        execQuery("CREATE INDEX IF NOT EXISTS idx_train_user ON training_logs(user_id, rating DESC)");
        execQuery("CREATE UNIQUE INDEX IF NOT EXISTS idx_train_dedup ON training_logs(user_id, user_message, ai_response)");
        execQuery("UPDATE schema_version SET version=3");
        ver = 3;
    }
    if (ver < 4) {
        execQuery(R"(CREATE TABLE IF NOT EXISTS voice_journal (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id     INTEGER NOT NULL DEFAULT 1,
            transcript  TEXT    NOT NULL,
            language    TEXT    NOT NULL DEFAULT 'auto',
            confidence  REAL    NOT NULL DEFAULT 0.0,
            processed   INTEGER NOT NULL DEFAULT 0,
            created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
        ))");
        execQuery("CREATE INDEX IF NOT EXISTS idx_journal_user ON voice_journal(user_id, processed, created_at)");
        execQuery("UPDATE schema_version SET version=4");
        ver = 4;
    }
    if (ver < 5) {
        execQuery(R"(CREATE TABLE IF NOT EXISTS response_cache (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            trigger    TEXT    NOT NULL,
            category   TEXT    NOT NULL DEFAULT 'chitchat',
            response   TEXT    NOT NULL,
            language   TEXT    NOT NULL DEFAULT 'ru',
            use_count  INTEGER NOT NULL DEFAULT 0,
            created_at TEXT    NOT NULL DEFAULT (datetime('now')),
            last_used  TEXT    NOT NULL DEFAULT (datetime('now'))
        ))");
        execQuery("CREATE UNIQUE INDEX IF NOT EXISTS idx_cache_trigger  ON response_cache(trigger, language)");
        execQuery("CREATE INDEX       IF NOT EXISTS idx_cache_category ON response_cache(category, language)");
        execQuery("UPDATE schema_version SET version=5");
        ver = 5;
    }
    if (ver < 6) {
        execQuery(R"(CREATE TABLE IF NOT EXISTS activity_log (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id     INTEGER NOT NULL DEFAULT 1,
            app_name    TEXT    NOT NULL,
            window_title TEXT   NOT NULL DEFAULT '',
            category    TEXT    NOT NULL DEFAULT 'other',
            duration_sec INTEGER NOT NULL DEFAULT 0,
            hour_of_day INTEGER NOT NULL DEFAULT 0,
            day_of_week INTEGER NOT NULL DEFAULT 1,
            created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
        ))");
        execQuery("CREATE INDEX IF NOT EXISTS idx_activity_user ON activity_log(user_id, created_at DESC)");
        execQuery("CREATE INDEX IF NOT EXISTS idx_activity_cat  ON activity_log(user_id, category, hour_of_day)");
        execQuery(R"(CREATE TABLE IF NOT EXISTS knowledge_base (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id         INTEGER NOT NULL DEFAULT 1,
            category        TEXT    NOT NULL DEFAULT 'general',
            key             TEXT    NOT NULL,
            value           TEXT    NOT NULL,
            confidence      REAL    NOT NULL DEFAULT 0.5,
            reinforcements  INTEGER NOT NULL DEFAULT 1,
            learned_at      TEXT    NOT NULL DEFAULT (datetime('now')),
            last_seen       TEXT    NOT NULL DEFAULT (datetime('now')),
            UNIQUE(user_id, key)
        ))");
        execQuery("CREATE INDEX IF NOT EXISTS idx_kb_user ON knowledge_base(user_id, category)");
        execQuery("CREATE INDEX IF NOT EXISTS idx_kb_conf ON knowledge_base(user_id, confidence DESC)");
        execQuery("UPDATE schema_version SET version=6");
        ver = 6;
    }
    if (ver < 7) {
        execQuery(R"(CREATE TABLE IF NOT EXISTS memory_stream (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id     INTEGER NOT NULL DEFAULT 1,
            event_type  TEXT    NOT NULL DEFAULT 'user_query',
            content     TEXT    NOT NULL,
            importance  REAL    NOT NULL DEFAULT 0.5,
            created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
        ))");
        execQuery("CREATE INDEX IF NOT EXISTS idx_memory_user ON memory_stream(user_id, created_at DESC)");
        execQuery("CREATE INDEX IF NOT EXISTS idx_memory_type ON memory_stream(user_id, event_type)");
        execQuery("UPDATE schema_version SET version=7");
        ver = 7;
    }
    if (ver < 8) {
        execQuery(R"(CREATE TABLE IF NOT EXISTS tasks (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id     INTEGER NOT NULL DEFAULT 1,
            title       TEXT    NOT NULL,
            category    TEXT    NOT NULL DEFAULT 'General',
            status      TEXT    NOT NULL DEFAULT 'Todo',
            priority    TEXT    NOT NULL DEFAULT 'Medium',
            deadline    TEXT,
            updated_at  TEXT    NOT NULL DEFAULT (datetime('now')),
            created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
        ))");
        execQuery("CREATE INDEX IF NOT EXISTS idx_task_user ON tasks(user_id, status)");
        execQuery("CREATE INDEX IF NOT EXISTS idx_task_dead ON tasks(user_id, deadline)");
        execQuery("UPDATE schema_version SET version=8");
        ver = 8;
    }
    if (ver < 9) {
        execQuery("ALTER TABLE users ADD COLUMN current_role TEXT NOT NULL DEFAULT 'Developer'");
        execQuery("UPDATE schema_version SET version=9");
        ver = 9;
    }
    if (ver < 10) {
        execQuery("ALTER TABLE knowledge_base ADD COLUMN origin_profile_role TEXT NOT NULL DEFAULT 'local'");
        execQuery("ALTER TABLE knowledge_base ADD COLUMN linked_artifact_id INTEGER DEFAULT NULL");
        execQuery("CREATE INDEX IF NOT EXISTS idx_kb_origin ON knowledge_base(origin_profile_role)");
        execQuery("UPDATE schema_version SET version=10");
        ver = 10;
    }
    if (ver < 11) {
        execQuery(R"(CREATE TABLE IF NOT EXISTS llm_cache (
            query_hash     TEXT PRIMARY KEY,
            original_query TEXT NOT NULL,
            response_text  TEXT NOT NULL,
            timestamp      TEXT NOT NULL DEFAULT (datetime('now'))
        ))");
        execQuery("UPDATE schema_version SET version=11");
        ver = 11;
    }
    if (ver < 12) {
        execQuery(R"(CREATE TABLE IF NOT EXISTS user_personality_matrix (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            chat_id    INTEGER NOT NULL,
            question   TEXT NOT NULL,
            answer     TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT (datetime('now'))
        ))");
        execQuery("CREATE INDEX IF NOT EXISTS idx_personality_chat ON user_personality_matrix(chat_id)");
        execQuery("UPDATE schema_version SET version=12");
        ver = 12;
    }
    if (ver < 13) {
        execQuery(R"(CREATE TABLE IF NOT EXISTS user_sessions (
            chat_id    INTEGER PRIMARY KEY,
            device_id  TEXT NOT NULL,
            auth_token TEXT NOT NULL,
            pc_name    TEXT NOT NULL DEFAULT '',
            status     TEXT NOT NULL DEFAULT 'active',
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            last_used  TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (chat_id) REFERENCES telegram_users(chat_id)
        ))");
        execQuery("CREATE INDEX IF NOT EXISTS idx_sessions_device ON user_sessions(device_id)");
        execQuery("CREATE INDEX IF NOT EXISTS idx_sessions_token  ON user_sessions(auth_token)");
        execQuery(R"(CREATE TABLE IF NOT EXISTS pc_registry (
            device_id         TEXT PRIMARY KEY,
            mac_address       TEXT NOT NULL DEFAULT '',
            broadcast_address TEXT NOT NULL DEFAULT '255.255.255.255',
            pc_name           TEXT NOT NULL DEFAULT '',
            status            TEXT NOT NULL DEFAULT 'UNKNOWN',
            last_heartbeat    TEXT NOT NULL DEFAULT (datetime('now')),
            wol_port          INTEGER NOT NULL DEFAULT 9
        ))");
        execQuery("UPDATE schema_version SET version=13");
        ver = 13;
    }
    if (ver < 14) {
        // FTS5 similarity index over llm_cache.original_query — powers the
        // Layer-1 confidence router (exact/similar/none). If the linked
        // SQLite build lacks the fts5 module this CREATE VIRTUAL TABLE
        // fails silently (logged); LlmCacheManager detects the missing
        // table and falls back to a plain LIKE scan.
        execQuery(R"(CREATE VIRTUAL TABLE IF NOT EXISTS llm_cache_fts USING fts5(
            query_hash UNINDEXED,
            original_query,
            tokenize = 'unicode61 remove_diacritics 2'
        ))");
        execQuery(R"(INSERT INTO llm_cache_fts(query_hash, original_query)
            SELECT query_hash, original_query FROM llm_cache
            WHERE query_hash NOT IN (SELECT query_hash FROM llm_cache_fts))");
        execQuery("UPDATE schema_version SET version=14");
        ver = 14;
    }
    return true;
}

bool DatabaseManager::execQuery(const QString& sql)
{
    QSqlQuery q(m_db);
    if (!q.exec(sql)) { logError(sql.left(60), q.lastError()); return false; }
    return true;
}

void DatabaseManager::logError(const QString& ctx, const QSqlError& err)
{
    m_lastError = QStringLiteral("[DB] %1 | %2").arg(ctx, err.text());
    qWarning() << m_lastError;
    emit databaseError(m_lastError);
}

// ============================================================
//  users
// ============================================================

qint64 DatabaseManager::addUser(const DbUserProfile& p)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("INSERT INTO users (name, scenario, language, preferences) VALUES (:n,:s,:l,:p)");
    q.bindValue(":n", p.name);
    q.bindValue(":s", p.scenario);
    q.bindValue(":l", p.language);
    q.bindValue(":p", p.preferences);
    if (!q.exec()) { logError("addUser", q.lastError()); return -1; }
    return q.lastInsertId().toLongLong();
}

bool DatabaseManager::updateUser(const DbUserProfile& p)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("UPDATE users SET name=:n,scenario=:s,language=:l,"
              "preferences=:p,current_role=:r,last_seen=datetime('now') WHERE id=:id");
    q.bindValue(":n",  p.name);
    q.bindValue(":s",  p.scenario);
    q.bindValue(":l",  p.language);
    q.bindValue(":p",  p.preferences);
    q.bindValue(":r",  p.currentRole.isEmpty() ? QStringLiteral("Developer") : p.currentRole);
    q.bindValue(":id", p.id);
    if (!q.exec()) { logError("updateUser", q.lastError()); return false; }
    return true;
}

static DbUserProfile userFromQuery(QSqlQuery& q)
{
    DbUserProfile p;
    p.id          = q.value("id").toLongLong();
    p.name        = q.value("name").toString();
    p.scenario    = q.value("scenario").toString();
    p.language    = q.value("language").toString();
    p.preferences = q.value("preferences").toString();
    p.currentRole = q.value("current_role").toString();
    if (p.currentRole.isEmpty()) p.currentRole = QStringLiteral("Developer");
    p.createdAt   = QDateTime::fromString(q.value("created_at").toString(), Qt::ISODate);
    p.lastSeen    = QDateTime::fromString(q.value("last_seen").toString(),  Qt::ISODate);
    return p;
}

std::optional<DbUserProfile> DatabaseManager::getUser(qint64 id)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("SELECT * FROM users WHERE id=:id");
    q.bindValue(":id", id);
    if (q.exec() && q.next()) return userFromQuery(q);
    return std::nullopt;
}

QList<DbUserProfile> DatabaseManager::getAllUsers()
{
    QList<DbUserProfile> res;
    auto db = connection();
    QSqlQuery q(db);
    if (q.exec("SELECT * FROM users ORDER BY id"))
        while (q.next()) res.append(userFromQuery(q));
    return res;
}

qint64 DatabaseManager::getOrCreateDefaultUser()
{
    auto db = connection();
    QSqlQuery q(db);
    q.exec("SELECT id FROM users WHERE id=1");
    if (q.next()) return 1;

    // Нейтральный дефолт: имя аккаунта Windows, роль "general".
    // Реальные имя/роль спрашиваются мастером первого запуска
    // (ProfileSetupDialog) — не берём имя автора приложения.
    DbUserProfile def;
    QString winUser = QString::fromLocal8Bit(qgetenv("USERNAME")).trimmed();
    def.name        = winUser.isEmpty() ? QStringLiteral("User") : winUser;
    def.scenario    = QStringLiteral("general");
    def.language    = QStringLiteral("auto");
    def.preferences = QStringLiteral("{}");
    return addUser(def);
}

// ============================================================
//  chat_history
// ============================================================

qint64 DatabaseManager::addMessage(const DbChatMessage& msg)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("INSERT INTO chat_history (user_id,role,content,model,session_id,tokens_used)"
              " VALUES (:uid,:role,:content,:model,:sid,:tok)");
    q.bindValue(":uid",     msg.userId);
    q.bindValue(":role",    msg.role);
    q.bindValue(":content", msg.content);
    q.bindValue(":model",   msg.model);
    q.bindValue(":sid",     msg.sessionId.isEmpty()
                            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                            : msg.sessionId);
    q.bindValue(":tok",     msg.tokensUsed);
    if (!q.exec()) { logError("addMessage", q.lastError()); return -1; }
    qint64 newId = q.lastInsertId().toLongLong();
    emit messageSaved(newId);
    return newId;
}

static DbChatMessage chatFromQuery(QSqlQuery& q)
{
    DbChatMessage m;
    m.id         = q.value("id").toLongLong();
    m.userId     = q.value("user_id").toLongLong();
    m.role       = q.value("role").toString();
    m.content    = q.value("content").toString();
    m.model      = q.value("model").toString();
    m.sessionId  = q.value("session_id").toString();
    m.tokensUsed = q.value("tokens_used").toInt();
    m.createdAt  = QDateTime::fromString(q.value("created_at").toString(), Qt::ISODate);
    return m;
}

QList<DbChatMessage> DatabaseManager::getSession(const QString& sessionId)
{
    QList<DbChatMessage> res;
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("SELECT * FROM chat_history WHERE session_id=:sid ORDER BY id ASC");
    q.bindValue(":sid", sessionId);
    if (q.exec()) while (q.next()) res.append(chatFromQuery(q));
    return res;
}

QList<DbChatMessage> DatabaseManager::getRecentMessages(qint64 userId, int limit)
{
    QList<DbChatMessage> res;
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("SELECT * FROM chat_history WHERE user_id=:uid ORDER BY id DESC LIMIT :lim");
    q.bindValue(":uid", userId);
    q.bindValue(":lim", limit);
    if (q.exec()) while (q.next()) res.prepend(chatFromQuery(q));
    return res;
}

QList<DbChatMessage> DatabaseManager::searchMessages(qint64 userId, const QString& queryText)
{
    QList<DbChatMessage> res;
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("SELECT * FROM chat_history WHERE user_id=:uid AND content LIKE :q"
              " ORDER BY id DESC LIMIT 100");
    q.bindValue(":uid", userId);
    q.bindValue(":q",   QStringLiteral("%%1%").arg(queryText));
    if (q.exec()) while (q.next()) res.append(chatFromQuery(q));
    return res;
}

bool DatabaseManager::deleteSession(const QString& sessionId)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("DELETE FROM chat_history WHERE session_id=:sid");
    q.bindValue(":sid", sessionId);
    if (!q.exec()) { logError("deleteSession", q.lastError()); return false; }
    return true;
}

bool DatabaseManager::clearHistory(qint64 userId)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("DELETE FROM chat_history WHERE user_id=:uid");
    q.bindValue(":uid", userId);
    if (!q.exec()) { logError("clearHistory", q.lastError()); return false; }
    return true;
}

int DatabaseManager::monthlyTokens(qint64 userId, const QString& model)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("SELECT COALESCE(SUM(tokens_used),0) FROM chat_history"
              " WHERE user_id=:uid AND model=:m"
              " AND strftime('%Y-%m',created_at)=strftime('%Y-%m','now')");
    q.bindValue(":uid", userId);
    q.bindValue(":m",   model);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

QList<QMap<QString,QString>> DatabaseManager::getSessions(qint64 userId, int limit)
{
    QList<QMap<QString,QString>> res;
    auto db = connection();
    QSqlQuery q(db);
    q.prepare(R"(SELECT session_id,
                        MIN(created_at) AS started_at,
                        MAX(created_at) AS last_at,
                        COUNT(*)        AS msg_count,
                        SUM(tokens_used) AS tokens
                 FROM chat_history WHERE user_id=:uid
                 GROUP BY session_id
                 ORDER BY last_at DESC LIMIT :lim)");
    q.bindValue(":uid", userId);
    q.bindValue(":lim", limit);
    if (q.exec()) {
        while (q.next()) {
            QMap<QString,QString> row;
            row["session_id"] = q.value("session_id").toString();
            row["started_at"] = q.value("started_at").toString();
            row["last_at"]    = q.value("last_at").toString();
            row["msg_count"]  = q.value("msg_count").toString();
            row["tokens"]     = q.value("tokens").toString();
            res.append(row);
        }
    }
    return res;
}

// ============================================================
//  commands
// ============================================================

qint64 DatabaseManager::addCommand(const DbUserCommand& cmd)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO commands (user_id,trigger,action,type,scenario,is_enabled)"
              " VALUES (:uid,:tr,:act,:tp,:sc,:en)");
    q.bindValue(":uid", cmd.userId);
    q.bindValue(":tr",  cmd.trigger);
    q.bindValue(":act", cmd.action);
    q.bindValue(":tp",  cmd.type);
    q.bindValue(":sc",  cmd.scenario);
    q.bindValue(":en",  cmd.isEnabled ? 1 : 0);
    if (!q.exec()) { logError("addCommand", q.lastError()); return -1; }
    return q.lastInsertId().toLongLong();
}

bool DatabaseManager::updateCommand(const DbUserCommand& cmd)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("UPDATE commands SET trigger=:tr,action=:act,type=:tp,"
              "scenario=:sc,is_enabled=:en WHERE id=:id");
    q.bindValue(":tr",  cmd.trigger);
    q.bindValue(":act", cmd.action);
    q.bindValue(":tp",  cmd.type);
    q.bindValue(":sc",  cmd.scenario);
    q.bindValue(":en",  cmd.isEnabled ? 1 : 0);
    q.bindValue(":id",  cmd.id);
    if (!q.exec()) { logError("updateCommand", q.lastError()); return false; }
    return true;
}

bool DatabaseManager::deleteCommand(qint64 id)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("DELETE FROM commands WHERE id=:id");
    q.bindValue(":id", id);
    if (!q.exec()) { logError("deleteCommand", q.lastError()); return false; }
    return true;
}

static DbUserCommand cmdFromQuery(QSqlQuery& q)
{
    DbUserCommand c;
    c.id         = q.value("id").toLongLong();
    c.userId     = q.value("user_id").toLongLong();
    c.trigger    = q.value("trigger").toString();
    c.action     = q.value("action").toString();
    c.type       = q.value("type").toString();
    c.scenario   = q.value("scenario").toString();
    c.isEnabled  = q.value("is_enabled").toInt() != 0;
    c.usageCount = q.value("usage_count").toInt();
    c.createdAt  = QDateTime::fromString(q.value("created_at").toString(), Qt::ISODate);
    return c;
}

QList<DbUserCommand> DatabaseManager::getCommands(qint64 userId,
                                                    const QString& scenario,
                                                    const QString& type)
{
    QList<DbUserCommand> res;
    QString sql = "SELECT * FROM commands WHERE user_id=:uid AND is_enabled=1";
    if (!scenario.isEmpty()) sql += " AND (scenario=:sc OR scenario='')";
    if (!type.isEmpty())     sql += " AND type=:tp";
    sql += " ORDER BY usage_count DESC, trigger ASC";

    auto db = connection();
    QSqlQuery q(db);
    q.prepare(sql);
    q.bindValue(":uid", userId);
    if (!scenario.isEmpty()) q.bindValue(":sc", scenario);
    if (!type.isEmpty())     q.bindValue(":tp", type);
    if (q.exec()) while (q.next()) res.append(cmdFromQuery(q));
    return res;
}

std::optional<DbUserCommand> DatabaseManager::findCommand(qint64 userId, const QString& trigger)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("SELECT * FROM commands WHERE user_id=:uid AND trigger=:tr AND is_enabled=1");
    q.bindValue(":uid", userId);
    q.bindValue(":tr",  trigger);
    if (q.exec() && q.next()) return cmdFromQuery(q);
    return std::nullopt;
}

bool DatabaseManager::incrementUsage(qint64 commandId)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("UPDATE commands SET usage_count=usage_count+1 WHERE id=:id");
    q.bindValue(":id", commandId);
    if (!q.exec()) { logError("incrementUsage", q.lastError()); return false; }
    return true;
}

// ============================================================
//  memory_kv
// ============================================================

bool DatabaseManager::memSet(qint64 userId, const QString& key, const QString& value)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("INSERT INTO memory_kv (user_id,key,value,updated_at)"
              " VALUES (:uid,:k,:v,datetime('now'))"
              " ON CONFLICT(user_id,key) DO UPDATE"
              " SET value=excluded.value,updated_at=excluded.updated_at");
    q.bindValue(":uid", userId);
    q.bindValue(":k",   key);
    q.bindValue(":v",   value);
    if (!q.exec()) { logError("memSet", q.lastError()); return false; }
    return true;
}

QString DatabaseManager::memGet(qint64 userId, const QString& key, const QString& def)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("SELECT value FROM memory_kv WHERE user_id=:uid AND key=:k");
    q.bindValue(":uid", userId);
    q.bindValue(":k",   key);
    if (q.exec() && q.next()) return q.value(0).toString();
    return def;
}

bool DatabaseManager::memDelete(qint64 userId, const QString& key)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("DELETE FROM memory_kv WHERE user_id=:uid AND key=:k");
    q.bindValue(":uid", userId);
    q.bindValue(":k",   key);
    if (!q.exec()) { logError("memDelete", q.lastError()); return false; }
    return true;
}

QMap<QString,QString> DatabaseManager::memGetAll(qint64 userId)
{
    QMap<QString,QString> res;
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("SELECT key,value FROM memory_kv WHERE user_id=:uid ORDER BY key");
    q.bindValue(":uid", userId);
    if (q.exec()) while (q.next()) res.insert(q.value(0).toString(), q.value(1).toString());
    return res;
}

// ============================================================
//  settings
// ============================================================

bool DatabaseManager::setConfig(const QString& key, const QVariant& value)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("INSERT INTO settings (key,value,updated_at)"
              " VALUES (:k,:v,datetime('now'))"
              " ON CONFLICT(key) DO UPDATE"
              " SET value=excluded.value,updated_at=excluded.updated_at");
    q.bindValue(":k", key);
    q.bindValue(":v", value.toString());
    if (!q.exec()) { logError("setConfig", q.lastError()); return false; }
    return true;
}

QVariant DatabaseManager::getConfig(const QString& key, const QVariant& def)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("SELECT value FROM settings WHERE key=:k");
    q.bindValue(":k", key);
    if (q.exec() && q.next()) return q.value(0);
    return def;
}

bool DatabaseManager::deleteConfig(const QString& key)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("DELETE FROM settings WHERE key=:k");
    q.bindValue(":k", key);
    if (!q.exec()) { logError("deleteConfig", q.lastError()); return false; }
    return true;
}

// ============================================================
//  indexed_files
// ============================================================

qint64 DatabaseManager::upsertFile(const DbIndexedFile& f)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO indexed_files
                 (file_path,file_name,extension,content_hash,symbols,summary,
                  size_bytes,indexed_at,file_modified_at)
                 VALUES (:path,:name,:ext,:hash,:sym,:sum,:sz,datetime('now'),:fmod)
                 ON CONFLICT(file_path) DO UPDATE SET
                     file_name=excluded.file_name,
                     extension=excluded.extension,
                     content_hash=excluded.content_hash,
                     symbols=excluded.symbols,
                     summary=excluded.summary,
                     size_bytes=excluded.size_bytes,
                     indexed_at=excluded.indexed_at,
                     file_modified_at=excluded.file_modified_at)");
    q.bindValue(":path", f.filePath);
    q.bindValue(":name", f.fileName);
    q.bindValue(":ext",  f.extension);
    q.bindValue(":hash", f.contentHash);
    q.bindValue(":sym",  f.symbols);
    q.bindValue(":sum",  f.summary);
    q.bindValue(":sz",   f.sizeBytes);
    q.bindValue(":fmod", f.fileModifiedAt.toString(Qt::ISODate));
    if (!q.exec()) { logError("upsertFile", q.lastError()); return -1; }
    emit fileIndexed(f.filePath);
    return q.lastInsertId().toLongLong();
}

bool DatabaseManager::deleteFileIndex(const QString& filePath)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("DELETE FROM indexed_files WHERE file_path=:path");
    q.bindValue(":path", filePath);
    if (!q.exec()) { logError("deleteFileIndex", q.lastError()); return false; }
    return true;
}

static DbIndexedFile fileFromQuery(QSqlQuery& q)
{
    DbIndexedFile f;
    f.id             = q.value("id").toLongLong();
    f.filePath       = q.value("file_path").toString();
    f.fileName       = q.value("file_name").toString();
    f.extension      = q.value("extension").toString();
    f.contentHash    = q.value("content_hash").toString();
    f.symbols        = q.value("symbols").toString();
    f.summary        = q.value("summary").toString();
    f.sizeBytes      = q.value("size_bytes").toLongLong();
    f.indexedAt      = QDateTime::fromString(q.value("indexed_at").toString(), Qt::ISODate);
    f.fileModifiedAt = QDateTime::fromString(q.value("file_modified_at").toString(), Qt::ISODate);
    return f;
}

std::optional<DbIndexedFile> DatabaseManager::getFileIndex(const QString& filePath)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("SELECT * FROM indexed_files WHERE file_path=:path");
    q.bindValue(":path", filePath);
    if (q.exec() && q.next()) return fileFromQuery(q);
    return std::nullopt;
}

QList<DbIndexedFile> DatabaseManager::getIndexedFiles(const QString& ext)
{
    QList<DbIndexedFile> res;
    auto db = connection();
    QSqlQuery q(db);
    if (ext.isEmpty()) {
        q.exec("SELECT * FROM indexed_files ORDER BY file_path");
    } else {
        q.prepare("SELECT * FROM indexed_files WHERE extension=:ext ORDER BY file_path");
        q.bindValue(":ext", ext);
        q.exec();
    }
    while (q.next()) res.append(fileFromQuery(q));
    return res;
}

QList<QString> DatabaseManager::getStaleFiles(const QStringList& allPaths)
{
    QList<QString> stale;
    auto db = connection();
    for (const QString& path : allPaths) {
        QFileInfo fi(path);
        if (!fi.exists()) continue;
        QSqlQuery q(db);
        q.prepare("SELECT file_modified_at FROM indexed_files WHERE file_path=:p");
        q.bindValue(":p", path);
        if (!q.exec() || !q.next()) { stale.append(path); continue; }
        QDateTime dbMod = QDateTime::fromString(q.value(0).toString(), Qt::ISODate);
        if (fi.lastModified() > dbMod) stale.append(path);
    }
    return stale;
}

bool DatabaseManager::clearFileIndex()
{
    return execQuery("DELETE FROM indexed_files");
}

int DatabaseManager::indexedFileCount()
{
    auto db = connection();
    QSqlQuery q(db);
    q.exec("SELECT COUNT(*) FROM indexed_files");
    if (q.next()) return q.value(0).toInt();
    return 0;
}

// ============================================================
//  behavior_patterns
// ============================================================

bool DatabaseManager::upsertPattern(const DbBehaviorPattern& p)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("INSERT INTO behavior_patterns"
              " (user_id,trigger,response,context,frequency,confidence,last_seen)"
              " VALUES (:uid,:tr,:resp,:ctx,1,:conf,datetime('now'))"
              " ON CONFLICT(user_id,trigger,response) DO UPDATE SET"
              " frequency=frequency+1,"
              " confidence=excluded.confidence,"
              " context=excluded.context,"
              " last_seen=excluded.last_seen");
    q.bindValue(":uid",  p.userId);
    q.bindValue(":tr",   p.trigger);
    q.bindValue(":resp", p.response);
    q.bindValue(":ctx",  p.context);
    q.bindValue(":conf", static_cast<double>(p.confidence));
    if (!q.exec()) { logError("upsertPattern", q.lastError()); return false; }
    return true;
}

static DbBehaviorPattern patternFromQuery(QSqlQuery& q)
{
    DbBehaviorPattern p;
    p.id         = q.value("id").toLongLong();
    p.userId     = q.value("user_id").toLongLong();
    p.trigger    = q.value("trigger").toString();
    p.response   = q.value("response").toString();
    p.context    = q.value("context").toString();
    p.frequency  = q.value("frequency").toInt();
    p.confidence = q.value("confidence").toFloat();
    p.lastSeen   = QDateTime::fromString(q.value("last_seen").toString(), Qt::ISODate);
    return p;
}

QList<DbBehaviorPattern> DatabaseManager::getTopPatterns(qint64 userId, int limit)
{
    QList<DbBehaviorPattern> res;
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("SELECT * FROM behavior_patterns WHERE user_id=:uid"
              " ORDER BY frequency DESC, confidence DESC LIMIT :lim");
    q.bindValue(":uid", userId);
    q.bindValue(":lim", limit);
    if (q.exec()) while (q.next()) res.append(patternFromQuery(q));
    return res;
}

QList<DbBehaviorPattern> DatabaseManager::findPatterns(qint64 userId, const QString& triggerHint)
{
    QList<DbBehaviorPattern> res;
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("SELECT * FROM behavior_patterns WHERE user_id=:uid AND trigger LIKE :hint"
              " ORDER BY frequency DESC LIMIT 20");
    q.bindValue(":uid",  userId);
    q.bindValue(":hint", QStringLiteral("%%1%").arg(triggerHint));
    if (q.exec()) while (q.next()) res.append(patternFromQuery(q));
    return res;
}

int DatabaseManager::patternCount(qint64 userId)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM behavior_patterns WHERE user_id=:uid");
    q.bindValue(":uid", userId);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

bool DatabaseManager::clearPatterns(qint64 userId)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("DELETE FROM behavior_patterns WHERE user_id=:uid");
    q.bindValue(":uid", userId);
    if (!q.exec()) { logError("clearPatterns", q.lastError()); return false; }
    return true;
}

// ============================================================
//  training_logs — fine-tuning датасет
// ============================================================

qint64 DatabaseManager::addTrainingLog(const DbTrainingLog& log)
{
    auto db = connection();
    QSqlQuery q(db);
    // INSERT OR IGNORE — не дублируем одинаковые пары вопрос/ответ
    q.prepare(R"(INSERT OR IGNORE INTO training_logs
                 (user_id, user_message, ai_response, model, session_id, rating)
                 VALUES (:uid, :umsg, :aresp, :model, :sid, :rating))");
    q.bindValue(":uid",    log.userId);
    q.bindValue(":umsg",   log.userMessage.simplified());
    q.bindValue(":aresp",  log.aiResponse.simplified());
    q.bindValue(":model",  log.model);
    q.bindValue(":sid",    log.sessionId);
    q.bindValue(":rating", log.rating);
    if (!q.exec()) { logError("addTrainingLog", q.lastError()); return -1; }
    return q.lastInsertId().toLongLong();
}

bool DatabaseManager::deleteTrainingLog(qint64 id)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("DELETE FROM training_logs WHERE id=:id");
    q.bindValue(":id", id);
    if (!q.exec()) { logError("deleteTrainingLog", q.lastError()); return false; }
    return true;
}

QList<DbTrainingLog> DatabaseManager::getTrainingLogs(qint64 userId, int limit)
{
    QList<DbTrainingLog> res;
    auto db = connection();
    QSqlQuery q(db);
    q.prepare(R"(SELECT * FROM training_logs WHERE user_id=:uid
                 ORDER BY rating DESC, created_at DESC LIMIT :lim)");
    q.bindValue(":uid", userId);
    q.bindValue(":lim", limit);
    if (q.exec()) {
        while (q.next()) {
            DbTrainingLog t;
            t.id          = q.value("id").toLongLong();
            t.userId      = q.value("user_id").toLongLong();
            t.userMessage = q.value("user_message").toString();
            t.aiResponse  = q.value("ai_response").toString();
            t.model       = q.value("model").toString();
            t.sessionId   = q.value("session_id").toString();
            t.rating      = q.value("rating").toInt();
            t.createdAt   = QDateTime::fromString(q.value("created_at").toString(), Qt::ISODate);
            res.append(t);
        }
    }
    return res;
}

int DatabaseManager::trainingLogCount(qint64 userId, int minRating)
{
    auto db = connection();
    QSqlQuery q(db);
    // minRating < 0 (значение по умолчанию) — считаем ВСЕ пары, как раньше.
    // minRating >= 0 — считаем только пары с rating >= minRating, например
    // trainingLogCount(1, 1) даёт реальное число лайкнутых ответов вместо
    // дублирования общего total (см. фикс диалога Training Statistics).
    if (minRating < 0) {
        q.prepare("SELECT COUNT(*) FROM training_logs WHERE user_id=:uid");
        q.bindValue(":uid", userId);
    } else {
        q.prepare("SELECT COUNT(*) FROM training_logs WHERE user_id=:uid AND rating>=:minr");
        q.bindValue(":uid", userId);
        q.bindValue(":minr", minRating);
    }
    if (q.exec() && q.next()) return q.value(0).toInt();
    logError("trainingLogCount", q.lastError());
    return 0;
}

bool DatabaseManager::exportToJsonl(qint64 userId, const QString& filePath)
{
    auto logs = getTrainingLogs(userId);
    if (logs.isEmpty()) return false;

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logError("exportToJsonl: cannot open file", QSqlError());
        return false;
    }

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);

    for (const DbTrainingLog& log : logs) {
        // Формат Alpaca / Unsloth / LLaMA-Factory
        QJsonObject obj;
        obj["instruction"] = log.userMessage;
        obj["input"]       = QString();
        obj["output"]      = log.aiResponse;
        obj["model"]       = log.model;
        out << QJsonDocument(obj).toJson(QJsonDocument::Compact) << "\n";
    }

    f.close();
    qDebug() << "[DB] Exported" << logs.size() << "training logs to:" << filePath;
    return true;
}

int DatabaseManager::cleanupTrainingLogs(qint64 userId)
{
    auto db = connection();
    int removed = 0;
    QSqlQuery q(db);

    // 1. Удаляем слишком короткие сообщения пользователя (< 5 символов)
    q.prepare("DELETE FROM training_logs WHERE user_id=:uid AND length(user_message) < 5");
    q.bindValue(":uid", userId);
    q.exec();
    removed += q.numRowsAffected();

    // 2. Удаляем слишком короткие ответы AI (< 20 символов — мусор)
    q.prepare("DELETE FROM training_logs WHERE user_id=:uid AND length(ai_response) < 20");
    q.bindValue(":uid", userId);
    q.exec();
    removed += q.numRowsAffected();

    // 3. Удаляем типичные ошибки распознавания речи
    const QStringList noisePatterns = {
        "...", "???", "хм", "мм", "ну", "эм", "ага", "ок", "окей",
        "ok", "uh", "um", "hmm", "yeah", "yep", "nope"
    };
    for (const QString& pattern : noisePatterns) {
        q.prepare("DELETE FROM training_logs WHERE user_id=:uid "
                  "AND lower(trim(user_message))=:p");
        q.bindValue(":uid", userId);
        q.bindValue(":p",   pattern.toLower());
        q.exec();
        removed += q.numRowsAffected();
    }

    // 4. Оставляем только уникальные пары (дубли уже отсечены UNIQUE INDEX,
    //    но могут быть "почти дубли" с разными пробелами — нормализация при вставке)

    qDebug() << "[DB] Cleanup removed" << removed << "training log entries";
    return removed;
}

// ============================================================
//  voice_journal — пассивная запись голоса
// ============================================================

qint64 DatabaseManager::addVoiceJournalEntry(const QString& transcript,
                                              const QString& language,
                                              float confidence,
                                              const QDateTime& capturedAt)
{
    if (transcript.trimmed().length() < 3) return -1;

    auto db = connection();
    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO voice_journal (user_id, transcript, language, confidence, created_at)
                 VALUES (:uid, :tr, :lang, :conf, :at))");
    q.bindValue(":uid",  1);
    q.bindValue(":tr",   transcript.simplified());
    q.bindValue(":lang", language);
    q.bindValue(":conf", static_cast<double>(confidence));
    q.bindValue(":at",   capturedAt.toString(Qt::ISODate));
    if (!q.exec()) { logError("addVoiceJournalEntry", q.lastError()); return -1; }
    return q.lastInsertId().toLongLong();
}

bool DatabaseManager::markJournalEntryProcessed(qint64 id)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("UPDATE voice_journal SET processed=1 WHERE id=:id");
    q.bindValue(":id", id);
    if (!q.exec()) { logError("markJournalEntryProcessed", q.lastError()); return false; }
    return true;
}

QList<QMap<QString,QVariant>> DatabaseManager::getUnprocessedJournalEntries(qint64 userId, int limit)
{
    QList<QMap<QString,QVariant>> res;
    auto db = connection();
    QSqlQuery q(db);
    q.prepare(R"(SELECT id, transcript, language, confidence, created_at
                 FROM voice_journal
                 WHERE user_id=:uid AND processed=0
                 ORDER BY created_at ASC
                 LIMIT :lim)");
    q.bindValue(":uid", userId);
    q.bindValue(":lim", limit);
    if (q.exec()) {
        while (q.next()) {
            QMap<QString,QVariant> row;
            row["id"]         = q.value("id").toLongLong();
            row["transcript"] = q.value("transcript").toString();
            row["language"]   = q.value("language").toString();
            row["confidence"] = q.value("confidence").toFloat();
            row["created_at"] = QDateTime::fromString(q.value("created_at").toString(), Qt::ISODate);
            res.append(row);
        }
    }
    return res;
}

int DatabaseManager::voiceJournalCount(qint64 userId, bool processedOnly)
{
    auto db = connection();
    QSqlQuery q(db);
    if (processedOnly) {
        q.prepare("SELECT COUNT(*) FROM voice_journal WHERE user_id=:uid AND processed=1");
    } else {
        q.prepare("SELECT COUNT(*) FROM voice_journal WHERE user_id=:uid");
    }
    q.bindValue(":uid", userId);
    if (q.exec() && q.next()) return q.value(0).toInt();
    logError("voiceJournalCount", q.lastError());
    return 0;
}

int DatabaseManager::cleanupOldJournalEntries(qint64 userId, int olderThanDays)
{
    auto db = connection();
    QSqlQuery q(db);
    // Удаляем обработанные записи старше N дней
    q.prepare(R"(DELETE FROM voice_journal
                 WHERE user_id=:uid AND processed=1
                 AND created_at < datetime('now', :days))");
    q.bindValue(":uid",  userId);
    q.bindValue(":days", QStringLiteral("-%1 days").arg(olderThanDays));
    if (!q.exec()) { logError("cleanupOldJournalEntries", q.lastError()); return 0; }
    int removed = q.numRowsAffected();

    // Также удаляем необработанные старше 2x периода (мусор который никто не обработал)
    q.prepare(R"(DELETE FROM voice_journal
                 WHERE user_id=:uid AND processed=0
                 AND created_at < datetime('now', :days))");
    q.bindValue(":uid",  userId);
    q.bindValue(":days", QStringLiteral("-%1 days").arg(olderThanDays * 2));
    if (q.exec()) removed += q.numRowsAffected();

    qDebug() << "[DB] voice_journal cleanup: deleted" << removed << "entries";
    return removed;
}

bool DatabaseManager::updateTrainingLogRating(const QString& userMsg,
                                               const QString& aiResp, int rating)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare(R"(UPDATE training_logs SET rating=:r
                 WHERE user_id=1
                   AND user_message=:umsg
                   AND ai_response=:aresp)");
    q.bindValue(":r",     rating);
    q.bindValue(":umsg",  userMsg.simplified());
    q.bindValue(":aresp", aiResp.simplified());
    if (!q.exec()) { logError("updateTrainingLogRating", q.lastError()); return false; }
    return q.numRowsAffected() > 0;
}

// ============================================================
//  memory_stream — Core Memory с time-decay
// ============================================================

qint64 DatabaseManager::addMemoryEvent(const DbMemoryEvent& ev)
{
    if (ev.content.trimmed().isEmpty()) return -1;

    auto db = connection();
    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO memory_stream (user_id, event_type, content, importance)
                 VALUES (:uid, :et, :content, :imp))");
    q.bindValue(":uid",     ev.userId);
    q.bindValue(":et",      ev.eventType.isEmpty() ? QStringLiteral("user_query") : ev.eventType);
    q.bindValue(":content", ev.content.simplified());
    q.bindValue(":imp",     qBound(0.0, ev.importance, 1.0));
    if (!q.exec()) { logError("addMemoryEvent", q.lastError()); return -1; }
    return q.lastInsertId().toLongLong();
}

QList<DbMemoryEvent> DatabaseManager::getTopMemoryEvents(qint64 userId, int limit)
{
    QList<DbMemoryEvent> res;
    auto db = connection();
    QSqlQuery q(db);
    // Score = importance / (1.0 + delta_hours)
    // delta_hours = (unixepoch('now') - unixepoch(created_at)) / 3600.0
    // SQLite strftime('%s', ...) returns Unix epoch as TEXT; CAST to REAL.
    q.prepare(R"(SELECT id, user_id, event_type, content, importance, created_at,
                        importance / (1.0 + (CAST(strftime('%s','now') AS REAL)
                                           - CAST(strftime('%s', created_at) AS REAL)) / 3600.0) AS score
                 FROM memory_stream
                 WHERE user_id = :uid
                 ORDER BY score DESC
                 LIMIT :lim)");
    q.bindValue(":uid", userId);
    q.bindValue(":lim", limit);
    if (q.exec()) {
        while (q.next()) {
            DbMemoryEvent ev;
            ev.id         = q.value("id").toLongLong();
            ev.userId     = q.value("user_id").toLongLong();
            ev.eventType  = q.value("event_type").toString();
            ev.content    = q.value("content").toString();
            ev.importance = q.value("importance").toDouble();
            ev.createdAt  = QDateTime::fromString(q.value("created_at").toString(), Qt::ISODate);
            res.append(ev);
        }
    }
    return res;
}

// ============================================================
//  response_cache
// ============================================================

QString DatabaseManager::getCachedResponse(const QString& trigger,
                                            const QString& language,
                                            const QString& category)
{
    auto db = connection();
    QSqlQuery q(db);
    if (category.isEmpty()) {
        q.prepare(R"(SELECT id, response FROM response_cache
                     WHERE trigger=:t AND language=:lang
                     ORDER BY use_count DESC LIMIT 1)");
    } else {
        q.prepare(R"(SELECT id, response FROM response_cache
                     WHERE trigger=:t AND language=:lang AND category=:cat
                     ORDER BY use_count DESC LIMIT 1)");
        q.bindValue(":cat", category);
    }
    q.bindValue(":t",    trigger.trimmed().toLower());
    q.bindValue(":lang", language);
    if (!q.exec() || !q.next()) return QString();
    qint64 id = q.value(0).toLongLong();
    QString response = q.value(1).toString();
    QSqlQuery upd(db);
    upd.prepare("UPDATE response_cache SET use_count=use_count+1,last_used=datetime('now') WHERE id=:id");
    upd.bindValue(":id", id);
    upd.exec();
    return response;
}

bool DatabaseManager::saveCachedResponse(const QString& trigger,
                                          const QString& response,
                                          const QString& language,
                                          const QString& category)
{
    if (trigger.isEmpty() || response.isEmpty()) return false;
    auto db = connection();
    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO response_cache (trigger, category, response, language)
                 VALUES (:t, :cat, :resp, :lang)
                 ON CONFLICT(trigger, language)
                 DO UPDATE SET response=excluded.response,
                               category=excluded.category,
                               last_used=datetime('now'))");
    q.bindValue(":t",    trigger.trimmed().toLower());
    q.bindValue(":cat",  category);
    q.bindValue(":resp", response.trimmed());
    q.bindValue(":lang", language);
    if (!q.exec()) { logError("saveCachedResponse", q.lastError()); return false; }
    return true;
}

QString DatabaseManager::getRandomCached(const QString& category, const QString& language)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare(R"(SELECT id, response FROM response_cache
                 WHERE category=:cat AND language=:lang
                 ORDER BY use_count ASC, RANDOM() LIMIT 1)");
    q.bindValue(":cat",  category);
    q.bindValue(":lang", language);
    if (!q.exec() || !q.next()) return QString();
    qint64 id = q.value(0).toLongLong();
    QString response = q.value(1).toString();
    QSqlQuery upd(db);
    upd.prepare("UPDATE response_cache SET use_count=use_count+1,last_used=datetime('now') WHERE id=:id");
    upd.bindValue(":id", id);
    upd.exec();
    return response;
}

int DatabaseManager::responseCacheCount()
{
    auto db = connection();
    QSqlQuery q(db);
    q.exec("SELECT COUNT(*) FROM response_cache");
    return q.next() ? q.value(0).toInt() : 0;
}

// ============================================================
//  tasks — Kanban task manager
// ============================================================

qint64 DatabaseManager::addTask(const DbTask& t)
{
    QMutexLocker lock(&m_mutex);
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("INSERT INTO tasks (user_id,title,category,status,priority,deadline,updated_at)"
              " VALUES(:uid,:title,:cat,:st,:pri,:dl,datetime('now'))");
    q.bindValue(":uid",   t.userId);
    q.bindValue(":title", t.title);
    q.bindValue(":cat",   t.category);
    q.bindValue(":st",    t.status);
    q.bindValue(":pri",   t.priority);
    q.bindValue(":dl",    t.deadline.isValid() ? t.deadline.toString(Qt::ISODate) : QVariant());
    if (!q.exec()) { logError("addTask", q.lastError()); return -1; }
    return q.lastInsertId().toLongLong();
}

bool DatabaseManager::updateTask(const DbTask& t)
{
    QMutexLocker lock(&m_mutex);
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("UPDATE tasks SET title=:title,category=:cat,status=:st,"
              "priority=:pri,deadline=:dl,updated_at=datetime('now') WHERE id=:id");
    q.bindValue(":title", t.title);
    q.bindValue(":cat",   t.category);
    q.bindValue(":st",    t.status);
    q.bindValue(":pri",   t.priority);
    q.bindValue(":dl",    t.deadline.isValid() ? t.deadline.toString(Qt::ISODate) : QVariant());
    q.bindValue(":id",    t.id);
    if (!q.exec()) { logError("updateTask", q.lastError()); return false; }
    return true;
}

bool DatabaseManager::deleteTask(qint64 id)
{
    QMutexLocker lock(&m_mutex);
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("DELETE FROM tasks WHERE id=:id");
    q.bindValue(":id", id);
    if (!q.exec()) { logError("deleteTask", q.lastError()); return false; }
    return true;
}

static DbTask taskFromQuery(QSqlQuery& q)
{
    DbTask t;
    t.id        = q.value("id").toLongLong();
    t.userId    = q.value("user_id").toLongLong();
    t.title     = q.value("title").toString();
    t.category  = q.value("category").toString();
    t.status    = q.value("status").toString();
    t.priority  = q.value("priority").toString();
    t.deadline  = q.value("deadline").isNull()
                      ? QDateTime()
                      : QDateTime::fromString(q.value("deadline").toString(), Qt::ISODate);
    t.updatedAt = QDateTime::fromString(q.value("updated_at").toString(), Qt::ISODate);
    t.createdAt = QDateTime::fromString(q.value("created_at").toString(), Qt::ISODate);
    return t;
}

QList<DbTask> DatabaseManager::getTasks(qint64 userId, const QString& status)
{
    auto db = connection();
    QSqlQuery q(db);
    if (status.isEmpty()) {
        q.prepare("SELECT * FROM tasks WHERE user_id=:uid ORDER BY"
                  " CASE priority WHEN 'High' THEN 0 WHEN 'Medium' THEN 1 ELSE 2 END,"
                  " created_at DESC");
    } else {
        q.prepare("SELECT * FROM tasks WHERE user_id=:uid AND status=:st ORDER BY"
                  " CASE priority WHEN 'High' THEN 0 WHEN 'Medium' THEN 1 ELSE 2 END,"
                  " created_at DESC");
        q.bindValue(":st", status);
    }
    q.bindValue(":uid", userId);
    QList<DbTask> result;
    if (q.exec()) { while (q.next()) result.append(taskFromQuery(q)); }
    else logError("getTasks", q.lastError());
    return result;
}

std::optional<DbTask> DatabaseManager::getTask(qint64 id)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("SELECT * FROM tasks WHERE id=:id");
    q.bindValue(":id", id);
    if (q.exec() && q.next()) return taskFromQuery(q);
    return std::nullopt;
}

QList<DbTask> DatabaseManager::getOverdueTasks(qint64 userId, int withinHours)
{
    auto db = connection();
    QSqlQuery q(db);
    q.prepare("SELECT * FROM tasks WHERE user_id=:uid AND status!='Done'"
              " AND deadline IS NOT NULL"
              " AND deadline <= datetime('now', '+' || :hrs || ' hours')"
              " ORDER BY deadline ASC");
    q.bindValue(":uid", userId);
    q.bindValue(":hrs", withinHours);
    QList<DbTask> result;
    if (q.exec()) { while (q.next()) result.append(taskFromQuery(q)); }
    else logError("getOverdueTasks", q.lastError());
    return result;
}
