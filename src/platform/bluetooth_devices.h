#pragma once
// -------------------------------------------------------
// bluetooth_devices.h — Перечисление Bluetooth-устройств
//
// Только уже известные системе устройства: сопряжённые,
// запомненные и подключённые. Инквайри (поиск новых в
// эфире) намеренно не запускается — он занимает секунды
// и будит радиомодуль, а панели нужно знать лишь то, что
// и так подключено.
// -------------------------------------------------------

#include <QString>
#include <QVector>

struct BluetoothDeviceInfo
{
    QString name;
    QString address;          // AA:BB:CC:DD:EE:FF
    bool    connected = false;
    bool    paired    = false;
};

// Пустой список — либо нет адаптера, либо ни одного известного
// устройства; различать эти случаи вызывающим не требуется.
QVector<BluetoothDeviceInfo> enumerateBluetoothDevices();
