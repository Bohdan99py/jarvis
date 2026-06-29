// ============================================================
// security_camera.cpp — Smart Security Camera
// ============================================================

#include "security_camera.h"
#include "jarvis_paths.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QScreen>
#include <QApplication>
#include <QCoreApplication>
#include <QProcess>
#include <QDebug>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifdef JARVIS_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/face.hpp>

static cv::Mat qimageToMat(const QImage& img)
{
    QImage rgb = img.convertToFormat(QImage::Format_RGB888);
    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC3,
                const_cast<uchar*>(rgb.constBits()),
                static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat result;
    mat.copyTo(result);
    cv::cvtColor(result, result, cv::COLOR_RGB2BGR);
    return result;
}

static QImage matToQImage(const cv::Mat& mat)
{
    cv::Mat rgb;
    cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows,
                  static_cast<int>(rgb.step),
                  QImage::Format_RGB888).copy();
}

static cv::CascadeClassifier& faceCascade()
{
    static cv::CascadeClassifier cascade;
    static bool loaded = false;
    if (!loaded) {
        const QString path = SecurityCamera::cascadePath();
        if (!path.isEmpty()) cascade.load(path.toStdString());
        loaded = true;
    }
    return cascade;
}

static cv::VideoCapture& cam()
{
    static cv::VideoCapture cap;
    return cap;
}

static void openCam()  { auto& c = cam(); if (!c.isOpened()) c.open(0); }
static void closeCam() { auto& c = cam(); if (c.isOpened()) c.release(); }
#endif

// ============================================================
//  Paths
// ============================================================

QString SecurityCamera::ownerModelPath()
{
    return JarvisPaths::subPath(QStringLiteral("security/owner_face.yml"));
}

QString SecurityCamera::cascadePath()
{
    static const QStringList paths = {
        JarvisPaths::subPath(QStringLiteral("security/haarcascade_frontalface_default.xml")),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../redist/opencv/build/etc/haarcascades/haarcascade_frontalface_default.xml"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../redist/opencv/etc/haarcascades/haarcascade_frontalface_default.xml"),
        QStringLiteral("C:/opencv/etc/haarcascades/haarcascade_frontalface_default.xml"),
        QStringLiteral("C:/tools/opencv/etc/haarcascades/haarcascade_frontalface_default.xml"),
    };
    QString envPath = qEnvironmentVariable("OPENCV_DIR");
    if (!envPath.isEmpty()) {
        QString p = envPath + QStringLiteral("/etc/haarcascades/haarcascade_frontalface_default.xml");
        if (QFileInfo::exists(p)) return p;
    }
    for (const auto& p : paths)
        if (QFileInfo::exists(p)) return p;
    return {};
}

// ============================================================
//  Construction
// ============================================================

SecurityCamera::SecurityCamera(QObject* parent)
    : QObject(parent)
{
    m_sentinelTimer = new QTimer(this);
    connect(m_sentinelTimer, &QTimer::timeout,
            this, &SecurityCamera::onSentinelTick);

    m_deescalateTimer = new QTimer(this);
    m_deescalateTimer->setSingleShot(true);
    connect(m_deescalateTimer, &QTimer::timeout, this, [this]() {
        if (m_powerState == FullAlert)
            deescalateToSentinel();
    });

    m_lastActivityTime.start();

#ifdef JARVIS_HAS_OPENCV
    m_ownerEnrolled = QFileInfo::exists(ownerModelPath());

    // Auto-download Haar cascade if missing
    const QString cascade = cascadePath();
    if (cascade.isEmpty()) {
        const QString destPath = JarvisPaths::subPath(
            QStringLiteral("security/haarcascade_frontalface_default.xml"));
        if (!QFileInfo::exists(destPath)) {
            qDebug() << "[SecurityCam] Downloading face cascade...";
            QDir().mkpath(QFileInfo(destPath).absolutePath());
            QProcess wget;
            wget.start(QStringLiteral("powershell.exe"), {
                QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
                QStringLiteral("Invoke-WebRequest -Uri "
                    "'https://raw.githubusercontent.com/opencv/opencv/4.x/data/"
                    "haarcascades/haarcascade_frontalface_default.xml' "
                    "-OutFile '%1'").arg(destPath)
            });
            if (wget.waitForFinished(30000) && wget.exitCode() == 0)
                qDebug() << "[SecurityCam] Cascade downloaded:" << destPath;
            else
                qWarning() << "[SecurityCam] Cascade download failed";
        }
    }
#endif

    qDebug() << "[SecurityCam] Init. Owner enrolled:" << m_ownerEnrolled;
}

SecurityCamera::~SecurityCamera()
{
    stopMonitoring();
#ifdef JARVIS_HAS_OPENCV
    closeCam();
#endif
}

// ============================================================
//  Tier 1: OS Input Hook (mouse/keyboard activity)
// ============================================================

static SecurityCamera* g_securityInstance = nullptr;
static HHOOK g_mouseHook = nullptr;
static HHOOK g_kbHook = nullptr;

static LRESULT CALLBACK lowLevelInputProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    Q_UNUSED(lParam)
    if (nCode >= 0 && g_securityInstance) {
        QMetaObject::invokeMethod(g_securityInstance,
            "onInputActivity", Qt::QueuedConnection);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void SecurityCamera::installInputHook()
{
    if (m_inputHookInstalled) return;
    g_securityInstance = this;
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, lowLevelInputProc, nullptr, 0);
    g_kbHook    = SetWindowsHookExW(WH_KEYBOARD_LL, lowLevelInputProc, nullptr, 0);
    m_inputHookInstalled = (g_mouseHook != nullptr || g_kbHook != nullptr);
    qDebug() << "[SecurityCam] Input hooks installed:" << m_inputHookInstalled;
}

void SecurityCamera::removeInputHook()
{
    if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
    if (g_kbHook)    { UnhookWindowsHookEx(g_kbHook);    g_kbHook = nullptr; }
    g_securityInstance = nullptr;
    m_inputHookInstalled = false;
}

void SecurityCamera::onInputActivity()
{
    m_lastActivityTime.restart();

    if (m_powerState == Off) {
        // Someone touched the PC while camera was off → full alert!
        escalateToFullAlert();
    }
}

// ============================================================
//  Power state transitions
// ============================================================

void SecurityCamera::escalateToFullAlert()
{
    if (m_powerState == FullAlert) return;

    m_powerState = FullAlert;
#ifdef JARVIS_HAS_OPENCV
    openCam();
#endif

    // Auto-deescalate after 10s of no activity
    m_deescalateTimer->start(FULL_ALERT_TIMEOUT_MS);

    emit powerStateChanged(FullAlert);
    qDebug() << "[SecurityCam] → FULL ALERT";
}

void SecurityCamera::deescalateToSentinel()
{
    if (m_powerState == Sentinel) return;

    m_powerState = Sentinel;
    // Keep camera at low res
#ifdef JARVIS_HAS_OPENCV
    auto& c = cam();
    if (c.isOpened()) {
        c.set(cv::CAP_PROP_FRAME_WIDTH,  SENTINEL_RES_W);
        c.set(cv::CAP_PROP_FRAME_HEIGHT, SENTINEL_RES_H);
    }
#endif

    emit powerStateChanged(Sentinel);
    qDebug() << "[SecurityCam] → SENTINEL (low power)";
}

void SecurityCamera::deescalateToOff()
{
    if (m_powerState == Off) return;

    m_powerState = Off;
#ifdef JARVIS_HAS_OPENCV
    closeCam();
#endif

    emit powerStateChanged(Off);
    qDebug() << "[SecurityCam] → OFF (input hooks only)";
}

// ============================================================
//  Monitoring lifecycle
// ============================================================

void SecurityCamera::startMonitoring(int sentinelIntervalSec)
{
    m_monitoring = true;

    installInputHook();

    // Start in Sentinel mode (low power)
    m_powerState = Sentinel;
#ifdef JARVIS_HAS_OPENCV
    openCam();
    auto& c = cam();
    if (c.isOpened()) {
        c.set(cv::CAP_PROP_FRAME_WIDTH,  SENTINEL_RES_W);
        c.set(cv::CAP_PROP_FRAME_HEIGHT, SENTINEL_RES_H);
    }
#endif

    m_sentinelTimer->setInterval(sentinelIntervalSec * 1000);
    m_sentinelTimer->start();
    m_lastActivityTime.restart();

    emit powerStateChanged(Sentinel);
    emit alertMessage(QStringLiteral("🛡 Security armed (smart power mode)"));

    qDebug() << "[SecurityCam] Monitoring started. Sentinel interval:"
             << sentinelIntervalSec << "s";
}

void SecurityCamera::stopMonitoring()
{
    m_monitoring = false;
    m_sentinelTimer->stop();
    m_deescalateTimer->stop();
    removeInputHook();
    deescalateToOff();
}

bool SecurityCamera::isMonitoring() const { return m_monitoring; }

void SecurityCamera::checkNow() { onSentinelTick(); }

// ============================================================
//  Frame capture
// ============================================================

QImage SecurityCamera::captureFrameLowRes()
{
#ifdef JARVIS_HAS_OPENCV
    auto& c = cam();
    if (!c.isOpened()) return {};
    cv::Mat frame;
    c.read(frame);
    if (frame.empty()) return {};
    // Ensure low res
    if (frame.cols > SENTINEL_RES_W) {
        cv::Mat small;
        cv::resize(frame, small, cv::Size(SENTINEL_RES_W, SENTINEL_RES_H));
        return matToQImage(small);
    }
    return matToQImage(frame);
#else
    return {};
#endif
}

QImage SecurityCamera::captureFrameFullRes()
{
#ifdef JARVIS_HAS_OPENCV
    auto& c = cam();
    if (!c.isOpened()) { openCam(); }
    // Restore full resolution
    c.set(cv::CAP_PROP_FRAME_WIDTH,  1280);
    c.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    cv::Mat frame;
    c.read(frame);
    if (frame.empty()) return {};
    return matToQImage(frame);
#else
    QScreen* screen = QApplication::primaryScreen();
    if (!screen) return {};
    return screen->grabWindow(0).toImage();
#endif
}

// ============================================================
//  Detection algorithms
// ============================================================

int SecurityCamera::detectFaces(const QImage& frame)
{
#ifdef JARVIS_HAS_OPENCV
    auto& cascade = faceCascade();
    if (cascade.empty()) return 0;
    cv::Mat mat = qimageToMat(frame);
    cv::Mat gray;
    cv::cvtColor(mat, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, gray);
    std::vector<cv::Rect> faces;
    cascade.detectMultiScale(gray, faces, 1.1, 4,
                              cv::CASCADE_SCALE_IMAGE, cv::Size(60, 60));
    return static_cast<int>(faces.size());
#else
    Q_UNUSED(frame) return 0;
#endif
}

bool SecurityCamera::isOwner(const QImage& frame)
{
#ifdef JARVIS_HAS_OPENCV
    if (!m_ownerEnrolled) return false;
    auto recognizer = cv::face::LBPHFaceRecognizer::create();
    recognizer->read(ownerModelPath().toStdString());
    cv::Mat mat = qimageToMat(frame);
    cv::Mat gray;
    cv::cvtColor(mat, gray, cv::COLOR_BGR2GRAY);
    auto& cascade = faceCascade();
    if (cascade.empty()) return false;
    std::vector<cv::Rect> faces;
    cascade.detectMultiScale(gray, faces, 1.1, 4,
                              cv::CASCADE_SCALE_IMAGE, cv::Size(60, 60));
    for (const auto& face : faces) {
        cv::Mat roi = gray(face);
        cv::resize(roi, roi, cv::Size(100, 100));
        int label = -1; double conf = 0;
        recognizer->predict(roi, label, conf);
        if (label == 0 && conf < 80.0) return true;
    }
    return false;
#else
    Q_UNUSED(frame) return true;
#endif
}

bool SecurityCamera::detectMotion(const QImage& frame)
{
    if (m_prevFrame.isNull()) { m_prevFrame = frame; return false; }

#ifdef JARVIS_HAS_OPENCV
    cv::Mat curr = qimageToMat(frame);
    cv::Mat prev = qimageToMat(m_prevFrame);
    m_prevFrame = frame;

    cv::Mat gC, gP, diff;
    cv::cvtColor(curr, gC, cv::COLOR_BGR2GRAY);
    cv::cvtColor(prev, gP, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gC, gC, cv::Size(21, 21), 0);
    cv::GaussianBlur(gP, gP, cv::Size(21, 21), 0);
    cv::absdiff(gP, gC, diff);
    cv::threshold(diff, diff, 30, 255, cv::THRESH_BINARY);

    double pct = cv::countNonZero(diff) * 100.0 / (diff.rows * diff.cols);
    return pct > MOTION_THRESHOLD;
#else
    m_prevFrame = frame;
    return false;
#endif
}

void SecurityCamera::lockScreen()
{
    LockWorkStation();
}

// ============================================================
//  Owner enrollment
// ============================================================

void SecurityCamera::enrollOwnerFace(int sampleCount)
{
#ifdef JARVIS_HAS_OPENCV
    auto& cascade = faceCascade();
    if (cascade.empty()) {
        emit alertMessage(QStringLiteral("Cannot enroll: face cascade not found"));
        return;
    }
    openCam();
    auto& c = cam();
    if (!c.isOpened()) {
        emit alertMessage(QStringLiteral("Cannot enroll: no webcam"));
        return;
    }
    c.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    c.set(cv::CAP_PROP_FRAME_HEIGHT, 480);

    std::vector<cv::Mat> samples;
    std::vector<int> labels;

    for (int i = 0; i < sampleCount * 3 && static_cast<int>(samples.size()) < sampleCount; ++i) {
        cv::Mat frame;
        c.read(frame);
        if (frame.empty()) continue;
        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        std::vector<cv::Rect> faces;
        cascade.detectMultiScale(gray, faces, 1.1, 4,
                                  cv::CASCADE_SCALE_IMAGE, cv::Size(60, 60));
        if (faces.size() == 1) {
            cv::Mat roi = gray(faces[0]);
            cv::resize(roi, roi, cv::Size(100, 100));
            samples.push_back(roi);
            labels.push_back(0);
            emit enrollmentProgress(static_cast<int>(samples.size()), sampleCount);
        }
        cv::waitKey(200);
    }

    if (static_cast<int>(samples.size()) < 3) {
        emit alertMessage(QStringLiteral("Enrollment failed: only %1 samples")
                              .arg(samples.size()));
        return;
    }

    QDir().mkpath(QFileInfo(ownerModelPath()).absolutePath());
    auto rec = cv::face::LBPHFaceRecognizer::create();
    rec->train(samples, labels);
    rec->save(ownerModelPath().toStdString());
    m_ownerEnrolled = true;

    emit enrollmentComplete(static_cast<int>(samples.size()));
    emit alertMessage(QStringLiteral("Owner enrolled (%1 samples)")
                          .arg(samples.size()));
#else
    Q_UNUSED(sampleCount)
    emit alertMessage(QStringLiteral("OpenCV not available"));
#endif
}

bool SecurityCamera::isOwnerEnrolled() const { return m_ownerEnrolled; }

void SecurityCamera::confirmCompanionOk(int cooldownMinutes)
{
    m_companionSuppressed = true;
    m_companionCooldown.start();
    // Timer auto-expires via elapsed() check in onSentinelTick.
    // Store the cooldown duration for the check.
    const int ms = cooldownMinutes * 60 * 1000;
    QTimer::singleShot(ms, this, [this]() { m_companionSuppressed = false; });
    qDebug() << "[SecurityCam] Companion confirmed OK. Suppressed for"
             << cooldownMinutes << "min";
}

// ============================================================
//  Sentinel tick — the core loop
// ============================================================

void SecurityCamera::onSentinelTick()
{
    QMutexLocker lock(&m_mutex);

    // Check if we should power down
    const qint64 idleMs = m_lastActivityTime.elapsed();
    if (m_powerState == Sentinel && idleMs > CAMERA_OFF_TIMEOUT_MS) {
        deescalateToOff();
        return;
    }

    if (m_powerState == Off) return; // only input hooks active

    // ── Sentinel mode: low-res motion check ────────────────
    if (m_powerState == Sentinel) {
        const QImage lowRes = captureFrameLowRes();
        if (lowRes.isNull()) return;

        if (detectMotion(lowRes)) {
            // Motion detected → escalate to full alert
            escalateToFullAlert();

            if (m_alertMotion)
                emit motionDetected(lowRes);
        }
        return;
    }

    // ── Full Alert mode: high-res face detection ───────────
    const QImage frame = captureFrameFullRes();
    if (frame.isNull()) return;

    const int faceCount = detectFaces(frame);

    if (faceCount == 0) {
        ++m_ownerAbsentTicks;
        if (m_ownerEnrolled && m_ownerAbsentTicks >= OWNER_ABSENT_LOCK_TICKS) {
            // Owner hasn't been seen for ~30s → auto-lock
            lockScreen();
            emit ownerAbsent(frame);
            emit alertMessage(QStringLiteral(
                "🔒 Owner absent for %1s — screen auto-locked")
                .arg(m_ownerAbsentTicks * m_sentinelTimer->interval() / 1000));
            m_ownerAbsentTicks = 0;
            deescalateToSentinel();
        }
        if (!m_deescalateTimer->isActive())
            m_deescalateTimer->start(FULL_ALERT_TIMEOUT_MS);
        return;
    }

    m_ownerAbsentTicks = 0; // reset — faces detected

    // Faces detected → keep alert alive
    m_deescalateTimer->start(FULL_ALERT_TIMEOUT_MS);

    const bool ownerPresent = m_ownerEnrolled && isOwner(frame);

    // Check if companion cooldown is active
    if (m_companionSuppressed && m_companionCooldown.elapsed() > 0) {
        // Cooldown expired — re-enable alerts
        m_companionSuppressed = false;
    }

    if (ownerPresent) {
        emit ownerRecognized(frame);

        if (faceCount > 1 && !m_companionSuppressed) {
            // Owner is here but someone else is looking too.
            // Don't lock — just send a soft notification with a question.
            emit companionNotice(frame, faceCount);
            qDebug() << "[SecurityCam] Owner + " << (faceCount - 1)
                     << " companion(s) — soft alert sent";
        }

        // Owner is here → deescalate to sentinel
        deescalateToSentinel();
        return;
    }

    // ── No owner among the faces — real threat ──────────────

    if (faceCount > 1 && m_alertShoulder) {
        // Multiple strangers at the screen — hard alert + lock
        emit unknownFaceDetected(frame, faceCount);
        if (m_autoLock) {
            lockScreen();
            emit alertMessage(QStringLiteral(
                "⚠ %1 UNKNOWN faces — LOCKED + photo sent").arg(faceCount));
        }
    } else if (faceCount == 1 && m_alertUnknown) {
        // Single stranger — hard alert + lock
        emit unknownFaceDetected(frame, 1);
        if (m_autoLock) {
            lockScreen();
            emit alertMessage(QStringLiteral(
                "🚨 UNKNOWN FACE — LOCKED + photo sent"));
        }
    }
}
