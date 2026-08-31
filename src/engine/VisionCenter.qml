import QtQuick
import Jarvis.Theme
import Jarvis.Controls

// ============================================================
// VisionCenter.qml — что Джарвис видит и кого узнаёт.
//
// C++ (VisionCenterDialog) владеет данными и побочными эффектами;
// этот файл — внешним видом. Общение с C++ только через объект
// "visionCenter".
// ============================================================

Rectangle {
    id: root
    color: Theme.bg

    readonly property bool en: vcEnglish
    readonly property color cyan: Theme.accent
    readonly property color teal: Theme.accentMuted

    readonly property int currentTab: tabs.currentIndex

    Column {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Text {
            text: root.en ? "VISION CENTER" : "ЦЕНТР ЗРЕНИЯ"
            color: root.cyan
            font.pixelSize: 18
            font.bold: true
            font.family: "Segoe UI Semibold"
        }

        // ---- Tab bar ----
        JarvisTabBar {
            id: tabs
            titles: [
                root.en ? "Known faces"  : "Знакомые лица",
                root.en ? "Camera"       : "Камера",
                root.en ? "Screen vision": "Экранное зрение"
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
                GradientStop { position: 0.5; color: root.cyan }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        Item {
            width: parent.width
            height: parent.height - y

            // ======================================================
            // Tab 0 — Known faces
            // ======================================================
            Item {
                anchors.fill: parent
                visible: root.currentTab === 0
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.motionBase } }

                Column {
                    anchors.fill: parent
                    spacing: 10

                    Row {
                        width: parent.width
                        spacing: 8

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.en ? "Faces I can recognise: " : "Лиц узнаю: "
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 12
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: faceCount
                            color: root.cyan
                            font.pixelSize: 13
                            font.bold: true
                        }
                        Item { width: parent.width - 380; height: 1 }

                        JarvisButton {
                            glyph: ownerEnrolled ? "＋" : "👤"
                            text: ownerEnrolled
                                ? (root.en ? "Add more samples" : "Добавить образцы")
                                : (root.en ? "Teach my face"    : "Обучить моё лицо")
                            onClicked: visionCenter.teachMyFace()
                        }
                    }

                    Text {
                        width: parent.width
                        visible: knownFaces.length === 0
                        wrapMode: Text.WordWrap
                        text: root.en
                            ? "No faces learned yet. Teach your own face and I'll recognise you on camera — each profile stores light-pattern samples, so the more samples, the surer I am."
                            : "Пока не выучено ни одного лица. Обучите своё — и я буду узнавать вас на камере. В профиле хранятся образцы светового рисунка: чем больше образцов, тем увереннее узнавание."
                        color: Theme.onSurfaceVariant
                        font.pixelSize: 11
                        lineHeight: 1.4
                    }

                    Flickable {
                        width: parent.width
                        height: parent.height - y
                        clip: true
                        contentHeight: facesCol.height
                        boundsBehavior: Flickable.StopAtBounds

                        Column {
                            id: facesCol
                            width: parent.width
                            spacing: 8

                            Repeater {
                                model: knownFaces
                                delegate: Rectangle {
                                    required property var modelData
                                    width: facesCol.width
                                    height: 62
                                    radius: Theme.radiusMd
                                    color: Theme.surface1
                                    border.width: 1
                                    border.color: Theme.outline

                                    Row {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 12

                                        Rectangle {
                                            width: 36; height: 36; radius: 18
                                            anchors.verticalCenter: parent.verticalCenter
                                            color: Theme.accentSubtle
                                            border.width: 1
                                            border.color: modelData.local ? root.cyan : Theme.info
                                            Text {
                                                anchors.centerIn: parent
                                                text: "👤"
                                                font.pixelSize: 16
                                            }
                                        }

                                        Column {
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 3
                                            Row {
                                                spacing: 6
                                                Text {
                                                    text: modelData.name
                                                    color: Theme.onSurface
                                                    font.pixelSize: 13
                                                    font.bold: true
                                                }
                                                Text {
                                                    visible: modelData.age > 0
                                                    text: modelData.age
                                                    color: Theme.onSurfaceVariant
                                                    font.pixelSize: 11
                                                    anchors.verticalCenter: parent.verticalCenter
                                                }
                                                Rectangle {
                                                    visible: modelData.status.length > 0
                                                    width: statusTxt.implicitWidth + 12
                                                    height: 16
                                                    radius: 8
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    color: Theme.accentSubtle
                                                    Text {
                                                        id: statusTxt
                                                        anchors.centerIn: parent
                                                        text: modelData.status
                                                        color: root.teal
                                                        font.pixelSize: 9
                                                    }
                                                }
                                            }
                                            Text {
                                                // Происхождение важно: своё лицо можно
                                                // дообучить с этой камеры, приехавшее
                                                // по сети — нет.
                                                text: (modelData.local
                                                        ? (root.en ? "learned here" : "обучено здесь")
                                                        : (root.en ? "from peer " : "от узла ") + modelData.origin)
                                                      + " · "
                                                      + modelData.samples
                                                      + (root.en ? " samples" : " образцов")
                                                color: Theme.onSurfaceDim
                                                font.pixelSize: 9
                                            }
                                        }

                                        Item {
                                            width: parent.width - 340
                                            height: 1
                                        }

                                        JarvisButton {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: root.en ? "Forget" : "Забыть"
                                            variant: JarvisButton.Danger
                                            onClicked: visionCenter.forgetFace(modelData.id)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ======================================================
            // Tab 1 — Camera
            // ======================================================
            Item {
                anchors.fill: parent
                visible: root.currentTab === 1
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.motionBase } }

                Column {
                    anchors.fill: parent
                    spacing: 12

                    Row {
                        spacing: 8
                        Rectangle {
                            width: 9; height: 9; radius: 4.5
                            anchors.verticalCenter: parent.verticalCenter
                            color: webcamPresent ? Theme.success : Theme.error
                        }
                        Text {
                            text: webcamPresent
                                ? (root.en ? "Webcam detected" : "Веб-камера найдена")
                                : (root.en ? "No webcam — camera features unavailable"
                                           : "Веб-камера не найдена — функции камеры недоступны")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 12
                        }
                    }

                    Repeater {
                        model: [
                            { key: "monitor",
                              on: monitoringOn,
                              t: root.en ? "Keep watching" : "Наблюдать",
                              d: root.en ? "Check the frame every minute for faces and motion"
                                         : "Проверять кадр раз в минуту на лица и движение" },
                            { key: "unknown",
                              on: alertUnknownOn,
                              t: root.en ? "Warn on unknown face" : "Предупреждать о незнакомце",
                              d: root.en ? "Tell me when someone I don't recognise appears"
                                         : "Сообщать, когда появляется тот, кого я не узнаю" },
                            { key: "autolock",
                              on: autoLockOn,
                              t: root.en ? "Lock screen on threat" : "Блокировать экран при угрозе",
                              d: root.en ? "Lock the PC automatically when something looks wrong"
                                         : "Автоматически блокировать ПК, если что-то не так" },
                            { key: "motion",
                              on: motionAlertOn,
                              t: root.en ? "Motion alerts with video" : "Оповещения о движении с видео",
                              d: root.en ? "A 20-second clip goes to Telegram"
                                         : "Клип на 20 секунд уходит в Telegram" }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width
                            height: 56
                            radius: Theme.radiusMd
                            color: Theme.surface1
                            border.width: 1
                            border.color: Theme.outline
                            opacity: webcamPresent ? 1 : 0.45

                            // Тумблер прижат к правому краю, подпись занимает
                            // то, что осталось. Раньше между ними стояла
                            // распорка шириной parent.width - 400, то есть
                            // под текст закладывалось ровно 400 пикселей
                            // независимо от того, сколько он занимает: стоит
                            // подписи или переводу стать длиннее — и распорка
                            // выталкивает тумблер за край карточки, где его
                            // не видно и не нажать.
                            JarvisToggle {
                                id: rowToggle
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                on: modelData.on
                                label: ""
                                accessibleName: modelData.t
                                enabled: webcamPresent
                                onToggleRequested: want => {
                                    if (modelData.key === "monitor")
                                        visionCenter.toggleMonitoring(want)
                                    else if (modelData.key === "unknown")
                                        visionCenter.setAlertUnknown(want)
                                    else if (modelData.key === "autolock")
                                        visionCenter.setAutoLock(want)
                                    else
                                        visionCenter.setMotionAlert(want)
                                }
                            }

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 12
                                anchors.right: rowToggle.left
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                Text {
                                    width: parent.width
                                    elide: Text.ElideRight
                                    text: modelData.t
                                    color: Theme.onSurface
                                    font.pixelSize: 12
                                    font.bold: true
                                }
                                Text {
                                    width: parent.width
                                    elide: Text.ElideRight
                                    text: modelData.d
                                    color: Theme.onSurfaceDim
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }

                    // Всё, что раньше было отдельными пунктами меню
                    // «Камера». Flow, а не Row: кнопок шесть, и в узком
                    // окне они должны переноситься, а не уезжать за край.
                    Flow {
                        width: parent.width
                        spacing: 8

                        JarvisButton {
                            visible: webcamPresent
                            glyph: "👁"
                            text: root.en ? "Look now" : "Посмотреть сейчас"
                            onClicked: visionCenter.captureNow()
                        }

                        JarvisButton {
                            visible: webcamPresent
                            glyph: "🎥"
                            text: root.en ? "Who's on camera" : "Кто перед камерой"
                            onClicked: visionCenter.showLiveView()
                        }

                        // Кнопки разблокировки здесь нет и не будет: сеанс
                        // запирает сама Windows, и вернуть человека за
                        // машину может только она — паролем, PIN или Hello.
                        JarvisButton {
                            variant: JarvisButton.Danger
                            glyph: "🔒"
                            text: root.en ? "Lock screen now" : "Заблокировать экран"
                            onClicked: visionCenter.lockScreen()
                        }

                        JarvisButton {
                            glyph: "🖼"
                            text: root.en ? "Teach a face from photos"
                                          : "Обучить лицо по фотографиям"
                            onClicked: visionCenter.enrollFromPhoto()
                        }

                        JarvisButton {
                            glyph: "📸"
                            text: root.en ? "Take screenshot" : "Сделать скриншот"
                            onClicked: visionCenter.takeScreenshot()
                        }
                    }

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        color: Theme.onSurfaceDim
                        font.pixelSize: 10
                        text: root.en
                              ? "Locking uses the Windows lock screen — you come back with your password, PIN or Hello. Returning by face is Windows Hello's job, not mine."
                              : "Блокировка — штатная, средствами Windows: вернуться можно паролем, PIN или Windows Hello. Узнавание лица на входе делает Windows Hello, а не JARVIS."
                    }
                }
            }

            // ======================================================
            // Tab 2 — Screen vision
            // ======================================================
            Item {
                anchors.fill: parent
                visible: root.currentTab === 2
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.motionBase } }

                Column {
                    anchors.fill: parent
                    spacing: 10

                    Repeater {
                        model: [
                            { ok: ocrAvailable,
                              t: root.en ? "Reading text on screen (Tesseract OCR)"
                                         : "Чтение текста с экрана (Tesseract OCR)",
                              d: ocrAvailable
                                 ? (root.en ? "I can read what's in a screenshot, not just look at it"
                                            : "Могу прочитать, что на скриншоте, а не только посмотреть")
                                 : (root.en ? "Tesseract missing — screenshots capture, but text isn't extracted"
                                            : "Tesseract не найден — скриншоты снимаются, но текст не извлекается") },
                            { ok: pdfTextAvailable,
                              t: root.en ? "Reading PDFs (Poppler)" : "Чтение PDF (Poppler)",
                              d: pdfTextAvailable
                                 ? (root.en ? "PDFs in the knowledge base get read and distilled"
                                            : "PDF из базы знаний читаются и разбираются")
                                 : (root.en ? "Poppler missing — PDFs can't be read"
                                            : "Poppler не найден — PDF прочитать нельзя") }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width
                            height: 58
                            radius: Theme.radiusMd
                            color: Theme.surface1
                            border.width: 1
                            border.color: Theme.outline

                            Row {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10

                                Rectangle {
                                    width: 9; height: 9; radius: 4.5
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: modelData.ok ? Theme.success : Theme.error
                                }

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2
                                    Text {
                                        text: modelData.t
                                        color: Theme.onSurface
                                        font.pixelSize: 12
                                        font.bold: true
                                    }
                                    Text {
                                        text: modelData.d
                                        color: Theme.onSurfaceDim
                                        font.pixelSize: 10
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: root.en
                            ? "Screen vision also feeds learning: what you work in, and the text of the window you're in, become concepts in the synapse graph (Training → Synapse Graph, coloured as “Watched”)."
                            : "Экранное зрение работает и на обучение: приложение, в котором вы работаете, и текст его окна становятся понятиями в графе синапсов (Обучение → Граф синапсов, цвет «Увидено в работе»)."
                        color: Theme.onSurfaceVariant
                        font.pixelSize: 11
                        lineHeight: 1.4
                    }
                }
            }
        }
    }
}
