#pragma once
// -------------------------------------------------------
// workflow_manager.h — Именованные цепочки действий
//
// Агент каждый раз выбирает инструменты заново — это гибко,
// но недетерминированно и стоит запроса к модели. Workflow —
// это записанная один раз последовательность вызовов:
//
//     Development
//       1. Запустить Rider
//       2. Открыть проект
//       3. Открыть документацию
//       4. Поставить громкость 30%
//
// Запускается мгновенно, без модели, и всегда одинаково.
// Шаги — это ТОЛЬКО вызовы инструментов из ToolRegistry:
// «сообразить на месте» умеет агент, а workflow нужен ровно
// для обратного — повторяемости.
//
// Разрешения не обходятся: каждый шаг проходит через
// PermissionGate так же, как если бы его вызвала модель.
// -------------------------------------------------------

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class ToolRegistry;
class PermissionGate;

// ============================================================
//  Шаг
// ============================================================
struct WorkflowStep
{
    QString     title;      // "Запустить Rider" — для UI; пусто = соберём сами
    QString     tool;       // имя инструмента из ToolRegistry
    QJsonObject args;
    bool        continueOnError = false;   // не обрывать цепочку на этом шаге

    bool isValid() const { return !tool.isEmpty(); }

    static WorkflowStep fromJson(const QJsonObject& obj);
    QJsonObject         toJson() const;
};

// ============================================================
//  Workflow
// ============================================================
struct Workflow
{
    QString              name;          // "Development" — ключ, регистр не важен
    QString              description;
    QString              icon;          // эмодзи для списка
    QVector<WorkflowStep> steps;

    bool isValid() const { return !name.isEmpty() && !steps.isEmpty(); }

    static Workflow fromJson(const QJsonObject& obj);
    QJsonObject     toJson() const;
};

// ============================================================
//  WorkflowManager
// ============================================================
class WorkflowManager : public QObject
{
    Q_OBJECT

public:
    WorkflowManager(ToolRegistry* tools, PermissionGate* gate, QObject* parent = nullptr);

    // --- Хранилище (JSON в AppData) ---
    void load();
    bool save() const;
    QString storagePath() const;

    QVector<Workflow> all() const { return m_workflows; }
    const Workflow*   find(const QString& name) const;   // без учёта регистра
    QStringList       names() const;
    int               count() const { return m_workflows.size(); }

    bool addOrReplace(const Workflow& wf);   // сразу сохраняет
    bool remove(const QString& name);

    // --- Выполнение ---
    // Синхронно: ждать нечего, кроме диалогов подтверждения, а они
    // крутят собственный вложенный цикл событий. Возвращает отчёт —
    // тот же текст, что уходит модели.
    QString run(const QString& name, bool* okOut = nullptr);
    bool    isRunning() const { return m_running; }

    // Компактный список для системного промпта и list_workflows
    QString summaryForModel() const;

signals:
    void listChanged();
    void workflowStarted(const QString& name, int stepCount);
    void stepStarted(const QString& name, int index, const QString& title);
    void stepFinished(const QString& name, int index, bool ok, const QString& summary);
    void workflowFinished(const QString& name, bool ok, const QString& report);

private:
    void seedDefaults();
    QString describeStep(const WorkflowStep& step) const;

    ToolRegistry*     m_tools = nullptr;
    PermissionGate*   m_gate  = nullptr;
    QVector<Workflow> m_workflows;
    bool              m_running = false;
};

namespace JarvisTools {

// list_workflows / run_workflow / save_workflow — чтобы цепочки можно
// было и запускать голосом, и создавать словами («запомни это как
// workflow Gaming»).
void registerWorkflowTools(ToolRegistry& registry, WorkflowManager* workflows);

} // namespace JarvisTools
