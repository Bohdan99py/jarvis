import QtQuick
import QtQuick.Controls
import Jarvis.Theme

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


    property int currentTab: initialTab

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
        Row {
            width: parent.width
            height: 34
            spacing: 6

            Repeater {
                model: [
                    en ? "Overview" : "Обзор",
                    en ? "App Usage" : "Использование",
                    en ? "Local Training" : "Локальное обучение",
                    en ? "History" : "История",
                    en ? "Synapse Graph" : "Граф синапсов"
                ]
                delegate: Rectangle {
                    width: tabText.implicitWidth + 24
                    height: 34
                    radius: 8
                    color: root.currentTab === index ? Theme.accentSubtle : "transparent"
                    border.width: 1
                    border.color: root.currentTab === index ? root.cyan : Theme.accentSubtle
                    Text {
                        id: tabText
                        anchors.centerIn: parent
                        text: modelData
                        color: root.currentTab === index ? root.cyan : Theme.onSurfaceVariant
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
            // Tab 0 — Overview
            // ======================================================
            Item {
                anchors.fill: parent
                visible: root.currentTab === 0
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }

                Row {
                    width: parent.width
                    height: 160
                    spacing: 24

                    // Circular progress ring
                    Canvas {
                        id: ring
                        width: 140; height: 140
                        property real progress: datasetProgress
                        Behavior on progress { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } }
                        onProgressChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            var cx = width / 2, cy = height / 2, r = width / 2 - 10
                            ctx.lineWidth = 10
                            ctx.lineCap = "round"
                            ctx.strokeStyle = "Theme.accentSubtle"
                            ctx.beginPath()
                            ctx.arc(cx, cy, r, 0, Math.PI * 2)
                            ctx.stroke()
                            ctx.strokeStyle = Theme.accent
                            ctx.beginPath()
                            ctx.arc(cx, cy, r, -Math.PI / 2, -Math.PI / 2 + progress * Math.PI * 2)
                            ctx.stroke()
                        }
                        Column {
                            anchors.centerIn: parent
                            spacing: 0
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: Math.round(ring.progress * 100) + "%"
                                color: root.cyan
                                font.pixelSize: 24
                                font.bold: true
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: en ? "of goal" : "от цели"
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 10
                            }
                        }
                    }

                    Column {
                        spacing: 10
                        width: parent.width - 140 - 24
                        anchors.verticalCenter: parent.verticalCenter

                        Text { text: en ? "Training Dataset" : "Датасет обучения"; color: root.cyan; font.pixelSize: 13; font.bold: true; font.family: "Segoe UI Semibold" }

                        Row {
                            spacing: 28
                            Column {
                                Text { text: datasetTotal; color: Theme.onSurface; font.pixelSize: 20; font.bold: true }
                                Text { text: en ? "pairs saved" : "пар сохранено"; color: Theme.onSurfaceVariant; font.pixelSize: 10 }
                            }
                            Column {
                                Text { text: datasetLiked; color: root.cyan; font.pixelSize: 20; font.bold: true }
                                Text { text: en ? "liked (👍)" : "лайкнуто (👍)"; color: Theme.onSurfaceVariant; font.pixelSize: 10 }
                            }
                            Column {
                                Text { text: datasetGoal; color: Theme.onSurfaceVariant; font.pixelSize: 20; font.bold: true }
                                Text { text: en ? "goal" : "цель"; color: Theme.onSurfaceVariant; font.pixelSize: 10 }
                            }
                        }

                        Rectangle {
                            width: 160; height: 32; radius: 8
                            color: exportArea.pressed ? Theme.outlineStrong : Theme.accentSubtle
                            border.width: 1
                            border.color: root.cyan
                            Text {
                                anchors.centerIn: parent
                                text: en ? "📤 Export .jsonl" : "📤 Экспорт .jsonl"
                                color: root.cyan
                                font.pixelSize: 11
                            }
                            MouseArea {
                                id: exportArea
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: trainingCenter.exportDataset()
                            }
                        }
                    }
                }

                Rectangle {
                    id: journalCard
                    y: 176
                    width: parent.width
                    height: 150
                    radius: 10
                    color: Theme.surface1
                    border.width: 1
                    border.color: Theme.accentSubtle

                    Column {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8

                        Row {
                            width: parent.width
                            Text { text: en ? "Voice Journal" : "Голосовой журнал"; color: root.cyan; font.pixelSize: 13; font.bold: true; font.family: "Segoe UI Semibold" }
                            Item { width: parent.width - 260; height: 1 }
                            Row {
                                spacing: 6
                                Text {
                                    text: en ? "Recording:" : "Запись:"
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Rectangle {
                                    // No local "checked" state on purpose — always reflects the
                                    // authoritative recordingActive context property directly, so
                                    // it can never drift out of sync with the real backend state.
                                    width: 44; height: 22; radius: 11
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: recordingActive ? root.teal : Theme.outlineStrong
                                    Behavior on color { ColorAnimation { duration: 150 } }
                                    Rectangle {
                                        width: 18; height: 18; radius: 9
                                        color: Theme.onSurface
                                        y: 2
                                        x: recordingActive ? parent.width - width - 2 : 2
                                        Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: trainingCenter.toggleRecording(!recordingActive)
                                    }
                                }
                            }
                        }

                        // Stacked bar: processed vs pending
                        Rectangle {
                            width: parent.width
                            height: 10
                            radius: 5
                            color: Theme.outline
                            Row {
                                anchors.fill: parent
                                Rectangle {
                                    width: journalTotal > 0
                                           ? parent.width * (journalDone / journalTotal) : 0
                                    height: parent.height
                                    radius: 5
                                    color: root.teal
                                    Behavior on width { NumberAnimation { duration: 450; easing.type: Easing.OutCubic } }
                                }
                            }
                        }

                        Row {
                            spacing: 28
                            Column {
                                Text { text: journalTotal; color: Theme.onSurface; font.pixelSize: 16; font.bold: true }
                                Text { text: en ? "recorded" : "записано"; color: Theme.onSurfaceVariant; font.pixelSize: 10 }
                            }
                            Column {
                                Text { text: journalDone; color: root.teal; font.pixelSize: 16; font.bold: true }
                                Text { text: en ? "processed" : "обработано"; color: Theme.onSurfaceVariant; font.pixelSize: 10 }
                            }
                            Column {
                                Text { text: journalPending; color: Theme.warning; font.pixelSize: 16; font.bold: true }
                                Text { text: en ? "pending" : "в ожидании"; color: Theme.onSurfaceVariant; font.pixelSize: 10 }
                            }
                        }
                    }
                }

                Rectangle {
                    y: 340
                    width: 130; height: 30; radius: 6
                    color: refreshArea.pressed ? Theme.outlineStrong : Theme.accentSubtle
                    border.width: 1
                    border.color: Theme.outlineStrong
                    Text {
                        anchors.centerIn: parent
                        text: (en ? "🔄 Refresh" : "🔄 Обновить")
                        color: root.cyan
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: refreshArea
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: trainingCenter.refreshStats()
                    }
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

                    Row {
                        width: parent.width
                        Text {
                            text: (en ? "Total records: " : "Всего записей: ") + appUsageTotal
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 12
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Item { width: parent.width - 420; height: 1 }
                        Row {
                            spacing: 6
                            Text {
                                text: en ? "Enable learning:" : "Включить обучение:"
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Rectangle {
                                width: 44; height: 22; radius: 11
                                anchors.verticalCenter: parent.verticalCenter
                                color: appLearningEnabled ? root.teal : Theme.outlineStrong
                                Behavior on color { ColorAnimation { duration: 150 } }
                                Rectangle {
                                    width: 18; height: 18; radius: 9
                                    color: Theme.onSurface
                                    y: 2
                                    x: appLearningEnabled ? parent.width - width - 2 : 2
                                    Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: trainingCenter.toggleAppLearning(!appLearningEnabled)
                                }
                            }
                            Rectangle {
                                width: 90; height: 26; radius: 6
                                color: clearArea.pressed ? Qt.alpha(Theme.error, 0.3) : Qt.alpha(Theme.error, 0.12)
                                border.width: 1
                                border.color: Qt.alpha(Theme.error, 0.4)
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    anchors.centerIn: parent
                                    text: en ? "Clear data" : "Очистить"
                                    color: Theme.error
                                    font.pixelSize: 10
                                }
                                MouseArea {
                                    id: clearArea
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: trainingCenter.clearAppUsageData()
                                }
                            }
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
                                delegate: Row {
                                    width: predCol.width
                                    spacing: 10
                                    Text {
                                        width: 140
                                        text: modelData.name
                                        color: Theme.onSurface
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                    Rectangle {
                                        width: parent.width - 140 - 60
                                        height: 14
                                        radius: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: Theme.outline
                                        Rectangle {
                                            width: parent.width * modelData.confidence
                                            height: parent.height
                                            radius: 4
                                            color: modelData.confidence >= 0.5 ? Theme.success : root.teal
                                            Behavior on width { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
                                        }
                                    }
                                    Text {
                                        width: 50
                                        text: Math.round(modelData.confidence * 100) + "% ×" + modelData.frequency
                                        color: Theme.onSurfaceVariant
                                        font.pixelSize: 9
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
                                font.pixelSize: 11
                            }

                            Repeater {
                                model: appToday
                                delegate: Row {
                                    width: todayCol.width
                                    spacing: 10
                                    Text {
                                        width: 140
                                        text: modelData.name
                                        color: Theme.onSurface
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                    Rectangle {
                                        width: parent.width - 140 - 110
                                        height: 14
                                        radius: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: Theme.outline
                                        Rectangle {
                                            width: parent.width * modelData.fraction
                                            height: parent.height
                                            radius: 4
                                            color: Theme.info
                                            Behavior on width { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
                                        }
                                    }
                                    Text {
                                        width: 100
                                        text: modelData.minutes + " " + (en ? "min" : "мин")
                                              + " · " + modelData.sessions + (en ? " sess." : " сесс.")
                                        color: Theme.onSurfaceVariant
                                        font.pixelSize: 9
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

                    Flow {
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: modelOptions
                            delegate: Rectangle {
                                radius: 8
                                height: 28
                                width: modelTxt.implicitWidth + 20
                                color: modelData === selectedModel ? Theme.outlineStrong : Theme.outline
                                border.width: 1
                                border.color: modelData === selectedModel ? root.cyan : Theme.outlineStrong
                                Text {
                                    id: modelTxt
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: Theme.onSurface
                                    font.pixelSize: 11
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: trainingCenter.selectModel(modelData)
                                }
                            }
                        }
                    }

                    Text { text: en ? "Max examples" : "Макс. примеров"; color: root.cyan; font.pixelSize: 13; font.bold: true; font.family: "Segoe UI Semibold" }

                    Flow {
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: [
                                { label: en ? "Quick (30)" : "Быстро (30)", value: 30 },
                                { label: en ? "Standard (80)" : "Стандарт (80)", value: 80 },
                                { label: en ? "Thorough (150)" : "Тщательно (150)", value: 150 },
                                { label: en ? "Maximum (300)" : "Максимум (300)", value: 300 }
                            ]
                            delegate: Rectangle {
                                radius: 8
                                height: 28
                                width: exTxt.implicitWidth + 20
                                color: modelData.value === maxExamples ? Theme.outlineStrong : Theme.outline
                                border.width: 1
                                border.color: modelData.value === maxExamples ? root.cyan : Theme.outlineStrong
                                Text {
                                    id: exTxt
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: Theme.onSurface
                                    font.pixelSize: 11
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: trainingCenter.setMaxExamples(modelData.value)
                                }
                            }
                        }
                    }

                    Rectangle {
                        width: 180; height: 36; radius: 8
                        enabled: likedCount > 0 && ollamaAvailable && !trainingActive
                        opacity: enabled ? 1 : 0.4
                        color: trainBtnArea.pressed ? Theme.accentMuted : Theme.accentMuted
                        border.width: 1
                        border.color: Theme.accentMuted

                        // Gentle pulse while training is running
                        SequentialAnimation on opacity {
                            running: trainingActive
                            loops: Animation.Infinite
                            NumberAnimation { from: 0.6; to: 1.0; duration: 700; easing.type: Easing.InOutSine }
                            NumberAnimation { from: 1.0; to: 0.6; duration: 700; easing.type: Easing.InOutSine }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: trainingActive
                                  ? (en ? "⏳ Training..." : "⏳ Обучение...")
                                  : (en ? "🚀 Start Training" : "🚀 Начать обучение")
                            color: Theme.onSurface
                            font.pixelSize: 12
                            font.bold: true
                        }
                        MouseArea {
                            id: trainBtnArea
                            anchors.fill: parent
                            enabled: parent.enabled
                            cursorShape: Qt.PointingHandCursor
                            onClicked: trainingCenter.startTraining()
                        }
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

                    Rectangle {
                        width: parent.width
                        height: 34
                        radius: 8
                        color: Theme.outline
                        border.width: 1
                        border.color: searchInput.activeFocus ? root.cyan : Theme.outlineStrong

                        TextInput {
                            id: searchInput
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 36
                            verticalAlignment: TextInput.AlignVCenter
                            color: Theme.onSurface
                            font.pixelSize: 12
                            clip: true
                            selectByMouse: true
                            onAccepted: trainingCenter.searchHistory(text)
                        }
                        Text {
                            text: en ? "Search chat history..." : "Поиск по истории чатов..."
                            visible: searchInput.text.length === 0 && !searchInput.activeFocus
                            color: Theme.onSurfaceDim
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            font.pixelSize: 12
                        }
                        Text {
                            text: "🔍"
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -8
                                cursorShape: Qt.PointingHandCursor
                                onClicked: trainingCenter.searchHistory(searchInput.text)
                            }
                        }
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
                                        // Alpha on the leaf colour, not opacity
                                        // on a subtree — no offscreen pass.
                                        color: Qt.rgba(Theme.accentMuted.r, Theme.accentMuted.g,
                                                       Theme.accentMuted.b,
                                                       0.18 + 0.55 * modelData.norm)

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

                                        x: modelData.x * graphContent.width - width / 2
                                        y: modelData.y * graphContent.height - diameter / 2
                                        width: Math.max(diameter, label.implicitWidth)
                                        height: diameter + 14

                                        // Staggered grow-in: the network assembles
                                        // itself instead of appearing all at once.
                                        opacity: 0
                                        scale: 0.4
                                        OpacityAnimator on opacity {
                                            running: true
                                            from: 0; to: 1
                                            duration: 260
                                            easing.type: Easing.OutCubic
                                        }
                                        ScaleAnimator on scale {
                                            running: true
                                            from: 0.4; to: 1
                                            duration: 340 + (nodeItem.index % 8) * 45
                                            easing.type: Easing.OutBack
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
                                                                  nodeItem.tint.b, 0.35)
                                            visible: modelData.major || hover.hovered
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
                                            color: hover.hovered
                                                ? nodeItem.tint
                                                : Qt.rgba(nodeItem.tint.r, nodeItem.tint.g,
                                                          nodeItem.tint.b, 0.75)
                                            border.width: 1
                                            border.color: Qt.rgba(nodeItem.tint.r, nodeItem.tint.g,
                                                                  nodeItem.tint.b, 0.9)

                                            HoverHandler { id: hover }
                                            ToolTip.visible: hover.hovered
                                            ToolTip.text: modelData.label
                                                + (root.en ? "\nfired ×" : "\nсработало ×") + modelData.activations
                                                + (root.en ? "\nsynapses: " : "\nсвязей: ") + modelData.degree
                                        }

                                        Text {
                                            id: label
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.top: dot.bottom
                                            anchors.topMargin: 2
                                            text: modelData.label
                                            // Minor concepts stay unlabelled until hovered:
                                            // forty permanent labels is a wall of text, and
                                            // the structure is carried by the connected few.
                                            visible: modelData.major || hover.hovered
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
                            }

                            // ---- Fit / reset ----
                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: 8
                                width: 54
                                height: 24
                                radius: Theme.radiusSm
                                color: fitArea.pressed ? Theme.outlineStrong : Theme.surface2
                                border.width: 1
                                border.color: Theme.outline
                                Text {
                                    anchors.centerIn: parent
                                    text: root.en ? "⤢ Fit" : "⤢ Сброс"
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 10
                                }
                                MouseArea {
                                    id: fitArea
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        graphContent.scale = 1.0
                                        graphContent.x = 0
                                        graphContent.y = 0
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

                            Row {
                                width: parent.width
                                Text {
                                    text: root.en ? "Strongest links" : "Крепчайшие связи"
                                    color: root.violet
                                    font.pixelSize: 12
                                    font.bold: true
                                    font.family: "Segoe UI Semibold"
                                }
                                Item { width: parent.width - 150; height: 1 }
                                Rectangle {
                                    width: 26; height: 20; radius: Theme.radiusSm
                                    color: sgRefreshArea.pressed ? Theme.outlineStrong : Theme.surface2
                                    border.width: 1
                                    border.color: Theme.outline
                                    Text {
                                        anchors.centerIn: parent
                                        text: "🔄"
                                        font.pixelSize: 10
                                    }
                                    MouseArea {
                                        id: sgRefreshArea
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: trainingCenter.refreshStats()
                                    }
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
                                                font.pixelSize: 10
                                                elide: Text.ElideRight
                                            }
                                            Row {
                                                width: parent.width
                                                spacing: 6
                                                Rectangle {
                                                    width: parent.width - 62
                                                    height: 4
                                                    radius: 2
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    color: Theme.outline
                                                    Rectangle {
                                                        width: parent.width * Math.min(1, modelData.weight / 3.0)
                                                        height: parent.height
                                                        radius: 2
                                                        color: root.violet
                                                        Behavior on width {
                                                            NumberAnimation { duration: 420
                                                                              easing.type: Easing.OutCubic }
                                                        }
                                                    }
                                                }
                                                Text {
                                                    width: 56
                                                    text: modelData.weight.toFixed(2)
                                                        + " ×" + modelData.coActivations
                                                    color: Theme.onSurfaceDim
                                                    font.pixelSize: 8
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
