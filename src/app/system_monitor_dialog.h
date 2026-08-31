#pragma once
// -------------------------------------------------------
// system_monitor_dialog.h — Панель состояния машины
//
// Не «ещё один диспетчер задач»: смысл в том, что те же
// цифры видит и JARVIS (инструмент system_status), и
// человек — и разговор про «почему тормозит» начинается
// с одной картины у обоих.
//
// Опрос идёт только пока окно открыто: сэмплировать
// процессы раз в секунду в фоне ради панели, которую
// никто не смотрит, незачем.
// -------------------------------------------------------

#include <QDialog>

class SystemMonitor;
class QLabel;
class QTableWidget;
class Sparkline;

class SystemMonitorDialog : public QDialog
{
    Q_OBJECT

public:
    // Монитор общий с фоновым наблюдателем и НЕ принадлежит диалогу:
    // два независимых монитора означали бы два снимка процессов
    // в секунду вместо одного.
    SystemMonitorDialog(SystemMonitor* monitor, bool english, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void buildUi();
    void refresh();

    struct Card {
        Sparkline* chart = nullptr;
        QLabel*    value = nullptr;
        QLabel*    note  = nullptr;
    };
    // Карточка сразу добавляется в переданную компоновку — иначе
    // пришлось бы возвращать наружу и данные, и её каркас.
    Card makeCard(const QString& title, const QString& color, class QBoxLayout* into);

    SystemMonitor* m_monitor = nullptr;     // не владеем
    bool           m_english = false;
    int            m_backgroundIntervalMs = 5000;

    Card           m_cpu;
    Card           m_ram;
    Card           m_net;

    QLabel*        m_disks   = nullptr;
    QLabel*        m_uptime  = nullptr;
    QTableWidget*  m_procs   = nullptr;
};
