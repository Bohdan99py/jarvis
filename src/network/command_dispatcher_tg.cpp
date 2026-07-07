// ============================================================
// command_dispatcher_tg.cpp — Sandboxed Telegram Command Executor
// ============================================================

#include "command_dispatcher_tg.h"
#include "j2j_telegram_gateway.h"
#include "telegram_access_manager.h"
#include "camera_agent.h"
#include "security_camera.h"
#include "database_manager.h"
#include "llm_cache_manager.h"
#include "voice_synthesis_manager.h"
#include "gourmet_module.h"
#include "media_analyzer_module.h"
#include "proactive_reminder_manager.h"
#include "jarvis_paths.h"

#include <QSysInfo>
#include <QStorageInfo>
#include <QScreen>
#include <QApplication>
#include <QPixmap>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
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
    , m_gourmet(new GourmetModule(this))
    , m_mediaAnalyzer(new MediaAnalyzerModule(this))
    , m_camera(new CameraAgent(this))
    , m_security(new SecurityCamera(this))
    , m_startTime(QDateTime::currentDateTime())
{
    qDebug() << "[CommandDispatcher] Initialized with"
             << 13 << "registered commands";
}

void CommandDispatcherTg::setJarvisCore(Jarvis* jarvis)
{
    // Раньше GourmetModule::m_jarvis никогда не выставлялся —
    // processIngredients() всегда падал в ветку "no Jarvis core attached"
    // и отвечал "Гурмэ-модуль не подключён к ядру" на любой /fridge.
    if (m_gourmet) m_gourmet->setJarvisCore(jarvis);
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

    if (cmd == QStringLiteral("/screen_analyze"))
        return cmdScreenAnalyze(chatId, english);

    if (cmd == QStringLiteral("/stop_voice"))
        return cmdStopVoice(english);

    if (cmd == QStringLiteral("/cache_stats"))
        return cmdCacheStats(english);

    if (cmd == QStringLiteral("/fridge"))
        return cmdFridge(chatId, args, english);

    if (cmd == QStringLiteral("/summarize"))
        return cmdSummarize(chatId, args, english);

    if (cmd == QStringLiteral("/remind"))
        return cmdRemind(chatId, args, english);

    if (cmd == QStringLiteral("/webcam") || cmd == QStringLiteral("/cam"))
        return cmdWebcam(chatId, english);

    if (cmd == QStringLiteral("/video"))
        return cmdVideo(chatId, args, english);

    if (cmd == QStringLiteral("/surveillance") || cmd == QStringLiteral("/watch"))
        return cmdSurveillance(chatId, args, english);

    if (cmd == QStringLiteral("/security") || cmd == QStringLiteral("/guard"))
        return cmdSecurity(chatId, args, english);

    if (cmd == QStringLiteral("/enroll_face"))
        return cmdEnrollFace(chatId, english);

    if (cmd == QStringLiteral("/companion_ok")) {
        m_security->confirmCompanionOk(30);
        DispatchResult r;
        r.handled = true;
        r.response = english
            ? QStringLiteral("👍 Got it. Alerts suppressed for 30 minutes.")
            : QStringLiteral("👍 Понял. Алерты отключены на 30 минут.");
        return r;
    }

    if (cmd == QStringLiteral("/companion_no")) {
        m_security->lockScreen();
        DispatchResult r;
        r.handled = true;
        r.response = english
            ? QStringLiteral("🔒 Screen locked immediately.")
            : QStringLiteral("🔒 Экран заблокирован.");
        return r;
    }

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

// ============================================================
//  /screen_analyze — User+, capture + send for AI analysis
// ============================================================

DispatchResult CommandDispatcherTg::cmdScreenAnalyze(qint64 /*chatId*/, bool english)
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
        r.response = english ? QStringLiteral("⚠ Screen capture failed.")
                             : QStringLiteral("⚠ Не удалось захватить экран.");
        return r;
    }

    const QString dir = J2JTelegramGateway::workspaceOutputDir();
    const QString ts  = QDateTime::currentDateTime()
        .toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString path = dir + QStringLiteral("/screen_analyze_%1.png").arg(ts);

    if (!shot.save(path, "PNG")) {
        r.response = english ? QStringLiteral("⚠ Failed to save capture.")
                             : QStringLiteral("⚠ Не удалось сохранить снимок.");
        return r;
    }

    r.response = english
        ? QStringLiteral("🔍 Screen captured for analysis. "
                         "AI will process the image and respond shortly.")
        : QStringLiteral("🔍 Экран захвачен для анализа. "
                         "ИИ обработает изображение и ответит в ближайшее время.");
    r.imagePath = path;

    qDebug() << "[CommandDispatcher] /screen_analyze saved:" << path;
    return r;
}

// ============================================================
//  /stop_voice — User+, flush TTS queue
// ============================================================

DispatchResult CommandDispatcherTg::cmdStopVoice(bool english)
{
    DispatchResult r;
    r.handled = true;

    VoiceSynthesisManager::instance().stopSpeaking();

    r.response = english
        ? QStringLiteral("🔇 Voice playback stopped.")
        : QStringLiteral("🔇 Голосовое воспроизведение остановлено.");

    qDebug() << "[CommandDispatcher] /stop_voice — TTS queue flushed";
    return r;
}

// ============================================================
//  /cache_stats — User+, LLM cache statistics
// ============================================================

DispatchResult CommandDispatcherTg::cmdCacheStats(bool english)
{
    DispatchResult r;
    r.handled = true;

    const int cacheCount  = LlmCacheManager::instance().cacheEntryCount();
    const int offlineCount = DatabaseManager::instance().responseCacheCount();

    if (english) {
        r.response = QStringLiteral(
            "💾 *LLM Cache Statistics*\n"
            "```\n"
            "LLM response cache:    %1 entries\n"
            "Offline response pool: %2 entries\n"
            "Schema version:        11\n"
            "```"
        ).arg(cacheCount).arg(offlineCount);
    } else {
        r.response = QStringLiteral(
            "💾 *Статистика кэша LLM*\n"
            "```\n"
            "Кэш ответов LLM:         %1 записей\n"
            "Пул офлайн-ответов:       %2 записей\n"
            "Версия схемы БД:          11\n"
            "```"
        ).arg(cacheCount).arg(offlineCount);
    }

    qDebug() << "[CommandDispatcher] /cache_stats — LLM:" << cacheCount
             << "offline:" << offlineCount;
    return r;
}

// ============================================================
//  /fridge — User+, gourmet recipe from ingredients
// ============================================================

DispatchResult CommandDispatcherTg::cmdFridge(qint64 /*chatId*/,
                                              const QString& args,
                                              bool english)
{
    DispatchResult r;
    r.handled  = true;
    r.response = m_gourmet->processIngredients(args, english);
    qDebug() << "[CommandDispatcher] /fridge dispatched";
    return r;
}

// ============================================================
//  /summarize — User+, media/article summary
// ============================================================

DispatchResult CommandDispatcherTg::cmdSummarize(qint64 /*chatId*/,
                                                  const QString& args,
                                                  bool english)
{
    DispatchResult r;
    r.handled  = true;
    r.response = m_mediaAnalyzer->summarize(args, english);
    qDebug() << "[CommandDispatcher] /summarize dispatched";
    return r;
}

// ============================================================
//  /remind — User+, set a contextual reminder
// ============================================================

DispatchResult CommandDispatcherTg::cmdRemind(qint64 chatId,
                                               const QString& args,
                                               bool english)
{
    DispatchResult r;
    r.handled = true;

    if (args.trimmed().isEmpty()) {
        r.response = english
            ? QStringLiteral("Usage: `/remind 30 Take laundry out`\n"
                             "First number is delay in minutes.")
            : QStringLiteral("Использование: `/remind 30 Развесить стирку`\n"
                             "Первое число — задержка в минутах.");
        return r;
    }

    // Parse: /remind <minutes> <text>
    const QString firstToken = args.section(QLatin1Char(' '), 0, 0);
    bool ok = false;
    int minutes = firstToken.toInt(&ok);
    QString reminderText;

    if (ok && minutes > 0) {
        reminderText = args.section(QLatin1Char(' '), 1).trimmed();
        if (reminderText.isEmpty())
            reminderText = english ? QStringLiteral("General reminder")
                                   : QStringLiteral("Напоминание");
    } else {
        minutes = 30;
        reminderText = args.trimmed();
    }

    if (minutes > 1440) minutes = 1440;

    ProactiveReminderManager::instance().addReminder(
        chatId, reminderText, minutes, english);

    r.response = english
        ? QStringLiteral("⏰ Reminder set for *%1 min*: %2\n"
                         "Active reminders: %3")
            .arg(minutes).arg(reminderText)
            .arg(ProactiveReminderManager::instance().activeCount())
        : QStringLiteral("⏰ Напоминание через *%1 мин*: %2\n"
                         "Активных напоминаний: %3")
            .arg(minutes).arg(reminderText)
            .arg(ProactiveReminderManager::instance().activeCount());

    qDebug() << "[CommandDispatcher] /remind set:" << minutes << "min —" << reminderText;
    return r;
}

// ============================================================
//  /webcam — capture a photo from the webcam
// ============================================================

DispatchResult CommandDispatcherTg::cmdWebcam(qint64 chatId, bool english)
{
    DispatchResult r;
    r.handled = true;

    if (!m_camera->hasWebcam()) {
        r.response = english
            ? QStringLiteral("📷 No webcam detected on this PC.")
            : QStringLiteral("📷 Веб-камера не обнаружена.");
        return r;
    }

    r.response = english
        ? QStringLiteral("📷 Capturing webcam...")
        : QStringLiteral("📷 Захватываю с веб-камеры...");

    // Async: capture will arrive via signal → send as photo
    auto conn = std::make_shared<QMetaObject::Connection>();
    auto errConn = std::make_shared<QMetaObject::Connection>();

    *conn = connect(m_camera, &CameraAgent::webcamCaptured, this,
                    [this, chatId, english, conn, errConn](const QImage& img) {
        disconnect(*conn);
        disconnect(*errConn);

        // Save to workspace and send
        const QString dir = J2JTelegramGateway::workspaceOutputDir();
        const QString ts = QDateTime::currentDateTime()
            .toString(QStringLiteral("yyyyMMdd_HHmmss"));
        const QString path = dir + QStringLiteral("/webcam_%1.jpg").arg(ts);
        img.save(path, "JPEG", 85);

        m_gateway->sendImageToMobile(chatId, path,
            english ? QStringLiteral("📷 Webcam capture")
                    : QStringLiteral("📷 Снимок с веб-камеры"));

        qDebug() << "[CommandDispatcher] Webcam sent to chat" << chatId;
    });

    *errConn = connect(m_camera, &CameraAgent::captureError, this,
                       [this, chatId, english, conn, errConn](const QString& msg) {
        disconnect(*conn);
        disconnect(*errConn);
        m_gateway->sendOutboundMessage(chatId,
            (english ? QStringLiteral("⚠ Webcam error: ") : QStringLiteral("⚠ Ошибка камеры: ")) + msg);
    });

    m_camera->captureWebcam();
    return r;
}

// ============================================================
//  /video — record a webcam clip WITH microphone audio and send it
//  (unlike /surveillance and the silent security motion clips, this
//  captures a real audio track via Qt Multimedia/QMediaRecorder).
// ============================================================

DispatchResult CommandDispatcherTg::cmdVideo(qint64 chatId, const QString& args,
                                              bool english)
{
    DispatchResult r;
    r.handled = true;

    if (!m_camera->hasWebcam()) {
        r.response = english
            ? QStringLiteral("📷 No webcam detected on this PC.")
            : QStringLiteral("📷 Веб-камера не обнаружена.");
        return r;
    }

    if (m_camera->isRecordingClip()) {
        r.response = english
            ? QStringLiteral("🎥 Already recording a clip — wait for it to finish.")
            : QStringLiteral("🎥 Уже идёт запись — дождитесь окончания.");
        return r;
    }

    int durationSec = 10;
    static const QRegularExpression numRx(QStringLiteral("(\\d+)"));
    auto m = numRx.match(args);
    if (m.hasMatch()) durationSec = qBound(3, m.captured(1).toInt(), 30);

    const QString dir = J2JTelegramGateway::workspaceOutputDir();
    const QString ts  = QDateTime::currentDateTime()
        .toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString path = dir + QStringLiteral("/video_%1.mp4").arg(ts);

    auto conn    = std::make_shared<QMetaObject::Connection>();
    auto errConn = std::make_shared<QMetaObject::Connection>();

    *conn = connect(m_camera, &CameraAgent::clipReady, this,
                    [this, chatId, english, conn, errConn](const QString& clipPath) {
        disconnect(*conn);
        disconnect(*errConn);
        m_gateway->sendVideoToMobile(chatId, clipPath,
            english ? QStringLiteral("🎥 Video clip (with audio)")
                    : QStringLiteral("🎥 Видеозапись (со звуком)"));
        qDebug() << "[CommandDispatcher] Audio+video clip sent to chat" << chatId;
    });
    *errConn = connect(m_camera, &CameraAgent::clipError, this,
                       [this, chatId, english, conn, errConn](const QString& msg) {
        disconnect(*conn);
        disconnect(*errConn);
        m_gateway->sendOutboundMessage(chatId,
            (english ? QStringLiteral("⚠ Recording error: ")
                     : QStringLiteral("⚠ Ошибка записи: ")) + msg);
    });

    m_camera->recordClip(path, durationSec);

    r.response = english
        ? QStringLiteral("🎥 Recording %1s of video with audio...").arg(durationSec)
        : QStringLiteral("🎥 Записываю %1с видео со звуком...").arg(durationSec);
    return r;
}

// ============================================================
//  /surveillance — periodic webcam+desktop streaming
// ============================================================

DispatchResult CommandDispatcherTg::cmdSurveillance(qint64 chatId,
                                                     const QString& args,
                                                     bool english)
{
    DispatchResult r;
    r.handled = true;

    const QString lo = args.toLower().trimmed();

    // /surveillance stop
    if (lo.contains(QStringLiteral("stop")) || lo.contains(QStringLiteral("стоп"))
        || lo.contains(QStringLiteral("off")) || lo.contains(QStringLiteral("выкл"))) {
        m_camera->stopSurveillance();
        r.response = english
            ? QStringLiteral("🛑 Surveillance stopped.")
            : QStringLiteral("🛑 Наблюдение остановлено.");
        return r;
    }

    // Parse interval (default 30s)
    int interval = 30;
    static const QRegularExpression numRx(QStringLiteral("(\\d+)"));
    auto m = numRx.match(args);
    if (m.hasMatch()) interval = qBound(5, m.captured(1).toInt(), 300);

    bool webcam  = !lo.contains(QStringLiteral("desktop only"));
    bool desktop = !lo.contains(QStringLiteral("cam only"));

    // Connect surveillance frames to Telegram delivery
    connect(m_camera, &CameraAgent::surveillanceFrame, this,
            [this, chatId](const QImage& img, const QString& source) {
        const QString dir = J2JTelegramGateway::workspaceOutputDir();
        const QString ts = QDateTime::currentDateTime()
            .toString(QStringLiteral("yyyyMMdd_HHmmss"));
        const QString path = dir + QStringLiteral("/surv_%1_%2.jpg")
            .arg(source, ts);
        img.save(path, "JPEG", 75);

        const QString caption = (source == QStringLiteral("webcam"))
            ? QStringLiteral("👁 Webcam") : QStringLiteral("🖥 Desktop");
        m_gateway->sendImageToMobile(chatId, path, caption);
    }, Qt::UniqueConnection);

    m_camera->startSurveillance(interval, webcam, desktop);

    r.response = english
        ? QStringLiteral("👁 Surveillance active. Interval: *%1s*\n"
                          "Webcam: %2 | Desktop: %3\n"
                          "Use `/surveillance stop` to end.")
            .arg(interval)
            .arg(webcam  ? QStringLiteral("ON") : QStringLiteral("OFF"))
            .arg(desktop ? QStringLiteral("ON") : QStringLiteral("OFF"))
        : QStringLiteral("👁 Наблюдение запущено. Интервал: *%1с*\n"
                          "Камера: %2 | Рабочий стол: %3\n"
                          "Для остановки: `/surveillance stop`")
            .arg(interval)
            .arg(webcam  ? QStringLiteral("ВКЛ") : QStringLiteral("ВЫКЛ"))
            .arg(desktop ? QStringLiteral("ВКЛ") : QStringLiteral("ВЫКЛ"));

    qDebug() << "[CommandDispatcher] Surveillance started for chat" << chatId
             << "interval:" << interval << "webcam:" << webcam << "desktop:" << desktop;
    return r;
}

// ============================================================
//  /security — start/stop security monitoring
// ============================================================

DispatchResult CommandDispatcherTg::cmdSecurity(qint64 chatId,
                                                 const QString& args,
                                                 bool english)
{
    DispatchResult r;
    r.handled = true;

    const QString lo = args.toLower().trimmed();

    if (lo.contains(QStringLiteral("stop")) || lo.contains(QStringLiteral("off"))
        || lo.contains(QStringLiteral("стоп")) || lo.contains(QStringLiteral("выкл"))) {
        m_security->stopMonitoring();
        r.response = english
            ? QStringLiteral("🛡 Security monitoring stopped.")
            : QStringLiteral("🛡 Охрана отключена.");
        return r;
    }

    if (lo.contains(QStringLiteral("status")) || lo.contains(QStringLiteral("статус"))) {
        r.response = english
            ? QStringLiteral("🛡 *Security Status*\n"
                              "Monitoring: %1\n"
                              "Owner enrolled: %2\n"
                              "Auto-lock on threat: ON")
                .arg(m_security->isMonitoring() ? QStringLiteral("ACTIVE") : QStringLiteral("OFF"))
                .arg(m_security->isOwnerEnrolled() ? QStringLiteral("YES") : QStringLiteral("NO"))
            : QStringLiteral("🛡 *Статус безопасности*\n"
                              "Мониторинг: %1\n"
                              "Владелец распознан: %2\n"
                              "Авто-блокировка: ВКЛ")
                .arg(m_security->isMonitoring() ? QStringLiteral("АКТИВЕН") : QStringLiteral("ВЫКЛ"))
                .arg(m_security->isOwnerEnrolled() ? QStringLiteral("ДА") : QStringLiteral("НЕТ"));
        return r;
    }

    // Parse interval
    int interval = 5;
    static const QRegularExpression numRx(QStringLiteral("(\\d+)"));
    auto m = numRx.match(args);
    if (m.hasMatch()) interval = qBound(3, m.captured(1).toInt(), 60);

    // Connect security alerts to Telegram
    connect(m_security, &SecurityCamera::unknownFaceDetected, this,
            [this, chatId](const QImage& frame, int count) {
        const QString dir = J2JTelegramGateway::workspaceOutputDir();
        const QString path = dir + QStringLiteral("/alert_unknown_%1.jpg")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        frame.save(path, "JPEG", 90);
        m_gateway->sendImageToMobile(chatId, path,
            QStringLiteral("🚨 UNKNOWN FACE (%1) — screen locked!").arg(count));
    }, Qt::UniqueConnection);

    // Soft alert: owner present but someone else is looking.
    // Send photo + inline buttons (no commands to memorize).
    connect(m_security, &SecurityCamera::companionNotice, this,
            [this, chatId](const QImage& frame, int totalFaces) {
        const QString dir = J2JTelegramGateway::workspaceOutputDir();
        const QString path = dir + QStringLiteral("/companion_%1.jpg")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        frame.save(path, "JPEG", 85);

        const int others = totalFaces - 1;
        m_gateway->sendImageToMobile(chatId, path,
            QStringLiteral("👀 %1 person(s) near your screen").arg(others));

        // Send inline keyboard: OK / Lock
        QJsonArray row;
        row.append(QJsonObject{
            {QStringLiteral("text"), QStringLiteral("✅ Всё ОК")},
            {QStringLiteral("callback_data"), QStringLiteral("sec_companion_ok")}
        });
        row.append(QJsonObject{
            {QStringLiteral("text"), QStringLiteral("🔒 Заблокировать!")},
            {QStringLiteral("callback_data"), QStringLiteral("sec_companion_lock")}
        });
        QJsonArray rows;
        rows.append(row);
        QJsonObject markup;
        markup[QStringLiteral("inline_keyboard")] = rows;

        m_gateway->sendOutboundWithButtons(chatId,
            QStringLiteral("Это нормально?"), markup);

        // Set flag so natural language "да"/"ок"/"нет" works as answer
        m_gateway->markAwaitingCompanionAnswer(chatId);
    }, Qt::UniqueConnection);

    // Owner absent auto-lock notification
    connect(m_security, &SecurityCamera::ownerAbsent, this,
            [this, chatId](const QImage& frame) {
        const QString dir = J2JTelegramGateway::workspaceOutputDir();
        const QString path = dir + QStringLiteral("/absent_%1.jpg")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        frame.save(path, "JPEG", 75);
        m_gateway->sendImageToMobile(chatId, path,
            QStringLiteral("🔒 You left — screen auto-locked"));
    }, Qt::UniqueConnection);

    connect(m_security, &SecurityCamera::motionDetected, this,
            [this, chatId](const QImage& frame) {
        const QString dir = J2JTelegramGateway::workspaceOutputDir();
        const QString path = dir + QStringLiteral("/alert_motion_%1.jpg")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        frame.save(path, "JPEG", 75);
        m_gateway->sendImageToMobile(chatId, path,
            QStringLiteral("🔔 Motion detected"));
    }, Qt::UniqueConnection);

    connect(m_security, &SecurityCamera::alertMessage, this,
            [this, chatId](const QString& text) {
        m_gateway->sendOutboundMessage(chatId, text);
    }, Qt::UniqueConnection);

    m_security->startMonitoring(interval);

    r.response = english
        ? QStringLiteral("🛡 *Security armed.* Check every %1s.\n"
                          "Owner enrolled: %2\n"
                          "• Unknown face → photo + lock\n"
                          "• Shoulder surfing → photo + lock\n"
                          "• Motion → photo alert\n\n"
                          "Use `/security stop` to disarm.\n"
                          "Use `/enroll_face` to train owner recognition.")
            .arg(interval)
            .arg(m_security->isOwnerEnrolled() ? QStringLiteral("YES") : QStringLiteral("NO — use /enroll_face"))
        : QStringLiteral("🛡 *Охрана включена.* Проверка каждые %1с.\n"
                          "Владелец обучен: %2\n"
                          "• Чужое лицо → фото + блокировка\n"
                          "• Подглядывание → фото + блокировка\n"
                          "• Движение → фото-алерт\n\n"
                          "Отключить: `/security stop`\n"
                          "Обучить лицо: `/enroll_face`")
            .arg(interval)
            .arg(m_security->isOwnerEnrolled() ? QStringLiteral("ДА") : QStringLiteral("НЕТ — используйте /enroll_face"));

    return r;
}

// ============================================================
//  /enroll_face — train owner face recognition
// ============================================================

DispatchResult CommandDispatcherTg::cmdEnrollFace(qint64 chatId, bool english)
{
    DispatchResult r;
    r.handled = true;

    r.response = english
        ? QStringLiteral("📸 Starting face enrollment. Look at the webcam...\n"
                          "Capturing 10 samples (hold still, vary angle slightly).")
        : QStringLiteral("📸 Начинаю обучение лица. Смотрите в камеру...\n"
                          "Захватываю 10 образцов (не двигайтесь, слегка меняйте угол).");

    connect(m_security, &SecurityCamera::enrollmentProgress, this,
            [this, chatId, english](int current, int total) {
        m_gateway->sendOutboundMessage(chatId,
            (english ? QStringLiteral("📸 Sample %1/%2") : QStringLiteral("📸 Образец %1/%2"))
                .arg(current).arg(total));
    }, Qt::UniqueConnection);

    connect(m_security, &SecurityCamera::enrollmentComplete, this,
            [this, chatId, english](int samples) {
        m_gateway->sendOutboundMessage(chatId,
            english ? QStringLiteral("✅ Face enrolled! %1 samples captured.\n"
                                      "Security can now distinguish you from strangers.").arg(samples)
                    : QStringLiteral("✅ Лицо обучено! %1 образцов.\n"
                                      "Теперь охрана отличает вас от посторонних.").arg(samples));
    }, Qt::UniqueConnection);

    // Run enrollment async
    QTimer::singleShot(500, m_security, [this]() {
        m_security->enrollOwnerFace(10);
    });

    return r;
}
