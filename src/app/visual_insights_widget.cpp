// ============================================================
// visual_insights_widget.cpp — Interactive Attachments Side Panel
// ============================================================

#include "visual_insights_widget.h"
#include "jarvis_paths.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QBuffer>
#include <QDesktopServices>
#include <QUrl>
#include <QDebug>

#ifdef JARVIS_HAS_WEBENGINE
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#else
#include <QSvgWidget>
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QTemporaryFile>
#include <QDir>
#endif

static const QString kBtnStyle = QStringLiteral(
    "QPushButton { background: rgba(102,252,241,0.08); color: #66FCF1; "
    "border: 1px solid rgba(102,252,241,0.2); border-radius: 4px; "
    "font-size: 13px; padding: 2px 8px; } "
    "QPushButton:hover { background: rgba(102,252,241,0.22); }"
    "QPushButton:disabled { color: rgba(102,252,241,0.2); border-color: rgba(102,252,241,0.08); }");

static const QString kNavBtnStyle = QStringLiteral(
    "QPushButton { background: rgba(102,252,241,0.08); color: #66FCF1; "
    "border: 1px solid rgba(102,252,241,0.2); border-radius: 4px; "
    "font-size: 12px; padding: 2px 6px; } "
    "QPushButton:hover { background: rgba(102,252,241,0.22); } "
    "QPushButton:disabled { color: rgba(102,252,241,0.15); border-color: rgba(102,252,241,0.06); }");

VisualInsightsWidget::VisualInsightsWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(0);
    setStyleSheet(QStringLiteral(
        "VisualInsightsWidget { "
        "  background: rgba(8,10,18,250); "
        "  border-left: 1px solid rgba(0,212,255,0.25); }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(4);

    // ── Header ──
    auto* header = new QHBoxLayout();
    header->setSpacing(6);

    m_closeBtn = new QPushButton(QStringLiteral("\342\234\225"), this);
    m_closeBtn->setFixedSize(28, 28);
    m_closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: rgba(255,80,80,0.15); color: #ff5050; "
        "border: 1px solid rgba(255,80,80,0.3); border-radius: 4px; "
        "font-size: 14px; font-weight: bold; } "
        "QPushButton:hover { background: rgba(255,80,80,0.35); }"));
    connect(m_closeBtn, &QPushButton::clicked,
            this, &VisualInsightsWidget::slideClose);
    header->addWidget(m_closeBtn);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setStyleSheet(QStringLiteral(
        "color: #00d4ff; font-size: 13px; font-weight: bold; "
        "letter-spacing: 1px; background: transparent;"));
    header->addWidget(m_titleLabel, 1);

    m_prevBtn = new QPushButton(QStringLiteral("\342\227\200"), this); // ◀
    m_prevBtn->setFixedHeight(26);
    m_prevBtn->setStyleSheet(kNavBtnStyle);
    m_prevBtn->setToolTip(QStringLiteral("Previous attachment"));
    connect(m_prevBtn, &QPushButton::clicked, this, [this]() {
        if (m_historyIndex > 0) { --m_historyIndex; renderCurrent(); }
    });
    header->addWidget(m_prevBtn);

    m_nextBtn = new QPushButton(QStringLiteral("\342\226\266"), this); // ▶
    m_nextBtn->setFixedHeight(26);
    m_nextBtn->setStyleSheet(kNavBtnStyle);
    m_nextBtn->setToolTip(QStringLiteral("Next attachment"));
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() {
        if (m_historyIndex >= 0 && m_historyIndex < m_history.size() - 1) {
            ++m_historyIndex; renderCurrent();
        }
    });
    header->addWidget(m_nextBtn);

    m_folderBtn = new QPushButton(QStringLiteral("\U0001F4C1"), this); // 📁
    m_folderBtn->setFixedHeight(26);
    m_folderBtn->setStyleSheet(kBtnStyle);
    m_folderBtn->setToolTip(QStringLiteral("Show in folder"));
    m_folderBtn->setVisible(false);
    connect(m_folderBtn, &QPushButton::clicked, this, [this]() {
        if (m_historyIndex < 0 || m_historyIndex >= m_history.size()) return;
        const QString fp = m_history[m_historyIndex].filePath;
        if (fp.isEmpty()) return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(fp).absolutePath()));
    });
    header->addWidget(m_folderBtn);

    m_saveBtn = new QPushButton(QStringLiteral("Save"), this);
    m_saveBtn->setFixedHeight(28);
    m_saveBtn->setStyleSheet(kBtnStyle);
    connect(m_saveBtn, &QPushButton::clicked,
            this, &VisualInsightsWidget::onSaveClicked);
    header->addWidget(m_saveBtn);

    root->addLayout(header);

    // ── Diagram area ──
#ifdef JARVIS_HAS_WEBENGINE
    m_webView = new QWebEngineView(this);
    m_webView->setStyleSheet(QStringLiteral("background: transparent;"));
    m_webView->page()->setBackgroundColor(QColor(8, 10, 18));
    auto* settings = m_webView->page()->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    root->addWidget(m_webView, 1);
#else
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(false);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical   { background: rgba(11,12,16,200); width:  8px; }"
        "QScrollBar:horizontal { background: rgba(11,12,16,200); height: 8px; }"
        "QScrollBar::handle:vertical   { background: rgba(0,212,255,0.3); border-radius: 4px; }"
        "QScrollBar::handle:horizontal { background: rgba(0,212,255,0.3); border-radius: 4px; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }"));

    m_svgWidget = new QSvgWidget(this);
    m_svgWidget->setStyleSheet(QStringLiteral("background: transparent;"));
    m_svgWidget->setVisible(false);

    m_imageLabel = new QLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet(QStringLiteral("background: transparent; color: #66FCF1;"));
    m_imageLabel->setVisible(false);

    m_scrollArea->setWidget(m_svgWidget);
    root->addWidget(m_scrollArea, 1);
#endif

    // ── Slide animation ──
    m_slideAnim = new QPropertyAnimation(this, "panelWidth", this);
    m_slideAnim->setDuration(250);
    m_slideAnim->setEasingCurve(QEasingCurve::OutCubic);

    updateNavControls();
}

int  VisualInsightsWidget::panelWidth() const   { return width(); }
void VisualInsightsWidget::setPanelWidth(int w) { setFixedWidth(w); }

// ============================================================
//  History plumbing
// ============================================================

void VisualInsightsWidget::pushAndRender(HistoryEntry entry)
{
    m_history.append(std::move(entry));
    m_historyIndex = m_history.size() - 1;
    renderCurrent();
    slideOpen();
}

void VisualInsightsWidget::renderCurrent()
{
    if (m_historyIndex < 0 || m_historyIndex >= m_history.size()) return;
    const HistoryEntry& e = m_history[m_historyIndex];

    m_titleLabel->setText(QStringLiteral("%1  (%2/%3)")
        .arg(e.title).arg(m_historyIndex + 1).arg(m_history.size()));

    switch (e.kind) {
    case Kind::Mermaid:
#ifdef JARVIS_HAS_WEBENGINE
        m_webView->setHtml(buildMermaidHtml(e.mermaidSource));
#else
    {
        m_svgData.clear();
        m_imageLabel->setVisible(false);
        m_svgWidget->setVisible(true);
        m_scrollArea->setWidget(m_svgWidget);
        QProcess mmdc;
        QTemporaryFile tmp;
        tmp.setAutoRemove(true);
        tmp.setFileTemplate(QDir::tempPath() + QStringLiteral("/jarvis_mmd_XXXXXX.mmd"));
        if (tmp.open()) {
            tmp.write(e.mermaidSource.toUtf8());
            tmp.flush();
            const QString outSvg = QDir::tempPath() + QStringLiteral("/jarvis_panel.svg");
            mmdc.start(QStringLiteral("mmdc"), {
                QStringLiteral("-i"), tmp.fileName(),
                QStringLiteral("-o"), outSvg,
                QStringLiteral("-b"), QStringLiteral("transparent"),
                QStringLiteral("-t"), QStringLiteral("dark")});
            if (mmdc.waitForStarted(3000) && mmdc.waitForFinished(15000)
                && mmdc.exitCode() == 0) {
                QFile f(outSvg);
                if (f.open(QIODevice::ReadOnly)) {
                    m_svgData = f.readAll();
                    f.close();
                }
                QFile::remove(outSvg);
            }
        }
        updateSvgDisplay();
    }
#endif
        break;

    case Kind::Svg:
#ifdef JARVIS_HAS_WEBENGINE
    {
        QString html = QStringLiteral(
            "<!DOCTYPE html><html><head>"
            "<style>body{margin:0;background:#080a12;display:flex;"
            "justify-content:center;align-items:center;min-height:100vh;}"
            "svg{max-width:100%;height:auto;}</style></head><body>");
        html += QString::fromUtf8(e.svgData);
        html += QStringLiteral("</body></html>");
        m_webView->setHtml(html);
    }
#else
        m_svgData = e.svgData;
        m_imageLabel->setVisible(false);
        m_svgWidget->setVisible(true);
        m_scrollArea->setWidget(m_svgWidget);
        updateSvgDisplay();
#endif
        break;

    case Kind::Image:
#ifdef JARVIS_HAS_WEBENGINE
        m_webView->setHtml(buildImageHtml(e.image));
#else
    {
        m_svgData.clear();
        m_svgWidget->setVisible(false);
        m_imageLabel->setVisible(true);
        m_scrollArea->setWidget(m_imageLabel);
        const int availW = qMax(200, m_scrollArea->viewport()->width() - 8);
        QPixmap pm = QPixmap::fromImage(e.image);
        if (pm.width() > availW)
            pm = pm.scaledToWidth(availW, Qt::SmoothTransformation);
        m_imageLabel->setPixmap(pm);
        m_imageLabel->resize(pm.size());
    }
#endif
        break;

    case Kind::FileRef:
#ifdef JARVIS_HAS_WEBENGINE
        m_webView->setHtml(buildFileCardHtml(e.filePath, e.title));
#else
    {
        m_svgData.clear();
        m_svgWidget->setVisible(false);
        m_imageLabel->setVisible(true);
        m_scrollArea->setWidget(m_imageLabel);
        m_imageLabel->setPixmap(QPixmap());
        m_imageLabel->setText(QStringLiteral("\U0001F4C4  %1\n\n%2")
            .arg(QFileInfo(e.filePath).fileName(), e.filePath));
        m_imageLabel->resize(m_imageLabel->sizeHint());
    }
#endif
        break;
    }

    updateNavControls();
}

void VisualInsightsWidget::updateNavControls()
{
    const bool hasHistory = !m_history.isEmpty();
    m_prevBtn->setEnabled(hasHistory && m_historyIndex > 0);
    m_nextBtn->setEnabled(hasHistory && m_historyIndex < m_history.size() - 1);
    const bool hasFile = hasHistory && m_historyIndex >= 0
                       && !m_history[m_historyIndex].filePath.isEmpty();
    m_folderBtn->setVisible(hasFile);
}

// ============================================================
//  Public entry points — each adds a NEW history entry
// ============================================================

void VisualInsightsWidget::showMermaid(const QString& mermaidSource)
{
    HistoryEntry e;
    e.kind = Kind::Mermaid;
    e.title = QStringLiteral("Diagram");
    e.mermaidSource = mermaidSource;
    pushAndRender(std::move(e));
}

void VisualInsightsWidget::showSvg(const QByteArray& svgData,
                                    const QString& mermaidSource)
{
    if (!mermaidSource.isEmpty()) {
        showMermaid(mermaidSource);
        return;
    }
    HistoryEntry e;
    e.kind = Kind::Svg;
    e.title = QStringLiteral("Diagram");
    e.svgData = svgData;
    pushAndRender(std::move(e));
}

void VisualInsightsWidget::showDiagram(const QImage& image)
{
    HistoryEntry e;
    e.kind = Kind::Image;
    e.title = QStringLiteral("Image");
    e.image = image;
    pushAndRender(std::move(e));
}

void VisualInsightsWidget::showImageFile(const QString& filePath)
{
    QImage img(filePath);
    if (img.isNull()) {
        showFileRef(filePath, QFileInfo(filePath).fileName());
        return;
    }
    HistoryEntry e;
    e.kind = Kind::Image;
    e.title = QFileInfo(filePath).fileName();
    e.image = img;
    e.filePath = filePath;
    pushAndRender(std::move(e));
}

void VisualInsightsWidget::showFileRef(const QString& filePath, const QString& title)
{
    HistoryEntry e;
    e.kind = Kind::FileRef;
    e.title = title.isEmpty() ? QFileInfo(filePath).fileName() : title;
    e.filePath = filePath;
    pushAndRender(std::move(e));
}

// ============================================================
//  Reopen — bring back the panel without adding a new entry
// ============================================================

void VisualInsightsWidget::reopen()
{
    if (m_history.isEmpty()) return;

    // Already open (mid-slide counts as open too) — toggle it closed,
    // matching the toolbar button's usual "press again to hide" feel.
    if (isVisible() && width() > 0) {
        slideClose();
        return;
    }

    if (m_historyIndex < 0) m_historyIndex = m_history.size() - 1;
    renderCurrent();
    slideOpen();
}

// ============================================================
//  Slide open / close
// ============================================================

void VisualInsightsWidget::slideOpen()
{
    setVisible(true);
    m_slideAnim->stop();
    m_slideAnim->setStartValue(width());
    m_slideAnim->setEndValue(DEFAULT_WIDTH);
    m_slideAnim->start();
}

void VisualInsightsWidget::slideClose()
{
    m_slideAnim->stop();
    m_slideAnim->setStartValue(width());
    m_slideAnim->setEndValue(0);
    connect(m_slideAnim, &QPropertyAnimation::finished, this, [this]() {
        if (width() == 0) setVisible(false);
    }, Qt::UniqueConnection);
    m_slideAnim->start();
}

void VisualInsightsWidget::clear()
{
#ifdef JARVIS_HAS_WEBENGINE
    m_webView->setHtml(
        QStringLiteral("<html><body style='background:#080a12'></body></html>"));
#else
    m_svgData.clear();
    m_svgWidget->load(QByteArray());
    m_imageLabel->clear();
#endif
    slideClose();
}

// ============================================================
//  Save
// ============================================================

void VisualInsightsWidget::onSaveClicked()
{
    if (m_historyIndex < 0 || m_historyIndex >= m_history.size()) return;
    const HistoryEntry& e = m_history[m_historyIndex];

    // Already a real file on disk — saving it again makes no sense, just
    // reveal where it already lives.
    if (e.kind == Kind::FileRef) {
        if (!e.filePath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(e.filePath).absolutePath()));
        return;
    }

    const QString ts = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd_HHmmss"));
    const QString base = JarvisPaths::subPath(QStringLiteral("visuals"))
        + QStringLiteral("/diagram_") + ts;

    if (!e.mermaidSource.isEmpty()) {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save Diagram"),
            base + QStringLiteral(".mmd"),
            QStringLiteral("Mermaid (*.mmd);;All (*)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write(e.mermaidSource.toUtf8());
            f.close();
        }
    } else if (!e.image.isNull()) {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save Diagram"),
            base + QStringLiteral(".png"),
            QStringLiteral("PNG (*.png);;All (*)"));
        if (!path.isEmpty())
            e.image.save(path);
    } else if (!e.svgData.isEmpty()) {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save Diagram"),
            base + QStringLiteral(".svg"),
            QStringLiteral("SVG (*.svg);;All (*)"));
        if (!path.isEmpty()) {
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(e.svgData);
                f.close();
            }
        }
    }
}

// ============================================================
//  WebEngine-only: HTML builders
// ============================================================

#ifdef JARVIS_HAS_WEBENGINE

QString VisualInsightsWidget::buildMermaidHtml(const QString& mermaidCode) const
{
    QString escaped = mermaidCode;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('`'),  QStringLiteral("\\`"));
    escaped.replace(QLatin1Char('\''), QStringLiteral("\\'"));

    QString h;
    h += QStringLiteral("<!DOCTYPE html><html><head><meta charset='utf-8'>\n");

    h += QStringLiteral("<style>\n"
        "* { margin:0; padding:0; box-sizing:border-box; }\n"
        "body { background:#080a12; color:#c0c8d8; font-family:'Segoe UI',sans-serif;\n"
        "  display:flex; flex-direction:column; align-items:center; padding:16px; min-height:100vh; }\n"
        "#dc { width:100%; display:flex; justify-content:center; overflow:auto; }\n"
        "#dc svg { max-width:100%; height:auto; cursor:pointer; }\n"
        ".node rect,.node circle,.node polygon { cursor:pointer; transition:filter .2s; }\n"
        ".node:hover rect,.node:hover circle,.node:hover polygon {\n"
        "  filter:brightness(1.4) drop-shadow(0 0 8px rgba(0,212,255,0.6)); }\n"
        "#tt { display:none; position:fixed; background:rgba(0,20,40,0.95);\n"
        "  border:1px solid #00d4ff; border-radius:8px; padding:10px 14px;\n"
        "  color:#66FCF1; font-size:13px; max-width:320px; z-index:9999;\n"
        "  pointer-events:none; box-shadow:0 4px 20px rgba(0,0,0,0.5); }\n"
        "#tt .ti { color:#00d4ff; font-weight:bold; font-size:14px; margin-bottom:4px; }\n"
        "#tt .tx { color:#a0b0c0; font-size:12px; white-space:pre-line; }\n"
        "#zc { position:fixed; bottom:16px; right:16px; display:flex; gap:6px; z-index:100; }\n"
        ".zb { background:rgba(0,212,255,0.1); color:#00d4ff;\n"
        "  border:1px solid rgba(0,212,255,0.3); border-radius:6px;\n"
        "  width:36px; height:36px; font-size:18px; cursor:pointer;\n"
        "  display:flex; align-items:center; justify-content:center; }\n"
        ".zb:hover { background:rgba(0,212,255,0.25); }\n"
        "#zl { color:#66FCF1; font-size:12px; align-self:center; min-width:40px; text-align:center; }\n"
        "</style>\n");

    h += QStringLiteral("<script type='module'>\n"
        "import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.esm.min.mjs';\n"
        "mermaid.initialize({\n"
        "  startOnLoad:false, theme:'dark',\n"
        "  themeVariables:{\n"
        "    darkMode:true, primaryColor:'#0d2137', primaryTextColor:'#66FCF1',\n"
        "    primaryBorderColor:'#00d4ff', lineColor:'#00d4ff',\n"
        "    secondaryColor:'#0a1929', tertiaryColor:'#061220',\n"
        "    background:'#080a12', mainBkg:'#0d2137',\n"
        "    nodeBorder:'#00d4ff', clusterBkg:'#0a1929',\n"
        "    titleColor:'#66FCF1', actorBorder:'#00d4ff',\n"
        "    actorBkg:'#0d2137', actorTextColor:'#66FCF1',\n"
        "    actorLineColor:'#00586a', signalColor:'#66FCF1',\n"
        "    signalTextColor:'#c0c8d8', labelBoxBkgColor:'#0d2137',\n"
        "    labelBoxBorderColor:'#00586a', labelTextColor:'#c0c8d8',\n"
        "    loopTextColor:'#66FCF1', noteBorderColor:'#00586a',\n"
        "    noteBkgColor:'#0a1929', noteTextColor:'#c0c8d8',\n"
        "    activationBorderColor:'#00d4ff', activationBkgColor:'#0d2137',\n"
        "    sequenceNumberColor:'#080a12'\n"
        "  },\n"
        "  securityLevel:'loose', fontFamily:'Consolas,monospace', fontSize:14\n"
        "});\n"
        "const dc=document.getElementById('dc');\n"
        "const code=`");
    h += escaped;
    h += QStringLiteral("`;\n"
        "try {\n"
        "  const {svg}=await mermaid.render('jd',code);\n"
        "  dc.innerHTML=svg; setup();\n"
        "} catch(e) {\n"
        "  dc.innerHTML='<pre style=\"color:#ff5050;padding:20px\">'+e.message+'</pre>';\n"
        "}\n"

        "function parseDiagram(src) {\n"
        "  const info={};\n"
        "  const lines=src.split('\\n');\n"
        "  for(const ln of lines){\n"
        "    let m;\n"
        "    if(m=ln.match(/participant\\s+(\\S+)\\s+as\\s+(.+)/i))\n"
        "      info[m[1].trim()]={role:'Participant',desc:m[2].trim()};\n"
        "    else if(m=ln.match(/^\\s*(\\S+?)\\s*-+>>?\\+?\\s*(\\S+?)\\s*:\\s*(.+)/))\n"
        "      info[m[1].trim()+'->'+m[2].trim()]={role:'Message',desc:m[3].trim(),from:m[1].trim(),to:m[2].trim()};\n"
        "    else if(m=ln.match(/Note\\s+(?:over|left of|right of)\\s+([^:]+):\\s*(.+)/i))\n"
        "      info['note:'+m[1].trim()]={role:'Note',desc:m[2].trim(),about:m[1].trim()};\n"
        "    else if(m=ln.match(/^\\s*(\\w+)\\s*[\\[\\(\\{]([^\\]\\)\\}]+)[\\]\\)\\}]/))\n"
        "      info[m[1].trim()]={role:'Node',desc:m[2].trim()};\n"
        "    else if(m=ln.match(/^\\s*(\\w+)\\s*-->\\|?([^|]*?)\\|?\\s*(\\w+)/))\n"
        "      if(m[2].trim()) info[m[1]+'->'+m[3]]={role:'Edge',desc:m[2].trim()};\n"
        "  }\n"
        "  return info;\n"
        "}\n"

        "function setup() {\n"
        "  const info=parseDiagram(code);\n"
        "  const tt=document.getElementById('tt');\n"
        "  const els=dc.querySelectorAll('.node,.actor,.note,.messageText,.activation,.loopText,.labelText,.edgeLabel');\n"
        "  els.forEach(el=>{\n"
        "    el.style.cursor='pointer';\n"
        "    el.addEventListener('click',e=>{\n"
        "      e.stopPropagation();\n"
        "      const texts=[...el.querySelectorAll('text,tspan,.nodeLabel,span,foreignObject')]\n"
        "        .map(t=>t.textContent.trim()).filter(t=>t.length>0);\n"
        "      const label=texts.join(' ')||'Element';\n"
        "      let detail=null;\n"
        "      for(const[k,v] of Object.entries(info)){\n"
        "        if(label.includes(v.desc)||v.desc.includes(label)||label.includes(k)){\n"
        "          detail=v; break;\n"
        "        }\n"
        "      }\n"
        "      if(!detail && el.id){\n"
        "        for(const[k,v] of Object.entries(info)){\n"
        "          if(el.id.includes(k)||k.includes(el.id)){detail=v;break;}\n"
        "        }\n"
        "      }\n"
        "      const ti=tt.querySelector('.ti');\n"
        "      const tx=tt.querySelector('.tx');\n"
        "      if(detail){\n"
        "        ti.textContent=detail.role+': '+detail.desc;\n"
        "        let extra='';\n"
        "        if(detail.from) extra+='From: '+detail.from+' \\u2192 To: '+detail.to+'\\n';\n"
        "        if(detail.about) extra+='About: '+detail.about+'\\n';\n"
        "        tx.textContent=extra||detail.desc;\n"
        "      } else {\n"
        "        ti.textContent=label;\n"
        "        tx.textContent='Click on messages or participants for details';\n"
        "      }\n"
        "      tt.style.display='block';\n"
        "      tt.style.left=Math.min(e.clientX+12,innerWidth-340)+'px';\n"
        "      tt.style.top=Math.min(e.clientY-50,innerHeight-100)+'px';\n"
        "    });\n"
        "  });\n"
        "  document.addEventListener('click',()=>{tt.style.display='none';});\n"
        "  let sc=1;\n"
        "  const sv=dc.querySelector('svg');\n"
        "  if(sv){\n"
        "    sv.style.transformOrigin='top center';\n"
        "    const uz=()=>{sv.style.transform='scale('+sc+')';document.getElementById('zl').textContent=Math.round(sc*100)+'%';};\n"
        "    document.getElementById('zi').addEventListener('click',()=>{sc=Math.min(sc+0.2,5);uz();});\n"
        "    document.getElementById('zo').addEventListener('click',()=>{sc=Math.max(sc-0.2,0.3);uz();});\n"
        "    document.getElementById('zf').addEventListener('click',()=>{sc=1;uz();});\n"
        "  }\n"
        "}\n"
        "</script>\n");

    h += QStringLiteral("</head><body>\n"
        "<div id='dc'>Loading diagram...</div>\n"
        "<div id='tt'><div class='ti'></div><div class='tx'></div></div>\n"
        "<div id='zc'>\n"
        "  <button class='zb' id='zo'>-</button>\n"
        "  <span id='zl'>100%</span>\n"
        "  <button class='zb' id='zf'>Fit</button>\n"
        "  <button class='zb' id='zi'>+</button>\n"
        "</div>\n"
        "</body></html>");

    return h;
}

QString VisualInsightsWidget::buildImageHtml(const QImage& image) const
{
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    image.save(&buf, "PNG");
    buf.close();
    const QString b64 = QString::fromLatin1(ba.toBase64());
    return QStringLiteral(
        "<!DOCTYPE html><html><head>"
        "<style>body{margin:0;background:#080a12;display:flex;"
        "justify-content:center;align-items:center;min-height:100vh;}"
        "img{max-width:100%;height:auto;}</style></head>"
        "<body><img src='data:image/png;base64,")
        + b64 + QStringLiteral("'/></body></html>");
}

QString VisualInsightsWidget::buildFileCardHtml(const QString& filePath, const QString& title) const
{
    const QFileInfo fi(filePath);
    return QStringLiteral(
        "<!DOCTYPE html><html><head><style>"
        "body{margin:0;background:#080a12;color:#c0c8d8;font-family:'Segoe UI',sans-serif;"
        "display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:100vh;}"
        ".card{background:#0d2137;border:1px solid rgba(0,212,255,0.3);border-radius:14px;"
        "padding:32px 40px;text-align:center;max-width:80%;}"
        ".icon{font-size:48px;margin-bottom:12px;}"
        ".name{color:#66FCF1;font-size:16px;font-weight:bold;word-break:break-all;}"
        ".path{color:#7a8aa0;font-size:12px;margin-top:8px;word-break:break-all;}"
        ".hint{color:#4a6070;font-size:11px;margin-top:16px;}"
        "</style></head><body>"
        "<div class='card'>"
        "<div class='icon'>\U0001F4C4</div>"
        "<div class='name'>%1</div>"
        "<div class='path'>%2</div>"
        "<div class='hint'>Use the \U0001F4C1 button above to open its folder</div>"
        "</div></body></html>")
        .arg((title.isEmpty() ? fi.fileName() : title).toHtmlEscaped(),
             filePath.toHtmlEscaped());
}

#else // SVG fallback

void VisualInsightsWidget::updateSvgDisplay()
{
    if (!m_svgData.isEmpty()) {
        m_svgWidget->load(m_svgData);
        QSvgRenderer renderer(m_svgData);
        if (renderer.isValid()) {
            const int availW = qMax(200, m_scrollArea->viewport()->width() - 8);
            double scale = static_cast<double>(availW) / qMax(1, renderer.defaultSize().width());
            scale = qBound(0.3, scale, 3.0);
            m_svgWidget->setFixedSize(renderer.defaultSize() * scale);
        }
        m_svgWidget->setVisible(true);
    }
}

#endif
