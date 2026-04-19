#pragma once
// -------------------------------------------------------
// plugin_manager.h — Загрузчик и менеджер плагинов
//
// Сканирует папку plugins/, загружает DLL,
// проверяет совместимость API, управляет жизненным циклом.
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QPluginLoader>

#include "plugin_interface.h"

class PluginHost;

struct PluginInfo
{
    QString name;           // Имя плагина
    QString displayName;    // Человекочитаемое имя
    QString version;        // Версия
    QString filePath;       // Путь к DLL
    bool loaded = false;    // Загружен ли
    bool enabled = true;    // Включён ли
    JarvisPlugin* instance = nullptr;
    QPluginLoader* loader = nullptr;
};

class PluginManager : public QObject
{
    Q_OBJECT

public:
    explicit PluginManager(PluginHost* host, QObject* parent = nullptr);
    ~PluginManager() override;

    // Сканировать и загрузить плагины из директории
    void loadPlugins(const QString& pluginsDir);

    // Выгрузить все плагины
    void unloadAll();

    // Перезагрузить конкретный плагин (hot-reload)
    bool reloadPlugin(const QString& name);

    // Список плагинов
    QVector<PluginInfo> plugins() const { return m_plugins; }

    // Попытка обработки команды через плагины
    // Возвращает true если какой-то плагин обработал
    bool tryHandleCommand(const QString& input, QString& response);

    // Включить/выключить плагин
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
