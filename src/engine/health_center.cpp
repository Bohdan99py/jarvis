// -------------------------------------------------------
// health_center.cpp — см. health_center.h
// -------------------------------------------------------

#include "health_center.h"

#include "event_feed.h"
#include "tool_registry.h"

#include <QElapsedTimer>
#include <QDebug>

QString healthStateName(HealthState state)
{
    switch (state) {
    case HealthState::Ok:      return QStringLiteral("OK");
    case HealthState::Warning: return QStringLiteral("WARNING");
    case HealthState::Failed:  return QStringLiteral("FAIL");
    case HealthState::Unknown: return QStringLiteral("?");
    }
    return QStringLiteral("?");
}

HealthProbeResult HealthProbeResult::ok(const QString& detail)
{
    return { HealthState::Ok, detail };
}

HealthProbeResult HealthProbeResult::warning(const QString& detail)
{
    return { HealthState::Warning, detail };
}

HealthProbeResult HealthProbeResult::failed(const QString& detail)
{
    return { HealthState::Failed, detail };
}

QString HealthReport::toLine() const
{
    QString line = QStringLiteral("%1  %2")
                       .arg(name, -16)
                       .arg(healthStateName(state), -8);
    if (!detail.isEmpty())
        line += detail;
    return line.trimmed();
}

// ============================================================
//  HealthCenter
// ============================================================

HealthCenter::HealthCenter(QObject* parent)
    : QObject(parent)
{
}

void HealthCenter::addProbe(const QString& id, const QString& name, Probe probe)
{
    if (id.trimmed().isEmpty() || !probe)
        return;

    for (Entry& e : m_probes) {
        if (e.id == id) {
            // Перерегистрация — это замена, а не второй такой же пункт
            // в списке: MainWindow пересоздаётся, Jarvis нет.
            e.name  = name;
            e.probe = std::move(probe);
            return;
        }
    }
    m_probes.append(Entry{ id, name, std::move(probe) });
}

bool HealthCenter::hasProbe(const QString& id) const
{
    for (const Entry& e : m_probes) {
        if (e.id == id)
            return true;
    }
    return false;
}

QVector<HealthReport> HealthCenter::run()
{
    QVector<HealthReport> reports;
    reports.reserve(m_probes.size());

    for (const Entry& e : m_probes) {
        HealthReport r;
        r.id   = e.id;
        r.name = e.name;

        QElapsedTimer timer;
        timer.start();

        try {
            const HealthProbeResult res = e.probe();
            r.state  = res.state;
            r.detail = res.detail;
        } catch (const std::exception& ex) {
            r.state  = HealthState::Failed;
            r.detail = QString::fromUtf8(ex.what());
        } catch (...) {
            r.state  = HealthState::Failed;
            r.detail = QStringLiteral("проверка упала");
        }

        r.ms = static_cast<int>(timer.elapsed());
        reports.append(r);
    }

    m_last      = reports;
    m_lastRunAt = QDateTime::currentDateTime();

    // Сломанное состояние должно быть видно и без открытого экрана —
    // ради этого диагностика и запускается.
    QStringList broken;
    for (const HealthReport& r : reports) {
        if (r.state == HealthState::Failed)
            broken << r.name;
    }
    if (!broken.isEmpty()) {
        EventFeed::instance().post(
            QStringLiteral("health"), EventLevel::Error,
            QStringLiteral("Не работает: %1").arg(broken.join(QStringLiteral(", "))),
            summaryForModel(),
            QStringLiteral("health/broken"));
    }

    emit finished(reports);
    return reports;
}

HealthState HealthCenter::worst() const
{
    HealthState worst = HealthState::Ok;
    for (const HealthReport& r : m_last) {
        // Unknown хуже Warning, но лучше Failed: «не смогли проверить»
        // — это не поломка, но и не «всё хорошо».
        if (r.state == HealthState::Failed)
            return HealthState::Failed;
        if (r.state == HealthState::Unknown)
            worst = HealthState::Unknown;
        else if (r.state == HealthState::Warning && worst != HealthState::Unknown)
            worst = HealthState::Warning;
    }
    return worst;
}

QString HealthCenter::summaryForModel() const
{
    if (m_last.isEmpty())
        return QStringLiteral("Diagnostics have not been run yet.");

    QStringList lines;
    for (const HealthReport& r : m_last)
        lines << r.toLine();
    return lines.join(QChar('\n'));
}

// ============================================================
//  Инструмент
// ============================================================

namespace JarvisTools {

void registerHealthTools(ToolRegistry& reg, HealthCenter* center)
{
    if (!center) {
        qWarning() << "[Tools] registerHealthTools: center is null";
        return;
    }

    ToolSpec t;
    t.name        = QStringLiteral("run_diagnostics");
    t.category    = QStringLiteral("health");
    t.risk        = ToolRisk::Safe;
    t.description = QStringLiteral(
        "Check JARVIS's own subsystems right now: database, LLM key, tools, "
        "triggers, action log, disk space, git, voice. Use it when the user says "
        "something is broken, when a tool failed for no obvious reason, or when "
        "they ask whether everything is working. Report what is actually broken - "
        "do not read the whole list back.");
    t.schema  = ToolSchema::empty();
    t.handler = [center](const QJsonObject&) -> ToolResult {
        const QVector<HealthReport> reports = center->run();
        if (reports.isEmpty())
            return ToolResult::failure(QStringLiteral("No health probes registered."));

        int broken = 0, warned = 0;
        for (const HealthReport& r : reports) {
            if (r.state == HealthState::Failed)  ++broken;
            if (r.state == HealthState::Warning) ++warned;
        }

        const QString display = broken > 0
            ? QStringLiteral("Диагностика: сломано %1").arg(broken)
            : (warned > 0 ? QStringLiteral("Диагностика: предупреждений %1").arg(warned)
                          : QStringLiteral("Диагностика: всё в порядке"));

        return ToolResult::success(center->summaryForModel(), display);
    };
    reg.registerTool(t);
}

} // namespace JarvisTools
