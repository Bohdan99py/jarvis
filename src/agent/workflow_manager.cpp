// -------------------------------------------------------
// workflow_manager.cpp — см. workflow_manager.h
// -------------------------------------------------------

#include "workflow_manager.h"

#include "action_log.h"
#include "edit_journal.h"
#include "permission_gate.h"
#include "tool_registry.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDebug>

// ============================================================
//  Сериализация
// ============================================================

WorkflowStep WorkflowStep::fromJson(const QJsonObject& obj)
{
    WorkflowStep step;
    step.title = obj.value(QStringLiteral("title")).toString();
    step.tool  = obj.value(QStringLiteral("tool")).toString();
    step.args  = obj.value(QStringLiteral("args")).toObject();
    step.continueOnError = obj.value(QStringLiteral("continue_on_error")).toBool(false);
    return step;
}

QJsonObject WorkflowStep::toJson() const
{
    QJsonObject obj;
    if (!title.isEmpty())
        obj[QStringLiteral("title")] = title;
    obj[QStringLiteral("tool")] = tool;
    obj[QStringLiteral("args")] = args;
    if (continueOnError)
        obj[QStringLiteral("continue_on_error")] = true;
    return obj;
}

Workflow Workflow::fromJson(const QJsonObject& obj)
{
    Workflow wf;
    wf.name        = obj.value(QStringLiteral("name")).toString();
    wf.description = obj.value(QStringLiteral("description")).toString();
    wf.icon        = obj.value(QStringLiteral("icon")).toString();

    const QJsonArray steps = obj.value(QStringLiteral("steps")).toArray();
    for (const QJsonValue& v : steps) {
        const WorkflowStep step = WorkflowStep::fromJson(v.toObject());
        if (step.isValid())
            wf.steps.append(step);
    }
    return wf;
}

QJsonObject Workflow::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("name")]        = name;
    obj[QStringLiteral("description")] = description;
    obj[QStringLiteral("icon")]        = icon;

    QJsonArray arr;
    for (const WorkflowStep& s : steps)
        arr.append(s.toJson());
    obj[QStringLiteral("steps")] = arr;
    return obj;
}

// ============================================================
//  WorkflowManager
// ============================================================

WorkflowManager::WorkflowManager(ToolRegistry* tools, PermissionGate* gate, QObject* parent)
    : QObject(parent)
    , m_tools(tools)
    , m_gate(gate)
{
    load();
}

QString WorkflowManager::storagePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/workflows.json");
}

void WorkflowManager::load()
{
    m_workflows.clear();

    QFile f(storagePath());
    if (!f.exists()) {
        seedDefaults();
        save();
        emit listChanged();
        return;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[Workflows] cannot read" << storagePath() << f.errorString();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    // Файл лежит в AppData и правится руками — битый JSON не должен
    // стирать всё остальное, поэтому просто пропускаем непригодное.
    const QJsonArray arr = doc.isArray() ? doc.array()
                                         : doc.object().value(QStringLiteral("workflows")).toArray();
    for (const QJsonValue& v : arr) {
        const Workflow wf = Workflow::fromJson(v.toObject());
        if (wf.isValid())
            m_workflows.append(wf);
        else
            qWarning() << "[Workflows] skipped a malformed entry";
    }

    emit listChanged();
}

bool WorkflowManager::save() const
{
    QJsonArray arr;
    for (const Workflow& wf : m_workflows)
        arr.append(wf.toJson());

    QFile f(storagePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[Workflows] cannot write" << storagePath() << f.errorString();
        return false;
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

void WorkflowManager::seedDefaults()
{
    // Один пример, из которого понятно устройство. Наполнять список
    // догадками о том, какие у человека установлены программы, смысла
    // нет — остальные цепочки он создаст словами (save_workflow).
    Workflow diag;
    diag.name        = QStringLiteral("Diagnostics");
    diag.icon        = QStringLiteral("🩺");
    diag.description = QStringLiteral("Состояние машины: нагрузка, память, диски, "
                                      "самые тяжёлые процессы");

    WorkflowStep status;
    status.title = QStringLiteral("Состояние системы");
    status.tool  = QStringLiteral("system_status");
    diag.steps.append(status);

    WorkflowStep procs;
    procs.title = QStringLiteral("Топ процессов");
    procs.tool  = QStringLiteral("list_processes");
    procs.args  = QJsonObject{ { QStringLiteral("top"), 10 } };
    diag.steps.append(procs);

    m_workflows.append(diag);
}

const Workflow* WorkflowManager::find(const QString& name) const
{
    for (const Workflow& wf : m_workflows) {
        if (wf.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return &wf;
    }
    return nullptr;
}

QStringList WorkflowManager::names() const
{
    QStringList out;
    out.reserve(m_workflows.size());
    for (const Workflow& wf : m_workflows)
        out << wf.name;
    return out;
}

bool WorkflowManager::addOrReplace(const Workflow& wf)
{
    if (!wf.isValid())
        return false;

    for (int i = 0; i < m_workflows.size(); ++i) {
        if (m_workflows[i].name.compare(wf.name, Qt::CaseInsensitive) == 0) {
            m_workflows[i] = wf;
            const bool ok = save();
            emit listChanged();
            return ok;
        }
    }
    m_workflows.append(wf);
    const bool ok = save();
    emit listChanged();
    return ok;
}

bool WorkflowManager::remove(const QString& name)
{
    for (int i = 0; i < m_workflows.size(); ++i) {
        if (m_workflows[i].name.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
            m_workflows.remove(i);
            const bool ok = save();
            emit listChanged();
            return ok;
        }
    }
    return false;
}

QString WorkflowManager::describeStep(const WorkflowStep& step) const
{
    if (!step.title.isEmpty())
        return step.title;
    return m_tools ? m_tools->describeCall(step.tool, step.args) : step.tool;
}

// ============================================================
//  Выполнение
// ============================================================

QString WorkflowManager::run(const QString& name, bool* okOut)
{
    if (okOut)
        *okOut = false;

    if (!m_tools || !m_gate)
        return QStringLiteral("Workflows are not wired up.");

    if (m_running)
        return QStringLiteral("Another workflow is already running.");

    const Workflow* found = find(name);
    if (!found) {
        return QStringLiteral("No workflow named '%1'. Available: %2")
            .arg(name, names().isEmpty() ? QStringLiteral("(none)")
                                         : names().join(QStringLiteral(", ")));
    }

    // Копия: список может быть перезаписан из UI прямо во время прогона,
    // а итератор по m_workflows этого не переживёт.
    const Workflow wf = *found;

    m_running = true;
    emit workflowStarted(wf.name, wf.steps.size());

    // Шаги попадут в журнал действий не как «пользователь сделал», а
    // как «сценарий сделал» — иначе через день не отличить одно от другого.
    ActionLog::Actor scope(QStringLiteral("workflow:") + wf.name);

    // И один батч правок на весь сценарий: отменять половину цепочки
    // бессмысленно — она и задумана как одно действие.
    EditJournal::Scope edits(QStringLiteral("сценарий \"") + wf.name + QChar('"'));

    QStringList report;
    bool allOk = true;

    for (int i = 0; i < wf.steps.size(); ++i) {
        const WorkflowStep& step = wf.steps[i];
        const QString title = describeStep(step);

        const ToolSpec* spec = m_tools->find(step.tool);
        if (!spec) {
            const QString line = QStringLiteral("%1. %2 — unknown tool '%3'")
                                     .arg(i + 1).arg(title, step.tool);
            report << line;
            emit stepFinished(wf.name, i, false, line);
            if (!step.continueOnError) { allOk = false; break; }
            allOk = false;
            continue;
        }

        emit stepStarted(wf.name, i, title);

        // Разрешение — тем же гейтом, что и у модели.
        QString reason;
        const bool allowed = m_gate->evaluateBlocking(
            step.tool, spec->risk, m_tools->describeCall(step.tool, step.args), &reason);

        if (!allowed) {
            const QString line = QStringLiteral("%1. %2 — declined").arg(i + 1).arg(title);
            report << line;
            // Отказ — тоже часть истории: «почему сценарий не доделался»
            // иначе восстанавливается только по памяти.
            ActionLog::instance().record(step.tool, step.args, ActionOutcome::Denied,
                                         reason, static_cast<int>(spec->risk));
            emit stepFinished(wf.name, i, false, reason.isEmpty() ? line : reason);
            // Отказ пользователя обрывает цепочку всегда: продолжать
            // сценарий, из которого выкинули шаг, — почти наверняка не то,
            // чего от нас хотели.
            allOk = false;
            break;
        }

        const ToolResult res = m_tools->invoke(step.tool, step.args);
        const QString line = QStringLiteral("%1. %2 — %3")
                                 .arg(i + 1)
                                 .arg(title,
                                      res.ok ? (res.display.isEmpty() ? QStringLiteral("ok")
                                                                      : res.display)
                                             : res.text);
        report << line;
        emit stepFinished(wf.name, i, res.ok, res.display.isEmpty() ? title : res.display);

        if (!res.ok) {
            allOk = false;
            if (!step.continueOnError)
                break;
        }
    }

    m_running = false;

    const QString text = QStringLiteral("Workflow \"%1\":\n").arg(wf.name)
                         + report.join(QChar('\n'));
    emit workflowFinished(wf.name, allOk, text);

    if (okOut)
        *okOut = allOk;
    return text;
}

QString WorkflowManager::summaryForModel() const
{
    if (m_workflows.isEmpty())
        return QStringLiteral("No workflows saved yet.");

    QStringList lines;
    for (const Workflow& wf : m_workflows) {
        QStringList stepNames;
        for (const WorkflowStep& s : wf.steps)
            stepNames << (s.title.isEmpty() ? s.tool : s.title);
        lines << QStringLiteral("%1%2 — %3\n    steps: %4")
                     .arg(wf.icon.isEmpty() ? QString() : wf.icon + QChar(' '),
                          wf.name,
                          wf.description.isEmpty() ? QStringLiteral("(no description)")
                                                   : wf.description,
                          stepNames.join(QStringLiteral(" -> ")));
    }
    return lines.join(QChar('\n'));
}

// ============================================================
//  Инструменты
// ============================================================

namespace JarvisTools {

void registerWorkflowTools(ToolRegistry& reg, WorkflowManager* wm)
{
    if (!wm) {
        qWarning() << "[Tools] registerWorkflowTools: manager is null";
        return;
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("list_workflows");
        t.category    = QStringLiteral("workflows");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "List the saved workflows - named, repeatable chains of tool calls "
            "(\"Development\", \"Gaming\") with their steps.");
        t.schema  = ToolSchema::empty();
        t.handler = [wm](const QJsonObject&) -> ToolResult {
            return ToolResult::success(wm->summaryForModel(),
                                       QStringLiteral("Сценариев: %1").arg(wm->count()));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("run_workflow");
        t.category    = QStringLiteral("workflows");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Run a saved workflow by name. Every step still goes through the normal "
            "permission checks, so a dangerous step inside it will still ask the user.");
        t.schema  = ToolSchema().str("name", "Workflow name").build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Запустить сценарий \"%1\"")
                .arg(a.value(QStringLiteral("name")).toString());
        };
        t.handler = [wm](const QJsonObject& a) -> ToolResult {
            const QString name = a.value(QStringLiteral("name")).toString().trimmed();
            bool ok = false;
            const QString report = wm->run(name, &ok);
            return ok ? ToolResult::success(report,
                                            QStringLiteral("Сценарий \"%1\" выполнен").arg(name))
                      : ToolResult::failure(report);
        };
        reg.registerTool(t);
    }

    {
        // Схема шага описывается вручную: это массив объектов, а простые
        // str/integer/boolean такого не выражают.
        QJsonObject stepProps;
        stepProps[QStringLiteral("tool")] = QJsonObject{
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("Name of an existing tool") }
        };
        stepProps[QStringLiteral("args")] = QJsonObject{
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("description"), QStringLiteral("Arguments for that tool") }
        };
        stepProps[QStringLiteral("title")] = QJsonObject{
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"),
              QStringLiteral("Short human label shown while the step runs") }
        };
        stepProps[QStringLiteral("continue_on_error")] = QJsonObject{
            { QStringLiteral("type"), QStringLiteral("boolean") },
            { QStringLiteral("description"),
              QStringLiteral("Keep going if this step fails") }
        };

        QJsonObject stepSchema;
        stepSchema[QStringLiteral("type")]       = QStringLiteral("object");
        stepSchema[QStringLiteral("properties")] = stepProps;
        stepSchema[QStringLiteral("required")]   = QJsonArray{ QStringLiteral("tool") };

        QJsonObject stepsArray;
        stepsArray[QStringLiteral("type")]        = QStringLiteral("array");
        stepsArray[QStringLiteral("items")]       = stepSchema;
        stepsArray[QStringLiteral("description")] = QStringLiteral(
            "Ordered list of tool calls to perform");

        ToolSpec t;
        t.name        = QStringLiteral("save_workflow");
        t.category    = QStringLiteral("workflows");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Create or replace a named workflow so the same chain can be replayed "
            "later without asking the model again. Use it when the user says "
            "something like \"remember this as my Gaming setup\".");
        t.schema = ToolSchema()
                       .str("name", "Workflow name, e.g. Development")
                       .str("description", "One line describing what it does", false)
                       .str("icon", "Optional emoji for the list", false)
                       .raw("steps", stepsArray)
                       .build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Сохранить сценарий \"%1\" (%2 шагов)")
                .arg(a.value(QStringLiteral("name")).toString())
                .arg(a.value(QStringLiteral("steps")).toArray().size());
        };
        t.handler = [wm, &reg](const QJsonObject& a) -> ToolResult {
            Workflow wf;
            wf.name        = a.value(QStringLiteral("name")).toString().trimmed();
            wf.description = a.value(QStringLiteral("description")).toString();
            wf.icon        = a.value(QStringLiteral("icon")).toString();

            const QJsonArray steps = a.value(QStringLiteral("steps")).toArray();
            QStringList unknown;
            for (const QJsonValue& v : steps) {
                const WorkflowStep step = WorkflowStep::fromJson(v.toObject());
                if (!step.isValid())
                    continue;
                // Ссылку на несуществующий инструмент ловим сейчас, а не
                // при запуске: иначе сценарий молча сломается через неделю.
                if (!reg.contains(step.tool)) {
                    unknown << step.tool;
                    continue;
                }
                wf.steps.append(step);
            }

            if (!unknown.isEmpty())
                return ToolResult::failure(
                    QStringLiteral("Unknown tools in steps: %1. Available: %2")
                        .arg(unknown.join(QStringLiteral(", ")),
                             reg.names().join(QStringLiteral(", "))));

            if (!wf.isValid())
                return ToolResult::failure(
                    QStringLiteral("A workflow needs a name and at least one valid step."));

            if (!wm->addOrReplace(wf))
                return ToolResult::failure(QStringLiteral("Could not save the workflow."));

            return ToolResult::success(
                QStringLiteral("Saved workflow '%1' with %2 steps.")
                    .arg(wf.name).arg(wf.steps.size()),
                QStringLiteral("Сценарий \"%1\" сохранён").arg(wf.name));
        };
        reg.registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("delete_workflow");
        t.category    = QStringLiteral("workflows");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral("Delete a saved workflow by name.");
        t.schema  = ToolSchema().str("name", "Workflow name").build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("Удалить сценарий \"%1\"")
                .arg(a.value(QStringLiteral("name")).toString());
        };
        t.handler = [wm](const QJsonObject& a) -> ToolResult {
            const QString name = a.value(QStringLiteral("name")).toString().trimmed();
            if (wm->remove(name))
                return ToolResult::success(QStringLiteral("Deleted workflow '%1'").arg(name),
                                           QStringLiteral("Сценарий удалён"));
            return ToolResult::failure(QStringLiteral("No workflow named '%1'").arg(name));
        };
        reg.registerTool(t);
    }
}

} // namespace JarvisTools
