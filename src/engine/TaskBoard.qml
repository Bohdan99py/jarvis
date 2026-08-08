import QtQuick
import Jarvis.Theme

// ============================================================
// TaskBoard.qml — Kanban board visuals for TaskManagerDialog.
//
// C++ (TaskManagerDialog) owns the data and persistence; this
// file owns everything about how it looks and animates: the
// progress bar, the glass cards, the glow, and the transitions
// that play when a card is added, removed, or moved between
// columns. Talks back to C++ only through the "taskBoard"
// invokable object (moveTask / openTask / requestAdd).
// ============================================================

Rectangle {
    id: root
    color: Theme.bg

    readonly property bool en: boardEnglish
    readonly property color cyan: Theme.accent
    readonly property color teal: Theme.accentMuted

    function nextStatus(s) {
        return s === "Todo" ? "InProgress" : (s === "InProgress" ? "Done" : "")
    }
    function prevStatus(s) {
        return s === "Done" ? "InProgress" : (s === "InProgress" ? "Todo" : "")
    }
    function statusLabel(s) {
        if (s === "Todo") return en ? "TO DO" : "СДЕЛАТЬ"
        if (s === "InProgress") return en ? "IN PROGRESS" : "В РАБОТЕ"
        return en ? "DONE" : "ГОТОВО"
    }

    Column {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        // ---- Header ----
        Row {
            width: parent.width
            height: 32
            Text {
                text: en ? "TASK MANAGER" : "МЕНЕДЖЕР ЗАДАЧ"
                color: root.cyan
                font.pixelSize: 18
                font.bold: true
                font.family: "Segoe UI Semibold"
                anchors.verticalCenter: parent.verticalCenter
            }
            Item { width: parent.width - 260; height: 1 }
            Rectangle {
                width: 130
                height: 30
                radius: 6
                anchors.verticalCenter: parent.verticalCenter
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: root.teal }
                    GradientStop { position: 1.0; color: root.cyan }
                }
                Text {
                    anchors.centerIn: parent
                    text: en ? "+ NEW TASK" : "+ ЗАДАЧА"
                    color: Theme.bg
                    font.bold: true
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: taskBoard.requestAdd()
                }
            }
        }

        // ---- Progress bar ----
        Column {
            width: parent.width
            spacing: 4

            Row {
                width: parent.width
                Text {
                    text: en ? "Progress: %1 of %2 done".arg(boardDoneCount).arg(boardTotalCount)
                             : "Прогресс: %1 из %2 готово".arg(boardDoneCount).arg(boardTotalCount)
                    color: Theme.onSurfaceVariant
                    font.pixelSize: 11
                }
                Item { width: parent.width - 260; height: 1 }
                Text {
                    text: Math.round(boardProgress * 100) + "%"
                    color: root.cyan
                    font.pixelSize: 11
                    font.bold: true
                }
            }
            Rectangle {
                width: parent.width
                height: 8
                radius: 4
                color: Theme.outline
                border.width: 1
                border.color: Theme.accentSubtle

                Rectangle {
                    id: fill
                    height: parent.height
                    radius: 4
                    width: parent.width * boardProgress
                    Behavior on width { NumberAnimation { duration: 450; easing.type: Easing.OutCubic } }
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: root.teal }
                        GradientStop { position: 1.0; color: root.cyan }
                    }
                }
            }
        }

        // ---- Separator ----
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

        // ---- Columns ----
        Row {
            width: parent.width
            height: parent.height - y
            spacing: 12

            Repeater {
                model: [
                    { status: "Todo",       listModel: todoModel },
                    { status: "InProgress", listModel: inProgressModel },
                    { status: "Done",       listModel: doneModel }
                ]

                delegate: Rectangle {
                    width: (root.width - 32 - 24) / 3
                    height: parent.height
                    radius: 10
                    color: Theme.surface1
                    border.width: 1
                    border.color: Theme.accentSubtle

                    property string columnStatus: modelData.status

                    Column {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: root.statusLabel(modelData.status)
                            color: root.cyan
                            font.pixelSize: 14
                            font.bold: true
                            font.family: "Segoe UI Semibold"
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: modelData.listModel.count
                                  + " " + (modelData.listModel.count === 1
                                           ? (en ? "task" : "задача")
                                           : (en ? "tasks" : "задач"))
                            color: root.teal
                            font.pixelSize: 11
                        }

                        Flickable {
                            width: parent.width
                            height: parent.height - 46
                            clip: true
                            contentHeight: cardColumn.height
                            boundsBehavior: Flickable.StopAtBounds

                            Column {
                                id: cardColumn
                                width: parent.width
                                spacing: 8

                                add: Transition {
                                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 220 }
                                    NumberAnimation { properties: "y"; duration: 220; easing.type: Easing.OutCubic }
                                }
                                move: Transition {
                                    NumberAnimation { properties: "y"; duration: 220; easing.type: Easing.OutCubic }
                                }
                                populate: Transition {
                                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 }
                                }

                                Repeater {
                                    model: modelData.listModel

                                    delegate: Rectangle {
                                        id: card
                                        width: cardColumn.width
                                        height: cardCol.implicitHeight + 20
                                        radius: 8
                                        color: overdue ? Qt.alpha(Theme.error, 0.10)
                                                        : Theme.outline
                                        border.width: 1
                                        border.color: overdue ? Qt.alpha(Theme.error, 0.5)
                                                               : Theme.accentSubtle

                                        Rectangle {
                                            x: 0; y: 8
                                            width: 3
                                            height: parent.height - 16
                                            radius: 1.5
                                            color: priorityColor
                                        }

                                        Column {
                                            id: cardCol
                                            x: 12; y: 10
                                            width: parent.width - 24
                                            spacing: 3

                                            Text {
                                                width: parent.width
                                                text: title
                                                color: Theme.onSurface
                                                font.pixelSize: 12
                                                font.bold: true
                                                wrapMode: Text.WordWrap
                                            }
                                            Text {
                                                width: parent.width
                                                text: category + " · " + priority
                                                      + (deadlineText.length > 0 ? " · " + deadlineText : "")
                                                color: overdue ? Theme.error : Theme.onSurfaceVariant
                                                font.pixelSize: 10
                                                wrapMode: Text.WordWrap
                                            }

                                            Row {
                                                spacing: 8
                                                topPadding: 4

                                                Rectangle {
                                                    visible: root.prevStatus(columnStatus) !== ""
                                                    width: 22; height: 20; radius: 5
                                                    color: prevArea.pressed ? Theme.outlineStrong
                                                                             : Theme.accentSubtle
                                                    Text { anchors.centerIn: parent; text: "◀"; color: root.cyan; font.pixelSize: 10 }
                                                    MouseArea {
                                                        id: prevArea
                                                        anchors.fill: parent
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: taskBoard.moveTask(taskId, columnStatus, root.prevStatus(columnStatus))
                                                    }
                                                }
                                                Rectangle {
                                                    visible: root.nextStatus(columnStatus) !== ""
                                                    width: 22; height: 20; radius: 5
                                                    color: nextArea.pressed ? Theme.outlineStrong
                                                                             : Theme.accentSubtle
                                                    Text { anchors.centerIn: parent; text: "▶"; color: root.cyan; font.pixelSize: 10 }
                                                    MouseArea {
                                                        id: nextArea
                                                        anchors.fill: parent
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: taskBoard.moveTask(taskId, columnStatus, root.nextStatus(columnStatus))
                                                    }
                                                }
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            z: -1
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: taskBoard.openTask(taskId)
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
}
