#pragma once
// -------------------------------------------------------
// device_hub.h — Одно место, где видно все устройства
//
// Устройства в проекте уже были: ESP32-нода, спаренные
// телефоны, соседние экземпляры JARVIS в mesh, Bluetooth.
// Не было общего взгляда — каждое жило в своей подсистеме
// и о своём состоянии сообщало по-своему.
//
// Хаб ничего не опрашивает сам и не знает ни одного из этих
// типов: источники регистрирует Jarvis лямбдами, как у
// GlobalSearch. Иначе слой действий пришлось бы связать с
// network, а он про сеть знать не должен.
// -------------------------------------------------------

#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

#include <functional>

// ============================================================
//  Устройство
// ============================================================
struct DeviceInfo
{
    enum class Status {
        Online,     // это сама машина
        Connected,  // на связи прямо сейчас
        Idle,       // известно, но давно не отвечало
        Offline     // настроено, но недоступно
    };

    QString id;
    QString name;
    QString kind;        // pc | esp32 | phone | peer | bluetooth
    QString icon;
    Status  status = Status::Offline;
    QString statusText;

    // Пары «подпись — значение» для карточки и для ответа модели
    QVector<QPair<QString, QString>> details;

    QString statusName() const;
};

// ============================================================
//  DeviceHub
// ============================================================
class DeviceHub : public QObject
{
    Q_OBJECT

public:
    explicit DeviceHub(QObject* parent = nullptr);

    // Провайдер вызывается каждый раз при опросе — он обязан быть
    // дешёвым: читать уже известное состояние, а не лезть в сеть.
    using Provider = std::function<QVector<DeviceInfo>()>;
    void addProvider(const QString& name, Provider provider);

    QVector<DeviceInfo> devices() const;
    const DeviceInfo*   find(const QString& idOrName,
                             QVector<DeviceInfo>& cache) const;

    QString summaryForModel() const;

signals:
    void changed();

private:
    struct Entry {
        QString  name;
        Provider provider;
    };
    QVector<Entry> m_providers;
};
