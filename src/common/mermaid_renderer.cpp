// ============================================================
// mermaid_renderer.cpp — Mermaid Diagram → PNG Renderer
// ============================================================

#include "mermaid_renderer.h"
#include "jarvis_paths.h"

#include <QProcess>
#include <QTemporaryFile>
#include <QDir>
#include <QFileInfo>
#include <QUuid>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QRegularExpression>
#include <QtConcurrent>
#include <QDebug>

MermaidRenderer::MermaidRenderer(QObject* parent)
    : QObject(parent)
{
}

// ============================================================
//  Output directory
// ============================================================

QString MermaidRenderer::outputDir()
{
    return JarvisPaths::subPath(QStringLiteral("visuals"));
}

// ============================================================
//  mmdc availability check
// ============================================================

bool MermaidRenderer::isMmdcAvailable()
{
    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.start(QStringLiteral("mmdc"), {QStringLiteral("--version")});
    if (!probe.waitForStarted(3000)) return false;
    probe.waitForFinished(5000);
    return probe.exitCode() == 0;
}

// ============================================================
//  Tag parsing: <diagram>...</diagram>
// ============================================================

QString MermaidRenderer::extractDiagramBlock(const QString& text)
{
    static const QRegularExpression re(
        QStringLiteral("<diagram>\\s*([\\s\\S]*?)\\s*</diagram>"),
        QRegularExpression::CaseInsensitiveOption);

    auto match = re.match(text);
    if (match.hasMatch())
        return match.captured(1).trimmed();
    return {};
}

QString MermaidRenderer::stripDiagramBlock(const QString& text)
{
    static const QRegularExpression re(
        QStringLiteral("<diagram>[\\s\\S]*?</diagram>"),
        QRegularExpression::CaseInsensitiveOption);

    QString result = text;
    result.remove(re);
    return result.trimmed();
}

// ============================================================
//  Synchronous render
// ============================================================

MermaidRenderResult MermaidRenderer::render(const QString& mermaidSource,
                                             const QString& outputName)
{
    if (mermaidSource.trimmed().isEmpty())
        return {false, {}, {}, {}, mermaidSource, QStringLiteral("Empty Mermaid source")};

    const QString baseName = outputName.isEmpty()
        ? QStringLiteral("diagram_%1")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8))
        : QFileInfo(outputName).completeBaseName();

    QDir().mkpath(outputDir());

    MermaidRenderResult result;

    if (isMmdcAvailable()) {
        // Prefer SVG output for vector quality
        const QString svgPath = outputDir() + QStringLiteral("/") + baseName + QStringLiteral(".svg");
        result = renderViaMmdc(mermaidSource, svgPath);
    } else {
        const QString pngPath = outputDir() + QStringLiteral("/") + baseName + QStringLiteral(".png");
        result = renderFallbackPlaceholder(mermaidSource, pngPath);
    }

    result.mermaidSource = mermaidSource;
    return result;
}

// ============================================================
//  Async render — runs on thread pool
// ============================================================

void MermaidRenderer::renderAsync(const QString& mermaidSource,
                                   const QString& outputName)
{
    QtConcurrent::run([this, mermaidSource, outputName]() {
        MermaidRenderResult result = render(mermaidSource, outputName);
        QMetaObject::invokeMethod(this, [this, result]() {
            emit renderFinished(result);
        }, Qt::QueuedConnection);
    });
}

// ============================================================
//  Render via mmdc CLI
// ============================================================

MermaidRenderResult MermaidRenderer::renderViaMmdc(const QString& source,
                                                     const QString& outPath)
{
    QTemporaryFile tmpFile;
    tmpFile.setAutoRemove(true);
    tmpFile.setFileTemplate(QDir::tempPath() + QStringLiteral("/jarvis_mermaid_XXXXXX.mmd"));
    if (!tmpFile.open()) {
        return {false, {}, {}, {}, source,
                QStringLiteral("Failed to create temp file: %1")
                    .arg(tmpFile.errorString())};
    }
    tmpFile.write(source.toUtf8());
    tmpFile.flush();

    QProcess mmdc;
    mmdc.setProcessChannelMode(QProcess::MergedChannels);
    mmdc.start(QStringLiteral("mmdc"), {
        QStringLiteral("-i"), tmpFile.fileName(),
        QStringLiteral("-o"), outPath,
        QStringLiteral("-b"), QStringLiteral("transparent"),
        QStringLiteral("-t"), QStringLiteral("dark"),
    });

    if (!mmdc.waitForStarted(5000)) {
        return {false, {}, {}, {}, source,
                QStringLiteral("mmdc failed to start: %1")
                    .arg(mmdc.errorString())};
    }

    if (!mmdc.waitForFinished(RENDER_TIMEOUT_MS)) {
        mmdc.kill();
        return {false, {}, {}, {}, source, QStringLiteral("mmdc timed out")};
    }

    if (mmdc.exitCode() != 0) {
        const QString err = QString::fromUtf8(mmdc.readAll());
        qWarning() << "[MermaidRenderer] mmdc error:" << err;
        return {false, {}, {}, {}, source,
                QStringLiteral("mmdc exit %1: %2")
                    .arg(mmdc.exitCode()).arg(err.left(200))};
    }

    // Read the SVG file
    QFile svgFile(outPath);
    if (svgFile.open(QIODevice::ReadOnly)) {
        QByteArray svgContent = svgFile.readAll();
        svgFile.close();

        if (!svgContent.isEmpty()) {
            qDebug() << "[MermaidRenderer] SVG rendered via mmdc:" << outPath
                     << "size:" << svgContent.size() << "bytes";
            return {true, outPath, {}, svgContent, source, {}};
        }
    }

    // SVG read failed — try loading as raster image fallback
    QImage img(outPath);
    if (!img.isNull()) {
        qDebug() << "[MermaidRenderer] Raster rendered via mmdc:" << outPath;
        return {true, outPath, img, {}, source, {}};
    }

    return {false, outPath, {}, {}, source,
            QStringLiteral("mmdc produced unreadable output")};
}

// ============================================================
//  ASCII diagram detection + extraction
// ============================================================

bool MermaidRenderer::containsAsciiDiagram(const QString& text)
{
    // Look for code blocks that contain box-drawing or arrow characters
    static const QRegularExpression codeBlockRe(
        QStringLiteral("```[^`]*```"),
        QRegularExpression::DotMatchesEverythingOption);

    auto it = codeBlockRe.globalMatch(text);
    while (it.hasNext()) {
        const QString block = it.next().captured(0);
        int indicators = 0;
        if (block.contains(QStringLiteral("──")))    ++indicators;
        if (block.contains(QStringLiteral("│")))      ++indicators;
        if (block.contains(QStringLiteral("┌")))      ++indicators;
        if (block.contains(QStringLiteral("└")))      ++indicators;
        if (block.contains(QStringLiteral("─>")))     ++indicators;
        if (block.contains(QStringLiteral("<─")))     ++indicators;
        if (block.contains(QStringLiteral("→")))      ++indicators;
        if (block.contains(QStringLiteral("←")))      ++indicators;
        if (block.contains(QStringLiteral("|")))      ++indicators;
        if (block.contains(QStringLiteral("---")))    ++indicators;
        if (block.contains(QStringLiteral("===")))    ++indicators;
        if (block.contains(QStringLiteral("+-")))     ++indicators;
        if (indicators >= 3) return true;
    }
    return false;
}

QPair<QString, QString> MermaidRenderer::extractAsciiDiagram(const QString& text)
{
    // Find the longest code block that looks like a diagram
    static const QRegularExpression codeBlockRe(
        QStringLiteral("```(?:\\w*\\n)?([^`]+)```"),
        QRegularExpression::DotMatchesEverythingOption);

    QString bestBlock;
    int bestScore = 0;
    QRegularExpressionMatch bestMatch;

    auto it = codeBlockRe.globalMatch(text);
    while (it.hasNext()) {
        auto match = it.next();
        const QString content = match.captured(1);
        int score = 0;
        if (content.contains(QStringLiteral("──")))  score += 2;
        if (content.contains(QStringLiteral("│")))    score += 2;
        if (content.contains(QStringLiteral("→")))    score += 2;
        if (content.contains(QStringLiteral("←")))    score += 2;
        if (content.contains(QStringLiteral("─>")))   score += 2;
        if (content.contains(QStringLiteral("|")))    ++score;
        if (content.contains(QStringLiteral("---")))  ++score;
        score += content.length() / 100;

        if (score > bestScore) {
            bestScore = score;
            bestBlock = content;
            bestMatch = match;
        }
    }

    if (bestBlock.isEmpty())
        return {QString(), text};

    // Build the remaining text with the diagram block removed
    QString remaining = text;
    remaining.replace(bestMatch.captured(0), QString());
    remaining = remaining.trimmed();

    return {bestBlock, remaining};
}

// ============================================================
//  Render ASCII art as styled PNG
// ============================================================

MermaidRenderResult MermaidRenderer::renderAsciiArt(const QString& asciiText,
                                                     const QString& outputName)
{
    if (asciiText.trimmed().isEmpty())
        return {false, {}, {}, {}, {}, QStringLiteral("Empty ASCII art")};

    const QString name = outputName.isEmpty()
        ? QStringLiteral("ascii_diagram_%1.png")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8))
        : outputName;

    const QString outPath = outputDir() + QStringLiteral("/") + name;
    QDir().mkpath(outputDir());

    const QFont monoFont(QStringLiteral("Consolas"), 13);
    const QFontMetrics fm(monoFont);

    const QStringList lines = asciiText.split(QLatin1Char('\n'));
    const int lineHeight = fm.lineSpacing();

    int maxWidth = 0;
    for (const QString& line : lines)
        maxWidth = qMax(maxWidth, fm.horizontalAdvance(line));

    const int pad = 32;
    const int headerH = 40;
    const int imgWidth  = maxWidth + pad * 2;
    const int imgHeight = headerH + lines.size() * lineHeight + pad * 2;

    QImage img(qMax(imgWidth, 400), qMax(imgHeight, 200),
               QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(8, 10, 18));

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // Border
    p.setPen(QPen(QColor(0, 212, 255, 50), 2));
    p.drawRoundedRect(4, 4, img.width() - 8, img.height() - 8, 10, 10);

    // Header
    p.setFont(QFont(QStringLiteral("Segoe UI"), 12, QFont::Bold));
    p.setPen(QColor(0, 212, 255));
    p.drawText(pad, pad - 4, QStringLiteral("J.A.R.V.I.S. — Diagram"));

    // Separator
    p.setPen(QPen(QColor(0, 212, 255, 40), 1));
    p.drawLine(pad, headerH + 4, img.width() - pad, headerH + 4);

    // Diagram text
    p.setFont(monoFont);
    p.setPen(QColor(102, 252, 241)); // cyan for structural characters

    int y = headerH + pad;
    for (const QString& line : lines) {
        p.drawText(pad, y + fm.ascent(), line);
        y += lineHeight;
    }

    p.end();

    if (!img.save(outPath, "PNG")) {
        return {false, {}, {}, {}, {},
                QStringLiteral("Failed to save ASCII diagram PNG")};
    }

    qDebug() << "[MermaidRenderer] ASCII art rendered:" << outPath
             << img.size();
    return {true, outPath, img, {}, {}, {}};
}

// ============================================================
//  Fallback: render styled text placeholder as PNG
// ============================================================

MermaidRenderResult MermaidRenderer::renderFallbackPlaceholder(
    const QString& source, const QString& outPath)
{
    const int width  = 800;
    const int margin = 24;
    const QFont titleFont(QStringLiteral("Consolas"), 14, QFont::Bold);
    const QFont codeFont(QStringLiteral("Consolas"), 11);
    const QFont hintFont(QStringLiteral("Segoe UI"), 9);

    // Measure text height
    QFontMetrics codeFm(codeFont);
    const QStringList lines = source.split(QLatin1Char('\n'));
    const int lineHeight = codeFm.lineSpacing();
    const int codeHeight = lines.size() * lineHeight;
    const int height = margin + 30 + 16 + codeHeight + 16 + 20 + margin;

    QImage img(width, qMax(height, 200), QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(11, 12, 16));

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // Border
    p.setPen(QPen(QColor(102, 252, 241, 40), 2));
    p.drawRoundedRect(4, 4, width - 8, img.height() - 8, 12, 12);

    int y = margin;

    // Title
    p.setFont(titleFont);
    p.setPen(QColor(102, 252, 241));
    p.drawText(margin, y + codeFm.ascent(), QStringLiteral("📊 Mermaid Diagram"));
    y += 30 + 8;

    // Separator
    p.setPen(QPen(QColor(102, 252, 241, 30), 1));
    p.drawLine(margin, y, width - margin, y);
    y += 8;

    // Code lines
    p.setFont(codeFont);
    p.setPen(QColor(197, 198, 199));
    for (const QString& line : lines) {
        p.drawText(margin + 8, y + codeFm.ascent(), line);
        y += lineHeight;
    }

    y += 8;

    // Hint
    p.setFont(hintFont);
    p.setPen(QColor(136, 146, 164));
    p.drawText(margin, y + codeFm.ascent(),
               QStringLiteral("Install mmdc (npm i -g @mermaid-js/mermaid-cli) for rendered diagrams"));

    p.end();

    if (!img.save(outPath, "PNG")) {
        return {false, {}, {}, {}, source,
                QStringLiteral("Failed to save fallback PNG")};
    }

    qDebug() << "[MermaidRenderer] Fallback placeholder:" << outPath;
    return {true, outPath, img, {}, source, {}};
}
