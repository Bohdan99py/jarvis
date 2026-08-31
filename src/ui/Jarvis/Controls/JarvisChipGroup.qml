import QtQuick
import QtQuick.Controls.Basic
import Jarvis.Theme

// ============================================================
// JarvisChipGroup — выбор одного значения из нескольких: роль,
// стиль разработки, рабочие часы.
//
// В UserCenter этот блок был написан трижды почти дословно —
// Flow + Repeater + Rectangle + MouseArea. Ни фокуса, ни Space,
// ни роли переключателя для скринридера.
//
// options — либо массив строк, либо массив { value, label }.
// Выбранное значение приходит СНАРУЖИ (value) и наружу же
// уходит сигналом picked: сам компонент состояние не держит.
// Так исключается рассинхрон, когда значение меняют мимо чипов.
//
//     JarvisChipGroup {
//         options: root.roleOptions
//         value: root.selectedAddRole
//         onPicked: v => root.selectedAddRole = v
//     }
// ============================================================

Flow {
    id: control

    property var options: []
    property var value: undefined
    property color accentColor: Theme.accent

    signal picked(var pickedValue)

    spacing: Theme.spaceSm

    function valueOf(o) { return (o && o.value !== undefined) ? o.value : o }
    function labelOf(o) { return (o && o.label !== undefined) ? o.label : String(o) }

    Repeater {
        model: control.options

        delegate: AbstractButton {
            id: chip

            required property var modelData

            readonly property var chipValue: control.valueOf(modelData)
            readonly property bool selected: chipValue === control.value

            // checkable: false — состояние не наше. Иначе кнопка
            // переключала бы себя сама, привязка к value рвалась бы
            // при первом же клике, и подсветка разъехалась бы с
            // настоящим значением.
            checkable: false
            hoverEnabled: true
            activeFocusOnTab: true

            padding: Theme.spaceSm
            leftPadding: Theme.spaceMd
            rightPadding: Theme.spaceMd
            implicitHeight: Theme.hitTarget - 4

            Accessible.role: Accessible.RadioButton
            Accessible.name: control.labelOf(modelData)
            Accessible.checked: selected

            onClicked: control.picked(chip.chipValue)

            background: Rectangle {
                radius: Theme.radiusSm
                color: chip.selected ? JarvisUi.tint(control.accentColor, 0.12)
                     : chip.hovered  ? Theme.surface2
                                     : Theme.surface1
                border.width: 1
                border.color: chip.selected    ? JarvisUi.tint(control.accentColor, 0.45)
                            : chip.activeFocus ? Theme.outlineStrong
                                               : Theme.outline

                Behavior on color        { ColorAnimation { duration: JarvisUi.durFast } }
                Behavior on border.color { ColorAnimation { duration: JarvisUi.durFast } }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    cursorShape: Qt.PointingHandCursor
                }
            }

            contentItem: Text {
                text: control.labelOf(chip.modelData)
                color: chip.selected ? control.accentColor : Theme.onSurfaceVariant
                font.family: Type.family
                font.pixelSize: 11
                font.weight: chip.selected ? Font.DemiBold : Font.Normal
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter

                Behavior on color { ColorAnimation { duration: JarvisUi.durFast } }
            }
        }
    }
}
