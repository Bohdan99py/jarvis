import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jarvis.Theme
import Jarvis.Controls

// ============================================================
// ChatComposer.qml — строка ввода главного экрана.
//
// Enter отправляет, Shift+Enter переносит строку. Поле растёт по
// содержимому до потолка в шесть строк, дальше прокручивается:
// без потолка длинная вставка съедала весь экран и лента
// схлопывалась в ничто.
//
// Текст живёт в chatCtl.draft, а не только в поле: C++ подставляет
// черновик из десятка мест (переспрос, подсказка, распознанная
// речь, экранная клавиатура), и все они должны работать одинаково.
// ============================================================

Rectangle {
    id: control

    readonly property bool en: chatCtl.english
    readonly property bool canSend:
        input.text.trim().length > 0 && chatCtl.inputEnabled

    color: Theme.surface1
    border.width: 1
    border.color: input.activeFocus ? Theme.accent : Theme.outline
    radius: Theme.radiusMd

    implicitHeight: layout.implicitHeight + Theme.spaceSm * 2

    Behavior on border.color { ColorAnimation { duration: JarvisUi.durFast } }

    function submit() {
        if (!canSend) return
        chatCtl.send(input.text)
    }

    Connections {
        target: chatCtl

        // Присваивание из C++ не должно двигать курсор, если текст и
        // так совпадает: иначе каждый setDraft() во время набора
        // выбрасывал бы каретку в конец.
        function onDraftChanged() {
            if (input.text !== chatCtl.draft)
                input.text = chatCtl.draft
        }
        function onFocusRequested() { input.forceActiveFocus() }
    }

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.spaceSm
        spacing: Theme.spaceSm

        JarvisButton {
            Layout.alignment: Qt.AlignBottom
            glyph: "📎"
            accessibleName: control.en ? "Attach file" : "Прикрепить файл"
            variant: JarvisButton.Ghost
            onClicked: chatCtl.attach()
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            Layout.maximumHeight: input.lineHeight * 6 + Theme.spaceSm * 2
            Layout.preferredHeight: Math.min(input.implicitHeight,
                                             Layout.maximumHeight)

            TextArea {
                id: input

                readonly property real lineHeight: Math.ceil(metrics.lineSpacing)

                enabled: chatCtl.inputEnabled
                placeholderText: chatCtl.placeholder
                placeholderTextColor: Theme.onSurfaceDim
                color: Theme.onSurface
                font.family: Type.family
                font.pixelSize: Type.body
                selectionColor: Theme.accentSubtle
                selectedTextColor: Theme.onSurface
                wrapMode: TextArea.Wrap
                background: Item {}

                // Обратная сторона связи: набранное сразу видно C++,
                // и onSend() читает chatCtl.draft, а не лезет в поле.
                onTextChanged: chatCtl.draft = text

                // Connections срабатывает только на ИЗМЕНЕНИЕ, поэтому
                // черновик, выставленный до загрузки экрана, иначе не
                // попадал бы в поле вовсе.
                Component.onCompleted: text = chatCtl.draft

                FontMetrics {
                    id: metrics
                    font: input.font
                }

                Keys.onReturnPressed: event => {
                    if (event.modifiers & Qt.ShiftModifier) {
                        event.accepted = false
                        return
                    }
                    control.submit()
                }
                Keys.onEnterPressed: event => {
                    if (event.modifiers & Qt.ShiftModifier) {
                        event.accepted = false
                        return
                    }
                    control.submit()
                }
            }
        }

        JarvisButton {
            Layout.alignment: Qt.AlignBottom
            glyph: chatCtl.micGlyph
            accessibleName: chatCtl.micTooltip.length > 0
                            ? chatCtl.micTooltip
                            : (control.en ? "Voice input" : "Голосовой ввод")
            enabled: chatCtl.micEnabled
            // Слушает — красный. Говорят прямо сейчас — зелёный:
            // это разные факты, и раньше второй показывался подменой
            // таблицы стилей на каждый замер громкости.
            variant: chatCtl.micSpeaking ? JarvisButton.Primary
                   : chatCtl.listening   ? JarvisButton.Danger
                                         : JarvisButton.Ghost
            accentColor: chatCtl.micSpeaking ? Theme.success : Theme.accent
            onClicked: chatCtl.toggleMic()

            ToolTip.visible: hovered && chatCtl.micTooltip.length > 0
            ToolTip.text: chatCtl.micTooltip
            ToolTip.delay: 600
        }

        JarvisButton {
            Layout.alignment: Qt.AlignBottom
            glyph: "➤"
            accessibleName: control.en ? "Send" : "Отправить"
            variant: JarvisButton.Primary
            enabled: control.canSend
            onClicked: control.submit()
        }
    }
}
