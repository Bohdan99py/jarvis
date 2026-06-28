#pragma once
// ============================================================
// memory_consolidation.h — Two-Tier Memory Storage Abstraction
//
// Tier 1 (External Asset Pool):
//   Raw screenshots, compilation logs, full source snapshots.
//   Lives on external drive (D:/Jarvis/ or user-configured).
//   May be disconnected at any time — treated as volatile.
//
// Tier 2 (Local Semantic Cache):
//   Distilled metadata, text summaries, learned weights,
//   behavior patterns, project insights. Lives on system SSD
//   via JarvisPaths/DatabaseManager. Always available.
//
// Consolidation pipeline:
//   When external drive is CONNECTED and system is IDLE,
//   a background pass reads new Tier 1 assets, extracts
//   semantic tokens, and writes distilled summaries to Tier 2.
//   If the drive disconnects, JARVIS operates fully from Tier 2
//   with zero data loss — only raw assets become inaccessible.
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QTimer>

class DatabaseManager;

// ============================================================
//  ExternalDriveStatus — connection state of the asset pool
// ============================================================

struct ExternalDriveStatus
{
    bool     connected    = false;
    QString  mountPoint;           // "D:/Jarvis"
    qint64   freeBytes    = 0;
    qint64   totalBytes   = 0;
    QDateTime lastChecked;

    double freeGb() const { return freeBytes / (1024.0 * 1024.0 * 1024.0); }
    double totalGb() const { return totalBytes / (1024.0 * 1024.0 * 1024.0); }
};

// ============================================================
//  ConsolidationEntry — one distilled insight from Tier 1
// ============================================================

struct ConsolidationEntry
{
    qint64    id = 0;
    QString   sourceFile;     // original Tier 1 path (for re-access if drive connected)
    QString   assetType;      // "screenshot_meta", "build_log", "source_snapshot", "crash_dump"
    QString   summary;        // distilled text summary
    QStringList tokens;       // semantic keywords
    double    importance = 0.5;
    QDateTime sourceTimestamp;
    QDateTime consolidatedAt;
};

// ============================================================
//  MemoryConsolidation — the pipeline
// ============================================================

class MemoryConsolidation : public QObject
{
    Q_OBJECT

public:
    static MemoryConsolidation& instance();

    MemoryConsolidation(const MemoryConsolidation&)            = delete;
    MemoryConsolidation& operator=(const MemoryConsolidation&) = delete;

    // --- Configuration ---
    void setExternalRoot(const QString& path);
    QString externalRoot() const { return m_externalRoot; }

    // --- Drive status ---
    ExternalDriveStatus checkDriveStatus() const;
    bool isExternalAvailable() const;

    // --- Tier 1: External Asset Pool ---
    // Write raw assets to external drive (graceful fallback on disconnect)
    bool storeRawAsset(const QString& subDir,
                       const QString& fileName,
                       const QByteArray& data);
    bool storeRawAssetFile(const QString& subDir,
                           const QString& fileName,
                           const QString& sourcePath);
    QString externalAssetPath(const QString& subDir,
                              const QString& fileName) const;

    // --- Tier 2: Local Semantic Cache ---
    void ensureTable();
    qint64 storeConsolidatedEntry(const ConsolidationEntry& entry);
    QList<ConsolidationEntry> recentConsolidations(int limit = 20) const;
    QList<ConsolidationEntry> searchConsolidations(const QString& keyword,
                                                    int limit = 10) const;
    int consolidatedCount() const;

    // --- Consolidation pipeline ---
    void startBackgroundConsolidation(int intervalMinutes = 15);
    void stopBackgroundConsolidation();
    bool isConsolidating() const { return m_consolidating; }

    // Single-pass: scan Tier 1 for new assets, distill to Tier 2
    int consolidateExternalMemory();

    // --- Graceful fallback ---
    // Returns local semantic summary even when drive is disconnected
    QString buildMemoryDigest(int maxEntries = 10) const;

signals:
    void driveStatusChanged(bool connected);
    void consolidationProgress(int processed, int total);
    void consolidationComplete(int newEntries);
    void assetStored(const QString& path, const QString& type);

private:
    explicit MemoryConsolidation(QObject* parent = nullptr);
    ~MemoryConsolidation() override;

    void onConsolidationTick();

    // Distillation workers
    ConsolidationEntry distillBuildLog(const QString& filePath) const;
    ConsolidationEntry distillScreenshotMeta(const QString& filePath) const;
    ConsolidationEntry distillSourceSnapshot(const QString& filePath) const;
    ConsolidationEntry distillGenericAsset(const QString& filePath) const;

    // Track which files have already been consolidated
    bool isAlreadyConsolidated(const QString& sourcePath) const;
    void markConsolidated(const QString& sourcePath);

    QString  m_externalRoot;       // e.g. "D:/Jarvis"
    QTimer*  m_consolidationTimer = nullptr;
    bool     m_consolidating      = false;
    bool     m_lastDriveState     = false;

    // Subdirectories on external drive
    static constexpr const char* DIR_SCREENSHOTS  = "screenshots";
    static constexpr const char* DIR_BUILD_LOGS   = "build_logs";
    static constexpr const char* DIR_SOURCE_POOL  = "source_pool";
    static constexpr const char* DIR_CRASH_DUMPS  = "crash_dumps";
    static constexpr const char* DIR_RAW_ASSETS   = "raw_assets";
};
