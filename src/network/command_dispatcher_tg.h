#pragma once
// ============================================================
// command_dispatcher_tg.h — Sandboxed Telegram Command Executor
//
// Maps Telegram slash commands to safe local actions:
//   • /screenshot      — capture + send back (Admin only)
//   • /sysinfo         — CPU, RAM, uptime report (Admin only)
//   • /tasks           — Kanban board summary (Tester+)
//   • /note <text>     — save a personal note (User+)
//   • /notes           — list recent notes (User+)
//   • /uptime          — how long JARVIS has been running (User+)
//   • /disk            — disk usage summary (Admin only)
//   • /screen_analyze  — capture + AI visual analysis (User+)
//   • /stop_voice      — flush TTS playback queue (User+)
//   • /cache_stats     — LLM cache statistics (User+)
//
// Security: every command is gated by TelegramAccessManager
// before reaching this class. Raw OS shell execution is
// intentionally not exposed — all actions are implemented
// as structured Qt API calls.
//
// Thread safety: read-only system queries are lock-free;
// DB writes go through DatabaseManager's mutex.
// ============================================================

#include <QObject>
#include <QString>
#include <QDateTime>

class TelegramAccessManager;
class J2JTelegramGateway;
class GourmetModule;
class MediaAnalyzerModule;
class CameraAgent;
class SecurityCamera;

struct DispatchResult {
    bool    handled  = false;
    QString response;
    QString imagePath;   // non-empty → send as photo after text
};

class CommandDispatcherTg : public QObject
{
    Q_OBJECT

public:
    explicit CommandDispatcherTg(J2JTelegramGateway* gateway,
                                 TelegramAccessManager* accessMgr,
                                 QObject* parent = nullptr);

    // Returns true if the command was handled by the dispatcher.
    // The gateway should not pass it to the LLM if true.
    DispatchResult dispatch(qint64 chatId, const QString& command, bool english);

    // Register new commands into the ACL at startup
    static void registerCommands();

private:
    // Individual command handlers
    DispatchResult cmdScreenshot(qint64 chatId, bool english);
    DispatchResult cmdSysInfo(bool english);
    DispatchResult cmdDisk(bool english);
    DispatchResult cmdUptime(bool english);
    DispatchResult cmdTasks(qint64 chatId, bool english);
    DispatchResult cmdNote(qint64 chatId, const QString& text, bool english);
    DispatchResult cmdNotes(qint64 chatId, bool english);
    DispatchResult cmdScreenAnalyze(qint64 chatId, bool english);
    DispatchResult cmdStopVoice(bool english);
    DispatchResult cmdCacheStats(bool english);
    DispatchResult cmdFridge(qint64 chatId, const QString& args, bool english);
    DispatchResult cmdSummarize(qint64 chatId, const QString& args, bool english);
    DispatchResult cmdRemind(qint64 chatId, const QString& args, bool english);
    DispatchResult cmdWebcam(qint64 chatId, bool english);
    DispatchResult cmdSurveillance(qint64 chatId, const QString& args, bool english);
    DispatchResult cmdSecurity(qint64 chatId, const QString& args, bool english);
    DispatchResult cmdEnrollFace(qint64 chatId, bool english);

    J2JTelegramGateway*    m_gateway       = nullptr;
    TelegramAccessManager* m_accessMgr     = nullptr;
    GourmetModule*         m_gourmet       = nullptr;
    MediaAnalyzerModule*   m_mediaAnalyzer = nullptr;
    CameraAgent*           m_camera        = nullptr;
    SecurityCamera*        m_security      = nullptr;
    QDateTime              m_startTime;
};
