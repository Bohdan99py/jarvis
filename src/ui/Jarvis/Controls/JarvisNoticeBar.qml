import QtQuick
import QtQuick.Layouts
import Jarvis.Theme

// ============================================================
// JarvisNoticeBar — полоса-уведомление над строкой ввода:
// подсказка, уточняющий вопрос, «ты отвечаешь на …», обновление.
//
// Раньше каждая из них была отдельным QWidget со своей таблицей
// стилей и своим QPropertyAnimation по maximumHeight — четыре
// почти одинаковых блока, разошедшихся в цветах и отступах.
//
// Раскрытие анимируется высотой самого компонента, а не opacity:
// прозрачная, но занимающая место полоса отталкивала бы ленту вниз
// ещё до появления.
// ============================================================

Item {
    id: control

    property bool open: false
    property string text: ""
    property color tone: Theme.accent

    // Подписи кнопок. Индекс нажатой уходит в actionTriggered —
    // так одна полоса обслуживает и «Да / ✕», и произвольный список
    // вариантов уточнения.
    property var actions: []
    property bool dismissable: true

    signal actionTriggered(int index)
    signal dismissed()

    clip: true
    implicitHeight: open ? content.implicitHeight : 0
    visible: implicitHeight > 0

    Behavior on implicitHeight {
        NumberAnimation {
            duration: JarvisUi.durBase
            easing.type: control.open ? Easing.OutCubic : Easing.InCubic
        }
    }

    Rectangle {
        id: content
        width: parent.width
        implicitHeight: row.implicitHeight + Theme.spaceSm * 2

        radius: Theme.radiusSm
        color: JarvisUi.tint(control.tone, 0.10)
        border.width: 1
        border.color: JarvisUi.tint(control.tone, 0.35)

        RowLayout {
            id: row
            anchors.fill: parent
            anchors.margins: Theme.spaceSm
            spacing: Theme.spaceSm

            Text {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: control.text
                color: Theme.onSurface
                font.family: Type.family
                font.pixelSize: Type.caption
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }

            Repeater {
                model: control.actions

                delegate: JarvisButton {
                    required property string modelData
                    required property int index

                    Layout.alignment: Qt.AlignVCenter
                    text: modelData
                    accentColor: control.tone
                    variant: index === 0 ? JarvisButton.Primary
                                         : JarvisButton.Secondary
                    onClicked: control.actionTriggered(index)
                }
            }

            JarvisButton {
                Layout.alignment: Qt.AlignVCenter
                visible: control.dismissable
                glyph: "✕"
                accessibleName: qsTr("Dismiss")
                variant: JarvisButton.Ghost
                onClicked: control.dismissed()
            }
        }
    }
}
