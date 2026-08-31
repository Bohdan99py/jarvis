import QtQuick
import QtQuick.Shapes
import Jarvis.Theme

// ============================================================
// JarvisProgressRing — круговой индикатор с процентом внутри.
//
// Сделан на Shape, а не на Canvas. Canvas перерисовывается
// JavaScript-ом в главном потоке: пока кольцо анимировало
// заполнение, оно било по тому же потоку, что и весь остальной
// интерфейс. Shape уходит на GPU и не трогает главный поток вовсе.
//
// Заодно в старой версии цвет дорожки был задан строкой
// "Theme.accentSubtle" — в кавычках. Canvas молча принимал
// невалидный цвет, и дорожки под кольцом просто не было видно.
// ============================================================

Item {
    id: control

    property real value: 0
    property int thickness: 10
    property color ringColor: Theme.accent
    property color trackColor: Theme.surface3
    property string caption: ""

    readonly property real clamped: isNaN(value) ? 0 : Math.max(0, Math.min(1, value))

    // Анимируем отдельное свойство, а не value: анимация не должна
    // ломать привязку value к данным.
    property real displayed: clamped
    Behavior on displayed {
        NumberAnimation { duration: JarvisUi.durSlow * 2; easing.type: Easing.OutCubic }
    }
    onClampedChanged: displayed = clamped

    implicitWidth: 140
    implicitHeight: 140

    Accessible.role: Accessible.ProgressBar
    Accessible.name: Math.round(control.clamped * 100) + "% " + control.caption

    Shape {
        id: ring
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        readonly property real cx: width / 2
        readonly property real cy: height / 2
        readonly property real r:  Math.max(0, Math.min(width, height) / 2 - control.thickness / 2)

        ShapePath {
            strokeColor: control.trackColor
            strokeWidth: control.thickness
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: ring.cx
                centerY: ring.cy
                radiusX: ring.r
                radiusY: ring.r
                startAngle: -90
                sweepAngle: 360
            }
        }

        ShapePath {
            strokeColor: control.ringColor
            strokeWidth: control.thickness
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: ring.cx
                centerY: ring.cy
                radiusX: ring.r
                radiusY: ring.r
                startAngle: -90
                // Ровно 0 всё равно рисует точку из-за круглых торцов.
                sweepAngle: control.displayed > 0 ? 360 * control.displayed : 0
            }
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 0

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Math.round(control.displayed * 100) + "%"
            color: control.ringColor
            font.family: Type.family
            font.pixelSize: Type.heading
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: control.caption.length > 0
            text: control.caption
            color: Theme.onSurfaceVariant
            font.family: Type.family
            font.pixelSize: 10
        }
    }
}
