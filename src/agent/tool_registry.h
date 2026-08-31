#pragma once
// -------------------------------------------------------
// tool_registry.h — Реестр ИНСТРУМЕНТОВ J.A.R.V.I.S.
//
// Чем отличается от CommandRegistry:
//
//   CommandRegistry : фраза  -> совпадение по ключевым словам -> текст
//   ToolRegistry    : ЛЮБОЙ запрос -> модель сама выбирает инструмент
//                     и аргументы (Anthropic tool use) -> действие
//
// То есть это тот слой, из-за отсутствия которого JARVIS оставался
// чат-приложением: модель могла только говорить, а не делать.
// Исполнение переиспользует уже существующее — PcController,
// SystemController, WindowController; здесь только описания,
// схемы аргументов и уровень риска.
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <functional>

// ============================================================
//  Уровень риска — вход в систему подтверждений
// ============================================================
enum class ToolRisk {
    Safe      = 0,  // читать, открывать, смотреть — выполняется сразу
    Moderate  = 1,  // писать в файл, закрыть окно, нажать клавиши
    Dangerous = 2   // удалить, убить процесс, выключить ПК, shell
};

QString toolRiskName(ToolRisk risk);

// ============================================================
//  Вердикт проверки — сработал ли инструмент НА САМОМ ДЕЛЕ
//
// «Запустил Unreal» до сих пор значило «ShellExecute вернул успех».
// Между этим и «Unreal работает» помещается весь список причин, по
// которым приложение не стартует: упало на сплэше, ждёт лицензию,
// уже было запущено и второй экземпляр молча закрылся.
//
// Поэтому у инструмента две функции: handler делает, verify через
// секунду смотрит, что из этого вышло.
// ============================================================
enum class VerifyState {
    Skipped = 0,   // проверки нет — не путать с «проверено и хорошо»
    Confirmed,     // постусловие выполнено
    Partial,       // сделано частично: процесс есть, окна нет
    Contradicted   // не произошло ничего: инструмент соврал
};

QString verifyStateName(VerifyState state);

struct ToolVerdict
{
    VerifyState state = VerifyState::Skipped;
    QString     detail;   // «процесс есть, окно проекта не появилось»

    bool isKnown() const { return state != VerifyState::Skipped; }
    bool isBad()   const { return state == VerifyState::Partial
                               || state == VerifyState::Contradicted; }

    static ToolVerdict confirmed(const QString& detail = QString())
    { ToolVerdict v; v.state = VerifyState::Confirmed;    v.detail = detail; return v; }
    static ToolVerdict partial(const QString& detail)
    { ToolVerdict v; v.state = VerifyState::Partial;      v.detail = detail; return v; }
    static ToolVerdict contradicted(const QString& detail)
    { ToolVerdict v; v.state = VerifyState::Contradicted; v.detail = detail; return v; }
};

// ============================================================
//  Результат вызова
// ============================================================
struct ToolResult
{
    bool    ok = true;
    QString text;     // что увидит модель (может быть длинным)
    QString display;  // одна строка для UI: "Chrome запущен"

    // Заполняется реестром после вызова verify. Обработчику трогать
    // не нужно: он сообщает, что сделал, а не что из этого получилось.
    ToolVerdict verdict;

    static ToolResult success(const QString& text, const QString& display = QString())
    {
        ToolResult r; r.ok = true; r.text = text;
        r.display = display.isEmpty() ? text.left(120) : display;
        return r;
    }
    static ToolResult failure(const QString& text)
    {
        ToolResult r; r.ok = false; r.text = text; r.display = text.left(120);
        return r;
    }
};

// ============================================================
//  Описание инструмента
// ============================================================
struct ToolSpec
{
    QString     name;         // snake_case, как его увидит модель
    QString     description;  // для модели — когда именно применять
    QJsonObject schema;       // JSON Schema аргументов (input_schema)
    ToolRisk    risk = ToolRisk::Safe;
    QString     category;     // "system" | "files" | "windows" | "shell" | ...

    // Человеческая формулировка планируемого действия для диалога
    // подтверждения: "Удалить C:\Projects\OldBuild". Если не задана —
    // собирается автоматически из имени и аргументов.
    std::function<QString(const QJsonObject&)> preview;

    std::function<ToolResult(const QJsonObject&)> handler;

    // Проверка постусловия. Вызывается ПОСЛЕ handler и только если тот
    // отчитался об успехе: проверять провалившийся вызов нечего.
    //
    // Не у каждого инструмента она есть и не должна быть: read_file
    // либо вернул текст, либо нет, третьего состояния у него не бывает.
    // Смысл verify появляется там, где между «команда принята» и
    // «результат наступил» есть зазор — запуск приложения, закрытие
    // окна, запись файла, убийство процесса.
    //
    // Ждать внутри можно: инструменты выполняются строго по одному,
    // и ожидание здесь честнее, чем отчёт об успехе, которого нет.
    std::function<ToolVerdict(const QJsonObject& args, const ToolResult& result)> verify;
};

// ============================================================
//  Сборщик JSON Schema — чтобы не писать QJsonObject руками
// ============================================================
class ToolSchema
{
public:
    ToolSchema& str(const QString& name, const QString& desc, bool required = true);
    ToolSchema& integer(const QString& name, const QString& desc, bool required = true);
    ToolSchema& boolean(const QString& name, const QString& desc, bool required = true);
    ToolSchema& choice(const QString& name, const QStringList& values,
                       const QString& desc, bool required = true);

    // Готовый кусок JSON Schema — для массивов и вложенных объектов,
    // которые не выражаются простыми str/integer/boolean.
    ToolSchema& raw(const QString& name, const QJsonObject& propertySchema,
                    bool required = true);

    QJsonObject build() const;

    // Пустая схема — инструмент без аргументов
    static QJsonObject empty();

private:
    ToolSchema& add(const QString& name, const QString& type,
                    const QString& desc, bool required);

    QJsonObject m_props;
    QStringList m_required;
};

// ============================================================
//  ToolRegistry
// ============================================================
class ToolRegistry : public QObject
{
    Q_OBJECT

public:
    explicit ToolRegistry(QObject* parent = nullptr);

    void registerTool(ToolSpec spec);

    bool             contains(const QString& name) const;
    const ToolSpec*  find(const QString& name) const;
    QStringList      names() const;
    QStringList      categories() const;
    int              count() const { return m_tools.size(); }

    // Массив в формате Anthropic Messages API (поле "tools").
    // only — фильтр по именам; пусто = все.
    QJsonArray toAnthropicJson(const QStringList& only = QStringList()) const;

    // Человеческое описание планируемого вызова (для подтверждения/лога)
    QString describeCall(const QString& name, const QJsonObject& args) const;

    // Синхронное выполнение. Проверку разрешений делает вызывающий
    // (AgentLoop) — реестр только исполняет.
    ToolResult invoke(const QString& name, const QJsonObject& args);

signals:
    // display и risk идут вместе с фактом вызова: подписчику (журнал
    // действий) иначе пришлось бы возвращаться в реестр за тем, что
    // здесь уже известно.
    void toolInvoked(const QString& name, const QJsonObject& args,
                     bool ok, const QString& display, int risk);

    // Отдельно от toolInvoked, а не полем в нём: подписчиков на факт
    // вызова четверо, а на результат проверки — только те, кто умеет
    // его показать. state — VerifyState как int, чтобы сигнал был
    // доступен из QML без регистрации типа.
    void toolVerified(const QString& name, int state, const QString& detail);

private:
    QVector<ToolSpec> m_tools;
};
