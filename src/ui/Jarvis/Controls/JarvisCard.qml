import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jarvis.Theme

// ============================================================
// JarvisCard — кликабельная карточка: элемент сетки режимов,
// пользователь, устройство, агент.
//
// Основа — ItemDelegate: hover, pressed, фокус по Tab и роль для
// скринридера уже внутри. Rectangle + MouseArea дают тот же вид
// и ни одного из этих состояний.
//
// Два независимых состояния, которые постоянно путают:
//   selected — карточка выделена курсором/клавиатурой (что смотрим)
//   active   — сущность включена в системе (что работает)
// Выделение показываем ступенью поверхности, включённость — рамкой
// акцентом. Менять border.width под выделение нельзя: содержимое
// сдвигается на пиксель и это читается как дрожание.
// ============================================================

ItemDelegate {
    id: control

    property bool selected: false
    property bool active: false
    property color accentColor: Theme.accent

    // Цветная метка слева — «корешок» карточки. Держит цвет
    // сущности, не заливая им всю плоскость.
    property bool spine: true

    property int contentPadding: Theme.spaceMd
    property alias contentSpacing: body.spacing

    default property alias content: body.data

    hoverEnabled: true

    // Отступы задаём через padding контрола, а не anchors внутри
    // contentItem: Control сам расставляет contentItem по padding и
    // якоря в нём молча игнорирует. Корешок съедает три пикселя
    // слева — без поправки текст прижимается к нему вплотную.
    padding: contentPadding
    leftPadding: contentPadding + (spine ? 6 : 0)

    Accessible.role: Accessible.Button

    // Подъём под курсором — единственное движение карточки; ни
    // масштаба, ни теней. Выделенная карточка не прыгает: она уже
    // «поднята» цветом поверхности.
    transform: Translate {
        y: control.hovered && !control.selected ? -2 : 0
        Behavior on y {
            NumberAnimation { duration: JarvisUi.durFast; easing.type: Easing.OutCubic }
        }
    }

    background: Rectangle {
        radius: Theme.radiusMd
        color: control.selected ? Theme.surface3
             : control.hovered  ? Theme.surface2
                                : Theme.surface1
        border.width: 1
        border.color: control.active       ? control.accentColor
                    : control.activeFocus  ? control.accentColor
                    : control.selected     ? Theme.outlineStrong
                                           : Theme.outline

        Behavior on color        { ColorAnimation { duration: JarvisUi.durFast } }
        Behavior on border.color { ColorAnimation { duration: JarvisUi.durFast } }

        Rectangle {
            visible: control.spine
            width: 3
            radius: 2
            color: control.accentColor
            opacity: control.active ? 1.0 : control.selected ? 0.7 : 0.25
            anchors {
                left: parent.left
                top: parent.top
                bottom: parent.bottom
                topMargin: Theme.spaceMd
                bottomMargin: Theme.spaceMd
                leftMargin: 1
            }
            Behavior on opacity { NumberAnimation { duration: JarvisUi.durFast } }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            cursorShape: Qt.PointingHandCursor
        }
    }

    contentItem: ColumnLayout {
        id: body
        spacing: Theme.spaceSm
    }
}
