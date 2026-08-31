// -------------------------------------------------------
// system_watcher.cpp — см. system_watcher.h
// -------------------------------------------------------

#include "system_watcher.h"

#include "event_feed.h"
#include "system_monitor.h"

#include <QStringList>

namespace {

QString humanBytes(quint64 bytes)
{
    static const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = double(bytes);
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    return QString::number(v, 'f', v < 10.0 ? 1 : 0)
           + QLatin1Char(' ') + QLatin1String(units[i]);
}

QString minutesText(int seconds)
{
    const int minutes = seconds / 60;
    if (minutes <= 0)
        return QStringLiteral("%1 с").arg(seconds);
    return QStringLiteral("%1 мин").arg(minutes);
}

} // namespace

SystemWatcher::SystemWatcher(SystemMonitor* monitor, QObject* parent)
    : QObject(parent)
    , m_monitor(monitor)
{
    if (m_monitor)
        connect(m_monitor, &SystemMonitor::sampled, this, &SystemWatcher::onSampled);
}

void SystemWatcher::setCpuRule(int percent, int holdSeconds)
{
    m_cpu.threshold   = qBound(10, percent, 100);
    m_cpu.holdSeconds = qMax(5, holdSeconds);
}

void SystemWatcher::setRamRule(int percent, int holdSeconds)
{
    m_ram.threshold   = qBound(10, percent, 100);
    m_ram.holdSeconds = qMax(5, holdSeconds);
}

void SystemWatcher::setDiskRule(int freePercent)
{
    m_diskFreePercent = qBound(1, freePercent, 50);
}

int SystemWatcher::update(Sustained& rule, int value, int intervalMs)
{
    if (!rule.firing) {
        if (value >= rule.threshold) {
            rule.streakMs += intervalMs;
            if (rule.streakMs >= rule.holdSeconds * 1000) {
                rule.firing = true;
                return 1;
            }
        } else {
            rule.streakMs = 0;   // прервалось — считаем заново
        }
        return 0;
    }

    // Уже сработало: ждём падения ниже порога отпускания
    if (value <= rule.releaseThreshold()) {
        rule.firing   = false;
        rule.streakMs = 0;
        return -1;
    }
    return 0;
}

void SystemWatcher::onSampled()
{
    if (!m_enabled || !m_monitor)
        return;

    const int interval = m_monitor->sampleIntervalMs();
    EventFeed& feed = EventFeed::instance();

    // --- CPU ---
    switch (update(m_cpu, m_monitor->cpuPercent(), interval)) {
    case 1: {
        // Виновник — самый прожорливый процесс на момент срабатывания.
        QString culprit;
        const auto top = m_monitor->topProcesses(1);
        if (!top.isEmpty() && top.first().cpuPercent >= 5.0) {
            culprit = QStringLiteral("%1 — %2%")
                          .arg(top.first().name)
                          .arg(top.first().cpuPercent, 0, 'f', 0);
        }
        feed.post(QStringLiteral("system"), EventLevel::Warning,
                  QStringLiteral("Загрузка CPU выше %1%% уже %2")
                      .arg(m_cpu.threshold)
                      .arg(minutesText(m_cpu.holdSeconds)),
                  culprit,
                  QStringLiteral("system/cpu-high"));
        break;
    }
    case -1:
        feed.post(QStringLiteral("system"), EventLevel::Info,
                  QStringLiteral("Загрузка CPU вернулась в норму"),
                  QStringLiteral("сейчас %1%").arg(m_monitor->cpuPercent()),
                  QStringLiteral("system/cpu-normal"));
        break;
    default:
        break;
    }

    // --- Память ---
    switch (update(m_ram, m_monitor->ramPercent(), interval)) {
    case 1: {
        QString culprit;
        const auto top = m_monitor->topProcesses(1);
        if (!top.isEmpty())
            culprit = QStringLiteral("%1 — %2")
                          .arg(top.first().name, humanBytes(top.first().memBytes));
        feed.post(QStringLiteral("system"), EventLevel::Warning,
                  QStringLiteral("Память занята более чем на %1%% уже %2")
                      .arg(m_ram.threshold)
                      .arg(minutesText(m_ram.holdSeconds)),
                  culprit,
                  QStringLiteral("system/ram-high"));
        break;
    }
    case -1:
        feed.post(QStringLiteral("system"), EventLevel::Info,
                  QStringLiteral("Память освободилась"),
                  QStringLiteral("сейчас %1%").arg(m_monitor->ramPercent()),
                  QStringLiteral("system/ram-normal"));
        break;
    default:
        break;
    }

    // --- Диски ---
    for (const DiskSample& disk : m_monitor->disks()) {
        if (disk.totalBytes == 0)
            continue;

        const int freePercent = int(disk.freeBytes * 100 / disk.totalBytes);
        const bool warned = m_diskWarned.contains(disk.root);

        if (freePercent <= m_diskFreePercent && !warned) {
            m_diskWarned << disk.root;
            EventFeed::instance().post(
                QStringLiteral("system"), EventLevel::Warning,
                QStringLiteral("На диске %1 осталось %2%% места").arg(disk.root).arg(freePercent),
                QStringLiteral("свободно %1 из %2")
                    .arg(humanBytes(disk.freeBytes), humanBytes(disk.totalBytes)),
                QStringLiteral("system/disk-low/") + disk.root);
        } else if (warned && freePercent > m_diskFreePercent + 3) {
            // Гистерезис и здесь: место, гуляющее вокруг порога, иначе
            // выдаст предупреждение при каждой сборке.
            m_diskWarned.removeAll(disk.root);
        }
    }
}
