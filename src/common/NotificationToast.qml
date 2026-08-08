import QtQuick
import QtQuick.Effects
import Jarvis.Theme

// ============================================================
// NotificationToast.qml — visual layer for the toast.
//
// Everything about how it LOOKS lives here. All state comes in from
// C++ via rootContext properties (toastTitle/toastMessage/
// toastAccentColor/toastAnswerable/toastQuickOptions) and the "toast"
// object, whose invokable methods (submitAnswer/requestDismiss) are
// the only way this file talks back to C++.
//
// Дизайн: карточка поднята над фоном ступенью поверхности и мягкой
// тенью, а не пульсирующей неоновой обводкой. Бесконечная пульсация
// убрана намеренно — анимация без конца тянет внимание на себя
// постоянно, хотя уведомление важно ровно в момент появления.
// Вместо неё однократное появление со смещением: движение сообщает
// «я только что пришло» и затихает.
// ============================================================

Item {
    id: root
    width: 372
    height: card.height + 20

    // Цвет-акцент приходит из C++ (разный для типов уведомлений).
    // Если не задан — берём фирменный из токенов.
    readonly property color accent: toastAccentColor
                                    && String(toastAccentColor).length > 0
                                    ? toastAccentColor : Theme.accent

    // ---- Появление: сдвиг + проявление ----
    // Замедляющееся easing (быстрый старт, мягкая остановка) читается
    // как «прилетело и встало», а не «выскочило».
    opacity: 0
    transform: Translate { id: slide; x: 24 }
    Component.onCompleted: appear.start()
    ParallelAnimation {
        id: appear
        NumberAnimation {
            target: root; property: "opacity"; to: 1.0
            duration: Theme.motionBase * Theme.motionScale
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: slide; property: "x"; to: 0
            duration: Theme.motionBase * Theme.motionScale
            easing.type: Easing.OutCubic
        }
    }

    // ---- Мягкая тень для отрыва от фона ----
    // Заменяет неоновое свечение: даёт глубину, не претендуя на внимание.
    Rectangle {
        id: shadowSource
        anchors.fill: card
        radius: card.radius
        color: "#000000"
        visible: false
    }
    MultiEffect {
        anchors.fill: shadowSource
        source: shadowSource
        blurEnabled: true
        blur: 0.55
        blurMax: 28
        opacity: 0.55
        autoPaddingEnabled: true
        z: -1
    }

    // ---- Карточка ----
    Rectangle {
        id: card
        x: 10
        y: 8
        width: root.width - 20
        height: column.implicitHeight + 2 * Theme.spaceLg
        radius: Theme.radiusLg
        color: Theme.surface3
        border.width: 1
        border.color: Theme.outlineStrong
        clip: true

        // Click-anywhere-to-dismiss background — declared first so it sits
        // beneath the interactive controls below; Qt Quick hit-tests
        // top-down, so clicks on the close button / pills / input field
        // are consumed by those items first and never reach this one.
        MouseArea {
            anchors.fill: parent
            onClicked: toast.requestDismiss()
        }

        // Полоса типа уведомления. Сплошной акцент вместо градиента
        // в фиолетовый: градиент здесь ничего не сообщал, а два цвета
        // на 3 пикселях читаются как грязь.
        Rectangle {
            x: 0
            y: 0
            width: 3
            height: parent.height
            color: root.accent
        }

        // Close button
        Text {
            id: closeGlyph
            text: "✕"
            color: closeArea.containsMouse ? Theme.onSurface : Theme.onSurfaceDim
            font.pixelSize: Type.caption
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.spaceMd
            Behavior on color {
                ColorAnimation { duration: Theme.motionFast * Theme.motionScale }
            }
            MouseArea {
                id: closeArea
                anchors.fill: parent
                anchors.margins: -10   // добираем до комфортной зоны нажатия
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: toast.requestDismiss()
            }
        }

        Column {
            id: column
            x: Theme.spaceLg + 4
            y: Theme.spaceLg
            width: parent.width - (Theme.spaceLg + 4) - Theme.spaceXl
            spacing: Theme.spaceSm

            Text {
                text: toastTitle
                color: Theme.onSurface
                font.pixelSize: Type.body
                font.weight: Font.DemiBold
                font.family: Type.family
                width: parent.width
                wrapMode: Text.WordWrap
            }
            Text {
                text: toastMessage
                color: Theme.onSurfaceVariant
                font.pixelSize: Type.caption
                font.family: Type.family
                lineHeight: Type.lineHeightBody
                lineHeightMode: Text.ProportionalHeight
                width: parent.width
                wrapMode: Text.WordWrap
            }

            // Quick-reply pills (e.g. Yes/No)
            Row {
                visible: toastQuickOptions.length > 0
                spacing: Theme.spaceSm
                topPadding: Theme.spaceXs

                Repeater {
                    model: toastQuickOptions
                    delegate: Rectangle {
                        radius: Theme.radiusSm
                        width: pillText.implicitWidth + 2 * Theme.spaceMd
                        height: Theme.hitTarget
                        color: pillArea.pressed ? Theme.accentSubtle
                             : pillArea.containsMouse ? Theme.surface2
                             : "transparent"
                        border.width: 1
                        border.color: pillArea.containsMouse
                                      ? Theme.accent : Theme.outlineStrong

                        Behavior on color {
                            ColorAnimation { duration: Theme.motionFast * Theme.motionScale }
                        }

                        Text {
                            id: pillText
                            anchors.centerIn: parent
                            text: modelData
                            color: Theme.onSurface
                            font.pixelSize: Type.caption
                            font.family: Type.family
                        }
                        MouseArea {
                            id: pillArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
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
                height: Theme.hitTarget + 4
                radius: Theme.radiusSm
                color: Theme.surface2
                border.width: 1
                border.color: input.activeFocus ? Theme.accent : Theme.outline

                Behavior on border.color {
                    ColorAnimation { duration: Theme.motionFast * Theme.motionScale }
                }

                TextInput {
                    id: input
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spaceMd
                    anchors.rightMargin: Theme.spaceXxl
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.onSurface
                    font.pixelSize: Type.caption
                    font.family: Type.family
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
                    color: Theme.onSurfaceDim
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spaceMd
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: Type.caption
                    font.family: Type.family
                }
                Text {
                    text: "➤"
                    color: sendArea.containsMouse ? Theme.accent : Theme.onSurfaceVariant
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spaceMd
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: Type.caption
                    MouseArea {
                        id: sendArea
                        anchors.fill: parent
                        anchors.margins: -10
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
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
