import QtQuick
import QtQuick.Layouts
import Jarvis.Theme

// ============================================================
// JarvisChatBubble — одно сообщение ленты.
//
// Раньше сообщение было строкой HTML внутри общего QTextDocument.
// Из-за этого у него не было состояний: ни наведения, ни выделения,
// ни собственного цвета из темы — только разметка, вписанная в
// строку на месте вызова.
//
// Ширина ограничена measureMax (~680 px): строка длиннее 75 знаков
// теряется глазом на возврате. Поэтому пузырь не растягивается на
// всю ширину окна, как растягивался HTML-блок.
// ============================================================

Rectangle {
    id: control

    property string who: ""
    property string text: ""
    property string time: ""
    property color accentColor: Theme.accent
    property bool own: false

    radius: Theme.radiusMd
    color: own ? Theme.surface2 : Theme.surface1
    border.width: 1
    border.color: hover.hovered ? Theme.outlineStrong : Theme.outline

    implicitHeight: body.implicitHeight + Theme.spaceMd * 2

    Behavior on border.color { ColorAnimation { duration: JarvisUi.durFast } }

    Accessible.role: Accessible.StaticText
    Accessible.name: control.who + ": " + control.text

    HoverHandler { id: hover }

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: Theme.spaceMd
        spacing: Theme.spaceXs

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            Text {
                text: control.who
                color: control.accentColor
                font.family: Type.family
                font.pixelSize: Type.caption
                font.weight: Font.DemiBold
            }
            Text {
                text: control.time
                color: Theme.onSurfaceDim
                font.family: Type.family
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
        }

        // Само сообщение — обычным текстом, не RichText: тело
        // приходит от пользователя и из ответов модели, и любая
        // угловая скобка в нём не должна становиться разметкой.
        Text {
            Layout.fillWidth: true
            text: control.text
            textFormat: Text.PlainText
            color: Theme.onSurface
            font.family: Type.family
            font.pixelSize: Type.body
            lineHeight: Type.lineHeightBody
            lineHeightMode: Text.ProportionalHeight
            wrapMode: Text.WordWrap
        }
    }
}
