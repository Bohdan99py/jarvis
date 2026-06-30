#pragma once
// ============================================================
// camera_agent.h — Webcam Capture + Desktop Surveillance
//
// Provides:
//   1. Webcam photo capture (single frame)
//   2. Desktop screenshot capture
//   3. Periodic surveillance mode ("video nanny") —
//      captures webcam/desktop at intervals and streams
//      to Telegram via the gateway
//
// Uses Qt6 Multimedia (QCamera + QImageCapture).
// Thread safety: all captures run on the main thread
// via QTimer; images are delivered as QImage signals.
// ============================================================

#include <QObject>
#include <QImage>
#include <QTimer>

class QCamera;
class QMediaCaptureSession;
class QImageCapture;
class QVideoSink;

class CameraAgent : public QObject
{
    Q_OBJECT

public:
    explicit CameraAgent(QObject* parent = nullptr);
    ~CameraAgent() override;

    // Single-shot captures
    void captureWebcam();
    void captureDesktop();

    // Surveillance mode — periodic captures sent via signal
    void startSurveillance(int intervalSec = 30, bool webcam = true, bool desktop = true);
    void stopSurveillance();
    bool isSurveilling() const;

    // Camera availability
    bool hasWebcam() const;
    QStringList availableCameras() const;

signals:
    void webcamCaptured(const QImage& image);
    void desktopCaptured(const QImage& image);
    void surveillanceFrame(const QImage& image, const QString& source);
    void captureError(const QString& message);

private slots:
    void onSurveillanceTick();

private:
    void initCamera();
    void releaseCamera();

    QCamera*               m_camera   = nullptr;
    QMediaCaptureSession*  m_session  = nullptr;
    QImageCapture*         m_capture  = nullptr;
    QVideoSink*            m_sink     = nullptr;
    QTimer*                m_survTimer = nullptr;
    QTimer*                m_releaseTimer = nullptr;

    bool m_survWebcam  = true;
    bool m_survDesktop = true;
    bool m_cameraReady = false;
};
