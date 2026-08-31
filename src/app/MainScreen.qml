import QtQuick
import QtQuick.Layouts
import Jarvis.Theme
import Jarvis.Controls

// ============================================================
// MainScreen.qml — главный экран целиком: шапка, лента, нижняя
// область (полосы, вложения, ввод).
//
// Файл сборочный: он расставляет три части и ничего не решает сам.
// ============================================================

Rectangle {
    id: root
    color: Theme.bg

    readonly property bool en: chatCtl.english

    // Цвет состояния — из темы по строковому оттенку. Раньше он был
    // вписан в таблицу стилей QLabel четырьмя hex-константами.
    readonly property color toneColor:
          chatCtl.statusTone === "speaking" ? Theme.success
        : chatCtl.statusTone === "typing"   ? Theme.warning
        : chatCtl.statusTone === "thinking" ? Theme.info
                                            : Theme.accent

    // Лента и боковая панель — соседи в строке: панель раскрывается
    // шириной, лента отдаёт ей место сама.
    RowLayout {
        id: cols
        anchors.fill: parent
        spacing: 0


    ColumnLayout {
        id: chatCol
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.margins: Theme.spaceLg
        // preferredWidth: 0 вместе с fillWidth означает «возьму то, что
        // осталось». Без него колонка требует свою implicit-ширину —
        // ширину самого широкого потомка, — RowLayout переполняется, и
        // боковая панель, объявленная позже, рисуется поверх ленты.
        Layout.preferredWidth: 0
        Layout.minimumWidth: 320
        spacing: Theme.spaceMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            JarvisButton {
                Layout.alignment: Qt.AlignVCenter
                glyph: "☰"
                accessibleName: root.en ? "Menu" : "Меню"
                variant: JarvisButton.Ghost
                onClicked: chatCtl.openMenu()
            }

            Text {
                text: "⬡"
                color: Theme.accent
                font.pixelSize: Type.title
            }
            Text {
                text: "J.A.R.V.I.S."
                color: Theme.onSurface
                font.family: Type.family
                font.pixelSize: Type.title
                font.weight: Font.DemiBold
                font.letterSpacing: 1
            }
            Text {
                Layout.alignment: Qt.AlignVCenter
                // Мерим КОЛОНКУ, а не окно: с раскрытой боковой панелью
                // окно широкое, а места у шапки мало — по ширине окна
                // подпись оставалась включённой и обрезалась в «Д».
                // На узком месте версия и подпись состояния уходят
                // первыми: заголовок и точка важнее.
                visible: chatCol.width > 520
                text: "v" + welcomeCtl.version
                color: Theme.onSurfaceDim
                font.family: Type.family
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            JarvisBadge {
                Layout.alignment: Qt.AlignVCenter
                visible: chatCtl.agentName.length > 0
                text: chatCtl.agentName
                implicitHeight: 26
            }

            // Точка — состояние («в сети», «говорит»), пульсация —
            // работа прямо сейчас. Два разных факта на одном элементе,
            // поэтому и элемент один.
            JarvisStatusDot {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 10
                implicitHeight: 10
                color: root.toneColor
                pulsing: chatCtl.busy || chatCtl.statusTone === "speaking"
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                Layout.maximumWidth: 220
                visible: chatCol.width > 420
                text: chatCtl.statusText
                color: root.toneColor
                font.family: Type.family
                font.pixelSize: Type.caption
                elide: Text.ElideRight
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.outline
        }

        ChatView {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        ComposerArea {
            Layout.fillWidth: true
        }
    }

        VisualInsights {
            id: panel
            Layout.fillHeight: true
            // Зазор появляется вместе с панелью: в свёрнутом виде он
            // оставил бы пустую полосу у правого края.
            Layout.leftMargin: visualCtl.open ? Theme.spaceMd : 0
        }
    }
}
