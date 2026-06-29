#pragma once
// ============================================================
// visual_insights_widget.h — Interactive Diagram Side Panel
//
// Full-height right panel powered by QWebEngineView.
// Renders Mermaid diagrams via embedded Mermaid.js — vector
// quality, native browser zoom, clickable nodes with tooltips.
// Falls back to raster QImage display for ASCII art diagrams.
// ============================================================

#include <QWidget>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QPropertyAnimation>

class QWebEngineView;

class VisualInsightsWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int panelWidth READ panelWidth WRITE setPanelWidth)

public:
    explicit VisualInsightsWidget(QWidget* parent = nullptr);

    int  panelWidth() const;
    void setPanelWidth(int w);

public slots:
    // Preferred: render Mermaid source interactively via Mermaid.js
    void showMermaid(const QString& mermaidSource);

    // SVG from mmdc or other source
    void showSvg(const QByteArray& svgData, const QString& mermaidSource);

    // Raster fallback for ASCII art
    void showDiagram(const QImage& image);

    void slideOpen();
    void slideClose();
    void clear();

private slots:
    void onSaveClicked();

private:
    QString buildMermaidHtml(const QString& mermaidCode) const;
    QString buildImageHtml(const QImage& image) const;

    QWebEngineView*     m_webView    = nullptr;
    QLabel*             m_titleLabel = nullptr;
    QPushButton*        m_closeBtn   = nullptr;
    QPushButton*        m_saveBtn    = nullptr;
    QPropertyAnimation* m_slideAnim  = nullptr;

    QString  m_currentMermaidSource;
    QImage   m_currentRasterImage;
    int      m_diagramCount = 0;

    static constexpr int DEFAULT_WIDTH = 520;
};
