import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jarvis.Theme
import Jarvis.Controls

// ============================================================
// AttachmentBar.qml — полоса прикреплённых файлов.
//
// Список горизонтальный и прокручиваемый: файлов может быть
// больше, чем влезает в ширину окна, и переносить их на вторую
// строку значит толкать ленту вверх на непредсказуемую высоту.
// ============================================================

Item {
    id: root

    readonly property bool en: chatCtl.english

    clip: true
    implicitHeight: attachModel.count > 0 ? 40 : 0
    visible: implicitHeight > 0

    Behavior on implicitHeight {
        NumberAnimation { duration: JarvisUi.durBase; easing.type: Easing.OutCubic }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spaceSm

        Text {
            Layout.alignment: Qt.AlignVCenter
            // Сводка приходит из модели (число файлов + общий объём):
            // посчитать её в QML нельзя — байты наружу не отдаются.
            text: attachModel.summary ? attachModel.summary : ""
            color: Theme.onSurfaceVariant
            font.family: Type.family
            font.pixelSize: 11
        }

        ListView {
            id: chips
            Layout.fillWidth: true
            Layout.fillHeight: true

            model: attachModel
            orientation: ListView.Horizontal
            spacing: Theme.spaceSm
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true

            delegate: Rectangle {
                id: chip

                required property int index
                required property string name
                required property string sizeText
                required property string path
                required property string glyph
                required property string tone

                readonly property color toneColor:
                      tone === "error"   ? Theme.error
                    : tone === "warning" ? Theme.warning
                                         : Theme.accent

                height: chips.height - 4
                width: chipRow.implicitWidth + Theme.spaceMd
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined

                radius: Theme.radiusPill
                color: Theme.surface2
                border.width: 1
                border.color: JarvisUi.tint(chip.toneColor, 0.35)

                ToolTip.visible: chipHover.hovered
                ToolTip.text: chip.path
                ToolTip.delay: 600

                HoverHandler { id: chipHover }

                RowLayout {
                    id: chipRow
                    anchors.centerIn: parent
                    spacing: Theme.spaceXs

                    Text {
                        text: chip.glyph
                        color: chip.toneColor
                        font.pixelSize: 12
                    }
                    Text {
                        // Имя обрезаем, размер — нет: длинное имя
                        // выталкивало бы размер за край чипа, а он
                        // и есть причина, по которой файл может не
                        // уйти вместе с запросом.
                        Layout.maximumWidth: 180
                        text: chip.name
                        color: Theme.onSurface
                        font.family: Type.family
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }
                    Text {
                        text: chip.sizeText
                        color: Theme.onSurfaceDim
                        font.family: Type.family
                        font.pixelSize: 10
                    }
                    JarvisButton {
                        glyph: "✕"
                        accessibleName: root.en ? "Remove attachment"
                                                : "Убрать вложение"
                        variant: JarvisButton.Ghost
                        implicitHeight: 20
                        implicitWidth: 20
                        onClicked: attachModel.removeAt(chip.index)
                    }
                }
            }
        }
    }
}
