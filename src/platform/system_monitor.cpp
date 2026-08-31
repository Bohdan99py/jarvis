// -------------------------------------------------------
// system_monitor.cpp — см. system_monitor.h
// -------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "system_monitor.h"

#include <QStorageInfo>
#include <QTimer>
#include <QDebug>

#include <algorithm>

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iphlpapi.h>

namespace {

quint64 fileTimeToU64(const FILETIME& ft)
{
    return (static_cast<quint64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

} // namespace

SystemMonitor::SystemMonitor(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &SystemMonitor::tick);

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    m_cpuCores = qMax(1, int(si.dwNumberOfProcessors));
}

void SystemMonitor::start(int intervalMs)
{
    m_timer->setInterval(qBound(200, intervalMs, 10000));
    if (!m_netClock.isValid())
        m_netClock.start();
    if (!m_procClock.isValid())
        m_procClock.start();

    m_timer->start();
    tick();   // первый кадр сразу — иначе панель секунду пустая
}

void SystemMonitor::stop()
{
    m_timer->stop();
}

bool SystemMonitor::isRunning() const
{
    return m_timer->isActive();
}

void SystemMonitor::setSampleIntervalMs(int ms)
{
    m_timer->setInterval(qBound(200, ms, 60000));
}

int SystemMonitor::sampleIntervalMs() const
{
    return m_timer->interval();
}

qint64 SystemMonitor::uptimeSeconds() const
{
    return static_cast<qint64>(GetTickCount64() / 1000);
}

// ============================================================
//  Тик
// ============================================================

void SystemMonitor::tick()
{
    sampleCpu();
    sampleMemory();
    sampleNetwork();

    if (m_tick % kProcEveryTicks == 0)
        sampleProcesses();
    if (m_tick % kDiskEveryTicks == 0 || m_disks.isEmpty())
        sampleDisks();

    ++m_tick;
    emit sampled();
}

// ============================================================
//  CPU
// ============================================================

void SystemMonitor::sampleCpu()
{
    FILETIME idleFt, kernelFt, userFt;
    if (!GetSystemTimes(&idleFt, &kernelFt, &userFt))
        return;

    const quint64 idle   = fileTimeToU64(idleFt);
    const quint64 kernel = fileTimeToU64(kernelFt);
    const quint64 user   = fileTimeToU64(userFt);

    if (m_prevKernel != 0) {
        const quint64 dIdle   = idle   - m_prevIdle;
        const quint64 dKernel = kernel - m_prevKernel;
        const quint64 dUser   = user   - m_prevUser;
        const quint64 total   = dKernel + dUser;   // kernel уже включает idle

        if (total > 0)
            m_cpu = int((total - dIdle) * 100 / total);
    }

    m_prevIdle   = idle;
    m_prevKernel = kernel;
    m_prevUser   = user;

    push(m_cpuHistory, m_cpu);
}

// ============================================================
//  Память
// ============================================================

void SystemMonitor::sampleMemory()
{
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem))
        return;

    m_ramTotal   = mem.ullTotalPhys;
    m_ramUsed    = mem.ullTotalPhys - mem.ullAvailPhys;
    m_ramPercent = int(mem.dwMemoryLoad);

    push(m_ramHistory, m_ramPercent);
}

// ============================================================
//  Сеть
// ============================================================

void SystemMonitor::sampleNetwork()
{
    // GetIfTable, а не GetIfTable2: нужен только суммарный счётчик
    // октетов, и старый API есть везде без дополнительных заголовков.
    ULONG size = 0;
    if (GetIfTable(nullptr, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER)
        return;

    QByteArray buffer(int(size), Qt::Uninitialized);
    auto* table = reinterpret_cast<MIB_IFTABLE*>(buffer.data());
    if (GetIfTable(table, &size, FALSE) != NO_ERROR)
        return;

    quint64 rx = 0, tx = 0;
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_IFROW& row = table->table[i];
        if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        rx += row.dwInOctets;
        tx += row.dwOutOctets;
    }

    const qint64 elapsedMs = m_netClock.restart();
    if (m_prevRx != 0 && elapsedMs > 0) {
        // Счётчики 32-битные и переполняются: отрицательная дельта — это
        // не «минус трафик», а оборот счётчика, такой отсчёт пропускаем.
        const double seconds = double(elapsedMs) / 1000.0;
        m_netDown = rx >= m_prevRx ? double(rx - m_prevRx) / 1024.0 / seconds : 0.0;
        m_netUp   = tx >= m_prevTx ? double(tx - m_prevTx) / 1024.0 / seconds : 0.0;
    }
    m_prevRx = rx;
    m_prevTx = tx;

    push(m_netHistory, m_netDown + m_netUp);
}

// ============================================================
//  Процессы
// ============================================================

void SystemMonitor::sampleProcesses()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return;

    const qint64 nowMs = m_procClock.elapsed();

    QVector<ProcessSample> fresh;
    QHash<quint32, ProcTime> times;

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snap, &entry)) {
        do {
            ProcessSample p;
            p.name = QString::fromWCharArray(entry.szExeFile);
            p.pid  = entry.th32ProcessID;

            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                                   FALSE, entry.th32ProcessID);
            if (h) {
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc)))
                    p.memBytes = pmc.WorkingSetSize;

                FILETIME creation, exit, kernel, user;
                if (GetProcessTimes(h, &creation, &exit, &kernel, &user)) {
                    const quint64 cpuTime = fileTimeToU64(kernel) + fileTimeToU64(user);
                    times.insert(p.pid, ProcTime{ cpuTime, nowMs });

                    // Загрузка процесса = его процессорное время за интервал,
                    // делённое на интервал и на число ядер. Без деления на
                    // ядра восьмипоточная сборка показывала бы 800%.
                    const auto prev = m_procTimes.constFind(p.pid);
                    if (prev != m_procTimes.constEnd() && nowMs > prev->atMs) {
                        const double deltaMs   = double(nowMs - prev->atMs);
                        const double cpuMs     = double(cpuTime - prev->cpuTime) / 10000.0;
                        p.cpuPercent = qBound(0.0, cpuMs / deltaMs / m_cpuCores * 100.0, 100.0);
                    }
                }
                CloseHandle(h);
            }
            fresh.append(p);
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);

    m_procTimes = times;
    m_processes = fresh;
}

QVector<ProcessSample> SystemMonitor::topProcesses(int count) const
{
    QVector<ProcessSample> sorted = m_processes;

    // Сортируем по загрузке CPU, а при равной (обычно нулевой) — по
    // памяти: список «что сейчас жрёт машину» в простое должен быть
    // осмысленным, а не случайным.
    std::sort(sorted.begin(), sorted.end(),
              [](const ProcessSample& a, const ProcessSample& b) {
        if (qFuzzyCompare(a.cpuPercent + 1.0, b.cpuPercent + 1.0))
            return a.memBytes > b.memBytes;
        return a.cpuPercent > b.cpuPercent;
    });

    if (sorted.size() > count)
        sorted.resize(count);
    return sorted;
}

// ============================================================
//  Диски
// ============================================================

void SystemMonitor::sampleDisks()
{
    QVector<DiskSample> fresh;
    for (const QStorageInfo& si : QStorageInfo::mountedVolumes()) {
        if (!si.isValid() || !si.isReady() || si.isReadOnly())
            continue;
        if (si.bytesTotal() <= 0)
            continue;

        DiskSample d;
        d.root       = si.rootPath();
        d.totalBytes = quint64(si.bytesTotal());
        d.freeBytes  = quint64(qMax<qint64>(0, si.bytesAvailable()));
        fresh.append(d);
    }
    m_disks = fresh;
}
