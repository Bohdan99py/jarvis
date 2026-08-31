// -------------------------------------------------------
// device_hub.cpp — см. device_hub.h
// -------------------------------------------------------

#include "device_hub.h"

QString DeviceInfo::statusName() const
{
    switch (status) {
    case Status::Online:    return QStringLiteral("online");
    case Status::Connected: return QStringLiteral("connected");
    case Status::Idle:      return QStringLiteral("idle");
    case Status::Offline:   return QStringLiteral("offline");
    }
    return QStringLiteral("offline");
}

DeviceHub::DeviceHub(QObject* parent)
    : QObject(parent)
{
}

void DeviceHub::addProvider(const QString& name, Provider provider)
{
    if (!provider)
        return;
    m_providers.append(Entry{ name, std::move(provider) });
}

QVector<DeviceInfo> DeviceHub::devices() const
{
    QVector<DeviceInfo> all;
    for (const Entry& e : m_providers)
        all += e.provider();
    return all;
}

const DeviceInfo* DeviceHub::find(const QString& idOrName, QVector<DeviceInfo>& cache) const
{
    // Список живёт ровно столько, сколько нужно вызывающему: устройства
    // собираются заново на каждый опрос, поэтому указатель в чужой
    // временный вектор отдавать нельзя — принимаем его снаружи.
    cache = devices();

    const QString q = idOrName.trimmed();
    if (q.isEmpty())
        return nullptr;

    for (const DeviceInfo& d : cache) {
        if (d.id.compare(q, Qt::CaseInsensitive) == 0)
            return &d;
    }
    for (const DeviceInfo& d : cache) {
        if (d.name.compare(q, Qt::CaseInsensitive) == 0
            || d.kind.compare(q, Qt::CaseInsensitive) == 0)
            return &d;
    }
    for (const DeviceInfo& d : cache) {
        if (d.name.contains(q, Qt::CaseInsensitive))
            return &d;
    }
    return nullptr;
}

QString DeviceHub::summaryForModel() const
{
    const QVector<DeviceInfo> all = devices();
    if (all.isEmpty())
        return QStringLiteral("No devices known.");

    QStringList lines;
    for (const DeviceInfo& d : all) {
        QString line = QStringLiteral("%1 [%2] — %3")
                           .arg(d.name, d.kind, d.statusText.isEmpty() ? d.statusName()
                                                                       : d.statusText);
        QStringList details;
        for (const auto& kv : d.details)
            details << kv.first + QStringLiteral(": ") + kv.second;
        if (!details.isEmpty())
            line += QStringLiteral("\n    ") + details.join(QStringLiteral(", "));
        lines << line;
    }
    return lines.join(QChar('\n'));
}
