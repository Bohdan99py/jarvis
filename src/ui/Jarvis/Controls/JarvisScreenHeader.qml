import QtQuick
import QtQuick.Layouts
import Jarvis.Theme

// ============================================================
// JarvisScreenHeader — шапка экрана: заголовок, пояснение и
// место справа под статус или действие.
//
// Сейчас каждый экран пишет шапку сам, и они разошлись: где-то
// 18 px «Segoe UI Semibold» капсом, где-то Type.heading обычным
// регистром. Иерархия текста должна быть одна на всё приложение,
// иначе экраны выглядят как разные программы.
//
// Пояснение ограничено по ширине (Type.measureMax ~ 680 px):
// строка длиннее 75 знаков теряется глазом на возврате.
// ============================================================

RowLayout {
    id: control

    property string title: ""
    property string subtitle: ""

    // Слот справа: бейдж активного режима, кнопка, счётчик.
    default property alias trailing: trailingRow.data

    spacing: Theme.spaceLg

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2

        Text {
            Layout.fillWidth: true
            text: control.title
            color: Theme.onSurface
            font.family: Type.family
            font.pixelSize: Type.heading
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            Layout.maximumWidth: Type.measureMax
            visible: control.subtitle.length > 0
            text: control.subtitle
            color: Theme.onSurfaceVariant
            font.family: Type.family
            font.pixelSize: Type.caption
            lineHeight: Type.lineHeightBody
            lineHeightMode: Text.ProportionalHeight
            wrapMode: Text.WordWrap
        }
    }

    Row {
        id: trailingRow
        Layout.alignment: Qt.AlignVCenter
        spacing: Theme.spaceSm
    }
}
