#pragma once
// -------------------------------------------------------
// jarvis.h — Ядро ассистента: команды, TTS, мозги, IDE
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

#include "command_registry.h"

class KeyEmulator;
class SessionMemory;
class ClaudeApi;
class ActionPredictor;
class AutoUpdater;
class ProjectIndexer;
class CodeActions;

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

class Jarvis : public QObject
{
    Q_OBJECT

public:
    explicit Jarvis(QObject* parent = nullptr);
    ~Jarvis() override;

    QString processCommand(const QString& input);
    void speakAsync(const QString& text);
    bool isSpeaking() const { return m_speaking.load(); }

    KeyEmulator*     keyEmulator()      const { return m_keyEmulator; }
    SessionMemory*   memory()           const { return m_memory; }
    ClaudeApi*       claudeApi()        const { return m_claudeApi; }
    ActionPredictor* actionPredictor()  const { return m_predictor; }
    AutoUpdater*     autoUpdater()      const { return m_updater; }
    ProjectIndexer*  projectIndexer()   const { return m_indexer; }
    CodeActions*     codeActions()      const { return m_codeActions; }

signals:
    void speakingChanged(bool speaking);
    void asyncResponseReady(const QString& response);
    void asyncResponseError(const QString& error);
    void suggestionAvailable(const QString& description, const QString& action);

private:
    void registerCommands();

    // Команды
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

    // Обновление
    QString cmdCheckUpdate(const QString& input);

    // Индексатор
    QString cmdIndexProject(const QString& input);
    QString cmdFindSymbol(const QString& input);
    QString cmdProjectMap(const QString& input);
    QString cmdGrep(const QString& input);

    void handleClaudeResponse(const QString& response);

    static QString extractArg(const QString& input, const QStringList& prefixes);
    static WORD parseVirtualKey(const QString& name);

    ComInitializer   m_com;
    CommandRegistry  m_registry;
    KeyEmulator*     m_keyEmulator  = nullptr;
    SessionMemory*   m_memory       = nullptr;
    ClaudeApi*       m_claudeApi    = nullptr;
    ActionPredictor* m_predictor    = nullptr;
    AutoUpdater*     m_updater      = nullptr;
    ProjectIndexer*  m_indexer      = nullptr;
    CodeActions*     m_codeActions  = nullptr;

    std::atomic<bool> m_speaking{false};
    QMutex m_ttsMutex;
};
