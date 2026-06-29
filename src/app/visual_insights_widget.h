#pragma once
// ============================================================
// visual_insights_widget.h — Interactive Diagram Side Panel
//
// Full-height right panel. When Qt WebEngine is available,
// renders Mermaid via embedded Mermaid.js in Chromium.
// Otherwise falls back to QSvgWidget for vector SVG display.
// ============================================================

#include <QWidget>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QPropertyAnimation>

#ifdef JARVIS_HAS_WEBENGINE
class QWebEngineView;
#else
class QSvgWidget;
#endif

class VisualInsightsWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int panelWidth READ panelWidth WRITE setPanelWidth)

public:
    explicit VisualInsightsWidget(QWidget* parent = nullptr);

    int  panelWidth() const;
    void setPanelWidth(int w);

public slots:
    void showMermaid(const QString& mermaidSource);
    void showSvg(const QByteArray& svgData, const QString& mermaidSource);
    void showDiagram(const QImage& image);
    void slideOpen();
    void slideClose();
    void clear();

private slots:
    void onSaveClicked();

private:
#ifdef JARVIS_HAS_WEBENGINE
    QString buildMermaidHtml(const QString& mermaidCode) const;
    QString buildImageHtml(const QImage& image) const;
    QWebEngineView* m_webView = nullptr;
#else
    void updateSvgDisplay();
    QSvgWidget*  m_svgWidget  = nullptr;
    QLabel*      m_imageLabel = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QByteArray   m_svgData;
#endif

    QLabel*             m_titleLabel = nullptr;
    QPushButton*        m_closeBtn   = nullptr;
    QPushButton*        m_saveBtn    = nullptr;
    QPropertyAnimation* m_slideAnim  = nullptr;

    QString  m_currentMermaidSource;
    QImage   m_currentRasterImage;
    int      m_diagramCount = 0;

    static constexpr int DEFAULT_WIDTH = 520;
};
