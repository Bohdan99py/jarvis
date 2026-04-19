#pragma once
// -------------------------------------------------------
// jarvis.h — Ядро ассистента: команды, TTS, плагины
// -------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define _WIN32_WINNT 0x0601

#include <QObject>
#include <QString>
#include <QMutex>
#include <windows.h>
#include <objbase.h>
#include <atomic>

#include "jarvis_core_export.h"
#include "command_registry.h"
#include "plugin_host.h"

class KeyEmulator;
class SessionMemory;
class ClaudeApi;
class ActionPredictor;
class PluginManager;
class Updater;

// RAII-обёртка для COM
class ComInitializer
{
public:
    ComInitializer()  { m_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~ComInitializer() { if (SUCCEEDED(m_hr)) CoUninitialize(); }

    ComInitializer(const ComInitializer&) = delete;
    ComInitializer& operator=(const ComInitializer&) = delete;

    bool ok() const { return SUCCEEDED(m_hr); }
private:
    HRESULT m_hr = E_FAIL;
};

class JARVIS_CORE_EXPORT Jarvis : public QObject, public PluginHost
{
    Q_OBJECT

public:
    explicit Jarvis(QObject* parent = nullptr);
    ~Jarvis() override;

    // Обработка команды (синхронная для локальных, async для API)
    QString processCommand(const QString& input);
    void speakAsync(const QString& text) override;
    bool isSpeaking() const { return m_speaking.load(); }

    KeyEmulator*     keyEmulator()     const { return m_keyEmulator; }
    SessionMemory*   memory()          const { return m_memory; }
    ClaudeApi*       claudeApi()       const { return m_claudeApi; }
    ActionPredictor* actionPredictor() const { return m_predictor; }
    PluginManager*   pluginManager()   const { return m_pluginMgr; }
    Updater*         updater()         const { return m_updater; }

    // --- PluginHost реализация ---
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

    void appendLog(const QString& who, const QString& text, const QString& color) override;
    void showSuggestion(const QString& description, const QString& action) override;
    void setStatus(const QString& text, const QString& color) override;
    QString executeCommand(const QString& input) override;

signals:
    void speakingChanged(bool speaking);

    // Асинхронный ответ от Claude API
    void asyncResponseReady(const QString& response);
    void asyncResponseError(const QString& error);

    // Предложение действия
    void suggestionAvailable(const QString& description, const QString& action);

    // UI-сигналы (для PluginHost)
    void logRequested(const QString& who, const QString& text, const QString& color);
    void statusRequested(const QString& text, const QString& color);

private:
    void registerCommands();

    // Обработчики команд
    QString cmdTime(const QString& input);
    QString cmdDate(const QString& input);
    QString cmdGreeting(const QString& input);
    QString cmdUserName(const QString& input);
    QString cmdHelp(const QString& input);
    QString cmdLaunchApp(const QString& app);
    QString cmdWebSearch(const QString& query);
    QString cmdYoutube(const QString& query);
    QString cmdLock(const QString& input);
    QString cmdBrowser(const QString& input);
    QString cmdTypeText(const QString& input);
    QString cmdPressKey(const QString& input);
    QString cmdCombo(const QString& input);

    // Память и API
    QString cmdSetApiKey(const QString& input);
    QString cmdRememberFact(const QString& input);
    QString cmdRecallFact(const QString& input);
    QString cmdShowMemory(const QString& input);
    QString cmdShowStats(const QString& input);

    // Обновления и плагины
    QString cmdCheckUpdate(const QString& input);
    QString cmdListPlugins(const QString& input);
    QString cmdReloadPlugin(const QString& input);

    // Обработка ответа Claude API
    void handleClaudeResponse(const QString& response);

    static QString extractArg(const QString& input, const QStringList& prefixes);
    static WORD parseVirtualKey(const QString& name);

    ComInitializer   m_com;
    CommandRegistry  m_registry;
    KeyEmulator*     m_keyEmulator = nullptr;
    SessionMemory*   m_memory      = nullptr;
    ClaudeApi*       m_claudeApi   = nullptr;
    ActionPredictor* m_predictor   = nullptr;
    PluginManager*   m_pluginMgr   = nullptr;
    Updater*         m_updater     = nullptr;

    std::atomic<bool> m_speaking{false};
    QMutex m_ttsMutex;
};
