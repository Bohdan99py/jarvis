#pragma once
// -------------------------------------------------------
// system_watcher.h — Превращает цифры в события
//
// SystemMonitor выдаёт отсчёт раз в несколько секунд. Если
// класть каждый в ленту, лента станет бесполезной. Здесь
// живёт правило, по которому отсчёты становятся событием:
//
//   держится выше порога дольше N секунд  ->  одно событие
//   вернулось в норму                     ->  одно событие
//
// Плюс гистерезис: порог включения и порог выключения
// разные, иначе значение, гуляющее вокруг 90%, выдаст
// десяток «превысило / вернулось» подряд.
//
// К предупреждению о нагрузке приписывается процесс-виновник:
// «CPU выше 90% четыре минуты» без имени процесса — это ещё
// не повод что-то делать, а с именем — уже повод.
// -------------------------------------------------------

#include <QObject>
#include <QString>

class SystemMonitor;

class SystemWatcher : public QObject
{
    Q_OBJECT

public:
    explicit SystemWatcher(SystemMonitor* monitor, QObject* parent = nullptr);

    // Порог срабатывания и сколько секунд он должен держаться
    void setCpuRule(int percent, int holdSeconds);
    void setRamRule(int percent, int holdSeconds);

    // Свободного места меньше этого процента — предупреждение
    void setDiskRule(int freePercent);

    void setEnabled(bool on) { m_enabled = on; }
    bool isEnabled() const   { return m_enabled; }

private:
    void onSampled();

    // Одно правило вида «держится выше порога»
    struct Sustained {
        int  threshold   = 90;   // порог включения, %
        int  holdSeconds = 180;  // сколько держаться, чтобы сработать
        int  streakMs    = 0;    // сколько уже держится
        bool firing      = false;

        // Порог выключения ниже порога включения — гистерезис.
        int releaseThreshold() const { return qMax(0, threshold - 15); }
    };

    // Возвращает: 1 — только что сработало, -1 — только что отпустило, 0 — без изменений
    static int update(Sustained& rule, int value, int intervalMs);

    SystemMonitor* m_monitor = nullptr;
    bool           m_enabled = true;

    Sustained m_cpu;
    Sustained m_ram;

    int  m_diskFreePercent = 5;
    // Корни дисков, по которым предупреждение уже выдано: повторять
    // его каждые пять секунд, пока место не освободят, незачем.
    QStringList m_diskWarned;
};
