import QtQuick
import QtQuick.Layouts
import Jarvis.Theme
import Jarvis.Controls

// ============================================================
// TrainingCenter.qml — Training & Dataset Center dashboard.
//
// C++ (TrainingCenterDialog) owns the data and side effects;
// this file owns the look: tabs, animated progress ring, bar
// charts, toggles, pills, and the live training log. Talks back
// to C++ only through the "trainingCenter" invokable object.
// ============================================================

Rectangle {
    id: root
    color: Theme.bg

    readonly property bool en: tcEnglish
    readonly property color cyan: Theme.accent
    readonly property color teal: Theme.accentMuted
    readonly property color violet: Theme.info


    readonly property int currentTab: tabs.currentIndex

    Column {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ---- Header ----
        Text {
            text: en ? "TRAINING CENTER" : "ЦЕНТР ОБУЧЕНИЯ"
            color: root.cyan
            font.pixelSize: 18
            font.bold: true
            font.family: "Segoe UI Semibold"
        }

        // ---- Tab bar ----
        JarvisTabBar {
            id: tabs
            titles: [
                en ? "Overview"       : "Обзор",
                en ? "App Usage"      : "Использование",
                en ? "Local Training" : "Локальное обучение",
                en ? "History"        : "История",
                en ? "Synapse Graph"  : "Граф синапсов"
            ]
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
            // Tab 0 — Overview
            // ======================================================
            Item {
                anchors.fill: parent
                visible: root.currentTab === 0
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.spaceLg

                    // Раньше блоки этой вкладки стояли на абсолютных
                    // координатах (y: 176, y: 340) — любая правка высоты
                    // карточки заставляла пересчитывать их вручную.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceXl

                        JarvisProgressRing {
                            value: datasetProgress
                            caption: en ? "of goal" : "от цели"
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: Theme.spaceMd

                            Text {
                                text: en ? "Training Dataset" : "Датасет обучения"
                                color: root.cyan
                                font.family: Type.family
                                font.pixelSize: Type.caption
                                font.weight: Font.DemiBold
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceXxl

                                JarvisStat {
                                    value: String(datasetTotal)
                                    label: en ? "pairs saved" : "пар сохранено"
                                    large: true
                                }
                                JarvisStat {
                                    value: String(datasetLiked)
                                    label: en ? "liked (👍)" : "лайкнуто (👍)"
                                    valueColor: root.cyan
                                    large: true
                                }
                                JarvisStat {
                                    value: String(datasetGoal)
                                    label: en ? "goal" : "цель"
                                    valueColor: Theme.onSurfaceVariant
                                    large: true
                                }
                                Item { Layout.fillWidth: true }
                            }

                            JarvisButton {
                                glyph: "📤"
                                text: en ? "Export .jsonl" : "Экспорт .jsonl"
                                onClicked: trainingCenter.exportDataset()
                            }
                        }
                    }

                    JarvisPanel {
                        Layout.fillWidth: true
                        title: en ? "Voice Journal" : "Голосовой журнал"
                        compact: true

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceMd

                            Item { Layout.fillWidth: true }

                            JarvisToggle {
                                // Локального состояния нет намеренно: тумблер
                                // всегда отражает recordingActive из C++ и не
                                // может разъехаться с реальным состоянием.
                                on: recordingActive
                                label: en ? "Recording" : "Запись"
                                onToggleRequested: want => trainingCenter.toggleRecording(want)
                            }
                        }

                        JarvisProgressBar {
                            Layout.fillWidth: true
                            value: journalTotal > 0 ? journalDone / journalTotal : 0
                            fillColor: root.teal
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceXxl

                            JarvisStat {
                                value: String(journalTotal)
                                label: en ? "recorded" : "записано"
                            }
                            JarvisStat {
                                value: String(journalDone)
                                label: en ? "processed" : "обработано"
                                valueColor: root.teal
                            }
                            JarvisStat {
                                value: String(journalPending)
                                label: en ? "pending" : "в ожидании"
                                valueColor: Theme.warning
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }

                    JarvisButton {
                        glyph: "🔄"
                        text: en ? "Refresh" : "Обновить"
                        variant: JarvisButton.Ghost
                        onClicked: trainingCenter.refreshStats()
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // ======================================================
            // Tab 1 — App Usage
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
                            text: (en ? "Total records: " : "Всего записей: ") + appUsageTotal
                            color: Theme.onSurfaceVariant
                            font.family: Type.family
                            font.pixelSize: Type.caption
                            elide: Text.ElideRight
                        }

                        JarvisToggle {
                            on: appLearningEnabled
                            label: en ? "Enable learning" : "Включить обучение"
                            onToggleRequested: want => trainingCenter.toggleAppLearning(want)
                        }

                        JarvisButton {
                            text: en ? "Clear data" : "Очистить"
                            variant: JarvisButton.Danger
                            onClicked: trainingCenter.clearAppUsageData()
                        }
                    }

                    Text { text: en ? "Predictions for now" : "Предсказания на сейчас"; color: Theme.success; font.pixelSize: 13; font.bold: true; font.family: "Segoe UI Semibold" }

                    Flickable {
                        width: parent.width
                        height: 130
                        clip: true
                        contentHeight: predCol.height
                        boundsBehavior: Flickable.StopAtBounds

                        Column {
                            id: predCol
                            width: parent.width
                            spacing: 6

                            Text {
                                visible: appPredictions.length === 0
                                text: en ? "Not enough data yet. Keep using your PC!"
                                         : "Ещё мало данных. Продолжайте использовать ПК!"
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }

                            Repeater {
                                model: appPredictions
                                delegate: RowLayout {
                                    width: predCol.width
                                    spacing: Theme.spaceMd

                                    Text {
                                        Layout.preferredWidth: 140
                                        text: modelData.name
                                        color: Theme.onSurface
                                        font.family: Type.family
                                        font.pixelSize: Type.caption
                                        elide: Text.ElideRight
                                    }
                                    JarvisProgressBar {
                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignVCenter
                                        implicitHeight: 12
                                        value: modelData.confidence
                                        fillColor: modelData.confidence >= 0.5 ? Theme.success : root.teal
                                    }
                                    Text {
                                        Layout.preferredWidth: 60
                                        text: Math.round(modelData.confidence * 100) + "% ×" + modelData.frequency
                                        color: Theme.onSurfaceVariant
                                        font.family: Type.family
                                        font.pixelSize: 10
                                    }
                                }
                            }
                        }
                    }

                    Text { text: en ? "Today's usage" : "Сегодня"; color: Theme.info; font.pixelSize: 13; font.bold: true; font.family: "Segoe UI Semibold" }

                    Flickable {
                        width: parent.width
                        height: parent.height - 260
                        clip: true
                        contentHeight: todayCol.height
                        boundsBehavior: Flickable.StopAtBounds

                        Column {
                            id: todayCol
                            width: parent.width
                            spacing: 6

                            Text {
                                visible: appToday.length === 0
                                text: en ? "No data for today" : "Нет данных за сегодня"
                                color: Theme.onSurfaceVariant
                                font.family: Type.family
                                font.pixelSize: Type.caption
                            }

                            Repeater {
                                model: appToday
                                delegate: RowLayout {
                                    width: todayCol.width
                                    spacing: Theme.spaceMd

                                    Text {
                                        Layout.preferredWidth: 140
                                        text: modelData.name
                                        color: Theme.onSurface
                                        font.family: Type.family
                                        font.pixelSize: Type.caption
                                        elide: Text.ElideRight
                                    }
                                    JarvisProgressBar {
                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignVCenter
                                        implicitHeight: 12
                                        value: modelData.fraction
                                        fillColor: Theme.info
                                    }
                                    Text {
                                        Layout.preferredWidth: 100
                                        text: modelData.minutes + " " + (en ? "min" : "мин")
                                              + " · " + modelData.sessions + (en ? " sess." : " сесс.")
                                        color: Theme.onSurfaceVariant
                                        font.family: Type.family
                                        font.pixelSize: 10
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ======================================================
            // Tab 2 — Local Training
            // ======================================================
            Item {
                anchors.fill: parent
                visible: root.currentTab === 2
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }

                Column {
                    anchors.fill: parent
                    spacing: 10

                    Text {
                        width: parent.width
                        visible: !ollamaAvailable
                        text: en ? "⚠️ Ollama not found — install from ollama.com, then reopen this dialog."
                                 : "⚠️ Ollama не найдена — установите с ollama.com и откройте это окно заново."
                        color: Theme.error
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        text: (en ? "👍 Liked responses available: " : "👍 Лайкнутых ответов: ") + likedCount
                        color: Theme.onSurfaceVariant
                        font.pixelSize: 12
                    }

                    Text { text: en ? "Base model" : "Базовая модель"; color: root.cyan; font.pixelSize: 13; font.bold: true; font.family: "Segoe UI Semibold" }

                    JarvisChipGroup {
                        width: parent.width
                        options: modelOptions
                        value: selectedModel
                        onPicked: v => trainingCenter.selectModel(v)
                    }

                    Text { text: en ? "Max examples" : "Макс. примеров"; color: root.cyan; font.pixelSize: 13; font.bold: true; font.family: "Segoe UI Semibold" }

                    JarvisChipGroup {
                        width: parent.width
                        options: [
                            { value: 30,  label: en ? "Quick (30)"      : "Быстро (30)" },
                            { value: 80,  label: en ? "Standard (80)"   : "Стандарт (80)" },
                            { value: 150, label: en ? "Thorough (150)"  : "Тщательно (150)" },
                            { value: 300, label: en ? "Maximum (300)"   : "Максимум (300)" }
                        ]
                        value: maxExamples
                        onPicked: v => trainingCenter.setMaxExamples(v)
                    }

                    // Пульсирующая точка стоит РЯДОМ с кнопкой, а не внутри
                    // неё. Раньше пульсация висела на opacity самой кнопки и
                    // дралась с opacity, которым показывали недоступность:
                    // выключенная кнопка всё равно мигала.
                    RowLayout {
                        width: parent.width
                        spacing: Theme.spaceSm

                        JarvisButton {
                            glyph: trainingActive ? "⏳" : "🚀"
                            text: trainingActive
                                  ? (en ? "Training…" : "Обучение…")
                                  : (en ? "Start Training" : "Начать обучение")
                            variant: JarvisButton.Primary
                            enabled: likedCount > 0 && ollamaAvailable && !trainingActive
                            onClicked: trainingCenter.startTraining()
                        }

                        JarvisStatusDot {
                            Layout.alignment: Qt.AlignVCenter
                            visible: trainingActive
                            pulsing: trainingActive
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Rectangle {
                        width: parent.width
                        height: parent.height - y
                        radius: 8
                        color: Theme.surface2
                        border.width: 1
                        border.color: Theme.accentSubtle
                        clip: true

                        Flickable {
                            id: logFlick
                            anchors.fill: parent
                            anchors.margins: 10
                            contentHeight: logText.implicitHeight
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds

                            onContentHeightChanged: contentY = Math.max(0, contentHeight - height)

                            Text {
                                id: logText
                                width: logFlick.width
                                text: trainingLog.length > 0 ? trainingLog
                                      : (en ? "Training log will appear here..." : "Здесь появится лог обучения...")
                                color: Theme.onSurfaceDim
                                font.family: "Consolas"
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            // ======================================================
            // Tab 3 — History Search
            // ======================================================
            Item {
                anchors.fill: parent
                visible: root.currentTab === 3
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }

                Column {
                    anchors.fill: parent
                    spacing: 10

                    JarvisSearchField {
                        id: searchInput
                        width: parent.width
                        placeholder: en ? "Search chat history…" : "Поиск по истории чатов…"
                        onAccepted: trainingCenter.searchHistory(text)
                    }

                    Text {
                        text: en ? "Found " + historyResults.length + " results"
                                 : "Найдено " + historyResults.length + " результатов"
                        visible: searchInput.text.length > 0
                        color: root.cyan
                        font.pixelSize: 11
                    }

                    Flickable {
                        width: parent.width
                        height: parent.height - y
                        clip: true
                        contentHeight: resultsCol.height
                        boundsBehavior: Flickable.StopAtBounds

                        Column {
                            id: resultsCol
                            width: parent.width
                            spacing: 8

                            Repeater {
                                model: historyResults
                                delegate: Rectangle {
                                    width: resultsCol.width
                                    height: resultCol.implicitHeight + 16
                                    radius: 8
                                    color: Theme.outline
                                    border.width: 1
                                    border.color: Theme.accentSubtle

                                    Column {
                                        id: resultCol
                                        x: 10; y: 8
                                        width: parent.width - 20
                                        spacing: 3
                                        Text {
                                            width: parent.width
                                            text: "▶ " + modelData.userMessage
                                            color: Theme.onSurface
                                            font.pixelSize: 11
                                            font.bold: true
                                            wrapMode: Text.WordWrap
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            width: parent.width
                                            text: modelData.aiResponse
                                            color: Theme.onSurfaceVariant
                                            font.pixelSize: 10
                                            wrapMode: Text.WordWrap
                                            maximumLineCount: 3
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }


            // ======================================================
            // Tab 4 — Synapse Graph (SynapseGraph associative memory)
            //
            // The network itself, not a ranked list of it: concepts as
            // nodes, Hebbian synapses as lines, both positioned by the
            // force-directed layout computed in C++. Node size and colour
            // are activation count; line thickness is synapse weight — so
            // clusters on screen are literally the associations Jarvis has
            // learned to fire together.
            // ======================================================
            Item {
                id: synapseTab
                anchors.fill: parent
                visible: root.currentTab === 4
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }

                // One colour per learning channel — the grouping comes from
                // synapse_nodes.source, so the picture says where each piece
                // of knowledge came from rather than only how hot it is.
                readonly property color srcDialogue: Theme.accent
                readonly property color srcWatched:  Theme.warning
                readonly property color srcFact:     Theme.info

                function sourceColor(source) {
                    if (source === "watched") return srcWatched
                    if (source === "fact")    return srcFact
                    return srcDialogue
                }

                // ── Focus: "what is this concept wired to" ───────────────
                // Hover is transient, a click sticks (Esc / clicking empty
                // space clears it) — reading a graph is mostly this question,
                // and answering it by eye fails as soon as lines cross.
                property int hoveredNode:  -1
                property int selectedNode: -1

                // Досье на выбранный узел — приходит из C++ по клику.
                // Картинка показывает, ЧТО с чем связано; на вопрос
                // "откуда я вообще это знаю" отвечают только реальные
                // реплики, а они лежат в БД, не в модели графа.
                property var nodeInfo: ({})

                function selectNode(index, nodeId) {
                    if (selectedNode === index) {
                        selectedNode = -1
                        nodeInfo = ({})
                        return
                    }
                    selectedNode = index
                    nodeInfo = trainingCenter.synapseNodeDetail(nodeId)
                }

                function clearSelection() {
                    selectedNode = -1
                    nodeInfo = ({})
                }
                readonly property int focusNode: selectedNode >= 0 ? selectedNode : hoveredNode

                // index -> {neighbourIndex: true}, built once per data change
                // rather than scanned per node per frame.
                readonly property var adjacency: {
                    const adj = {}
                    for (let i = 0; i < synapseGraphEdges.length; ++i) {
                        const e = synapseGraphEdges[i]
                        if (adj[e.a] === undefined) adj[e.a] = {}
                        if (adj[e.b] === undefined) adj[e.b] = {}
                        adj[e.a][e.b] = true
                        adj[e.b][e.a] = true
                    }
                    return adj
                }

                function isLit(index) {
                    if (focusNode < 0) return true
                    if (focusNode === index) return true
                    const n = adjacency[focusNode]
                    return n !== undefined && n[index] === true
                }

                function edgeLit(a, b) {
                    if (focusNode < 0) return true
                    return focusNode === a || focusNode === b
                }

                Column {
                    anchors.fill: parent
                    spacing: 10

                    // ---- Stat header ----
                    Row {
                        width: parent.width
                        height: 26
                        spacing: 6

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.en ? "Concept nodes: " : "Узлов-понятий: "
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 12
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: synapseNodeCount
                            color: root.cyan
                            font.pixelSize: 13
                            font.bold: true
                        }
                        Item { width: 20; height: 1 }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.en ? "Synapses: " : "Связей: "
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 12
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: synapseEdgeCount
                            color: root.teal
                            font.pixelSize: 13
                            font.bold: true
                        }
                        Item { width: 20; height: 1 }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.en ? "· drawing top " + synapseGraphNodes.length
                                          : "· на схеме топ-" + synapseGraphNodes.length
                            color: Theme.onSurfaceDim
                            font.pixelSize: 10
                        }
                    }

                    // ---- Graph + side panel ----
                    Row {
                        width: parent.width
                        height: parent.height - y
                        spacing: 12

                        // ============ The network ============
                        Rectangle {
                            id: graphViewport
                            width: parent.width - 232
                            height: parent.height
                            radius: Theme.radiusMd
                            color: Theme.surface1
                            border.width: 1
                            border.color: Theme.outline
                            clip: true

                            Text {
                                anchors.centerIn: parent
                                width: parent.width - 60
                                visible: synapseGraphNodes.length === 0
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: root.en
                                    ? "No associations yet.\nThe network draws itself as Jarvis answers things — concepts that fire together grow a synapse between them."
                                    : "Связей пока нет.\nСхема рисует себя сама, пока Джарвис отвечает: понятия, срабатывающие вместе, отращивают между собой синапс."
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                                lineHeight: 1.4
                            }

                            // Pan/zoom surface. The content item is what moves;
                            // the viewport clips it.
                            Item {
                                id: graphContent
                                width: graphViewport.width
                                height: graphViewport.height
                                transformOrigin: Item.Center

                                // ---- Synapses ----
                                // Straight lines are rotated Rectangles rather
                                // than Shape/Canvas geometry: one batched
                                // scene-graph node each, nothing repainted on
                                // the UI thread.
                                Repeater {
                                    model: synapseGraphEdges
                                    delegate: Rectangle {
                                        id: synapseLine
                                        required property var modelData

                                        readonly property real px1: modelData.x1 * graphContent.width
                                        readonly property real py1: modelData.y1 * graphContent.height
                                        readonly property real px2: modelData.x2 * graphContent.width
                                        readonly property real py2: modelData.y2 * graphContent.height
                                        readonly property real len: Math.sqrt((px2 - px1) * (px2 - px1)
                                                                            + (py2 - py1) * (py2 - py1))

                                        x: px1
                                        y: py1 - height / 2
                                        width: len
                                        height: 1 + 2.5 * modelData.norm
                                        transformOrigin: Item.Left
                                        rotation: Math.atan2(py2 - py1, px2 - px1) * 180 / Math.PI

                                        readonly property bool lit:
                                            synapseTab.edgeLit(modelData.a, modelData.b)

                                        // Alpha on the leaf colour, not opacity
                                        // on a subtree — no offscreen pass.
                                        // Unrelated synapses drop to a trace
                                        // rather than vanishing: the shape of
                                        // the whole network stays readable
                                        // behind the highlighted part.
                                        color: Qt.rgba(
                                            lit ? Theme.accent.r : Theme.accentMuted.r,
                                            lit ? Theme.accent.g : Theme.accentMuted.g,
                                            lit ? Theme.accent.b : Theme.accentMuted.b,
                                            synapseTab.focusNode < 0
                                                ? 0.18 + 0.55 * modelData.norm
                                                : (lit ? 0.85 : 0.05))
                                        Behavior on color {
                                            ColorAnimation { duration: Theme.motionFast }
                                        }

                                        // Signal travelling down the strongest
                                        // synapses. Only the top band animates,
                                        // and only while the tab is on screen.
                                        Rectangle {
                                            visible: synapseLine.modelData.norm > 0.6
                                            width: 3
                                            height: parent.height
                                            radius: height / 2
                                            color: Theme.accent
                                            SequentialAnimation on x {
                                                running: synapseTab.visible
                                                         && synapseLine.modelData.norm > 0.6
                                                loops: Animation.Infinite
                                                NumberAnimation {
                                                    from: 0
                                                    to: Math.max(0, synapseLine.len - 3)
                                                    duration: 1400 + 900 * (1 - synapseLine.modelData.norm)
                                                    easing.type: Easing.InOutSine
                                                }
                                                PauseAnimation { duration: 700 }
                                            }
                                        }
                                    }
                                }

                                // ---- Concepts ----
                                Repeater {
                                    model: synapseGraphNodes
                                    delegate: Item {
                                        id: nodeItem
                                        required property var modelData
                                        required property int index

                                        readonly property real diameter: 11 + 21 * modelData.heat
                                        readonly property color tint: synapseTab.sourceColor(modelData.source)
                                        readonly property bool lit: synapseTab.isLit(nodeItem.index)
                                        readonly property bool dimmed:
                                            synapseTab.focusNode >= 0 && !lit

                                        // Grows out of the neighbour that anchors it,
                                        // then settles into its own position — the
                                        // network builds outward from what it already
                                        // knows instead of fading in finished.
                                        readonly property real homeX:
                                            modelData.x * graphContent.width - width / 2
                                        readonly property real homeY:
                                            modelData.y * graphContent.height - diameter / 2

                                        x: modelData.fromX * graphContent.width - width / 2
                                        y: modelData.fromY * graphContent.height - diameter / 2
                                        width: Math.max(diameter, label.implicitWidth)
                                        height: diameter + 14

                                        opacity: 0
                                        scale: 0.4

                                        // Staggered by index so growth ripples outward
                                        // from the most-established concepts.
                                        Component.onCompleted: settle.start()
                                        ParallelAnimation {
                                            id: settle
                                            NumberAnimation {
                                                target: nodeItem; property: "x"
                                                to: nodeItem.homeX
                                                duration: 520 + (nodeItem.index % 8) * 60
                                                easing.type: Easing.OutCubic
                                            }
                                            NumberAnimation {
                                                target: nodeItem; property: "y"
                                                to: nodeItem.homeY
                                                duration: 520 + (nodeItem.index % 8) * 60
                                                easing.type: Easing.OutCubic
                                            }
                                            OpacityAnimator {
                                                target: nodeItem
                                                from: 0; to: 1
                                                duration: 300
                                                easing.type: Easing.OutCubic
                                            }
                                            ScaleAnimator {
                                                target: nodeItem
                                                from: 0.4; to: 1
                                                duration: 340 + (nodeItem.index % 8) * 45
                                                easing.type: Easing.OutBack
                                            }
                                        }

                                        // Re-anchor if the window resizes after the
                                        // entrance animation has already finished.
                                        Binding {
                                            target: nodeItem; property: "x"
                                            value: nodeItem.homeX
                                            when: !settle.running && nodeItem.opacity > 0
                                        }
                                        Binding {
                                            target: nodeItem; property: "y"
                                            value: nodeItem.homeY
                                            when: !settle.running && nodeItem.opacity > 0
                                        }

                                        Rectangle {
                                            id: halo
                                            anchors.centerIn: dot
                                            width: nodeItem.diameter + 12
                                            height: width
                                            radius: width / 2
                                            color: "transparent"
                                            border.width: 1
                                            border.color: Qt.rgba(nodeItem.tint.r, nodeItem.tint.g,
                                                                  nodeItem.tint.b,
                                                                  nodeItem.dimmed ? 0.06 : 0.35)
                                            visible: (modelData.major || hover.hovered
                                                      || synapseTab.focusNode === nodeItem.index)
                                                     && !nodeItem.dimmed
                                            // Breathing ring on the hottest
                                            // concepts — the "this one is alive"
                                            // cue, paused when off-screen.
                                            SequentialAnimation on scale {
                                                running: synapseTab.visible && halo.visible
                                                loops: Animation.Infinite
                                                NumberAnimation { from: 1.0; to: 1.22; duration: 1300
                                                                  easing.type: Easing.InOutSine }
                                                NumberAnimation { from: 1.22; to: 1.0; duration: 1300
                                                                  easing.type: Easing.InOutSine }
                                            }
                                        }

                                        Rectangle {
                                            id: dot
                                            width: nodeItem.diameter
                                            height: nodeItem.diameter
                                            radius: width / 2
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            y: 0
                                            // Dimming is alpha on this leaf, not opacity
                                            // on the node subtree — same look, no
                                            // offscreen composite per node.
                                            color: Qt.rgba(nodeItem.tint.r, nodeItem.tint.g,
                                                           nodeItem.tint.b,
                                                           nodeItem.dimmed ? 0.12
                                                                           : (hover.hovered ? 1.0 : 0.75))
                                            border.width: 1
                                            border.color: Qt.rgba(nodeItem.tint.r, nodeItem.tint.g,
                                                                  nodeItem.tint.b,
                                                                  nodeItem.dimmed ? 0.15 : 0.9)
                                            Behavior on color {
                                                ColorAnimation { duration: Theme.motionFast }
                                            }

                                            HoverHandler {
                                                id: hover
                                                onHoveredChanged: synapseTab.hoveredNode =
                                                    hovered ? nodeItem.index
                                                            : (synapseTab.hoveredNode === nodeItem.index
                                                               ? -1 : synapseTab.hoveredNode)
                                            }
                                            TapHandler {
                                                onTapped: synapseTab.selectNode(
                                                    nodeItem.index, modelData.id)
                                            }
                                            // Своя подсказка вместо Controls.ToolTip: из-за
                                            // неё файл тянул целый модуль QtQuick.Controls
                                            // ради одного всплывающего прямоугольника, тогда
                                            // как все остальные экраны проекта обходятся
                                            // QtQuick + Jarvis.Theme. Чем меньше модулей
                                            // нужно развернуть, тем меньше способов получить
                                            // пустое окно на машине пользователя.
                                            Rectangle {
                                                visible: hover.hovered
                                                z: 100
                                                x: parent.width + 6
                                                y: -height / 2 + parent.height / 2
                                                width: tipText.implicitWidth + 14
                                                height: tipText.implicitHeight + 10
                                                radius: Theme.radiusSm
                                                color: Theme.surface3
                                                border.width: 1
                                                border.color: Theme.outlineStrong
                                                Text {
                                                    id: tipText
                                                    anchors.centerIn: parent
                                                    color: Theme.onSurface
                                                    font.pixelSize: 10
                                                    text: modelData.label
                                                        + (root.en ? "\nfired ×" : "\nсработало ×") + modelData.activations
                                                        + (root.en ? "\nsynapses: " : "\nсвязей: ") + modelData.degree
                                                        + (root.en ? "\nlearned from: " : "\nисточник: ") + modelData.source
                                                }
                                            }
                                        }

                                        Text {
                                            id: label
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.top: dot.bottom
                                            anchors.topMargin: 2
                                            text: modelData.label
                                            // Level of detail by APPARENT size, the way
                                            // brain-map gates labels on zoom × radius:
                                            // forty permanent labels is a wall of text at
                                            // rest, but zooming in should reveal the small
                                            // concepts rather than keep hiding them. Focus
                                            // and hover always win.
                                            visible: !nodeItem.dimmed
                                                     && (hover.hovered
                                                         || synapseTab.focusNode === nodeItem.index
                                                         || modelData.major
                                                         || graphContent.scale * nodeItem.diameter > 26)
                                            color: hover.hovered ? nodeItem.tint : Theme.onSurfaceVariant
                                            font.pixelSize: hover.hovered ? 10 : 9
                                            font.bold: modelData.major
                                        }
                                    }
                                }
                            }

                            // Drag to pan, wheel to zoom. Below the nodes in
                            // declaration order would swallow hover, so it sits
                            // behind them via z.
                            MouseArea {
                                anchors.fill: parent
                                z: -1
                                drag.target: graphContent
                                drag.axis: Drag.XAndYAxis
                                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                onWheel: (wheel) => {
                                    const next = graphContent.scale
                                               * (wheel.angleDelta.y > 0 ? 1.12 : 1 / 1.12)
                                    graphContent.scale = Math.max(0.5, Math.min(3.0, next))
                                }
                                // Clicking empty space clears a stuck focus, the
                                // counterpart to Esc in brain-map.
                                onClicked: synapseTab.clearSelection()
                            }

                            // ---- Fit ----
                            JarvisButton {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: Theme.spaceSm
                                glyph: "⤢"
                                text: root.en ? "Fit" : "Вписать"
                                variant: JarvisButton.Ghost

                                // Actually fits, rather than resetting zoom to
                                // 1.0 and calling it fitting: the layout rarely
                                // fills its unit square evenly, so scale comes
                                // from the real bounding box of the nodes and the
                                // content is then centred on that box's middle.
                                onClicked: {
                                    if (synapseGraphNodes.length === 0) {
                                        graphContent.scale = 1.0
                                        graphContent.x = 0
                                        graphContent.y = 0
                                        return
                                    }
                                    let minX = 1, maxX = 0, minY = 1, maxY = 0
                                    for (let i = 0; i < synapseGraphNodes.length; ++i) {
                                        const n = synapseGraphNodes[i]
                                        minX = Math.min(minX, n.x); maxX = Math.max(maxX, n.x)
                                        minY = Math.min(minY, n.y); maxY = Math.max(maxY, n.y)
                                    }
                                    // Margin in normalized units leaves room for
                                    // labels, which sit below their node.
                                    const pad = 0.06
                                    const spanX = Math.max(0.05, (maxX - minX) + pad * 2)
                                    const spanY = Math.max(0.05, (maxY - minY) + pad * 2)
                                    const k = Math.max(0.5, Math.min(3.0,
                                                   Math.min(1 / spanX, 1 / spanY)))
                                    graphContent.scale = k
                                    // Content scales about its centre, so shifting
                                    // the bbox centre onto that point is a plain
                                    // scaled offset from 0.5, 0.5.
                                    const cx = (minX + maxX) / 2
                                    const cy = (minY + maxY) / 2
                                    graphContent.x = (0.5 - cx) * graphContent.width * k
                                    graphContent.y = (0.5 - cy) * graphContent.height * k
                                }
                            }

                            // ---- Досье на узел: почему это здесь ----
                            // Картинка отвечает "что с чем связано". На вопрос
                            // "откуда я это знаю" отвечают только настоящие
                            // реплики, поэтому карточка приходит из БД по клику,
                            // а не собирается из модели графа.
                            Rectangle {
                                id: nodeCard
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.margins: 8
                                width: 300
                                radius: Theme.radiusMd
                                color: Qt.rgba(Theme.surface2.r, Theme.surface2.g,
                                               Theme.surface2.b, 0.96)
                                border.width: 1
                                border.color: Theme.outlineStrong
                                visible: opacity > 0
                                opacity: (synapseTab.selectedNode >= 0
                                          && synapseTab.nodeInfo.found === true) ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }

                                readonly property var info: synapseTab.nodeInfo

                                Flickable {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    contentHeight: cardCol.implicitHeight
                                    clip: true
                                    boundsBehavior: Flickable.StopAtBounds

                                    Column {
                                        id: cardCol
                                        width: parent.width
                                        spacing: 10

                                        Row {
                                            width: parent.width
                                            spacing: 6
                                            Text {
                                                width: parent.width - 20
                                                text: nodeCard.info.label || ""
                                                color: Theme.onSurface
                                                font.pixelSize: 15
                                                font.bold: true
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                text: "✕"
                                                color: Theme.onSurfaceVariant
                                                font.pixelSize: 13
                                                TapHandler { onTapped: synapseTab.clearSelection() }
                                            }
                                        }

                                        // Одна фраза-объяснение, собранная в C++:
                                        // склонения и числа — не работа разметки.
                                        Text {
                                            width: parent.width
                                            text: nodeCard.info.why || ""
                                            color: Theme.onSurfaceVariant
                                            font.pixelSize: 11
                                            wrapMode: Text.WordWrap
                                            lineHeight: 1.35
                                        }

                                        Rectangle {
                                            width: parent.width; height: 1
                                            color: Theme.outline
                                        }

                                        Text {
                                            text: root.en ? "WIRED TO" : "СВЯЗАНО С"
                                            color: Theme.onSurfaceVariant
                                            font.pixelSize: 9
                                            font.letterSpacing: 1.5
                                            visible: (nodeCard.info.neighbours || []).length > 0
                                        }

                                        Repeater {
                                            model: nodeCard.info.neighbours || []
                                            delegate: Item {
                                                required property var modelData
                                                width: cardCol.width
                                                height: 22

                                                Text {
                                                    anchors.left: parent.left
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    width: parent.width - 92
                                                    text: modelData.label
                                                    color: Theme.onSurface
                                                    font.pixelSize: 11
                                                    elide: Text.ElideRight
                                                }
                                                // Толщина связи — та же величина, что
                                                // рисует линию на схеме, только числом.
                                                Rectangle {
                                                    anchors.right: strength.left
                                                    anchors.rightMargin: 6
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    width: 46
                                                    height: 3
                                                    radius: 1.5
                                                    color: Theme.outline
                                                    Rectangle {
                                                        width: parent.width
                                                               * Math.max(0.05, Math.min(1, modelData.weight / 3))
                                                        height: parent.height
                                                        radius: parent.radius
                                                        color: Theme.accent
                                                    }
                                                }
                                                Text {
                                                    id: strength
                                                    anchors.right: parent.right
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: "×" + modelData.coActivations
                                                    color: Theme.onSurfaceVariant
                                                    font.pixelSize: 10
                                                }
                                            }
                                        }

                                        Rectangle {
                                            width: parent.width; height: 1
                                            color: Theme.outline
                                            visible: (nodeCard.info.origins || []).length > 0
                                        }

                                        Text {
                                            text: root.en ? "LEARNED FROM" : "ЗАПОМНИЛ ИЗ"
                                            color: Theme.onSurfaceVariant
                                            font.pixelSize: 9
                                            font.letterSpacing: 1.5
                                            visible: (nodeCard.info.origins || []).length > 0
                                        }

                                        Repeater {
                                            model: nodeCard.info.origins || []
                                            delegate: Column {
                                                required property var modelData
                                                width: cardCol.width
                                                spacing: 2
                                                bottomPadding: 6

                                                Text {
                                                    width: parent.width
                                                    text: "“" + modelData.query + "”"
                                                    color: Theme.onSurface
                                                    font.pixelSize: 11
                                                    font.italic: true
                                                    wrapMode: Text.WordWrap
                                                }
                                                Text {
                                                    width: parent.width
                                                    text: modelData.response
                                                    color: Theme.onSurfaceVariant
                                                    font.pixelSize: 10
                                                    wrapMode: Text.WordWrap
                                                    maximumLineCount: 3
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }

                                        // Понятие знакомо, а истории за ним нет —
                                        // это нормальное состояние, и молчать о нём
                                        // хуже, чем сказать прямо.
                                        Text {
                                            width: parent.width
                                            visible: (nodeCard.info.origins || []).length === 0
                                            text: root.en
                                                ? "No stored conversation is attached to this concept yet."
                                                : "К этому понятию пока не привязан ни один сохранённый разговор."
                                            color: Theme.onSurfaceVariant
                                            font.pixelSize: 10
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }
                            }

                            // ---- Legend: how Jarvis came to know each thing ----
                            Rectangle {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 8
                                width: legendCol.implicitWidth + 18
                                height: legendCol.implicitHeight + 16
                                radius: Theme.radiusSm
                                color: Qt.rgba(Theme.surface2.r, Theme.surface2.g,
                                               Theme.surface2.b, 0.85)
                                border.width: 1
                                border.color: Theme.outline
                                visible: synapseGraphNodes.length > 0

                                Column {
                                    id: legendCol
                                    anchors.centerIn: parent
                                    spacing: 5

                                    Text {
                                        text: root.en ? "LEARNED FROM" : "ОТКУДА ЗНАЮ"
                                        color: Theme.onSurfaceDim
                                        font.pixelSize: 8
                                        font.bold: true
                                    }

                                    Repeater {
                                        model: synapseLegend
                                        delegate: Row {
                                            required property var modelData
                                            spacing: 6
                                            Rectangle {
                                                width: 7; height: 7; radius: 3.5
                                                anchors.verticalCenter: parent.verticalCenter
                                                color: synapseTab.sourceColor(modelData.source)
                                            }
                                            Text {
                                                text: modelData.label
                                                color: Theme.onSurfaceVariant
                                                font.pixelSize: 9
                                            }
                                            Text {
                                                text: modelData.count
                                                color: Theme.onSurfaceDim
                                                font.pixelSize: 9
                                                font.bold: true
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // ============ Strongest associations ============
                        Column {
                            width: 220
                            height: parent.height
                            spacing: 8

                            RowLayout {
                                width: parent.width
                                spacing: Theme.spaceSm

                                Text {
                                    Layout.fillWidth: true
                                    text: root.en ? "Strongest links" : "Крепчайшие связи"
                                    color: root.violet
                                    font.family: Type.family
                                    font.pixelSize: Type.caption
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                JarvisButton {
                                    glyph: "🔄"
                                    accessibleName: root.en ? "Refresh" : "Обновить"
                                    variant: JarvisButton.Ghost
                                    onClicked: trainingCenter.refreshStats()
                                }
                            }

                            Flickable {
                                width: parent.width
                                height: parent.height - 34
                                clip: true
                                contentHeight: edgeCol.height
                                boundsBehavior: Flickable.StopAtBounds

                                Column {
                                    id: edgeCol
                                    width: parent.width
                                    spacing: 7

                                    Text {
                                        visible: synapseTopEdges.length === 0
                                        text: root.en ? "Nothing linked yet." : "Пока ничего не связано."
                                        color: Theme.onSurfaceVariant
                                        font.pixelSize: 10
                                    }

                                    Repeater {
                                        model: synapseTopEdges
                                        delegate: Column {
                                            required property var modelData
                                            width: edgeCol.width
                                            spacing: 3

                                            Text {
                                                width: parent.width
                                                text: modelData.labelA + " ↔ " + modelData.labelB
                                                color: Theme.onSurface
                                                font.family: Type.family
                                                font.pixelSize: 10
                                                elide: Text.ElideRight
                                            }
                                            RowLayout {
                                                width: parent.width
                                                spacing: Theme.spaceXs + 2

                                                JarvisProgressBar {
                                                    Layout.fillWidth: true
                                                    Layout.alignment: Qt.AlignVCenter
                                                    implicitHeight: 4
                                                    // Вес 3.0 — практический потолок:
                                                    // выше связи почти не встречаются,
                                                    // и полоса просто упирается в край.
                                                    value: modelData.weight / 3.0
                                                    fillColor: root.violet
                                                }
                                                Text {
                                                    Layout.preferredWidth: 56
                                                    text: modelData.weight.toFixed(2)
                                                        + " ×" + modelData.coActivations
                                                    color: Theme.onSurfaceDim
                                                    font.family: Type.family
                                                    font.pixelSize: 9
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
    }
}
