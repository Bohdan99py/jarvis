import QtQuick
import Jarvis.Theme

// ============================================================
// JarvisBadge — метка состояния: «Активен», «3 ошибки», «beta».
//
// Заливка — акцент на 12–14%, текст — сам акцент в полную силу.
// Не сплошная заливка акцентом: на неоновом фоне тёмный текст
// вибрирует, а сама метка начинает спорить с содержимым.
//
// Точка включается там, где метка сообщает СОСТОЯНИЕ, а не
// количество: состояние должно читаться и без различения оттенков.
// ============================================================

Rectangle {
    id: control

    property string text: ""
    property color accentColor: Theme.accent
    property bool dot: false
    property bool pulsing: false
    property bool outlined: false

    radius: Theme.radiusPill
    color: JarvisUi.tint(accentColor, 0.13)
    border.width: outlined ? 1 : 0
    border.color: JarvisUi.tint(accentColor, 0.45)

    implicitWidth: row.implicitWidth + Theme.spaceMd * 2
    implicitHeight: 22

    Behavior on color { ColorAnimation { duration: JarvisUi.durBase } }

    Accessible.role: Accessible.StaticText
    Accessible.name: control.text

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spaceXs + 1

        JarvisStatusDot {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.dot
            implicitWidth: 6
            implicitHeight: 6
            color: control.accentColor
            pulsing: control.pulsing
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            color: control.accentColor
            font.family: Type.family
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
    }
}
