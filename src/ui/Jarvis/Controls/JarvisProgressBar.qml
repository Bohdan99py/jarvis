import QtQuick
import Jarvis.Theme

// ============================================================
// JarvisProgressBar — тонкая полоса прогресса.
//
// value — доля 0..1; значения вне диапазона зажимаются, чтобы
// деление вида done/total при total = 0 не рисовало полосу
// шириной NaN (она в этом случае просто исчезала).
// ============================================================

Rectangle {
    id: control

    property real value: 0
    property color fillColor: Theme.accent

    readonly property real clamped: isNaN(value) ? 0 : Math.max(0, Math.min(1, value))

    implicitHeight: 8
    implicitWidth: 200
    radius: height / 2
    color: Theme.surface3

    Accessible.role: Accessible.ProgressBar
    Accessible.description: Math.round(clamped * 100) + "%"

    Rectangle {
        width: parent.width * control.clamped
        height: parent.height
        radius: parent.radius
        color: control.fillColor

        Behavior on width {
            NumberAnimation { duration: JarvisUi.durSlow; easing.type: Easing.OutCubic }
        }
        Behavior on color { ColorAnimation { duration: JarvisUi.durBase } }
    }
}
