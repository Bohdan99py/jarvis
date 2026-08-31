import QtQuick
import QtQuick.Layouts
import Jarvis.Theme
import Jarvis.Controls

// ============================================================
// Modes.qml — Work modes / behavioral profiles.
//
// C++ (ModesDialog) владеет данными и применяет режим; этот файл
// владеет видом. Всё, что приходит из C++, читается через объект
// modesCtl (Q_PROPERTY + NOTIFY) и зеркалится в свойства root —
// только у настоящего свойства есть сигнал xChanged, на который
// можно повесить обработчик. Раньше здесь стоял onActiveIdChanged
// поверх контекстного свойства: сигнала с таким именем не
// существовало, QML считал это ошибкой и НЕ создавал компонент —
// экран открывался пустым.
// ============================================================

Rectangle {
    id: root
    color: Theme.bg
    focus: true

    // ---- Данные из C++ ---------------------------------------
    readonly property bool   en:       modesCtl.english
    readonly property var    modes:    modesCtl.modes
    readonly property string activeId: modesCtl.activeId

    // ---- Состояние экрана ------------------------------------
    property string previewId: ""
    property string query: ""

    readonly property int dur:      Theme.motionBase * Theme.motionScale
    readonly property int durFast:  Theme.motionFast * Theme.motionScale

    readonly property var shownModes: {
        const f = query.trim().toLowerCase()
        if (f.length === 0)
            return modes
        var out = []
        for (var i = 0; i < modes.length; ++i) {
            const m = modes[i]
            const hay = (m.name + " " + m.description + " " + m.id).toLowerCase()
            if (hay.indexOf(f) >= 0)
                out.push(m)
        }
        return out
    }

    function findMode(id) {
        for (var i = 0; i < modes.length; ++i)
            if (modes[i].id === id)
                return modes[i]
        return null
    }

    function accentOf(m) {
        return (m && m.accent && String(m.accent).length > 0) ? m.accent : Theme.accent
    }

    // Выделение всегда должно указывать на существующий режим:
    // после rescan/фильтра прежний id может исчезнуть.
    function syncPreview() {
        if (findMode(previewId))
            return
        previewId = activeId.length > 0 ? activeId
                  : (modes.length > 0 ? modes[0].id : "")
    }

    readonly property var previewMode: findMode(previewId)

    onModesChanged: syncPreview()
    onActiveIdChanged: syncPreview()
    Component.onCompleted: syncPreview()

    // Клавиатура: стрелки — по карточкам, Enter — включить.
    function step(delta) {
        const list = shownModes
        if (list.length === 0)
            return
        var idx = -1
        for (var i = 0; i < list.length; ++i)
            if (list[i].id === previewId) { idx = i; break }
        idx = Math.max(0, Math.min(list.length - 1, idx + delta))
        previewId = list[idx].id
        grid.positionViewAtIndex(idx, GridView.Contain)
    }

    Keys.onDownPressed:  step(grid.columns)
    Keys.onUpPressed:    step(-grid.columns)
    Keys.onRightPressed: step(1)
    Keys.onLeftPressed:  step(-1)
    Keys.onReturnPressed: if (previewMode && previewMode.id !== activeId)
                              modesCtl.activate(previewMode.id)
    Keys.onEnterPressed:  if (previewMode && previewMode.id !== activeId)
                              modesCtl.activate(previewMode.id)

    // Мягкое свечение под шапкой: единственное «украшение» на экране,
    // подкрашивается акцентом выбранного режима.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 190
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: JarvisUi.tint(root.accentOf(root.previewMode), 0.10)
                Behavior on color { ColorAnimation { duration: root.dur } }
            }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceXl
        spacing: Theme.spaceLg

        // ================= ШАПКА =================
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceLg

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

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
                    text: root.en
                        ? "A mode is a behavioral profile. Switching one toggles the right set of skills and adjusts JARVIS's focus — his character stays the same."
                        : "Режим — это профиль поведения. Переключаешь режим — JARVIS включает нужные скиллы и меняет фокус; характер при этом остаётся прежним."
                }
            }

            // Живой индикатор активного режима
            Rectangle {
                id: activeChip
                readonly property var mode: root.findMode(root.activeId)
                visible: mode !== null
                radius: Theme.radiusPill
                color: JarvisUi.tint(root.accentOf(mode), 0.12)
                border.width: 1
                border.color: JarvisUi.tint(root.accentOf(mode), 0.45)
                implicitWidth: activeChipRow.implicitWidth + Theme.spaceLg * 2
                implicitHeight: 34
                Behavior on color { ColorAnimation { duration: root.dur } }

                Row {
                    id: activeChipRow
                    anchors.centerIn: parent
                    spacing: Theme.spaceSm

                    Rectangle {
                        width: 8; height: 8; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.accentOf(activeChip.mode)
                        SequentialAnimation on opacity {
                            running: Theme.motionScale > 0
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.35; duration: 900; easing.type: Easing.InOutQuad }
                            NumberAnimation { to: 1.0;  duration: 900; easing.type: Easing.InOutQuad }
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: activeChip.mode
                              ? ((activeChip.mode.icon ? activeChip.mode.icon + " " : "")
                                 + activeChip.mode.name)
                              : ""
                        color: Theme.onSurface
                        font.pixelSize: Type.caption
                        font.family: Type.family
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        // ================= ТЕЛО =================
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spaceLg

            // ---------- Левая колонка: поиск + сетка ----------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spaceMd

                // Поиск
                JarvisSearchField {
                    Layout.fillWidth: true
                    visible: root.modes.length > 4
                    placeholder: root.en ? "Search modes…" : "Поиск режима…"
                    onTextChanged: root.query = text
                }

                // Сетка карточек
                GridView {
                    id: grid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.shownModes.length > 0
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.shownModes
                    cacheBuffer: 400

                    // 260 — минимальная ширина, при которой название режима
                    // помещается в строку. При ширине диалога 960 (минимум)
                    // это даёт две колонки, а не одну.
                    readonly property int columns: Math.max(1, Math.floor(width / 260))
                    cellWidth: Math.floor(width / columns)
                    cellHeight: 146

                    populate: Transition {
                        NumberAnimation {
                            properties: "opacity"
                            from: 0; to: 1
                            duration: root.dur
                            easing.type: Easing.OutCubic
                        }
                    }

                    delegate: Item {
                        id: cell
                        // modelData/index приходят из модели в контекст
                        // делегата; сразу перекладываем в объявленные
                        // свойства — иначе вложенные элементы (у них свой
                        // контекст) до них не достучатся по имени cell.*.
                        property var mode: modelData

                        width: grid.cellWidth
                        height: grid.cellHeight

                        JarvisCard {
                            id: card
                            anchors.fill: parent
                            anchors.rightMargin: Theme.spaceMd
                            anchors.bottomMargin: Theme.spaceMd

                            readonly property color accent: root.accentOf(cell.mode)

                            // selected — куда смотрим, active — что включено.
                            // Подъём под курсором, ступень поверхности и
                            // цветной корешок слева живут внутри JarvisCard.
                            selected:    cell.mode.id === root.previewId
                            active:      cell.mode.id === root.activeId
                            accentColor: accent

                            onClicked: {
                                root.previewId = cell.mode.id
                                root.forceActiveFocus()
                            }
                            onDoubleClicked: modesCtl.activate(cell.mode.id)

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceSm

                                Rectangle {
                                    Layout.alignment: Qt.AlignTop
                                    implicitWidth: 34
                                    implicitHeight: 34
                                    radius: Theme.radiusSm
                                    color: JarvisUi.tint(card.accent, 0.12)
                                    Text {
                                        anchors.centerIn: parent
                                        text: cell.mode.icon && cell.mode.icon.length > 0
                                              ? cell.mode.icon : "◆"
                                        color: card.accent
                                        font.pixelSize: Type.body
                                    }
                                }

                                // Название переносим, а не обрезаем: рядом
                                // стоит метка «Активен», и на узкой карточке
                                // от однострочного заголовка оставалось
                                // «Разработка…» — самое нужное слово терялось.
                                Text {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop
                                    text: cell.mode.name
                                    color: Theme.onSurface
                                    font.pixelSize: Type.body
                                    font.family: Type.family
                                    font.weight: Font.DemiBold
                                    lineHeight: 1.15
                                    lineHeightMode: Text.ProportionalHeight
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }

                                JarvisBadge {
                                    Layout.alignment: Qt.AlignTop
                                    visible: card.active
                                    text: root.en ? "Active" : "Активен"
                                    accentColor: card.accent
                                    implicitHeight: 20
                                    dot: true
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                text: cell.mode.description
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Type.caption
                                font.family: Type.family
                                lineHeight: 1.4
                                lineHeightMode: Text.ProportionalHeight
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignTop
                            }
                        }
                    }
                }

                // ---------- Пустые состояния ----------
                // Пустая сетка выглядит ровно как несработавший экран,
                // поэтому оба случая объясняют себя словами.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.shownModes.length === 0
                    spacing: Theme.spaceMd

                    Item { Layout.fillHeight: true }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.modes.length === 0 ? "◇" : "⌕"
                        color: Theme.onSurfaceDim
                        font.pixelSize: Type.display
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.modes.length === 0
                              ? (root.en ? "No modes installed" : "Режимы не найдены")
                              : (root.en ? "Nothing matches your search" : "Ничего не найдено")
                        color: Theme.onSurface
                        font.pixelSize: Type.title
                        font.family: Type.family
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.maximumWidth: 460
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: Theme.onSurfaceVariant
                        font.pixelSize: Type.caption
                        font.family: Type.family
                        text: root.modes.length === 0
                            ? (root.en
                                ? "A mode is a folder with a mode.json manifest. Drop one into the modes folder and hit Refresh."
                                : "Режим — это папка с манифестом mode.json. Положи её в папку режимов и нажми «Обновить».")
                            : (root.en ? "Try a different word." : "Попробуй другое слово.")
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        visible: root.modes.length === 0
                        text: modesCtl.modesDir
                        color: Theme.onSurfaceDim
                        font.pixelSize: 11
                        font.family: Type.familyMono
                    }
                    Row {
                        Layout.alignment: Qt.AlignHCenter
                        visible: root.modes.length === 0
                        spacing: Theme.spaceSm

                        Rectangle {
                            width: openFolderText.implicitWidth + Theme.spaceXl
                            height: 32
                            radius: Theme.radiusSm
                            color: openFolderArea.containsMouse ? Theme.surface3 : Theme.surface2
                            border.width: 1
                            border.color: Theme.outline
                            Behavior on color { ColorAnimation { duration: root.durFast } }
                            Text {
                                id: openFolderText
                                anchors.centerIn: parent
                                text: root.en ? "Open modes folder" : "Открыть папку режимов"
                                color: Theme.onSurface
                                font.pixelSize: Type.caption
                                font.family: Type.family
                            }
                            MouseArea {
                                id: openFolderArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: modesCtl.openModesFolder()
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }

                // ---------- Подвал ----------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    Text {
                        text: root.en
                            ? root.shownModes.length + " of " + root.modes.length + " modes"
                            : root.shownModes.length + " из " + root.modes.length + " режимов"
                        color: Theme.onSurfaceDim
                        font.pixelSize: 11
                        font.family: Type.family
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: root.en ? "Refresh" : "Обновить"
                        color: refreshArea.containsMouse ? Theme.accent : Theme.onSurfaceDim
                        font.pixelSize: 11
                        font.family: Type.family
                        Behavior on color { ColorAnimation { duration: root.durFast } }
                        MouseArea {
                            id: refreshArea
                            anchors.fill: parent
                            anchors.margins: -Theme.spaceSm
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: modesCtl.refresh()
                        }
                    }
                }
            }

            // ---------- Правая колонка: детали ----------
            Rectangle {
                // Когда режимов нет вовсе, панель деталей — пустая рамка
                // с неактивной кнопкой; убираем её, чтобы весь экран занимало
                // объяснение, что делать дальше.
                visible: root.modes.length > 0
                Layout.preferredWidth: 340
                Layout.fillHeight: true
                radius: Theme.radiusMd
                color: Theme.surface1
                border.width: 1
                border.color: Theme.outline

                // Тонкая полоса акцента сверху — панель «принимает цвет»
                // выбранного режима, не перекрашиваясь целиком.
                Rectangle {
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    anchors.margins: 1
                    height: 3
                    radius: 2
                    color: root.accentOf(root.previewMode)
                    opacity: root.previewMode ? 0.9 : 0.0
                    Behavior on color   { ColorAnimation  { duration: root.dur } }
                    Behavior on opacity { NumberAnimation { duration: root.dur } }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spaceLg
                    anchors.topMargin: Theme.spaceLg + 4
                    spacing: Theme.spaceMd

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceMd

                        Rectangle {
                            implicitWidth: 44
                            implicitHeight: 44
                            radius: Theme.radiusMd
                            visible: root.previewMode !== null
                            color: JarvisUi.tint(root.accentOf(root.previewMode), 0.12)
                            Behavior on color { ColorAnimation { duration: root.dur } }
                            Text {
                                anchors.centerIn: parent
                                text: root.previewMode && root.previewMode.icon
                                      ? root.previewMode.icon : "◆"
                                color: root.accentOf(root.previewMode)
                                font.pixelSize: Type.title
                            }
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
                    Repeater {
                        model: 2
                        delegate: ColumnLayout {
                            id: skillGroup
                            // index — из контекста делегата; кладём в своё
                            // свойство, чтобы читать его из вложенных чипов.
                            readonly property bool isEnable: index === 0
                            readonly property var list: {
                                if (!root.previewMode) return []
                                return (isEnable ? root.previewMode.enableSkills
                                                 : root.previewMode.disableSkills) || []
                            }

                            visible: list.length > 0
                            Layout.fillWidth: true
                            spacing: Theme.spaceXs

                            Text {
                                text: skillGroup.isEnable
                                      ? (root.en ? "Enables"  : "Включает")
                                      : (root.en ? "Disables" : "Отключает")
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
                                        id: chip
                                        property string skillId: modelData

                                        radius: Theme.radiusSm
                                        color: Theme.surface2
                                        border.width: 1
                                        border.color: skillGroup.isEnable
                                                      ? JarvisUi.tint(Theme.success, 0.35)
                                                      : Theme.outline
                                        implicitWidth: chipText.implicitWidth + Theme.spaceMd
                                        implicitHeight: 22
                                        Text {
                                            id: chipText
                                            anchors.centerIn: parent
                                            text: chip.skillId
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
                        visible: root.previewMode !== null
                                 && root.previewMode.exclusive === true
                        text: root.en
                            ? "⚠ Exclusive — turns off every other skill."
                            : "⚠ Эксклюзивный — выключает все остальные скиллы."
                        color: Theme.warning
                        font.pixelSize: 11
                        font.family: Type.family
                        wrapMode: Text.WordWrap
                    }

                    Item { Layout.fillHeight: true }

                    // Единственная акцентная кнопка на экране
                    Rectangle {
                        id: applyBtn
                        readonly property bool isActive: root.previewMode
                                                         && root.previewMode.id === root.activeId
                        readonly property bool canApply: root.previewMode !== null && !isActive

                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        radius: Theme.radiusSm
                        color: !canApply ? Theme.surface2
                             : applyArea.pressed ? Theme.accentMuted
                             : applyArea.containsMouse ? Qt.lighter(Theme.accent, 1.08)
                             : Theme.accent
                        Behavior on color { ColorAnimation { duration: root.durFast } }

                        scale: applyArea.pressed && canApply ? 0.985 : 1.0
                        Behavior on scale { NumberAnimation { duration: root.durFast } }

                        Row {
                            anchors.centerIn: parent
                            spacing: Theme.spaceSm
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: applyBtn.isActive
                                text: "✓"
                                color: Theme.onSurfaceDim
                                font.pixelSize: Type.caption
                                font.weight: Font.DemiBold
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: !root.previewMode ? "—"
                                    : applyBtn.isActive ? (root.en ? "Currently active" : "Сейчас активен")
                                    : (root.en ? "Activate this mode" : "Включить этот режим")
                                color: applyBtn.canApply ? Theme.onAccent : Theme.onSurfaceDim
                                font.pixelSize: Type.caption
                                font.family: Type.family
                                font.weight: Font.DemiBold
                            }
                        }
                        MouseArea {
                            id: applyArea
                            anchors.fill: parent
                            enabled: applyBtn.canApply
                            hoverEnabled: true
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: modesCtl.activate(root.previewMode.id)
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.activeId.length > 0
                        horizontalAlignment: Text.AlignHCenter
                        text: root.en ? "Clear active mode" : "Снять активный режим"
                        color: clearArea.containsMouse ? Theme.onSurface : Theme.onSurfaceDim
                        font.pixelSize: Type.caption
                        font.family: Type.family
                        Behavior on color { ColorAnimation { duration: root.durFast } }
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
}
