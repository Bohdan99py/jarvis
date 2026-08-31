// -------------------------------------------------------
// context_tracker.cpp — см. context_tracker.h
// -------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "context_tracker.h"

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QTimer>
#include <QDebug>

#include <windows.h>

namespace {

// Хвост заголовка, по которому опознаётся само приложение. Порядок
// важен: длинные варианты идут первыми, иначе "Visual Studio Code"
// будет съеден правилом "Visual Studio".
const QStringList& appSuffixes()
{
    static const QStringList kSuffixes = {
        QStringLiteral("Visual Studio Code"),
        QStringLiteral("Microsoft Visual Studio"),
        QStringLiteral("JetBrains Rider"),
        QStringLiteral("IntelliJ IDEA"),
        QStringLiteral("Android Studio"),
        QStringLiteral("Sublime Text"),
        QStringLiteral("Unreal Editor"),
        QStringLiteral("Google Chrome"),
        QStringLiteral("Mozilla Firefox"),
        QStringLiteral("Microsoft Edge"),
        QStringLiteral("Notepad++"),
        QStringLiteral("PyCharm"),
        QStringLiteral("CLion"),
        QStringLiteral("Rider"),
        QStringLiteral("Qt Creator"),
        QStringLiteral("Blender"),
        QStringLiteral("Cursor"),
    };
    return kSuffixes;
}

bool looksLikeFileName(const QString& part)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^[^\\/:*?"<>|]+\.[A-Za-z0-9_+-]{1,8}$)"));
    return re.match(part.trimmed()).hasMatch();
}

bool isBrowserProcess(const QString& processLower)
{
    static const QStringList kBrowsers = {
        QStringLiteral("chrome"),  QStringLiteral("firefox"),
        QStringLiteral("msedge"),  QStringLiteral("opera"),
        QStringLiteral("brave"),   QStringLiteral("vivaldi"),
        QStringLiteral("yandex"),  QStringLiteral("arc")
    };
    for (const QString& b : kBrowsers) {
        if (processLower.contains(b))
            return true;
    }
    return false;
}

QString trimTitlePart(QString part)
{
    // Незасохранённый файл в IDE помечается звёздочкой или точкой.
    part = part.trimmed();
    while (!part.isEmpty()
           && (part.startsWith(QChar('*')) || part.startsWith(QChar(0x2022))))
        part = part.mid(1).trimmed();
    return part;
}

} // namespace

// ============================================================
//  Разбор заголовка
// ============================================================

QString ContextTracker::friendlyAppName(const QString& processName)
{
    const QString lower = processName.toLower();

    struct Pair { const char* needle; const char* name; };
    static const Pair kMap[] = {
        { "rider",          "Rider" },
        { "clion",          "CLion" },
        { "idea",           "IntelliJ IDEA" },
        { "pycharm",        "PyCharm" },
        { "webstorm",       "WebStorm" },
        { "studio64",       "Android Studio" },
        { "devenv",         "Visual Studio" },
        { "code",           "VS Code" },
        { "cursor",         "Cursor" },
        { "qtcreator",      "Qt Creator" },
        { "notepad++",      "Notepad++" },
        { "sublime",        "Sublime Text" },
        { "unrealeditor",   "Unreal Editor" },
        { "ue5",            "Unreal Engine" },
        { "unity",          "Unity" },
        { "blender",        "Blender" },
        { "godot",          "Godot" },
        { "kicad",          "KiCad" },
        { "eeschema",       "KiCad Schematic" },
        { "pcbnew",         "KiCad PCB" },
        { "arduino",        "Arduino IDE" },
        { "platformio",     "PlatformIO" },
        { "chrome",         "Chrome" },
        { "msedge",         "Edge" },
        { "firefox",        "Firefox" },
        { "opera",          "Opera" },
        { "brave",          "Brave" },
        { "windowsterminal","Terminal" },
        { "powershell",     "PowerShell" },
        { "cmd.exe",        "Command Prompt" },
        { "explorer",       "Explorer" },
        { "discord",        "Discord" },
        { "steam",          "Steam" },
        { "telegram",       "Telegram" },
        { "obs64",          "OBS" },
        { "photoshop",      "Photoshop" },
        { "krita",          "Krita" },
        { "figma",          "Figma" },
        { "excel",          "Excel" },
        { "winword",        "Word" },
    };

    for (const Pair& p : kMap) {
        if (lower.contains(QLatin1String(p.needle)))
            return QString::fromLatin1(p.name);
    }

    // Незнакомое приложение: "SomeApp.exe" -> "SomeApp"
    QString name = processName;
    if (name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
        name.chop(4);
    return name;
}

void ContextTracker::parseWindowTitle(const QString& processName,
                                      const QString& title,
                                      QString* fileOut,
                                      QString* projectOut,
                                      QString* pageOut)
{
    if (fileOut)    fileOut->clear();
    if (projectOut) projectOut->clear();
    if (pageOut)    pageOut->clear();

    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty())
        return;

    // Заголовки делятся длинным, коротким тире или дефисом с пробелами.
    static const QRegularExpression sep(QStringLiteral(" [-\\x{2013}\\x{2014}] "));
    QStringList parts = trimmed.split(sep, Qt::SkipEmptyParts);
    for (QString& p : parts)
        p = trimTitlePart(p);

    // Убираем сегмент с именем самого приложения — он ничего не говорит
    // о том, что открыто.
    for (int i = parts.size() - 1; i >= 0; --i) {
        for (const QString& suffix : appSuffixes()) {
            if (parts[i].compare(suffix, Qt::CaseInsensitive) == 0
                || parts[i].endsWith(suffix, Qt::CaseInsensitive)) {
                parts.removeAt(i);
                break;
            }
        }
    }
    if (parts.isEmpty())
        return;

    if (isBrowserProcess(processName.toLower())) {
        if (pageOut)
            *pageOut = parts.join(QStringLiteral(" - "));
        return;
    }

    // Редакторы: одна часть похожа на имя файла, другая — на проект.
    // Порядок у разных IDE разный ("проект – файл" у JetBrains,
    // "файл - папка - VS Code" у VS Code), поэтому ищем по признаку,
    // а не по позиции.
    for (const QString& part : parts) {
        const QString base = QFileInfo(part).fileName();
        if (looksLikeFileName(base)) {
            if (fileOut && fileOut->isEmpty())
                *fileOut = base;
        } else if (projectOut && projectOut->isEmpty()) {
            *projectOut = part;
        }
    }

    // Единственная часть и она не файл — считаем её названием документа
    // или проекта (Unreal, Blender, Photoshop так и делают).
    if (projectOut && projectOut->isEmpty() && fileOut && fileOut->isEmpty()
        && parts.size() == 1) {
        *projectOut = parts.first();
    }
}

// ============================================================
//  MachineContext
// ============================================================

QString MachineContext::toPromptBlock() const
{
    if (isEmpty())
        return QString();

    QString out;
    out += QStringLiteral("Active window: %1").arg(appName);
    if (!windowTitle.isEmpty())
        out += QStringLiteral("  —  \"%1\"").arg(windowTitle);
    out += QChar('\n');

    if (!currentFile.isEmpty())
        out += QStringLiteral("Open file: %1\n").arg(currentFile);
    if (!projectName.isEmpty())
        out += QStringLiteral("Project in that window: %1\n").arg(projectName);
    if (!browserPage.isEmpty())
        out += QStringLiteral("Browser page: %1\n").arg(browserPage);

    if (!projectRoot.isEmpty())
        out += QStringLiteral("Indexed project root: %1\n").arg(projectRoot);
    if (!recentFiles.isEmpty())
        out += QStringLiteral("Recently changed files: %1\n")
                   .arg(recentFiles.mid(0, 6).join(QStringLiteral(", ")));
    if (!recentApps.isEmpty())
        out += QStringLiteral("Recent apps: %1\n")
                   .arg(recentApps.join(QStringLiteral(" -> ")));
    if (!runningApps.isEmpty())
        out += QStringLiteral("Already open (do NOT offer to launch these — they are "
                              "running; switch to them instead): %1\n")
                   .arg(runningApps.join(QStringLiteral(", ")));
    if (!clipboardPreview.isEmpty())
        out += QStringLiteral("Clipboard: %1\n").arg(clipboardPreview);

    out += QStringLiteral(
        "Resolve \"here\", \"this\", \"it\", \"this file\", \"this project\" against "
        "the window above — that is what the user is looking at, not the JARVIS "
        "window. If you need the full path of the open file, search for it under "
        "the project root instead of guessing. Call get_context if the user may "
        "have switched windows since this snapshot.\n");
    return out;
}

QString MachineContext::toHumanText() const
{
    if (isEmpty())
        return QStringLiteral("No foreign window has been seen yet.");

    QStringList lines;
    lines << QStringLiteral("Active app:    %1 (%2)").arg(appName, processName);
    lines << QStringLiteral("Window title:  %1").arg(windowTitle);
    if (!currentFile.isEmpty())
        lines << QStringLiteral("Current file:  %1").arg(currentFile);
    if (!projectName.isEmpty())
        lines << QStringLiteral("Project:       %1").arg(projectName);
    if (!browserPage.isEmpty())
        lines << QStringLiteral("Browser page:  %1").arg(browserPage);
    if (!projectRoot.isEmpty())
        lines << QStringLiteral("Project root:  %1").arg(projectRoot);
    if (!recentFiles.isEmpty())
        lines << QStringLiteral("Recent files:  %1")
                     .arg(recentFiles.mid(0, 8).join(QStringLiteral(", ")));
    if (!recentApps.isEmpty())
        lines << QStringLiteral("Recent apps:   %1")
                     .arg(recentApps.join(QStringLiteral(" -> ")));
    if (!runningApps.isEmpty())
        lines << QStringLiteral("Already open:  %1")
                     .arg(runningApps.join(QStringLiteral(", ")));
    if (!clipboardPreview.isEmpty())
        lines << QStringLiteral("Clipboard:     %1").arg(clipboardPreview);
    lines << QStringLiteral("Captured:      %1")
                 .arg(capturedAt.toString(QStringLiteral("HH:mm:ss")));
    return lines.join(QChar('\n'));
}

// ============================================================
//  ContextTracker
// ============================================================

ContextTracker::ContextTracker(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &ContextTracker::poll);
}

void ContextTracker::start(int pollMs)
{
    m_timer->setInterval(qBound(300, pollMs, 10000));
    m_timer->start();
    poll();   // первый снимок сразу, не через полторы секунды
}

void ContextTracker::stop()
{
    m_timer->stop();
}

bool ContextTracker::isRunning() const
{
    return m_timer->isActive();
}

void ContextTracker::setProjectInfoProvider(ProjectInfoProvider provider)
{
    m_projectInfo = std::move(provider);
}

void ContextTracker::setRunningAppsProvider(RunningAppsProvider provider)
{
    m_runningApps = std::move(provider);
}

void ContextTracker::poll()
{
    HWND fg = GetForegroundWindow();
    if (!fg)
        return;

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid || pid == GetCurrentProcessId())
        return;   // это наше собственное окно — «здесь» им быть не может

    wchar_t titleBuf[512] = {};
    GetWindowTextW(fg, titleBuf, 511);
    const QString title = QString::fromWCharArray(titleBuf).trimmed();
    if (title.isEmpty())
        return;   // служебные окна без заголовка нам ни о чём не говорят

    QString process;
    if (HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t exePath[MAX_PATH] = {};
        DWORD sz = MAX_PATH;
        if (QueryFullProcessImageNameW(hProc, 0, exePath, &sz))
            process = QFileInfo(QString::fromWCharArray(exePath)).fileName();
        CloseHandle(hProc);
    }

    const QString app = friendlyAppName(process);
    const bool changed = (title != m_title) || (process != m_process);

    m_title   = title;
    m_process = process;
    m_app     = app;

    if (!app.isEmpty()) {
        m_recentApps.removeAll(app);
        m_recentApps.prepend(app);
        while (m_recentApps.size() > kMaxRecentApps)
            m_recentApps.removeLast();
    }

    if (changed)
        emit focusChanged(app, title);
}

MachineContext ContextTracker::snapshot() const
{
    MachineContext ctx;
    ctx.capturedAt  = QDateTime::currentDateTime();
    ctx.appName     = m_app;
    ctx.processName = m_process;
    ctx.windowTitle = m_title;
    ctx.recentApps  = m_recentApps;

    parseWindowTitle(m_process, m_title,
                     &ctx.currentFile, &ctx.projectName, &ctx.browserPage);

    if (m_projectInfo)
        m_projectInfo(ctx.projectRoot, ctx.recentFiles);

    if (m_runningApps)
        ctx.runningApps = m_runningApps();

    // Буфер читаем только в момент снимка: опрашивать его по таймеру —
    // лишний повод драться за clipboard с другими программами.
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        QString clip = cb->text().trimmed();
        if (!clip.isEmpty()) {
            clip.replace(QChar('\n'), QChar(' '));
            ctx.clipboardPreview = clip.length() > 160
                                       ? clip.left(157) + QStringLiteral("...")
                                       : clip;
        }
    }

    return ctx;
}

QString ContextTracker::promptBlock() const
{
    return snapshot().toPromptBlock();
}
