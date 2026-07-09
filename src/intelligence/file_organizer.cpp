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
//  Extension-based fast classification
// ============================================================

QString FileOrganizer::classifyByExtension(const QString& fileName)
{
    const QString ext = QFileInfo(fileName).suffix().toLower();

    static const QHash<QString, QString> byExt = {
        {"jpg", "Изображения"}, {"jpeg", "Изображения"}, {"png", "Изображения"},
        {"gif", "Изображения"}, {"bmp", "Изображения"}, {"webp", "Изображения"},
        {"svg", "Изображения"}, {"heic", "Изображения"}, {"tiff", "Изображения"},

        {"zip", "Архивы"}, {"rar", "Архивы"}, {"7z", "Архивы"},
        {"tar", "Архивы"}, {"gz", "Архивы"},

        {"exe", "Установщики"}, {"msi", "Установщики"},

        {"mp4", "Видео"}, {"mkv", "Видео"}, {"avi", "Видео"},
        {"mov", "Видео"}, {"webm", "Видео"},

        {"mp3", "Аудио"}, {"wav", "Аудио"}, {"flac", "Аудио"},
        {"ogg", "Аудио"}, {"m4a", "Аудио"},

        {"xls", "Таблицы"}, {"xlsx", "Таблицы"}, {"csv", "Таблицы"},
        {"ppt", "Презентации"}, {"pptx", "Презентации"},
        {"doc", "Документы"}, {"docx", "Документы"}, {"odt", "Документы"}, {"rtf", "Документы"},

        {"cpp", "Код"}, {"h", "Код"}, {"hpp", "Код"}, {"py", "Код"}, {"js", "Код"},
        {"ts", "Код"}, {"java", "Код"}, {"cs", "Код"}, {"html", "Код"}, {"css", "Код"},
        {"json", "Код"}, {"xml", "Код"}, {"sql", "Код"}, {"sh", "Код"}, {"bat", "Код"}, {"ps1", "Код"},
    };

    return byExt.value(ext, QString()); // empty = ambiguous, needs content classification
}

// ============================================================
//  Build plan
// ============================================================

void FileOrganizer::buildPlan(const QString& targetFolder,
                              std::function<void(const OrganizePlan&)> callback)
{
    OrganizePlan plan;
    plan.targetFolder = targetFolder;
    plan.builtAt = QDateTime::currentDateTime();

    QDir dir(targetFolder);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot, QDir::Name);

    QVector<int> ambiguousIndices;

    for (const QFileInfo& fi : entries) {
        OrganizeItem item;
        item.filePath  = fi.absoluteFilePath();
        item.fileName  = fi.fileName();
        item.sizeBytes = fi.size();

        const QString cat = classifyByExtension(item.fileName);
        if (!cat.isEmpty()) {
            item.category  = cat;
            item.confident = true;
        } else {
            item.category  = QStringLiteral("Нераспознано");
            item.confident = false;
            ambiguousIndices.append(plan.items.size());
        }
        plan.items.append(item);
    }

    if (ambiguousIndices.isEmpty() || !m_api) {
        if (callback) callback(plan);
        return;
    }

    // Copy plan into heap-owned shared state so the async LLM callback
    // (may fire much later) can still find it.
    auto planPtr = std::make_shared<OrganizePlan>(plan);
    classifyAmbiguous(ambiguousIndices, *planPtr, [planPtr, callback]() {
        if (callback) callback(*planPtr);
    });
}

void FileOrganizer::classifyAmbiguous(const QVector<int>& indices, OrganizePlan& plan,
                                       std::function<void()> onDone)
{
    // Cap the batch so the prompt stays a reasonable size.
    static constexpr int MAX_BATCH = 30;
    static constexpr int SNIPPET_CHARS = 600;

    QString prompt = QStringLiteral(
        "[FILE CLASSIFICATION TASK]\n"
        "For each file below, pick exactly one category from this list:\n"
        "Документы, Изображения, Архивы, Установщики, Видео, Аудио, Таблицы, "
        "Презентации, Код, Другое\n"
        "If you cannot tell, use \"Другое\".\n"
        "Respond with ONLY a JSON array like "
        "[{\"file\":\"name.pdf\",\"category\":\"Документы\"}] — no other text.\n\n"
        "Files:\n");

    int included = 0;
    for (int idx : indices) {
        if (included >= MAX_BATCH) break;
        const OrganizeItem& item = plan.items[idx];
        const QString snippet = ContentSearch::isSupportedFormat(item.filePath)
            ? ContentSearch::extractText(item.filePath, SNIPPET_CHARS)
            : QString();
        prompt += QStringLiteral("- %1: %2\n").arg(item.fileName,
            snippet.isEmpty() ? QStringLiteral("(no readable text)") : snippet.left(SNIPPET_CHARS));
        ++included;
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
                        const QString file = obj[QStringLiteral("file")].toString();
                        const QString cat  = obj[QStringLiteral("category")].toString();
                        if (file.isEmpty() || cat.isEmpty()) continue;
                        for (auto& item : plan.items) {
                            if (item.fileName == file) {
                                item.category  = cat;
                                item.confident = (cat != QStringLiteral("Другое"));
                                break;
                            }
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

        const QString destDir  = QDir(plan.targetFolder).filePath(item.category);
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
