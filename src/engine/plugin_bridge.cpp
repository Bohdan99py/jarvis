// -------------------------------------------------------
// plugin_bridge.cpp — см. plugin_bridge.h
// -------------------------------------------------------

#include "plugin_bridge.h"

#include "action_log.h"
#include "command_registry.h"
#include "event_feed.h"
#include "jarvis.h"
#include "jarvis_paths.h"
#include "permission_gate.h"
#include "plugin_manager.h"
#include "session_memory.h"
#include "tool_registry.h"

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <QDebug>

PluginBridge::PluginBridge(Jarvis* core,
                           ToolRegistry* tools,
                           PermissionGate* gate,
                           QObject* parent)
    : QObject(parent)
    , m_core(core)
    , m_tools(tools)
    , m_gate(gate)
{
    m_manager = new PluginManager(this, this);

    connect(m_manager, &PluginManager::pluginInitializing, this,
            [this](const QString& name) { m_loadingPlugin = name; });

    connect(m_manager, &PluginManager::pluginLoaded, this, [this](const QString& name) {
        m_loadingPlugin.clear();
        EventFeed::instance().post(QStringLiteral("plugin"), EventLevel::Good,
                                   QStringLiteral("Плагин загружен: %1").arg(name));
    });

    connect(m_manager, &PluginManager::pluginError, this,
            [this](const QString& name, const QString& error) {
        m_loadingPlugin.clear();
        // Плагин, который не загрузился, обязан быть виден: иначе
        // «функция пропала» превращается в получасовое расследование.
        EventFeed::instance().post(QStringLiteral("plugin"), EventLevel::Warning,
                                   QStringLiteral("Плагин не загружен: %1").arg(name),
                                   error, QStringLiteral("plugin-error/") + name);
    });
}

PluginBridge::~PluginBridge() = default;

void PluginBridge::loadAll()
{
    // Папка рядом с приложением — для плагинов, приехавших с установкой.
    m_manager->loadPlugins(QCoreApplication::applicationDirPath()
                           + QStringLiteral("/plugins"));

    // И папка пользователя: в Program Files писать нельзя, а ставить
    // плагины куда-то надо.
    const QString userDir = JarvisPaths::subPath(QStringLiteral("plugins"));
    if (QDir(userDir).absolutePath()
        != QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/plugins")).absolutePath()) {
        m_manager->loadPlugins(userDir);
    }

    qDebug() << "[Plugins] loaded" << m_manager->plugins().size()
             << "entries," << m_pluginTools.size() << "tools registered";
}

// ============================================================
//  Память
// ============================================================

void PluginBridge::addMessage(const QString& role, const QString& content)
{
    if (m_core && m_core->memory())
        m_core->memory()->addMessage(role, content);
}

QJsonArray PluginBridge::recentMessages(int max) const
{
    if (m_core && m_core->memory())
        return m_core->memory()->recentMessagesAsJson(max);
    return {};
}

void PluginBridge::rememberFact(const QString& key, const QString& value)
{
    if (m_core && m_core->memory())
        m_core->memory()->rememberFact(key, value);
}

QString PluginBridge::recallFact(const QString& key) const
{
    if (m_core && m_core->memory())
        return m_core->memory()->recallFact(key);
    return QString();
}

QJsonObject PluginBridge::allFacts() const
{
    if (m_core && m_core->memory())
        return m_core->memory()->allFacts();
    return {};
}

QJsonObject PluginBridge::commandStats() const
{
    // Статистика собирается из журнала действий: сколько раз какой
    // инструмент вызывали. Отдельного счётчика ради этого заводить
    // не нужно — он уже есть, просто под другим именем.
    QJsonObject stats;
    for (const ActionRecord& r : ActionLog::instance().records()) {
        if (r.outcome != ActionOutcome::Ok)
            continue;
        stats[r.tool] = stats.value(r.tool).toInt(0) + 1;
    }
    return stats;
}

QString PluginBridge::buildSystemPrompt() const
{
    if (m_core && m_core->memory())
        return m_core->memory()->buildSystemPrompt();
    return QString();
}

// ============================================================
//  Команды и инструменты
// ============================================================

void PluginBridge::registerCommand(const QStringList& keywords,
                                   CommandHandler handler,
                                   const QString& description,
                                   bool prefixMatch)
{
    if (!m_core || !handler)
        return;
    m_core->commandRegistry()->registerCommand(keywords, std::move(handler),
                                               description, prefixMatch);
}

bool PluginBridge::registerTool(const QString& name,
                                const QString& description,
                                const QJsonObject& schema,
                                int risk,
                                ToolHandler handler)
{
    if (!m_tools || !handler)
        return false;

    const QString toolName = name.trimmed();
    if (toolName.isEmpty())
        return false;

    // Занятое имя не переопределяется: плагин, подменивший kill_process
    // или write_file, — это не расширение, а подмена, и человек об этом
    // не узнает никак.
    if (m_tools->contains(toolName)) {
        qWarning() << "[Plugins] tool name already taken:" << toolName;
        EventFeed::instance().post(
            QStringLiteral("plugin"), EventLevel::Warning,
            QStringLiteral("Плагин %1: имя инструмента занято — %2")
                .arg(m_loadingPlugin, toolName));
        return false;
    }

    const QString owner = m_loadingPlugin.isEmpty() ? QStringLiteral("plugin")
                                                    : m_loadingPlugin;

    ToolSpec spec;
    spec.name        = toolName;
    spec.description = description;
    spec.schema      = schema.isEmpty() ? ToolSchema::empty() : schema;
    spec.risk        = static_cast<ToolRisk>(qBound(0, risk, 2));
    spec.category    = QStringLiteral("plugin:") + owner;
    spec.handler     = [handler](const QJsonObject& args) -> ToolResult {
        bool ok = true;
        QString text;
        // Исключение из чужой DLL не должно ронять JARVIS — реестр ловит
        // и своё, но здесь граница доверия проходит явно.
        try {
            text = handler(args, ok);
        } catch (const std::exception& e) {
            return ToolResult::failure(QStringLiteral("Плагин упал: %1")
                                           .arg(QString::fromUtf8(e.what())));
        } catch (...) {
            return ToolResult::failure(QStringLiteral("Плагин упал"));
        }
        return ok ? ToolResult::success(text) : ToolResult::failure(text);
    };

    m_tools->registerTool(spec);
    m_pluginTools << toolName;
    return true;
}

QString PluginBridge::callTool(const QString& name, const QJsonObject& args, bool& ok)
{
    ok = false;
    if (!m_tools)
        return QStringLiteral("Tools are not available.");

    const ToolSpec* spec = m_tools->find(name);
    if (!spec)
        return QStringLiteral("Unknown tool '%1'.").arg(name);

    // Разрешения проверяются и здесь: плагин не обходной путь к
    // действиям, которые у модели потребовали бы подтверждения.
    if (m_gate) {
        QString reason;
        if (!m_gate->evaluateBlocking(name, spec->risk,
                                      m_tools->describeCall(name, args), &reason)) {
            return reason.isEmpty() ? QStringLiteral("Declined.") : reason;
        }
    }

    ActionLog::Actor scope(QStringLiteral("plugin"));
    const ToolResult res = m_tools->invoke(name, args);
    ok = res.ok;
    return res.text;
}

void PluginBridge::postEvent(const QString& source, int level,
                             const QString& title, const QString& detail)
{
    EventFeed::instance().post(source.isEmpty() ? QStringLiteral("plugin") : source,
                               static_cast<EventLevel>(qBound(0, level, 3)),
                               title, detail);
}

// ============================================================
//  UI и утилиты
// ============================================================

void PluginBridge::appendLog(const QString& who, const QString& text, const QString& color)
{
    emit logRequested(who, text, color);
}

void PluginBridge::showSuggestion(const QString& description, const QString& action)
{
    emit suggestionRequested(description, action);
}

void PluginBridge::setStatus(const QString& text, const QString& color)
{
    emit statusRequested(text, color);
}

void PluginBridge::speakAsync(const QString& text)
{
    if (m_core)
        m_core->speakAsync(text);
}

QString PluginBridge::executeCommand(const QString& input)
{
    return m_core ? m_core->processCommand(input) : QString();
}

QString PluginBridge::dataDir(const QString& pluginName) const
{
    const QString safe = QString(pluginName).replace(
        QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    const QString dir = JarvisPaths::subPath(QStringLiteral("plugins/data/") + safe);
    QDir().mkpath(dir);
    return dir;
}

QString PluginBridge::summaryForModel() const
{
    const QVector<PluginInfo> all = m_manager ? m_manager->plugins() : QVector<PluginInfo>();
    if (all.isEmpty())
        return QStringLiteral("No plugins installed.");

    QStringList lines;
    for (const PluginInfo& p : all) {
        lines << QStringLiteral("%1 %2 — %3")
                     .arg(p.loaded ? QStringLiteral("[on] ")
                                   : (p.enabled ? QStringLiteral("[fail]")
                                                : QStringLiteral("[off]")),
                          p.displayName.isEmpty() ? p.name : p.displayName,
                          p.version.isEmpty() ? QStringLiteral("?") : p.version);
    }
    if (!m_pluginTools.isEmpty())
        lines << QStringLiteral("Tools from plugins: ")
                 + m_pluginTools.join(QStringLiteral(", "));
    return lines.join(QChar('\n'));
}

// ============================================================
//  Инструмент
// ============================================================

namespace JarvisTools {

void registerPluginTools(ToolRegistry& reg, PluginBridge* bridge)
{
    if (!bridge)
        return;

    ToolSpec t;
    t.name        = QStringLiteral("list_plugins");
    t.category    = QStringLiteral("plugins");
    t.risk        = ToolRisk::Safe;
    t.description = QStringLiteral(
        "Installed plugins: which ones loaded, which failed, and which tools came "
        "from a plugin rather than from the core. Use it when a tool the user expects "
        "does not exist, or when something behaves unexpectedly - a failed plugin is "
        "a common reason.");
    t.schema  = ToolSchema::empty();
    t.handler = [bridge](const QJsonObject&) -> ToolResult {
        return ToolResult::success(
            bridge->summaryForModel(),
            QStringLiteral("Плагинов: %1").arg(bridge->manager()
                                                   ? bridge->manager()->plugins().size() : 0));
    };
    reg.registerTool(t);
}

} // namespace JarvisTools
