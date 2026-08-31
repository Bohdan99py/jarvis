// -------------------------------------------------------
// action_log.cpp — см. action_log.h
// -------------------------------------------------------

#include "action_log.h"

#include "jarvis_paths.h"
#include "permission_gate.h"
#include "tool_registry.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QDebug>

// ============================================================
//  Запись
// ============================================================

QString actionOutcomeName(ActionOutcome outcome)
{
    switch (outcome) {
    case ActionOutcome::Ok:     return QStringLiteral("ok");
    case ActionOutcome::Failed: return QStringLiteral("failed");
    case ActionOutcome::Denied: return QStringLiteral("denied");
    }
    return QStringLiteral("ok");
}

namespace {

ActionOutcome outcomeFromName(const QString& name)
{
    if (name == QLatin1String("failed")) return ActionOutcome::Failed;
    if (name == QLatin1String("denied")) return ActionOutcome::Denied;
    return ActionOutcome::Ok;
}

// Аргументы в строке журнала — компактно: полный JSON тут читать
// невозможно, а «что именно запускали» видно и по значениям.
QString shortArgs(const QJsonObject& args)
{
    if (args.isEmpty())
        return QString();

    QStringList parts;
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        QString v = it.value().toVariant().toString();
        if (v.length() > 40)
            v = v.left(37) + QStringLiteral("...");
        parts << v;
        if (parts.size() >= 3)
            break;
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

QString ActionRecord::toLine(bool withDate) const
{
    const QString when = withDate ? at.toString(QStringLiteral("dd.MM HH:mm"))
                                  : at.toString(QStringLiteral("HH:mm"));

    QString line = QStringLiteral("#%1  %2  %3  %4(%5)")
                       .arg(id)
                       .arg(when, actor, tool, shortArgs(args));

    if (outcome != ActionOutcome::Ok)
        line += QStringLiteral("  [%1]").arg(actionOutcomeName(outcome));
    if (!display.isEmpty())
        line += QStringLiteral(" — ") + display;
    return line;
}

ActionRecord ActionRecord::fromJson(const QJsonObject& obj)
{
    ActionRecord r;
    r.id      = static_cast<qint64>(obj.value(QStringLiteral("id")).toDouble());
    r.at      = QDateTime::fromString(obj.value(QStringLiteral("at")).toString(), Qt::ISODate);
    r.actor   = obj.value(QStringLiteral("actor")).toString();
    r.tool    = obj.value(QStringLiteral("tool")).toString();
    r.args    = obj.value(QStringLiteral("args")).toObject();
    r.outcome = outcomeFromName(obj.value(QStringLiteral("outcome")).toString());
    r.display = obj.value(QStringLiteral("display")).toString();
    r.risk    = obj.value(QStringLiteral("risk")).toInt(0);
    return r;
}

QJsonObject ActionRecord::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")]      = static_cast<double>(id);
    obj[QStringLiteral("at")]      = at.toString(Qt::ISODate);
    obj[QStringLiteral("actor")]   = actor;
    obj[QStringLiteral("tool")]    = tool;
    if (!args.isEmpty())
        obj[QStringLiteral("args")] = args;
    obj[QStringLiteral("outcome")] = actionOutcomeName(outcome);
    if (!display.isEmpty())
        obj[QStringLiteral("display")] = display;
    if (risk != 0)
        obj[QStringLiteral("risk")] = risk;
    return obj;
}

// ============================================================
//  Актор
// ============================================================

ActionLog::Actor::Actor(const QString& name)
    : m_previous(ActionLog::instance().actor())
{
    ActionLog::instance().setActor(name);
}

ActionLog::Actor::~Actor()
{
    ActionLog::instance().setActor(m_previous);
}

// ============================================================
//  ActionLog
// ============================================================

ActionLog& ActionLog::instance()
{
    static ActionLog log;
    return log;
}

ActionLog::ActionLog()
{
    load();
}

QString ActionLog::storagePath() const
{
    return JarvisPaths::subPath(QStringLiteral("agent/actions.jsonl"));
}

void ActionLog::setActor(const QString& actor)
{
    m_actor = actor.trimmed().isEmpty() ? QStringLiteral("user") : actor.trimmed();
}

void ActionLog::load()
{
    QFile f(storagePath());
    if (!f.exists())
        return;
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[Actions] cannot read" << storagePath() << f.errorString();
        return;
    }

    int lines = 0;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;
        ++lines;

        // Оборванная последняя строка после аварийного завершения —
        // единственная жертва; остальное читается как обычно.
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (!doc.isObject())
            continue;

        const ActionRecord r = ActionRecord::fromJson(doc.object());
        if (r.tool.isEmpty())
            continue;

        m_records.append(r);
        m_nextId = qMax(m_nextId, r.id + 1);
    }
    f.close();

    while (m_records.size() > kMaxMemory)
        m_records.removeFirst();

    // Файл рос всю прошлую сессию — подрезаем один раз при старте,
    // а не после каждой записи.
    if (lines > kMaxFileLines)
        rewriteFile();
}

void ActionLog::appendLine(const ActionRecord& record) const
{
    QFile f(storagePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "[Actions] cannot append to" << storagePath() << f.errorString();
        return;
    }
    f.write(QJsonDocument(record.toJson()).toJson(QJsonDocument::Compact));
    f.write("\n");
    f.close();
}

void ActionLog::rewriteFile() const
{
    QFile f(storagePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "[Actions] cannot rewrite" << storagePath() << f.errorString();
        return;
    }
    for (const ActionRecord& r : m_records) {
        f.write(QJsonDocument(r.toJson()).toJson(QJsonDocument::Compact));
        f.write("\n");
    }
    f.close();
}

qint64 ActionLog::record(const QString& tool,
                         const QJsonObject& args,
                         ActionOutcome outcome,
                         const QString& display,
                         int risk)
{
    if (tool.trimmed().isEmpty())
        return 0;

    ActionRecord r;
    r.id      = m_nextId++;
    r.at      = QDateTime::currentDateTime();
    r.actor   = m_actor;
    r.tool    = tool;
    r.args    = args;
    r.outcome = outcome;
    r.display = QString(display).replace(QChar('\n'), QChar(' ')).left(200).trimmed();
    r.risk    = risk;

    m_records.append(r);
    while (m_records.size() > kMaxMemory)
        m_records.removeFirst();

    appendLine(r);

    emit recorded(r);
    return r.id;
}

QVector<ActionRecord> ActionLog::records(int limit) const
{
    if (limit <= 0 || m_records.size() <= limit)
        return m_records;
    return m_records.mid(m_records.size() - limit);
}

QVector<ActionRecord> ActionLog::since(const QDateTime& from) const
{
    QVector<ActionRecord> out;
    for (int i = m_records.size() - 1; i >= 0; --i) {
        if (m_records[i].at < from)
            break;
        out.prepend(m_records[i]);
    }
    return out;
}

const ActionRecord* ActionLog::find(qint64 id) const
{
    for (int i = m_records.size() - 1; i >= 0; --i) {
        if (m_records[i].id == id)
            return &m_records[i];
    }
    return nullptr;
}

const ActionRecord* ActionLog::lastRepeatable() const
{
    for (int i = m_records.size() - 1; i >= 0; --i) {
        const ActionRecord& r = m_records[i];
        // Повторять неудачу или сам повтор бессмысленно: в первом случае
        // повторится ошибка, во втором — рекурсия по журналу.
        if (r.outcome != ActionOutcome::Ok)
            continue;
        if (r.tool == QLatin1String("repeat_action") || r.tool == QLatin1String("list_actions"))
            continue;
        return &r;
    }
    return nullptr;
}

void ActionLog::clear()
{
    m_records.clear();
    rewriteFile();
}

// ============================================================
//  Инструменты
// ============================================================

namespace JarvisTools {

void registerActionTools(ToolRegistry& reg, PermissionGate* gate)
{
    {
        ToolSpec t;
        t.name        = QStringLiteral("list_actions");
        t.category    = QStringLiteral("actions");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "What JARVIS actually DID: every tool call, who started it (the user, "
            "you, a workflow or a trigger) and how it ended. Use it for \"what have "
            "you been doing\", \"what did you change\", \"what ran while I was away\", "
            "or to find the id of an action the user wants repeated. This is a record "
            "of actions - list_events is a record of things that happened by themselves.");
        t.schema = ToolSchema()
                       .integer("minutes", "Only actions from the last N minutes", false)
                       .integer("limit", "How many to return (default 25)", false)
                       .boolean("only_failed", "Failed and denied actions only", false)
                       .str("actor", "Filter by actor: user, agent, workflow:Name, trigger:Name", false)
                       .build();
        t.handler = [](const QJsonObject& a) -> ToolResult {
            const int minutes = a.value(QStringLiteral("minutes")).toInt(0);
            const int limit   = qBound(1, a.value(QStringLiteral("limit")).toInt(25), 200);
            const bool onlyFailed = a.value(QStringLiteral("only_failed")).toBool(false);
            const QString actor = a.value(QStringLiteral("actor")).toString().trimmed();

            const ActionLog& log = ActionLog::instance();
            QVector<ActionRecord> all = minutes > 0
                ? log.since(QDateTime::currentDateTime().addSecs(-60ll * minutes))
                : log.records();

            const bool withDate = minutes <= 0;
            QStringList lines;
            for (int i = all.size() - 1; i >= 0 && lines.size() < limit; --i) {
                const ActionRecord& r = all[i];
                if (onlyFailed && r.outcome == ActionOutcome::Ok)
                    continue;
                if (!actor.isEmpty() && r.actor.compare(actor, Qt::CaseInsensitive) != 0)
                    continue;
                lines.prepend(r.toLine(withDate));
            }

            if (lines.isEmpty())
                return ToolResult::success(QStringLiteral("No actions recorded for that filter."),
                                           QStringLiteral("Действий нет"));
            return ToolResult::success(lines.join(QChar('\n')),
                                       QStringLiteral("Действий: %1").arg(lines.size()));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("repeat_action");
        t.category    = QStringLiteral("actions");
        // Риск самого повтора неизвестен заранее — он равен риску того,
        // что повторяют, поэтому гейт спрашивается по целевому инструменту
        // внутри обработчика, а не по этой записи.
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Run a previously logged action again, exactly as it ran before. Pass the "
            "id from list_actions, or omit it to repeat the last successful action. "
            "The repeated action goes through the normal permission check again.");
        t.schema = ToolSchema()
                       .integer("id", "Action id from list_actions; omit for the last one", false)
                       .build();
        t.preview = [](const QJsonObject& a) {
            const int id = a.value(QStringLiteral("id")).toInt(0);
            const ActionRecord* r = id > 0 ? ActionLog::instance().find(id)
                                           : ActionLog::instance().lastRepeatable();
            return r ? QStringLiteral("Повторить: %1").arg(r->toLine())
                     : QStringLiteral("Повторить последнее действие");
        };
        t.handler = [&reg, gate](const QJsonObject& a) -> ToolResult {
            ActionLog& log = ActionLog::instance();

            const int id = a.value(QStringLiteral("id")).toInt(0);
            const ActionRecord* found = id > 0 ? log.find(id) : log.lastRepeatable();
            if (!found) {
                return ToolResult::failure(
                    id > 0 ? QStringLiteral("No action with id %1 in the log.").arg(id)
                           : QStringLiteral("Nothing repeatable in the log yet."));
            }

            // Копия: запись может уехать из буфера, пока ждём подтверждения.
            const ActionRecord rec = *found;

            const ToolSpec* spec = reg.find(rec.tool);
            if (!spec)
                return ToolResult::failure(
                    QStringLiteral("Tool '%1' no longer exists.").arg(rec.tool));

            if (!gate)
                return ToolResult::failure(QStringLiteral("Permissions are not wired up."));

            QString reason;
            if (!gate->evaluateBlocking(rec.tool, spec->risk,
                                        reg.describeCall(rec.tool, rec.args), &reason)) {
                log.record(rec.tool, rec.args, ActionOutcome::Denied,
                           reason, static_cast<int>(spec->risk));
                return ToolResult::failure(reason.isEmpty()
                                               ? QStringLiteral("Denied by user.")
                                               : reason);
            }

            ActionLog::Actor scope(QStringLiteral("repeat"));
            const ToolResult res = reg.invoke(rec.tool, rec.args);
            return res.ok
                ? ToolResult::success(res.text,
                                      QStringLiteral("Повтор: %1").arg(res.display))
                : ToolResult::failure(res.text);
        };
        reg.registerTool(t);
    }
}

} // namespace JarvisTools
