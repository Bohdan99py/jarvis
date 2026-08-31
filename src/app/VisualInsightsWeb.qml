import QtQuick
import QtWebEngine

// ============================================================
// VisualInsightsWeb.qml — поверхность показа на WebEngine.
//
// Отдельным файлом, а не веткой внутри панели: import QtWebEngine
// падает, если модуль не задеплоен, и в сборке без него не
// загрузилась бы вся панель целиком. Loader создаёт этот файл
// только когда WebEngine действительно есть.
// ============================================================

WebEngineView {
    id: web

    // qrc:/web/ — база, относительно которой страница резолвит
    // <script src='mermaid.min.js'> (см. buildMermaidHtml).
    Component.onCompleted: loadHtml(visualCtl.html, "qrc:/web/")

    Connections {
        target: visualCtl
        function onCurrentChanged() { web.loadHtml(visualCtl.html, "qrc:/web/") }

        // Мост: скрипт приходит из C++, результат уходит обратно.
        // Логика опроса с токенами и таймаутами остаётся там же,
        // где была отлажена.
        function onRunJsRequested(callId, script) {
            web.runJavaScript(script, function(result) {
                visualCtl.jsResult(callId, result === undefined || result === null
                                           ? "" : String(result))
            })
        }
    }
}
