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
#include <QString>

class QCamera;
class QMediaCaptureSession;
class QImageCapture;
class QVideoSink;
class QAudioInput;
class QMediaRecorder;

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

    // Video+audio clip recording (webcam + microphone) — produces an MP4
    // (H.264/AAC) that plays with sound, e.g. for Telegram alerts.
    // Unlike the silent OpenCV motion clips, this captures real audio.
    void recordClip(const QString& outputPath, int durationSec);
    bool isRecordingClip() const;

signals:
    void webcamCaptured(const QImage& image);
    void desktopCaptured(const QImage& image);
    void surveillanceFrame(const QImage& image, const QString& source);
    void captureError(const QString& message);

    // Video+audio clip recording result
    void clipReady(const QString& path);
    void clipError(const QString& message);

private slots:
    void onSurveillanceTick();

private:
    void initCamera();
    void releaseCamera();
    void ensureRecorder();

    QCamera*               m_camera   = nullptr;
    QMediaCaptureSession*  m_session  = nullptr;
    QImageCapture*         m_capture  = nullptr;
    QVideoSink*            m_sink     = nullptr;
    QAudioInput*           m_audioInput = nullptr;
    QMediaRecorder*        m_recorder   = nullptr;
    QTimer*                m_survTimer = nullptr;
    QTimer*                m_releaseTimer = nullptr;
    QTimer*                m_recordStopTimer = nullptr;

    bool m_survWebcam  = true;
    bool m_survDesktop = true;
    bool m_cameraReady = false;
};
