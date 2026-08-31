import QtQuick
import QtQuick.Layouts
import Jarvis.Theme

// ============================================================
// JarvisPanel — поверхность-контейнер: карточки, секции, боковые
// колонки. Один радиус, одна рамка, один отступ на всё приложение.
//
// Глубина набирается ступенью поверхности (surface1 на bg), а не
// тенью и не неоновой обводкой: рамка разделяет, но не обводит.
//
// Содержимое кладётся прямо внутрь — панель сама разложит его в
// колонку и посчитает свою высоту:
//
//     JarvisPanel {
//         Layout.fillWidth: true
//         title: "SYSTEM MONITOR"
//         Text { Layout.fillWidth: true; text: "…" }
//     }
// ============================================================

Rectangle {
    id: control

    // Пустой заголовок = панель без шапки. Отдельного флага не
    // держим: одно свойство, одно состояние.
    property string title: ""

    // compact — панель-плитка рядом с другими такими же.
    // В большой панели заголовок ведёт: он называет раздел. В плитке
    // наоборот — главное содержимое, а заголовок только подписывает
    // его. Одинаковый кегль в обоих случаях переворачивает иерархию:
    // подпись начинает кричать громче данных.
    property bool compact: false

    // Полоса акцента сверху: панель «принимает цвет» выбранной
    // сущности, не перекрашиваясь целиком.
    property bool  accentBar: false
    property color accentColor: Theme.accent

    property int contentPadding: compact ? Theme.spaceMd : Theme.spaceLg
    property alias contentSpacing: body.spacing

    default property alias content: body.data

    color: Theme.surface1
    radius: Theme.radiusMd
    border.width: 1
    border.color: Theme.outline

    // Ширину задаёт родитель (панели всегда тянутся по колонке),
    // высоту — содержимое.
    implicitHeight: body.implicitHeight + body.anchors.topMargin + body.anchors.bottomMargin

    Rectangle {
        visible: control.accentBar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        anchors.margins: 1
        height: 3
        radius: 2
        color: control.accentColor
        Behavior on color { ColorAnimation { duration: JarvisUi.durBase } }
    }

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: control.contentPadding
        anchors.topMargin: control.contentPadding + (control.accentBar ? 4 : 0)
        spacing: Theme.spaceMd

        Text {
            Layout.fillWidth: true
            visible: control.title.length > 0
            text: control.title
            color: control.compact ? Theme.onSurfaceVariant : Theme.onSurface
            font.family: Type.family
            font.pixelSize: control.compact ? Type.caption : Type.title
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
    }
}
