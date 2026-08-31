// -------------------------------------------------------
// agent_loop.cpp — см. agent_loop.h
// -------------------------------------------------------

#include "agent_loop.h"
#include "action_log.h"
#include "claude_api.h"
#include "edit_journal.h"
#include "jarvis_state.h"

#include <QDebug>
#include <QPointer>

AgentLoop::AgentLoop(ClaudeApi* api,
                     ToolRegistry* tools,
                     PermissionGate* gate,
                     QObject* parent)
    : QObject(parent)
    , m_api(api)
    , m_tools(tools)
    , m_gate(gate)
{
}

QString AgentLoop::agentSystemRules()
{
    return QStringLiteral(
        "\n\n=== ACTING ON THIS MACHINE ===\n"
        "You are not a chat window. You control this Windows PC through tools.\n"
        "\n"
        "Rules:\n"
        "1. If the request can be fulfilled by acting, ACT - do not describe what "
        "the user could do themselves. \"Open X\" means call the tool, not explain how.\n"
        "2. Chain tools when a request needs several steps (find the project, launch "
        "the editor, open the docs). Check each result before moving on.\n"
        "3. Before destructive or irreversible steps, use a read-only tool first "
        "(list, read, find) so you act on facts, not guesses.\n"
        "4. Tools marked as requiring confirmation may be declined by the user. "
        "If declined, stop that branch and say so plainly - never retry it silently "
        "and never route around it with a shell command.\n"
        "5. Never call run_command for something a dedicated tool already does.\n"
        "6. When done, answer in ONE short paragraph: what you actually did and the "
        "resulting state. No step-by-step retelling - the UI already showed every step.\n"
        "7. If nothing needs doing on the machine, just answer normally.\n"
        "8. Never answer \"what did you do\", \"what ran while I was away\" or \"what "
        "changed in the project\" from memory of this conversation - call list_actions "
        "or git_log. Your memory of your own actions is not evidence, and actions "
        "taken by triggers happened without you at all.\n"
        "9. Write a commit message from the actual diff (git_diff), never from what "
        "you assume the user changed.\n"
        "10. When the user describes something that should happen automatically from "
        "now on (\"every time I plug in the ESP32\", \"at eight in the morning\", "
        "\"when the build finishes\"), do not promise to remember it - save_trigger, "
        "then say in one line what will fire and when.\n"
        "11. Some tool results end with a [verification: ...] line. That is the machine "
        "checking what actually happened afterwards, not the tool repeating itself:\n"
        "   - confirmed: the effect is real, move on and do not re-check it.\n"
        "   - partial: it half worked. Say so and deal with the remainder - never "
        "report the whole step as done.\n"
        "   - contradicted: it did NOT happen despite the tool claiming success. Do not "
        "repeat the identical call; find out why (list_processes, list_windows, "
        "read_file) or try a different route.\n"
        "   No line at all means nothing was checked - so do not claim it was verified.\n"
        "Answer in the user's language.\n");
}

// ============================================================
//  Запуск
// ============================================================

void AgentLoop::run(const QString& userMessage, const QString& systemPrompt)
{
    if (m_running) {
        emit failed(QStringLiteral("Agent is already running."));
        return;
    }
    if (!m_api || !m_tools || !m_gate) {
        emit failed(QStringLiteral("Agent is not wired up."));
        return;
    }

    m_running     = true;
    m_iteration   = 0;
    m_runId++;

    // Один запуск агента — один батч журнала: «отмени» возвращает всё,
    // что модель сделала одним ответом, а не последний из пяти файлов.
    // Инструменты открывают собственные вложенные батчи — они вольются
    // в этот. Закрывается в stop() и cancel().
    EditJournal::instance().beginBatch(QStringLiteral("действия ассистента"));
    m_messages    = QJsonArray();
    m_toolResults = QJsonArray();
    m_queue.clear();
    m_transcript.clear();
    m_lastText.clear();
    m_systemPrompt = systemPrompt;

    JarvisState::instance().enter(JarvisPhase::Thinking);

    QJsonObject first;
    first[QStringLiteral("role")]    = QStringLiteral("user");
    first[QStringLiteral("content")] = userMessage;
    m_messages.append(first);

    emit started();
    requestModel();
}

void AgentLoop::cancel()
{
    if (!m_running)
        return;
    m_runId++;            // все висящие колбэки станут чужими
    m_running = false;
    m_queue.clear();
    JarvisState::instance().toIdle();
    // Отмена не проходит через stop(), а батч закрыть надо — иначе
    // следующий запуск вложится в незакрытый и «отмени» будет
    // откатывать два ответа сразу.
    EditJournal::instance().endBatch();
    emit failed(QStringLiteral("Cancelled."));
}

// ============================================================
//  Шаг: спросить модель
// ============================================================

void AgentLoop::requestModel()
{
    if (!m_running)
        return;

    ++m_iteration;
    if (m_iteration > m_maxIterations) {
        // Не зацикливаемся: отдаём то, что успели сделать.
        stop(m_lastText.isEmpty()
                 ? QStringLiteral("Reached the step limit (%1) before finishing.")
                       .arg(m_maxIterations)
                 : m_lastText,
             QString());
        return;
    }

    emit thinking(m_iteration);
    // Без подписи и намеренно тем же вызовом, что в run(): повторный
    // вход в ту же фазу ничего не сбрасывает, поэтому секундомер
    // отсчитывает всё ожидание модели целиком, а не последний из
    // восьми запросов, слитых в одно «думаю».
    JarvisState::instance().enter(JarvisPhase::Thinking);

    const quint64 runId = m_runId;
    QPointer<AgentLoop> self(this);

    m_api->sendConversation(
        m_messages,
        m_tools->toAnthropicJson(),
        m_systemPrompt,
        [self, runId](bool ok, const QJsonObject& root, const QString& error) {
            if (!self || self->m_runId != runId || !self->m_running)
                return;   // отменили или начали новый запуск
            if (!ok) {
                self->stop(QString(), error);
                return;
            }
            self->handleModelResponse(root);
        });
}

// ============================================================
//  Разбор ответа модели
// ============================================================

void AgentLoop::handleModelResponse(const QJsonObject& root)
{
    const QJsonArray content = root[QStringLiteral("content")].toArray();

    // Ответ ассистента кладём в диалог ДОСЛОВНО: id блоков tool_use
    // должны совпасть с tool_result на следующем шаге.
    QJsonObject assistantMsg;
    assistantMsg[QStringLiteral("role")]    = QStringLiteral("assistant");
    assistantMsg[QStringLiteral("content")] = content;
    m_messages.append(assistantMsg);

    QString text;
    m_queue.clear();

    for (const QJsonValue& v : content) {
        const QJsonObject block = v.toObject();
        const QString type = block[QStringLiteral("type")].toString();

        if (type == QStringLiteral("text")) {
            text += block[QStringLiteral("text")].toString();
        } else if (type == QStringLiteral("tool_use")) {
            PendingCall call;
            call.id    = block[QStringLiteral("id")].toString();
            call.name  = block[QStringLiteral("name")].toString();
            call.input = block[QStringLiteral("input")].toObject();
            m_queue.append(call);
        }
    }

    if (!text.trimmed().isEmpty()) {
        m_lastText = text.trimmed();
        if (!m_queue.isEmpty())
            emit narration(m_lastText);   // "сейчас открою..." перед действиями
    }

    if (m_queue.isEmpty()) {
        stop(m_lastText, QString());
        return;
    }

    m_toolResults = QJsonArray();
    runNextTool();
}

// ============================================================
//  Выполнение инструментов — строго по одному
// ============================================================

void AgentLoop::runNextTool()
{
    if (!m_running)
        return;

    if (m_queue.isEmpty()) {
        submitToolResults();
        return;
    }

    const PendingCall call = m_queue.takeFirst();

    const ToolSpec* spec = m_tools->find(call.name);
    if (!spec) {
        const QString msg = QStringLiteral("Unknown tool '%1'").arg(call.name);
        emit toolFinished(call.name, false, msg);
        appendToolResult(call.id, msg, true);
        runNextTool();
        return;
    }

    const QString summary = m_tools->describeCall(call.name, call.input);

    const quint64 runId = m_runId;
    const int     risk  = static_cast<int>(spec->risk);
    QPointer<AgentLoop> self(this);

    // Ожидание человека — это не «выполняется»: пока висит диалог
    // подтверждения, работает не машина, а очередь к ней. Иначе
    // «выполняю 4 минуты» означало бы, что человек четыре минуты
    // не смотрел на экран.
    if (m_gate->needsConfirmation(spec->risk, call.name))
        JarvisState::instance().enter(JarvisPhase::Waiting,
                                      QStringLiteral("Жду подтверждения: ") + summary);

    m_gate->evaluate(call.name, spec->risk, summary,
                     [self, runId, call, summary, risk](bool allowed, const QString& reason) {
        if (!self || self->m_runId != runId || !self->m_running)
            return;

        if (!allowed) {
            self->m_transcript << QStringLiteral("x ") + summary;
            ActionLog::instance().record(call.name, call.input, ActionOutcome::Denied,
                                         reason, risk);
            emit self->toolDenied(call.name, reason);
            // Отказ возвращается модели текстом — она должна сказать об
            // этом пользователю, а не искать обход.
            self->appendToolResult(call.id,
                                   reason.isEmpty() ? QStringLiteral("Denied by user.")
                                                    : reason,
                                   true);
            self->runNextTool();
            return;
        }

        emit self->toolStarted(call.name, summary);
        JarvisState& state = JarvisState::instance();
        state.enter(JarvisPhase::Executing, summary);
        state.noteToolRun();

        // Инициатор для журнала: это решение модели, а не команда человека.
        ActionLog::Actor scope(QStringLiteral("agent"));

        // Внутри invoke() состояние уходит в Verifying, если у
        // инструмента есть проверка постусловия.
        const ToolResult res = self->m_tools->invoke(call.name, call.input);
        const QString line = res.display.isEmpty() ? summary : res.display;

        self->m_transcript << (res.ok ? QStringLiteral("+ ") : QStringLiteral("x ")) + line;
        emit self->toolFinished(call.name, res.ok, line);

        self->appendToolResult(call.id, res.text, !res.ok);
        self->runNextTool();
    });
}

void AgentLoop::appendToolResult(const QString& toolUseId, const QString& text, bool isError)
{
    // 8000 символов — потолок на один результат: листинг папки или
    // вывод сборки легко занимает больше, чем весь остальной контекст.
    QString payload = text;
    if (payload.length() > 8000)
        payload = payload.left(8000) + QStringLiteral("\n... [truncated]");
    if (payload.isEmpty())
        payload = isError ? QStringLiteral("failed") : QStringLiteral("done");

    QJsonObject block;
    block[QStringLiteral("type")]        = QStringLiteral("tool_result");
    block[QStringLiteral("tool_use_id")] = toolUseId;
    block[QStringLiteral("content")]     = payload;
    if (isError)
        block[QStringLiteral("is_error")] = true;

    m_toolResults.append(block);
}

void AgentLoop::submitToolResults()
{
    QJsonObject userMsg;
    userMsg[QStringLiteral("role")]    = QStringLiteral("user");
    userMsg[QStringLiteral("content")] = m_toolResults;
    m_messages.append(userMsg);
    m_toolResults = QJsonArray();

    requestModel();
}

// ============================================================
//  Завершение
// ============================================================

void AgentLoop::stop(const QString& finalText, const QString& error)
{
    m_running = false;
    m_queue.clear();
    EditJournal::instance().endBatch();

    if (!error.isEmpty()) {
        JarvisState::instance().enter(JarvisPhase::Error, error);
        emit failed(error);
        return;
    }

    JarvisState::instance().toIdle();

    QString out = finalText.trimmed();
    if (out.isEmpty()) {
        out = m_transcript.isEmpty()
                  ? QStringLiteral("Done.")
                  : QStringLiteral("Done:\n") + m_transcript.join(QStringLiteral("\n"));
    }
    emit finished(out);
}
