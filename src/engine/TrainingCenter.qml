import QtQuick
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
            // ======================================================
            Item {
                anchors.fill: parent
                visible: root.currentTab === 4
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }

                Column {
                    anchors.fill: parent
                    spacing: 10

                    Row {
                        width: parent.width
                        Text {
                            text: en ? "Concept nodes: " : "Узлов-понятий: "
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 12
                        }
                        Text {
                            text: synapseNodeCount
                            color: root.cyan
                            font.pixelSize: 12
                            font.bold: true
                        }
                        Item { width: 24; height: 1 }
                        Text {
                            text: en ? "Synapses: " : "Связей: "
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 12
                        }
                        Text {
                            text: synapseEdgeCount
                            color: root.teal
                            font.pixelSize: 12
                            font.bold: true
                        }
                        Item { width: parent.width - 420; height: 1 }
                        Rectangle {
                            width: 90; height: 26; radius: 6
                            color: sgRefreshArea.pressed ? Theme.outlineStrong : Theme.accentSubtle
                            border.width: 1
                            border.color: Theme.outlineStrong
                            Text {
                                anchors.centerIn: parent
                                text: en ? "🔄 Refresh" : "🔄 Обновить"
                                color: root.cyan
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

                    Row {
                        width: parent.width
                        height: parent.height - y
                        spacing: 16

                        // ---- Most-activated concepts ----
                        Column {
                            width: (parent.width - 16) / 2
                            height: parent.height
                            spacing: 8

                            Text {
                                text: en ? "Most-fired concepts" : "Самые активные понятия"
                                color: Theme.success
                                font.pixelSize: 13
                                font.bold: true
                                font.family: "Segoe UI Semibold"
                            }

                            Flickable {
                                width: parent.width
                                height: parent.height - 30
                                clip: true
                                contentHeight: nodeCol.height
                                boundsBehavior: Flickable.StopAtBounds

                                Column {
                                    id: nodeCol
                                    width: parent.width
                                    spacing: 6

                                    Text {
                                        visible: synapseTopNodes.length === 0
                                        text: en ? "No concepts learned yet — the graph grows as JARVIS answers things."
                                                 : "Понятий пока нет — граф растёт по мере того, как JARVIS отвечает."
                                        color: Theme.onSurfaceVariant
                                        font.pixelSize: 11
                                        width: parent.width
                                        wrapMode: Text.WordWrap
                                    }

                                    Repeater {
                                        model: synapseTopNodes
                                        delegate: Row {
                                            width: nodeCol.width
                                            spacing: 10
                                            Text {
                                                width: 110
                                                text: modelData.label
                                                color: Theme.onSurface
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                            }
                                            Rectangle {
                                                width: parent.width - 110 - 34
                                                height: 14
                                                radius: 4
                                                anchors.verticalCenter: parent.verticalCenter
                                                color: Theme.outline
                                                Rectangle {
                                                    width: parent.width * modelData.fraction
                                                    height: parent.height
                                                    radius: 4
                                                    color: Theme.success
                                                    Behavior on width { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
                                                }
                                            }
                                            Text {
                                                width: 30
                                                text: modelData.activations
                                                color: Theme.onSurfaceVariant
                                                font.pixelSize: 9
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // ---- Strongest synapses ----
                        Column {
                            width: (parent.width - 16) / 2
                            height: parent.height
                            spacing: 8

                            Text {
                                text: en ? "Strongest synapses" : "Самые крепкие связи"
                                color: root.violet
                                font.pixelSize: 13
                                font.bold: true
                                font.family: "Segoe UI Semibold"
                            }

                            Flickable {
                                width: parent.width
                                height: parent.height - 30
                                clip: true
                                contentHeight: edgeCol.height
                                boundsBehavior: Flickable.StopAtBounds

                                Column {
                                    id: edgeCol
                                    width: parent.width
                                    spacing: 6

                                    Text {
                                        visible: synapseTopEdges.length === 0
                                        text: en ? "No associations yet."
                                                 : "Связей пока нет."
                                        color: Theme.onSurfaceVariant
                                        font.pixelSize: 11
                                    }

                                    Repeater {
                                        model: synapseTopEdges
                                        delegate: Column {
                                            width: edgeCol.width
                                            spacing: 2
                                            Text {
                                                width: parent.width
                                                text: modelData.labelA + " ↔ " + modelData.labelB
                                                color: Theme.onSurface
                                                font.pixelSize: 11
                                                elide: Text.ElideRight
                                            }
                                            Row {
                                                width: parent.width
                                                spacing: 8
                                                Rectangle {
                                                    width: parent.width - 90
                                                    height: 10
                                                    radius: 4
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    color: Theme.outline
                                                    Rectangle {
                                                        width: parent.width * modelData.fraction
                                                        height: parent.height
                                                        radius: 4
                                                        color: root.violet
                                                        Behavior on width { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
                                                    }
                                                }
                                                Text {
                                                    width: 82
                                                    text: modelData.weight.toFixed(2) + " · ×" + modelData.coActivations
                                                    color: Theme.onSurfaceVariant
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
