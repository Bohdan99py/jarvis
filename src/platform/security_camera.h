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
    // Снимок для предпросмотра. Если устройство прямо сейчас читает
    // сторожевой цикл, свежий кадр взять неоткуда — отдаём последний
    // удачный вместо пустого. Пустой кадр окно показывало как "камера
    // занята другим приложением", хотя занимали её мы сами.
    QImage snapshotFullRes()
    {
        const QImage fresh = captureFrameFullRes();
        if (!fresh.isNull())
            return fresh;
        return lastFrame();
    }

    // Последний удачно прочитанный кадр (может быть пустым, если камеру
    // ещё ни разу не открывали).
    QImage lastFrame() const;

    // ── Monitoring lifecycle ────────────────────────────────
    void startMonitoring(int sentinelIntervalSec = 60);
    void stopMonitoring();
    bool isMonitoring() const;
    void checkNow();

    // ── Configuration ───────────────────────────────────────
    void setAlertOnUnknownFace(bool v) { m_alertUnknown = v; }
    void setAlertOnMotion(bool v)      { m_alertMotion = v; }
    void setAutoLockOnThreat(bool v)   { m_autoLock = v; }
    // setAutoUnlock/autoUnlock убраны вместе с оверлеем: разблокировать
    // сеанс Windows программно нельзя. Узнавание лица теперь дело
    // Windows Hello, а не JARVIS.

    // Геттеры к тем же флагам. Нужны, чтобы пункт меню спрашивал
    // состояние у камеры, а не хранил своё: раньше галочка жила только
    // в QAction, и любой другой способ поменять режим (Telegram,
    // профиль) с ней расходился.
    bool alertOnUnknownFace() const { return m_alertUnknown; }
    bool alertOnMotion() const      { return m_alertMotion; }
    bool autoLockOnThreat() const   { return m_autoLock; }

    // User confirmed via Telegram that the extra person is OK —
    // suppress shoulder-surfing alerts for a cooldown period.
    void confirmCompanionOk(int cooldownMinutes = 30);

    // Запирает сеанс средствами Windows (LockWorkStation).
    //
    // Раньше здесь показывалось собственное полноэкранное окно. Оно не
    // было блокировкой и быть ей не могло: меню «Пуск», панель задач и
    // Ctrl+Alt+Del в Windows находятся выше любого окна приложения —
    // именно для того, чтобы программа не могла запереть чужую машину.
    // Окно рисовалось поверх обоев, а «Пуск» открывался поверх него и
    // позволял просто снять JARVIS.
    //
    // Разблокировки здесь нет и быть не может: снять блокировку сеанса
    // программе не даёт та же защита. Возвращает человека за машину
    // Windows — паролем, PIN или Hello.
    void lockScreen();

    // ── Power state ─────────────────────────────────────────
    enum PowerState { Off, Sentinel, FullAlert };
    PowerState powerState() const { return m_powerState; }

    // ── Lock state ──────────────────────────────────────────
    // Спрашивается у Windows, а не хранится флагом.
    //
    // Хранимый m_screenLocked и был источником всех поломок блокировки:
    // его выставляли в одном месте, снимали в трёх, и любой путь,
    // забывший его снять, оставлял камеру уверенной, что экран заперт —
    // после чего «Заблокировать» больше не включался, а проверка лица
    // тикала до перезапуска. Производное состояние разойтись не может.
    bool isScreenLocked() const;

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

    // Сеанс заперт средствами Windows. Не «покажи оверлей», а
    // уведомление постфактум: показывать UI больше нечего, но записать
    // в ленту и сказать в Telegram — есть что.
    void screenLocked();

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
    bool m_autoLock          = true;
    bool m_ownerEnrolled     = false;
    bool m_monitoring        = false;
    bool m_inputHookInstalled = false;
    std::atomic<bool> m_recording{false};
    std::atomic<bool> m_stopping{false};

    QImage m_prevFrame;
    QElapsedTimer  m_companionCooldown;
    bool           m_companionSuppressed = false;
    int            m_ownerAbsentTicks = 0;

    // True while a clip is recording and for a cooldown window after it
    // finishes — prevents onSentinelTick/onLockCheckTick's independent
    // motion checks from re-triggering recordMotionClip() again seconds
    // after the first clip, which previously produced a second, mostly
    // motion-less video for the same real-world event.
    bool           m_clipCooldownActive = false;
    static constexpr int CLIP_COOLDOWN_SEC = 60;

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

    // Кэш последнего кадра — общий для предпросмотра и сторожа
    QImage cacheFrame(const QImage& frame);

    mutable QMutex m_frameMutex;
    QImage         m_lastFrame;

    static constexpr int SENTINEL_RES_W       = 320;
    static constexpr int SENTINEL_RES_H       = 240;
    static constexpr int FULL_ALERT_TIMEOUT_MS = 10000;
    static constexpr int CAMERA_OFF_TIMEOUT_MS = 300000; // 5 min
    static constexpr double MOTION_THRESHOLD   = 5.0;
    static constexpr int MOTION_CLIP_DURATION_SEC = 20;
};
