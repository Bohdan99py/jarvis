import QtQuick
import Jarvis.Theme
import Jarvis.Controls

// ============================================================
// VisualInsightsSvg.qml — поверхность показа без WebEngine.
//
// Показывает то, что удалось получить в SVG. Интерактивности
// mermaid-страницы (зум, подсказки на узлах) здесь нет — её даёт
// только Chromium.
// ============================================================

Item {
    Image {
        anchors.fill: parent
        anchors.margins: Theme.spaceMd
        source: visualCtl.svgSource
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        // Без sourceSize SVG растрируется в свой номинальный размер
        // и на широкой панели выглядит мылом.
        sourceSize.width: width
        sourceSize.height: height
        visible: visualCtl.svgSource.length > 0
    }

    JarvisEmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spaceXl, 420)
        visible: visualCtl.svgSource.length === 0
        glyph: "◇"
        title: qsTr("Nothing to show")
        description: qsTr("This build has no WebEngine, so only diagrams "
                          + "exported to SVG can be displayed.")
    }
}
