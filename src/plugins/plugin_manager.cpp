// -------------------------------------------------------
// plugin_manager.cpp — Загрузчик и менеджер плагинов
// -------------------------------------------------------

#include "plugin_manager.h"
#include "plugin_host.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QCoreApplication>

// ============================================================
// Конструктор / Деструктор
// ============================================================

PluginManager::PluginManager(PluginHost* host, QObject* parent)
    : QObject(parent)
    , m_host(host)
{
}

PluginManager::~PluginManager()
{
    unloadAll();
}

// ============================================================
// Загрузка плагинов из директории
// ============================================================

void PluginManager::loadPlugins(const QString& pluginsDir)
{
    QDir dir(pluginsDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
        return;
    }

    // Читаем конфиг плагинов (какие включены/выключены)
    QJsonObject pluginConfig;
    QFile configFile(dir.filePath(QStringLiteral("plugins.json")));
    if (configFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(configFile.readAll());
        configFile.close();
        if (doc.isObject()) {
            pluginConfig = doc.object();
        }
    }

    // Сканируем DLL/so файлы
#ifdef Q_OS_WIN
    QStringList filters = {QStringLiteral("*.dll")};
#else
    QStringList filters = {QStringLiteral("*.so"), QStringLiteral("*.dylib")};
#endif

    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);

    for (const QFileInfo& fi : files) {
        // Пропускаем файлы ядра
        if (fi.fileName().contains(QStringLiteral("JarvisCore"))) {
            continue;
        }

        // Проверяем, не отключён ли плагин в конфиге
        QString baseName = fi.baseName();
        QJsonObject cfg = pluginConfig[baseName].toObject();
        bool enabled = cfg[QStringLiteral("enabled")].toBool(true);

        if (!enabled) {
            PluginInfo info;
            info.name = baseName;
            info.displayName = baseName;
            info.filePath = fi.absoluteFilePath();
            info.loaded = false;
            info.enabled = false;
            m_plugins.append(info);
            continue;
        }

        loadSinglePlugin(fi.absoluteFilePath());
    }
}

// ============================================================
// Загрузка одного плагина
// ============================================================

bool PluginManager::loadSinglePlugin(const QString& filePath)
{
    auto* loader = new QPluginLoader(filePath, this);

    QObject* pluginObj = loader->instance();
    if (!pluginObj) {
        QString error = loader->errorString();
        emit pluginError(QFileInfo(filePath).baseName(), error);
        delete loader;
        return false;
    }

    JarvisPlugin* plugin = qobject_cast<JarvisPlugin*>(pluginObj);
    if (!plugin) {
        emit pluginError(QFileInfo(filePath).baseName(),
                         QStringLiteral("Не реализует JarvisPlugin интерфейс"));
        loader->unload();
        delete loader;
        return false;
    }

    // Проверяем совместимость API
    if (plugin->apiVersion() != JARVIS_PLUGIN_API_VERSION) {
        emit pluginError(plugin->name(),
                         QStringLiteral("Несовместимая версия API: %1 (нужна %2)")
                             .arg(plugin->apiVersion())
                             .arg(JARVIS_PLUGIN_API_VERSION));
        loader->unload();
        delete loader;
        return false;
    }

    // Инициализируем
    if (!plugin->initialize(m_host)) {
        emit pluginError(plugin->name(),
                         QStringLiteral("Ошибка инициализации"));
        loader->unload();
        delete loader;
        return false;
    }

    PluginInfo info;
    info.name = plugin->name();
    info.displayName = plugin->displayName();
    info.version = plugin->version();
    info.filePath = filePath;
    info.loaded = true;
    info.enabled = true;
    info.instance = plugin;
    info.loader = loader;

    m_plugins.append(info);

    emit pluginLoaded(info.name);
    return true;
}

// ============================================================
// Выгрузка
// ============================================================

void PluginManager::unloadPlugin(PluginInfo& info)
{
    if (!info.loaded || !info.instance) return;

    info.instance->shutdown();
    info.loader->unload();
    delete info.loader;

    info.instance = nullptr;
    info.loader = nullptr;
    info.loaded = false;

    emit pluginUnloaded(info.name);
}

void PluginManager::unloadAll()
{
    for (auto& info : m_plugins) {
        unloadPlugin(info);
    }
    m_plugins.clear();
}

// ============================================================
// Hot-reload
// ============================================================

bool PluginManager::reloadPlugin(const QString& name)
{
    for (int i = 0; i < m_plugins.size(); ++i) {
        if (m_plugins[i].name == name) {
            QString filePath = m_plugins[i].filePath;
            unloadPlugin(m_plugins[i]);
            m_plugins.removeAt(i);
            return loadSinglePlugin(filePath);
        }
    }
    return false;
}

// ============================================================
// Обработка команд через плагины
// ============================================================

bool PluginManager::tryHandleCommand(const QString& input, QString& response)
{
    for (auto& info : m_plugins) {
        if (!info.loaded || !info.enabled || !info.instance) continue;

        if (info.instance->handleCommand(input, response)) {
            return true;
        }
    }
    return false;
}

// ============================================================
// Включение/выключение
// ============================================================

void PluginManager::setPluginEnabled(const QString& name, bool enabled)
{
    for (auto& info : m_plugins) {
        if (info.name == name) {
            info.enabled = enabled;

            if (!enabled && info.loaded) {
                unloadPlugin(info);
            } else if (enabled && !info.loaded) {
                loadSinglePlugin(info.filePath);
            }
            return;
        }
    }
}
