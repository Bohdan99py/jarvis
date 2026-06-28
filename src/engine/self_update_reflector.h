#pragma once
// ============================================================
// self_update_reflector.h — Auto-Changelog + Dynamic User Manual
//
// Eliminates all hardcoded version notes and help text.
//
// Changelog pipeline:
//   On first run of a new version, detects git diff from
//   previous version tag, auto-generates human-readable
//   changelog, stores in system_changelogs SQLite table.
//
// User Manual:
//   Queries active modules at runtime and auto-compiles
//   instruction text from module self-descriptions.
//   No static strings — the manual updates itself.
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>

// ============================================================
//  ChangelogEntry — one version's changelog
// ============================================================

struct ChangelogEntry
{
    qint64    id = 0;
    QString   version;
    QString   summary;        // human-readable HTML changelog
    QDateTime generatedAt;
    bool      seenByUser = false;
};

// ============================================================
//  ModuleCapability — one active module's self-description
// ============================================================

struct ModuleCapability
{
    QString   moduleId;       // "curiosity_engine", "pdf_distiller", etc.
    QString   displayName;    // "Proactive Curiosity Engine"
    QString   icon;
    QString   description;    // what this module does
    QString   commands;       // available commands/interactions
    bool      active = true;
};

// ============================================================
//  SelfUpdateReflector
// ============================================================

class SelfUpdateReflector : public QObject
{
    Q_OBJECT

public:
    static SelfUpdateReflector& instance();

    SelfUpdateReflector(const SelfUpdateReflector&)            = delete;
    SelfUpdateReflector& operator=(const SelfUpdateReflector&) = delete;

    void ensureTable();

    // --- Changelog ---
    // Check if current version has a changelog; generate if missing
    void ensureChangelog(const QString& currentVersion);

    // Get changelog for a version (or latest)
    ChangelogEntry changelogFor(const QString& version) const;
    QList<ChangelogEntry> allChangelogs(int limit = 10) const;
    bool hasUnseenChangelog() const;
    void markChangelogSeen(const QString& version);

    // Generate changelog from git history
    QString generateChangelogFromGit(const QString& version) const;

    // --- Dynamic User Manual ---
    // Build manual from active module introspection
    QList<ModuleCapability> activeModules() const;
    QString buildDynamicManualHtml(bool english) const;
    QString buildChangelogHtml(bool english) const;

signals:
    void changelogGenerated(const QString& version);

private:
    explicit SelfUpdateReflector(QObject* parent = nullptr);
    ~SelfUpdateReflector() override;

    void storeChangelog(const QString& version, const QString& summary);
    QString detectPreviousVersion() const;
    QStringList gitLogSince(const QString& fromTag) const;
    QString formatCommitsAsChangelog(const QStringList& commits,
                                      const QString& version) const;
};
