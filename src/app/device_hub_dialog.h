#pragma once
// -------------------------------------------------------
// device_hub_dialog.h — Панель устройств
//
// Карточка на устройство: имя, состояние, подробности.
// Обновляется по таймеру, пока окно открыто, — источники
// (ESP32, mesh, Bluetooth) сами о своих изменениях не
// сообщают единым сигналом, а заводить его ради панели
// значило бы протянуть новый сигнал через четыре
// подсистемы.
// -------------------------------------------------------

#include <QDialog>

class DeviceHub;
class Jarvis;
class QListWidget;
class QTimer;

class DeviceHubDialog : public QDialog
{
    Q_OBJECT

public:
    DeviceHubDialog(Jarvis* jarvis, bool english, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void buildUi();
    void reload();

    Jarvis*      m_jarvis  = nullptr;
    bool         m_english = false;
    QListWidget* m_list    = nullptr;
    QTimer*      m_timer   = nullptr;
};
