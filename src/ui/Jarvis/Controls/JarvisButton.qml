import QtQuick
import QtQuick.Controls.Basic
import Jarvis.Theme

// ============================================================
// JarvisButton — кнопка.
//
// Наследуемся от Button, а не собираем из Rectangle + MouseArea:
// от Button бесплатно достаются фокус по Tab, Space/Enter, роль
// для скринридера и состояние pressed. Ручная сборка всё это
// теряет молча — кнопка выглядит кнопкой, но не является ею.
//
// Иерархия важнее украшений: на экране ровно одна Primary. Всё
// остальное — Secondary или Ghost, иначе глазу не за что зацепиться.
// ============================================================

Button {
    id: control

    enum Variant { Secondary, Primary, Ghost, Danger }

    property int variant: JarvisButton.Secondary
    property color accentColor: Theme.accent

    // Глиф слева от подписи ("＋", "⌕", эмодзи режима). Пустая
    // строка — кнопка без иконки, отдельного флага не нужно.
    property string glyph: ""

    // Кнопка без подписи — только глиф. Отдельного флага не заводим:
    // пустой text при заданном glyph и есть это состояние. Отступы
    // становятся симметричными, кнопка — квадратной в hitTarget, и
    // остаётся полноразмерной мишенью, а не «мелкой иконкой».
    readonly property bool iconOnly: text.length === 0 && glyph.length > 0

    // Скринридеру нечего прочитать у кнопки без подписи: глиф — это
    // картинка. Для iconOnly имя обязательно задать здесь.
    property string accessibleName: ""

    readonly property bool _primary: variant === JarvisButton.Primary
    readonly property bool _ghost:   variant === JarvisButton.Ghost
    readonly property bool _danger:  variant === JarvisButton.Danger

    readonly property color _fg: !enabled            ? Theme.onSurfaceDim
                               : _primary            ? Theme.onAccent
                               : _danger             ? Theme.error
                               : _ghost              ? (hovered ? Theme.onSurface : Theme.onSurfaceVariant)
                                                     : Theme.onSurface

    hoverEnabled: true
    padding: Theme.spaceMd
    leftPadding:  iconOnly ? Theme.spaceSm : Theme.spaceLg
    rightPadding: iconOnly ? Theme.spaceSm : Theme.spaceLg
    implicitHeight: Theme.hitTarget
    implicitWidth: iconOnly ? Theme.hitTarget
                            : Math.max(Theme.hitTarget, implicitContentWidth + leftPadding + rightPadding)

    Accessible.name: accessibleName.length > 0 ? accessibleName : text

    font.family: Type.family
    font.pixelSize: Type.caption
    font.weight: _primary ? Font.DemiBold : Font.Normal

    background: Rectangle {
        radius: Theme.radiusSm
        color: !control.enabled     ? Theme.surface1
             : control._ghost       ? (control.hovered ? Theme.surface2 : "transparent")
             : control._primary     ? (control.down ? Qt.darker(control.accentColor, 1.15)
                                     : control.hovered ? Qt.lighter(control.accentColor, 1.08)
                                     : control.accentColor)
             : control.down         ? Theme.surface3
             : control.hovered      ? Theme.surface2
                                    : Theme.surface1

        border.width: control._primary ? 0 : 1
        border.color: !control.enabled           ? Theme.outline
                    : control.activeFocus        ? control.accentColor
                    : control._danger            ? JarvisUi.tint(Theme.error, 0.45)
                    : control._ghost             ? "transparent"
                                                 : Theme.outline

        Behavior on color        { ColorAnimation { duration: JarvisUi.durFast } }
        Behavior on border.color { ColorAnimation { duration: JarvisUi.durFast } }

        // Button не трогает курсор — а «рука» это единственный
        // намёк, что элемент кликабелен, до наведения.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }

    contentItem: Row {
        spacing: Theme.spaceSm

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.glyph.length > 0
            text: control.glyph
            color: control._fg
            font.pixelSize: control.font.pixelSize
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            color: control._fg
            font: control.font
            elide: Text.ElideRight

            Behavior on color { ColorAnimation { duration: JarvisUi.durFast } }
        }
    }
}
