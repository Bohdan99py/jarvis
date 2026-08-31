import QtQuick
import QtQuick.Layouts
import Jarvis.Theme

// ============================================================
// ComposerArea.qml — нижняя часть главного экрана: полосы
// уведомлений, вложения и строка ввода.
//
// Всё это один QQuickWidget, а не три: высота области зависит от
// того, сколько полос раскрыто, и считать её должен тот, кто их
// раскладывает. Три отдельных виджета пришлось бы синхронизировать
// по высоте вручную — ровно та арифметика, от которой уходим.
// ============================================================

// Фон задан явно, а не унаследован от хоста: полосы залиты
// полупрозрачным тоном, и на неокрашенной подложке они
// композитятся на белое — текст пропадает.
Rectangle {
    id: root

    color: Theme.bg
    implicitHeight: column.implicitHeight

    ColumnLayout {
        id: column
        anchors.fill: parent
        spacing: Theme.spaceSm

        NoticeStack {
            Layout.fillWidth: true
        }

        AttachmentBar {
            Layout.fillWidth: true
        }

        ChatComposer {
            Layout.fillWidth: true
        }
    }
}
