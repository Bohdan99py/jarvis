// -------------------------------------------------------
// sparkline.cpp — см. sparkline.h
// -------------------------------------------------------

#include "sparkline.h"
#include "system_monitor.h"

#include <QPainter>
#include <QPainterPath>

Sparkline::Sparkline(const QColor& color, QWidget* parent)
    : QWidget(parent)
    , m_capacity(SystemMonitor::historyCapacity())
    , m_color(color)
{
    setMinimumHeight(48);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void Sparkline::setData(const QVector<double>& values, double maxValue)
{
    m_values = values;
    m_max    = maxValue;
    update();
}

void Sparkline::setCapacity(int capacity)
{
    m_capacity = qMax(2, capacity);
    update();
}

void Sparkline::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF area = rect().adjusted(1, 1, -1, -1);

    // Сетка — три горизонтальные линии, чтобы глаз цеплялся за
    // уровень, а не гадал по высоте пятна.
    p.setPen(QPen(QColor(255, 255, 255, 16), 1));
    for (int i = 1; i <= 3; ++i) {
        const double y = area.top() + area.height() * i / 4.0;
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }

    if (m_values.size() < 2)
        return;

    double maxV = m_max;
    if (maxV <= 0.0) {
        maxV = 1.0;
        for (double v : m_values)
            maxV = qMax(maxV, v);
    }

    const double stepX  = area.width() / double(qMax(1, m_capacity - 1));
    // График всегда прижат к правому краю: свежие значения приходят
    // справа, и пока буфер не заполнен, слева просто пусто.
    const double startX = area.right() - stepX * (m_values.size() - 1);

    QPainterPath line;
    for (int i = 0; i < m_values.size(); ++i) {
        const double x = startX + stepX * i;
        const double y = area.bottom()
                         - qBound(0.0, m_values[i] / maxV, 1.0) * area.height();
        if (i == 0) line.moveTo(x, y);
        else        line.lineTo(x, y);
    }

    QPainterPath fill = line;
    fill.lineTo(area.right(), area.bottom());
    fill.lineTo(startX, area.bottom());
    fill.closeSubpath();

    QLinearGradient grad(0, area.top(), 0, area.bottom());
    QColor top = m_color; top.setAlpha(90);
    QColor bot = m_color; bot.setAlpha(10);
    grad.setColorAt(0.0, top);
    grad.setColorAt(1.0, bot);

    p.fillPath(fill, grad);
    p.setPen(QPen(m_color, 1.6));
    p.drawPath(line);
}
