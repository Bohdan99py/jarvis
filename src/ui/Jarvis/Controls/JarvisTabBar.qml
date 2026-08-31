import QtQuick
import QtQuick.Controls.Basic
import Jarvis.Theme

// ============================================================
// JarvisTabBar — вкладки экрана.
//
// Четыре экрана (UserCenter, OrganizePanel, VisionCenter,
// TrainingCenter) собрали каждый свой вариант из Row + Repeater +
// Rectangle. Вид разошёлся, а стрелками не переключается ни один:
// у голых прямоугольников нет ни фокуса, ни роли вкладки.
// TabBar даёт и то и другое.
//
// Использование — список подписей, а не набор дочерних кнопок:
// подписи почти всегда приходят из C++ уже переведёнными.
//
//     JarvisTabBar { titles: [ "План", "Правила" ] }
// ============================================================

TabBar {
    id: control

    property var titles: []
    property color accentColor: Theme.accent

    spacing: Theme.spaceSm

    // Полоса-подложка TabBar по умолчанию рисует линию снизу во всю
    // ширину — лишняя горизонталь на экране, где панели и так
    // разделены рамками.
    background: Item {}

    Repeater {
        model: control.titles

        delegate: TabButton {
            id: tab

            required property string modelData
            required property int index

            text: modelData
            padding: Theme.spaceMd
            leftPadding: Theme.spaceLg
            rightPadding: Theme.spaceLg
            implicitHeight: Theme.hitTarget + 2
            width: implicitWidth

            readonly property bool current: control.currentIndex === index

            background: Rectangle {
                radius: Theme.radiusSm
                color: tab.current   ? JarvisUi.tint(control.accentColor, 0.12)
                     : tab.hovered   ? Theme.surface2
                                     : "transparent"
                border.width: 1
                border.color: tab.current      ? JarvisUi.tint(control.accentColor, 0.45)
                            : tab.activeFocus  ? Theme.outlineStrong
                                               : "transparent"

                Behavior on color        { ColorAnimation { duration: JarvisUi.durFast } }
                Behavior on border.color { ColorAnimation { duration: JarvisUi.durFast } }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    cursorShape: Qt.PointingHandCursor
                }
            }

            contentItem: Text {
                text: tab.text
                color: tab.current ? control.accentColor : Theme.onSurfaceVariant
                font.family: Type.family
                font.pixelSize: Type.caption
                font.weight: tab.current ? Font.DemiBold : Font.Normal
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight

                Behavior on color { ColorAnimation { duration: JarvisUi.durFast } }
            }
        }
    }
}
