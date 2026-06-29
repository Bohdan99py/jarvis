#pragma once
// ============================================================
// security_camera.h — Smart Security Camera with Power Mgmt
//
// Three-tier wake system (camera is OFF by default):
//
//   Tier 1 — OS Triggers (zero CPU cost):
//     Mouse/keyboard activity after idle → wake camera
//
//   Tier 2 — Low-Power Sentinel (1 FPS, 320x240, ~0.5% CPU):
//     Background subtraction detects >5% pixel change
//     → wakes full-res face detection
//
//   Tier 3 — Full Alert (30 FPS, native res):
//     Haar cascade face detection
//     LBPH owner recognition
//     Shoulder surfing (multi-face)
//     Auto-lock + Telegram photo alert
//
// After 10 seconds with no motion/faces → drops back to Tier 2.
// After 60 seconds idle → drops to Tier 1 (camera off).
//
// Compile guard: JARVIS_HAS_OPENCV
// ============================================================

#include <QObject>
#include <QImage>
#include <QTimer>
#include <QString>
#include <QMutex>
#include <QElapsedTimer>

class SecurityCamera : public QObject
{
    Q_OBJECT

public:
    explicit SecurityCamera(QObject* parent = nullptr);
    ~SecurityCamera() override;

    // ── Enrollment ──────────────────────────────────────────
    void enrollOwnerFace(int sampleCount = 10);
    bool isOwnerEnrolled() const;

    // ── Monitoring lifecycle ────────────────────────────────
    void startMonitoring(int sentinelIntervalSec = 2);
    void stopMonitoring();
    bool isMonitoring() const;
    void checkNow();

    // ── Configuration ───────────────────────────────────────
    void setAlertOnUnknownFace(bool v) { m_alertUnknown = v; }
    void setAlertOnMotion(bool v)      { m_alertMotion = v; }
    void setAlertOnShoulderSurf(bool v){ m_alertShoulder = v; }
    void setAutoLockOnThreat(bool v)   { m_autoLock = v; }

    // User confirmed via Telegram that the extra person is OK —
    // suppress shoulder-surfing alerts for a cooldown period.
    void confirmCompanionOk(int cooldownMinutes = 30);
    void lockScreen();

    // ── Power state ─────────────────────────────────────────
    enum PowerState { Off, Sentinel, FullAlert };
    PowerState powerState() const { return m_powerState; }

signals:
    void unknownFaceDetected(const QImage& frame, int faceCount);
    void motionDetected(const QImage& frame);
    void ownerRecognized(const QImage& frame);
    void alertMessage(const QString& text);

    // Soft alert: owner is present but someone else is looking.
    void companionNotice(const QImage& frame, int totalFaces);

    // Owner left the PC — screen auto-locked after ~30s absence
    void ownerAbsent(const QImage& lastFrame);
    void powerStateChanged(int newState);

    void enrollmentProgress(int current, int total);
    void enrollmentComplete(int samplesCollected);

private slots:
    void onSentinelTick();
    void onInputActivity();

private:
    void escalateToFullAlert();
    void deescalateToSentinel();
    void deescalateToOff();

    QImage captureFrameLowRes();
    QImage captureFrameFullRes();
    int    detectFaces(const QImage& frame);
    bool   isOwner(const QImage& frame);
    bool   detectMotion(const QImage& frame);
    void   installInputHook();
    void   removeInputHook();

    QTimer*        m_sentinelTimer = nullptr;
    QTimer*        m_deescalateTimer = nullptr;
    QElapsedTimer  m_lastActivityTime;

    PowerState m_powerState  = Off;
    bool m_alertUnknown      = true;
    bool m_alertMotion       = true;
    bool m_alertShoulder     = true;
    bool m_autoLock          = true;
    bool m_ownerEnrolled     = false;
    bool m_monitoring        = false;
    bool m_inputHookInstalled = false;

    QImage m_prevFrame;
    QElapsedTimer  m_companionCooldown;
    bool           m_companionSuppressed = false;
    int            m_ownerAbsentTicks = 0;

    static constexpr int OWNER_ABSENT_LOCK_TICKS = 6; // ~30s at 5s interval

    static QString ownerModelPath();
    static QString cascadePath();

    mutable QMutex m_mutex;

    static constexpr int SENTINEL_RES_W       = 320;
    static constexpr int SENTINEL_RES_H       = 240;
    static constexpr int FULL_ALERT_TIMEOUT_MS = 10000;
    static constexpr int CAMERA_OFF_TIMEOUT_MS = 60000;
    static constexpr double MOTION_THRESHOLD   = 5.0;
};
