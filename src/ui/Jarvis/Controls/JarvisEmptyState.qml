import QtQuick
import QtQuick.Layouts
import Jarvis.Theme

// ============================================================
// JarvisEmptyState — «здесь пока пусто».
//
// Пустой список выглядит ровно как несработавший экран, поэтому
// пустое состояние обязано объяснить себя словами: что случилось
// и что делать дальше. Одной иконки недостаточно.
//
// hint — моноширинная строка под текстом: путь к папке, имя файла,
// команда. Показывать её текстом абзаца нельзя — пути ломаются
// переносами и их невозможно прочитать по буквам.
// ============================================================

ColumnLayout {
    id: control

    property string glyph: "\u25C7"
    property string title: ""
    property string description: ""
    property string hint: ""

    // Слот под действие: кнопка «Открыть папку», «Обновить».
    default property alias action: actionRow.data

    spacing: Theme.spaceMd

    Item { Layout.fillHeight: true }

    Text {
        Layout.alignment: Qt.AlignHCenter
        text: control.glyph
        color: Theme.onSurfaceDim
        font.pixelSize: Type.display
    }

    Text {
        Layout.fillWidth: true
        Layout.maximumWidth: 460
        Layout.alignment: Qt.AlignHCenter
        text: control.title
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.onSurface
        font.family: Type.family
        font.pixelSize: Type.title
        font.weight: Font.DemiBold
    }

    Text {
        Layout.fillWidth: true
        Layout.maximumWidth: 460
        Layout.alignment: Qt.AlignHCenter
        visible: control.description.length > 0
        text: control.description
        color: Theme.onSurfaceVariant
        font.family: Type.family
        font.pixelSize: Type.caption
        lineHeight: Type.lineHeightBody
        lineHeightMode: Text.ProportionalHeight
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }

    Text {
        Layout.fillWidth: true
        Layout.maximumWidth: 460
        Layout.alignment: Qt.AlignHCenter
        visible: control.hint.length > 0
        text: control.hint
        color: Theme.onSurfaceDim
        font.family: Type.familyMono
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideMiddle
    }

    Row {
        id: actionRow
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Theme.spaceSm
        spacing: Theme.spaceSm
    }

    Item { Layout.fillHeight: true }
}
