#pragma once
// -------------------------------------------------------
// sparkline.h — Маленький график по кольцевому буферу
//
// Отдельный виджет вместо QtCharts: нужен ровно один
// заливаемый график без осей, легенды и интерактива, а
// Charts тянет целый модуль и свой рендер-бэкенд.
//
// Живёт отдельным файлом, потому что его рисуют двое —
// панель состояния системы и карточки дашборда.
// -------------------------------------------------------

#include <QColor>
#include <QVector>
#include <QWidget>

class Sparkline : public QWidget
{
    Q_OBJECT

public:
    explicit Sparkline(const QColor& color, QWidget* parent = nullptr);

    // maxValue <= 0 — масштаб по максимуму самих данных (для сети,
    // где потолок заранее неизвестен). Иначе фиксированный (проценты).
    void setData(const QVector<double>& values, double maxValue);

    // Сколько отсчётов помещается по ширине. По умолчанию берётся
    // ёмкость истории SystemMonitor, чтобы график не растягивался,
    // пока буфер не заполнен.
    void setCapacity(int capacity);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<double> m_values;
    double          m_max      = 100.0;
    int             m_capacity = 0;
    QColor          m_color;
};
