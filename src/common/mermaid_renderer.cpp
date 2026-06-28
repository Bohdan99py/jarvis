// ============================================================
// mermaid_renderer.cpp — Mermaid Diagram → PNG Renderer
// ============================================================

#include "mermaid_renderer.h"
#include "jarvis_paths.h"

#include <QProcess>
#include <QTemporaryFile>
#include <QDir>
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
        return {false, {}, {}, QStringLiteral("Empty Mermaid source")};

    const QString name = outputName.isEmpty()
        ? QStringLiteral("diagram_%1.png")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8))
        : outputName;

    const QString outPath = outputDir() + QStringLiteral("/") + name;
    QDir().mkpath(outputDir());

    MermaidRenderResult result;

    if (isMmdcAvailable())
        result = renderViaMmdc(mermaidSource, outPath);
    else
        result = renderFallbackPlaceholder(mermaidSource, outPath);

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
    // Write Mermaid source to a temp .mmd file
    QTemporaryFile tmpFile;
    tmpFile.setAutoRemove(true);
    tmpFile.setFileTemplate(QDir::tempPath() + QStringLiteral("/jarvis_mermaid_XXXXXX.mmd"));
    if (!tmpFile.open()) {
        return {false, {}, {},
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
        QStringLiteral("-w"), QStringLiteral("1024"),
    });

    if (!mmdc.waitForStarted(5000)) {
        return {false, {}, {},
                QStringLiteral("mmdc failed to start: %1")
                    .arg(mmdc.errorString())};
    }

    if (!mmdc.waitForFinished(RENDER_TIMEOUT_MS)) {
        mmdc.kill();
        return {false, {}, {}, QStringLiteral("mmdc timed out")};
    }

    if (mmdc.exitCode() != 0) {
        const QString err = QString::fromUtf8(mmdc.readAll());
        qWarning() << "[MermaidRenderer] mmdc error:" << err;
        return {false, {}, {},
                QStringLiteral("mmdc exit %1: %2")
                    .arg(mmdc.exitCode()).arg(err.left(200))};
    }

    QImage img(outPath);
    if (img.isNull()) {
        return {false, outPath, {},
                QStringLiteral("mmdc produced unreadable output")};
    }

    qDebug() << "[MermaidRenderer] Rendered via mmdc:" << outPath
             << img.size();
    return {true, outPath, img, {}};
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
        return {false, {}, {},
                QStringLiteral("Failed to save fallback PNG")};
    }

    qDebug() << "[MermaidRenderer] Fallback placeholder:" << outPath;
    return {true, outPath, img, {}};
}
