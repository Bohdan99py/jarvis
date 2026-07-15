#pragma once
// ============================================================
// visual_insights_widget.h — Interactive Attachments Side Panel
//
// Full-height right panel. When Qt WebEngine is available,
// renders Mermaid via embedded Mermaid.js in Chromium.
// Otherwise falls back to QSvgWidget for vector SVG display.
//
// Keeps every diagram/image/generated-file shown this session in a
// history list — Prev/Next navigate it, and the toolbar's "Attachments"
// button (see MainWindow) reopens the panel at wherever it was left
// instead of losing everything the moment the user closes it.
// ============================================================

#include <QWidget>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QPropertyAnimation>
#include <QVector>

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

    bool hasHistory() const { return !m_history.isEmpty(); }

public slots:
    void showMermaid(const QString& mermaidSource);
    void showSvg(const QByteArray& svgData, const QString& mermaidSource);
    void showDiagram(const QImage& image);

    // An actual image file on disk (a photo/picture found via search) —
    // unlike showDiagram(), this carries the file path through so the
    // folder button works and the entry can be told apart from generated
    // in-memory images.
    void showImageFile(const QString& filePath);

    // A generated file with no inline visual (e.g. a .kicad_sch) — shows a
    // simple file card with an "open folder" action instead of rendering
    // content, but still joins the same history so it's reachable via
    // Prev/Next like any other attachment.
    void showFileRef(const QString& filePath, const QString& title);

    // Reopen the panel at whatever was last shown, or toggle it closed if
    // it's already open. Distinguishes "bring back what I had" (this) from
    // the showXxx() slots above, which always add a NEW history entry.
    void reopen();

    void slideOpen();
    void slideClose();
    void clear();

private slots:
    void onSaveClicked();

private:
    enum class Kind { Mermaid, Svg, Image, FileRef };
    struct HistoryEntry {
        Kind       kind = Kind::Mermaid;
        QString    title;
        QString    mermaidSource;
        QByteArray svgData;
        QImage     image;
        QString    filePath;   // set for FileRef; empty for everything else
    };

    void pushAndRender(HistoryEntry entry);
    void renderCurrent();
    void updateNavControls();

#ifdef JARVIS_HAS_WEBENGINE
    QString buildMermaidHtml(const QString& mermaidCode) const;
    QString buildImageHtml(const QImage& image) const;
    QString buildFileCardHtml(const QString& filePath, const QString& title) const;
    QWebEngineView* m_webView = nullptr;
#else
    void updateSvgDisplay();
    QSvgWidget*  m_svgWidget  = nullptr;
    QLabel*      m_imageLabel = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QByteArray   m_svgData;
#endif

    QLabel*             m_titleLabel = nullptr;
    QPushButton*        m_prevBtn    = nullptr;
    QPushButton*        m_nextBtn    = nullptr;
    QPushButton*        m_folderBtn  = nullptr;
    QPushButton*        m_closeBtn   = nullptr;
    QPushButton*        m_saveBtn    = nullptr;
    QPropertyAnimation* m_slideAnim  = nullptr;

    QVector<HistoryEntry> m_history;
    int                   m_historyIndex = -1;

    static constexpr int DEFAULT_WIDTH = 520;
};
