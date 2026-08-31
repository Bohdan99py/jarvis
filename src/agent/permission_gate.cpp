// -------------------------------------------------------
// permission_gate.cpp — см. permission_gate.h
// -------------------------------------------------------

#include "permission_gate.h"

#include <QEventLoop>
#include <QTimer>
#include <QSettings>
#include <QDebug>

QString permissionModeName(PermissionMode mode)
{
    switch (mode) {
    case PermissionMode::Paranoid: return QStringLiteral("paranoid");
    case PermissionMode::Balanced: return QStringLiteral("balanced");
    case PermissionMode::Trusted:  return QStringLiteral("trusted");
    }
    return QStringLiteral("balanced");
}

PermissionMode permissionModeFromString(const QString& name, PermissionMode fallback)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("paranoid") || n == QLatin1String("strict"))
        return PermissionMode::Paranoid;
    if (n == QLatin1String("balanced") || n == QLatin1String("normal"))
        return PermissionMode::Balanced;
    if (n == QLatin1String("trusted") || n == QLatin1String("autonomous"))
        return PermissionMode::Trusted;
    return fallback;
}

QString permissionModeDescription(PermissionMode mode, bool english)
{
    switch (mode) {
    case PermissionMode::Paranoid:
        return english ? QStringLiteral("asks before every action, even reading")
                       : QStringLiteral("спрашивает про всё, даже про чтение");
    case PermissionMode::Balanced:
        return english ? QStringLiteral("reads and opens silently, asks before changing anything")
                       : QStringLiteral("читает и открывает молча, спрашивает перед изменениями");
    case PermissionMode::Trusted:
        return english ? QStringLiteral("acts on its own; only destructive actions ask")
                       : QStringLiteral("действует сам; спрашивает только про необратимое");
    }
    return QString();
}

PermissionGate::PermissionGate(QObject* parent)
    : QObject(parent)
{
    load();
}

void PermissionGate::setMode(PermissionMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    save();
    emit modeChanged(static_cast<int>(mode));
}

bool PermissionGate::needsConfirmation(ToolRisk risk, const QString& toolName) const
{
    // Dangerous спрашивается ВСЕГДА — никакой режим и никакой
    // "разрешить на сессию" его не снимает. Это единственное место,
    // где человек обязан присутствовать.
    if (risk == ToolRisk::Dangerous)
        return true;

    if (m_sessionGrants.contains(toolName))
        return false;

    switch (m_mode) {
    case PermissionMode::Paranoid: return true;
    case PermissionMode::Balanced: return risk != ToolRisk::Safe;
    case PermissionMode::Trusted:  return false;   // Safe + Moderate молча
    }
    return true;
}

void PermissionGate::evaluate(const QString& toolName,
                              ToolRisk risk,
                              const QString& summary,
                              Decision done)
{
    if (!done)
        return;

    if (!needsConfirmation(risk, toolName)) {
        done(true, QString());
        return;
    }

    if (!m_interactive) {
        // Некому спрашивать: фон, Telegram, автозадача.
        done(false, QStringLiteral(
                        "Action '%1' requires user confirmation, but no interactive "
                        "session is available. Ask the user to run it from the desktop app.")
                        .arg(toolName));
        return;
    }

    const quint64 id = m_nextId++;

    Pending p;
    p.toolName = toolName;
    p.summary  = summary;
    p.done     = std::move(done);

    if (m_timeoutMs > 0) {
        p.timer = new QTimer(this);
        p.timer->setSingleShot(true);
        p.timer->setInterval(m_timeoutMs);
        connect(p.timer, &QTimer::timeout, this, [this, id]() {
            finish(id, false, QStringLiteral("User did not respond in time — action cancelled."), false);
        });
        p.timer->start();
    }

    m_pending.insert(id, p);
    emit confirmationRequired(id, toolName, summary, static_cast<int>(risk));
}

bool PermissionGate::evaluateBlocking(const QString& toolName,
                                     ToolRisk risk,
                                     const QString& summary,
                                     QString* reasonOut)
{
    bool allowed = false;
    bool decided = false;
    QString reason;

    QEventLoop loop;
    evaluate(toolName, risk, summary, [&](bool a, const QString& r) {
        allowed = a;
        reason  = r;
        decided = true;
        if (loop.isRunning())
            loop.quit();
    });

    // Safe-инструмент решается сразу, ещё до exec() — иначе цикл
    // остался бы крутиться до таймаута с уже готовым ответом.
    if (!decided)
        loop.exec();

    if (reasonOut)
        *reasonOut = reason;
    return allowed;
}

void PermissionGate::resolve(quint64 requestId, bool allowed, bool rememberForSession)
{
    finish(requestId, allowed,
           allowed ? QString() : QStringLiteral("User declined this action."),
           rememberForSession);
}

void PermissionGate::finish(quint64 id, bool allowed, const QString& reason, bool remember)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;   // уже разрешён (таймаут против клика — кто первый)

    Pending p = *it;
    m_pending.erase(it);

    if (p.timer) {
        p.timer->stop();
        p.timer->deleteLater();
    }

    if (allowed && remember)
        allowForSession(p.toolName);

    emit decisionMade(id, allowed);

    if (p.done)
        p.done(allowed, reason);
}

void PermissionGate::allowForSession(const QString& toolName)
{
    if (!m_sessionGrants.contains(toolName))
        m_sessionGrants << toolName;
}

void PermissionGate::clearSessionGrants()
{
    m_sessionGrants.clear();
}

QStringList PermissionGate::sessionGrants() const
{
    return m_sessionGrants;
}

// ============================================================
//  Персистентность — только режим. Session grants намеренно
//  живут лишь до перезапуска.
// ============================================================

void PermissionGate::load()
{
    QSettings s(QStringLiteral("JARVIS"), QStringLiteral("Jarvis"));
    const int stored = s.value(QStringLiteral("agent/permissionMode"),
                               static_cast<int>(PermissionMode::Balanced)).toInt();
    if (stored >= 0 && stored <= 2)
        m_mode = static_cast<PermissionMode>(stored);
}

void PermissionGate::save() const
{
    QSettings s(QStringLiteral("JARVIS"), QStringLiteral("Jarvis"));
    s.setValue(QStringLiteral("agent/permissionMode"), static_cast<int>(m_mode));
}
