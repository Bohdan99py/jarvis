// ============================================================
// camera_agent.cpp — Webcam Capture + Desktop Surveillance
// ============================================================

#include "camera_agent.h"

#include <QCamera>
#include <QMediaCaptureSession>
#include <QImageCapture>
#include <QMediaDevices>
#include <QVideoSink>
#include <QVideoFrame>
#include <QScreen>
#include <QApplication>
#include <QDateTime>
#include <QDebug>

CameraAgent::CameraAgent(QObject* parent)
    : QObject(parent)
{
    m_survTimer = new QTimer(this);
    connect(m_survTimer, &QTimer::timeout,
            this, &CameraAgent::onSurveillanceTick);

    qDebug() << "[CameraAgent] Initialized. Webcams available:"
             << availableCameras().size();
}

CameraAgent::~CameraAgent()
{
    stopSurveillance();
    if (m_camera) m_camera->stop();
}

// ============================================================
//  Camera availability
// ============================================================

bool CameraAgent::hasWebcam() const
{
    return !QMediaDevices::videoInputs().isEmpty();
}

QStringList CameraAgent::availableCameras() const
{
    QStringList names;
    for (const auto& cam : QMediaDevices::videoInputs())
        names.append(cam.description());
    return names;
}

// ============================================================
//  Lazy camera init
// ============================================================

void CameraAgent::initCamera()
{
    if (m_camera) return;

    const auto cameras = QMediaDevices::videoInputs();
    if (cameras.isEmpty()) {
        qWarning() << "[CameraAgent] No webcam found";
        return;
    }

    m_camera  = new QCamera(cameras.first(), this);
    m_session = new QMediaCaptureSession(this);
    m_capture = new QImageCapture(this);
    m_sink    = new QVideoSink(this);

    m_session->setCamera(m_camera);
    m_session->setImageCapture(m_capture);
    m_session->setVideoSink(m_sink);

    connect(m_capture, &QImageCapture::imageCaptured, this,
            [this](int /*id*/, const QImage& img) {
        m_cameraReady = true;
        emit webcamCaptured(img);
        qDebug() << "[CameraAgent] Webcam frame captured:" << img.size();
    });

    connect(m_capture, &QImageCapture::errorOccurred, this,
            [this](int /*id*/, QImageCapture::Error /*err*/, const QString& msg) {
        emit captureError(QStringLiteral("Webcam: ") + msg);
        qWarning() << "[CameraAgent] Capture error:" << msg;
    });

    m_camera->start();

    // Give camera 500ms to warm up before first capture
    QTimer::singleShot(500, this, [this]() {
        m_cameraReady = true;
        qDebug() << "[CameraAgent] Camera ready";
    });
}

// ============================================================
//  Single-shot captures
// ============================================================

void CameraAgent::captureWebcam()
{
    if (!hasWebcam()) {
        emit captureError(QStringLiteral("No webcam available"));
        return;
    }

    initCamera();

    if (!m_camera || !m_capture) {
        emit captureError(QStringLiteral("Camera init failed"));
        return;
    }

    if (!m_cameraReady) {
        // Camera still warming up — retry after delay
        QTimer::singleShot(600, this, [this]() {
            if (m_capture && m_capture->isReadyForCapture())
                m_capture->capture();
            else
                emit captureError(QStringLiteral("Camera not ready"));
        });
        return;
    }

    if (m_capture->isReadyForCapture())
        m_capture->capture();
    else
        emit captureError(QStringLiteral("Camera not ready for capture"));
}

void CameraAgent::captureDesktop()
{
    QScreen* screen = QApplication::primaryScreen();
    if (!screen) {
        emit captureError(QStringLiteral("No screen available"));
        return;
    }

    QImage shot = screen->grabWindow(0).toImage();
    if (shot.isNull()) {
        emit captureError(QStringLiteral("Desktop capture failed"));
        return;
    }

    emit desktopCaptured(shot);
    qDebug() << "[CameraAgent] Desktop captured:" << shot.size();
}

// ============================================================
//  Surveillance mode
// ============================================================

void CameraAgent::startSurveillance(int intervalSec, bool webcam, bool desktop)
{
    m_survWebcam  = webcam;
    m_survDesktop = desktop;

    if (webcam) initCamera();

    m_survTimer->setInterval(intervalSec * 1000);
    m_survTimer->start();

    qDebug() << "[CameraAgent] Surveillance started. Interval:"
             << intervalSec << "sec. Webcam:" << webcam
             << "Desktop:" << desktop;
}

void CameraAgent::stopSurveillance()
{
    m_survTimer->stop();
    qDebug() << "[CameraAgent] Surveillance stopped";
}

bool CameraAgent::isSurveilling() const
{
    return m_survTimer->isActive();
}

void CameraAgent::onSurveillanceTick()
{
    if (m_survDesktop) {
        QScreen* screen = QApplication::primaryScreen();
        if (screen) {
            QImage shot = screen->grabWindow(0).toImage();
            if (!shot.isNull())
                emit surveillanceFrame(shot, QStringLiteral("desktop"));
        }
    }

    if (m_survWebcam && m_capture && m_capture->isReadyForCapture()) {
        // Webcam capture is async — delivered via webcamCaptured signal
        // which we re-emit as surveillanceFrame
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(this, &CameraAgent::webcamCaptured, this,
                        [this, conn](const QImage& img) {
            disconnect(*conn);
            emit surveillanceFrame(img, QStringLiteral("webcam"));
        });
        m_capture->capture();
    }
}
