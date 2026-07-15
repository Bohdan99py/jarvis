import QtQuick
import QtQuick.Effects

// ============================================================
// NotificationToast.qml — visual layer for the neon toast.
//
// Everything about how it LOOKS lives here: glass panel, rounded
// corners, blurred neon glow that breathes, the optional free-text
// answer field and quick-reply pills. All state comes in from C++
// via rootContext properties (toastTitle/toastMessage/toastAccentColor/
// toastAnswerable/toastQuickOptions) and the "toast" object, whose
// invokable methods (submitAnswer/requestDismiss) are the only way
// this file talks back to C++.
// ============================================================

Item {
    id: root
    width: 372
    height: card.height + 16

    readonly property color accent: toastAccentColor

    // Breathing phase 0..1..0, drives the glow's opacity/brightness
    property real glow: 0.0
    SequentialAnimation on glow {
        loops: Animation.Infinite
        NumberAnimation { from: 0.0; to: 1.0; duration: 1400; easing.type: Easing.InOutSine }
        NumberAnimation { from: 1.0; to: 0.0; duration: 1400; easing.type: Easing.InOutSine }
    }

    // ---- Blurred neon glow behind the glass card ----
    Rectangle {
        id: glowSource
        anchors.fill: card
        anchors.margins: -5
        radius: card.radius + 5
        color: "transparent"
        border.width: 3
        border.color: root.accent
        opacity: 0.30 + 0.40 * root.glow
        visible: false
    }
    MultiEffect {
        anchors.fill: glowSource
        source: glowSource
        blurEnabled: true
        blur: 0.7
        blurMax: 48
        autoPaddingEnabled: true
    }

    // ---- Glass card ----
    Rectangle {
        id: card
        x: 8
        y: 8
        width: root.width - 16
        height: column.implicitHeight + 24
        radius: 14
        color: Qt.rgba(12 / 255, 18 / 255, 30 / 255, 0.93)
        border.width: 1.4
        border.color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.45 + 0.45 * root.glow)
        clip: true

        // Click-anywhere-to-dismiss background — declared first so it sits
        // beneath the interactive controls below; Qt Quick hit-tests
        // top-down, so clicks on the close button / pills / input field
        // are consumed by those items first and never reach this one.
        MouseArea {
            anchors.fill: parent
            onClicked: toast.requestDismiss()
        }

        // Accent bar, cyan -> violet gradient
        Rectangle {
            x: 10
            y: 12
            width: 3
            height: parent.height - 24
            radius: 1.5
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: root.accent }
                GradientStop { position: 1.0; color: "#7c4dff" }
            }
        }

        // Close button
        Text {
            id: closeGlyph
            text: "✕"
            color: Qt.rgba(1, 1, 1, 0.35)
            font.pixelSize: 12
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 10
            MouseArea {
                anchors.fill: parent
                anchors.margins: -8
                onClicked: toast.requestDismiss()
            }
        }

        Column {
            id: column
            x: 24
            y: 12
            width: parent.width - 24 - 22
            spacing: 6

            Text {
                text: toastTitle
                color: root.accent
                font.pixelSize: 14
                font.bold: true
                font.family: "Segoe UI Semibold"
                width: parent.width
                wrapMode: Text.WordWrap
            }
            Text {
                text: toastMessage
                color: "#c0d8ee"
                font.pixelSize: 12
                width: parent.width
                wrapMode: Text.WordWrap
            }

            // Quick-reply pills (e.g. Yes/No)
            Row {
                visible: toastQuickOptions.length > 0
                spacing: 8

                Repeater {
                    model: toastQuickOptions
                    delegate: Rectangle {
                        radius: 8
                        width: pillText.implicitWidth + 20
                        height: 26
                        color: pillArea.pressed
                            ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.35)
                            : Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.15)
                        border.width: 1
                        border.color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.55)

                        Text {
                            id: pillText
                            anchors.centerIn: parent
                            text: modelData
                            color: "#e8f0fe"
                            font.pixelSize: 12
                        }
                        MouseArea {
                            id: pillArea
                            anchors.fill: parent
                            onClicked: toast.submitAnswer(modelData)
                        }
                    }
                }
            }

            // Free-text answer field — lets Jarvis remember what you typed
            Rectangle {
                id: answerBox
                visible: toastAnswerable
                width: parent.width
                height: 34
                radius: 8
                color: Qt.rgba(1, 1, 1, 0.05)
                border.width: 1
                border.color: input.activeFocus
                    ? root.accent
                    : Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.30)

                TextInput {
                    id: input
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 32
                    verticalAlignment: TextInput.AlignVCenter
                    color: "#e8f0fe"
                    font.pixelSize: 12
                    clip: true
                    selectByMouse: true
                    onAccepted: {
                        if (text.trim().length > 0) {
                            toast.submitAnswer(text)
                            text = ""
                        }
                    }
                }
                Text {
                    text: toastPlaceholder
                    visible: input.text.length === 0 && !input.activeFocus
                    color: Qt.rgba(1, 1, 1, 0.30)
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: 12
                }
                Text {
                    text: "➤"
                    color: root.accent
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: 13
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -8
                        onClicked: {
                            if (input.text.trim().length > 0) {
                                toast.submitAnswer(input.text)
                                input.text = ""
                            }
                        }
                    }
                }
            }
        }
    }
}
