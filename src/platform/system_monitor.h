#pragma once
// -------------------------------------------------------
// system_monitor.h — Живые показатели машины
//
// Инструмент system_status отвечает на разовый вопрос
// «как дела у ПК» и ради честной цифры CPU честно спит
// 150 мс между двумя замерами. Для панели, которая
// обновляется раз в секунду, так делать нельзя: здесь
// предыдущий замер хранится между тиками, и опрос
// получается мгновенным.
//
// Считаем сами, без PDH: GetSystemTimes для CPU,
// GlobalMemoryStatusEx для памяти, GetIfTable для сети,
// Toolhelp + GetProcessTimes для процессов.
//
// GPU здесь намеренно нет: без вендорских библиотек
// (NVML/ADL) её загрузку не получить, а дёргать
// nvidia-smi раз в секунду — процесс на каждый тик.
// -------------------------------------------------------

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

class QTimer;

// ============================================================
//  Снимки
// ============================================================

struct ProcessSample
{
    QString  name;
    quint32  pid        = 0;
    double   cpuPercent = 0.0;
    quint64  memBytes   = 0;
};

struct DiskSample
{
    QString  root;
    quint64  totalBytes = 0;
    quint64  freeBytes  = 0;

    int usedPercent() const
    {
        return totalBytes ? int((totalBytes - freeBytes) * 100 / totalBytes) : 0;
    }
};

// ============================================================
//  SystemMonitor
// ============================================================
class SystemMonitor : public QObject
{
    Q_OBJECT

public:
    explicit SystemMonitor(QObject* parent = nullptr);

    void start(int intervalMs = 1000);
    void stop();
    bool isRunning() const;

    // Один монитор на всё приложение: фоновый наблюдатель опрашивает
    // редко, а открытая панель просит чаще и возвращает как было.
    // Два независимых монитора означали бы два снимка процессов.
    void setSampleIntervalMs(int ms);
    int  sampleIntervalMs() const;

    // --- Текущие значения ---
    int     cpuPercent() const     { return m_cpu; }
    int     ramPercent() const     { return m_ramPercent; }
    quint64 ramUsedBytes() const   { return m_ramUsed; }
    quint64 ramTotalBytes() const  { return m_ramTotal; }
    double  netDownKbps() const    { return m_netDown; }
    double  netUpKbps() const      { return m_netUp; }
    qint64  uptimeSeconds() const;

    // --- История (последние historyCapacity() отсчётов) ---
    QVector<int>    cpuHistory() const  { return m_cpuHistory; }
    QVector<int>    ramHistory() const  { return m_ramHistory; }
    QVector<double> netHistory() const  { return m_netHistory; }   // down+up, KB/s

    static int historyCapacity() { return kHistory; }

    // --- Списки ---
    QVector<ProcessSample> topProcesses(int count = 12) const;
    QVector<DiskSample>    disks() const { return m_disks; }

signals:
    void sampled();

private:
    void tick();
    void sampleCpu();
    void sampleMemory();
    void sampleNetwork();
    void sampleProcesses();
    void sampleDisks();

    template <typename T>
    static void push(QVector<T>& history, T value)
    {
        history.append(value);
        while (history.size() > kHistory)
            history.removeFirst();
    }

    QTimer* m_timer = nullptr;
    int     m_tick  = 0;

    // CPU: предыдущие значения счётчиков, а не пауза внутри замера
    quint64 m_prevIdle = 0, m_prevKernel = 0, m_prevUser = 0;
    int     m_cpu = 0;

    int     m_ramPercent = 0;
    quint64 m_ramUsed = 0, m_ramTotal = 0;

    quint64        m_prevRx = 0, m_prevTx = 0;
    double         m_netDown = 0.0, m_netUp = 0.0;
    QElapsedTimer  m_netClock;

    QVector<int>    m_cpuHistory;
    QVector<int>    m_ramHistory;
    QVector<double> m_netHistory;

    QVector<ProcessSample> m_processes;
    QVector<DiskSample>    m_disks;

    // pid -> (суммарное процессорное время, момент замера) для расчёта
    // загрузки конкретного процесса между двумя обновлениями списка
    struct ProcTime { quint64 cpuTime = 0; qint64 atMs = 0; };
    QHash<quint32, ProcTime> m_procTimes;
    QElapsedTimer            m_procClock;

    int m_cpuCores = 1;

    static constexpr int kHistory        = 120;  // 2 минуты при шаге в секунду
    static constexpr int kProcEveryTicks = 3;    // список процессов — не каждый тик
    static constexpr int kDiskEveryTicks = 15;
};
