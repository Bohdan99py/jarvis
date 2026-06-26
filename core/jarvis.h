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
#include <QStringList>
#include <QMutex>
#include <windows.h>
#include <objbase.h>
#include <atomic>

#include "command_registry.h"
#include "applauncher.h"
#include "user_profile.h"
#include "activity_tracker.h"

class KeyEmulator;
class SessionMemory;
class ClaudeApi;
class OllamaApi;
class GeminiApi;
class ActionPredictor;
class AutoUpdater;
class ProjectIndexer;
class CodeActions;
class AttachmentsManager;
class PcCommandRegistry;
class TrainingPipelineController;

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

    // Обработка пользовательского ввода.
    // Brain в MainWindow уже определил намерение.
    // attachmentBlock — готовый блок из AttachmentsManager::buildAttachmentBlock().
    QString processCommand(const QString& input,
                           const QString& attachmentBlock = QString(),
                           const QString& langInstruction = QString());

    void speakAsync(const QString& text);
    bool isSpeaking() const { return m_speaking.load(); }

    KeyEmulator*        keyEmulator()        const { return m_keyEmulator; }
    SessionMemory*      memory()             const { return m_memory; }
    ClaudeApi*          claudeApi()          const { return m_claudeApi; }
    OllamaApi*          ollamaApi()          const { return m_geminiApi; }  // m_geminiApi хранит OllamaApi
    GeminiApi*          geminiBackup()       const { return m_geminiBackup; }
    ActionPredictor*    actionPredictor()    const { return m_predictor; }
    AutoUpdater*        autoUpdater()        const { return m_updater; }
    ProjectIndexer*     projectIndexer()     const { return m_indexer; }
    CodeActions*        codeActions()        const { return m_codeActions; }
    AttachmentsManager* attachments()        const { return m_attachments; }
    UserProfile*        userProfile()        const { return m_profile; }
    PcCommandRegistry*  pcCommands()         const { return m_pcCommands; }
    ActivityTracker*    activityTracker()    const { return m_activity; }

    // Multi-user: switch active user (all data scoped to userId)
    void setCurrentUserId(qint64 id) { m_currentUserId = id; }
    qint64 currentUserId() const     { return m_currentUserId; }

    // Мультиагентный режим: true = Claude для кода, Gemini для бесед
    void setMultiAgentMode(bool enabled);
    bool multiAgentMode() const { return m_multiAgentMode; }

    // Синхронизация языка UI — нужна потому что gUiLanguage() (lang.h)
    // это static inline и в MSVC JarvisCore.lib имеет свой экземпляр,
    // независимый от app/mainwindow.cpp. Без явной передачи IS_EN в core
    // всегда возвращает Russian (дефолт), даже если в UI выбран English.
    void setUiLanguage(bool english) { m_uiEnglish = english; }
    bool uiEnglish() const           { return m_uiEnglish; }

    // Синхронизировать данные индексатора с SessionMemory (system prompt)
    void syncProjectInfoToMemory();

    // === IDE-агент (вайбкодинг) ===
    // Определяет, похож ли ввод на кодинг-запрос/запрос новой фичи.
    // Используется и для RAG-режима в buildProjectContext, и для
    // автоматического открытия IDE.
    static bool isCodingIntent(const QString& input);

    // Открыть текущий проект в IDE (по умолчанию CLion).
    // ideName — алиас из AppLauncher (clion, rider, vscode...);
    // пустая строка = CLion, и только один раз автоматически за сессию.
    // Возвращает текст для лога/чата либо пустую строку, если
    // открывать не нужно (нет проекта / уже открывали автоматически).
    QString openProjectInIDE(const QString& ideName = QString());

signals:
    void speakingChanged(bool speaking);
    void asyncResponseReady(const QString& response);
    void asyncResponseError(const QString& error);
    void geminiError(const QString& error);  // причина fallback на Claude — видна в UI
    void suggestionAvailable(const QString& description, const QString& action);
    void attachmentsConsumed();
    void agentSelected(const QString& agentName);
    void ideOpened(const QString& message);   // JARVIS открыл проект в IDE

private:
    void registerCommands();

    // === Контекст для Claude (RAG) ===
    static QStringList extractKeywords(const QString& input);
    QString buildProjectContext(const QString& userQuery) const;

    // === Роутинг агентов ===
    bool routeToClaude(const QString& input, const QString& attachmentBlock) const;

    // === Системные команды реестра ===
    QString cmdSetApiKey(const QString& input);
    QString cmdSetGeminiKey(const QString& input);
    QString cmdRememberFact(const QString& input);
    QString cmdRecallFact(const QString& input);
    QString cmdShowMemory(const QString& input);
    QString cmdShowStats(const QString& input);
    QString cmdHelp(const QString& input);
    QString cmdCheckUpdate(const QString& input);
    QString cmdIndexProject(const QString& input);
    QString cmdFindSymbol(const QString& input);
    QString cmdProjectMap(const QString& input);
    QString cmdGrep(const QString& input);
    QString cmdOpenProjectIDE(const QString& input);
    QString cmdShowProfile(const QString& input);

    // === Виртуальная клавиатура ===
    QString cmdTypeText(const QString& input);
    QString cmdPressKey(const QString& input);
    QString cmdCombo(const QString& input);

    void handleClaudeResponse(const QString& response);

    // === Автопродолжение больших файлов ===
    // Если генерация [FILE:path] обрезана по лимиту токенов
    // (ClaudeApi::wasTruncated()), JARVIS сам запрашивает продолжение
    // у Claude, накапливает части в m_pendingFile и пишет файл целиком,
    // когда встречает [/FILE] или достигает лимита итераций.
    struct PendingFileGeneration {
        bool    active        = false; // идёт накопление большого файла
        QString filePath;
        QString content;               // накопленное содержимое (без [FILE:]/[/FILE])
        int     continuations = 0;     // сколько автопродолжений уже сделано
    };

    // Общий обработчик ответа Claude для всех путей (основной + мультиагентные
    // fallback'и): выполняет [FILE:]/[DIFF:]/[MKDIR:]/[DELETE:], сохраняет в
    // историю и эмитит asyncResponseReady. При обрезанном [FILE:] блоке —
    // запускает автопродолжение вместо немедленной финализации.
    void handleClaudeCodeResponse(const QString& userInput,
                                   const QString& response,
                                   bool hadAttachments);

    static QString extractArg(const QString& input, const QStringList& prefixes);
    static WORD parseVirtualKey(const QString& name);

    ComInitializer      m_com;
    CommandRegistry     m_registry;
    KeyEmulator*        m_keyEmulator  = nullptr;
    SessionMemory*      m_memory       = nullptr;
    ClaudeApi*          m_claudeApi    = nullptr;
    OllamaApi*          m_geminiApi    = nullptr;   // Ollama — локальный LLM
    GeminiApi*          m_geminiBackup = nullptr;   // Gemini — fallback если Ollama недоступна
    ActionPredictor*    m_predictor    = nullptr;
    AutoUpdater*        m_updater      = nullptr;
    ProjectIndexer*     m_indexer      = nullptr;
    CodeActions*        m_codeActions  = nullptr;
    AttachmentsManager* m_attachments  = nullptr;
    AppLauncher         m_appLauncher;             // запуск приложений/IDE
    UserProfile*        m_profile      = nullptr;  // обучение паттернов/сценариев
    PcCommandRegistry*  m_pcCommands   = nullptr;  // голосовое управление ПК (мышь/окна/система/макросы)
    ActivityTracker*    m_activity     = nullptr;  // deep context awareness + knowledge base
    TrainingPipelineController* m_trainingPipeline = nullptr; // background voice→training data
    qint64              m_currentUserId = 1;       // active user (multi-user support)
    PendingFileGeneration m_pendingFile;            // автопродолжение больших файлов

    bool              m_multiAgentMode    = false;
    bool              m_uiEnglish         = true;   // synced from MainWindow
    bool              m_ideOpenedThisSession = false; // CLion открыт авто-режимом в этой сессии
    std::atomic<bool> m_speaking{false};
    QMutex            m_ttsMutex;

    // Сколько раз подряд можно автоматически просить Claude "продолжай файл"
    // прежде чем сохранить накопленное и сообщить пользователю предупреждение.
    // 8 × MAX_TOKENS(8192) ≈ 65K токенов — покрывает файлы ~8-10 тыс. строк.
    static constexpr int MAX_FILE_CONTINUATIONS = 8;
};