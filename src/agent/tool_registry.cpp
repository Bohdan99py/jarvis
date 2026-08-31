// -------------------------------------------------------
// tool_registry.cpp — см. tool_registry.h
// -------------------------------------------------------

#include "tool_registry.h"

#include "jarvis_state.h"

#include <QDebug>
#include <QElapsedTimer>

QString toolRiskName(ToolRisk risk)
{
    switch (risk) {
    case ToolRisk::Safe:      return QStringLiteral("safe");
    case ToolRisk::Moderate:  return QStringLiteral("moderate");
    case ToolRisk::Dangerous: return QStringLiteral("dangerous");
    }
    return QStringLiteral("safe");
}

QString verifyStateName(VerifyState state)
{
    switch (state) {
    case VerifyState::Skipped:      return QStringLiteral("skipped");
    case VerifyState::Confirmed:    return QStringLiteral("confirmed");
    case VerifyState::Partial:      return QStringLiteral("partial");
    case VerifyState::Contradicted: return QStringLiteral("contradicted");
    }
    return QStringLiteral("skipped");
}

// ============================================================
//  ToolSchema
// ============================================================

ToolSchema& ToolSchema::add(const QString& name, const QString& type,
                            const QString& desc, bool required)
{
    QJsonObject prop;
    prop[QStringLiteral("type")]        = type;
    prop[QStringLiteral("description")] = desc;
    m_props[name] = prop;
    if (required)
        m_required << name;
    return *this;
}

ToolSchema& ToolSchema::str(const QString& name, const QString& desc, bool required)
{ return add(name, QStringLiteral("string"), desc, required); }

ToolSchema& ToolSchema::integer(const QString& name, const QString& desc, bool required)
{ return add(name, QStringLiteral("integer"), desc, required); }

ToolSchema& ToolSchema::boolean(const QString& name, const QString& desc, bool required)
{ return add(name, QStringLiteral("boolean"), desc, required); }

ToolSchema& ToolSchema::choice(const QString& name, const QStringList& values,
                               const QString& desc, bool required)
{
    QJsonObject prop;
    prop[QStringLiteral("type")]        = QStringLiteral("string");
    prop[QStringLiteral("description")] = desc;
    QJsonArray arr;
    for (const QString& v : values)
        arr.append(v);
    prop[QStringLiteral("enum")] = arr;
    m_props[name] = prop;
    if (required)
        m_required << name;
    return *this;
}

ToolSchema& ToolSchema::raw(const QString& name, const QJsonObject& propertySchema,
                            bool required)
{
    m_props[name] = propertySchema;
    if (required)
        m_required << name;
    return *this;
}

QJsonObject ToolSchema::build() const
{
    QJsonObject schema;
    schema[QStringLiteral("type")]       = QStringLiteral("object");
    schema[QStringLiteral("properties")] = m_props;
    QJsonArray req;
    for (const QString& r : m_required)
        req.append(r);
    schema[QStringLiteral("required")] = req;
    return schema;
}

QJsonObject ToolSchema::empty()
{
    QJsonObject schema;
    schema[QStringLiteral("type")]       = QStringLiteral("object");
    schema[QStringLiteral("properties")] = QJsonObject();
    return schema;
}

// ============================================================
//  ToolRegistry
// ============================================================

ToolRegistry::ToolRegistry(QObject* parent)
    : QObject(parent)
{
}

void ToolRegistry::registerTool(ToolSpec spec)
{
    if (spec.name.isEmpty() || !spec.handler) {
        qWarning() << "[Tools] rejected malformed tool:" << spec.name;
        return;
    }
    for (int i = 0; i < m_tools.size(); ++i) {
        if (m_tools[i].name == spec.name) {
            m_tools[i] = std::move(spec);   // перерегистрация — замена
            return;
        }
    }
    m_tools.append(std::move(spec));
}

bool ToolRegistry::contains(const QString& name) const
{
    return find(name) != nullptr;
}

const ToolSpec* ToolRegistry::find(const QString& name) const
{
    for (const ToolSpec& t : m_tools) {
        if (t.name == name)
            return &t;
    }
    return nullptr;
}

QStringList ToolRegistry::names() const
{
    QStringList out;
    out.reserve(m_tools.size());
    for (const ToolSpec& t : m_tools)
        out << t.name;
    return out;
}

QStringList ToolRegistry::categories() const
{
    QStringList out;
    for (const ToolSpec& t : m_tools) {
        if (!t.category.isEmpty() && !out.contains(t.category))
            out << t.category;
    }
    out.sort();
    return out;
}

QJsonArray ToolRegistry::toAnthropicJson(const QStringList& only) const
{
    QJsonArray arr;
    for (const ToolSpec& t : m_tools) {
        if (!only.isEmpty() && !only.contains(t.name))
            continue;

        QJsonObject obj;
        obj[QStringLiteral("name")] = t.name;

        // Уровень риска дописывается в описание: модель должна понимать,
        // что опасный инструмент потребует подтверждения у человека,
        // иначе она планирует так, будто всё выполнится молча.
        QString desc = t.description;
        if (t.risk == ToolRisk::Moderate)
            desc += QStringLiteral(" (requires user confirmation)");
        else if (t.risk == ToolRisk::Dangerous)
            desc += QStringLiteral(" (DANGEROUS: always requires explicit user confirmation)");
        obj[QStringLiteral("description")]  = desc;
        obj[QStringLiteral("input_schema")] = t.schema.isEmpty() ? ToolSchema::empty()
                                                                 : t.schema;
        arr.append(obj);
    }
    return arr;
}

QString ToolRegistry::describeCall(const QString& name, const QJsonObject& args) const
{
    const ToolSpec* spec = find(name);
    if (spec && spec->preview) {
        const QString custom = spec->preview(args);
        if (!custom.isEmpty())
            return custom;
    }

    // Автоформат: name(arg=value, arg=value)
    QStringList parts;
    for (auto it = args.begin(); it != args.end(); ++it) {
        QString v;
        if (it.value().isString()) {
            v = it.value().toString();
            if (v.length() > 60)
                v = v.left(57) + QStringLiteral("...");
        } else if (it.value().isBool()) {
            v = it.value().toBool() ? QStringLiteral("true") : QStringLiteral("false");
        } else if (it.value().isDouble()) {
            v = QString::number(it.value().toDouble());
        } else {
            v = QStringLiteral("...");
        }
        parts << it.key() + QStringLiteral("=") + v;
    }
    return parts.isEmpty() ? name
                           : name + QStringLiteral("(") + parts.join(QStringLiteral(", "))
                                 + QStringLiteral(")");
}

ToolResult ToolRegistry::invoke(const QString& name, const QJsonObject& args)
{
    const ToolSpec* spec = find(name);
    if (!spec) {
        // Модель придумала несуществующий инструмент — это должно вернуться
        // ей текстом, а не упасть: она исправится следующим шагом.
        return ToolResult::failure(
            QStringLiteral("Unknown tool '%1'. Available: %2")
                .arg(name, names().join(QStringLiteral(", "))));
    }

    QElapsedTimer timer;
    timer.start();

    ToolResult result;
    try {
        result = spec->handler(args);
    } catch (const std::exception& e) {
        result = ToolResult::failure(QStringLiteral("Tool '%1' threw: %2")
                                         .arg(name, QString::fromUtf8(e.what())));
    } catch (...) {
        result = ToolResult::failure(QStringLiteral("Tool '%1' threw an unknown exception").arg(name));
    }

    // --------------------------------------------------------
    //  Проверка постусловия
    //
    //  Только после успеха: у провалившегося вызова проверять
    //  нечего, а лишняя секунда ожидания на каждой ошибке —
    //  это заметно.
    // --------------------------------------------------------
    if (result.ok && spec->verify) {
        JarvisState::instance().enter(JarvisPhase::Verifying,
                                      QStringLiteral("Проверяю: %1").arg(result.display));
        try {
            result.verdict = spec->verify(args, result);
        } catch (const std::exception& e) {
            // Проверка не должна ронять вызов, который уже состоялся:
            // «не смог проверить» — это Skipped, а не провал действия.
            qWarning() << "[Tools] verify" << name << "threw:" << e.what();
            result.verdict = ToolVerdict();
        } catch (...) {
            qWarning() << "[Tools] verify" << name << "threw";
            result.verdict = ToolVerdict();
        }

        if (result.verdict.isKnown()) {
            // Модель должна увидеть расхождение в том же тексте, что и
            // результат: отдельным сообщением оно потеряется, а именно
            // из-за него ей и предстоит менять план.
            const QString note = QStringLiteral("\n[verification: %1] %2")
                                     .arg(verifyStateName(result.verdict.state),
                                          result.verdict.detail);
            result.text += note;

            if (result.verdict.state == VerifyState::Contradicted) {
                // Инструмент отчитался об успехе, которого не было. Это
                // провал, а не примечание: тогда is_error дойдёт до
                // модели, журнал запишет Failed, а UI покажет крестик.
                result.ok      = false;
                result.display = QStringLiteral("не подтверждено: ") + result.verdict.detail;
            } else if (result.verdict.state == VerifyState::Partial) {
                result.display += QStringLiteral(" — частично: ") + result.verdict.detail;
            }
        }

        emit toolVerified(name, static_cast<int>(result.verdict.state), result.verdict.detail);
    }

    qDebug() << "[Tools]" << name << (result.ok ? "ok" : "FAIL")
             << verifyStateName(result.verdict.state)
             << timer.elapsed() << "ms |" << result.display.left(80);

    emit toolInvoked(name, args, result.ok, result.display, static_cast<int>(spec->risk));
    return result;
}
