#pragma once
// ============================================================
// media_preview_widget.h — Local Media Preview Window
//
// Borderless frameless overlay that displays images and video
// on the host machine when JARVIS sends media to Telegram.
// Supports smooth-scaled images (QPixmap) and looped video
// playback (QMediaPlayer + QVideoWidget).
// ============================================================

#include <QWidget>
#include <QString>
#include <QPixmap>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QStackedLayout;
class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;

class MediaPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MediaPreviewWidget(QWidget* parent = nullptr);
    ~MediaPreviewWidget() override;

    void displayMedia(const QString& filePath, const QString& mediaType);
    void displayImageBuffer(const QByteArray& imageData, const QString& title);
    void clearPreview();

signals:
    void closed();

protected:
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    void showImage(const QString& filePath);
    void showVideo(const QString& filePath);
    void stopVideo();
    void rescalePixmap();

    // Title bar (custom, for frameless drag)
    QWidget*        m_titleBar    = nullptr;
    QLabel*         m_titleLabel  = nullptr;
    QPushButton*    m_closeBtn    = nullptr;

    // Image display
    QLabel*         m_imageLabel  = nullptr;
    QPixmap         m_originalPix;

    // Video display
    QMediaPlayer*   m_player      = nullptr;
    QAudioOutput*   m_audioOut    = nullptr;
    QVideoWidget*   m_videoWidget = nullptr;

    // Layout
    QVBoxLayout*    m_mainLayout  = nullptr;
    QStackedLayout* m_stack       = nullptr;

    // Frameless window dragging
    QPoint          m_dragPos;
    bool            m_dragging    = false;

    QString         m_currentType;
};
