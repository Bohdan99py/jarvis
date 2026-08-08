import QtQuick

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
    color: "#0B0C10"

    readonly property bool en: opEnglish
    readonly property color cyan: "#66FCF1"
    readonly property color teal: "#45A29E"

    function rgba(r, g, b, a) { return Qt.rgba(r / 255, g / 255, b / 255, a) }
    function fmtSize(bytes) {
        if (bytes > 1024 * 1024) return (bytes / 1024 / 1024).toFixed(1) + " MB"
        if (bytes > 1024) return (bytes / 1024).toFixed(0) + " KB"
        return bytes + " B"
    }

    property int currentTab: initialTab
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
            color: "#8a95a5"
            font.pixelSize: 11
            elide: Text.ElideMiddle
        }

        // ---- Tab bar ----
        Row {
            width: parent.width
            height: 34
            spacing: 6

            Repeater {
                model: [en ? "Plan" : "План", en ? "Rules" : "Правила"]
                delegate: Rectangle {
                    width: tabTxt.implicitWidth + 24
                    height: 34
                    radius: 8
                    color: root.currentTab === index ? root.rgba(102, 252, 241, 0.15) : "transparent"
                    border.width: 1
                    border.color: root.currentTab === index ? root.cyan : root.rgba(102, 252, 241, 0.15)
                    Text {
                        id: tabTxt
                        anchors.centerIn: parent
                        text: modelData
                        color: root.currentTab === index ? root.cyan : "#8a95a5"
                        font.pixelSize: 12
                        font.bold: root.currentTab === index
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.currentTab = index
                    }
                }
            }
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
                                color: unsorted ? root.rgba(255, 82, 82, 0.12) : root.rgba(102, 252, 241, 0.08)
                                border.width: 1
                                border.color: unsorted ? root.rgba(255, 82, 82, 0.4) : root.rgba(102, 252, 241, 0.2)
                                Text {
                                    id: chipTxt
                                    anchors.centerIn: parent
                                    text: (unsorted ? "❓ " : "📁 ") + modelData.category + " · " + modelData.count
                                    color: unsorted ? "#ff5252" : root.cyan
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
                                    color: modelData.confident ? root.rgba(102, 252, 241, 0.04) : root.rgba(255, 82, 82, 0.06)
                                    border.width: 1
                                    border.color: modelData.confident ? root.rgba(102, 252, 241, 0.12) : root.rgba(255, 82, 82, 0.25)

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
                                                color: "#e8f0fe"
                                                font.pixelSize: 12
                                                elide: Text.ElideMiddle
                                            }
                                            Text {
                                                width: 90
                                                horizontalAlignment: Text.AlignRight
                                                text: root.fmtSize(modelData.sizeBytes)
                                                color: "#8a95a5"
                                                font.pixelSize: 10
                                            }
                                        }
                                        Row {
                                            spacing: 6
                                            Text {
                                                text: (modelData.confident ? "📁 " : "❓ ") + modelData.category
                                                      + (modelData.subcategory.length > 0 ? " / " + modelData.subcategory : "")
                                                color: modelData.confident ? root.teal : "#ff5252"
                                                font.pixelSize: 10
                                            }
                                            Text {
                                                text: en ? "· change" : "· изменить"
                                                color: root.rgba(102, 252, 241, 0.6)
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
                                                    color: root.rgba(255, 255, 255, 0.06)
                                                    border.width: 1
                                                    border.color: root.rgba(255, 255, 255, 0.15)
                                                    Text {
                                                        id: catTxt
                                                        anchors.centerIn: parent
                                                        text: modelData
                                                        color: "#e8f0fe"
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

                    Row {
                        id: listActions
                        width: parent.width
                        height: 34
                        spacing: 10

                        Rectangle {
                            width: 130; height: 32; radius: 8
                            visible: opTargetFolder.length > 0
                            color: rescanArea.pressed ? root.rgba(102, 252, 241, 0.3) : root.rgba(102, 252, 241, 0.12)
                            border.width: 1; border.color: root.cyan
                            Text { anchors.centerIn: parent; text: en ? "🔄 Rescan" : "🔄 Пересканировать"; color: root.cyan; font.pixelSize: 11 }
                            MouseArea { id: rescanArea; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: organizePanel.rescan() }
                        }
                        Item { width: Math.max(0, parent.width - 130 - 110 - 90 - 110 - 30); height: 1 }
                        Rectangle {
                            width: 110; height: 32; radius: 8
                            color: undoArea.pressed ? root.rgba(255, 255, 255, 0.15) : root.rgba(255, 255, 255, 0.06)
                            border.width: 1; border.color: root.rgba(255, 255, 255, 0.2)
                            Text { anchors.centerIn: parent; text: en ? "↩ Undo Last" : "↩ Отменить"; color: "#c5c6c7"; font.pixelSize: 11 }
                            MouseArea { id: undoArea; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: organizePanel.undoLast() }
                        }
                        Rectangle {
                            width: 90; height: 32; radius: 8
                            color: cancelArea.pressed ? root.rgba(255, 82, 82, 0.3) : root.rgba(255, 82, 82, 0.12)
                            border.width: 1; border.color: root.rgba(255, 82, 82, 0.4)
                            Text { anchors.centerIn: parent; text: en ? "✕ Cancel" : "✕ Отмена"; color: "#ff5252"; font.pixelSize: 11 }
                            MouseArea { id: cancelArea; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: organizePanel.cancelDialog() }
                        }
                        Rectangle {
                            width: 110; height: 32; radius: 8
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: root.teal }
                                GradientStop { position: 1.0; color: root.cyan }
                            }
                            Text { anchors.centerIn: parent; text: en ? "✅ Apply" : "✅ Применить"; color: "#0B0C10"; font.pixelSize: 12; font.bold: true }
                            MouseArea { id: applyArea; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: organizePanel.apply() }
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

                    Row {
                        width: parent.width
                        Text {
                            text: en ? "Category rules — extensions and content-aware subcategories"
                                     : "Правила категорий — расширения и подкатегории по содержимому"
                            color: "#8a95a5"
                            font.pixelSize: 11
                            width: parent.width - 130
                            wrapMode: Text.WordWrap
                        }
                        Rectangle {
                            width: 120; height: 26; radius: 7
                            color: resetArea.pressed ? root.rgba(255, 255, 255, 0.15) : root.rgba(255, 255, 255, 0.06)
                            border.width: 1; border.color: root.rgba(255, 255, 255, 0.2)
                            Text { anchors.centerIn: parent; text: en ? "Reset defaults" : "Сбросить"; color: "#c5c6c7"; font.pixelSize: 10 }
                            MouseArea { id: resetArea; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: organizePanel.resetRules() }
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
                                    color: root.rgba(15, 17, 22, 0.85)
                                    border.width: 1
                                    border.color: root.rgba(102, 252, 241, 0.12)

                                    Column {
                                        id: ruleContent
                                        x: 14; y: 10
                                        width: parent.width - 28
                                        spacing: 6

                                        Row {
                                            width: parent.width
                                            Text {
                                                text: modelData.category
                                                color: root.cyan
                                                font.pixelSize: 13
                                                font.bold: true
                                                width: parent.width - 90
                                            }
                                            Rectangle {
                                                width: 80; height: 22; radius: 6
                                                color: removeArea.pressed ? root.rgba(255, 82, 82, 0.3) : root.rgba(255, 82, 82, 0.12)
                                                border.width: 1; border.color: root.rgba(255, 82, 82, 0.4)
                                                Text { anchors.centerIn: parent; text: en ? "Remove" : "Удалить"; color: "#ff5252"; font.pixelSize: 9 }
                                                MouseArea { id: removeArea; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: organizePanel.removeRule(modelData.category) }
                                            }
                                        }

                                        Text { text: en ? "Extensions (comma-separated):" : "Расширения (через запятую):"; color: "#8a95a5"; font.pixelSize: 9 }
                                        Rectangle {
                                            width: parent.width; height: 28; radius: 6
                                            color: root.rgba(255, 255, 255, 0.05)
                                            border.width: 1
                                            border.color: extInput.activeFocus ? root.cyan : root.rgba(255, 255, 255, 0.15)
                                            TextInput {
                                                id: extInput
                                                anchors.fill: parent
                                                anchors.margins: 8
                                                verticalAlignment: TextInput.AlignVCenter
                                                color: "#e8f0fe"
                                                font.pixelSize: 11
                                                text: modelData.extensions
                                                selectByMouse: true
                                                onEditingFinished: organizePanel.updateRule(modelData.category, text, ctxToggle.checked, subInput.text)
                                            }
                                        }

                                        Row {
                                            spacing: 8
                                            Rectangle {
                                                id: ctxToggle
                                                property bool checked: modelData.contextAware
                                                width: 36; height: 20; radius: 10
                                                color: checked ? root.teal : root.rgba(255, 255, 255, 0.15)
                                                Behavior on color { ColorAnimation { duration: 150 } }
                                                Rectangle {
                                                    width: 16; height: 16; radius: 8
                                                    color: "#0B0C10"
                                                    x: ctxToggle.checked ? parent.width - width - 2 : 2
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    Behavior on x { NumberAnimation { duration: 150 } }
                                                }
                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: {
                                                        ctxToggle.checked = !ctxToggle.checked
                                                        organizePanel.updateRule(modelData.category, extInput.text, ctxToggle.checked, subInput.text)
                                                    }
                                                }
                                            }
                                            Text {
                                                text: en ? "Content-aware subcategories" : "Подкатегории по содержимому"
                                                color: "#c5c6c7"
                                                font.pixelSize: 10
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                        }

                                        Column {
                                            width: parent.width
                                            spacing: 4
                                            visible: ctxToggle.checked
                                            Text { text: en ? "Subcategories (comma-separated):" : "Подкатегории (через запятую):"; color: "#8a95a5"; font.pixelSize: 9 }
                                            Rectangle {
                                                width: parent.width; height: 28; radius: 6
                                                color: root.rgba(255, 255, 255, 0.05)
                                                border.width: 1
                                                border.color: subInput.activeFocus ? root.cyan : root.rgba(255, 255, 255, 0.15)
                                                TextInput {
                                                    id: subInput
                                                    anchors.fill: parent
                                                    anchors.margins: 8
                                                    verticalAlignment: TextInput.AlignVCenter
                                                    color: "#e8f0fe"
                                                    font.pixelSize: 11
                                                    text: modelData.subcategories
                                                    selectByMouse: true
                                                    onEditingFinished: organizePanel.updateRule(modelData.category, extInput.text, ctxToggle.checked, text)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Row {
                        width: parent.width
                        height: 34
                        spacing: 8
                        Rectangle {
                            width: parent.width - 128
                            height: 32
                            radius: 8
                            color: root.rgba(255, 255, 255, 0.05)
                            border.width: 1
                            border.color: newCatInput.activeFocus ? root.cyan : root.rgba(255, 255, 255, 0.15)
                            TextInput {
                                id: newCatInput
                                anchors.fill: parent
                                anchors.margins: 10
                                verticalAlignment: TextInput.AlignVCenter
                                color: "#e8f0fe"
                                font.pixelSize: 12
                                selectByMouse: true
                            }
                            Text {
                                text: en ? "New category name..." : "Название новой категории..."
                                visible: newCatInput.text.length === 0 && !newCatInput.activeFocus
                                color: root.rgba(255, 255, 255, 0.3)
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                font.pixelSize: 12
                            }
                        }
                        Rectangle {
                            width: 110; height: 32; radius: 8
                            color: addArea.pressed ? root.rgba(102, 252, 241, 0.3) : root.rgba(102, 252, 241, 0.12)
                            border.width: 1; border.color: root.cyan
                            Text { anchors.centerIn: parent; text: en ? "+ Add" : "+ Добавить"; color: root.cyan; font.pixelSize: 11 }
                            MouseArea {
                                id: addArea
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (newCatInput.text.trim().length > 0) {
                                        organizePanel.addRule(newCatInput.text)
                                        newCatInput.text = ""
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
