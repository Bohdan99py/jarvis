#pragma once
// -------------------------------------------------------
// plugin_manager.h — Загрузчик и менеджер плагинов
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QPluginLoader>

#include "jarvis_core_export.h"
#include "plugin_interface.h"

class PluginHost;

struct PluginInfo
{
    QString name;
    QString displayName;
    QString version;
    QString filePath;
    bool loaded = false;
    bool enabled = true;
    JarvisPlugin* instance = nullptr;
    QPluginLoader* loader = nullptr;
};

class JARVIS_CORE_EXPORT PluginManager : public QObject
{
    Q_OBJECT

public:
    explicit PluginManager(PluginHost* host, QObject* parent = nullptr);
    ~PluginManager() override;

    void loadPlugins(const QString& pluginsDir);
    void unloadAll();
    bool reloadPlugin(const QString& name);

    QVector<PluginInfo> plugins() const { return m_plugins; }

    bool tryHandleCommand(const QString& input, QString& response);

    void setPluginEnabled(const QString& name, bool enabled);

signals:
    void pluginLoaded(const QString& name);
    void pluginUnloaded(const QString& name);
    void pluginError(const QString& name, const QString& error);

private:
    bool loadSinglePlugin(const QString& filePath);
    void unloadPlugin(PluginInfo& info);

    PluginHost* m_host = nullptr;
    QVector<PluginInfo> m_plugins;
};
