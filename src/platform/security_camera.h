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
//     Histogram-based owner recognition
//     Shoulder surfing (multi-face)
//     Auto-lock + Telegram video alert
//
// After 10 seconds with no motion/faces → drops back to Tier 2.
// After 5 minutes idle → drops to Tier 1 (camera off).
//
// Compile guard: JARVIS_HAS_OPENCV
// ============================================================

#include <QObject>
#include <QImage>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QMutex>
#include <QRect>
#include <QList>
#include <QElapsedTimer>
#include <atomic>

// Результат идентификации одного лица в кадре
struct FaceObservation {
    QRect   rect;              // где лицо в кадре
    QString name;              // "" = неизвестный
    int     age = 0;           // 0 = неизвестен
    QString status;            // "владелец", роль и т.п.
    double  confidence = 0.0;  // LBP-скор совпадения
    bool    known = false;

    QString label() const;     // "Имя, возраст, статус" / "Неизвестный"
};

class SecurityCamera : public QObject
{
    Q_OBJECT

public:
    explicit SecurityCamera(QObject* parent = nullptr);
    ~SecurityCamera() override;

    // ── Enrollment ──────────────────────────────────────────
    void enrollOwnerFace(int sampleCount = 10);
    bool isOwnerEnrolled() const;

    // Обучение по загруженным фото (а не с живой камеры) — для владельца
    // ИЛИ любого другого человека (родные, друзья). Ищет по одному лицу
    // на фото (самое крупное, если их несколько), сохраняет в FaceRegistry.
    // Возвращает число фото, из которых удалось извлечь лицо.
    // isOwner=true дополнительно помечает человека владельцем этого ПК
    // (влияет на авто-блокировку экрана при его отсутствии).
    int enrollFaceFromImages(const QStringList& imagePaths, const QString& name,
                             int age, const QString& status, bool isOwner = false);

    // Идентичность владельца для FaceRegistry: при обучении лицо
    // сохраняется с этими данными и уходит по мешу другим узлам.
    void setOwnerIdentity(const QString& name, int age, const QString& status);

    // ── Face identification (кто перед камерой) ─────────────
    // Захватывает кадр, находит лица, сверяет с FaceRegistry
    // (локальные + принятые по P2P) и возвращает наблюдения.
    QList<FaceObservation> identifyFaces(const QImage& frame);

    // Кадр с зелёными/красными рамками и подписью "Имя, возраст, статус"
    static QImage annotateFaces(const QImage& frame,
                                const QList<FaceObservation>& faces);

    // Разовый снимок с камеры в полном разрешении (для Live View)
    QImage snapshotFullRes() { return captureFrameFullRes(); }

    // ── Monitoring lifecycle ────────────────────────────────
    void startMonitoring(int sentinelIntervalSec = 60);
    void stopMonitoring();
    bool isMonitoring() const;
    void checkNow();

    // ── Configuration ───────────────────────────────────────
    void setAlertOnUnknownFace(bool v) { m_alertUnknown = v; }
    void setAlertOnMotion(bool v)      { m_alertMotion = v; }
    void setAlertOnShoulderSurf(bool v){ m_alertShoulder = v; }
    void setAutoLockOnThreat(bool v)   { m_autoLock = v; }
    void setAutoUnlock(bool v)         { m_autoUnlock = v; }

    // User confirmed via Telegram that the extra person is OK —
    // suppress shoulder-surfing alerts for a cooldown period.
    void confirmCompanionOk(int cooldownMinutes = 30);
    void lockScreen();

    // ── Power state ─────────────────────────────────────────
    enum PowerState { Off, Sentinel, FullAlert };
    PowerState powerState() const { return m_powerState; }

    // ── Lock state ──────────────────────────────────────────
    bool isScreenLocked() const { return m_screenLocked; }

signals:
    void unknownFaceDetected(const QImage& frame, int faceCount);
    void motionDetected(const QImage& frame);
    void ownerRecognized(const QImage& frame);
    void alertMessage(const QString& text);

    // Идентифицированные лица + аннотированный кадр (рамка/имя/статус)
    void facesIdentified(const QImage& annotatedFrame,
                         const QList<FaceObservation>& faces);

    // Soft alert: owner is present but someone else is looking.
    void companionNotice(const QImage& frame, int totalFaces);

    // Owner left the PC — screen auto-locked
    void ownerAbsent(const QImage& lastFrame);
    void powerStateChanged(int newState);

    void enrollmentProgress(int current, int total);
    void enrollmentComplete(int samplesCollected);

    // Lock screen overlay should be shown/hidden by the UI layer
    void requestLockOverlay();
    void requestUnlockOverlay();

    // 20s motion-triggered video clip ready for Telegram
    void motionVideoReady(const QString& videoPath);

private slots:
    void onSentinelTick();
    void onInputActivity();
    void onLockCheckTick();

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

    void   recordMotionClip();

    QTimer*        m_sentinelTimer = nullptr;
    QTimer*        m_deescalateTimer = nullptr;
    QTimer*        m_lockCheckTimer = nullptr;
    QElapsedTimer  m_lastActivityTime;

    PowerState m_powerState  = Off;
    bool m_alertUnknown      = true;
    bool m_alertMotion       = true;
    bool m_alertShoulder     = true;
    bool m_autoLock          = true;
    bool m_autoUnlock        = true;
    bool m_ownerEnrolled     = false;
    bool m_monitoring        = false;
    bool m_inputHookInstalled = false;
    bool m_screenLocked      = false;
    std::atomic<bool> m_recording{false};
    std::atomic<bool> m_stopping{false};

    QImage m_prevFrame;
    QElapsedTimer  m_companionCooldown;
    bool           m_companionSuppressed = false;
    int            m_ownerAbsentTicks = 0;

    // Идентичность владельца (для FaceRegistry при обучении)
    QString m_ownerName;
    int     m_ownerAge = 0;
    QString m_ownerStatus;

    static constexpr int OWNER_ABSENT_LOCK_TICKS = 3; // ~15s at 5s sentinel checks
    static constexpr int LOCK_CHECK_INTERVAL_MS  = 3000; // 3s face scan while locked

    static QString ownerSamplesDir();
    static QString opencvDllPath();

public:
    static bool isOpenCvAvailable();
    static QString cascadePath();

private:
    mutable QMutex m_mutex;

    static constexpr int SENTINEL_RES_W       = 320;
    static constexpr int SENTINEL_RES_H       = 240;
    static constexpr int FULL_ALERT_TIMEOUT_MS = 10000;
    static constexpr int CAMERA_OFF_TIMEOUT_MS = 300000; // 5 min
    static constexpr double MOTION_THRESHOLD   = 5.0;
    static constexpr int MOTION_CLIP_DURATION_SEC = 20;
};
