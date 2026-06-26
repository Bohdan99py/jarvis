// ============================================================
// command_dispatcher_tg.cpp — Sandboxed Telegram Command Executor
// ============================================================

#include "command_dispatcher_tg.h"
#include "j2j_telegram_gateway.h"
#include "telegram_access_manager.h"
#include "database_manager.h"
#include "jarvis_paths.h"

#include <QSysInfo>
#include <QStorageInfo>
#include <QScreen>
#include <QApplication>
#include <QPixmap>
#include <QDir>
#include <QFile>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QDebug>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>

// ============================================================
//  Construction
// ============================================================

CommandDispatcherTg::CommandDispatcherTg(J2JTelegramGateway* gateway,
                                         TelegramAccessManager* accessMgr,
                                         QObject* parent)
    : QObject(parent)
    , m_gateway(gateway)
    , m_accessMgr(accessMgr)
    , m_startTime(QDateTime::currentDateTime())
{
    qDebug() << "[CommandDispatcher] Initialized with"
             << 7 << "registered commands";
}

// ============================================================
//  Dispatch entry point
// ============================================================

DispatchResult CommandDispatcherTg::dispatch(qint64 chatId,
                                              const QString& command,
                                              bool english)
{
    const QString cmd  = command.section(QLatin1Char(' '), 0, 0).toLower();
    const QString args = command.section(QLatin1Char(' '), 1).trimmed();

    if (cmd == QStringLiteral("/screenshot"))
        return cmdScreenshot(chatId, english);

    if (cmd == QStringLiteral("/sysinfo"))
        return cmdSysInfo(english);

    if (cmd == QStringLiteral("/disk"))
        return cmdDisk(english);

    if (cmd == QStringLiteral("/uptime"))
        return cmdUptime(english);

    if (cmd == QStringLiteral("/tasks"))
        return cmdTasks(chatId, english);

    if (cmd == QStringLiteral("/note"))
        return cmdNote(chatId, args, english);

    if (cmd == QStringLiteral("/notes"))
        return cmdNotes(chatId, english);

    return {};  // not handled
}

// ============================================================
//  /screenshot — Admin only, capture primary screen
// ============================================================

DispatchResult CommandDispatcherTg::cmdScreenshot(qint64 /*chatId*/, bool english)
{
    DispatchResult r;
    r.handled = true;

    QScreen* screen = QApplication::primaryScreen();
    if (!screen) {
        r.response = english ? QStringLiteral("⚠ No screen available.")
                             : QStringLiteral("⚠ Экран недоступен.");
        return r;
    }

    QPixmap shot = screen->grabWindow(0);
    if (shot.isNull()) {
        r.response = english ? QStringLiteral("⚠ Screenshot capture failed.")
                             : QStringLiteral("⚠ Не удалось сделать снимок экрана.");
        return r;
    }

    const QString dir = J2JTelegramGateway::workspaceOutputDir();
    const QString ts = QDateTime::currentDateTime()
        .toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString path = dir + QStringLiteral("/screenshot_%1.png").arg(ts);

    if (!shot.save(path, "PNG")) {
        r.response = english ? QStringLiteral("⚠ Failed to save screenshot.")
                             : QStringLiteral("⚠ Не удалось сохранить снимок.");
        return r;
    }

    r.response = english
        ? QStringLiteral("📸 Screenshot captured")
        : QStringLiteral("📸 Скриншот сделан");
    r.imagePath = path;

    qDebug() << "[CommandDispatcher] Screenshot saved:" << path;
    return r;
}

// ============================================================
//  /sysinfo — Admin only
// ============================================================

DispatchResult CommandDispatcherTg::cmdSysInfo(bool english)
{
    DispatchResult r;
    r.handled = true;

    // RAM usage via Windows API
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    const double totalGb = mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    const double usedGb  = (mem.ullTotalPhys - mem.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);

    // Uptime
    const qint64 uptickMs = static_cast<qint64>(GetTickCount64());
    const int uptimeH = static_cast<int>(uptickMs / 3600000);
    const int uptimeM = static_cast<int>((uptickMs % 3600000) / 60000);

    // Process count
    DWORD pids[4096];
    DWORD bytesReturned = 0;
    int procCount = 0;
    if (EnumProcesses(pids, sizeof(pids), &bytesReturned))
        procCount = static_cast<int>(bytesReturned / sizeof(DWORD));

    if (english) {
        r.response = QStringLiteral(
            "🖥 *System Info*\n"
            "```\n"
            "Host:       %1\n"
            "OS:         %2 %3\n"
            "CPU:        %4\n"
            "RAM:        %5 / %6 GB (%7%%)\n"
            "Uptime:     %8h %9m\n"
            "Processes:  %10\n"
            "```"
        ).arg(QSysInfo::machineHostName(),
              QSysInfo::productType(),
              QSysInfo::productVersion(),
              QSysInfo::currentCpuArchitecture(),
              QString::number(usedGb, 'f', 1),
              QString::number(totalGb, 'f', 1),
              QString::number(mem.dwMemoryLoad),
              QString::number(uptimeH),
              QString::number(uptimeM),
              QString::number(procCount));
    } else {
        r.response = QStringLiteral(
            "🖥 *Информация о системе*\n"
            "```\n"
            "Хост:       %1\n"
            "ОС:         %2 %3\n"
            "CPU:        %4\n"
            "ОЗУ:        %5 / %6 ГБ (%7%%)\n"
            "Аптайм:     %8ч %9м\n"
            "Процессов:  %10\n"
            "```"
        ).arg(QSysInfo::machineHostName(),
              QSysInfo::productType(),
              QSysInfo::productVersion(),
              QSysInfo::currentCpuArchitecture(),
              QString::number(usedGb, 'f', 1),
              QString::number(totalGb, 'f', 1),
              QString::number(mem.dwMemoryLoad),
              QString::number(uptimeH),
              QString::number(uptimeM),
              QString::number(procCount));
    }

    qDebug() << "[CommandDispatcher] /sysinfo served";
    return r;
}

// ============================================================
//  /disk — Admin only
// ============================================================

DispatchResult CommandDispatcherTg::cmdDisk(bool english)
{
    DispatchResult r;
    r.handled = true;

    r.response = english ? QStringLiteral("💾 *Disk Usage*\n```\n")
                         : QStringLiteral("💾 *Использование дисков*\n```\n");

    const auto volumes = QStorageInfo::mountedVolumes();
    for (const auto& vol : volumes) {
        if (!vol.isReady() || vol.isReadOnly()) continue;
        if (vol.bytesTotal() == 0) continue;

        const double totalGb = vol.bytesTotal() / (1024.0 * 1024.0 * 1024.0);
        const double freeGb  = vol.bytesFree()  / (1024.0 * 1024.0 * 1024.0);
        const int pctUsed = static_cast<int>(100.0 * (1.0 - freeGb / totalGb));

        r.response += QStringLiteral("%1  %2 GB free / %3 GB  (%4%% used)\n")
            .arg(vol.rootPath(), -4)
            .arg(freeGb, 0, 'f', 1)
            .arg(totalGb, 0, 'f', 1)
            .arg(pctUsed);
    }
    r.response += QStringLiteral("```");

    qDebug() << "[CommandDispatcher] /disk served";
    return r;
}

// ============================================================
//  /uptime — User+
// ============================================================

DispatchResult CommandDispatcherTg::cmdUptime(bool english)
{
    DispatchResult r;
    r.handled = true;

    const qint64 secs = m_startTime.secsTo(QDateTime::currentDateTime());
    const int h = static_cast<int>(secs / 3600);
    const int m = static_cast<int>((secs % 3600) / 60);
    const int s = static_cast<int>(secs % 60);

    r.response = english
        ? QStringLiteral("⏱ JARVIS uptime: *%1h %2m %3s*\nStarted: %4")
            .arg(h).arg(m).arg(s)
            .arg(m_startTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
        : QStringLiteral("⏱ Аптайм JARVIS: *%1ч %2м %3с*\nЗапущен: %4")
            .arg(h).arg(m).arg(s)
            .arg(m_startTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));

    return r;
}

// ============================================================
//  /tasks — Tester+, Kanban summary
// ============================================================

DispatchResult CommandDispatcherTg::cmdTasks(qint64 /*chatId*/, bool english)
{
    DispatchResult r;
    r.handled = true;

    auto tasks = DatabaseManager::instance().getTasks(1);
    if (tasks.isEmpty()) {
        r.response = english ? QStringLiteral("📋 No tasks found.")
                             : QStringLiteral("📋 Задач не найдено.");
        return r;
    }

    r.response = english ? QStringLiteral("📋 *Tasks*\n") : QStringLiteral("📋 *Задачи*\n");

    int todo = 0, inProg = 0, done = 0;
    for (const auto& t : tasks) {
        QString icon;
        if (t.status == QStringLiteral("Todo"))            { icon = QStringLiteral("⬜"); ++todo; }
        else if (t.status == QStringLiteral("InProgress")) { icon = QStringLiteral("🔶"); ++inProg; }
        else                                               { icon = QStringLiteral("✅"); ++done; }

        r.response += QStringLiteral("%1 %2 `[%3]`\n").arg(icon, t.title, t.priority);
    }

    r.response += QStringLiteral("\n📊 Todo: %1 | In Progress: %2 | Done: %3")
        .arg(todo).arg(inProg).arg(done);

    return r;
}

// ============================================================
//  /note <text> — User+, save personal note
// ============================================================

DispatchResult CommandDispatcherTg::cmdNote(qint64 chatId, const QString& text,
                                            bool english)
{
    DispatchResult r;
    r.handled = true;

    if (text.isEmpty()) {
        r.response = english
            ? QStringLiteral("Usage: `/note Your note text here`")
            : QStringLiteral("Использование: `/note Текст вашей заметки`");
        return r;
    }

    // Store as a memory KV entry scoped to the chat_id
    const QString key = QStringLiteral("tg_note_%1_%2")
        .arg(chatId)
        .arg(QDateTime::currentMSecsSinceEpoch());
    DatabaseManager::instance().memSet(1, key, text);

    r.response = english
        ? QStringLiteral("📝 Note saved.")
        : QStringLiteral("📝 Заметка сохранена.");

    qDebug() << "[CommandDispatcher] Note saved for chat" << chatId;
    return r;
}

// ============================================================
//  /notes — User+, list recent notes
// ============================================================

DispatchResult CommandDispatcherTg::cmdNotes(qint64 chatId, bool english)
{
    DispatchResult r;
    r.handled = true;

    const QString prefix = QStringLiteral("tg_note_%1_").arg(chatId);
    auto allMem = DatabaseManager::instance().memGetAll(1);

    QStringList notes;
    for (auto it = allMem.begin(); it != allMem.end(); ++it) {
        if (it.key().startsWith(prefix))
            notes.append(it.value());
    }

    if (notes.isEmpty()) {
        r.response = english ? QStringLiteral("📝 No notes found.")
                             : QStringLiteral("📝 Заметок не найдено.");
        return r;
    }

    // Show last 10
    r.response = english ? QStringLiteral("📝 *Your Notes*\n")
                         : QStringLiteral("📝 *Ваши заметки*\n");

    int start = qMax(0, notes.size() - 10);
    for (int i = start; i < notes.size(); ++i)
        r.response += QStringLiteral("• %1\n").arg(notes[i]);

    return r;
}
