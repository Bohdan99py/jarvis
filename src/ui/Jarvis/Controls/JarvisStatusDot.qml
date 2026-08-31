import QtQuick
import Jarvis.Theme

// ============================================================
// JarvisStatusDot — точка состояния.
//
// Цвет никогда не единственный носитель смысла (WCAG 1.4.1):
// точка всегда стоит рядом с подписью — сама по себе она только
// привлекает взгляд, но ничего не сообщает.
//
// Пульсация подчиняется «уменьшить анимацию»: при motionScale = 0
// анимация не запускается вовсе, а не крутится вхолостую.
// ============================================================

Rectangle {
    id: control

    property bool pulsing: false

    implicitWidth: 8
    implicitHeight: 8
    width: implicitWidth
    height: implicitHeight
    radius: width / 2
    color: Theme.accent

    Accessible.ignored: true

    SequentialAnimation {
        running: control.pulsing && Theme.motionScale > 0
        loops: Animation.Infinite

        OpacityAnimator {
            target: control
            from: 1.0; to: 0.35
            duration: 900
            easing.type: Easing.InOutQuad
        }
        OpacityAnimator {
            target: control
            from: 0.35; to: 1.0
            duration: 900
            easing.type: Easing.InOutQuad
        }

        // Остановленная анимация оставляет opacity там, где её
        // прервали. Без сброса точка застывала полупрозрачной и
        // читалась как «выключено».
        onRunningChanged: if (!running) control.opacity = 1.0
    }
}
