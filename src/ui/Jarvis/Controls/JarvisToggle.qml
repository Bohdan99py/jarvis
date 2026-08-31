import QtQuick
import QtQuick.Controls.Basic
import Jarvis.Theme

// ============================================================
// JarvisToggle — переключатель «включено / выключено».
//
// Состояние приходит СНАРУЖИ (on) и наружу же уходит сигналом
// toggled: сам компонент ничего не запоминает. Поэтому
// checkable: false — иначе кнопка переключала бы себя сама,
// показывала бы «включено» ещё до того, как C++ реально что-то
// включил, и откатывалась бы обратно при следующем обновлении.
// Для тумблеров, за которыми стоит железо (камера, монитор,
// автоблокировка), это разница между «врёт» и «не врёт».
// ============================================================

AbstractButton {
    id: control

    property bool on: false
    property string label: ""
    property color accentColor: Theme.accentMuted

    // Тумблер без подписи (подпись стоит отдельным блоком слева) —
    // скринридеру всё равно нужно имя.
    property string accessibleName: ""

    // НЕ "toggled": AbstractButton уже объявляет toggled() без
    // аргументов, и своя версия с bool не переопределяет её, а молча
    // отбрасывается («Duplicate signal name: invalid override of
    // superclass signal»). Обработчик при этом вызывается — но с
    // undefined вместо запрошенного состояния, то есть каждый тумблер
    // в приложении просил выключить себя независимо от того, куда его
    // щёлкнули.
    signal toggleRequested(bool requested)

    checkable: false
    hoverEnabled: true
    activeFocusOnTab: true

    padding: 0
    implicitHeight: Theme.hitTarget

    Accessible.role: Accessible.CheckBox
    Accessible.name: accessibleName.length > 0 ? accessibleName : label
    Accessible.checked: on

    onClicked: control.toggleRequested(!control.on)

    contentItem: Row {
        spacing: Theme.spaceSm

        Rectangle {
            id: track
            anchors.verticalCenter: parent.verticalCenter
            width: 40
            height: 22
            radius: height / 2
            color: control.on ? control.accentColor : Theme.surface3
            border.width: 1
            border.color: control.activeFocus ? Theme.accent
                        : control.on          ? control.accentColor
                                              : Theme.outlineStrong
            opacity: control.enabled ? 1.0 : 0.4

            Behavior on color        { ColorAnimation { duration: JarvisUi.durFast } }
            Behavior on border.color { ColorAnimation { duration: JarvisUi.durFast } }

            Rectangle {
                width: 16
                height: 16
                radius: height / 2
                y: 2
                x: control.on ? track.width - width - 3 : 3
                color: control.on ? Theme.onAccent : Theme.onSurfaceVariant

                Behavior on x {
                    NumberAnimation { duration: JarvisUi.durFast; easing.type: Easing.OutCubic }
                }
                Behavior on color { ColorAnimation { duration: JarvisUi.durFast } }
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.label.length > 0
            text: control.label
            color: control.enabled
                   ? (control.hovered ? Theme.onSurface : Theme.onSurfaceVariant)
                   : Theme.onSurfaceDim
            font.family: Type.family
            font.pixelSize: Type.caption

            Behavior on color { ColorAnimation { duration: JarvisUi.durFast } }
        }
    }

    background: MouseArea {
        acceptedButtons: Qt.NoButton
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
