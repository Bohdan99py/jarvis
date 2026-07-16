import QtQuick

// ============================================================
// UserCenter.qml — User Center dashboard.
//
// C++ (UserCenterDialog) owns the data and side effects; this
// file owns the look: user cards, tabs, pills, and the inline
// add-user form. Talks back to C++ only through the "userCenter"
// invokable object.
// ============================================================

Rectangle {
    id: root
    color: "#0B0C10"

    readonly property bool en: ucEnglish
    readonly property color cyan: "#66FCF1"
    readonly property color teal: "#45A29E"

    function rgba(r, g, b, a) { return Qt.rgba(r / 255, g / 255, b / 255, a) }

    property int currentTab: 0
    property bool addFormOpen: false
    property string selectedAddRole: "general"

    readonly property var roleOptions: [
        { value: "programmer", label: en ? "Programmer" : "Программист" },
        { value: "artist",     label: en ? "Artist"     : "Художник" },
        { value: "game_dev",   label: en ? "Game Dev"   : "Геймдев" },
        { value: "tester",     label: en ? "Tester"     : "Тестировщик" },
        { value: "designer",   label: en ? "Designer"   : "Дизайнер" },
        { value: "student",    label: en ? "Student"    : "Студент" },
        { value: "general",    label: en ? "General"    : "Общее" }
    ]
    readonly property var devStyleOptions: [
        { value: "tdd_clean", label: en ? "TDD / Clean code" : "TDD / Чистый код" },
        { value: "ship_fast", label: en ? "Ship fast"        : "Сначала работает" },
        { value: "balanced",  label: en ? "Balanced"         : "Баланс" }
    ]
    readonly property var accentOptions: ["#66FCF1", "#45A29E", "#7c4dff", "#ff5252", "#FFD740", "#00e676"]
    readonly property var hoursOptions: [
        { label: "9–18",  start: 9,  end: 18 },
        { label: "10–19", start: 10, end: 19 },
        { label: "12–21", start: 12, end: 21 },
        { label: en ? "Flexible" : "Гибко", start: 0, end: 24 }
    ]

    Column {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ---- Header ----
        Text {
            text: en ? "USER CENTER" : "ПОЛЬЗОВАТЕЛИ"
            color: root.cyan
            font.pixelSize: 18
            font.bold: true
            font.family: "Segoe UI Semibold"
        }

        // ---- Tab bar ----
        Row {
            width: parent.width
            height: 34
            spacing: 6

            Repeater {
                model: [en ? "Users" : "Пользователи", en ? "My Profile" : "Мой профиль"]
                delegate: Rectangle {
                    width: tabText.implicitWidth + 24
                    height: 34
                    radius: 8
                    color: root.currentTab === index ? root.rgba(102, 252, 241, 0.15) : "transparent"
                    border.width: 1
                    border.color: root.currentTab === index ? root.cyan : root.rgba(102, 252, 241, 0.15)
                    Text {
                        id: tabText
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
            // Tab 0 — Users
            // ======================================================
            Item {
                anchors.fill: parent
                visible: root.currentTab === 0
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }

                Flickable {
                    anchors.fill: parent
                    clip: true
                    contentHeight: userFlow.height + addForm.height + 16
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        width: parent.width
                        spacing: 16

                        Flow {
                            id: userFlow
                            width: parent.width
                            spacing: 12

                            Repeater {
                                model: users
                                delegate: Rectangle {
                                    width: 230
                                    height: 108
                                    radius: 10
                                    color: modelData.isCurrent ? root.rgba(102, 252, 241, 0.10) : root.rgba(15, 17, 22, 0.85)
                                    border.width: modelData.isCurrent ? 2 : 1
                                    border.color: modelData.isCurrent ? root.cyan : root.rgba(102, 252, 241, 0.15)

                                    Rectangle {
                                        visible: modelData.id !== 1
                                        width: 22; height: 22; radius: 6
                                        anchors.top: parent.top
                                        anchors.right: parent.right
                                        anchors.margins: 8
                                        color: delArea.pressed ? root.rgba(255, 82, 82, 0.35) : root.rgba(255, 82, 82, 0.12)
                                        Text { anchors.centerIn: parent; text: "✕"; color: "#ff5252"; font.pixelSize: 11 }
                                        MouseArea {
                                            id: delArea
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: userCenter.deleteUser(modelData.id)
                                        }
                                    }

                                    Column {
                                        x: 14; y: 12
                                        width: parent.width - 28
                                        spacing: 4
                                        Text {
                                            text: modelData.name
                                            color: modelData.isCurrent ? root.cyan : "#e8f0fe"
                                            font.pixelSize: 14
                                            font.bold: true
                                            elide: Text.ElideRight
                                            width: parent.width
                                        }
                                        Text {
                                            text: modelData.role + " · " + modelData.language
                                            color: "#8a95a5"
                                            font.pixelSize: 11
                                        }
                                        Text {
                                            visible: modelData.lastSeen.length > 0
                                            text: (en ? "Last seen: " : "Был(а): ") + modelData.lastSeen
                                            color: "#8a95a5"
                                            font.pixelSize: 9
                                        }
                                        Text {
                                            visible: modelData.isCurrent
                                            text: en ? "● ACTIVE" : "● АКТИВЕН"
                                            color: root.cyan
                                            font.pixelSize: 9
                                            font.bold: true
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        z: -1
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: userCenter.switchUser(modelData.id)
                                    }
                                }
                            }

                            Rectangle {
                                width: 230
                                height: 108
                                radius: 10
                                color: addCardArea.pressed ? root.rgba(102, 252, 241, 0.12) : "transparent"
                                border.width: 1
                                border.color: root.rgba(102, 252, 241, 0.3)

                                Text {
                                    anchors.centerIn: parent
                                    text: en ? "+ Add User" : "+ Добавить"
                                    color: root.cyan
                                    font.pixelSize: 13
                                    font.bold: true
                                }
                                MouseArea {
                                    id: addCardArea
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.addFormOpen = !root.addFormOpen
                                }
                            }
                        }

                        // ---- Inline add-user form ----
                        Rectangle {
                            id: addForm
                            width: parent.width
                            height: addFormOpen ? addFormCol.implicitHeight + 24 : 0
                            radius: 10
                            clip: true
                            color: root.rgba(15, 17, 22, 0.85)
                            border.width: addFormOpen ? 1 : 0
                            border.color: root.rgba(102, 252, 241, 0.15)
                            Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

                            Column {
                                id: addFormCol
                                x: 14; y: 12
                                width: parent.width - 28
                                spacing: 8

                                Text {
                                    text: en ? "New user" : "Новый пользователь"
                                    color: root.cyan
                                    font.pixelSize: 13
                                    font.bold: true
                                }

                                Rectangle {
                                    width: Math.min(parent.width, 300)
                                    height: 32
                                    radius: 8
                                    color: root.rgba(255, 255, 255, 0.05)
                                    border.width: 1
                                    border.color: nameInput.activeFocus ? root.cyan : root.rgba(255, 255, 255, 0.15)
                                    TextInput {
                                        id: nameInput
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        verticalAlignment: TextInput.AlignVCenter
                                        color: "#e8f0fe"
                                        font.pixelSize: 12
                                        clip: true
                                        selectByMouse: true
                                    }
                                    Text {
                                        text: en ? "Name..." : "Имя..."
                                        visible: nameInput.text.length === 0 && !nameInput.activeFocus
                                        color: root.rgba(255, 255, 255, 0.3)
                                        anchors.left: parent.left
                                        anchors.leftMargin: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        font.pixelSize: 12
                                    }
                                }

                                Flow {
                                    width: parent.width
                                    spacing: 6
                                    Repeater {
                                        model: roleOptions
                                        delegate: Rectangle {
                                            radius: 7
                                            height: 26
                                            width: roleTxt.implicitWidth + 18
                                            color: modelData.value === root.selectedAddRole
                                                   ? root.rgba(102, 252, 241, 0.25) : root.rgba(255, 255, 255, 0.06)
                                            border.width: 1
                                            border.color: modelData.value === root.selectedAddRole
                                                          ? root.cyan : root.rgba(255, 255, 255, 0.15)
                                            Text {
                                                id: roleTxt
                                                anchors.centerIn: parent
                                                text: modelData.label
                                                color: "#e8f0fe"
                                                font.pixelSize: 10
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: root.selectedAddRole = modelData.value
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    width: 110; height: 30; radius: 7
                                    color: createArea.pressed ? "#00897b" : "#00695c"
                                    border.width: 1
                                    border.color: "#00897b"
                                    Text {
                                        anchors.centerIn: parent
                                        text: en ? "Create" : "Создать"
                                        color: "#ffffff"
                                        font.pixelSize: 11
                                        font.bold: true
                                    }
                                    MouseArea {
                                        id: createArea
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (nameInput.text.trim().length === 0) return
                                            userCenter.addUser(nameInput.text, root.selectedAddRole)
                                            nameInput.text = ""
                                            root.addFormOpen = false
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ======================================================
            // Tab 1 — My Profile
            // ======================================================
            Item {
                anchors.fill: parent
                visible: root.currentTab === 1
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }

                Flickable {
                    anchors.fill: parent
                    clip: true
                    contentHeight: profileCol.height
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: profileCol
                        width: parent.width
                        spacing: 14

                        Row {
                            width: parent.width
                            Column {
                                spacing: 2
                                Text { text: currentUserName; color: root.cyan; font.pixelSize: 18; font.bold: true }
                                Text {
                                    text: currentUserRole + " · " + currentUserLanguage
                                          + (detectedRole.length > 0 ? " · " + (en ? "detected: " : "определено: ") + detectedRole : "")
                                    color: "#8a95a5"
                                    font.pixelSize: 11
                                }
                            }
                            Item { width: parent.width - 400; height: 1 }
                            Rectangle {
                                width: 150; height: 30; radius: 7
                                color: editArea.pressed ? root.rgba(102, 252, 241, 0.25) : root.rgba(102, 252, 241, 0.1)
                                border.width: 1
                                border.color: root.rgba(102, 252, 241, 0.3)
                                Text {
                                    anchors.centerIn: parent
                                    text: en ? "✏ Edit Full Profile" : "✏ Полный профиль"
                                    color: root.cyan
                                    font.pixelSize: 11
                                }
                                MouseArea {
                                    id: editArea
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: userCenter.openEditProfile()
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 1
                            color: root.rgba(102, 252, 241, 0.12)
                        }

                        // ---- Activity + knowledge ----
                        Row {
                            width: parent.width
                            spacing: 12
                            Rectangle {
                                width: (parent.width - 12) / 2
                                height: 120
                                radius: 10
                                color: root.rgba(15, 17, 22, 0.85)
                                border.width: 1
                                border.color: root.rgba(102, 252, 241, 0.12)
                                Column {
                                    x: 12; y: 10
                                    width: parent.width - 24
                                    spacing: 4
                                    Text { text: en ? "Recent activity (1h)" : "Активность (1ч)"; color: root.cyan; font.pixelSize: 11; font.bold: true }
                                    Text {
                                        width: parent.width
                                        text: activitySummary.length > 0 ? activitySummary : (en ? "No data yet" : "Пока нет данных")
                                        color: "#8a95a5"
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 6
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            Rectangle {
                                width: (parent.width - 12) / 2
                                height: 120
                                radius: 10
                                color: root.rgba(15, 17, 22, 0.85)
                                border.width: 1
                                border.color: root.rgba(102, 252, 241, 0.12)
                                Column {
                                    x: 12; y: 10
                                    width: parent.width - 24
                                    spacing: 4
                                    Text { text: en ? "Knowledge base" : "База знаний"; color: root.cyan; font.pixelSize: 11; font.bold: true }
                                    Text {
                                        width: parent.width
                                        text: knowledgeSummary.length > 0 ? knowledgeSummary : (en ? "No data yet" : "Пока нет данных")
                                        color: "#8a95a5"
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 6
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }

                        Text {
                            text: en ? "PREFERENCES" : "ПРЕДПОЧТЕНИЯ"
                            color: root.cyan
                            font.pixelSize: 13
                            font.bold: true
                            font.family: "Segoe UI Semibold"
                        }

                        // ---- Nickname ----
                        Column {
                            width: parent.width
                            spacing: 4
                            Text { text: en ? "Nickname" : "Никнейм"; color: "#8a95a5"; font.pixelSize: 10 }
                            Rectangle {
                                width: Math.min(parent.width, 300)
                                height: 32
                                radius: 8
                                color: root.rgba(255, 255, 255, 0.05)
                                border.width: 1
                                border.color: nickInput.activeFocus ? root.cyan : root.rgba(255, 255, 255, 0.15)
                                TextInput {
                                    id: nickInput
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    verticalAlignment: TextInput.AlignVCenter
                                    color: "#e8f0fe"
                                    font.pixelSize: 12
                                    clip: true
                                    selectByMouse: true
                                    text: nickname
                                    onAccepted: userCenter.setNickname(text)
                                    onActiveFocusChanged: if (!activeFocus) userCenter.setNickname(text)
                                }
                            }
                        }

                        // ---- Dev style ----
                        Column {
                            width: parent.width
                            spacing: 4
                            Text { text: en ? "Dev style" : "Стиль разработки"; color: "#8a95a5"; font.pixelSize: 10 }
                            Flow {
                                width: parent.width
                                spacing: 6
                                Repeater {
                                    model: devStyleOptions
                                    delegate: Rectangle {
                                        radius: 7
                                        height: 26
                                        width: devTxt.implicitWidth + 18
                                        color: modelData.value === devStyle ? root.rgba(102, 252, 241, 0.25) : root.rgba(255, 255, 255, 0.06)
                                        border.width: 1
                                        border.color: modelData.value === devStyle ? root.cyan : root.rgba(255, 255, 255, 0.15)
                                        Text { id: devTxt; anchors.centerIn: parent; text: modelData.label; color: "#e8f0fe"; font.pixelSize: 10 }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: userCenter.setDevStyle(modelData.value)
                                        }
                                    }
                                }
                            }
                        }

                        // ---- Accent color ----
                        Column {
                            width: parent.width
                            spacing: 4
                            Text { text: en ? "UI accent color" : "Акцентный цвет"; color: "#8a95a5"; font.pixelSize: 10 }
                            Row {
                                spacing: 8
                                Repeater {
                                    model: accentOptions
                                    delegate: Rectangle {
                                        width: 26; height: 26; radius: 13
                                        color: modelData
                                        border.width: modelData === accentColor ? 3 : 1
                                        border.color: modelData === accentColor ? "#ffffff" : root.rgba(255, 255, 255, 0.3)
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: userCenter.setAccentColor(modelData)
                                        }
                                    }
                                }
                            }
                        }

                        // ---- Active hours ----
                        Column {
                            width: parent.width
                            spacing: 4
                            Text { text: en ? "Active hours" : "Рабочие часы"; color: "#8a95a5"; font.pixelSize: 10 }
                            Flow {
                                width: parent.width
                                spacing: 6
                                Repeater {
                                    model: hoursOptions
                                    delegate: Rectangle {
                                        radius: 7
                                        height: 26
                                        width: hrsTxt.implicitWidth + 18
                                        property bool active: modelData.start === activeStart && modelData.end === activeEnd
                                        color: active ? root.rgba(102, 252, 241, 0.25) : root.rgba(255, 255, 255, 0.06)
                                        border.width: 1
                                        border.color: active ? root.cyan : root.rgba(255, 255, 255, 0.15)
                                        Text { id: hrsTxt; anchors.centerIn: parent; text: modelData.label; color: "#e8f0fe"; font.pixelSize: 10 }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: userCenter.setActiveHours(modelData.start, modelData.end)
                                        }
                                    }
                                }
                            }
                        }

                        // ---- Mesh role ----
                        Column {
                            width: parent.width
                            spacing: 4
                            Text { text: en ? "Mesh role" : "Роль в меше"; color: "#8a95a5"; font.pixelSize: 10 }
                            Row {
                                spacing: 6
                                Repeater {
                                    model: [
                                        { value: "primary",   label: en ? "Primary"   : "Основной" },
                                        { value: "secondary", label: en ? "Secondary" : "Дополнительный" }
                                    ]
                                    delegate: Rectangle {
                                        radius: 7
                                        height: 26
                                        width: meshTxt.implicitWidth + 18
                                        color: modelData.value === meshRole ? root.rgba(102, 252, 241, 0.25) : root.rgba(255, 255, 255, 0.06)
                                        border.width: 1
                                        border.color: modelData.value === meshRole ? root.cyan : root.rgba(255, 255, 255, 0.15)
                                        Text { id: meshTxt; anchors.centerIn: parent; text: modelData.label; color: "#e8f0fe"; font.pixelSize: 10 }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: userCenter.setMeshRole(modelData.value)
                                        }
                                    }
                                }
                            }
                        }

                        Item { width: 1; height: 8 }
                    }
                }
            }
        }
    }
}
