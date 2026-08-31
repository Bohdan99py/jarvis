import QtQuick
import Jarvis.Theme

// ============================================================
// JarvisStat — «цифра и подпись к ней».
//
// В TrainingCenter этот блок повторён девять раз, каждый раз
// своими кеглями (20/10, 16/10, 13/9). Из-за этого одинаковые по
// смыслу числа выглядели разными по важности.
//
// Правило одно: цифра — крупная и светлая, подпись — мелкая и
// приглушённая. Никогда наоборот: подпись объясняет число, а не
// наоборот.
// ============================================================

Column {
    id: control

    property string value: ""
    property string label: ""
    property color valueColor: Theme.onSurface

    // Крупный вариант — для главного числа экрана. На одном экране
    // такое одно, иначе «главных» нет вовсе.
    property bool large: false

    spacing: 1

    Accessible.role: Accessible.StaticText
    Accessible.name: control.value + " " + control.label

    Text {
        text: control.value
        color: control.valueColor
        font.family: Type.family
        font.pixelSize: control.large ? Type.heading : Type.title
        font.weight: Font.DemiBold
    }

    Text {
        text: control.label
        color: Theme.onSurfaceVariant
        font.family: Type.family
        font.pixelSize: 10
    }
}
