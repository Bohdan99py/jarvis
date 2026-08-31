import QtQuick
import QtQuick.Layouts
import Jarvis.Theme
import Jarvis.Controls

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
    color: Theme.bg

    readonly property bool en: ucEnglish
    readonly property color cyan: Theme.accent
    readonly property color teal: Theme.accentMuted


    // Единственный источник истины по вкладке — сам таб-бар: раньше
    // currentTab и подсветка кнопки жили порознь и рассинхронизировались
    // при любом другом способе смены вкладки.
    readonly property int currentTab: tabs.currentIndex
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
    readonly property var accentOptions: [Theme.accent, Theme.accentMuted, Theme.info, Theme.error, Theme.warning, Theme.success]
    readonly property var hoursOptions: [
        { value: "9-18",  label: "9–18",  start: 9,  end: 18 },
        { value: "10-19", label: "10–19", start: 10, end: 19 },
        { value: "12-21", label: "12–21", start: 12, end: 21 },
        { value: "0-24",  label: en ? "Flexible" : "Гибко", start: 0, end: 24 }
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
        JarvisTabBar {
            id: tabs
            titles: [en ? "Users" : "Пользователи", en ? "My Profile" : "Мой профиль"]
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
                                delegate: JarvisCard {
                                    width: 230
                                    height: 108
                                    spine: false
                                    active: modelData.isCurrent
                                    contentSpacing: Theme.spaceXs
                                    onClicked: userCenter.switchUser(modelData.id)

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.spaceXs

                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.name
                                            color: modelData.isCurrent ? root.cyan : Theme.onSurface
                                            font.family: Type.family
                                            font.pixelSize: Type.caption
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                        // Первый пользователь — владелец, его не удаляют.
                                        JarvisButton {
                                            visible: modelData.id !== 1
                                            glyph: "✕"
                                            accessibleName: en ? "Delete user" : "Удалить пользователя"
                                            variant: JarvisButton.Danger
                                            onClicked: userCenter.deleteUser(modelData.id)
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.role + " · " + modelData.language
                                        color: Theme.onSurfaceVariant
                                        font.family: Type.family
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: modelData.lastSeen.length > 0
                                        text: (en ? "Last seen: " : "Был(а): ") + modelData.lastSeen
                                        color: Theme.onSurfaceDim
                                        font.family: Type.family
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }

                                    Item { Layout.fillHeight: true }

                                    JarvisBadge {
                                        visible: modelData.isCurrent
                                        text: en ? "ACTIVE" : "АКТИВЕН"
                                        dot: true
                                    }
                                }
                            }

                            JarvisCard {
                                width: 230
                                height: 108
                                spine: false
                                onClicked: root.addFormOpen = !root.addFormOpen

                                Item { Layout.fillHeight: true }
                                Text {
                                    Layout.fillWidth: true
                                    text: en ? "+ Add User" : "+ Добавить"
                                    color: root.cyan
                                    font.family: Type.family
                                    font.pixelSize: Type.caption
                                    font.weight: Font.DemiBold
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Item { Layout.fillHeight: true }
                            }
                        }

                        // ---- Inline add-user form ----
                        Rectangle {
                            id: addForm
                            width: parent.width
                            height: addFormOpen ? addFormCol.implicitHeight + 24 : 0
                            radius: 10
                            clip: true
                            color: Theme.surface1
                            border.width: addFormOpen ? 1 : 0
                            border.color: Theme.accentSubtle
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

                                JarvisTextField {
                                    id: nameInput
                                    width: Math.min(parent.width, 300)
                                    placeholder: en ? "Name…" : "Имя…"
                                    onAccepted: createBtn.clicked()
                                }

                                JarvisChipGroup {
                                    width: parent.width
                                    options: root.roleOptions
                                    value: root.selectedAddRole
                                    onPicked: v => root.selectedAddRole = v
                                }

                                JarvisButton {
                                    id: createBtn
                                    text: en ? "Create" : "Создать"
                                    variant: JarvisButton.Primary
                                    enabled: nameInput.text.trim().length > 0
                                    onClicked: {
                                        if (nameInput.text.trim().length === 0)
                                            return
                                        userCenter.addUser(nameInput.text, root.selectedAddRole)
                                        nameInput.clear()
                                        root.addFormOpen = false
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

                        // Раньше распорку задавала арифметика
                        // parent.width - 400: правка любой подписи ломала
                        // выравнивание кнопки.
                        RowLayout {
                            width: parent.width
                            spacing: Theme.spaceMd

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    Layout.fillWidth: true
                                    text: currentUserName
                                    color: root.cyan
                                    font.family: Type.family
                                    font.pixelSize: Type.title
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: currentUserRole + " · " + currentUserLanguage
                                          + (detectedRole.length > 0 ? " · " + (en ? "detected: " : "определено: ") + detectedRole : "")
                                    color: Theme.onSurfaceVariant
                                    font.family: Type.family
                                    font.pixelSize: Type.caption
                                    elide: Text.ElideRight
                                }
                            }

                            JarvisButton {
                                glyph: "✏"
                                text: en ? "Edit Full Profile" : "Полный профиль"
                                onClicked: userCenter.openEditProfile()
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 1
                            color: Theme.accentSubtle
                        }

                        // ---- Activity + knowledge ----
                        RowLayout {
                            width: parent.width
                            spacing: Theme.spaceMd

                            JarvisPanel {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 120
                                compact: true
                                title: en ? "Recent activity (1h)" : "Активность (1ч)"

                                Text {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    text: activitySummary.length > 0 ? activitySummary
                                                                     : (en ? "No data yet" : "Пока нет данных")
                                    color: Theme.onSurface
                                    font.family: Type.family
                                    font.pixelSize: Type.caption
                                    lineHeight: Type.lineHeightBody
                                    lineHeightMode: Text.ProportionalHeight
                                    wrapMode: Text.WordWrap
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignTop
                                }
                            }

                            JarvisPanel {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 120
                                compact: true
                                title: en ? "Knowledge base" : "База знаний"

                                Text {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    text: knowledgeSummary.length > 0 ? knowledgeSummary
                                                                      : (en ? "No data yet" : "Пока нет данных")
                                    color: Theme.onSurface
                                    font.family: Type.family
                                    font.pixelSize: Type.caption
                                    lineHeight: Type.lineHeightBody
                                    lineHeightMode: Text.ProportionalHeight
                                    wrapMode: Text.WordWrap
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignTop
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
                            Text { text: en ? "Nickname" : "Никнейм"; color: Theme.onSurfaceVariant; font.pixelSize: 10 }
                            JarvisTextField {
                                id: nickInput
                                width: Math.min(parent.width, 300)
                                text: nickname
                                placeholder: en ? "Nickname…" : "Никнейм…"
                                onAccepted: userCenter.setNickname(text)
                                onActiveFocusChanged: if (!activeFocus) userCenter.setNickname(text)
                            }
                        }

                        // ---- Dev style ----
                        Column {
                            width: parent.width
                            spacing: 4
                            Text { text: en ? "Dev style" : "Стиль разработки"; color: Theme.onSurfaceVariant; font.pixelSize: 10 }
                            JarvisChipGroup {
                                width: parent.width
                                options: root.devStyleOptions
                                value: devStyle
                                onPicked: v => userCenter.setDevStyle(v)
                            }
                        }

                        // ---- Accent color ----
                        Column {
                            width: parent.width
                            spacing: 4
                            Text { text: en ? "UI accent color" : "Акцентный цвет"; color: Theme.onSurfaceVariant; font.pixelSize: 10 }
                            Row {
                                spacing: 8
                                Repeater {
                                    model: accentOptions
                                    delegate: Rectangle {
                                        width: 26; height: 26; radius: 13
                                        color: modelData
                                        border.width: modelData === accentColor ? 3 : 1
                                        border.color: modelData === accentColor ? Theme.onSurface : Theme.onSurfaceDim
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
                            Text { text: en ? "Active hours" : "Рабочие часы"; color: Theme.onSurfaceVariant; font.pixelSize: 10 }
                            JarvisChipGroup {
                                width: parent.width
                                options: root.hoursOptions
                                // Ключ — строка "start-end": сравнивать
                                // объекты по === бесполезно.
                                value: activeStart + "-" + activeEnd
                                onPicked: v => {
                                    const o = root.hoursOptions.find(x => x.value === v)
                                    if (o) userCenter.setActiveHours(o.start, o.end)
                                }
                            }
                        }

                        // ---- Mesh role ----
                        Column {
                            width: parent.width
                            spacing: 4
                            Text { text: en ? "Mesh role" : "Роль в меше"; color: Theme.onSurfaceVariant; font.pixelSize: 10 }
                            JarvisChipGroup {
                                width: parent.width
                                options: [
                                    { value: "primary",   label: en ? "Primary"   : "Основной" },
                                    { value: "secondary", label: en ? "Secondary" : "Дополнительный" }
                                ]
                                value: meshRole
                                onPicked: v => userCenter.setMeshRole(v)
                            }
                        }

                        Item { width: 1; height: 8 }
                    }
                }
            }
        }
    }
}
