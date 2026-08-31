// -------------------------------------------------------
// device_hub_dialog.cpp — см. device_hub_dialog.h
// -------------------------------------------------------

#include "device_hub_dialog.h"

#include "device_hub.h"
#include "jarvis.h"

#include <QLabel>
#include <QListWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString statusColor(DeviceInfo::Status status)
{
    switch (status) {
    case DeviceInfo::Status::Online:    return QStringLiteral("#00d4ff");
    case DeviceInfo::Status::Connected: return QStringLiteral("#00e676");
    case DeviceInfo::Status::Idle:      return QStringLiteral("#ffb300");
    case DeviceInfo::Status::Offline:   return QStringLiteral("#ff5252");
    }
    return QStringLiteral("#8fa7bf");
}

} // namespace

DeviceHubDialog::DeviceHubDialog(Jarvis* jarvis, bool english, QWidget* parent)
    : QDialog(parent)
    , m_jarvis(jarvis)
    , m_english(english)
{
    setWindowTitle(english ? QStringLiteral("Devices") : QStringLiteral("Устройства"));
    resize(620, 520);

    m_timer = new QTimer(this);
    m_timer->setInterval(3000);
    connect(m_timer, &QTimer::timeout, this, &DeviceHubDialog::reload);

    buildUi();
}

void DeviceHubDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setWordWrap(true);
    root->addWidget(m_list, 1);

    auto* hint = new QLabel(
        m_english ? QStringLiteral("Ask JARVIS to control a device — "
                                   "\"set the ESP32 LED to alert\"")
                  : QStringLiteral("Управлять устройством можно словами — "
                                   "«поставь на ESP32 режим alert»"), this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    setStyleSheet(QStringLiteral(R"(
        QDialog { background-color: #080a12; }
        QLabel  { color: #3a4a5e; font-family: "Segoe UI", sans-serif; font-size: 11px; }
        QListWidget {
            background-color: rgba(255, 255, 255, 6);
            border: 1px solid rgba(0, 212, 255, 35);
            border-radius: 10px;
            color: #c0d8ee;
            font-family: "Consolas", "Segoe UI", monospace;
            font-size: 13px;
        }
        QListWidget::item { padding: 9px 8px; }
    )"));
}

void DeviceHubDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    reload();
    m_timer->start();
}

void DeviceHubDialog::hideEvent(QHideEvent* event)
{
    m_timer->stop();
    QDialog::hideEvent(event);
}

void DeviceHubDialog::reload()
{
    if (!m_jarvis || !m_jarvis->devices() || !m_list)
        return;

    // Полная перестройка списка на каждый тик: устройств единицы,
    // а точечное обновление потребовало бы сопоставлять строки с id
    // и следить за исчезнувшими — сложность без выигрыша.
    m_list->clear();

    for (const DeviceInfo& d : m_jarvis->devices()->devices()) {
        QString text = QStringLiteral("%1  %2")
                           .arg(d.icon.isEmpty() ? QStringLiteral("•") : d.icon, d.name);
        text += QStringLiteral("        ● ") + (d.statusText.isEmpty() ? d.statusName()
                                                                      : d.statusText);
        for (const auto& kv : d.details)
            text += QChar('\n') + QStringLiteral("      ") + kv.first
                    + QStringLiteral(": ") + kv.second;

        auto* item = new QListWidgetItem(text, m_list);
        item->setForeground(QColor(statusColor(d.status)));
        item->setFlags(Qt::NoItemFlags);
    }

    if (m_list->count() == 0) {
        auto* item = new QListWidgetItem(
            m_english ? QStringLiteral("No devices found.")
                      : QStringLiteral("Устройств не найдено."), m_list);
        item->setFlags(Qt::NoItemFlags);
    }
}
