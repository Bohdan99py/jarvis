pragma Singleton

import QtQuick
import Jarvis.Theme

// ============================================================
// JarvisUi — вспомогательные функции дизайн-системы.
//
// Цвет и размеры живут в Theme/Type (C++). Здесь только то, что
// удобнее считать в QML: подкраска акцентом и длительности,
// уже умноженные на motionScale.
//
// Раньше tint() копипастился в каждый экран, а длительности
// каждый экран пересчитывал сам (`Theme.motionBase * Theme.motionScale`).
// Экран, который забывал умножить, игнорировал «уменьшить анимацию».
// ============================================================

QtObject {
    // accent из манифеста режима приходит СТРОКОЙ ("#ff6b6b"), а у
    // строки нет .r/.g/.b — Qt.rgba() от них вернул бы невалидный
    // цвет. Qt.lighter() с коэффициентом 1.0 не меняет цвет, но
    // приводит и строку, и color к настоящему color.
    function tint(c, a) {
        const col = Qt.lighter(c, 1.0)
        return Qt.rgba(col.r, col.g, col.b, a)
    }

    readonly property int durFast: Theme.motionFast * Theme.motionScale
    readonly property int durBase: Theme.motionBase * Theme.motionScale
    readonly property int durSlow: Theme.motionSlow * Theme.motionScale
}
