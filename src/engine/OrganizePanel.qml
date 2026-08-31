import QtQuick
import QtQuick.Layouts
import Jarvis.Theme
import Jarvis.Controls

// ============================================================
// OrganizePanel.qml — File Organizer plan review + rules editor.
//
// C++ (OrganizePlanDialog) owns the data (the scanned plan, the
// FileOrganizer rule set) and side effects (apply/undo/persist);
// this file owns the look — tabs, category chips, the editable
// rules list. Talks back to C++ only through the "organizePanel"
// invokable object.
// ============================================================

Rectangle {
    id: root
    color: Theme.bg

    readonly property bool en: opEnglish
    readonly property color cyan: Theme.accent
    readonly property color teal: Theme.accentMuted

    function fmtSize(bytes) {
        if (bytes > 1024 * 1024) return (bytes / 1024 / 1024).toFixed(1) + " MB"
        if (bytes > 1024) return (bytes / 1024).toFixed(0) + " KB"
        return bytes + " B"
    }

    readonly property int currentTab: tabs.currentIndex
    property int expandedItemIndex: -1

    Column {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        // ---- Header ----
        Text {
            text: en ? "ORGANIZE FILES" : "ОРГАНИЗАЦИЯ ФАЙЛОВ"
            color: root.cyan
            font.pixelSize: 18
            font.bold: true
            font.family: "Segoe UI Semibold"
        }
        Text {
            visible: opTargetFolder.length > 0
            width: parent.width
            text: opTargetFolder
            color: Theme.onSurfaceVariant
            font.pixelSize: 11
            elide: Text.ElideMiddle
        }

        // ---- Tab bar ----
        JarvisTabBar {
            id: tabs
            titles: [en ? "Plan" : "План", en ? "Rules" : "Правила"]
            // initialTab — контекстное свойство из C++: значение на старте.
            // Клик по вкладке снимает эту привязку, дальше выбор за
            // пользователем — ровно то поведение, что нужно.
            currentIndex: initialTab
        }

        Rectangle {
            width: parent.width
            height: 2
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.2; color: root.teal }
                GradientStop { position: 0.5; color: root.cyan }
                GradientStop { position: 0.8; color: root.teal }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        // ---- Tab content ----
        Item {
            width: parent.width
            height: parent.height - y

            // ======================================================
            // Tab 0 — Plan
            // ======================================================
            Item {
                anchors.fill: parent
                visible: root.currentTab === 0
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }

                Column {
                    anchors.fill: parent
                    spacing: 10

                    Flow {
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: opCategoryCounts
                            delegate: Rectangle {
                                readonly property bool unsorted: modelData.category === "Нераспознано"
                                radius: 7
                                height: 24
                                width: chipTxt.implicitWidth + 16
                                color: unsorted ? Qt.alpha(Theme.error, 0.12) : Theme.outline
                                border.width: 1
                                border.color: unsorted ? Qt.alpha(Theme.error, 0.4) : Theme.accentSubtle
                                Text {
                                    id: chipTxt
                                    anchors.centerIn: parent
                                    text: (unsorted ? "❓ " : "📁 ") + modelData.category + " · " + modelData.count
                                    color: unsorted ? Theme.error : root.cyan
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }

                    Flickable {
                        width: parent.width
                        height: parent.height - 34 - listActions.height - 20
                        clip: true
                        contentHeight: itemsCol.height
                        boundsBehavior: Flickable.StopAtBounds

                        Column {
                            id: itemsCol
                            width: parent.width
                            spacing: 6

                            Repeater {
                                model: opPlanItems
                                delegate: Rectangle {
                                    id: itemRow
                                    readonly property int myIndex: index
                                    width: itemsCol.width
                                    height: rowContent.height + 16
                                    radius: 8
                                    color: modelData.confident ? Theme.outline : Qt.alpha(Theme.error, 0.06)
                                    border.width: 1
                                    border.color: modelData.confident ? Theme.accentSubtle : Qt.alpha(Theme.error, 0.25)

                                    Column {
                                        id: rowContent
                                        x: 12; y: 8
                                        width: parent.width - 24
                                        spacing: 4

                                        Row {
                                            width: parent.width
                                            Text {
                                                width: parent.width - 90
                                                text: modelData.fileName
                                                color: Theme.onSurface
                                                font.pixelSize: 12
                                                elide: Text.ElideMiddle
                                            }
                                            Text {
                                                width: 90
                                                horizontalAlignment: Text.AlignRight
                                                text: root.fmtSize(modelData.sizeBytes)
                                                color: Theme.onSurfaceVariant
                                                font.pixelSize: 10
                                            }
                                        }
                                        Row {
                                            spacing: 6
                                            Text {
                                                text: (modelData.confident ? "📁 " : "❓ ") + modelData.category
                                                      + (modelData.subcategory.length > 0 ? " / " + modelData.subcategory : "")
                                                color: modelData.confident ? root.teal : Theme.error
                                                font.pixelSize: 10
                                            }
                                            Text {
                                                text: en ? "· change" : "· изменить"
                                                color: Theme.accentMuted
                                                font.pixelSize: 10
                                                font.underline: true
                                                MouseArea {
                                                    anchors.fill: parent
                                                    anchors.margins: -4
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: root.expandedItemIndex =
                                                        (root.expandedItemIndex === itemRow.myIndex ? -1 : itemRow.myIndex)
                                                }
                                            }
                                        }

                                        Flow {
                                            width: parent.width
                                            spacing: 6
                                            visible: root.expandedItemIndex === itemRow.myIndex
                                            Repeater {
                                                model: opCategoryNames
                                                delegate: Rectangle {
                                                    radius: 6
                                                    height: 22
                                                    width: catTxt.implicitWidth + 14
                                                    color: Theme.outline
                                                    border.width: 1
                                                    border.color: Theme.outlineStrong
                                                    Text {
                                                        id: catTxt
                                                        anchors.centerIn: parent
                                                        text: modelData
                                                        color: Theme.onSurface
                                                        font.pixelSize: 10
                                                    }
                                                    MouseArea {
                                                        anchors.fill: parent
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: {
                                                            organizePanel.setItemCategory(itemRow.myIndex, modelData)
                                                            root.expandedItemIndex = -1
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Кнопки действий. Ширины больше не считаем руками:
                    // раньше распорку задавала арифметика вида
                    // parent.width - 130 - 110 - 90 - 110 - 30, и любая
                    // правка подписи ломала выравнивание.
                    RowLayout {
                        id: listActions
                        width: parent.width
                        spacing: Theme.spaceSm

                        JarvisButton {
                            visible: opTargetFolder.length > 0
                            glyph: "🔄"
                            text: en ? "Rescan" : "Пересканировать"
                            onClicked: organizePanel.rescan()
                        }

                        Item { Layout.fillWidth: true }

                        JarvisButton {
                            glyph: "↩"
                            text: en ? "Undo Last" : "Отменить"
                            variant: JarvisButton.Ghost
                            onClicked: organizePanel.undoLast()
                        }
                        JarvisButton {
                            glyph: "✕"
                            text: en ? "Cancel" : "Отмена"
                            variant: JarvisButton.Danger
                            onClicked: organizePanel.cancelDialog()
                        }
                        JarvisButton {
                            glyph: "✓"
                            text: en ? "Apply" : "Применить"
                            variant: JarvisButton.Primary
                            onClicked: organizePanel.apply()
                        }
                    }
                }
            }

            // ======================================================
            // Tab 1 — Rules
            // ======================================================
            Item {
                anchors.fill: parent
                visible: root.currentTab === 1
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }

                Column {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        width: parent.width
                        spacing: Theme.spaceMd

                        Text {
                            Layout.fillWidth: true
                            text: en ? "Category rules — extensions and content-aware subcategories"
                                     : "Правила категорий — расширения и подкатегории по содержимому"
                            color: Theme.onSurfaceVariant
                            font.family: Type.family
                            font.pixelSize: Type.caption
                            wrapMode: Text.WordWrap
                        }
                        JarvisButton {
                            text: en ? "Reset defaults" : "Сбросить"
                            variant: JarvisButton.Ghost
                            onClicked: organizePanel.resetRules()
                        }
                    }

                    Flickable {
                        width: parent.width
                        height: parent.height - 30 - 40 - 20
                        clip: true
                        contentHeight: rulesCol.height
                        boundsBehavior: Flickable.StopAtBounds

                        Column {
                            id: rulesCol
                            width: parent.width
                            spacing: 8

                            Repeater {
                                model: opRules
                                delegate: Rectangle {
                                    width: rulesCol.width
                                    height: ruleContent.height + 20
                                    radius: 8
                                    color: Theme.surface1
                                    border.width: 1
                                    border.color: Theme.accentSubtle

                                    Column {
                                        id: ruleContent
                                        x: 14; y: 10
                                        width: parent.width - 28
                                        spacing: 6

                                        RowLayout {
                                            width: parent.width
                                            spacing: Theme.spaceSm

                                            Text {
                                                Layout.fillWidth: true
                                                text: modelData.category
                                                color: root.cyan
                                                font.family: Type.family
                                                font.pixelSize: Type.caption
                                                font.weight: Font.DemiBold
                                                elide: Text.ElideRight
                                            }
                                            JarvisButton {
                                                text: en ? "Remove" : "Удалить"
                                                variant: JarvisButton.Danger
                                                onClicked: organizePanel.removeRule(modelData.category)
                                            }
                                        }

                                        Text { text: en ? "Extensions (comma-separated):" : "Расширения (через запятую):"; color: Theme.onSurfaceVariant; font.pixelSize: 9 }
                                        Rectangle {
                                            width: parent.width; height: 28; radius: 6
                                            color: Theme.outline
                                            border.width: 1
                                            border.color: extInput.activeFocus ? root.cyan : Theme.outlineStrong
                                            TextInput {
                                                id: extInput
                                                anchors.fill: parent
                                                anchors.margins: 8
                                                verticalAlignment: TextInput.AlignVCenter
                                                color: Theme.onSurface
                                                font.pixelSize: 11
                                                text: modelData.extensions
                                                selectByMouse: true
                                                onEditingFinished: organizePanel.updateRule(modelData.category, text, ctxToggle.contextAware, subInput.text)
                                            }
                                        }

                                        // Состояние держим здесь: C++ узнаёт о
                                        // переключении из updateRule() и вернёт
                                        // его в модель, но не в тот же кадр.
                                        JarvisToggle {
                                            id: ctxToggle
                                            property bool contextAware: modelData.contextAware
                                            on: contextAware
                                            label: en ? "Content-aware subcategories"
                                                      : "Подкатегории по содержимому"
                                            onToggleRequested: want => {
                                                ctxToggle.contextAware = want
                                                organizePanel.updateRule(modelData.category,
                                                                         extInput.text, want,
                                                                         subInput.text)
                                            }
                                        }

                                        Column {
                                            width: parent.width
                                            spacing: 4
                                            visible: ctxToggle.contextAware
                                            Text { text: en ? "Subcategories (comma-separated):" : "Подкатегории (через запятую):"; color: Theme.onSurfaceVariant; font.pixelSize: 9 }
                                            Rectangle {
                                                width: parent.width; height: 28; radius: 6
                                                color: Theme.outline
                                                border.width: 1
                                                border.color: subInput.activeFocus ? root.cyan : Theme.outlineStrong
                                                TextInput {
                                                    id: subInput
                                                    anchors.fill: parent
                                                    anchors.margins: 8
                                                    verticalAlignment: TextInput.AlignVCenter
                                                    color: Theme.onSurface
                                                    font.pixelSize: 11
                                                    text: modelData.subcategories
                                                    selectByMouse: true
                                                    onEditingFinished: organizePanel.updateRule(modelData.category, extInput.text, ctxToggle.contextAware, text)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        width: parent.width
                        spacing: Theme.spaceSm

                        JarvisTextField {
                            id: newCatInput
                            Layout.fillWidth: true
                            placeholder: en ? "New category name…" : "Название новой категории…"
                            onAccepted: addBtn.clicked()
                        }
                        JarvisButton {
                            id: addBtn
                            glyph: "+"
                            text: en ? "Add" : "Добавить"
                            enabled: newCatInput.text.trim().length > 0
                            onClicked: {
                                if (newCatInput.text.trim().length === 0)
                                    return
                                organizePanel.addRule(newCatInput.text)
                                newCatInput.clear()
                            }
                        }
                    }
                }
            }
        }
    }
}
