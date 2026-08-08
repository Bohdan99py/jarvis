#pragma once
// ============================================================
// file_organizer.h — Content-aware file organization
//
// Scans a folder, classifies each file (extension first, then file
// content via ContentSearch + one batched LLM call for anything
// ambiguous), and builds an OrganizePlan. Never touches the
// filesystem by itself — building a plan is read-only. Applying a
// plan goes through SystemController's allow-listed move primitives
// and is journaled so it can be undone.
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QDateTime>
#include <functional>

class ClaudeApi;
class SystemController;

struct OrganizeItem {
    QString filePath;
    QString fileName;
    QString category;    // "Документы", "Изображения", ... — target subfolder name
    QString subcategory; // e.g. "Учёба" — nested under category/ when non-empty
    bool    confident = true; // false → left untouched ("Нераспознано")
    qint64  sizeBytes = 0;
};

// A user-editable classification rule: which extensions map to which
// top-level category, and (optionally) which content-derived subcategory
// labels the LLM may choose among for files matching this rule.
struct OrganizeRule {
    QString     category;
    QStringList extensions;      // lowercase, no leading dot
    bool        contextAware = false;
    QStringList subcategories;   // valid labels when contextAware
};

struct OrganizePlan {
    QString targetFolder;
    QVector<OrganizeItem> items;
    QDateTime builtAt;

    // Grouped view for UI/Telegram summaries: category -> item count
    QVector<QPair<QString, int>> categoryCounts() const;
};

class FileOrganizer : public QObject
{
    Q_OBJECT
public:
    static FileOrganizer& instance();

    void setLlmApi(ClaudeApi* api) { m_api = api; }

    // Scans targetFolder (top-level files only — does not recurse into
    // subfolders, so it won't re-organize its own output). Read-only.
    void buildPlan(const QString& targetFolder,
                   std::function<void(const OrganizePlan&)> callback);

    // User-editable classification rules — lazily loaded from
    // DatabaseManager's settings table (JSON blob), seeded with
    // defaultRules() the first time they're read.
    QVector<OrganizeRule> rules() const;
    void setRules(const QVector<OrganizeRule>& rules);
    void resetRulesToDefault();
    static QVector<OrganizeRule> defaultRules();

    // Executes an already-approved plan via sys's safe primitives.
    // Only items with confident==true are moved. Returns the batch id
    // (empty on total failure) — pass it to undoBatch() to revert.
    QString applyPlan(const OrganizePlan& plan, SystemController* sys);

    // Reverts the most recent batch (or a specific one) by moving
    // files back to their original location.
    bool undoLastBatch(SystemController* sys);
    bool undoBatch(const QString& batchId, SystemController* sys);

    void ensureTable();

signals:
    void planReady(const OrganizePlan& plan);
    void batchApplied(const QString& batchId, int movedCount);
    void batchUndone(const QString& batchId, int restoredCount);

private:
    explicit FileOrganizer(QObject* parent = nullptr);

    void loadRulesIfNeeded() const;
    QString classifyByExtension(const QString& fileName) const;
    const OrganizeRule* ruleForCategory(const QString& category) const;

    // Single batched LLM call covering both concerns: items with no rule
    // match need a top-level category; items matched to a contextAware
    // rule need a subcategory. catIndices/subIndices may overlap in
    // neither direction (an item is in exactly one of the two lists).
    void classifyWithLlm(const QVector<int>& catIndices, const QVector<int>& subIndices,
                         OrganizePlan& plan, std::function<void()> onDone);

    ClaudeApi* m_api = nullptr;
    mutable QVector<OrganizeRule> m_rulesCache;
    mutable bool m_rulesLoaded = false;
};
