// ============================================================
// visual_insights_widget.cpp — Interactive Diagram Side Panel
// ============================================================

#include "visual_insights_widget.h"
#include "jarvis_paths.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QDateTime>
#include <QFile>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QBuffer>
#include <QDebug>

static const QString kBtnStyle = QStringLiteral(
    "QPushButton { background: rgba(102,252,241,0.08); color: #66FCF1; "
    "border: 1px solid rgba(102,252,241,0.2); border-radius: 4px; "
    "font-size: 13px; padding: 2px 8px; } "
    "QPushButton:hover { background: rgba(102,252,241,0.22); }");

VisualInsightsWidget::VisualInsightsWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(0);
    setStyleSheet(QStringLiteral(
        "VisualInsightsWidget { "
        "  background: rgba(8,10,18,250); "
        "  border-left: 1px solid rgba(0,212,255,0.25); "
        "}"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(4);

    auto* header = new QHBoxLayout();
    header->setSpacing(6);

    m_closeBtn = new QPushButton(QStringLiteral("✕"), this);
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

    m_saveBtn = new QPushButton(QStringLiteral("Save"), this);
    m_saveBtn->setFixedHeight(28);
    m_saveBtn->setStyleSheet(kBtnStyle);
    connect(m_saveBtn, &QPushButton::clicked,
            this, &VisualInsightsWidget::onSaveClicked);
    header->addWidget(m_saveBtn);

    root->addLayout(header);

    m_webView = new QWebEngineView(this);
    m_webView->setStyleSheet(QStringLiteral("background: transparent;"));
    m_webView->page()->setBackgroundColor(QColor(8, 10, 18));

    auto* settings = m_webView->page()->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);

    root->addWidget(m_webView, 1);

    m_slideAnim = new QPropertyAnimation(this, "panelWidth", this);
    m_slideAnim->setDuration(250);
    m_slideAnim->setEasingCurve(QEasingCurve::OutCubic);
}

int  VisualInsightsWidget::panelWidth() const   { return width(); }
void VisualInsightsWidget::setPanelWidth(int w) { setFixedWidth(w); }

// ============================================================
//  Show Mermaid source — rendered via Mermaid.js in Chromium
// ============================================================

void VisualInsightsWidget::showMermaid(const QString& mermaidSource)
{
    m_currentMermaidSource = mermaidSource;
    m_currentRasterImage   = QImage();
    m_diagramCount++;
    m_titleLabel->setText(
        QStringLiteral("Diagram #%1").arg(m_diagramCount));

    m_webView->setHtml(buildMermaidHtml(mermaidSource));
    slideOpen();
}

void VisualInsightsWidget::showSvg(const QByteArray& svgData,
                                    const QString& mermaidSource)
{
    if (!mermaidSource.isEmpty()) {
        showMermaid(mermaidSource);
        return;
    }

    m_currentMermaidSource.clear();
    m_currentRasterImage = QImage();
    m_diagramCount++;
    m_titleLabel->setText(
        QStringLiteral("Diagram #%1").arg(m_diagramCount));

    QString html = QStringLiteral(
        "<!DOCTYPE html><html><head>"
        "<style>body{margin:0;background:#080a12;display:flex;"
        "justify-content:center;align-items:center;min-height:100vh;}"
        "svg{max-width:100%;height:auto;}</style></head><body>");
    html += QString::fromUtf8(svgData);
    html += QStringLiteral("</body></html>");

    m_webView->setHtml(html);
    slideOpen();
}

void VisualInsightsWidget::showDiagram(const QImage& image)
{
    m_currentMermaidSource.clear();
    m_currentRasterImage = image;
    m_diagramCount++;
    m_titleLabel->setText(
        QStringLiteral("Diagram #%1").arg(m_diagramCount));

    m_webView->setHtml(buildImageHtml(image));
    slideOpen();
}

// ============================================================
//  Build Mermaid.js HTML page
// ============================================================

QString VisualInsightsWidget::buildMermaidHtml(const QString& mermaidCode) const
{
    QString escaped = mermaidCode;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('`'),  QStringLiteral("\\`"));
    escaped.replace(QLatin1Char('\''), QStringLiteral("\\'"));

    // Build HTML as concatenated QLatin1String segments to avoid
    // MSVC raw-string-literal issues with deeply nested JS/CSS.

    QString h;
    h += QStringLiteral("<!DOCTYPE html><html><head><meta charset='utf-8'>\n");

    // ── CSS ──
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

    // ── Mermaid.js import + render ──
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

        // ── Parse Mermaid source to build node descriptions ──
        "function parseDiagram(src) {\n"
        "  const info={};\n"
        "  const lines=src.split('\\n');\n"
        "  for(const ln of lines){\n"
        "    let m;\n"
        // participant A as Peer A (Client)
        "    if(m=ln.match(/participant\\s+(\\S+)\\s+as\\s+(.+)/i))\n"
        "      info[m[1].trim()]={role:'Participant',desc:m[2].trim()};\n"
        // A->>B: Message text  or  A-->>B: text
        "    else if(m=ln.match(/^\\s*(\\S+?)\\s*-+>>?\\+?\\s*(\\S+?)\\s*:\\s*(.+)/))\n"
        "      info[m[1].trim()+'->'+m[2].trim()]={role:'Message',desc:m[3].trim(),from:m[1].trim(),to:m[2].trim()};\n"
        // Note over A,B: text
        "    else if(m=ln.match(/Note\\s+(?:over|left of|right of)\\s+([^:]+):\\s*(.+)/i))\n"
        "      info['note:'+m[1].trim()]={role:'Note',desc:m[2].trim(),about:m[1].trim()};\n"
        // NodeId[Label] or NodeId(Label) or NodeId{Label}
        "    else if(m=ln.match(/^\\s*(\\w+)\\s*[\\[\\(\\{]([^\\]\\)\\}]+)[\\]\\)\\}]/))\n"
        "      info[m[1].trim()]={role:'Node',desc:m[2].trim()};\n"
        // A-->B or A-->|label|B
        "    else if(m=ln.match(/^\\s*(\\w+)\\s*-->\\|?([^|]*?)\\|?\\s*(\\w+)/))\n"
        "      if(m[2].trim()) info[m[1]+'->'+m[3]]={role:'Edge',desc:m[2].trim()};\n"
        "  }\n"
        "  return info;\n"
        "}\n"

        "function setup() {\n"
        "  const info=parseDiagram(code);\n"
        "  const tt=document.getElementById('tt');\n"

        // Find all clickable SVG elements
        "  const els=dc.querySelectorAll('.node,.actor,.note,.messageText,.activation,.loopText,.labelText,.edgeLabel');\n"
        "  els.forEach(el=>{\n"
        "    el.style.cursor='pointer';\n"
        "    el.addEventListener('click',e=>{\n"
        "      e.stopPropagation();\n"

        // Collect all visible text inside the clicked element
        "      const texts=[...el.querySelectorAll('text,tspan,.nodeLabel,span,foreignObject')]\n"
        "        .map(t=>t.textContent.trim()).filter(t=>t.length>0);\n"
        "      const label=texts.join(' ')||'Element';\n"

        // Try to find this element in our parsed info
        "      let detail=null;\n"
        "      for(const[k,v] of Object.entries(info)){\n"
        "        if(label.includes(v.desc)||v.desc.includes(label)||label.includes(k)){\n"
        "          detail=v; break;\n"
        "        }\n"
        "      }\n"

        // Also try matching by element ID
        "      if(!detail && el.id){\n"
        "        for(const[k,v] of Object.entries(info)){\n"
        "          if(el.id.includes(k)||k.includes(el.id)){detail=v;break;}\n"
        "        }\n"
        "      }\n"

        // Build tooltip content
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

        // Zoom controls
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
        "  <button class='zb' id='zo'>−</button>\n"
        "  <span id='zl'>100%</span>\n"
        "  <button class='zb' id='zf'>Fit</button>\n"
        "  <button class='zb' id='zi'>+</button>\n"
        "</div>\n"
        "</body></html>");

    return h;
}

// ============================================================
//  Build HTML for raster image fallback
// ============================================================

QString VisualInsightsWidget::buildImageHtml(const QImage& image) const
{
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    image.save(&buf, "PNG");
    buf.close();

    const QString base64 = QString::fromLatin1(ba.toBase64());

    return QStringLiteral(
        "<!DOCTYPE html><html><head>"
        "<style>body{margin:0;background:#080a12;display:flex;"
        "justify-content:center;align-items:center;min-height:100vh;}"
        "img{max-width:100%;height:auto;}</style></head>"
        "<body><img src='data:image/png;base64,")
        + base64
        + QStringLiteral("'/></body></html>");
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
    m_currentMermaidSource.clear();
    m_currentRasterImage = QImage();
    m_webView->setHtml(
        QStringLiteral("<html><body style='background:#080a12'></body></html>"));
    slideClose();
}

// ============================================================
//  Save
// ============================================================

void VisualInsightsWidget::onSaveClicked()
{
    const QString ts = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd_HHmmss"));
    const QString base = JarvisPaths::subPath(QStringLiteral("visuals"))
        + QStringLiteral("/diagram_") + ts;

    if (!m_currentMermaidSource.isEmpty()) {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save Diagram"),
            base + QStringLiteral(".mmd"),
            QStringLiteral("Mermaid (*.mmd);;HTML (*.html);;All (*)"));
        if (path.isEmpty()) return;

        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (path.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive))
                f.write(buildMermaidHtml(m_currentMermaidSource).toUtf8());
            else
                f.write(m_currentMermaidSource.toUtf8());
            f.close();
        }
    } else if (!m_currentRasterImage.isNull()) {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save Diagram"),
            base + QStringLiteral(".png"),
            QStringLiteral("PNG (*.png);;All (*)"));
        if (!path.isEmpty())
            m_currentRasterImage.save(path);
    }
}
