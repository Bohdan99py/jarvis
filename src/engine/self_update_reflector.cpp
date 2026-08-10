// ============================================================
// self_update_reflector.cpp — Auto-Changelog + Dynamic Manual
// ============================================================

#include "self_update_reflector.h"
#include "database_manager.h"
#include "memory_consolidation.h"
#include "curiosity_engine.h"
#include "self_journal.h"
#include "pdf_distiller.h"
#include "user_profile_extended.h"
#include "system_manifest.h"

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QProcess>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QDebug>

// ============================================================
//  Singleton
// ============================================================

SelfUpdateReflector& SelfUpdateReflector::instance()
{
    static SelfUpdateReflector inst;
    return inst;
}

SelfUpdateReflector::SelfUpdateReflector(QObject* parent)
    : QObject(parent)
{
}

SelfUpdateReflector::~SelfUpdateReflector() = default;

// ============================================================
//  Database table
// ============================================================

void SelfUpdateReflector::ensureTable()
{
    if (!DatabaseManager::instance().isOpen()) return;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS system_changelogs ("
        "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  version      TEXT NOT NULL UNIQUE,"
        "  summary      TEXT NOT NULL,"
        "  generated_at TEXT NOT NULL DEFAULT (datetime('now')),"
        "  seen         INTEGER NOT NULL DEFAULT 0"
        ")"));
}

// ============================================================
//  Changelog — ensure current version has an entry
// ============================================================

void SelfUpdateReflector::ensureChangelog(const QString& currentVersion)
{
    ensureTable();

    auto existing = changelogFor(currentVersion);
    if (!existing.summary.isEmpty()) return;

    const QString changelog = generateChangelogFromGit(currentVersion);
    if (!changelog.isEmpty()) {
        storeChangelog(currentVersion, changelog);
        emit changelogGenerated(currentVersion);
    }
}

void SelfUpdateReflector::storeChangelog(const QString& version,
                                          const QString& summary)
{
    if (!DatabaseManager::instance().isOpen()) return;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO system_changelogs (version, summary) "
        "VALUES (:ver, :sum)"));
    q.bindValue(QStringLiteral(":ver"), version);
    q.bindValue(QStringLiteral(":sum"), summary);
    q.exec();
}

ChangelogEntry SelfUpdateReflector::changelogFor(const QString& version) const
{
    ChangelogEntry entry;
    if (!DatabaseManager::instance().isOpen()) return entry;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, version, summary, generated_at, seen "
        "FROM system_changelogs WHERE version = :ver"));
    q.bindValue(QStringLiteral(":ver"), version);

    if (q.exec() && q.next()) {
        entry.id          = q.value(0).toLongLong();
        entry.version     = q.value(1).toString();
        entry.summary     = q.value(2).toString();
        entry.generatedAt = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
        entry.seenByUser  = q.value(4).toBool();
    }
    return entry;
}

QList<ChangelogEntry> SelfUpdateReflector::allChangelogs(int limit) const
{
    QList<ChangelogEntry> result;
    if (!DatabaseManager::instance().isOpen()) return result;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, version, summary, generated_at, seen "
        "FROM system_changelogs ORDER BY generated_at DESC LIMIT :lim"));
    q.bindValue(QStringLiteral(":lim"), limit);

    if (q.exec()) {
        while (q.next()) {
            ChangelogEntry e;
            e.id          = q.value(0).toLongLong();
            e.version     = q.value(1).toString();
            e.summary     = q.value(2).toString();
            e.generatedAt = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
            e.seenByUser  = q.value(4).toBool();
            result.append(e);
        }
    }
    return result;
}

bool SelfUpdateReflector::hasUnseenChangelog() const
{
    if (!DatabaseManager::instance().isOpen()) return false;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    QSqlQuery q(db);
    q.exec(QStringLiteral("SELECT 1 FROM system_changelogs WHERE seen = 0 LIMIT 1"));
    return q.next();
}

void SelfUpdateReflector::markChangelogSeen(const QString& version)
{
    if (!DatabaseManager::instance().isOpen()) return;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE system_changelogs SET seen = 1 WHERE version = :ver"));
    q.bindValue(QStringLiteral(":ver"), version);
    q.exec();
}

// ============================================================
//  Git-based changelog generation
// ============================================================

QString SelfUpdateReflector::detectPreviousVersion() const
{
    QSettings s(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
    return s.value(QStringLiteral("update/previous_version")).toString();
}

QStringList SelfUpdateReflector::gitLogSince(const QString& fromTag) const
{
    QStringList commits;
    const QString appDir = QCoreApplication::applicationDirPath();

    QProcess git;
    git.setWorkingDirectory(appDir);
    git.setProgram(QStringLiteral("git"));

    if (!fromTag.isEmpty()) {
        git.setArguments({
            QStringLiteral("log"),
            QStringLiteral("--oneline"),
            QStringLiteral("--no-merges"),
            QStringLiteral("-30"),
            fromTag + QStringLiteral("..HEAD")
        });
    } else {
        git.setArguments({
            QStringLiteral("log"),
            QStringLiteral("--oneline"),
            QStringLiteral("--no-merges"),
            QStringLiteral("-20")
        });
    }

    git.start();
    if (!git.waitForFinished(10000)) return commits;
    if (git.exitCode() != 0) return commits;

    const QString output = QString::fromUtf8(git.readAllStandardOutput());
    const QStringList lines = output.split(QChar('\n'), Qt::SkipEmptyParts);

    for (const QString& line : lines) {
        const int space = line.indexOf(QChar(' '));
        if (space > 0)
            commits.append(line.mid(space + 1).trimmed());
    }
    return commits;
}

QString SelfUpdateReflector::formatCommitsAsChangelog(
    const QStringList& commits, const QString& version) const
{
    if (commits.isEmpty()) return QString();

    // Categorize commits by prefix keywords
    QStringList features, fixes, refactors, other;

    for (const QString& msg : commits) {
        const QString lower = msg.toLower();

        if (lower.startsWith(QStringLiteral("add"))
            || lower.startsWith(QStringLiteral("implement"))
            || lower.startsWith(QStringLiteral("new"))
            || lower.contains(QStringLiteral("feature"))
            || lower.contains(QStringLiteral("engine")))
            features.append(msg);
        else if (lower.startsWith(QStringLiteral("fix"))
                 || lower.contains(QStringLiteral("bug"))
                 || lower.contains(QStringLiteral("crash"))
                 || lower.contains(QStringLiteral("error")))
            fixes.append(msg);
        else if (lower.startsWith(QStringLiteral("refactor"))
                 || lower.contains(QStringLiteral("cleanup"))
                 || lower.contains(QStringLiteral("modular"))
                 || lower.contains(QStringLiteral("cmake")))
            refactors.append(msg);
        else
            other.append(msg);
    }

    QString html;
    html += QStringLiteral("<h3 style='color:#66FCF1;'>J.A.R.V.I.S. v%1</h3>").arg(version);

    auto renderSection = [&html](const QString& title, const QString& icon,
                                  const QStringList& items) {
        if (items.isEmpty()) return;
        html += QStringLiteral("<h4 style='color:#45A29E;'>%1 %2</h4><ul>")
                    .arg(icon, title);
        for (const QString& item : items)
            html += QStringLiteral("<li>%1</li>").arg(item.toHtmlEscaped());
        html += QStringLiteral("</ul>");
    };

    renderSection(QStringLiteral("New Features"), QStringLiteral("✨"), features);
    renderSection(QStringLiteral("Bug Fixes"),    QStringLiteral("🐛"), fixes);
    renderSection(QStringLiteral("Refactoring"),  QStringLiteral("🔧"), refactors);
    renderSection(QStringLiteral("Other"),        QStringLiteral("📝"), other);

    return html;
}

QString SelfUpdateReflector::generateChangelogFromGit(const QString& version) const
{
    const QString prevVersion = detectPreviousVersion();
    const QString fromTag = prevVersion.isEmpty() ? QString()
                                                  : QStringLiteral("v") + prevVersion;
    const QStringList commits = gitLogSince(fromTag);
    return formatCommitsAsChangelog(commits, version);
}

// ============================================================
//  Dynamic User Manual — runtime module introspection
// ============================================================

QList<ModuleCapability> SelfUpdateReflector::activeModules() const
{
    // This used to be its own hand-written list of 12 modules, parallel to
    // SystemManifest's list of the same system — with different ids for the
    // same things ("voice_input" vs "voice_pipeline", "telegram" vs
    // "telegram_gateway"), a hardcoded active=true on every entry, and a
    // comment claiming runtime introspection. Two hand-maintained
    // descriptions of one machine drift apart by default; the manual is now
    // a rendering of the single registry, and `active` is whatever that
    // registry probed or was told.
    QList<ModuleCapability> modules;
    const auto caps = SystemManifest::activeCapabilities();
    modules.reserve(caps.size());

    for (const SystemManifest::Capability& c : caps) {
        // Live state belongs with the description in the manual — "Two-Tier
        // Memory" reads very differently with "external drive offline" after
        // it, and that is exactly the sort of thing a user opens the manual
        // to find out.
        QString description = c.description;
        if (!c.state.isEmpty())
            description += QStringLiteral(" — currently: ") + c.state;

        modules.append({
            c.id,
            c.label,
            c.icon.isEmpty() ? QStringLiteral("•") : c.icon,
            description,
            c.commands,
            c.active
        });
    }
    return modules;
}

QString SelfUpdateReflector::buildDynamicManualHtml(bool english) const
{
    const auto modules = activeModules();
    Q_UNUSED(english) // All module descriptions are already in English

    QString html;
    html += QStringLiteral(
        "<h3 style='color:#66FCF1; letter-spacing:2px;'>"
        "J.A.R.V.I.S. System Manual</h3>"
        "<p style='color:#888; font-size:11px;'>"
        "Auto-generated from %1 active modules — %2</p><hr>")
        .arg(modules.size())
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("dd MMM yyyy HH:mm")));

    for (const ModuleCapability& m : modules) {
        const QString statusDot = m.active
            ? QStringLiteral("<span style='color:#66FCF1;'>●</span>")
            : QStringLiteral("<span style='color:#555;'>○</span>");

        html += QStringLiteral(
            "<div style='margin:10px 0; padding:10px 14px; "
            "background:rgba(11,12,16,0.7); border:1px solid rgba(102,252,241,0.1); "
            "border-radius:8px;'>"
            "<b style='color:#66FCF1; font-size:13px;'>%1 %2 %3</b>"
            "<p style='color:#C5C6C7; font-size:12px; margin:6px 0;'>%4</p>"
            "<p style='color:#45A29E; font-size:11px; margin:2px 0;'>"
            "<b>Usage:</b> %5</p>"
            "</div>")
            .arg(statusDot, m.icon, m.displayName,
                 m.description, m.commands);
    }

    return html;
}

QString SelfUpdateReflector::buildChangelogHtml(bool english) const
{
    Q_UNUSED(english)

    const auto entries = allChangelogs(5);
    if (entries.isEmpty()) {
        return QStringLiteral(
            "<h3 style='color:#66FCF1;'>What's New</h3>"
            "<p style='color:#888;'>No changelog entries yet. "
            "Changelogs are auto-generated from git history on each new version.</p>");
    }

    QString html;
    for (const ChangelogEntry& e : entries) {
        html += e.summary;
        html += QStringLiteral(
            "<p style='color:#555; font-size:10px;'>Generated: %1</p><hr>")
            .arg(e.generatedAt.toString(QStringLiteral("dd MMM yyyy HH:mm")));
    }
    return html;
}
