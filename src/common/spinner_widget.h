#pragma once
// ============================================================
// spinner_widget.h — Вращающаяся дуга «идёт обдумывание»
//
// Пульсирующая точка сообщает «что-то происходит», но не отличает
// «занят» от «жив»: моргание одинаково выглядит и когда ответ считается,
// и когда система просто дышит в простое. Вращение читается иначе —
// у него есть направление, поэтому оно означает именно работу.
//
// Рисуется QPainter'ом, а не подменой символов по таймеру: символьный
// «спиннер» дёргается на 8–10 кадрах и зависит от шрифта, дуга же
// вращается непрерывно и масштабируется под любой размер.
//
// Анимация останавливается при скрытии виджета — таймер, крутящийся
// за невидимым индикатором, это чистая трата кадров.
// ============================================================

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>
#include <QShowEvent>
#include <QHideEvent>

// Без Q_OBJECT намеренно: своих сигналов, слотов и Q_PROPERTY здесь нет —
// анимация тянет угол через лямбду на valueChanged, а получателем connect'а
// выступает QWidget, который сам по себе QObject. Объявить Q_OBJECT в
// header-only классе значит потребовать moc для файла, которого нет в
// списке исходников ни одной цели, — обычно это оборачивается ошибкой
// линковки vtable на пустом месте.
class SpinnerWidget : public QWidget
{
public:
    explicit SpinnerWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedSize(16, 16);

        m_anim = new QVariantAnimation(this);
        m_anim->setStartValue(0.0);
        m_anim->setEndValue(360.0);
        m_anim->setDuration(900);
        m_anim->setLoopCount(-1);
        connect(m_anim, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& v) { setAngle(v.toReal()); });
    }

    qreal angle() const { return m_angle; }
    void  setAngle(qreal a) { m_angle = a; update(); }

    void setColor(const QColor& c) { m_color = c; update(); }

    // Толщина дуги в пикселях; остальное считается от размера виджета.
    void setThickness(qreal t) { m_thickness = t; update(); }

protected:
    void showEvent(QShowEvent* e) override
    {
        QWidget::showEvent(e);
        m_anim->start();
    }

    void hideEvent(QHideEvent* e) override
    {
        m_anim->stop();
        QWidget::hideEvent(e);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const qreal inset = m_thickness / 2.0 + 1.0;
        const QRectF box = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
        if (box.width() <= 0 || box.height() <= 0) return;

        // Слабое кольцо-подложка: без неё дуга висит в пустоте и кажется
        // обрывком, а не индикатором.
        QColor track = m_color;
        track.setAlphaF(0.18);
        p.setPen(QPen(track, m_thickness, Qt::SolidLine, Qt::RoundCap));
        p.drawEllipse(box);

        // Qt меряет углы в 1/16 градуса и против часовой стрелки —
        // отрицательный span даёт привычное вращение по часовой.
        p.setPen(QPen(m_color, m_thickness, Qt::SolidLine, Qt::RoundCap));
        const int start = int(-m_angle * 16.0) + 90 * 16;
        p.drawArc(box, start, -100 * 16);
    }

private:
    QVariantAnimation* m_anim = nullptr;
    qreal   m_angle     = 0.0;
    qreal   m_thickness = 2.0;
    QColor  m_color     = QColor(0xAA, 0x66, 0xFF);
};
