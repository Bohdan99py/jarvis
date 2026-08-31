import QtQuick
import QtQuick.Layouts
import Jarvis.Theme
import Jarvis.Controls

// ============================================================
// NoticeStack.qml — четыре полосы над строкой ввода.
//
// Порядок сверху вниз — по срочности: обновление приложения ждёт
// дольше всех и стоит выше, уточнение требует ответа прямо сейчас
// и стоит ближе к полю ввода.
//
// Свёрнутая полоса имеет нулевую высоту, поэтому стек сам
// схлопывается — отдельного «показать/скрыть контейнер» нет.
// ============================================================

ColumnLayout {
    id: root

    readonly property bool en: chatCtl.english

    spacing: Theme.spaceSm

    JarvisNoticeBar {
        Layout.fillWidth: true
        open: noticeCtl.updateOpen
        text: noticeCtl.updateText
        tone: Theme.info
        actions: noticeCtl.updateBusy
                 ? []
                 : [root.en ? "Update" : "Обновить"]
        onActionTriggered: noticeCtl.acceptUpdate()
        onDismissed: noticeCtl.dismissUpdate()

        // Прогресс показываем только пока качаем: пустая полоска
        // до начала загрузки читается как «застряло на нуле».
        JarvisProgressBar {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spaceSm
            visible: noticeCtl.updateBusy
            implicitHeight: 4
            value: noticeCtl.updateProgress / 100
            fillColor: Theme.info
        }
    }

    JarvisNoticeBar {
        Layout.fillWidth: true
        open: noticeCtl.suggestionOpen
        text: noticeCtl.suggestionText
        tone: Theme.warning
        actions: [root.en ? "Yes" : "Да"]
        onActionTriggered: noticeCtl.acceptSuggestion()
        onDismissed: noticeCtl.dismissSuggestion()
    }

    JarvisNoticeBar {
        Layout.fillWidth: true
        open: noticeCtl.answerOpen
        text: noticeCtl.answerText
        tone: Theme.accentMuted
        actions: []
        onDismissed: noticeCtl.dismissAnswer()
    }

    JarvisNoticeBar {
        Layout.fillWidth: true
        open: noticeCtl.clarifyOpen
        text: noticeCtl.clarifyText
        tone: Theme.accent
        actions: noticeCtl.clarifyOptions
        onActionTriggered: index => noticeCtl.chooseClarify(index)
        onDismissed: noticeCtl.dismissClarify()
    }
}
