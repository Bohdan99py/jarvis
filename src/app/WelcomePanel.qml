import QtQuick
import QtQuick.Layouts
import Jarvis.Theme
import Jarvis.Controls

// ============================================================
// WelcomePanel.qml — приветственная сводка, которую видно, пока
// в ленте нет сообщений.
//
// Раньше это была HTML-строка, которую собирал C++, и цвета
// статусов («#66FCF1», «#ff4444») были вписаны прямо в разметку —
// единственное место приложения, жившее мимо темы.
// ============================================================

ColumnLayout {
    id: root

    readonly property bool en: chatCtl.english

    spacing: Theme.spaceLg

    ColumnLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: 2

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "J.A.R.V.I.S."
            color: Theme.accent
            font.family: Type.family
            font.pixelSize: Type.display
            font.weight: Font.DemiBold
            font.letterSpacing: 6
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "v" + welcomeCtl.version + "  ·  " + welcomeCtl.today
            color: Theme.onSurfaceDim
            font.family: Type.family
            font.pixelSize: 11
            font.letterSpacing: 2
        }
    }

    Text {
        Layout.fillWidth: true
        Layout.maximumWidth: Type.measureMax
        Layout.alignment: Qt.AlignHCenter
        text: welcomeCtl.greeting
        color: Theme.onSurface
        font.family: Type.family
        font.pixelSize: Type.body
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }

    JarvisPanel {
        Layout.fillWidth: true
        compact: true

        Repeater {
            model: welcomeCtl.statusLines

            delegate: RowLayout {
                required property var modelData

                Layout.fillWidth: true
                spacing: Theme.spaceSm

                JarvisStatusDot {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 7
                    implicitHeight: 7
                    color: modelData.tone === "error" ? Theme.error
                         : modelData.tone === "warn"  ? Theme.warning
                                                      : Theme.success
                }
                Text {
                    Layout.fillWidth: true
                    text: modelData.text
                    color: Theme.onSurfaceVariant
                    font.family: Type.family
                    font.pixelSize: Type.caption
                    elide: Text.ElideRight
                }
            }
        }
    }

    JarvisPanel {
        Layout.fillWidth: true
        compact: true
        title: root.en ? "CURRENT THOUGHT" : "ТЕКУЩАЯ МЫСЛЬ"

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            Text {
                Layout.fillWidth: true
                text: welcomeCtl.thought
                color: Theme.onSurface
                font.family: Type.family
                font.pixelSize: Type.caption
                lineHeight: Type.lineHeightBody
                lineHeightMode: Text.ProportionalHeight
                wrapMode: Text.WordWrap
            }

            JarvisBadge {
                Layout.alignment: Qt.AlignTop
                visible: welcomeCtl.unverifiedCount > 0
                text: "❓ " + welcomeCtl.unverifiedCount
                accentColor: Theme.warning
            }
        }
    }
}
