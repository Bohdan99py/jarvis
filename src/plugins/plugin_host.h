#pragma once
// -------------------------------------------------------
// plugin_host.h — Интерфейс хоста для плагинов
//
// Ядро реализует этот интерфейс и передаёт плагинам. Через него
// плагин получает доступ к памяти, командам, UI и — с API v2 —
// к слою действий: он может ЗАРЕГИСТРИРОВАТЬ ИНСТРУМЕНТ.
//
// Это главное отличие v2 от v1. Раньше плагин умел только
// handleCommand: поймать фразу и вернуть текст. То есть всё, чему
// плагин учил JARVIS, было доступно ровно из чата и только по
// совпадению ключевых слов — ни модель, ни сценарий, ни триггер,
// ни Ctrl+K об этих возможностях не знали.
//
// Зарегистрированный инструмент попадает в общий ToolRegistry и
// живёт по общим правилам: у него есть уровень риска, его вызов
// проходит через PermissionGate и записывается в журнал действий.
//
// ВАЖНО про границы доверия: сам плагин — это DLL, загруженная в
// процесс JARVIS, и никакой песочницы вокруг неё нет. Разрешения
// ограничивают вызовы ЕГО ИНСТРУМЕНТОВ, а не то, что код плагина
// может сделать напрямую. Ставить чужие плагины — это ровно то же
// доверие, что и запускать чужую программу.
//
// Типы в сигнатурах намеренно простые (QString, QJsonObject,
// std::function): плагин собирается отдельно, и тащить через
// границу DLL внутренние структуры ядра — верный способ получить
// падение при первом же несовпадении версий.
// -------------------------------------------------------

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

class PluginHost
{
public:
    virtual ~PluginHost() = default;

    // --- Память ---
    virtual void addMessage(const QString& role, const QString& content) = 0;
    virtual QJsonArray recentMessages(int max = 20) const = 0;
    virtual void rememberFact(const QString& key, const QString& value) = 0;
    virtual QString recallFact(const QString& key) const = 0;
    virtual QJsonObject allFacts() const = 0;
    virtual QJsonObject commandStats() const = 0;
    virtual QString buildSystemPrompt() const = 0;

    // --- Команды ---
    using CommandHandler = std::function<QString(const QString&)>;
    virtual void registerCommand(const QStringList& keywords,
                                 CommandHandler handler,
                                 const QString& description,
                                 bool prefixMatch = false) = 0;

    // --- Инструменты (API v2) ---

    // Уровень риска инструмента плагина. Значения совпадают с ToolRisk
    // ядра, но объявлены здесь, чтобы плагин не включал его заголовки.
    enum ToolRiskLevel {
        RiskSafe      = 0,   // чтение — выполняется молча
        RiskModerate  = 1,   // запись, изменение состояния
        RiskDangerous = 2    // удаление, необратимое — спрашивается всегда
    };

    // ok = false означает неудачу; возвращённый текст в обоих случаях
    // уходит модели, поэтому в нём должно быть сказано, что произошло.
    using ToolHandler = std::function<QString(const QJsonObject& args, bool& ok)>;

    // schema — JSON Schema аргументов (объект с "type"/"properties"/
    // "required"). Пустой объект = инструмент без аргументов.
    // Имя обязано быть уникальным: занятое ядром имя не заменяется.
    virtual bool registerTool(const QString& name,
                              const QString& description,
                              const QJsonObject& schema,
                              int risk,
                              ToolHandler handler) = 0;

    // Вызвать чужой инструмент — свой или ядра. Разрешения проверяются
    // так же, как для модели: плагин не обходной путь к действиям.
    virtual QString callTool(const QString& name, const QJsonObject& args, bool& ok) = 0;

    // --- События ---
    // level: 0 info, 1 ok, 2 warning, 3 error. Событие видно в ленте и
    // может запустить триггер — так плагин доводит своё «что-то
    // случилось» до остальной системы.
    virtual void postEvent(const QString& source, int level,
                           const QString& title, const QString& detail = QString()) = 0;

    // --- UI ---
    virtual void appendLog(const QString& who, const QString& text, const QString& color) = 0;
    virtual void showSuggestion(const QString& description, const QString& action) = 0;
    virtual void setStatus(const QString& text, const QString& color) = 0;

    // --- Утилиты ---
    virtual void speakAsync(const QString& text) = 0;
    virtual QString executeCommand(const QString& input) = 0;

    // Личная папка плагина для настроек и кэша — создаётся хостом.
    // Писать рядом с DLL нельзя: она может лежать в Program Files.
    virtual QString dataDir(const QString& pluginName) const = 0;
};
