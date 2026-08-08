// ============================================================
// file_organizer.cpp — Content-aware file organization
// ============================================================

#include "file_organizer.h"
#include "content_search.h"
#include "claude_api.h"
#include "pc_controller.h"
#include "database_manager.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

// ============================================================
//  OrganizePlan helpers
// ============================================================

QVector<QPair<QString, int>> OrganizePlan::categoryCounts() const
{
    QVector<QPair<QString, int>> result;
    for (const auto& item : items) {
        bool found = false;
        for (auto& pair : result) {
            if (pair.first == item.category) { ++pair.second; found = true; break; }
        }
        if (!found) result.append({item.category, 1});
    }
    return result;
}

// ============================================================
//  Singleton
// ============================================================

FileOrganizer& FileOrganizer::instance()
{
    static FileOrganizer inst;
    return inst;
}

FileOrganizer::FileOrganizer(QObject* parent) : QObject(parent)
{
}

void FileOrganizer::ensureTable()
{
    if (!DatabaseManager::instance().isOpen()) return;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS organize_journal ("
        "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  batch_id   TEXT NOT NULL,"
        "  from_path  TEXT NOT NULL,"
        "  to_path    TEXT NOT NULL,"
        "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
        ")"));
    if (q.lastError().isValid())
        qWarning() << "[FileOrganizer] Table creation error:" << q.lastError().text();
}

// ============================================================
//  Rules — default set, persistence, extension lookup
// ============================================================

QVector<OrganizeRule> FileOrganizer::defaultRules()
{
    return {
        { QStringLiteral("Документы"),
          {"doc", "docx", "odt", "rtf", "pdf", "txt", "md", "epub", "fb2"},
          true,
          {QStringLiteral("Учёба"), QStringLiteral("Работа"), QStringLiteral("Личное"),
           QStringLiteral("Финансы"), QStringLiteral("Другое")} },

        { QStringLiteral("Изображения"),
          {"jpg", "jpeg", "png", "gif", "bmp", "webp", "svg", "heic", "tiff"},
          false, {} },

        { QStringLiteral("Архивы"),
          {"zip", "rar", "7z", "tar", "gz"},
          false, {} },

        { QStringLiteral("Установщики"),
          {"exe", "msi"},
          false, {} },

        { QStringLiteral("Видео"),
          {"mp4", "mkv", "avi", "mov", "webm"},
          false, {} },

        { QStringLiteral("Аудио"),
          {"mp3", "wav", "flac", "ogg", "m4a"},
          true,
          {QStringLiteral("Музыка"), QStringLiteral("Подкасты и аудиокниги"),
           QStringLiteral("Звуковые эффекты")} },

        { QStringLiteral("Таблицы"),
          {"xls", "xlsx", "csv"},
          false, {} },

        { QStringLiteral("Презентации"),
          {"ppt", "pptx"},
          false, {} },

        { QStringLiteral("3D модели"),
          {"obj", "fbx", "stl", "blend", "3ds", "dae", "gltf", "glb", "ply", "skp"},
          true,
          {QStringLiteral("Игровые ассеты"), QStringLiteral("3D-печать"),
           QStringLiteral("Референсы и сцены")} },

        { QStringLiteral("Код"),
          {"cpp", "h", "hpp", "py", "js", "ts", "java", "cs", "html", "css",
           "json", "xml", "sql", "sh", "bat", "ps1", "qml"},
          false, {} },
    };
}

static QJsonArray rulesToJson(const QVector<OrganizeRule>& rules)
{
    QJsonArray arr;
    for (const auto& r : rules) {
        QJsonObject obj;
        obj[QStringLiteral("category")] = r.category;
        obj[QStringLiteral("extensions")] = QJsonArray::fromStringList(r.extensions);
        obj[QStringLiteral("contextAware")] = r.contextAware;
        obj[QStringLiteral("subcategories")] = QJsonArray::fromStringList(r.subcategories);
        arr.append(obj);
    }
    return arr;
}

static QVector<OrganizeRule> rulesFromJson(const QJsonArray& arr)
{
    QVector<OrganizeRule> rules;
    for (const auto& val : arr) {
        const QJsonObject obj = val.toObject();
        OrganizeRule r;
        r.category = obj[QStringLiteral("category")].toString();
        for (const auto& e : obj[QStringLiteral("extensions")].toArray())
            r.extensions.append(e.toString());
        r.contextAware = obj[QStringLiteral("contextAware")].toBool();
        for (const auto& s : obj[QStringLiteral("subcategories")].toArray())
            r.subcategories.append(s.toString());
        if (!r.category.isEmpty()) rules.append(r);
    }
    return rules;
}

void FileOrganizer::loadRulesIfNeeded() const
{
    if (m_rulesLoaded) return;
    m_rulesLoaded = true;

    const QVariant stored = DatabaseManager::instance().getConfig(
        QStringLiteral("file_organizer.rules"));
    if (stored.isValid() && !stored.toString().isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(stored.toString().toUtf8());
        if (doc.isArray()) {
            const auto parsed = rulesFromJson(doc.array());
            if (!parsed.isEmpty()) {
                m_rulesCache = parsed;
                return;
            }
        }
    }

    // Nothing stored yet (or it was corrupt) — seed with defaults.
    m_rulesCache = defaultRules();
    DatabaseManager::instance().setConfig(
        QStringLiteral("file_organizer.rules"),
        QString::fromUtf8(QJsonDocument(rulesToJson(m_rulesCache)).toJson(QJsonDocument::Compact)));
}

QVector<OrganizeRule> FileOrganizer::rules() const
{
    loadRulesIfNeeded();
    return m_rulesCache;
}

void FileOrganizer::setRules(const QVector<OrganizeRule>& rules)
{
    m_rulesCache = rules;
    m_rulesLoaded = true;
    DatabaseManager::instance().setConfig(
        QStringLiteral("file_organizer.rules"),
        QString::fromUtf8(QJsonDocument(rulesToJson(rules)).toJson(QJsonDocument::Compact)));
}

void FileOrganizer::resetRulesToDefault()
{
    setRules(defaultRules());
}

const OrganizeRule* FileOrganizer::ruleForCategory(const QString& category) const
{
    for (const auto& r : m_rulesCache) {
        if (r.category == category) return &r;
    }
    return nullptr;
}

QString FileOrganizer::classifyByExtension(const QString& fileName) const
{
    loadRulesIfNeeded();
    const QString ext = QFileInfo(fileName).suffix().toLower();
    for (const auto& r : m_rulesCache) {
        if (r.extensions.contains(ext)) return r.category;
    }
    return QString(); // empty = ambiguous, needs content classification
}

// ============================================================
//  Build plan
// ============================================================

void FileOrganizer::buildPlan(const QString& targetFolder,
                              std::function<void(const OrganizePlan&)> callback)
{
    loadRulesIfNeeded();

    OrganizePlan plan;
    plan.targetFolder = targetFolder;
    plan.builtAt = QDateTime::currentDateTime();

    QDir dir(targetFolder);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot, QDir::Name);

    QVector<int> catIndices; // need a top-level category from the LLM
    QVector<int> subIndices; // matched a contextAware rule, need a subcategory

    for (const QFileInfo& fi : entries) {
        OrganizeItem item;
        item.filePath  = fi.absoluteFilePath();
        item.fileName  = fi.fileName();
        item.sizeBytes = fi.size();

        const QString cat = classifyByExtension(item.fileName);
        if (!cat.isEmpty()) {
            item.category  = cat;
            item.confident = true;
            const OrganizeRule* rule = ruleForCategory(cat);
            if (rule && rule->contextAware && !rule->subcategories.isEmpty())
                subIndices.append(plan.items.size());
        } else {
            item.category  = QStringLiteral("Нераспознано");
            item.confident = false;
            catIndices.append(plan.items.size());
        }
        plan.items.append(item);
    }

    if ((catIndices.isEmpty() && subIndices.isEmpty()) || !m_api) {
        if (callback) callback(plan);
        return;
    }

    // Copy plan into heap-owned shared state so the async LLM callback
    // (may fire much later) can still find it.
    auto planPtr = std::make_shared<OrganizePlan>(plan);
    classifyWithLlm(catIndices, subIndices, *planPtr, [planPtr, callback]() {
        if (callback) callback(*planPtr);
    });
}

void FileOrganizer::classifyWithLlm(const QVector<int>& catIndices, const QVector<int>& subIndices,
                                    OrganizePlan& plan, std::function<void()> onDone)
{
    // Cap the combined batch so the prompt stays a reasonable size.
    static constexpr int MAX_BATCH = 30;
    static constexpr int SNIPPET_CHARS = 600;

    QStringList categoryNames;
    for (const auto& r : m_rulesCache) categoryNames.append(r.category);
    categoryNames.append(QStringLiteral("Другое"));

    QString prompt = QStringLiteral(
        "[FILE CLASSIFICATION TASK]\n"
        "Each file below needs either a \"category\" or a \"subcategory\" — the "
        "task and the allowed options are given per file. Respond with ONLY a "
        "JSON array like [{\"file\":\"name.pdf\",\"value\":\"Документы\"}] — one "
        "entry per file, no other text. Pick exactly one value from that file's "
        "options list. If you cannot tell, use the last option in the list.\n\n"
        "Files:\n");

    int included = 0;
    auto appendFile = [&](int idx, const QString& need, const QStringList& options) {
        if (included >= MAX_BATCH) return;
        const OrganizeItem& item = plan.items[idx];
        const QString snippet = ContentSearch::isSupportedFormat(item.filePath)
            ? ContentSearch::extractText(item.filePath, SNIPPET_CHARS)
            : QString();
        prompt += QStringLiteral("- %1 [need: %2, options: %3]: %4\n").arg(
            item.fileName, need, options.join(QStringLiteral(", ")),
            snippet.isEmpty() ? QStringLiteral("(no readable text)") : snippet.left(SNIPPET_CHARS));
        ++included;
    };

    for (int idx : catIndices) {
        if (included >= MAX_BATCH) break;
        appendFile(idx, QStringLiteral("category"), categoryNames);
    }
    for (int idx : subIndices) {
        if (included >= MAX_BATCH) break;
        const OrganizeRule* rule = ruleForCategory(plan.items[idx].category);
        if (!rule) continue;
        appendFile(idx, QStringLiteral("subcategory"), rule->subcategories);
    }

    if (!m_api) { if (onDone) onDone(); return; }

    m_api->sendMessage(prompt, [this, &plan, onDone](bool ok, const QString& resp) {
        if (ok) {
            // Extract the JSON array even if the model wrapped it in prose/fences.
            const int start = resp.indexOf(QLatin1Char('['));
            const int end   = resp.lastIndexOf(QLatin1Char(']'));
            if (start >= 0 && end > start) {
                const QJsonDocument doc =
                    QJsonDocument::fromJson(resp.mid(start, end - start + 1).toUtf8());
                if (doc.isArray()) {
                    for (const auto& val : doc.array()) {
                        const QJsonObject obj = val.toObject();
                        const QString file  = obj[QStringLiteral("file")].toString();
                        const QString value = obj[QStringLiteral("value")].toString();
                        if (file.isEmpty() || value.isEmpty()) continue;
                        for (auto& item : plan.items) {
                            if (item.fileName != file) continue;
                            const OrganizeRule* rule = ruleForCategory(item.category);
                            if (rule && rule->contextAware && rule->subcategories.contains(value)) {
                                item.subcategory = value;
                            } else {
                                item.category  = value;
                                item.confident = (value != QStringLiteral("Другое"));
                            }
                            break;
                        }
                    }
                }
            }
        } else {
            qWarning() << "[FileOrganizer] Classification LLM call failed:" << resp;
        }
        if (onDone) onDone();
    });
}

// ============================================================
//  Apply / undo
// ============================================================

QString FileOrganizer::applyPlan(const OrganizePlan& plan, SystemController* sys)
{
    if (!sys) return QString();
    ensureTable();

    const QString batchId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    int moved = 0;

    for (const auto& item : plan.items) {
        if (!item.confident) continue; // "Нераспознано" — leave alone

        QString destDir = QDir(plan.targetFolder).filePath(item.category);
        if (!item.subcategory.isEmpty())
            destDir = QDir(destDir).filePath(item.subcategory);
        const QString destPath = QDir(destDir).filePath(item.fileName);

        if (!sys->createFolder(destDir)) continue;
        if (!sys->moveFile(item.filePath, destPath)) continue;

        if (db.isOpen()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "INSERT INTO organize_journal (batch_id, from_path, to_path) "
                "VALUES (:bid, :from, :to)"));
            q.bindValue(QStringLiteral(":bid"),  batchId);
            q.bindValue(QStringLiteral(":from"), item.filePath);
            q.bindValue(QStringLiteral(":to"),   destPath);
            q.exec();
        }
        ++moved;
    }

    emit batchApplied(batchId, moved);
    qDebug() << "[FileOrganizer] Applied batch" << batchId << "-" << moved << "file(s) moved";
    return moved > 0 ? batchId : QString();
}

bool FileOrganizer::undoLastBatch(SystemController* sys)
{
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return false;

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "SELECT batch_id FROM organize_journal ORDER BY id DESC LIMIT 1"));
    if (!q.next()) return false;

    return undoBatch(q.value(0).toString(), sys);
}

bool FileOrganizer::undoBatch(const QString& batchId, SystemController* sys)
{
    if (!sys || batchId.isEmpty()) return false;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return false;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT from_path, to_path FROM organize_journal WHERE batch_id = :bid"));
    q.bindValue(QStringLiteral(":bid"), batchId);
    if (!q.exec()) return false;

    int restored = 0;
    QVector<QPair<QString, QString>> moves;
    while (q.next())
        moves.append({q.value(0).toString(), q.value(1).toString()});

    for (const auto& mv : moves) {
        const QString& originalPath = mv.first;
        const QString& currentPath  = mv.second;
        if (sys->moveFile(currentPath, originalPath))
            ++restored;
    }

    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM organize_journal WHERE batch_id = :bid"));
    del.bindValue(QStringLiteral(":bid"), batchId);
    del.exec();

    emit batchUndone(batchId, restored);
    qDebug() << "[FileOrganizer] Undid batch" << batchId << "-" << restored << "file(s) restored";
    return restored > 0;
}
