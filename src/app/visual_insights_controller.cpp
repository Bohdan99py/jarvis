// ============================================================
// visual_insights_controller.cpp — см. visual_insights_controller.h
//
// Сборщики HTML и скрипт экспорта перенесены из
// visual_insights_widget.cpp дословно: это отлаженный код, и
// переезд панели в QML его не касается.
// ============================================================

#include "visual_insights_controller.h"

#include <QBuffer>
#include <QDesktopServices>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <QDebug>

namespace {
constexpr int kPollIntervalMs  = 150;
constexpr int kPollAttempts    = 80;        // 12 с — большие графы парсятся долго
constexpr int kMaxRasterPixels = 16000000;  // ~4000x4000, дальше масштаб режется
}

namespace {

// Kicks off the export on first call and reports progress after that.
// Returns "ready", "working", "pending" or "error: ...". The results are
// parked on window.__jarvisSvg / window.__jarvisPng for separate reads,
// because a 2x PNG data URL is far too big to keep re-returning while
// polling.
//
// The SVG is a clone, so the zoom transform and the panel-fit max-width
// don't leak into the saved file; width/height are pinned from the
// viewBox for viewers that ignore CSS, and an opaque rect goes underneath
// because the diagram's text is light.
QString exportScript(int maxPixels)
{
    return QStringLiteral(
        "(function(){\n"
        "  if (window.__jarvisExportState) return window.__jarvisExportState;\n"
        "  if (window.__jarvisDiagramState !== 'ready')\n"
        "    return window.__jarvisDiagramState || 'pending';\n"
        "  var s = document.querySelector('#dc svg');\n"
        "  if (!s) return 'pending';\n"
        "  window.__jarvisExportState = 'working';\n"
        "  try {\n"
        "    var c = s.cloneNode(true);\n"
        "    c.removeAttribute('style');\n"
        "    c.setAttribute('xmlns','http://www.w3.org/2000/svg');\n"
        "    c.setAttribute('xmlns:xlink','http://www.w3.org/1999/xlink');\n"
        "    var vb = (c.getAttribute('viewBox')||'').trim().split(/[\\s,]+/).map(Number);\n"
        "    var x=0,y=0,w=0,h=0;\n"
        "    if (vb.length===4 && vb.every(function(n){return isFinite(n);})) {\n"
        "      x=vb[0]; y=vb[1]; w=vb[2]; h=vb[3];\n"
        "    } else {\n"
        "      var r=s.getBoundingClientRect();\n"
        "      w=Math.ceil(r.width); h=Math.ceil(r.height);\n"
        "      c.setAttribute('viewBox','0 0 '+w+' '+h);\n"
        "    }\n"
        "    if (!(w>0 && h>0)) { window.__jarvisExportState=null; return 'pending'; }\n"
        "    c.setAttribute('width', w);\n"
        "    c.setAttribute('height', h);\n"
        "    var bg=document.createElementNS('http://www.w3.org/2000/svg','rect');\n"
        "    bg.setAttribute('x',x); bg.setAttribute('y',y);\n"
        "    bg.setAttribute('width',w); bg.setAttribute('height',h);\n"
        "    bg.setAttribute('fill','#080a12');\n"
        "    c.insertBefore(bg, c.firstChild);\n"
        "    var doc = '<?xml version=\"1.0\" encoding=\"UTF-8\"?>\\n'\n"
        "            + new XMLSerializer().serializeToString(c);\n"
        "    window.__jarvisSvg = doc;\n"
        // Rasterise through an <img>: the SVG is self-contained (Mermaid
        // inlines its <style>), so no external fetch is involved and the
        // canvas stays untainted.
        "    var scale = 2;\n"
        "    var budget = %1;\n"
        "    if (w*h*scale*scale > budget) scale = Math.max(1, Math.sqrt(budget/(w*h)));\n"
        "    var img = new Image();\n"
        "    img.onload = function(){\n"
        "      try {\n"
        "        var cv = document.createElement('canvas');\n"
        "        cv.width = Math.round(w*scale); cv.height = Math.round(h*scale);\n"
        "        var ctx = cv.getContext('2d');\n"
        "        ctx.fillStyle = '#080a12';\n"
        "        ctx.fillRect(0,0,cv.width,cv.height);\n"
        "        ctx.drawImage(img,0,0,cv.width,cv.height);\n"
        "        window.__jarvisPng = cv.toDataURL('image/png');\n"
        "      } catch(e) { window.__jarvisPng = ''; }\n"
        "      window.__jarvisExportState = 'ready';\n"
        "    };\n"
        // No PNG is not fatal — the SVG alone is still a real diagram.
        "    img.onerror = function(){\n"
        "      window.__jarvisPng = '';\n"
        "      window.__jarvisExportState = 'ready';\n"
        "    };\n"
        "    img.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(doc);\n"
        "  } catch(e) {\n"
        "    window.__jarvisExportState = 'error: ' + e.message;\n"
        "  }\n"
        "  return window.__jarvisExportState;\n"
        "})()").arg(maxPixels);
}

} // namespace

VisualInsightsController::VisualInsightsController(QObject* parent)
    : QObject(parent)
{
}

bool VisualInsightsController::webEngineAvailable() const
{
#ifdef JARVIS_HAS_WEBENGINE
    return true;
#else
    return false;
#endif
}

QString VisualInsightsController::title() const
{
    if (m_index < 0 || m_index >= m_history.size()) return {};
    return QStringLiteral("%1  (%2/%3)")
        .arg(m_history[m_index].title)
        .arg(m_index + 1)
        .arg(m_history.size());
}

bool VisualInsightsController::hasFile() const
{
    return m_index >= 0 && m_index < m_history.size()
        && !m_history[m_index].filePath.isEmpty();
}

QString VisualInsightsController::html() const
{
    if (m_index < 0 || m_index >= m_history.size()) return {};
    const HistoryEntry& e = m_history[m_index];

    switch (e.kind) {
    case Kind::Mermaid: return buildMermaidHtml(e.mermaidSource);
    case Kind::Svg:     return buildSvgHtml(e.svgData);
    case Kind::Image:   return buildImageHtml(e.image);
    case Kind::FileRef: return buildFileCardHtml(e.filePath, e.title);
    }
    return {};
}

QString VisualInsightsController::svgSource() const
{
    // Для сборки без WebEngine: data-URL, который QML Image покажет
    // напрямую, без временных файлов на диске.
    if (m_index < 0 || m_index >= m_history.size()) return {};
    const HistoryEntry& e = m_history[m_index];
    if (e.svgData.isEmpty()) return {};
    return QStringLiteral("data:image/svg+xml;base64,")
         + QString::fromLatin1(e.svgData.toBase64());
}

QString VisualInsightsController::buildSvgHtml(const QByteArray& svg) const
{
    return QStringLiteral(
        "<!DOCTYPE html><html><head>"
        "<style>body{margin:0;background:#080a12;display:flex;"
        "justify-content:center;align-items:center;min-height:100vh;}"
        "svg{max-width:100%;height:auto;}</style></head><body>")
        + QString::fromUtf8(svg)
        + QStringLiteral("</body></html>");
}

void VisualInsightsController::setOpen(bool on)
{
    if (m_open == on) return;
    m_open = on;
    emit openChanged();
}

void VisualInsightsController::pushAndShow(HistoryEntry entry)
{
    m_history.append(std::move(entry));
    m_index = m_history.size() - 1;

    // Новый показ обесценивает экспорт, который ещё летит от прошлой
    // страницы: его поздний ответ отсечётся по токену.
    ++m_renderToken;

    emit currentChanged();
    setOpen(true);
}

void VisualInsightsController::showMermaid(const QString& mermaidSource)
{
    pushAndShow({ Kind::Mermaid, QStringLiteral("Diagram"),
                  mermaidSource, {}, {}, {} });
}

void VisualInsightsController::showSvg(const QByteArray& svgData,
                                       const QString& mermaidSource)
{
    pushAndShow({ Kind::Svg, QStringLiteral("Diagram"),
                  mermaidSource, svgData, {}, {} });
}

void VisualInsightsController::showDiagram(const QImage& image)
{
    pushAndShow({ Kind::Image, QStringLiteral("Diagram"),
                  {}, {}, image, {} });
}

void VisualInsightsController::showImageFile(const QString& filePath)
{
    QImage img(filePath);
    if (img.isNull()) return;
    pushAndShow({ Kind::Image, QFileInfo(filePath).fileName(),
                  {}, {}, img, filePath });
}

void VisualInsightsController::showFileRef(const QString& filePath,
                                           const QString& title)
{
    pushAndShow({ Kind::FileRef,
                  title.isEmpty() ? QFileInfo(filePath).fileName() : title,
                  {}, {}, {}, filePath });
}

void VisualInsightsController::reopen()
{
    if (m_history.isEmpty()) return;
    setOpen(true);
}

void VisualInsightsController::prev()
{
    if (!canPrev()) return;
    --m_index;
    ++m_renderToken;
    emit currentChanged();
}

void VisualInsightsController::next()
{
    if (!canNext()) return;
    ++m_index;
    ++m_renderToken;
    emit currentChanged();
}

void VisualInsightsController::close()
{
    setOpen(false);
}

void VisualInsightsController::save()
{
    if (m_index < 0 || m_index >= m_history.size()) return;
    emit saveRequested(m_history[m_index].title);
}

void VisualInsightsController::openFolder()
{
    if (!hasFile()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(
        QFileInfo(m_history[m_index].filePath).absolutePath()));
}

// ── Мост к JS ─────────────────────────────────────────────

void VisualInsightsController::runJs(const QString& script,
                                     std::function<void(const QString&)> cb)
{
    const int id = m_nextCallId++;
    m_pendingJs.insert(id, std::move(cb));
    emit runJsRequested(id, script);
}

void VisualInsightsController::jsResult(int callId, const QString& value)
{
    // take(), а не value(): обработчик одноразовый, и оставленный в
    // таблице он держал бы захваченный callback экспорта до конца
    // жизни панели.
    auto cb = m_pendingJs.take(callId);
    if (cb) cb(value);
}

void VisualInsightsController::exportRendered(ExportCallback cb)
{
    if (!cb) return;

    if (m_index < 0 || m_index >= m_history.size()
        || m_history[m_index].kind != Kind::Mermaid
        || !webEngineAvailable()) {
        cb({}, {});
        return;
    }
    pollExport(std::move(cb), m_renderToken, kPollAttempts);
}

void VisualInsightsController::pollExport(ExportCallback cb, quint64 token,
                                          int attemptsLeft)
{
    // Панель уже показывает другое — этот ответ протух.
    if (token != m_renderToken) { cb({}, {}); return; }

    runJs(exportScript(kMaxRasterPixels),
        [this, cb, token, attemptsLeft](const QString& s) {
            if (token != m_renderToken) { cb({}, {}); return; }

            if (s == QLatin1String("ready")) { fetchExportResults(cb, token); return; }

            if (s.startsWith(QLatin1String("error"))) {
                qWarning() << "[VisualInsights] diagram export failed:" << s;
                cb({}, {});
                return;
            }
            if (attemptsLeft <= 1) {
                qWarning() << "[VisualInsights] diagram export timed out, state:" << s;
                cb({}, {});
                return;
            }
            QTimer::singleShot(kPollIntervalMs, this,
                [this, cb, token, attemptsLeft]() {
                    pollExport(cb, token, attemptsLeft - 1);
                });
        });
}

void VisualInsightsController::fetchExportResults(ExportCallback cb, quint64 token)
{
    runJs(QStringLiteral("window.__jarvisSvg || ''"),
        [this, cb, token](const QString& sv) {
            if (token != m_renderToken) { cb({}, {}); return; }
            const QByteArray svg = sv.toUtf8();

            runJs(QStringLiteral("window.__jarvisPng || ''"),
                [cb, svg](const QString& dataUrl) {
                    QImage raster;
                    const int comma = dataUrl.indexOf(QLatin1Char(','));
                    if (dataUrl.startsWith(QLatin1String("data:image/png;base64,"))
                        && comma > 0) {
                        const QByteArray png = QByteArray::fromBase64(
                            dataUrl.mid(comma + 1).toLatin1());
                        raster.loadFromData(png, "PNG");
                    }
                    cb(svg, raster);
                });
        });
}

QString VisualInsightsController::buildMermaidHtml(const QString& mermaidCode) const
{
    // The source goes into a JS template literal, so only backslash, backtick
    // and ${ can break out of it. Escaping the single quote as well (as this
    // used to) would corrupt any diagram label containing an apostrophe.
    QString escaped = mermaidCode;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('`'),  QStringLiteral("\\`"));
    escaped.replace(QLatin1Char('$'),  QStringLiteral("\\$"));

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

    // Mermaid is vendored into the binary (assets/web + assets/mermaid.qrc)
    // and pulled in over qrc:, not from a CDN — the panel has to draw
    // diagrams with no internet connection. setHtml() below supplies the
    // qrc:/web/ base URL this relative src resolves against.
    h += QStringLiteral("<script src='mermaid.min.js'></script>\n");

    h += QStringLiteral("<script>\n"
        "window.__jarvisDiagramState='pending';\n"
        "document.addEventListener('DOMContentLoaded', function(){\n"
        "const dc=document.getElementById('dc');\n"
        "if (typeof mermaid === 'undefined') {\n"
        "  window.__jarvisDiagramState='error: mermaid.js failed to load';\n"
        "  dc.innerHTML='<pre style=\"color:#ff5050;padding:20px\">mermaid.js failed to load</pre>';\n"
        "  return;\n"
        "}\n"
        "mermaid.initialize({\n"
        "  startOnLoad:false, theme:'dark',\n"
        // htmlLabels:false makes Mermaid emit real <text> nodes instead of
        // <foreignObject>+HTML. Chromium draws both, but only <text>
        // survives export: a foreignObject SVG shows empty boxes in
        // Inkscape, in QSvgRenderer and in most image viewers.
        "  htmlLabels:false, flowchart:{htmlLabels:false}, class:{htmlLabels:false},\n"
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
        "    sequenceNumberColor:'#080a12', edgeLabelBackground:'#080a12'\n"
        "  },\n"
        "  securityLevel:'loose', fontFamily:'Consolas,monospace', fontSize:14\n"
        "});\n"
        "const code=`");
    h += escaped;
    h += QStringLiteral("`;\n"
        "mermaid.render('jd',code).then(function(r){\n"
        "  dc.innerHTML=r.svg;\n"
        "  window.__jarvisDiagramState='ready';\n"
        "  setup();\n"
        "}).catch(function(e){\n"
        "  window.__jarvisDiagramState='error: '+e.message;\n"
        "  dc.innerHTML='<pre style=\"color:#ff5050;padding:20px\">'+e.message+'</pre>';\n"
        "});\n"

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
        "});\n"   // end DOMContentLoaded
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

QString VisualInsightsController::buildImageHtml(const QImage& image) const
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

QString VisualInsightsController::buildFileCardHtml(const QString& filePath, const QString& title) const
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


QString VisualInsightsController::currentMermaid() const
{
    if (m_index < 0 || m_index >= m_history.size()) return {};
    return m_history[m_index].mermaidSource;
}

QImage VisualInsightsController::currentImage() const
{
    if (m_index < 0 || m_index >= m_history.size()) return {};
    return m_history[m_index].image;
}

QByteArray VisualInsightsController::currentSvgData() const
{
    if (m_index < 0 || m_index >= m_history.size()) return {};
    return m_history[m_index].svgData;
}

QString VisualInsightsController::currentFilePath() const
{
    if (m_index < 0 || m_index >= m_history.size()) return {};
    return m_history[m_index].filePath;
}
