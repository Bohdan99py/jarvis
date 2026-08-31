import QtQuick
import QtQuick.Controls.Basic
import Jarvis.Theme
import Jarvis.Controls

// ============================================================
// ChatView.qml — лента сообщений главного экрана.
//
// Данные приходят из ChatModel (C++), а не из HTML-строки:
// chatModel — контекстное свойство, chatCtl — контроллер для
// действий (отправка, очистка).
//
// Прокрутка вниз происходит ТОЛЬКО если пользователь и так внизу.
// Старый QTextEdit дёргал скроллбар в максимум на каждое
// сообщение — читать историю во время ответа было невозможно.
// ============================================================

Rectangle {
    id: root
    color: Theme.bg

    readonly property bool en: chatCtl.english

    // «Внизу» с допуском: доводка прокрутки и субпиксельные высоты
    // почти никогда не дают ровное равенство.
    readonly property bool atBottom:
        list.contentHeight <= list.height
        || list.contentY >= list.contentHeight - list.height - 24

    ListView {
        id: list
        anchors.fill: parent
        anchors.margins: Theme.spaceLg
        anchors.bottomMargin: 0

        model: chatModel
        spacing: Theme.spaceSm
        clip: true
        cacheBuffer: 800
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true

        ScrollBar.vertical: ScrollBar { }

        // Лента «прилипает» к низу, пока пользователь сам не отмотал
        // вверх. Именно флаг, а не проверка «мы сейчас внизу?» в момент
        // вставки: высота сообщения становится известна на кадр-два
        // позже, contentHeight дорастает уже после прокрутки, и
        // проверка в этот момент врёт — лента застревала в 36 пикселях
        // от конца и держала кнопку «новые сообщения» включённой.
        property bool pinned: true

        onContentHeightChanged: if (pinned) Qt.callLater(positionViewAtEnd)
        onMovementEnded: pinned = root.atBottom

        delegate: Item {
            id: row

            required property int kind
            required property string who
            required property string text
            required property string time
            required property color accent

            width: list.width
            implicitHeight: bubble.implicitHeight

            readonly property bool own: kind === 1   // ChatModel::User

            JarvisChatBubble {
                id: bubble
                // Свои сообщения прижаты вправо, чужие влево —
                // сторона говорит, кто автор, ещё до чтения подписи.
                anchors.right: row.own ? parent.right : undefined
                anchors.left:  row.own ? undefined : parent.left
                width: Math.min(parent.width, Type.measureMax)

                who: row.who
                text: row.text
                time: row.time
                accentColor: row.accent
                own: row.own
            }
        }

    }

    // Пустая лента — не «нет данных», а старт сеанса: вместо заглушки
    // показываем сводку состояния системы.
    //
    // Панель — СОСЕД ListView, а не его ребёнок: элемент, объявленный
    // внутри ListView, попадает в его contentItem, то есть прокручивается
    // вместе с содержимым и учитывается в contentHeight.
    WelcomePanel {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spaceLg * 2, Type.measureMax)
        visible: list.count === 0

        // Пока панель на экране — контроллер опрашивает состояние
        // системы. Скрылась (пришло первое сообщение) — опрос
        // прекращается: дёргать диск и базу под чат незачем.
        onVisibleChanged: welcomeCtl.active = visible
        Component.onCompleted: welcomeCtl.active = visible
    }

    // Кнопка появляется, только когда лента ушла вверх: иначе она
    // всё время висит поверх последнего сообщения.
    JarvisButton {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spaceMd
        visible: !list.pinned && list.count > 0
        glyph: "↓"
        text: root.en ? "New messages" : "Новые сообщения"
        onClicked: {
            list.pinned = true
            list.positionViewAtEnd()
        }

        Behavior on opacity { NumberAnimation { duration: JarvisUi.durFast } }
    }

}
