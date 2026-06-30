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
#include <QThread>
#include <QtConcurrent>
#include <QCoreApplication>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifdef JARVIS_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgcodecs.hpp>

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

// ── LBP (Local Binary Patterns) for face recognition ──────
// Each pixel becomes an 8-bit code describing the texture pattern
// around it. Grid of regional LBP histograms captures both
// texture and spatial structure — same idea as LBPHFaceRecognizer.

static constexpr int LBP_GRID = 4; // 4x4 = 16 regions
static constexpr int FACE_SZ  = 200;

static cv::Mat computeLBP(const cv::Mat& gray)
{
    cv::Mat lbp = cv::Mat::zeros(gray.rows - 2, gray.cols - 2, CV_8UC1);
    for (int i = 1; i < gray.rows - 1; ++i) {
        for (int j = 1; j < gray.cols - 1; ++j) {
            const uchar c = gray.at<uchar>(i, j);
            uchar code = 0;
            code |= (gray.at<uchar>(i-1, j-1) >= c) << 7;
            code |= (gray.at<uchar>(i-1, j  ) >= c) << 6;
            code |= (gray.at<uchar>(i-1, j+1) >= c) << 5;
            code |= (gray.at<uchar>(i  , j+1) >= c) << 4;
            code |= (gray.at<uchar>(i+1, j+1) >= c) << 3;
            code |= (gray.at<uchar>(i+1, j  ) >= c) << 2;
            code |= (gray.at<uchar>(i+1, j-1) >= c) << 1;
            code |= (gray.at<uchar>(i  , j-1) >= c) << 0;
            lbp.at<uchar>(i-1, j-1) = code;
        }
    }
    return lbp;
}

// Build concatenated histogram from LBP_GRID x LBP_GRID regions
static cv::Mat lbpGridHistogram(const cv::Mat& grayFace)
{
    cv::Mat lbp = computeLBP(grayFace);
    const int cellH = lbp.rows / LBP_GRID;
    const int cellW = lbp.cols / LBP_GRID;
    const int histSize = 256;
    float range[] = {0, 256};
    const float* histRange = {range};

    cv::Mat fullHist;
    for (int gy = 0; gy < LBP_GRID; ++gy) {
        for (int gx = 0; gx < LBP_GRID; ++gx) {
            cv::Rect roi(gx * cellW, gy * cellH, cellW, cellH);
            cv::Mat cell = lbp(roi);
            cv::Mat hist;
            cv::calcHist(&cell, 1, nullptr, cv::Mat(), hist, 1,
                         &histSize, &histRange);
            cv::normalize(hist, hist, 0, 1, cv::NORM_MINMAX);
            fullHist.push_back(hist);
        }
    }
    return fullHist.reshape(1, 1); // 1 x (16*256) row vector
}

static double compareLBPHistograms(const cv::Mat& a, const cv::Mat& b)
{
    if (a.cols != b.cols) return 0;
    const int regionSize = 256;
    const int regions = a.cols / regionSize;
    double totalCorr = 0;
    for (int r = 0; r < regions; ++r) {
        cv::Mat ra = a.colRange(r * regionSize, (r + 1) * regionSize);
        cv::Mat rb = b.colRange(r * regionSize, (r + 1) * regionSize);
        totalCorr += cv::compareHist(ra.t(), rb.t(), cv::HISTCMP_CORREL);
    }
    return totalCorr / regions;
}

static cv::Mat prepareFace(const cv::Mat& gray, const cv::Rect& face)
{
    cv::Mat roi = gray(face);
    cv::Mat resized;
    cv::resize(roi, resized, cv::Size(FACE_SZ, FACE_SZ));
    cv::equalizeHist(resized, resized);
    return resized;
}

#endif

// ============================================================
//  Runtime OpenCV availability check (delay-load safe)
// ============================================================

QString SecurityCamera::opencvDllPath()
{
    static const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/opencv_world4100.dll"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../redist/opencv/build/x64/vc16/bin/opencv_world4100.dll"),
    };
    for (const auto& p : candidates)
        if (QFileInfo::exists(p)) return p;
    return {};
}

bool SecurityCamera::isOpenCvAvailable()
{
    const QString dll = opencvDllPath();

#ifdef JARVIS_HAS_OPENCV
    const bool compiled = true;
#else
    const bool compiled = false;
#endif

    static bool logged = false;
    if (!logged) {
        logged = true;
        qDebug() << "[SecurityCam] ── OpenCV diagnostic ──";
        qDebug() << "[SecurityCam]   compiled with JARVIS_HAS_OPENCV:" << compiled;
        qDebug() << "[SecurityCam]   applicationDirPath:" << QCoreApplication::applicationDirPath();
        qDebug() << "[SecurityCam]   DLL search result:" << (dll.isEmpty() ? "NOT FOUND" : dll);

        const QStringList check = {
            QCoreApplication::applicationDirPath() + QStringLiteral("/opencv_world4100.dll"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../redist/opencv/build/x64/vc16/bin/opencv_world4100.dll"),
        };
        for (const auto& p : check)
            qDebug() << "[SecurityCam]   " << p << "exists:" << QFileInfo::exists(p);
    }

    if (!compiled) {
        qWarning() << "[SecurityCam] Binary was NOT compiled with JARVIS_HAS_OPENCV!"
                    << "Reset CMake cache (Tools → CMake → Reset Cache) and rebuild.";
        return false;
    }
    return !dll.isEmpty();
}

// ============================================================
//  Paths
// ============================================================

QString SecurityCamera::ownerSamplesDir()
{
    return JarvisPaths::subPath(QStringLiteral("security/owner_samples"));
}

QString SecurityCamera::cascadePath()
{
    static const QStringList paths = {
        // Installed client copy — CI ships the cascade next to jarvis.exe
        // (see .github/workflows/build.yml "Prepare release package")
        QCoreApplication::applicationDirPath() + QStringLiteral("/haarcascade_frontalface_default.xml"),
        // First-run auto-download destination (fallback if not bundled)
        JarvisPaths::subPath(QStringLiteral("security/haarcascade_frontalface_default.xml")),
        // Developer build — found inside the source tree's redist/
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

    m_lockCheckTimer = new QTimer(this);
    connect(m_lockCheckTimer, &QTimer::timeout,
            this, &SecurityCamera::onLockCheckTick);

    m_lastActivityTime.start();

    if (isOpenCvAvailable()) {
        m_ownerEnrolled = QDir(ownerSamplesDir()).entryList(
            QStringList{QStringLiteral("*.png")}, QDir::Files).size() >= 3;

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
    }

    // Force diagnostic output on first creation
    isOpenCvAvailable();

    const QString cascade = cascadePath();
    qDebug() << "[SecurityCam] Init complete."
             << "OpenCV available:" << isOpenCvAvailable()
             << "Cascade:" << (cascade.isEmpty() ? "NOT FOUND" : cascade)
             << "Owner enrolled:" << m_ownerEnrolled;
}

SecurityCamera::~SecurityCamera()
{
    stopMonitoring();
#ifdef JARVIS_HAS_OPENCV
    if (isOpenCvAvailable()) closeCam();
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
    if (isOpenCvAvailable()) openCam();
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
#ifdef JARVIS_HAS_OPENCV
    if (isOpenCvAvailable()) {
        auto& c = cam();
        if (c.isOpened()) {
            c.set(cv::CAP_PROP_FRAME_WIDTH,  SENTINEL_RES_W);
            c.set(cv::CAP_PROP_FRAME_HEIGHT, SENTINEL_RES_H);
        }
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
    if (isOpenCvAvailable()) closeCam();
#endif

    emit powerStateChanged(Off);
    qDebug() << "[SecurityCam] → OFF (input hooks only)";
}

// ============================================================
//  Monitoring lifecycle
// ============================================================

void SecurityCamera::startMonitoring(int sentinelIntervalSec)
{
    if (!isOpenCvAvailable()) {
        const QString reason =
#ifdef JARVIS_HAS_OPENCV
            QStringLiteral("DLL not found next to jarvis.exe (%1). "
                           "Open Help → Component Manager → Install OpenCV.")
                .arg(QCoreApplication::applicationDirPath());
#else
            QStringLiteral("Binary was compiled WITHOUT OpenCV. "
                           "Reset CMake cache (Tools → CMake → Reset Cache) and rebuild.");
#endif
        qWarning() << "[SecurityCam] startMonitoring blocked:" << reason;
        emit alertMessage(reason);
        return;
    }

    m_monitoring = true;
    installInputHook();

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
    m_stopping = true;
    m_monitoring = false;
    m_sentinelTimer->stop();
    m_deescalateTimer->stop();
    m_lockCheckTimer->stop();
    if (m_screenLocked) {
        m_screenLocked = false;
        emit requestUnlockOverlay();
    }
    removeInputHook();

    if (m_recording) {
        QElapsedTimer wait;
        wait.start();
        while (m_recording && wait.elapsed() < 25000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    deescalateToOff();
    m_stopping = false;
}

bool SecurityCamera::isMonitoring() const { return m_monitoring; }

void SecurityCamera::checkNow() { onSentinelTick(); }

// ============================================================
//  Frame capture
// ============================================================

QImage SecurityCamera::captureFrameLowRes()
{
#ifdef JARVIS_HAS_OPENCV
    if (!isOpenCvAvailable()) return {};
    auto& c = cam();
    if (!c.isOpened()) return {};
    cv::Mat frame;
    c.read(frame);
    if (frame.empty()) return {};
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
    if (isOpenCvAvailable()) {
        auto& c = cam();
        if (!c.isOpened()) openCam();
        c.set(cv::CAP_PROP_FRAME_WIDTH,  1280);
        c.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
        cv::Mat frame;
        c.read(frame);
        if (frame.empty()) return {};
        return matToQImage(frame);
    }
#endif
    QScreen* screen = QApplication::primaryScreen();
    if (!screen) return {};
    return screen->grabWindow(0).toImage();
}

// ============================================================
//  Detection algorithms
// ============================================================

int SecurityCamera::detectFaces(const QImage& frame)
{
#ifdef JARVIS_HAS_OPENCV
    if (!isOpenCvAvailable()) return 0;
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
    if (!isOpenCvAvailable() || !m_ownerEnrolled) return false;

    const QString samplesPath = ownerSamplesDir();
    const QStringList files = QDir(samplesPath).entryList(
        QStringList{QStringLiteral("*.png")}, QDir::Files);
    if (files.isEmpty()) return false;

    cv::Mat mat = qimageToMat(frame);
    cv::Mat gray;
    cv::cvtColor(mat, gray, cv::COLOR_BGR2GRAY);
    auto& cascade = faceCascade();
    if (cascade.empty()) return false;

    std::vector<cv::Rect> faces;
    cascade.detectMultiScale(gray, faces, 1.1, 4,
                              cv::CASCADE_SCALE_IMAGE, cv::Size(60, 60));

    // Preload sample LBP histograms (cached per call — samples rarely change)
    std::vector<cv::Mat> sampleHists;
    sampleHists.reserve(files.size());
    for (const auto& fn : files) {
        cv::Mat sample = cv::imread(
            (samplesPath + QStringLiteral("/") + fn).toStdString(),
            cv::IMREAD_GRAYSCALE);
        if (sample.empty()) continue;
        sampleHists.push_back(lbpGridHistogram(sample));
    }
    if (sampleHists.empty()) return false;

    for (const auto& face : faces) {
        cv::Mat prepared = prepareFace(gray, face);
        cv::Mat probeHist = lbpGridHistogram(prepared);

        // Compare against every sample, take average of top-3 matches
        std::vector<double> scores;
        scores.reserve(sampleHists.size());
        for (const auto& sh : sampleHists)
            scores.push_back(compareLBPHistograms(probeHist, sh));

        std::sort(scores.rbegin(), scores.rend());
        const int topN = std::min(3, static_cast<int>(scores.size()));
        double avg = 0;
        for (int i = 0; i < topN; ++i) avg += scores[i];
        avg /= topN;

        qDebug() << "[SecurityCam] LBP match score:" << avg
                 << "(best:" << scores[0] << ", threshold: 0.45)";

        if (avg > 0.45) return true;
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
    if (isOpenCvAvailable()) {
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
    }
#endif
    m_prevFrame = frame;
    return false;
}

void SecurityCamera::lockScreen()
{
    if (m_screenLocked) return;
    m_screenLocked = true;
    emit requestLockOverlay();

    // While locked, scan for owner face every 3s to auto-unlock
    if (!m_lockCheckTimer->isActive())
        m_lockCheckTimer->start(LOCK_CHECK_INTERVAL_MS);

    qDebug() << "[SecurityCam] Screen locked via overlay";
}

// ============================================================
//  Owner enrollment
// ============================================================

void SecurityCamera::enrollOwnerFace(int sampleCount)
{
    if (!isOpenCvAvailable()) {
        const QString reason =
#ifdef JARVIS_HAS_OPENCV
            QStringLiteral("DLL not found next to jarvis.exe (%1). "
                           "Open Help → Component Manager → Install OpenCV.")
                .arg(QCoreApplication::applicationDirPath());
#else
            QStringLiteral("Binary was compiled WITHOUT OpenCV. "
                           "Reset CMake cache (Tools → CMake → Reset Cache) and rebuild.");
#endif
        qWarning() << "[SecurityCam] enrollOwnerFace blocked:" << reason;
        emit alertMessage(reason);
        return;
    }

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
            cv::Mat prepared = prepareFace(gray, faces[0]);
            samples.push_back(prepared);
            labels.push_back(0);
            emit enrollmentProgress(static_cast<int>(samples.size()), sampleCount);
        }
        QThread::msleep(200);
    }

    if (static_cast<int>(samples.size()) < 3) {
        emit alertMessage(QStringLiteral("Enrollment failed: only %1 samples")
                              .arg(samples.size()));
        return;
    }

    const QString dir = ownerSamplesDir();
    QDir().mkpath(dir);
    // Clear old samples
    for (const auto& f : QDir(dir).entryList(QStringList{QStringLiteral("*.png")}, QDir::Files))
        QFile::remove(dir + QStringLiteral("/") + f);

    for (int i = 0; i < static_cast<int>(samples.size()); ++i) {
        const QString path = QStringLiteral("%1/owner_%2.png").arg(dir).arg(i, 3, 10, QLatin1Char('0'));
        cv::imwrite(path.toStdString(), samples[i]);
    }
    m_ownerEnrolled = true;

    emit enrollmentComplete(static_cast<int>(samples.size()));
    emit alertMessage(QStringLiteral("Owner enrolled (%1 samples)")
                          .arg(samples.size()));
#else
    Q_UNUSED(sampleCount)
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

    // ── Sentinel mode: low-res motion check + face presence ─
    if (m_powerState == Sentinel) {
        const QImage lowRes = captureFrameLowRes();
        if (lowRes.isNull()) return;

        if (detectMotion(lowRes)) {
            escalateToFullAlert();
            if (m_alertMotion)
                emit motionDetected(lowRes);
            return;
        }

        if (m_ownerEnrolled && !m_screenLocked) {
            const int faces = detectFaces(lowRes);
            if (faces == 0) {
                ++m_ownerAbsentTicks;
                if (m_ownerAbsentTicks >= OWNER_ABSENT_LOCK_TICKS) {
                    escalateToFullAlert();
                    const QImage hiRes = captureFrameFullRes();
                    if (!hiRes.isNull() && detectFaces(hiRes) == 0) {
                        lockScreen();
                        emit ownerAbsent(hiRes);
                        emit alertMessage(QStringLiteral(
                            "🔒 Owner absent — screen locked. Come back to auto-unlock."));
                    }
                    m_ownerAbsentTicks = 0;
                }
            } else {
                m_ownerAbsentTicks = 0;
            }
        }
        return;
    }

    // ── Full Alert mode: high-res face detection ───────────
    const QImage frame = captureFrameFullRes();
    if (frame.isNull()) return;

    const int faceCount = detectFaces(frame);

    if (faceCount == 0) {
        ++m_ownerAbsentTicks;
        if (m_ownerEnrolled && !m_screenLocked
            && m_ownerAbsentTicks >= OWNER_ABSENT_LOCK_TICKS) {
            lockScreen();
            emit ownerAbsent(frame);
            emit alertMessage(QStringLiteral(
                "🔒 Owner absent — screen locked. Come back to auto-unlock."));
            m_ownerAbsentTicks = 0;
        }
        if (m_ownerEnrolled && !m_screenLocked
            && m_ownerAbsentTicks < OWNER_ABSENT_LOCK_TICKS) {
            m_deescalateTimer->stop();
        } else if (!m_deescalateTimer->isActive()) {
            m_deescalateTimer->start(FULL_ALERT_TIMEOUT_MS);
        }
        return;
    }

    m_ownerAbsentTicks = 0; // reset — faces detected

    // Faces detected → keep alert alive
    m_deescalateTimer->start(FULL_ALERT_TIMEOUT_MS);

    const bool ownerPresent = m_ownerEnrolled && isOwner(frame);

    // companion cooldown is managed by QTimer::singleShot in confirmCompanionOk()

    if (ownerPresent) {
        emit ownerRecognized(frame);

        if (m_screenLocked && m_autoUnlock) {
            m_screenLocked = false;
            m_lockCheckTimer->stop();
            emit requestUnlockOverlay();
            emit alertMessage(QStringLiteral("🔓 Owner recognized — screen unlocked"));
            qDebug() << "[SecurityCam] Owner recognized → auto-unlock";
        }

        if (faceCount > 1 && !m_companionSuppressed) {
            emit companionNotice(frame, faceCount);
            qDebug() << "[SecurityCam] Owner + " << (faceCount - 1)
                     << " companion(s) — soft alert sent";
        }

        deescalateToSentinel();
        return;
    }

    // ── No owner among the faces — real threat ──────────────

    if (faceCount > 1 && m_alertShoulder) {
        emit unknownFaceDetected(frame, faceCount);
        if (m_autoLock) {
            lockScreen();
            emit alertMessage(QStringLiteral(
                "⚠ %1 UNKNOWN faces — LOCKED").arg(faceCount));
        }
        if (!m_recording) recordMotionClip();
    } else if (faceCount == 1 && m_alertUnknown) {
        emit unknownFaceDetected(frame, 1);
        if (m_autoLock) {
            lockScreen();
            emit alertMessage(QStringLiteral(
                "🚨 UNKNOWN FACE — LOCKED"));
        }
        if (!m_recording) recordMotionClip();
    }
}

// ============================================================
//  Lock-check tick — runs every 3s while screen is locked
//  Keeps camera alive and checks for owner face to auto-unlock
// ============================================================

void SecurityCamera::onLockCheckTick()
{
    if (!m_screenLocked) {
        m_lockCheckTimer->stop();
        return;
    }

#ifdef JARVIS_HAS_OPENCV
    if (!isOpenCvAvailable()) return;

    auto& c = cam();
    if (!c.isOpened()) openCam();

    const QImage frame = captureFrameFullRes();
    if (frame.isNull()) return;

    // Check for motion while locked → record + alert
    if (detectMotion(frame) && !m_recording) {
        emit motionDetected(frame);
        recordMotionClip();
    }

    // Check if owner returned
    if (m_ownerEnrolled) {
        const int faces = detectFaces(frame);
        if (faces > 0 && isOwner(frame) && m_autoUnlock) {
            m_screenLocked = false;
            m_lockCheckTimer->stop();
            emit requestUnlockOverlay();
            emit ownerRecognized(frame);
            emit alertMessage(QStringLiteral("🔓 Owner recognized — screen unlocked"));
            qDebug() << "[SecurityCam] Lock-check: owner detected → unlock";
        }
    }
#endif
}

// ============================================================
//  Record 20-second motion clip (AVI, MJPG codec)
// ============================================================

void SecurityCamera::recordMotionClip()
{
#ifdef JARVIS_HAS_OPENCV
    if (!isOpenCvAvailable() || m_recording) return;
    m_recording = true;

    const QString dir = JarvisPaths::subPath(QStringLiteral("security/clips"));
    QDir().mkpath(dir);
    const QString path = QStringLiteral("%1/motion_%2.avi")
        .arg(dir, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));

    qDebug() << "[SecurityCam] Recording" << MOTION_CLIP_DURATION_SEC
             << "s clip to" << path;
    emit alertMessage(QStringLiteral("📹 Recording %1s security clip...")
                          .arg(MOTION_CLIP_DURATION_SEC));

    // Pause timers and release shared camera so the bg thread can use it
    m_sentinelTimer->stop();
    m_lockCheckTimer->stop();
    closeCam();

    const int duration = MOTION_CLIP_DURATION_SEC;

    QtConcurrent::run([this, path, duration]() {
        cv::VideoCapture recorder(0);
        if (!recorder.isOpened()) {
            qWarning() << "[SecurityCam] BG recorder: camera open failed";
            QMetaObject::invokeMethod(this, [this]() {
                m_recording = false;
                if (m_stopping) return;
                if (m_monitoring) {
                    openCam();
                    m_sentinelTimer->start();
                    if (m_screenLocked) m_lockCheckTimer->start(LOCK_CHECK_INTERVAL_MS);
                }
            }, Qt::QueuedConnection);
            return;
        }

        recorder.set(cv::CAP_PROP_FRAME_WIDTH, 640);
        recorder.set(cv::CAP_PROP_FRAME_HEIGHT, 480);

        const double fps = 10.0;
        cv::VideoWriter writer(
            path.toStdString(),
            cv::VideoWriter::fourcc('M','J','P','G'),
            fps, cv::Size(640, 480));

        if (!writer.isOpened()) {
            qWarning() << "[SecurityCam] BG recorder: VideoWriter open failed";
            recorder.release();
            QMetaObject::invokeMethod(this, [this]() {
                m_recording = false;
                if (m_stopping) return;
                if (m_monitoring) {
                    openCam();
                    m_sentinelTimer->start();
                    if (m_screenLocked) m_lockCheckTimer->start(LOCK_CHECK_INTERVAL_MS);
                }
            }, Qt::QueuedConnection);
            return;
        }

        const int totalFrames = static_cast<int>(fps * duration);
        const int delayMs = static_cast<int>(1000.0 / fps);

        for (int i = 0; i < totalFrames; ++i) {
            cv::Mat frame;
            recorder.read(frame);
            if (frame.empty()) continue;
            if (frame.cols != 640 || frame.rows != 480)
                cv::resize(frame, frame, cv::Size(640, 480));
            writer.write(frame);
            QThread::msleep(delayMs);
        }

        writer.release();
        recorder.release();

        qDebug() << "[SecurityCam] Clip saved:" << path;

        QMetaObject::invokeMethod(this, [this, path]() {
            m_recording = false;
            if (m_stopping) return;
            emit motionVideoReady(path);
            if (m_monitoring) {
                openCam();
                m_sentinelTimer->start();
                if (m_screenLocked) m_lockCheckTimer->start(LOCK_CHECK_INTERVAL_MS);
            }
        }, Qt::QueuedConnection);
    });
#endif
}
