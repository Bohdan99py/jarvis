// -------------------------------------------------------
// bluetooth_devices.cpp — см. bluetooth_devices.h
// -------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "bluetooth_devices.h"

#include <QDebug>

#include <winsock2.h>      // до windows.h: BluetoothAPIs тянет типы сокетов
#include <windows.h>
#include <bluetoothapis.h>

QVector<BluetoothDeviceInfo> enumerateBluetoothDevices()
{
    QVector<BluetoothDeviceInfo> devices;

    BLUETOOTH_FIND_RADIO_PARAMS radioParams;
    radioParams.dwSize = sizeof(radioParams);

    HANDLE radio = nullptr;
    HBLUETOOTH_RADIO_FIND radioFind = BluetoothFindFirstRadio(&radioParams, &radio);
    if (!radioFind)
        return devices;   // адаптера нет — это не ошибка

    do {
        BLUETOOTH_DEVICE_SEARCH_PARAMS search;
        ZeroMemory(&search, sizeof(search));
        search.dwSize               = sizeof(search);
        search.fReturnAuthenticated = TRUE;
        search.fReturnRemembered    = TRUE;
        search.fReturnConnected     = TRUE;
        search.fReturnUnknown       = FALSE;
        search.fIssueInquiry        = FALSE;   // без сканирования эфира
        search.cTimeoutMultiplier   = 0;
        search.hRadio               = radio;

        BLUETOOTH_DEVICE_INFO info;
        ZeroMemory(&info, sizeof(info));
        info.dwSize = sizeof(info);

        HBLUETOOTH_DEVICE_FIND deviceFind = BluetoothFindFirstDevice(&search, &info);
        if (deviceFind) {
            do {
                BluetoothDeviceInfo d;
                d.name      = QString::fromWCharArray(info.szName).trimmed();
                d.connected = info.fConnected;
                d.paired    = info.fAuthenticated;

                const auto& a = info.Address.rgBytes;
                d.address = QStringLiteral("%1:%2:%3:%4:%5:%6")
                                .arg(a[5], 2, 16, QLatin1Char('0'))
                                .arg(a[4], 2, 16, QLatin1Char('0'))
                                .arg(a[3], 2, 16, QLatin1Char('0'))
                                .arg(a[2], 2, 16, QLatin1Char('0'))
                                .arg(a[1], 2, 16, QLatin1Char('0'))
                                .arg(a[0], 2, 16, QLatin1Char('0'))
                                .toUpper();

                if (d.name.isEmpty())
                    d.name = d.address;

                devices.append(d);

                ZeroMemory(&info, sizeof(info));
                info.dwSize = sizeof(info);
            } while (BluetoothFindNextDevice(deviceFind, &info));

            BluetoothFindDeviceClose(deviceFind);
        }

        CloseHandle(radio);
        radio = nullptr;
    } while (BluetoothFindNextRadio(radioFind, &radio));

    BluetoothFindRadioClose(radioFind);
    return devices;
}
