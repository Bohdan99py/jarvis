// ============================================================
// media_preview_widget.cpp — Local Media Preview Window
// ============================================================
#include "media_preview_widget.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedLayout>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QFileInfo>
#include <QScreen>
#include <QGuiApplication>
#include <QGraphicsDropShadowEffect>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QDebug>

// ============================================================
//  Construction / Destruction
// ============================================================

MediaPreviewWidget::MediaPreviewWidget(QWidget* parent)
    : QWidget(parent, Qt::Window
                    | Qt::FramelessWindowHint
                    | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumSize(360, 280);
    resize(640, 480);

    // ── Outer layout wraps a rounded container ─────────────
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(8, 8, 8, 8);

    auto* container = new QWidget(this);
    container->setObjectName(QStringLiteral("mediaContainer"));
    container->setStyleSheet(QStringLiteral(
        "#mediaContainer {"
        "  background-color: rgba(11,12,16,240);"
        "  border: 1px solid rgba(102,252,241,0.18);"
        "  border-radius: 14px;"
        "}"
    ));
    outerLayout->addWidget(container);

    m_mainLayout = new QVBoxLayout(container);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // ── Title bar ──────────────────────────────────────────
    m_titleBar = new QWidget(container);
    m_titleBar->setFixedHeight(36);
    m_titleBar->setStyleSheet(QStringLiteral(
        "background: transparent;"
    ));

    auto* tbLayout = new QHBoxLayout(m_titleBar);
    tbLayout->setContentsMargins(14, 4, 8, 4);

    m_titleLabel = new QLabel(QStringLiteral("J.A.R.V.I.S. — Media Preview"), m_titleBar);
    m_titleLabel->setStyleSheet(QStringLiteral(
        "color: rgba(102,252,241,180); font-size: 12px;"
        "font-family: 'Segoe UI Semibold', sans-serif; letter-spacing: 1px;"
        "background: transparent;"
    ));
    tbLayout->addWidget(m_titleLabel);
    tbLayout->addStretch();

    m_closeBtn = new QPushButton(QStringLiteral("✕"), m_titleBar);
    m_closeBtn->setFixedSize(28, 28);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: rgba(197,198,199,100);"
        "  border: 1px solid rgba(102,252,241,15); border-radius: 14px;"
        "  font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background: rgba(255,76,76,40); color: #ff4c4c;"
        "  border-color: rgba(255,76,76,60); }"
    ));
    tbLayout->addWidget(m_closeBtn);

    m_mainLayout->addWidget(m_titleBar);

    // ── Content stack: image (page 0) / video (page 1) ────
    auto* stackWidget = new QWidget(container);
    m_stack = new QStackedLayout(stackWidget);
    m_stack->setContentsMargins(6, 0, 6, 6);

    // Page 0: Image
    m_imageLabel = new QLabel;
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet(QStringLiteral(
        "background: transparent; border-radius: 8px;"
    ));
    m_stack->addWidget(m_imageLabel);

    // Page 1: Video
    m_videoWidget = new QVideoWidget;
    m_videoWidget->setStyleSheet(QStringLiteral(
        "background: black; border-radius: 8px;"
    ));
    m_stack->addWidget(m_videoWidget);

    m_mainLayout->addWidget(stackWidget, 1);

    // ── Media player (lazy: created here, but only used for video) ──
    m_player   = new QMediaPlayer(this);
    m_audioOut = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOut);
    m_player->setVideoOutput(m_videoWidget);
    m_audioOut->setVolume(0.5f);

    connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia)
            m_player->play();
    });

    // ── Signals ────────────────────────────────────────────
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        stopVideo();
        hide();
        emit closed();
    });

    // ── Drop shadow ────────────────────────────────────────
    auto* shadow = new QGraphicsDropShadowEffect(container);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 120));
    container->setGraphicsEffect(shadow);

    // Center on primary screen
    if (auto* screen = QGuiApplication::primaryScreen()) {
        const QRect geo = screen->availableGeometry();
        move(geo.center() - rect().center());
    }
}

MediaPreviewWidget::~MediaPreviewWidget()
{
    stopVideo();
}

// ============================================================
//  Public API
// ============================================================

void MediaPreviewWidget::displayMedia(const QString& filePath, const QString& mediaType)
{
    m_currentType = mediaType;

    if (mediaType == QStringLiteral("video")) {
        showVideo(filePath);
    } else {
        showImage(filePath);
    }

    show();

    // Auto-raise without stealing keyboard focus:
    // raise() brings the window to the top of the Z-order,
    // but we skip activateWindow() so the user's IDE keeps focus.
    raise();

    const QString fileName = QFileInfo(filePath).fileName();
    m_titleLabel->setText(
        QStringLiteral("J.A.R.V.I.S. — %1").arg(fileName));

    qDebug() << "[MediaPreview] Displaying" << mediaType << ":" << filePath;
}

// ============================================================
//  Image display
// ============================================================

void MediaPreviewWidget::showImage(const QString& filePath)
{
    stopVideo();
    m_stack->setCurrentIndex(0);

    m_originalPix = QPixmap(filePath);
    if (m_originalPix.isNull()) {
        m_imageLabel->setText(QStringLiteral("⚠ Cannot load image"));
        m_imageLabel->setStyleSheet(QStringLiteral(
            "color: #ff4c4c; font-size: 14px; background: transparent;"));
        return;
    }

    rescalePixmap();
}

void MediaPreviewWidget::rescalePixmap()
{
    if (m_originalPix.isNull()) return;

    const QSize available = m_imageLabel->size();
    if (available.isEmpty()) return;

    const QPixmap scaled = m_originalPix.scaled(
        available, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(scaled);
}

// ============================================================
//  Video display
// ============================================================

void MediaPreviewWidget::showVideo(const QString& filePath)
{
    m_originalPix = QPixmap();
    m_imageLabel->clear();
    m_stack->setCurrentIndex(1);

    m_player->setSource(QUrl::fromLocalFile(filePath));
    m_player->play();
}

void MediaPreviewWidget::stopVideo()
{
    if (m_player->playbackState() != QMediaPlayer::StoppedState) {
        m_player->stop();
        m_player->setSource(QUrl());
    }
}

// ============================================================
//  Events
// ============================================================

void MediaPreviewWidget::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_currentType != QStringLiteral("video"))
        rescalePixmap();
}

// ── Frameless window drag ──────────────────────────────────

void MediaPreviewWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && m_titleBar
        && m_titleBar->geometry().contains(e->pos()))
    {
        m_dragging = true;
        m_dragPos  = e->globalPosition().toPoint() - frameGeometry().topLeft();
        e->accept();
    } else {
        QWidget::mousePressEvent(e);
    }
}

void MediaPreviewWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging) {
        move(e->globalPosition().toPoint() - m_dragPos);
        e->accept();
    } else {
        QWidget::mouseMoveEvent(e);
    }
}

void MediaPreviewWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        m_dragging = false;
    QWidget::mouseReleaseEvent(e);
}
