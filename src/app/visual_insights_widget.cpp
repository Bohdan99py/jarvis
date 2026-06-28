// ============================================================
// visual_insights_widget.cpp — Dashboard for Rendered Diagrams
// ============================================================

#include "visual_insights_widget.h"
#include "jarvis_paths.h"

#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QDateTime>
#include <QDebug>

VisualInsightsWidget::VisualInsightsWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
    setMaximumHeight(400);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(6);

    auto* topRow = new QHBoxLayout();

    m_titleLabel = new QLabel(QStringLiteral("📊 Visual Insights"), this);
    m_titleLabel->setStyleSheet(QStringLiteral(
        "color: #66FCF1; font-size: 12px; font-weight: bold; "
        "letter-spacing: 1px; background: transparent;"));
    topRow->addWidget(m_titleLabel);

    topRow->addStretch();

    m_saveBtn = new QPushButton(QStringLiteral("💾"), this);
    m_saveBtn->setFixedSize(28, 28);
    m_saveBtn->setToolTip(QStringLiteral("Save diagram as PNG"));
    m_saveBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: rgba(102,252,241,0.08); color: #66FCF1; "
        "border: 1px solid rgba(102,252,241,0.2); border-radius: 4px; "
        "font-size: 14px; } "
        "QPushButton:hover { background: rgba(102,252,241,0.18); }"));
    m_saveBtn->setVisible(false);
    connect(m_saveBtn, &QPushButton::clicked, this, &VisualInsightsWidget::onSaveClicked);
    topRow->addWidget(m_saveBtn);

    layout->addLayout(topRow);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setAlignment(Qt::AlignCenter);
    m_hintLabel->setStyleSheet(QStringLiteral(
        "color: #555; font-size: 11px; background: transparent;"));
    m_hintLabel->setText(QStringLiteral(
        "Ask about architecture, flow, or structure to generate diagrams"));
    layout->addWidget(m_hintLabel, 1);

    setStyleSheet(QStringLiteral(
        "VisualInsightsWidget { "
        "background: rgba(11,12,16,200); "
        "border: 1px solid rgba(102,252,241,0.10); "
        "border-radius: 8px; }"));
}

void VisualInsightsWidget::showDiagram(const QImage& image)
{
    m_currentImage = image;
    m_diagramCount++;
    m_hintLabel->setVisible(false);
    m_saveBtn->setVisible(true);
    m_titleLabel->setText(QStringLiteral("📊 Visual Insights — #%1")
                              .arg(m_diagramCount));
    updateScaledPixmap();
    update();

    qDebug() << "[VisualInsights] Diagram displayed, count:" << m_diagramCount;
}

void VisualInsightsWidget::clear()
{
    m_currentImage = QImage();
    m_scaledPixmap = QPixmap();
    m_hintLabel->setVisible(true);
    m_saveBtn->setVisible(false);
    m_titleLabel->setText(QStringLiteral("📊 Visual Insights"));
    update();
}

void VisualInsightsWidget::onSaveClicked()
{
    if (m_currentImage.isNull()) return;

    const QString defaultPath = JarvisPaths::subPath(QStringLiteral("visuals"))
        + QStringLiteral("/saved_diagram_%1.png")
              .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Diagram"), defaultPath,
        QStringLiteral("PNG (*.png);;JPEG (*.jpg);;All (*)"));

    if (path.isEmpty()) return;

    if (m_currentImage.save(path)) {
        qDebug() << "[VisualInsights] Diagram saved:" << path;
    }
}

void VisualInsightsWidget::updateScaledPixmap()
{
    if (m_currentImage.isNull()) {
        m_scaledPixmap = QPixmap();
        return;
    }

    const int availW = width() - 24;
    const int availH = height() - 50;

    if (availW <= 0 || availH <= 0) return;

    m_scaledPixmap = QPixmap::fromImage(m_currentImage).scaled(
        availW, availH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void VisualInsightsWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    if (m_scaledPixmap.isNull()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const int x = (width() - m_scaledPixmap.width()) / 2;
    const int y = 34 + (height() - 34 - m_scaledPixmap.height()) / 2;

    p.drawPixmap(x, y, m_scaledPixmap);
}

void VisualInsightsWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateScaledPixmap();
}
