import QtQuick
import QtQuick.Controls.Basic
import Jarvis.Theme

// ============================================================
// JarvisTextField — однострочное поле ввода.
//
// Основа — TextField, а не голый TextInput в Rectangle: от него
// достаются выделение мышью, контекстное меню, Ctrl+A/C/V, undo,
// плейсхолдер и роль для скринридера. В экранах поля собирали
// руками, и всего этого там не было — включая плейсхолдер, который
// каждый раз рисовали отдельным Text поверх.
//
// glyph — необязательный значок слева (лупа, «+»); clearable —
// крестик справа, который появляется только когда есть что стирать.
// ============================================================

TextField {
    id: control

    property string placeholder: ""
    property string glyph: ""
    property bool clearable: false

    placeholderText: placeholder
    placeholderTextColor: Theme.onSurfaceDim

    color: Theme.onSurface
    font.family: Type.family
    font.pixelSize: Type.caption

    selectionColor: Theme.accentSubtle
    selectedTextColor: Theme.onSurface

    implicitHeight: Theme.hitTarget + 4
    leftPadding:  glyph.length > 0 ? Theme.spaceXl + Theme.spaceSm : Theme.spaceMd
    rightPadding: clearable         ? Theme.spaceXl                : Theme.spaceMd

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.surface2
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : Theme.outline

        Behavior on border.color { ColorAnimation { duration: JarvisUi.durFast } }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Theme.spaceMd
            visible: control.glyph.length > 0
            text: control.glyph
            color: Theme.onSurfaceDim
            font.pixelSize: Type.body
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: Theme.spaceMd
            visible: control.clearable && control.length > 0
            text: "\u00D7"
            color: clearArea.containsMouse ? Theme.onSurface : Theme.onSurfaceDim
            font.pixelSize: Type.body

            Behavior on color { ColorAnimation { duration: JarvisUi.durFast } }

            MouseArea {
                id: clearArea
                anchors.fill: parent
                anchors.margins: -Theme.spaceSm
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: control.clear()
            }
        }
    }
}
