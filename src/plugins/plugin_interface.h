#pragma once
// -------------------------------------------------------
// plugin_interface.h — Интерфейс плагинов J.A.R.V.I.S.
//
// Каждый плагин — это DLL, реализующая этот интерфейс.
// Позволяет обновлять модули без пересборки ядра.
// -------------------------------------------------------

#include <QString>
#include <QJsonObject>
#include <QtPlugin>

// Версия API плагинов — при несовместимых изменениях увеличивается.
//
// v2: у хоста появился registerTool() — плагин добавляет инструменты в
//     общий реестр, а не только ловит фразы через handleCommand.
//     Плагины v1 продолжают загружаться: интерфейс самого плагина не
//     менялся, расширился только хост.
#define JARVIS_PLUGIN_API_VERSION 2

// Минимальная версия, с которой ядро ещё умеет работать. Плагин v1
// ничего не знает о новых методах хоста и от этого не страдает.
#define JARVIS_PLUGIN_API_MIN_VERSION 1

class PluginHost;  // forward — ядро предоставляет хост плагину

class JarvisPlugin
{
public:
    virtual ~JarvisPlugin() = default;

    // Метаданные
    virtual QString name() const = 0;          // "claude_api", "action_predictor"
    virtual QString displayName() const = 0;   // "Claude API", "Предугадывание"
    virtual QString version() const = 0;       // "1.0.0"
    virtual int apiVersion() const = 0;        // JARVIS_PLUGIN_API_VERSION

    // Жизненный цикл
    virtual bool initialize(PluginHost* host) = 0;  // Вызывается при загрузке
    virtual void shutdown() = 0;                      // Вызывается при выгрузке

    // Обработка команд (опционально)
    // Возвращает true если плагин обработал команду
    virtual bool handleCommand(const QString& input, QString& response) {
        Q_UNUSED(input); Q_UNUSED(response); return false;
    }

    // Конфигурация (опционально)
    virtual QJsonObject defaultConfig() const { return {}; }
    virtual void applyConfig(const QJsonObject& config) { Q_UNUSED(config); }
};

#define JarvisPlugin_iid "com.jarvis.Plugin/1.0"
Q_DECLARE_INTERFACE(JarvisPlugin, JarvisPlugin_iid)
