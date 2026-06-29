#pragma once
// ============================================================
// mermaid_renderer.h — Mermaid Diagram → PNG Renderer
//
// Takes a Mermaid syntax string and produces a PNG image via
// the mmdc CLI tool (Mermaid CLI). Falls back to a styled
// placeholder if mmdc is not installed.
//
// Output directory: JarvisPaths::subPath("visuals/")
// Thread safety: stateless — safe to call from any thread.
// ============================================================

#include <QObject>
#include <QString>
#include <QPair>
#include <QImage>

struct MermaidRenderResult
{
    bool    success = false;
    QString outputPath;        // absolute path to rendered file
    QImage  image;             // raster fallback (empty when SVG available)
    QByteArray svgData;        // raw SVG content (preferred over raster)
    QString mermaidSource;     // original Mermaid syntax for node parsing
    QString errorMessage;
};

class MermaidRenderer : public QObject
{
    Q_OBJECT

public:
    explicit MermaidRenderer(QObject* parent = nullptr);

    // Render Mermaid syntax to PNG. Blocks until done.
    MermaidRenderResult render(const QString& mermaidSource,
                               const QString& outputName = QString());

    // Async version — emits renderFinished when done
    void renderAsync(const QString& mermaidSource,
                     const QString& outputName = QString());

    // Check if mmdc CLI is available on PATH
    static bool isMmdcAvailable();

    // Extract <diagram>...</diagram> block from text.
    // Returns the Mermaid source, or empty if no tag found.
    static QString extractDiagramBlock(const QString& text);

    // Remove <diagram>...</diagram> from text, return remaining text
    static QString stripDiagramBlock(const QString& text);

    // Detect if text contains a code block with ASCII art (box-drawing, arrows).
    // Used as a fallback when LLM ignores the <diagram> instruction.
    static bool containsAsciiDiagram(const QString& text);

    // Extract the ASCII art code block content, return the rest as second element.
    static QPair<QString, QString> extractAsciiDiagram(const QString& text);

    // Render plain text (ASCII art) as a styled PNG image.
    MermaidRenderResult renderAsciiArt(const QString& asciiText,
                                       const QString& outputName = QString());

    static constexpr int RENDER_TIMEOUT_MS = 15000;

signals:
    void renderFinished(const MermaidRenderResult& result);

private:
    MermaidRenderResult renderViaMmdc(const QString& source,
                                      const QString& outPath);
    MermaidRenderResult renderFallbackPlaceholder(const QString& source,
                                                   const QString& outPath);

    static QString outputDir();
};
