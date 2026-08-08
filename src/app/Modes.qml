import QtQuick
import QtQuick.Layouts
import Jarvis.Theme

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
    color: Theme.bg

    readonly property bool en: modesEnglish

    // modesList — [{ id, name, description, icon, accent,
    //               enableSkills, disableSkills, exclusive, active }]
    // activeId  — id текущего режима, "" если не выбран
    property string previewId: activeId

    onActiveIdChanged: if (previewId === "") previewId = activeId

    function findMode(id) {
        for (var i = 0; i < modesList.length; ++i)
            if (modesList[i].id === id) return modesList[i]
        return null
    }

    // Режим, показанный в правой панели. Держим одним свойством на
    // корне, чтобы вложенные элементы обращались по имени, а не через
    // цепочки parent.parent — те молча ломаются от любой обёртки.
    readonly property var previewMode: findMode(previewId)
    function accentOf(m) {
        return (m && m.accent && String(m.accent).length > 0) ? m.accent : Theme.accent
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceXl
        spacing: Theme.spaceLg

        // ============== ЛЕВАЯ КОЛОНКА: сетка карточек ==============
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spaceMd

            Text {
                text: root.en ? "Work modes" : "Режимы работы"
                color: Theme.onSurface
                font.pixelSize: Type.heading
                font.family: Type.family
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                Layout.maximumWidth: Type.measureMax
                wrapMode: Text.WordWrap
                color: Theme.onSurfaceVariant
                font.pixelSize: Type.caption
                font.family: Type.family
                lineHeight: Type.lineHeightBody
                lineHeightMode: Text.ProportionalHeight
                text: root.en
                    ? "A mode is a behavioral profile. Switching one toggles the right set of skills and adjusts JARVIS's focus — his character stays the same."
                    : "Режим — это профиль поведения. Переключаешь режим — JARVIS включает нужные скиллы и меняет фокус; характер при этом остаётся прежним."
            }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: grid.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                GridLayout {
                    id: grid
                    width: parent.width
                    columns: Math.max(1, Math.floor(width / 260))
                    rowSpacing: Theme.spaceMd
                    columnSpacing: Theme.spaceMd

                    Repeater {
                        model: modesList

                        delegate: Rectangle {
                            id: card
                            Layout.fillWidth: true
                            Layout.preferredHeight: 132

                            readonly property bool isActive:  modelData.id === activeId
                            readonly property bool isPreview: modelData.id === root.previewId
                            readonly property color accent:   root.accentOf(modelData)

                            radius: Theme.radiusMd
                            // Выбор показываем ступенью поверхности, а не
                            // толщиной рамки: изменение border.width сдвигает
                            // содержимое и читается как дрожание.
                            color: isPreview ? Theme.surface3
                                 : cardArea.containsMouse ? Theme.surface2
                                 : Theme.surface1
                            border.width: 1
                            border.color: isActive ? accent
                                        : isPreview ? Theme.outlineStrong
                                        : Theme.outline

                            Behavior on color {
                                ColorAnimation { duration: Theme.motionFast * Theme.motionScale }
                            }

                            MouseArea {
                                id: cardArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.previewId = modelData.id
                                onDoubleClicked: modesCtl.activate(modelData.id)
                            }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: Theme.spaceMd
                                spacing: Theme.spaceSm

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spaceSm

                                    Text {
                                        text: modelData.icon && modelData.icon.length > 0
                                              ? modelData.icon : "•"
                                        font.pixelSize: Type.title
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        color: Theme.onSurface
                                        font.pixelSize: Type.body
                                        font.family: Type.family
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    // Метка активного режима: точка + текст,
                                    // а не только цвет — состояние читается
                                    // и без различения оттенков.
                                    Rectangle {
                                        visible: card.isActive
                                        radius: Theme.radiusPill
                                        color: Theme.accentSubtle
                                        implicitWidth: activeRow.implicitWidth + Theme.spaceMd
                                        implicitHeight: 20
                                        Row {
                                            id: activeRow
                                            anchors.centerIn: parent
                                            spacing: 5
                                            Rectangle {
                                                width: 6; height: 6; radius: 3
                                                color: card.accent
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Text {
                                                text: root.en ? "Active" : "Активен"
                                                color: card.accent
                                                font.pixelSize: 11
                                                font.family: Type.family
                                                font.weight: Font.DemiBold
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                        }
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    text: modelData.description
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: Type.caption
                                    font.family: Type.family
                                    lineHeight: 1.4
                                    lineHeightMode: Text.ProportionalHeight
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 3
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignTop
                                }
                            }
                        }
                    }
                }
            }
        }

        // ============== ПРАВАЯ КОЛОНКА: детали ==============
        Rectangle {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            radius: Theme.radiusMd
            color: Theme.surface1
            border.width: 1
            border.color: Theme.outline

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spaceLg
                spacing: Theme.spaceMd

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceMd

                    Text {
                        text: root.previewMode && root.previewMode.icon ? root.previewMode.icon : "•"
                        font.pixelSize: Type.heading
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.previewMode ? root.previewMode.name
                                              : (root.en ? "No mode selected" : "Режим не выбран")
                        color: Theme.onSurface
                        font.pixelSize: Type.title
                        font.family: Type.family
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: Theme.outline
                }

                Text {
                    Layout.fillWidth: true
                    text: root.previewMode ? root.previewMode.description : ""
                    color: Theme.onSurfaceVariant
                    font.pixelSize: Type.caption
                    font.family: Type.family
                    lineHeight: Type.lineHeightBody
                    lineHeightMode: Text.ProportionalHeight
                    wrapMode: Text.WordWrap
                }

                // Что режим включит и что выключит
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    Repeater {
                        model: [
                            { key: "enableSkills",
                              label: root.en ? "Enables"  : "Включает" },
                            { key: "disableSkills",
                              label: root.en ? "Disables" : "Отключает" }
                        ]
                        // Вложенный Repeater перекрывает modelData своим,
                        // поэтому у внешнего делегата есть id: обращаться
                        // к его данным изнутри можно только по имени.
                        delegate: ColumnLayout {
                            id: skillGroup
                            readonly property var list: {
                                if (!root.previewMode) return []
                                return root.previewMode[modelData.key] || []
                            }
                            visible: list.length > 0
                            Layout.fillWidth: true
                            spacing: Theme.spaceXs

                            Text {
                                text: modelData.label
                                color: Theme.onSurfaceDim
                                font.pixelSize: 11
                                font.family: Type.family
                                font.weight: Font.DemiBold
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: Theme.spaceXs
                                Repeater {
                                    model: skillGroup.list
                                    delegate: Rectangle {
                                        radius: Theme.radiusSm
                                        color: Theme.surface2
                                        border.width: 1
                                        border.color: Theme.outline
                                        implicitWidth: chipText.implicitWidth + Theme.spaceMd
                                        implicitHeight: 22
                                        Text {
                                            id: chipText
                                            anchors.centerIn: parent
                                            text: modelData
                                            color: Theme.onSurfaceVariant
                                            font.pixelSize: 11
                                            font.family: Type.familyMono
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.previewMode && root.previewMode.exclusive === true
                        text: root.en
                            ? "⚠ Exclusive — turns off every other skill."
                            : "⚠ Эксклюзивный — выключает все остальные скиллы."
                        color: Theme.warning
                        font.pixelSize: 11
                        font.family: Type.family
                        wrapMode: Text.WordWrap
                    }
                }

                Item { Layout.fillHeight: true }

                // Единственная акцентная кнопка на экране
                Rectangle {
                    id: applyBtn
                    readonly property bool isActive: root.previewMode && root.previewMode.id === activeId
                    readonly property bool canApply: root.previewMode && !isActive

                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    radius: Theme.radiusSm
                    color: !canApply ? Theme.surface2
                         : applyArea.pressed ? Theme.accentMuted
                         : Theme.accent
                    Behavior on color {
                        ColorAnimation { duration: Theme.motionFast * Theme.motionScale }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: !root.previewMode ? "—"
                            : applyBtn.isActive ? (root.en ? "Currently active" : "Сейчас активен")
                            : (root.en ? "Activate this mode" : "Включить этот режим")
                        color: applyBtn.canApply ? Theme.onAccent : Theme.onSurfaceDim
                        font.pixelSize: Type.caption
                        font.family: Type.family
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        id: applyArea
                        anchors.fill: parent
                        enabled: applyBtn.canApply
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: modesCtl.activate(root.previewMode.id)
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: activeId.length > 0
                    horizontalAlignment: Text.AlignHCenter
                    text: root.en ? "Clear active mode" : "Снять активный режим"
                    color: clearArea.containsMouse ? Theme.onSurface : Theme.onSurfaceDim
                    font.pixelSize: Type.caption
                    font.family: Type.family
                    MouseArea {
                        id: clearArea
                        anchors.fill: parent
                        anchors.margins: -Theme.spaceSm
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: modesCtl.activate("")
                    }
                }
            }
        }
    }
}
