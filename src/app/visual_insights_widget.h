#pragma once
// ============================================================
// visual_insights_widget.h — Dashboard for Rendered Diagrams
//
// Displays the last rendered Mermaid diagram inline in the
// main window. Listens for diagramGenerated(QImage) signals
// from the Telegram gateway.
//
// Features:
//   - Auto-scales diagram to fit the widget
//   - Click to save diagram as PNG
//   - Smooth fade-in animation on new diagram
//   - Cyberpunk-themed to match the main UI
// ============================================================

#include <QWidget>
#include <QImage>
#include <QLabel>
#include <QPushButton>

class VisualInsightsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VisualInsightsWidget(QWidget* parent = nullptr);

public slots:
    void showDiagram(const QImage& image);
    void clear();

private slots:
    void onSaveClicked();

private:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void updateScaledPixmap();

    QImage       m_currentImage;
    QPixmap      m_scaledPixmap;
    QLabel*      m_titleLabel  = nullptr;
    QLabel*      m_hintLabel   = nullptr;
    QPushButton* m_saveBtn     = nullptr;
    int          m_diagramCount = 0;
};
