import QtQuick
import QtQuick.Layouts

// ============================================================
// Modes.qml — Work modes / behavioral profiles.
//
// C++ (ModesDialog) owns the data and applies the mode; this
// file owns the look. Cards for each mode, click to preview,
// button to activate. Talks back to C++ through the "modesCtl"
// context property.
// ============================================================

Rectangle {
    id: root
    color: "#0B0C10"

    readonly property bool en: modesEnglish
    readonly property color cyan: "#66FCF1"
    readonly property color teal: "#45A29E"
    readonly property color dimText: "#8fa3b0"

    function rgba(r, g, b, a) { return Qt.rgba(r / 255, g / 255, b / 255, a) }

    // ----- Данные, обновляемые из C++ -----
    // modesList — [{ id, name, description, icon, accent,
    //               enableSkills, disableSkills, exclusive, active }]
    // activeId  — id текущего режима, "" если не выбран
    // previewId — локальный выбор карточки для правой панели

    property string previewId: activeId

    onActiveIdChanged: if (previewId === "") previewId = activeId

    function findMode(id) {
        for (var i = 0; i < modesList.length; ++i)
            if (modesList[i].id === id) return modesList[i]
        return null
    }

    // ----------------------------------------------------------
    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 14

        // ============== ЛЕВАЯ КОЛОНКА: сетка карточек ==============
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                Text {
                    text: root.en ? "WORK MODES" : "РЕЖИМЫ РАБОТЫ"
                    color: root.cyan
                    font.pixelSize: 18
                    font.bold: true
                    font.family: "Segoe UI Semibold"
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: root.dimText
                    font.pixelSize: 12
                    text: root.en
                        ? "A mode is a behavioral profile (like a Solution Configuration in Visual Studio). Switching a mode toggles the right set of skills and adjusts JARVIS's tone — without rewriting his character."
                        : "Режим — это профиль поведения (как «Solution Configuration» в Visual Studio). Переключаешь режим — JARVIS сам включает нужные скиллы и подстраивает тон, не ломая своего характера."
                }

                // Сетка карточек
                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: width
                    contentHeight: grid.implicitHeight
                    clip: true

                    GridLayout {
                        id: grid
                        width: parent.width
                        columns: Math.max(1, Math.floor(width / 240))
                        rowSpacing: 10
                        columnSpacing: 10

                        Repeater {
                            model: modesList

                            delegate: Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 128
                                radius: 10
                                color: root.rgba(15, 22, 38, 0.9)

                                readonly property bool isActive:  modelData.id === activeId
                                readonly property bool isPreview: modelData.id === root.previewId
                                readonly property color accent: modelData.accent
                                                                && modelData.accent.length > 0
                                                                ? modelData.accent : root.cyan

                                border.width: isPreview ? 2 : 1
                                border.color: isActive
                                    ? accent
                                    : (isPreview ? root.rgba(102, 252, 241, 0.7)
                                                 : root.rgba(102, 252, 241, 0.15))

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.previewId = modelData.id
                                    onDoubleClicked: modesCtl.activate(modelData.id)
                                }

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 6

                                    Row {
                                        spacing: 8
                                        width: parent.width

                                        Text {
                                            text: modelData.icon && modelData.icon.length > 0
                                                  ? modelData.icon : "•"
                                            font.pixelSize: 22
                                            color: parent.parent.parent.accent
                                        }
                                        Text {
                                            text: modelData.name
                                            color: "#ecf0f1"
                                            font.pixelSize: 15
                                            font.bold: true
                                            font.family: "Segoe UI Semibold"
                                            width: parent.width - 34
                                            elide: Text.ElideRight
                                        }
                                    }

                                    Text {
                                        text: modelData.description
                                        color: root.dimText
                                        font.pixelSize: 11
                                        wrapMode: Text.WordWrap
                                        width: parent.width
                                        maximumLineCount: 3
                                        elide: Text.ElideRight
                                    }

                                    // "Активен" / "Двойной клик — включить"
                                    Row {
                                        spacing: 6
                                        Rectangle {
                                            visible: parent.parent.parent.isActive
                                            radius: 4
                                            color: root.rgba(102, 252, 241, 0.15)
                                            border.width: 1
                                            border.color: parent.parent.parent.accent
                                            width: activeLbl.implicitWidth + 12
                                            height: 20
                                            Text {
                                                id: activeLbl
                                                anchors.centerIn: parent
                                                text: root.en ? "ACTIVE" : "АКТИВНО"
                                                color: parent.parent.parent.parent.accent
                                                font.pixelSize: 10
                                                font.bold: true
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

        // ============== ПРАВАЯ КОЛОНКА: детали и Apply ==============
        Rectangle {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            radius: 10
            color: root.rgba(15, 22, 38, 0.9)
            border.width: 1
            border.color: root.rgba(102, 252, 241, 0.15)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                // Заголовок с иконкой
                Row {
                    spacing: 10
                    Layout.fillWidth: true
                    Text {
                        readonly property var m: root.findMode(root.previewId)
                        text: m && m.icon ? m.icon : "•"
                        font.pixelSize: 26
                        color: {
                            var m = root.findMode(root.previewId)
                            return (m && m.accent && m.accent.length > 0) ? m.accent : root.cyan
                        }
                    }
                    Text {
                        readonly property var m: root.findMode(root.previewId)
                        text: m ? m.name : (root.en ? "No mode selected" : "Режим не выбран")
                        color: "#ecf0f1"
                        font.pixelSize: 16
                        font.bold: true
                        font.family: "Segoe UI Semibold"
                        wrapMode: Text.WordWrap
                        width: parent.width - 44
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: root.rgba(102, 252, 241, 0.15)
                }

                // Описание
                Text {
                    readonly property var m: root.findMode(root.previewId)
                    text: m ? m.description : ""
                    color: "#ecf0f1"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                // Какие скиллы включает / выключает
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        readonly property var m: root.findMode(root.previewId)
                        visible: m && m.enableSkills && m.enableSkills.length > 0
                        text: {
                            var m = root.findMode(root.previewId)
                            if (!m || !m.enableSkills) return ""
                            return (root.en ? "Enables: " : "Включает: ")
                                   + m.enableSkills.join(", ")
                        }
                        color: root.teal
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        readonly property var m: root.findMode(root.previewId)
                        visible: m && m.disableSkills && m.disableSkills.length > 0
                        text: {
                            var m = root.findMode(root.previewId)
                            if (!m || !m.disableSkills) return ""
                            return (root.en ? "Disables: " : "Отключает: ")
                                   + m.disableSkills.join(", ")
                        }
                        color: "#ff8080"
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        readonly property var m: root.findMode(root.previewId)
                        visible: m && m.exclusive === true
                        text: root.en
                            ? "Exclusive: turns off every other skill."
                            : "Эксклюзивный: выключает ВСЕ другие скиллы."
                        color: "#FFD740"
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                Item { Layout.fillHeight: true }   // spacer

                // Кнопки
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Rectangle {
                        readonly property var m: root.findMode(root.previewId)
                        readonly property bool isActive: m && m.id === activeId
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        radius: 6
                        color: isActive ? root.rgba(102, 252, 241, 0.2)
                                        : root.rgba(102, 252, 241, 0.35)
                        border.width: 1
                        border.color: root.cyan
                        opacity: m ? 1.0 : 0.4

                        Text {
                            anchors.centerIn: parent
                            text: {
                                var m = root.findMode(root.previewId)
                                if (!m)          return root.en ? "—" : "—"
                                if (m.id === activeId)
                                    return root.en ? "✓ Currently active" : "✓ Сейчас активен"
                                return root.en ? "Activate this mode" : "Включить этот режим"
                            }
                            color: root.cyan
                            font.pixelSize: 13
                            font.bold: true
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: parent.m && parent.m.id !== activeId
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: modesCtl.activate(parent.m.id)
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    radius: 6
                    color: "transparent"
                    border.width: 1
                    border.color: root.rgba(255, 255, 255, 0.12)
                    visible: activeId.length > 0

                    Text {
                        anchors.centerIn: parent
                        text: root.en ? "Clear active mode" : "Снять активный режим"
                        color: root.dimText
                        font.pixelSize: 11
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: modesCtl.activate("")
                    }
                }
            }
        }
    }
}
