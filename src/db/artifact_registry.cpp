// ============================================================
// artifact_registry.cpp — что Джарвис создал
// ============================================================

#include "artifact_registry.h"
#include "database_manager.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QImageReader>
#include <QDir>
#include "jarvis_paths.h"
#include <QDebug>

// ── Artifact ────────────────────────────────────────────────────────

bool ArtifactRegistry::Artifact::exists() const
{
    return !path.isEmpty() && QFileInfo::exists(path);
}

qint64 ArtifactRegistry::Artifact::sizeBytes() const
{
    const QFileInfo fi(path);
    return fi.exists() ? fi.size() : 0;
}

bool ArtifactRegistry::Artifact::isImage() const
{
    // Спрашиваем Qt, а не сверяем расширения списком: набор поддержанных
    // форматов зависит от развёрнутых плагинов imageformats, и жёсткий
    // список разошёлся бы с реальностью при первой же смене сборки.
    static const QList<QByteArray> supported = QImageReader::supportedImageFormats();
    const QByteArray suffix = QFileInfo(path).suffix().toLower().toUtf8();
    return !suffix.isEmpty() && supported.contains(suffix);
}

// ── Registry ────────────────────────────────────────────────────────

ArtifactRegistry& ArtifactRegistry::instance()
{
    static ArtifactRegistry inst;
    return inst;
}

ArtifactRegistry::ArtifactRegistry(QObject* parent) : QObject(parent) {}

void ArtifactRegistry::record(const QString& path, const QString& kind,
                              const QString& title, const QString& query)
{
    if (path.trimmed().isEmpty()) return;

    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return;

    // Абсолютный путь: относительный зависит от рабочего каталога
    // процесса, и запись, сделанная сегодня, завтра указывала бы в
    // пустоту.
    const QString abs = QFileInfo(path).absoluteFilePath();

    const QString shownTitle = title.trimmed().isEmpty()
        ? QFileInfo(abs).fileName() : title.trimmed();

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO artifacts (path, kind, title, query, created_at) "
        "VALUES (:p, :k, :t, :q, datetime('now')) "
        "ON CONFLICT(path) DO UPDATE SET "
        "  kind = :k2, title = :t2, created_at = datetime('now')"));
    q.bindValue(QStringLiteral(":p"),  abs);
    q.bindValue(QStringLiteral(":k"),  kind);
    q.bindValue(QStringLiteral(":t"),  shownTitle);
    // QString() — это NULL-строка, и Qt отправляет её как SQL NULL, а не
    // как пустую строку. Колонка NOT NULL, DEFAULT '' при явном указании
    // столбца в INSERT не применяется — запись падала для всего, что
    // регистрируется без исходного запроса (а это почти всё: скриншоты,
    // образцы лица, снятые кадры).
    //
    // Та же ошибка была в FaceRegistry.origin_node и чинилась сегодня же;
    // здесь я повторил её в собственной новой таблице.
    q.bindValue(QStringLiteral(":q"),
                query.isNull() ? QString(QLatin1String("")) : query);
    q.bindValue(QStringLiteral(":k2"), kind);
    q.bindValue(QStringLiteral(":t2"), shownTitle);

    if (!q.exec()) {
        qWarning() << "[ArtifactRegistry] record failed:" << q.lastError().text();
        return;
    }
    emit changed();
}

QList<ArtifactRegistry::Artifact> ArtifactRegistry::recent(int limit) const
{
    QList<Artifact> out;
    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return out;

    QSqlQuery q(db);
    if (limit > 0) {
        q.prepare(QStringLiteral(
            "SELECT id, path, kind, title, query, created_at FROM artifacts "
            "ORDER BY created_at DESC, id DESC LIMIT :lim"));
        q.bindValue(QStringLiteral(":lim"), limit);
    } else {
        q.prepare(QStringLiteral(
            "SELECT id, path, kind, title, query, created_at FROM artifacts "
            "ORDER BY created_at DESC, id DESC"));
    }
    if (!q.exec()) return out;

    while (q.next()) {
        Artifact a;
        a.id        = q.value(0).toLongLong();
        a.path      = q.value(1).toString();
        a.kind      = q.value(2).toString();
        a.title     = q.value(3).toString();
        a.query     = q.value(4).toString();
        a.createdAt = QDateTime::fromString(q.value(5).toString(),
                                            QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        out.append(a);
    }
    return out;
}

bool ArtifactRegistry::forget(qint64 id)
{
    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return false;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM artifacts WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    const bool ok = q.exec();
    if (ok) emit changed();
    return ok;
}

int ArtifactRegistry::pruneMissing()
{
    const auto all = recent(0);
    int removed = 0;
    for (const Artifact& a : all) {
        if (a.exists()) continue;
        auto db = DatabaseManager::instance().connection();
        QSqlQuery q(db);
        q.prepare(QStringLiteral("DELETE FROM artifacts WHERE id = :id"));
        q.bindValue(QStringLiteral(":id"), a.id);
        if (q.exec()) ++removed;
    }
    if (removed > 0) emit changed();
    return removed;
}

int ArtifactRegistry::scanKnownFolders()
{
    // Папки, куда Джарвис сам складывает результаты. Список намеренно
    // короткий и явный: сканировать всю папку данных значило бы затащить
    // в «Файлы» базу, логи и кэш — то есть внутреннюю кухню, к которой
    // пользователь отношения не имеет.
    struct Source { QString rel; const char* kind; QString title; };
    const QList<Source> sources = {
        { QStringLiteral("security/owner_samples"), kPhoto,
          QStringLiteral("Face sample") },
        { QStringLiteral("security/shots"),         kPhoto,
          QStringLiteral("Camera shot") },
        { QStringLiteral("screenshots"),            kScreenshot,
          QStringLiteral("Screen capture") },
        { QStringLiteral("diagrams"),               kDiagram,
          QStringLiteral("Diagram") },
    };

    const QStringList masks = { QStringLiteral("*.png"), QStringLiteral("*.jpg"),
                                QStringLiteral("*.svg") };
    int added = 0;

    for (const Source& s : sources) {
        QDir dir(JarvisPaths::subPath(s.rel));
        if (!dir.exists()) continue;
        const auto files = dir.entryInfoList(masks, QDir::Files, QDir::Time);
        for (const QFileInfo& fi : files) {
            record(fi.absoluteFilePath(), QString::fromLatin1(s.kind),
                   s.title + QStringLiteral(" — ") + fi.fileName());
            ++added;
        }
    }
    return added;
}

int ArtifactRegistry::count() const
{
    auto db = DatabaseManager::instance().connection();
    if (!db.isOpen()) return 0;

    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM artifacts")) && q.next())
        return q.value(0).toInt();
    return 0;
}
