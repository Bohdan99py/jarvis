// -------------------------------------------------------
// system_monitor_dialog.cpp — см. system_monitor_dialog.h
// -------------------------------------------------------

#include "system_monitor_dialog.h"
#include "system_monitor.h"
#include "sparkline.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QBoxLayout>
#include <QFrame>

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

} // namespace


// ============================================================
//  SystemMonitorDialog
// ============================================================

SystemMonitorDialog::SystemMonitorDialog(SystemMonitor* monitor, bool english, QWidget* parent)
    : QDialog(parent)
    , m_monitor(monitor)
    , m_english(english)
{
    setWindowTitle(english ? QStringLiteral("System Monitor")
                           : QStringLiteral("Состояние системы"));
    resize(860, 640);

    if (m_monitor)
        connect(m_monitor, &SystemMonitor::sampled, this, &SystemMonitorDialog::refresh);

    buildUi();
}

SystemMonitorDialog::Card SystemMonitorDialog::makeCard(const QString& title,
                                                        const QString& color,
                                                        QBoxLayout* into)
{
    auto* frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("monCard"));

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(4);

    auto* titleLabel = new QLabel(title, frame);
    titleLabel->setObjectName(QStringLiteral("monTitle"));
    layout->addWidget(titleLabel);

    Card card;
    card.value = new QLabel(QStringLiteral("—"), frame);
    card.value->setObjectName(QStringLiteral("monValue"));
    card.value->setStyleSheet(QStringLiteral("color: %1;").arg(color));
    layout->addWidget(card.value);

    card.chart = new Sparkline(QColor(color), frame);
    layout->addWidget(card.chart, 1);

    card.note = new QLabel(QString(), frame);
    card.note->setObjectName(QStringLiteral("monNote"));
    layout->addWidget(card.note);

    into->addWidget(frame, 1);
    return card;
}

void SystemMonitorDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // --- Карточки ---
    auto* cards = new QHBoxLayout();
    cards->setSpacing(12);

    m_cpu = makeCard(m_english ? QStringLiteral("CPU") : QStringLiteral("ПРОЦЕССОР"),
                     QStringLiteral("#00d4ff"), cards);
    m_ram = makeCard(m_english ? QStringLiteral("MEMORY") : QStringLiteral("ПАМЯТЬ"),
                     QStringLiteral("#7c4dff"), cards);
    m_net = makeCard(m_english ? QStringLiteral("NETWORK") : QStringLiteral("СЕТЬ"),
                     QStringLiteral("#00e676"), cards);

    root->addLayout(cards, 2);

    // --- Диски и аптайм ---
    auto* infoRow = new QHBoxLayout();
    m_disks = new QLabel(this);
    m_disks->setObjectName(QStringLiteral("monInfo"));
    m_disks->setTextFormat(Qt::RichText);
    infoRow->addWidget(m_disks, 1);

    m_uptime = new QLabel(this);
    m_uptime->setObjectName(QStringLiteral("monInfo"));
    m_uptime->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    infoRow->addWidget(m_uptime);
    root->addLayout(infoRow);

    // --- Процессы ---
    m_procs = new QTableWidget(0, 4, this);
    m_procs->setHorizontalHeaderLabels({
        m_english ? QStringLiteral("Process") : QStringLiteral("Процесс"),
        QStringLiteral("PID"),
        QStringLiteral("CPU"),
        m_english ? QStringLiteral("Memory") : QStringLiteral("Память")
    });
    m_procs->verticalHeader()->setVisible(false);
    m_procs->setSelectionMode(QAbstractItemView::NoSelection);
    m_procs->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_procs->setShowGrid(false);
    m_procs->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_procs->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_procs->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_procs->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    root->addWidget(m_procs, 3);

    setStyleSheet(QStringLiteral(R"(
        QDialog { background-color: #080a12; }
        QLabel  { color: #c0d8ee; font-family: "Segoe UI", sans-serif; }
        #monCard {
            background-color: rgba(255, 255, 255, 8);
            border: 1px solid rgba(0, 212, 255, 45);
            border-radius: 12px;
        }
        #monTitle { color: #3a4a5e; font-size: 11px; letter-spacing: 2px; }
        #monValue { font-size: 26px; font-weight: bold; }
        #monNote  { color: #5a6a7e; font-size: 11px; }
        #monInfo  { color: #8fa7bf; font-size: 12px; }
        QTableWidget {
            background-color: rgba(255, 255, 255, 6);
            border: 1px solid rgba(0, 212, 255, 35);
            border-radius: 10px;
            color: #c0d8ee;
            gridline-color: transparent;
        }
        QHeaderView::section {
            background-color: transparent;
            color: #3a4a5e;
            border: none;
            padding: 6px;
            font-size: 11px;
            letter-spacing: 1px;
        }
    )"));
}

void SystemMonitorDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (!m_monitor)
        return;

    // Пока панель открыта — раз в секунду, иначе графики двигаются
    // рывками. Прежний шаг запоминаем: фоновому наблюдателю чаще не надо.
    m_backgroundIntervalMs = m_monitor->sampleIntervalMs();
    m_monitor->setSampleIntervalMs(1000);
    refresh();
}

void SystemMonitorDialog::hideEvent(QHideEvent* event)
{
    if (m_monitor)
        m_monitor->setSampleIntervalMs(m_backgroundIntervalMs);
    QDialog::hideEvent(event);
}

void SystemMonitorDialog::refresh()
{
    if (!m_monitor)
        return;

    // --- CPU ---
    {
        QVector<double> data;
        for (int v : m_monitor->cpuHistory())
            data.append(double(v));
        m_cpu.chart->setData(data, 100.0);
        m_cpu.value->setText(QStringLiteral("%1%").arg(m_monitor->cpuPercent()));
    }

    // --- RAM ---
    {
        QVector<double> data;
        for (int v : m_monitor->ramHistory())
            data.append(double(v));
        m_ram.chart->setData(data, 100.0);
        m_ram.value->setText(QStringLiteral("%1%").arg(m_monitor->ramPercent()));
        m_ram.note->setText(QStringLiteral("%1 / %2")
                                .arg(humanBytes(m_monitor->ramUsedBytes()),
                                     humanBytes(m_monitor->ramTotalBytes())));
    }

    // --- Сеть ---
    {
        m_net.chart->setData(m_monitor->netHistory(), -1.0);   // масштаб по данным
        m_net.value->setText(QStringLiteral("%1 KB/s")
                                 .arg(m_monitor->netDownKbps() + m_monitor->netUpKbps(),
                                      0, 'f', 0));
        m_net.note->setText(QStringLiteral("↓ %1   ↑ %2")
                                .arg(m_monitor->netDownKbps(), 0, 'f', 0)
                                .arg(m_monitor->netUpKbps(), 0, 'f', 0));
    }

    // --- Диски ---
    {
        QStringList parts;
        for (const DiskSample& d : m_monitor->disks()) {
            const int used = d.usedPercent();
            const QString color = used >= 90 ? QStringLiteral("#ff5252")
                                 : used >= 75 ? QStringLiteral("#ffb300")
                                              : QStringLiteral("#00e676");
            parts << QStringLiteral("%1 <b style='color:%2'>%3%</b> (%4 %5)")
                         .arg(d.root, color)
                         .arg(used)
                         .arg(humanBytes(d.freeBytes),
                              m_english ? QStringLiteral("free") : QStringLiteral("свободно"));
        }
        m_disks->setText(parts.join(QStringLiteral("&nbsp;&nbsp;·&nbsp;&nbsp;")));
    }

    // --- Аптайм ---
    {
        const qint64 up = m_monitor->uptimeSeconds();
        m_uptime->setText((m_english ? QStringLiteral("uptime %1h %2m")
                                     : QStringLiteral("аптайм %1 ч %2 мин"))
                              .arg(up / 3600).arg((up % 3600) / 60));
    }

    // --- Процессы ---
    {
        const QVector<ProcessSample> top = m_monitor->topProcesses(14);
        m_procs->setRowCount(top.size());
        for (int i = 0; i < top.size(); ++i) {
            const ProcessSample& p = top[i];

            auto* name = new QTableWidgetItem(p.name);
            auto* pid  = new QTableWidgetItem(QString::number(p.pid));
            auto* cpu  = new QTableWidgetItem(QStringLiteral("%1%")
                                                  .arg(p.cpuPercent, 0, 'f', 1));
            auto* mem  = new QTableWidgetItem(humanBytes(p.memBytes));

            pid->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            cpu->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            mem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

            if (p.cpuPercent >= 20.0)
                cpu->setForeground(QColor(QStringLiteral("#ffb300")));
            if (p.cpuPercent >= 50.0)
                cpu->setForeground(QColor(QStringLiteral("#ff5252")));

            m_procs->setItem(i, 0, name);
            m_procs->setItem(i, 1, pid);
            m_procs->setItem(i, 2, cpu);
            m_procs->setItem(i, 3, mem);
        }
    }
}
