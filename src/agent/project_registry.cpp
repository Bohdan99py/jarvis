// -------------------------------------------------------
// project_registry.cpp — см. project_registry.h
// -------------------------------------------------------

#include "project_registry.h"

#include "applauncher.h"
#include "edit_journal.h"
#include "event_feed.h"
#include "jarvis_paths.h"
#include "tool_registry.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTimer>
#include <QDebug>

namespace {

// Команда проекта выполняется целиком, но её вывод уходит в контекст
// модели — а вывод сборки это десятки тысяч строк.
constexpr int kMaxOutputChars = 6000;

QString runShell(const QString& command, const QString& workingDir,
                 int timeoutSec, bool* okOut)
{
    QProcess proc;
    if (!workingDir.isEmpty() && QFileInfo(workingDir).isDir())
        proc.setWorkingDirectory(workingDir);
    proc.setProcessChannelMode(QProcess::MergedChannels);

    proc.start(QStringLiteral("cmd.exe"), { QStringLiteral("/c"), command });
    if (!proc.waitForStarted(5000)) {
        if (okOut) *okOut = false;
        return QStringLiteral("Failed to start the shell.");
    }

    // Не waitForFinished(): он заморозил бы окно вместе с диалогом
    // подтверждения следующего шага. Локальный цикл держит UI живым.
    QEventLoop loop;
    bool timedOut = false;
    QTimer killTimer;
    killTimer.setSingleShot(true);
    QObject::connect(&killTimer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        proc.kill();
        loop.quit();
    });
    QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &loop, &QEventLoop::quit);
    killTimer.start(qBound(1, timeoutSec, 1800) * 1000);
    loop.exec();
    killTimer.stop();
    proc.waitForFinished(2000);

    QString output = QString::fromLocal8Bit(proc.readAll()).trimmed();
    if (output.length() > kMaxOutputChars) {
        // Хвост, а не начало: ошибки компилятора и итог сборки — в конце.
        output = QStringLiteral("... [начало вывода обрезано]\n")
                 + output.right(kMaxOutputChars);
    }

    const int code = proc.exitCode();
    if (okOut)
        *okOut = !timedOut && code == 0;

    if (timedOut)
        return QStringLiteral("[прервано по таймауту %1 с]\n").arg(timeoutSec) + output;
    return QStringLiteral("[код возврата %1]\n").arg(code) + output;
}

bool hasFile(const QDir& dir, const QString& name)
{
    return QFileInfo::exists(dir.absoluteFilePath(name));
}

bool hasAnyWithSuffix(const QDir& dir, const QString& suffix)
{
    return !dir.entryList({ QStringLiteral("*.") + suffix }, QDir::Files).isEmpty();
}

} // namespace

// ============================================================
//  Проект
// ============================================================

bool ProjectEntry::exists() const
{
    return !path.isEmpty() && QFileInfo(path).isDir();
}

QString ProjectEntry::human() const
{
    QString line = name;
    if (!kind.isEmpty())
        line += QStringLiteral("  [") + kind + QChar(']');
    line += QStringLiteral("\n    ") + QDir::toNativeSeparators(path);
    if (!exists())
        line += QStringLiteral("   ← папки нет");
    if (!ide.isEmpty())
        line += QStringLiteral("\n    IDE: ") + ide;
    if (!buildCommand.isEmpty())
        line += QStringLiteral("\n    сборка: ") + buildCommand;
    if (!runCommand.isEmpty())
        line += QStringLiteral("\n    запуск: ") + runCommand;
    if (lastOpened.isValid())
        line += QStringLiteral("\n    открывали: ")
                + lastOpened.toString(QStringLiteral("dd.MM HH:mm"));
    return line;
}

ProjectEntry ProjectEntry::fromJson(const QJsonObject& obj)
{
    ProjectEntry p;
    p.name         = obj.value(QStringLiteral("name")).toString();
    p.path         = obj.value(QStringLiteral("path")).toString();
    p.kind         = obj.value(QStringLiteral("kind")).toString();
    p.ide          = obj.value(QStringLiteral("ide")).toString();
    p.buildCommand = obj.value(QStringLiteral("build")).toString();
    p.runCommand   = obj.value(QStringLiteral("run")).toString();
    p.docs         = obj.value(QStringLiteral("docs")).toString();
    p.openCount    = obj.value(QStringLiteral("open_count")).toInt(0);

    const QString opened = obj.value(QStringLiteral("last_opened")).toString();
    if (!opened.isEmpty())
        p.lastOpened = QDateTime::fromString(opened, Qt::ISODate);

    for (const QJsonValue& v : obj.value(QStringLiteral("tags")).toArray())
        p.tags << v.toString();
    return p;
}

QJsonObject ProjectEntry::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("path")] = path;
    if (!kind.isEmpty())         obj[QStringLiteral("kind")]  = kind;
    if (!ide.isEmpty())          obj[QStringLiteral("ide")]   = ide;
    if (!buildCommand.isEmpty()) obj[QStringLiteral("build")] = buildCommand;
    if (!runCommand.isEmpty())   obj[QStringLiteral("run")]   = runCommand;
    if (!docs.isEmpty())         obj[QStringLiteral("docs")]  = docs;
    if (openCount > 0)           obj[QStringLiteral("open_count")] = openCount;
    if (lastOpened.isValid())
        obj[QStringLiteral("last_opened")] = lastOpened.toString(Qt::ISODate);

    if (!tags.isEmpty()) {
        QJsonArray arr;
        for (const QString& t : tags)
            arr.append(t);
        obj[QStringLiteral("tags")] = arr;
    }
    return obj;
}

// ============================================================
//  Определение типа проекта
// ============================================================

ProjectEntry ProjectRegistry::sniff(const QString& path)
{
    ProjectEntry p;
    p.path = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    p.name = QDir(p.path).dirName();

    const QDir dir(p.path);
    if (!dir.exists())
        return p;

    // Порядок проверок = порядок специфичности: у проекта Unreal тоже
    // есть .sln, а у PlatformIO — CMakeLists.txt для IDE.
    if (hasAnyWithSuffix(dir, QStringLiteral("uproject"))) {
        p.kind = QStringLiteral("Unreal Engine");
        p.ide  = QStringLiteral("rider");
    } else if (hasFile(dir, QStringLiteral("platformio.ini"))) {
        p.kind         = QStringLiteral("PlatformIO / ESP32");
        p.ide          = QStringLiteral("code");
        p.buildCommand = QStringLiteral("pio run");
        p.runCommand   = QStringLiteral("pio run --target upload");
    } else if (hasFile(dir, QStringLiteral("CMakeLists.txt"))) {
        p.kind = QStringLiteral("CMake C++");
        p.ide  = QStringLiteral("clion");
        // Команду сборки не выдумываем: генератор и папка сборки у всех
        // свои, а неверная команда хуже пустой — её выполнят.
        if (QFileInfo::exists(dir.absoluteFilePath(QStringLiteral("build/build.ninja"))))
            p.buildCommand = QStringLiteral("ninja -C build");
    } else if (hasFile(dir, QStringLiteral("package.json"))) {
        p.kind         = QStringLiteral("Node.js");
        p.ide          = QStringLiteral("code");
        p.buildCommand = QStringLiteral("npm run build");
        p.runCommand   = QStringLiteral("npm start");
    } else if (hasFile(dir, QStringLiteral("pyproject.toml"))
               || hasFile(dir, QStringLiteral("requirements.txt"))) {
        p.kind = QStringLiteral("Python");
        p.ide  = QStringLiteral("code");
    } else if (hasAnyWithSuffix(dir, QStringLiteral("sln"))
               || hasAnyWithSuffix(dir, QStringLiteral("csproj"))) {
        p.kind = QStringLiteral(".NET");
    } else if (hasAnyWithSuffix(dir, QStringLiteral("pro"))) {
        p.kind = QStringLiteral("qmake / Qt");
        p.ide  = QStringLiteral("clion");
    }

    if (QFileInfo::exists(dir.absoluteFilePath(QStringLiteral(".git"))))
        p.tags << QStringLiteral("git");

    return p;
}

// ============================================================
//  ProjectRegistry
// ============================================================

ProjectRegistry::ProjectRegistry(QObject* parent)
    : QObject(parent)
{
    load();
}

QString ProjectRegistry::storagePath() const
{
    return JarvisPaths::subPath(QStringLiteral("agent/projects.json"));
}

void ProjectRegistry::load()
{
    m_projects.clear();

    QFile f(storagePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    const QJsonObject root = doc.object();
    m_active = root.value(QStringLiteral("active")).toString();

    const QJsonArray arr = doc.isArray()
        ? doc.array()
        : root.value(QStringLiteral("projects")).toArray();

    for (const QJsonValue& v : arr) {
        const ProjectEntry p = ProjectEntry::fromJson(v.toObject());
        if (p.isValid())
            m_projects.append(p);
        else
            qWarning() << "[Projects] skipped a malformed entry";
    }

    emit listChanged();
}

bool ProjectRegistry::save() const
{
    QJsonArray arr;
    for (const ProjectEntry& p : m_projects)
        arr.append(p.toJson());

    QJsonObject root;
    root[QStringLiteral("projects")] = arr;
    root[QStringLiteral("active")]   = m_active;

    QFile f(storagePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[Projects] cannot write" << storagePath() << f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

const ProjectEntry* ProjectRegistry::find(const QString& name) const
{
    const QString needle = name.trimmed();
    if (needle.isEmpty())
        return nullptr;

    for (const ProjectEntry& p : m_projects) {
        if (p.name.compare(needle, Qt::CaseInsensitive) == 0)
            return &p;
    }
    // Частичное совпадение — «запусти ESP32» про проект «ESP32 Car».
    for (const ProjectEntry& p : m_projects) {
        if (p.name.contains(needle, Qt::CaseInsensitive))
            return &p;
    }
    return nullptr;
}

const ProjectEntry* ProjectRegistry::forPath(const QString& path) const
{
    const QString needle = QDir::fromNativeSeparators(
        QFileInfo(path).absoluteFilePath());
    if (needle.isEmpty())
        return nullptr;

    // Самый длинный подходящий корень: вложенный проект точнее внешнего.
    const ProjectEntry* best = nullptr;
    for (const ProjectEntry& p : m_projects) {
        const QString root = QDir::fromNativeSeparators(p.path);
        if (root.isEmpty())
            continue;
        if (needle == root || needle.startsWith(root + QChar('/'))) {
            if (!best || root.length() > best->path.length())
                best = &p;
        }
    }
    return best;
}

QStringList ProjectRegistry::names() const
{
    QStringList out;
    out.reserve(m_projects.size());
    for (const ProjectEntry& p : m_projects)
        out << p.name;
    return out;
}

bool ProjectRegistry::addOrReplace(const ProjectEntry& project)
{
    if (!project.isValid())
        return false;

    bool replaced = false;
    for (int i = 0; i < m_projects.size(); ++i) {
        if (m_projects[i].name.compare(project.name, Qt::CaseInsensitive) != 0)
            continue;
        // Статистику открытий правка описания не обнуляет.
        ProjectEntry updated = project;
        updated.lastOpened = m_projects[i].lastOpened;
        updated.openCount  = m_projects[i].openCount;
        m_projects[i] = updated;
        replaced = true;
        break;
    }
    if (!replaced)
        m_projects.append(project);

    const bool ok = save();
    emit listChanged();
    return ok;
}

bool ProjectRegistry::remove(const QString& name)
{
    for (int i = 0; i < m_projects.size(); ++i) {
        if (m_projects[i].name.compare(name.trimmed(), Qt::CaseInsensitive) != 0)
            continue;
        if (m_active.compare(m_projects[i].name, Qt::CaseInsensitive) == 0)
            m_active.clear();
        m_projects.remove(i);
        const bool ok = save();
        emit listChanged();
        return ok;
    }
    return false;
}

QString ProjectRegistry::open(const QString& name, bool launchIde, bool* okOut)
{
    if (okOut)
        *okOut = false;

    const ProjectEntry* found = find(name);
    if (!found) {
        return QStringLiteral("No project named '%1'. Known: %2")
            .arg(name, names().isEmpty() ? QStringLiteral("(none)")
                                         : names().join(QStringLiteral(", ")));
    }

    ProjectEntry project = *found;   // копия: активатор может тронуть список
    if (!project.exists())
        return QStringLiteral("Project '%1' points at a folder that no longer exists: %2")
            .arg(project.name, project.path);

    m_active = project.name;

    QStringList report;
    report << QStringLiteral("Открыт проект \"%1\" (%2)")
                  .arg(project.name, project.kind.isEmpty() ? QStringLiteral("тип не определён")
                                                            : project.kind);
    report << QStringLiteral("  ") + QDir::toNativeSeparators(project.path);

    // Индексатор, профиль, контекст — всё это подхватит активатор.
    if (m_activator)
        m_activator(project);

    if (launchIde && !project.ide.isEmpty()) {
        AppLauncher launcher;
        const AppLauncher::LaunchResult res =
            launcher.launchProject(project.path, project.ide);
        report << (res.success
                       ? QStringLiteral("  IDE: %1 запущена").arg(project.ide)
                       : QStringLiteral("  IDE: не удалось запустить %1 — %2")
                             .arg(project.ide, res.errorMessage));
    }

    // Статистика пишется в сам список, а не в копию.
    for (ProjectEntry& stored : m_projects) {
        if (stored.name.compare(project.name, Qt::CaseInsensitive) != 0)
            continue;
        stored.lastOpened = QDateTime::currentDateTime();
        stored.openCount++;
        break;
    }
    save();

    EventFeed::instance().post(QStringLiteral("project"), EventLevel::Info,
                               QStringLiteral("Проект: %1").arg(project.name),
                               QDir::toNativeSeparators(project.path),
                               QStringLiteral("project/") + project.name);

    emit projectOpened(project.name, project.path);
    emit listChanged();

    if (okOut)
        *okOut = true;
    return report.join(QChar('\n'));
}

QString ProjectRegistry::runCommandFor(const QString& name, bool build, bool* okOut)
{
    if (okOut)
        *okOut = false;

    const QString target = name.trimmed().isEmpty() ? m_active : name;
    const ProjectEntry* found = find(target);
    if (!found) {
        return target.isEmpty()
            ? QStringLiteral("No active project. Open one first or pass a name.")
            : QStringLiteral("No project named '%1'.").arg(target);
    }

    const ProjectEntry project = *found;
    const QString command = build ? project.buildCommand : project.runCommand;
    if (command.isEmpty()) {
        return QStringLiteral(
                   "Project '%1' has no %2 command saved. Ask the user for it once, "
                   "then store it with add_project.")
            .arg(project.name, build ? QStringLiteral("build") : QStringLiteral("run"));
    }
    if (!project.exists())
        return QStringLiteral("Folder is gone: %1").arg(project.path);

    bool ok = false;
    const QString output = runShell(command, project.path, build ? 900 : 60, &ok);

    // Сборка меняет диск, но откатывать её нечем — журнал об этом
    // скажет прямо, вместо того чтобы промолчать.
    {
        EditJournal::Scope batch(QStringLiteral("инструмент build_project"));
        EditJournal::instance().recordExternal(
            QStringLiteral("%1 проекта %2: %3")
                .arg(build ? QStringLiteral("сборка") : QStringLiteral("запуск"),
                     project.name, command));
    }

    if (okOut)
        *okOut = ok;

    return QStringLiteral("%1 \"%2\": %3\n%4")
        .arg(build ? QStringLiteral("Сборка") : QStringLiteral("Запуск"),
             project.name,
             ok ? QStringLiteral("успешно") : QStringLiteral("с ошибкой"),
             output);
}

QString ProjectRegistry::summaryForModel() const
{
    if (m_projects.isEmpty())
        return QStringLiteral("No projects registered yet.");

    QStringList lines;
    for (const ProjectEntry& p : m_projects) {
        QString line = p.human();
        if (p.name.compare(m_active, Qt::CaseInsensitive) == 0)
            line = QStringLiteral("* ") + line + QStringLiteral("\n    (активный)");
        lines << line;
    }
    return lines.join(QStringLiteral("\n"));
}

// ============================================================
//  Инструменты
// ============================================================

namespace JarvisTools {

void registerProjectTools(ToolRegistry& reg, ProjectRegistry* projects)
{
    if (!projects) {
        qWarning() << "[Tools] registerProjectTools: registry is null";
        return;
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("list_projects");
        t.category    = QStringLiteral("projects");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "The user's projects: path, kind, IDE, build and run commands, which one "
            "is active. Call it whenever the user names a project (\"my ESP32 "
            "project\", \"the rally sim\") instead of guessing a path.");
        t.schema  = ToolSchema::empty();
        t.handler = [projects](const QJsonObject&) -> ToolResult {
            return ToolResult::success(projects->summaryForModel(),
                                       QStringLiteral("Проектов: %1").arg(projects->count()));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("add_project");
        t.category    = QStringLiteral("projects");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Register a project folder, or update an existing entry. Only 'path' is "
            "required - the kind, the IDE and the usual build command are detected "
            "from the folder. Save the build and run commands the user actually uses; "
            "do not invent them.");
        t.schema = ToolSchema()
                       .str("path", "Absolute path to the project root")
                       .str("name", "Short name to call it by (default: folder name)", false)
                       .str("kind", "Override the detected kind", false)
                       .str("ide", "IDE alias: rider, clion, code, ...", false)
                       .str("build", "Build command, run from the project root", false)
                       .str("run", "Run command", false)
                       .str("docs", "Docs link or path", false)
                       .build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Запомнить проект: %1")
                .arg(a.value(QStringLiteral("path")).toString());
        };
        t.handler = [projects](const QJsonObject& a) -> ToolResult {
            const QString path = a.value(QStringLiteral("path")).toString().trimmed();
            if (path.isEmpty())
                return ToolResult::failure(QStringLiteral("path is required"));
            if (!QFileInfo(path).isDir())
                return ToolResult::failure(QStringLiteral("Not a folder: %1").arg(path));

            ProjectEntry p = ProjectRegistry::sniff(path);

            const QString name = a.value(QStringLiteral("name")).toString().trimmed();
            if (!name.isEmpty())  p.name = name;

            const QString kind = a.value(QStringLiteral("kind")).toString().trimmed();
            if (!kind.isEmpty())  p.kind = kind;

            const QString ide = a.value(QStringLiteral("ide")).toString().trimmed();
            if (!ide.isEmpty())   p.ide = ide;

            const QString build = a.value(QStringLiteral("build")).toString().trimmed();
            if (!build.isEmpty()) p.buildCommand = build;

            const QString run = a.value(QStringLiteral("run")).toString().trimmed();
            if (!run.isEmpty())   p.runCommand = run;

            p.docs = a.value(QStringLiteral("docs")).toString().trimmed();

            if (!projects->addOrReplace(p))
                return ToolResult::failure(QStringLiteral("Could not save the project."));

            return ToolResult::success(
                QStringLiteral("Saved project:\n") + p.human(),
                QStringLiteral("Проект \"%1\" запомнен").arg(p.name));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("open_project");
        t.category    = QStringLiteral("projects");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Make a project the current one: the code index, the project context and "
            "the git tools all switch to it. Set launch_ide to also open it in its "
            "IDE - that is what \"open my ESP32 project\" usually means.");
        t.schema = ToolSchema()
                       .str("name", "Project name (a fragment is enough)")
                       .boolean("launch_ide", "Also start the project's IDE", false)
                       .build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Открыть проект \"%1\"%2")
                .arg(a.value(QStringLiteral("name")).toString(),
                     a.value(QStringLiteral("launch_ide")).toBool(false)
                         ? QStringLiteral(" вместе с IDE") : QString());
        };
        t.handler = [projects](const QJsonObject& a) -> ToolResult {
            const QString name = a.value(QStringLiteral("name")).toString();
            bool ok = false;
            const QString report = projects->open(
                name, a.value(QStringLiteral("launch_ide")).toBool(false), &ok);
            return ok ? ToolResult::success(report, report.section(QChar('\n'), 0, 0))
                      : ToolResult::failure(report);
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("build_project");
        t.category    = QStringLiteral("projects");
        // Команду написал человек и она хранится в реестре — это не тот
        // случай, когда модель сочиняет строку для shell (run_command).
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Run the project's saved build command (or its run command with "
            "action=run) from the project root and return the tail of the output - "
            "compiler errors live at the end. Without a name it uses the active "
            "project. If no command is saved, ask the user for it and store it with "
            "add_project instead of guessing one.");
        t.schema = ToolSchema()
                       .str("name", "Project name; omit for the active project", false)
                       .choice("action", { QStringLiteral("build"), QStringLiteral("run") },
                               "Which command to use (default build)", false)
                       .build();
        t.preview = [](const QJsonObject& a) {
            const QString action = a.value(QStringLiteral("action")).toString();
            return QStringLiteral("%1 проект %2")
                .arg(action == QLatin1String("run") ? QStringLiteral("Запустить")
                                                    : QStringLiteral("Собрать"),
                     a.value(QStringLiteral("name")).toString());
        };
        t.handler = [projects](const QJsonObject& a) -> ToolResult {
            const bool build = a.value(QStringLiteral("action")).toString()
                                   != QLatin1String("run");
            bool ok = false;
            const QString report = projects->runCommandFor(
                a.value(QStringLiteral("name")).toString(), build, &ok);
            return ok ? ToolResult::success(report, report.section(QChar('\n'), 0, 0))
                      : ToolResult::failure(report);
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("remove_project");
        t.category    = QStringLiteral("projects");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Forget a project. Only removes it from the list - nothing on disk is "
            "touched.");
        t.schema  = ToolSchema().str("name", "Project name").build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Забыть проект \"%1\" (с диска ничего не удаляется)")
                .arg(a.value(QStringLiteral("name")).toString());
        };
        t.handler = [projects](const QJsonObject& a) -> ToolResult {
            const QString name = a.value(QStringLiteral("name")).toString().trimmed();
            if (!projects->remove(name))
                return ToolResult::failure(QStringLiteral("No project named '%1'.").arg(name));
            return ToolResult::success(QStringLiteral("Forgot project '%1'.").arg(name),
                                       QStringLiteral("Проект забыт"));
        };
        reg.registerTool(t);
    }
}

} // namespace JarvisTools
