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
#include <QAudioInput>
#include <QMediaRecorder>
#include <QMediaFormat>
#include <QUrl>
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

    m_releaseTimer = new QTimer(this);
    m_releaseTimer->setSingleShot(true);
    m_releaseTimer->setInterval(5000);
    connect(m_releaseTimer, &QTimer::timeout,
            this, &CameraAgent::releaseCamera);

    qDebug() << "[CameraAgent] Initialized. Webcams available:"
             << availableCameras().size();
}

CameraAgent::~CameraAgent()
{
    stopSurveillance();
    if (m_recorder && m_recorder->recorderState() == QMediaRecorder::RecordingState)
        m_recorder->stop();
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
        if (!m_survTimer->isActive())
            m_releaseTimer->start();
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
//  Camera release (turns off LED when not needed)
// ============================================================

void CameraAgent::releaseCamera()
{
    if (!m_camera) return;
    if (m_survTimer->isActive()) return;
    if (isRecordingClip()) return; // don't yank the camera mid-recording

    m_camera->stop();
    delete m_camera;     m_camera     = nullptr;
    delete m_session;    m_session    = nullptr;
    delete m_capture;    m_capture    = nullptr;
    delete m_sink;       m_sink       = nullptr;
    delete m_recorder;   m_recorder   = nullptr;
    delete m_audioInput; m_audioInput = nullptr;
    m_cameraReady = false;

    qDebug() << "[CameraAgent] Camera released (LED off)";
}

// ============================================================
//  Video+audio clip recording
// ============================================================

void CameraAgent::ensureRecorder()
{
    if (m_recorder) return;

    initCamera();
    if (!m_session) return;

    m_audioInput = new QAudioInput(this);
    m_recorder   = new QMediaRecorder(this);

    QMediaFormat fmt;
    fmt.setFileFormat(QMediaFormat::FileFormat::MPEG4);
    fmt.setVideoCodec(QMediaFormat::VideoCodec::H264);
    fmt.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    m_recorder->setMediaFormat(fmt);

    m_session->setAudioInput(m_audioInput);
    m_session->setRecorder(m_recorder);

    connect(m_recorder, &QMediaRecorder::recorderStateChanged, this,
            [this](QMediaRecorder::RecorderState state) {
        if (state != QMediaRecorder::StoppedState) return;
        const QString path = m_recorder->actualLocation().toLocalFile();
        if (path.isEmpty()) return;
        qDebug() << "[CameraAgent] Clip recorded (video+audio):" << path;
        emit clipReady(path);
        // Now safe to release the camera if surveillance isn't active
        m_releaseTimer->start();
    });
    connect(m_recorder, &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error /*error*/, const QString& errorString) {
        qWarning() << "[CameraAgent] Recorder error:" << errorString;
        emit clipError(errorString);
    });
}

void CameraAgent::recordClip(const QString& outputPath, int durationSec)
{
    if (!hasWebcam()) {
        emit clipError(QStringLiteral("No webcam available"));
        return;
    }

    ensureRecorder();
    if (!m_recorder) {
        emit clipError(QStringLiteral("Recorder initialization failed"));
        return;
    }
    if (m_recorder->recorderState() == QMediaRecorder::RecordingState) {
        emit clipError(QStringLiteral("Already recording a clip"));
        return;
    }

    if (!m_recordStopTimer) {
        m_recordStopTimer = new QTimer(this);
        m_recordStopTimer->setSingleShot(true);
        connect(m_recordStopTimer, &QTimer::timeout, this, [this]() {
            if (m_recorder) m_recorder->stop();
        });
    }

    auto startRecording = [this, outputPath, durationSec]() {
        if (!m_recorder) return;
        m_recorder->setOutputLocation(QUrl::fromLocalFile(outputPath));
        m_recorder->record();
        m_recordStopTimer->start(durationSec * 1000);
        qDebug() << "[CameraAgent] Recording clip (video+audio) to" << outputPath
                 << "for" << durationSec << "s";
    };

    // Camera still warming up (just opened by ensureRecorder→initCamera) —
    // starting record() immediately can produce a corrupt/empty first clip.
    if (!m_cameraReady)
        QTimer::singleShot(600, this, startRecording);
    else
        startRecording();
}

bool CameraAgent::isRecordingClip() const
{
    return m_recorder && m_recorder->recorderState() == QMediaRecorder::RecordingState;
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
    m_releaseTimer->start();
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
