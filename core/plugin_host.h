#pragma once
// -------------------------------------------------------
// plugin_host.h — Интерфейс хоста для плагинов
//
// Ядро реализует этот интерфейс и передаёт плагинам.
// Через него плагины получают доступ к памяти, командам,
// UI, и другим сервисам ядра.
// -------------------------------------------------------

#include <QString>
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

    // --- UI ---
    virtual void appendLog(const QString& who, const QString& text, const QString& color) = 0;
    virtual void showSuggestion(const QString& description, const QString& action) = 0;
    virtual void setStatus(const QString& text, const QString& color) = 0;

    // --- Утилиты ---
    virtual void speakAsync(const QString& text) = 0;
    virtual QString executeCommand(const QString& input) = 0;
};
