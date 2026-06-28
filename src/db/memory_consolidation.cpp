// ============================================================
// memory_consolidation.cpp — Two-Tier Memory Consolidation
// ============================================================

#include "memory_consolidation.h"
#include "database_manager.h"
#include "jarvis_paths.h"

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QStorageInfo>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDebug>

// ============================================================
//  Singleton
// ============================================================

MemoryConsolidation& MemoryConsolidation::instance()
{
    static MemoryConsolidation inst;
    return inst;
}

MemoryConsolidation::MemoryConsolidation(QObject* parent)
    : QObject(parent)
{
    m_consolidationTimer = new QTimer(this);
    m_consolidationTimer->setSingleShot(false);
    connect(m_consolidationTimer, &QTimer::timeout,
            this, &MemoryConsolidation::onConsolidationTick);

    // Default external root — auto-detect D:/Jarvis or E:/Jarvis
    for (const QString& drive : {QStringLiteral("D:/Jarvis"),
                                  QStringLiteral("E:/Jarvis"),
                                  QStringLiteral("F:/Jarvis")}) {
        QStorageInfo si(drive.left(3));
        if (si.isValid() && si.isReady()) {
            m_externalRoot = drive;
            break;
        }
    }
}

MemoryConsolidation::~MemoryConsolidation()
{
    stopBackgroundConsolidation();
}

// ============================================================
//  Configuration
// ============================================================

void MemoryConsolidation::setExternalRoot(const QString& path)
{
    m_externalRoot = path;
    qDebug() << "[MemoryConsolidation] External root set to:" << path;
}

// ============================================================
//  Drive status — safe probe with no side effects
// ============================================================

ExternalDriveStatus MemoryConsolidation::checkDriveStatus() const
{
    ExternalDriveStatus status;
    status.lastChecked = QDateTime::currentDateTime();

    if (m_externalRoot.isEmpty()) return status;

    // Check if the drive letter is mounted and responsive
    const QString driveLetter = m_externalRoot.left(3); // "D:/"
    QStorageInfo si(driveLetter);

    if (!si.isValid() || !si.isReady()) return status;

    // Drive is mounted — check if our directory exists or can be created
    status.connected  = true;
    status.mountPoint = m_externalRoot;
    status.freeBytes  = si.bytesFree();
    status.totalBytes = si.bytesTotal();

    return status;
}

bool MemoryConsolidation::isExternalAvailable() const
{
    if (m_externalRoot.isEmpty()) return false;

    const QString driveLetter = m_externalRoot.left(3);
    QStorageInfo si(driveLetter);
    return si.isValid() && si.isReady();
}

// ============================================================
//  Tier 1: External Asset Pool — write with graceful fallback
// ============================================================

QString MemoryConsolidation::externalAssetPath(const QString& subDir,
                                                const QString& fileName) const
{
    if (m_externalRoot.isEmpty()) return QString();
    return m_externalRoot + QStringLiteral("/") + subDir
           + QStringLiteral("/") + fileName;
}

bool MemoryConsolidation::storeRawAsset(const QString& subDir,
                                         const QString& fileName,
                                         const QByteArray& data)
{
    if (!isExternalAvailable()) {
        qDebug() << "[MemoryConsolidation] External drive unavailable,"
                 << "skipping raw asset:" << fileName;
        return false;
    }

    const QString dirPath = m_externalRoot + QStringLiteral("/") + subDir;
    QDir().mkpath(dirPath);

    const QString fullPath = dirPath + QStringLiteral("/") + fileName;
    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[MemoryConsolidation] Cannot write:" << fullPath
                   << file.errorString();
        return false;
    }

    file.write(data);
    file.close();
    emit assetStored(fullPath, subDir);
    return true;
}

bool MemoryConsolidation::storeRawAssetFile(const QString& subDir,
                                              const QString& fileName,
                                              const QString& sourcePath)
{
    if (!isExternalAvailable()) {
        qDebug() << "[MemoryConsolidation] External drive unavailable,"
                 << "skipping file copy:" << fileName;
        return false;
    }

    const QString dirPath = m_externalRoot + QStringLiteral("/") + subDir;
    QDir().mkpath(dirPath);

    const QString destPath = dirPath + QStringLiteral("/") + fileName;

    if (QFile::exists(destPath))
        QFile::remove(destPath);

    if (!QFile::copy(sourcePath, destPath)) {
        qWarning() << "[MemoryConsolidation] Copy failed:" << sourcePath
                   << "→" << destPath;
        return false;
    }

    emit assetStored(destPath, subDir);
    return true;
}

// ============================================================
//  Tier 2: Local Semantic Cache — SQLite in JarvisPaths
// ============================================================

void MemoryConsolidation::ensureTable()
{
    if (!DatabaseManager::instance().isOpen()) return;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS memory_consolidation ("
        "  id               INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  source_file      TEXT NOT NULL,"
        "  asset_type       TEXT NOT NULL DEFAULT 'generic',"
        "  summary          TEXT NOT NULL,"
        "  tokens           TEXT DEFAULT '',"
        "  importance        REAL NOT NULL DEFAULT 0.5,"
        "  source_timestamp TEXT,"
        "  consolidated_at  TEXT NOT NULL DEFAULT (datetime('now'))"
        ")"));

    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_consol_type "
        "ON memory_consolidation(asset_type)"));
    q.exec(QStringLiteral(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_consol_source "
        "ON memory_consolidation(source_file)"));

    if (q.lastError().isValid())
        qWarning() << "[MemoryConsolidation] Table error:" << q.lastError().text();
}

qint64 MemoryConsolidation::storeConsolidatedEntry(const ConsolidationEntry& entry)
{
    if (!DatabaseManager::instance().isOpen()) return -1;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return -1;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO memory_consolidation "
        "(source_file, asset_type, summary, tokens, importance, source_timestamp) "
        "VALUES (:sf, :at, :sum, :tok, :imp, :ts)"));
    q.bindValue(QStringLiteral(":sf"),  entry.sourceFile);
    q.bindValue(QStringLiteral(":at"),  entry.assetType);
    q.bindValue(QStringLiteral(":sum"), entry.summary.left(2000));
    q.bindValue(QStringLiteral(":tok"), entry.tokens.join(QStringLiteral(",")));
    q.bindValue(QStringLiteral(":imp"), qBound(0.0, entry.importance, 1.0));
    q.bindValue(QStringLiteral(":ts"),  entry.sourceTimestamp.toString(Qt::ISODate));

    if (!q.exec()) {
        qWarning() << "[MemoryConsolidation] Insert error:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

QList<ConsolidationEntry> MemoryConsolidation::recentConsolidations(int limit) const
{
    QList<ConsolidationEntry> result;
    if (!DatabaseManager::instance().isOpen()) return result;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return result;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, source_file, asset_type, summary, tokens, importance, "
        "       source_timestamp, consolidated_at "
        "FROM memory_consolidation "
        "ORDER BY consolidated_at DESC LIMIT :lim"));
    q.bindValue(QStringLiteral(":lim"), limit);

    if (q.exec()) {
        while (q.next()) {
            ConsolidationEntry e;
            e.id              = q.value(0).toLongLong();
            e.sourceFile      = q.value(1).toString();
            e.assetType       = q.value(2).toString();
            e.summary         = q.value(3).toString();
            e.tokens          = q.value(4).toString().split(
                                    QChar(','), Qt::SkipEmptyParts);
            e.importance      = q.value(5).toDouble();
            e.sourceTimestamp  = QDateTime::fromString(
                                    q.value(6).toString(), Qt::ISODate);
            e.consolidatedAt  = QDateTime::fromString(
                                    q.value(7).toString(), Qt::ISODate);
            result.append(e);
        }
    }
    return result;
}

QList<ConsolidationEntry> MemoryConsolidation::searchConsolidations(
    const QString& keyword, int limit) const
{
    QList<ConsolidationEntry> result;
    if (!DatabaseManager::instance().isOpen()) return result;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return result;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, source_file, asset_type, summary, tokens, importance, "
        "       source_timestamp, consolidated_at "
        "FROM memory_consolidation "
        "WHERE summary LIKE :kw OR tokens LIKE :kw2 "
        "ORDER BY importance DESC, consolidated_at DESC "
        "LIMIT :lim"));
    q.bindValue(QStringLiteral(":kw"),
                QStringLiteral("%") + keyword + QStringLiteral("%"));
    q.bindValue(QStringLiteral(":kw2"),
                QStringLiteral("%") + keyword + QStringLiteral("%"));
    q.bindValue(QStringLiteral(":lim"), limit);

    if (q.exec()) {
        while (q.next()) {
            ConsolidationEntry e;
            e.id              = q.value(0).toLongLong();
            e.sourceFile      = q.value(1).toString();
            e.assetType       = q.value(2).toString();
            e.summary         = q.value(3).toString();
            e.tokens          = q.value(4).toString().split(
                                    QChar(','), Qt::SkipEmptyParts);
            e.importance      = q.value(5).toDouble();
            e.sourceTimestamp  = QDateTime::fromString(
                                    q.value(6).toString(), Qt::ISODate);
            e.consolidatedAt  = QDateTime::fromString(
                                    q.value(7).toString(), Qt::ISODate);
            result.append(e);
        }
    }
    return result;
}

int MemoryConsolidation::consolidatedCount() const
{
    if (!DatabaseManager::instance().isOpen()) return 0;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return 0;

    QSqlQuery q(db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM memory_consolidation"));
    return q.next() ? q.value(0).toInt() : 0;
}

// ============================================================
//  Consolidation tracking — which files have been processed
// ============================================================

bool MemoryConsolidation::isAlreadyConsolidated(const QString& sourcePath) const
{
    if (!DatabaseManager::instance().isOpen()) return false;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return false;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT 1 FROM memory_consolidation WHERE source_file = :sf LIMIT 1"));
    q.bindValue(QStringLiteral(":sf"), sourcePath);
    return q.exec() && q.next();
}

void MemoryConsolidation::markConsolidated(const QString& sourcePath)
{
    // The storeConsolidatedEntry with INSERT OR REPLACE handles this
    Q_UNUSED(sourcePath)
}

// ============================================================
//  Background consolidation loop
// ============================================================

void MemoryConsolidation::startBackgroundConsolidation(int intervalMinutes)
{
    ensureTable();
    m_consolidationTimer->setInterval(intervalMinutes * 60 * 1000);
    m_consolidationTimer->start();
    qDebug() << "[MemoryConsolidation] Background consolidation started,"
             << "interval:" << intervalMinutes << "min";
}

void MemoryConsolidation::stopBackgroundConsolidation()
{
    m_consolidationTimer->stop();
}

void MemoryConsolidation::onConsolidationTick()
{
    if (m_consolidating) return;
    if (!isExternalAvailable()) {
        // Drive disconnected — check if state changed
        if (m_lastDriveState) {
            m_lastDriveState = false;
            emit driveStatusChanged(false);
            qDebug() << "[MemoryConsolidation] External drive disconnected"
                     << "— operating from local cache";
        }
        return;
    }

    // Drive reconnected
    if (!m_lastDriveState) {
        m_lastDriveState = true;
        emit driveStatusChanged(true);
        qDebug() << "[MemoryConsolidation] External drive connected:"
                 << m_externalRoot;
    }

    const int newEntries = consolidateExternalMemory();
    if (newEntries > 0) {
        emit consolidationComplete(newEntries);
        qDebug() << "[MemoryConsolidation] Consolidated"
                 << newEntries << "new entries";
    }
}

// ============================================================
//  Core consolidation pass — scan Tier 1, distill to Tier 2
// ============================================================

int MemoryConsolidation::consolidateExternalMemory()
{
    if (m_consolidating) return 0;
    if (!isExternalAvailable()) return 0;

    m_consolidating = true;
    int processed = 0;
    int total = 0;

    // Scan each Tier 1 subdirectory
    struct DirSpec { const char* dir; const char* type; };
    static const DirSpec kDirs[] = {
        { DIR_BUILD_LOGS,  "build_log" },
        { DIR_SCREENSHOTS, "screenshot_meta" },
        { DIR_SOURCE_POOL, "source_snapshot" },
        { DIR_CRASH_DUMPS, "crash_dump" },
        { DIR_RAW_ASSETS,  "generic" },
    };

    for (const DirSpec& spec : kDirs) {
        const QString scanPath = m_externalRoot + QStringLiteral("/")
                                 + QLatin1String(spec.dir);
        if (!QDir(scanPath).exists()) continue;

        QDirIterator it(scanPath, QDir::Files, QDirIterator::Subdirectories);
        QStringList pending;
        while (it.hasNext()) {
            const QString filePath = it.next();
            if (!isAlreadyConsolidated(filePath))
                pending.append(filePath);
        }

        total += pending.size();
        const QString assetType = QString::fromLatin1(spec.type);
        for (const QString& filePath : pending) {
            ConsolidationEntry entry;

            if (assetType == QStringLiteral("build_log"))
                entry = distillBuildLog(filePath);
            else if (assetType == QStringLiteral("screenshot_meta"))
                entry = distillScreenshotMeta(filePath);
            else if (assetType == QStringLiteral("source_snapshot"))
                entry = distillSourceSnapshot(filePath);
            else
                entry = distillGenericAsset(filePath);

            if (!entry.summary.isEmpty()) {
                entry.sourceFile = filePath;
                entry.assetType  = assetType;
                if (storeConsolidatedEntry(entry) >= 0)
                    ++processed;
            }

            emit consolidationProgress(processed, total);
        }
    }

    m_consolidating = false;
    return processed;
}

// ============================================================
//  Distillation workers — extract semantic tokens from raw files
// ============================================================

ConsolidationEntry MemoryConsolidation::distillBuildLog(const QString& filePath) const
{
    ConsolidationEntry entry;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return entry;

    const QFileInfo fi(filePath);
    entry.sourceTimestamp = fi.lastModified();

    QTextStream in(&file);
    int errorCount = 0, warningCount = 0;
    QString firstError;
    int lineCount = 0;

    while (!in.atEnd() && lineCount < 5000) {
        const QString line = in.readLine();
        ++lineCount;

        const QString lower = line.toLower();
        if (lower.contains(QStringLiteral("error"))) {
            ++errorCount;
            if (firstError.isEmpty())
                firstError = line.trimmed().left(200);
        }
        if (lower.contains(QStringLiteral("warning")))
            ++warningCount;
    }

    entry.summary = QStringLiteral("Build log: %1 errors, %2 warnings")
                        .arg(errorCount).arg(warningCount);
    if (!firstError.isEmpty())
        entry.summary += QStringLiteral(". First error: ") + firstError;

    entry.tokens.append(QStringLiteral("build"));
    if (errorCount > 0) entry.tokens.append(QStringLiteral("error"));
    if (warningCount > 0) entry.tokens.append(QStringLiteral("warning"));
    entry.importance = errorCount > 0 ? 0.8 : 0.3;

    return entry;
}

ConsolidationEntry MemoryConsolidation::distillScreenshotMeta(
    const QString& filePath) const
{
    ConsolidationEntry entry;
    const QFileInfo fi(filePath);
    entry.sourceTimestamp = fi.lastModified();

    // For JSON metadata files alongside screenshots
    if (filePath.endsWith(QStringLiteral(".json"))) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) return entry;

        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) return entry;

        QJsonObject obj = doc.object();
        entry.summary = QStringLiteral("Screenshot: app=%1, title=%2")
            .arg(obj[QStringLiteral("app")].toString(),
                 obj[QStringLiteral("title")].toString().left(100));

        if (obj.contains(QStringLiteral("tokens"))) {
            QJsonArray arr = obj[QStringLiteral("tokens")].toArray();
            for (const auto& v : arr)
                entry.tokens.append(v.toString());
        }
        entry.tokens.append(QStringLiteral("screenshot"));
        entry.importance = 0.4;
    } else {
        // Image file — just record metadata, no pixel analysis
        entry.summary = QStringLiteral("Screenshot captured: %1 (%2 KB)")
                            .arg(fi.fileName())
                            .arg(fi.size() / 1024);
        entry.tokens.append(QStringLiteral("screenshot"));
        entry.importance = 0.2;
    }

    return entry;
}

ConsolidationEntry MemoryConsolidation::distillSourceSnapshot(
    const QString& filePath) const
{
    ConsolidationEntry entry;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return entry;

    const QFileInfo fi(filePath);
    entry.sourceTimestamp = fi.lastModified();

    QTextStream in(&file);
    const QString content = in.read(50000);

    int lineCount = content.count(QChar('\n'));
    int classCount = 0, funcCount = 0;

    // Quick structural scan
    static const QRegularExpression classRx(
        QStringLiteral("\\bclass\\s+\\w+"));
    static const QRegularExpression funcRx(
        QStringLiteral("\\b(?:void|int|bool|QString|float|double)\\s+\\w+\\s*\\("));

    auto classIt = classRx.globalMatch(content);
    while (classIt.hasNext()) { classIt.next(); ++classCount; }

    auto funcIt = funcRx.globalMatch(content);
    while (funcIt.hasNext()) { funcIt.next(); ++funcCount; }

    entry.summary = QStringLiteral("Source snapshot: %1 (%2 lines, %3 classes, %4 functions)")
                        .arg(fi.fileName())
                        .arg(lineCount)
                        .arg(classCount)
                        .arg(funcCount);

    entry.tokens.append(QStringLiteral("source_code"));
    if (fi.suffix() == QStringLiteral("cpp") || fi.suffix() == QStringLiteral("h"))
        entry.tokens.append(QStringLiteral("cpp"));
    if (fi.suffix() == QStringLiteral("cs"))
        entry.tokens.append(QStringLiteral("csharp"));
    entry.importance = 0.5;

    return entry;
}

ConsolidationEntry MemoryConsolidation::distillGenericAsset(
    const QString& filePath) const
{
    ConsolidationEntry entry;
    const QFileInfo fi(filePath);
    entry.sourceTimestamp = fi.lastModified();

    entry.summary = QStringLiteral("Asset: %1 (%2 KB, %3)")
                        .arg(fi.fileName())
                        .arg(fi.size() / 1024)
                        .arg(fi.suffix().toUpper());

    entry.tokens.append(fi.suffix().toLower());
    entry.importance = 0.2;

    return entry;
}

// ============================================================
//  Graceful fallback — build digest from Tier 2 only
// ============================================================

QString MemoryConsolidation::buildMemoryDigest(int maxEntries) const
{
    const auto entries = recentConsolidations(maxEntries);
    if (entries.isEmpty()) return QString();

    QString digest;
    digest.reserve(2000);

    const auto status = checkDriveStatus();
    if (status.connected) {
        digest += QStringLiteral("[External storage: %.1f GB free / %.1f GB total]\n")
                      .arg(status.freeGb(), 0, 'f', 1)
                      .arg(status.totalGb(), 0, 'f', 1);
    } else {
        digest += QStringLiteral("[External storage: DISCONNECTED — operating from local cache]\n");
    }

    digest += QStringLiteral("Recent consolidated memories (%1 total):\n")
                  .arg(consolidatedCount());

    for (const auto& e : entries) {
        digest += QStringLiteral("- [%1] %2")
                      .arg(e.assetType, e.summary.left(120));
        if (!e.tokens.isEmpty())
            digest += QStringLiteral(" {%1}").arg(e.tokens.join(QStringLiteral(", ")));
        digest += QChar('\n');
    }

    return digest;
}
