import QtQuick
import QtQuick.Layouts
import Jarvis.Theme
import Jarvis.Controls

// ============================================================
// VisualInsights.qml — боковая панель диаграмм, картинок и
// ссылок на файлы.
//
// Раскрытие анимируется шириной самой панели, а не отдельным
// QPropertyAnimation по свойству виджета: панель — сосед ленты в
// строке, и её ширина и есть то, что должно ехать.
// ============================================================

Item {
    id: root

    readonly property bool en: chatCtl.english
    readonly property int panelWidth: 520

    clip: true
    // Панель просит свою ширину, но не требует её: если окно узкое,
    // layout вправе выдать меньше, и это лучше, чем выдавить ленту.
    implicitWidth: visualCtl.open ? panelWidth : 0
    Layout.maximumWidth: panelWidth
    // Ниже этого панель бесполезна: диаграмма превращается в полоску.
    Layout.minimumWidth: visualCtl.open ? 260 : 0
    visible: implicitWidth > 0

    Behavior on implicitWidth {
        NumberAnimation {
            duration: JarvisUi.durBase
            easing.type: visualCtl.open ? Easing.OutCubic : Easing.InCubic
        }
    }

    Rectangle {
        // Заполняем родителя, а не держим фиксированные panelWidth:
        // в строке layout может выдать меньше запрошенного, и
        // прибитая ширина уезжала за край — правые кнопки шапки
        // просто срезало.
        //
        // Да, во время раскрытия содержимое переливается. Так вело
        // себя и прежнее окно на виджетах: оно анимировало
        // setFixedWidth самой панели.
        anchors.fill: parent

        color: Theme.surface1
        border.width: 1
        border.color: Theme.outline

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spaceSm
            spacing: Theme.spaceSm

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceXs

                JarvisButton {
                    glyph: "‹"
                    accessibleName: root.en ? "Previous" : "Предыдущее"
                    variant: JarvisButton.Ghost
                    enabled: visualCtl.canPrev
                    onClicked: visualCtl.prev()
                }
                JarvisButton {
                    glyph: "›"
                    accessibleName: root.en ? "Next" : "Следующее"
                    variant: JarvisButton.Ghost
                    enabled: visualCtl.canNext
                    onClicked: visualCtl.next()
                }

                Text {
                    Layout.fillWidth: true
                    text: visualCtl.title
                    color: Theme.onSurface
                    font.family: Type.family
                    font.pixelSize: Type.caption
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                }

                JarvisButton {
                    glyph: "📁"
                    accessibleName: root.en ? "Open folder" : "Открыть папку"
                    variant: JarvisButton.Ghost
                    visible: visualCtl.hasFile
                    onClicked: visualCtl.openFolder()
                }
                JarvisButton {
                    glyph: "💾"
                    accessibleName: root.en ? "Save" : "Сохранить"
                    variant: JarvisButton.Ghost
                    onClicked: visualCtl.save()
                }
                JarvisButton {
                    glyph: "✕"
                    accessibleName: root.en ? "Close" : "Закрыть"
                    variant: JarvisButton.Ghost
                    onClicked: visualCtl.close()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.outline
            }

            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Тяжёлую поверхность создаём только когда панель
                // действительно раскрыта: Chromium стоит дорого,
                // и держать его ради свёрнутой панели незачем.
                active: visualCtl.open
                asynchronous: true
                source: visualCtl.webEngineAvailable
                        ? "VisualInsightsWeb.qml"
                        : "VisualInsightsSvg.qml"
            }
        }
    }
}
