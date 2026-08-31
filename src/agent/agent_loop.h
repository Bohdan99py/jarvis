#pragma once
// -------------------------------------------------------
// agent_loop.h — Цикл "думает → делает → смотрит результат"
//
// Это то, что превращает ответ "Конечно, открываю Unreal!"
// в реально открытый Unreal.
//
//   run("подготовь окружение для Unreal")
//        │
//        ├─ модель просит launch_app("UnrealEditor")
//        ├─ PermissionGate: Safe → выполняем
//        ├─ отдаём результат модели
//        ├─ модель просит launch_app("Rider"), open_url(docs)…
//        └─ модель отвечает текстом → finished()
//
// Один цикл за раз. Всё синхронно по шагам: подтверждение
// пользователя асинхронно, поэтому инструменты выполняются
// строго по одному.
// -------------------------------------------------------

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "permission_gate.h"
#include "tool_registry.h"

class ClaudeApi;

class AgentLoop : public QObject
{
    Q_OBJECT

public:
    AgentLoop(ClaudeApi* api,
              ToolRegistry* tools,
              PermissionGate* gate,
              QObject* parent = nullptr);

    bool isRunning() const { return m_running; }

    // systemPrompt пустой => системный промпт из SessionMemory + правила агента
    void run(const QString& userMessage, const QString& systemPrompt = QString());
    void cancel();

    void setMaxIterations(int n) { m_maxIterations = qBound(1, n, 25); }
    int  maxIterations() const   { return m_maxIterations; }

    // Список выполненных действий последнего запуска — для отчёта в UI
    QStringList transcript() const { return m_transcript; }

    // Блок правил, который дописывается к системному промпту.
    // Публичный: тот же текст нужен и вне цикла (например, чтобы
    // объяснить модели в обычном чате, что она умеет действовать).
    static QString agentSystemRules();

signals:
    void started();
    void thinking(int iteration);                       // ждём модель
    void toolStarted(const QString& name, const QString& summary);
    void toolFinished(const QString& name, bool ok, const QString& summary);
    void toolDenied(const QString& name, const QString& reason);
    void narration(const QString& text);                // текст модели между действиями
    void finished(const QString& finalText);
    void failed(const QString& error);

private:
    struct PendingCall {
        QString     id;
        QString     name;
        QJsonObject input;
    };

    void requestModel();
    void handleModelResponse(const QJsonObject& root);
    void runNextTool();
    void appendToolResult(const QString& toolUseId, const QString& text, bool isError);
    void submitToolResults();
    void stop(const QString& finalText, const QString& error);

    ClaudeApi*      m_api   = nullptr;
    ToolRegistry*   m_tools = nullptr;
    PermissionGate* m_gate  = nullptr;

    QJsonArray  m_messages;      // весь диалог этого запуска
    QJsonArray  m_toolResults;   // блоки tool_result текущего шага
    QVector<PendingCall> m_queue;

    QString     m_systemPrompt;
    QString     m_lastText;
    QStringList m_transcript;

    bool    m_running       = false;
    int     m_iteration     = 0;
    int     m_maxIterations = 8;
    quint64 m_runId         = 0;   // защита от «хвостов» отменённого запуска
};
