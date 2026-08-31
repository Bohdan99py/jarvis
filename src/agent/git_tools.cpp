// -------------------------------------------------------
// git_tools.cpp — см. git_tools.h
// -------------------------------------------------------

#include "git_tools.h"

#include "edit_journal.h"
#include "tool_registry.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QDebug>

namespace {

// Ответ инструмента уходит в контекст модели целиком, а diff большого
// коммита легко перекрывает всё остальное. Режем и говорим об этом.
constexpr int kMaxOutputChars = 6000;

QString clamp(const QString& text)
{
    if (text.length() <= kMaxOutputChars)
        return text;
    return text.left(kMaxOutputChars)
           + QStringLiteral("\n... [обрезано, всего %1 символов]").arg(text.length());
}

struct GitResult
{
    bool    ok = false;
    int     exitCode = -1;
    QString output;
};

GitResult runGit(const QStringList& args, const QString& repo, int timeoutSec = 30)
{
    GitResult result;

    QProcess proc;
    proc.setWorkingDirectory(repo);

    // Каналы врозь, а не MergedChannels: в репозитории со смешанными
    // переводами строк git на каждый файл печатает в stderr «LF will be
    // replaced by CRLF». Слитые с диффом, эти строки съедают весь лимит
    // ответа, и модель получает предупреждения вместо изменений.
    proc.setProcessChannelMode(QProcess::SeparateChannels);

    // Без этого git может уйти в ожидание логина или пейджера — а ждать
    // его здесь некому: инструмент выполняется синхронно и молча.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));
    env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    proc.setProcessEnvironment(env);

    proc.start(QStringLiteral("git"), args);
    if (!proc.waitForStarted(5000)) {
        result.output = QStringLiteral("Git is not installed or not in PATH.");
        return result;
    }
    if (!proc.waitForFinished(timeoutSec * 1000)) {
        proc.kill();
        proc.waitForFinished(2000);
        result.output = QStringLiteral("git %1 timed out after %2s")
                            .arg(args.join(QChar(' '))).arg(timeoutSec);
        return result;
    }

    const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();

    result.exitCode = proc.exitCode();
    result.ok       = (result.exitCode == 0);
    // Удача — только stdout; провал — то, что git объяснил в stderr,
    // а если он смолчал, хотя бы то, что успел напечатать.
    result.output   = result.ok ? out : (err.isEmpty() ? out : err);
    return result;
}

// Путь -> корень репозитория. Пустой путь означает «текущий проект»:
// то, что человек видит на экране, а не то, что модель припомнила.
QString resolveRepo(const QString& given,
                    const JarvisTools::RepoProvider& provider,
                    QString* errorOut)
{
    QString start = given.trimmed();
    if (start.isEmpty() && provider)
        start = provider();

    if (start.isEmpty()) {
        *errorOut = QStringLiteral(
            "No repository given and no project is open. Pass 'repo' with a path.");
        return QString();
    }

    const QFileInfo fi(start);
    if (!fi.exists()) {
        *errorOut = QStringLiteral("Path does not exist: %1").arg(start);
        return QString();
    }
    const QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();

    const GitResult top = runGit({ QStringLiteral("rev-parse"),
                                   QStringLiteral("--show-toplevel") }, dir, 10);
    if (!top.ok) {
        *errorOut = QStringLiteral("Not a git repository: %1").arg(dir);
        return QString();
    }
    return QDir::fromNativeSeparators(top.output.trimmed());
}

QString repoName(const QString& repo)
{
    return QDir(repo).dirName();
}

} // namespace

namespace JarvisTools {

void registerGitTools(ToolRegistry& reg, RepoProvider defaultRepo)
{
    // --------------------------------------------------------
    //  git_status
    // --------------------------------------------------------
    {
        ToolSpec t;
        t.name        = QStringLiteral("git_status");
        t.category    = QStringLiteral("git");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "Working tree state of a git repository: current branch, how far it is "
            "ahead or behind the remote, and which files are modified, staged or "
            "untracked. With no 'repo' argument it uses the project the user is "
            "currently working in.");
        t.schema = ToolSchema()
                       .str("repo", "Repository path; omit for the current project", false)
                       .build();
        t.handler = [defaultRepo](const QJsonObject& a) -> ToolResult {
            QString error;
            const QString repo = resolveRepo(a.value(QStringLiteral("repo")).toString(),
                                             defaultRepo, &error);
            if (repo.isEmpty())
                return ToolResult::failure(error);

            const GitResult res = runGit({ QStringLiteral("status"),
                                           QStringLiteral("--short"),
                                           QStringLiteral("--branch") }, repo);
            if (!res.ok)
                return ToolResult::failure(res.output);

            // Считаем сами: «5 изменённых» в одну строку UI полезнее,
            // чем предложение человеку пересчитать строки глазами.
            int modified = 0, staged = 0, untracked = 0;
            const QStringList lines = res.output.split(QChar('\n'), Qt::SkipEmptyParts);
            for (const QString& line : lines) {
                if (line.startsWith(QLatin1String("##")))
                    continue;
                if (line.startsWith(QLatin1String("??")))
                    ++untracked;
                else {
                    if (line.length() > 0 && line.at(0) != QChar(' '))
                        ++staged;
                    if (line.length() > 1 && line.at(1) != QChar(' '))
                        ++modified;
                }
            }

            const QString clean = lines.size() <= 1
                ? QStringLiteral("Working tree clean.")
                : QString();

            const QString text = QStringLiteral("Repository: %1\n%2\n%3")
                                     .arg(repo, clamp(res.output), clean).trimmed();

            const QString display = lines.size() <= 1
                ? QStringLiteral("%1: чисто").arg(repoName(repo))
                : QStringLiteral("%1: изменено %2, в индексе %3, новых %4")
                      .arg(repoName(repo)).arg(modified).arg(staged).arg(untracked);

            return ToolResult::success(text, display);
        };
        reg.registerTool(t);
    }

    // --------------------------------------------------------
    //  git_diff
    // --------------------------------------------------------
    {
        ToolSpec t;
        t.name        = QStringLiteral("git_diff");
        t.category    = QStringLiteral("git");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "The actual changes in a repository, as a diff. Use it to answer \"what "
            "did I change\", to write a commit message from real content instead of "
            "guessing, or to explain why something broke after the last edits. Ask "
            "for a single file when the whole diff would be too large, or set "
            "summary_only for just the list of changed files with line counts.");
        t.schema = ToolSchema()
                       .str("repo", "Repository path; omit for the current project", false)
                       .str("file", "Limit the diff to this file or folder", false)
                       .boolean("staged", "Show staged changes instead of unstaged", false)
                       .boolean("summary_only", "Only file names and +/- counts", false)
                       .build();
        t.handler = [defaultRepo](const QJsonObject& a) -> ToolResult {
            QString error;
            const QString repo = resolveRepo(a.value(QStringLiteral("repo")).toString(),
                                             defaultRepo, &error);
            if (repo.isEmpty())
                return ToolResult::failure(error);

            QStringList args{ QStringLiteral("diff") };
            if (a.value(QStringLiteral("staged")).toBool(false))
                args << QStringLiteral("--staged");
            if (a.value(QStringLiteral("summary_only")).toBool(false))
                args << QStringLiteral("--stat");

            const QString file = a.value(QStringLiteral("file")).toString().trimmed();
            if (!file.isEmpty())
                args << QStringLiteral("--") << file;

            const GitResult res = runGit(args, repo, 60);
            if (!res.ok)
                return ToolResult::failure(res.output);

            if (res.output.isEmpty()) {
                return ToolResult::success(
                    QStringLiteral("No changes%1.")
                        .arg(a.value(QStringLiteral("staged")).toBool(false)
                                 ? QStringLiteral(" staged")
                                 : QString()),
                    QStringLiteral("%1: изменений нет").arg(repoName(repo)));
            }

            return ToolResult::success(clamp(res.output),
                                       QStringLiteral("%1: diff%2")
                                           .arg(repoName(repo),
                                                file.isEmpty() ? QString()
                                                               : QStringLiteral(" ") + file));
        };
        reg.registerTool(t);
    }

    // --------------------------------------------------------
    //  git_log
    // --------------------------------------------------------
    {
        ToolSpec t;
        t.name        = QStringLiteral("git_log");
        t.category    = QStringLiteral("git");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "Commit history. Answers \"what did I do today\", \"what changed this "
            "week\", \"when did this file last change\". 'since' takes anything git "
            "understands: midnight, yesterday, \"3 days ago\", 2026-08-01.");
        t.schema = ToolSchema()
                       .str("repo", "Repository path; omit for the current project", false)
                       .integer("count", "How many commits (default 15)", false)
                       .str("since", "Only commits after this point in time", false)
                       .str("file", "Only commits touching this file or folder", false)
                       .boolean("mine_only", "Only commits by the configured git user", false)
                       .boolean("with_stats", "Include changed file counts per commit", false)
                       .build();
        t.handler = [defaultRepo](const QJsonObject& a) -> ToolResult {
            QString error;
            const QString repo = resolveRepo(a.value(QStringLiteral("repo")).toString(),
                                             defaultRepo, &error);
            if (repo.isEmpty())
                return ToolResult::failure(error);

            const int count = qBound(1, a.value(QStringLiteral("count")).toInt(15), 200);

            QStringList args{ QStringLiteral("log"),
                              QStringLiteral("-n") + QString::number(count),
                              QStringLiteral("--date=format:%d.%m %H:%M"),
                              QStringLiteral("--pretty=format:%h  %ad  %an: %s") };

            if (a.value(QStringLiteral("with_stats")).toBool(false))
                args << QStringLiteral("--shortstat");

            const QString since = a.value(QStringLiteral("since")).toString().trimmed();
            if (!since.isEmpty())
                args << QStringLiteral("--since=") + since;

            if (a.value(QStringLiteral("mine_only")).toBool(false)) {
                const GitResult who = runGit({ QStringLiteral("config"),
                                               QStringLiteral("user.email") }, repo, 10);
                if (who.ok && !who.output.isEmpty())
                    args << QStringLiteral("--author=") + who.output.trimmed();
            }

            const QString file = a.value(QStringLiteral("file")).toString().trimmed();
            if (!file.isEmpty())
                args << QStringLiteral("--") << file;

            const GitResult res = runGit(args, repo, 60);
            if (!res.ok)
                return ToolResult::failure(res.output);

            if (res.output.isEmpty())
                return ToolResult::success(QStringLiteral("No commits match that filter."),
                                           QStringLiteral("%1: коммитов нет").arg(repoName(repo)));

            const int commits = res.output.count(QChar('\n')) + 1;
            return ToolResult::success(
                clamp(res.output),
                QStringLiteral("%1: история (%2)").arg(repoName(repo)).arg(commits));
        };
        reg.registerTool(t);
    }

    // --------------------------------------------------------
    //  git_commit
    // --------------------------------------------------------
    {
        ToolSpec t;
        t.name        = QStringLiteral("git_commit");
        t.category    = QStringLiteral("git");
        // Коммит меняет репозиторий, но не разрушает работу: изменения
        // остаются на месте, а сам коммит откатывается. Отсюда Moderate,
        // а не Dangerous.
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Commit the current changes. Read the diff first and write a message that "
            "describes what actually changed - never commit with a made-up message. "
            "By default only already staged changes are committed; set stage_all to "
            "stage every tracked modification first. Never pushes.");
        t.schema = ToolSchema()
                       .str("message", "Commit message")
                       .str("repo", "Repository path; omit for the current project", false)
                       .boolean("stage_all", "Stage all tracked modifications first", false)
                       .build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Коммит: \"%1\"%2")
                .arg(a.value(QStringLiteral("message")).toString().section(QChar('\n'), 0, 0),
                     a.value(QStringLiteral("stage_all")).toBool(false)
                         ? QStringLiteral(" (со всеми изменениями)") : QString());
        };
        t.handler = [defaultRepo](const QJsonObject& a) -> ToolResult {
            const QString message = a.value(QStringLiteral("message")).toString().trimmed();
            if (message.isEmpty())
                return ToolResult::failure(QStringLiteral("A commit needs a message."));

            QString error;
            const QString repo = resolveRepo(a.value(QStringLiteral("repo")).toString(),
                                             defaultRepo, &error);
            if (repo.isEmpty())
                return ToolResult::failure(error);

            QStringList args{ QStringLiteral("commit"), QStringLiteral("-m"), message };
            if (a.value(QStringLiteral("stage_all")).toBool(false))
                args.insert(1, QStringLiteral("--all"));

            const GitResult res = runGit(args, repo, 60);
            if (!res.ok) {
                // Самый частый случай — коммитить нечего; это не ошибка
                // инструмента, и модель должна сказать именно это.
                return ToolResult::failure(res.output.isEmpty()
                                               ? QStringLiteral("git commit failed")
                                               : res.output);
            }

            const GitResult head = runGit({ QStringLiteral("log"), QStringLiteral("-1"),
                                            QStringLiteral("--pretty=format:%h %s") },
                                          repo, 10);

            // Коммит откатывается, но только руками и осознанно: журнал
            // не станет трогать чужую историю — он лишь помнит, чем это
            // было и как это отменить.
            {
                EditJournal::Scope batch(QStringLiteral("инструмент git_commit"));
                EditJournal::instance().recordExternal(
                    QStringLiteral("коммит в %1: %2 — отменить: git reset --soft HEAD~1")
                        .arg(repoName(repo), head.ok ? head.output : message.left(60)));
            }

            return ToolResult::success(
                clamp(res.output),
                QStringLiteral("Коммит %1").arg(head.ok ? head.output : message.left(50)));
        };
        reg.registerTool(t);
    }
}

} // namespace JarvisTools
