#pragma once
// -------------------------------------------------------
// plugin_bridge.h — Ядро со стороны плагина
//
// PluginHost и PluginManager были написаны давно и не использовались
// ни разу: интерфейс хоста никто не реализовывал, папка плагинов не
// сканировалась, каталог plugins/ даже не подключён к сборке. То есть
// система плагинов существовала как объявление о намерениях.
//
// Мост её включает. Он и есть тот самый хост: держит указатели на
// подсистемы, которые плагину нужны (память, команды, реестр
// инструментов, лента), и переводит простые типы границы DLL в
// внутренние вызовы ядра.
//
// UI-методы хоста не тянут за собой окно: они превращаются в сигналы.
// Есть кому их показать — покажет, некому — плагин всё равно работает.
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>

#include "plugin_host.h"

class Jarvis;
class ToolRegistry;
class PermissionGate;
class PluginManager;

class PluginBridge : public QObject, public PluginHost
{
    Q_OBJECT

public:
    PluginBridge(Jarvis* core,
                 ToolRegistry* tools,
                 PermissionGate* gate,
                 QObject* parent = nullptr);
    ~PluginBridge() override;

    // Загружает плагины из папки рядом с приложением и из папки данных
    // пользователя. Вторая важнее: в Program Files писать нельзя, а
    // ставить плагины куда-то надо.
    void loadAll();

    PluginManager* manager() const { return m_manager; }

    // Имена инструментов, зарегистрированных плагинами — чтобы в списке
    // было видно, что пришло не из ядра.
    QStringList pluginToolNames() const { return m_pluginTools; }

    QString summaryForModel() const;

    // --- PluginHost ---
    void addMessage(const QString& role, const QString& content) override;
    QJsonArray recentMessages(int max = 20) const override;
    void rememberFact(const QString& key, const QString& value) override;
    QString recallFact(const QString& key) const override;
    QJsonObject allFacts() const override;
    QJsonObject commandStats() const override;
    QString buildSystemPrompt() const override;

    void registerCommand(const QStringList& keywords,
                         CommandHandler handler,
                         const QString& description,
                         bool prefixMatch = false) override;

    bool registerTool(const QString& name,
                      const QString& description,
                      const QJsonObject& schema,
                      int risk,
                      ToolHandler handler) override;

    QString callTool(const QString& name, const QJsonObject& args, bool& ok) override;

    void postEvent(const QString& source, int level,
                   const QString& title, const QString& detail = QString()) override;

    void appendLog(const QString& who, const QString& text, const QString& color) override;
    void showSuggestion(const QString& description, const QString& action) override;
    void setStatus(const QString& text, const QString& color) override;

    void speakAsync(const QString& text) override;
    QString executeCommand(const QString& input) override;

    QString dataDir(const QString& pluginName) const override;

signals:
    // Для UI: мост не знает ни про окно, ни про лог-виджет.
    void logRequested(const QString& who, const QString& text, const QString& color);
    void suggestionRequested(const QString& description, const QString& action);
    void statusRequested(const QString& text, const QString& color);

private:
    Jarvis*         m_core    = nullptr;
    ToolRegistry*   m_tools   = nullptr;
    PermissionGate* m_gate    = nullptr;
    PluginManager*  m_manager = nullptr;

    QStringList m_pluginTools;

    // Имя плагина, который сейчас инициализируется: инструменты,
    // зарегистрированные из initialize(), приписываются ему.
    QString m_loadingPlugin;
};

namespace JarvisTools {

// list_plugins — что загружено, что упало и какие инструменты пришли
// не из ядра.
void registerPluginTools(ToolRegistry& registry, PluginBridge* bridge);

} // namespace JarvisTools
