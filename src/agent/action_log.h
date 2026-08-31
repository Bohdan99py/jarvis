#pragma once
// -------------------------------------------------------
// action_log.h — Что JARVIS на самом деле сделал
//
// EventFeed отвечает на вопрос «что случилось», ActionLog — на
// вопрос «что сделали МЫ». Разница принципиальная: событие может
// произойти само, действие всегда чьё-то, и на него можно сослаться:
//
//     10:42  agent           launch_app(Rider)        ok
//     10:43  workflow:Dev    open_url(docs)           ok
//     10:47  user            kill_process(chrome)     denied
//     10:52  trigger:Unreal  run_workflow(Focus)      ok
//
// Отсюда берутся три вещи, которых иначе нет ниоткуда:
//   • «что ты делал последние полчаса» — без выдумывания;
//   • «повтори то, что было в 10:47» — по записи, а не по памяти
//     модели о собственных словах;
//   • разбор, почему машина в текущем состоянии, когда действие
//     сработало в фоне и человек его не видел.
//
// Актор не передаётся в record() параметром, а держится скоупом:
// инструменты вызываются из четырёх мест (агент, сценарий, триггер,
// UI), и протаскивать «кто вызвал» через ToolRegistry значило бы
// научить реестр тому, что его не касается.
//
// Журнал дописывается строками JSON (одна запись — одна строка):
// падение на середине портит максимум последнюю строку, а не весь
// файл, и хвост читается без разбора всей истории.
// -------------------------------------------------------

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

// ============================================================
//  Запись
// ============================================================
enum class ActionOutcome {
    Ok     = 0,
    Failed = 1,
    Denied = 2    // человек не разрешил — тоже часть истории
};

QString actionOutcomeName(ActionOutcome outcome);

struct ActionRecord
{
    qint64        id = 0;
    QDateTime     at;
    QString       actor;    // user | agent | workflow:Name | trigger:Name | ui
    QString       tool;
    QJsonObject   args;
    ActionOutcome outcome = ActionOutcome::Ok;
    QString       display; // одна строка результата — то же, что видит UI
    int           risk = 0; // ToolRisk как int: журнал не зависит от реестра

    QString timeText() const { return at.toString(QStringLiteral("HH:mm")); }

    // "10:42  agent  launch_app(Rider) — Rider запущен"
    QString toLine(bool withDate = false) const;

    static ActionRecord fromJson(const QJsonObject& obj);
    QJsonObject         toJson() const;
};

// ============================================================
//  ActionLog
// ============================================================
class ActionLog : public QObject
{
    Q_OBJECT

public:
    // Синглтон по той же причине, что и EventFeed: писать в журнал
    // должны все, кто вообще способен что-то выполнить.
    static ActionLog& instance();

    // --- Актор ---
    // Скоуп восстанавливает предыдущего актора в деструкторе, поэтому
    // вложенность (триггер -> сценарий -> инструмент) не ломается.
    class Actor
    {
    public:
        explicit Actor(const QString& name);
        ~Actor();
        Actor(const Actor&)            = delete;
        Actor& operator=(const Actor&) = delete;
    private:
        QString m_previous;
    };

    void    setActor(const QString& actor);
    QString actor() const { return m_actor; }

    // --- Запись ---
    qint64 record(const QString& tool,
                  const QJsonObject& args,
                  ActionOutcome outcome,
                  const QString& display,
                  int risk = 0);

    // --- Чтение ---
    QVector<ActionRecord> records(int limit = 200) const;
    QVector<ActionRecord> since(const QDateTime& from) const;
    const ActionRecord*   find(qint64 id) const;

    // Последнее действие, которое имеет смысл повторять: удачное и не
    // сам повтор. Нужно для «сделай это ещё раз» без явного номера.
    const ActionRecord* lastRepeatable() const;

    int  count() const { return m_records.size(); }
    void clear();

    QString storagePath() const;

signals:
    void recorded(const ActionRecord& record);

private:
    ActionLog();

    void load();
    void appendLine(const ActionRecord& record) const;
    void rewriteFile() const;

    QVector<ActionRecord> m_records;   // новые в конце
    QString               m_actor = QStringLiteral("user");
    qint64                m_nextId = 1;
    bool                  m_dirtyFile = false;

    // В памяти держим столько, сколько имеет смысл показывать и
    // отдавать модели; на диске — вчетверо больше, чтобы «что было
    // вчера» пережило перезапуск.
    static constexpr int kMaxMemory    = 1000;
    static constexpr int kMaxFileLines = 4000;
};

class ToolRegistry;
class PermissionGate;

namespace JarvisTools {

// list_actions / repeat_action — журнал нужен не только панели:
// «что ты делал» и «повтори» человек спрашивает словами.
void registerActionTools(ToolRegistry& registry, PermissionGate* gate);

} // namespace JarvisTools
