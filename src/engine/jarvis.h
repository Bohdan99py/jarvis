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
#include <QImage>
#include <QMutex>
#include <windows.h>
#include <objbase.h>
#include <atomic>

#include "command_registry.h"
#include "skill_manager.h"
#include "mode_manager.h"
#include "applauncher.h"

struct DbTask;

class KeyEmulator;
class SessionMemory;
class ClaudeApi;
class OllamaApi;
class ActionPredictor;
class AutoUpdater;
class ProjectIndexer;
class ProjectProfile;
class DevAdvisor;
class CodeActions;
class AttachmentsManager;
class UserProfile;
class ActivityTracker;
class PcCommandRegistry;
class ToolRegistry;
class ContextTracker;
class ContextAdvisor;
class WorkflowManager;
class TriggerEngine;
class HealthCenter;
class ClipboardWatcher;
class VoiceInput;
class PassiveListener;
class AudioManager;
class ProjectRegistry;
class PluginBridge;
struct ProjectEntry;
class GlobalSearch;
class SystemMonitor;
class SystemWatcher;
class DeviceHub;
struct SearchHit;
class PermissionGate;
class AgentLoop;
class TrainingPipelineController;
class J2JMeshConnector;
class TranslationEngine;
class PersonalityEngine;
class ReflectionEngine;
class LocalTrainer;
class BackgroundLearner;
class CaseDistiller;
class SecurityCamera;
class Esp32HubManager;
class QTimer;
struct OrganizePlan;

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
    // replyToMessageId: incoming Telegram message's reply_to_message.
    // message_id (0 if none) — passed to CuriosityEngine::consumeAnswer so
    // an explicit reply to a proactive question is recognized regardless
    // of elapsed time.
    QString processCommand(const QString& input,
                           const QString& attachmentBlock = QString(),
                           const QString& langInstruction = QString(),
                           qint64 chatId = 0,
                           qint64 replyToMessageId = 0);

    void speakAsync(const QString& text);
    bool isSpeaking() const { return m_speaking.load(); }

    // Diagram rendering — shared between GUI and Telegram paths.
    // Checks if an LLM response contains a diagram (<diagram> tags
    // or ASCII art in code blocks), renders it to a QImage, and
    // returns the cleaned text separately.
    struct DiagramResult {
        bool       hasDiagram = false;
        QImage     image;             // raster fallback
        QByteArray svgData;           // preferred: vector SVG content
        QString    mermaidSource;     // original source for interactive parsing
        QString    textWithoutDiagram;
    };
    static DiagramResult tryRenderDiagram(const QString& llmResponse);

    // Returns true if the user query looks like a request for a
    // visual diagram/schema. Used to inject Mermaid instructions
    // into the LLM system prompt.
    static bool needsVisualExplanation(const QString& input);

    KeyEmulator*        keyEmulator()        const { return m_keyEmulator; }
    SessionMemory*      memory()             const { return m_memory; }
    ClaudeApi*          claudeApi()          const { return m_claudeApi; }
    // Single shared instance — desktop UI (Guard menu) and the Telegram
    // /security command both arm/wire THIS object rather than each
    // constructing their own, so one real motion event can't produce two
    // independent alerts from two cameras watching the same webcam.
    SecurityCamera*     securityCamera()     const { return m_securityCamera; }
    OllamaApi*          ollamaApi()          const { return m_ollamaApi; }  // m_ollamaApi хранит OllamaApi
    ActionPredictor*    actionPredictor()    const { return m_predictor; }
    AutoUpdater*        autoUpdater()        const { return m_updater; }
    ProjectIndexer*     projectIndexer()     const { return m_indexer; }
    ProjectProfile*     projectProfile()     const { return m_projectProfile; }
    DevAdvisor*         devAdvisor()         const { return m_devAdvisor; }
    CodeActions*        codeActions()        const { return m_codeActions; }
    AttachmentsManager* attachments()        const { return m_attachments; }
    UserProfile*        userProfile()        const { return m_profile; }
    PcCommandRegistry*  pcCommands()         const { return m_pcCommands; }
    ActivityTracker*    activityTracker()    const { return m_activity; }
    J2JMeshConnector*   meshConnector()      const { return m_mesh; }
    TranslationEngine*  translationEngine()  const { return m_translator; }
    PersonalityEngine*  personalityEngine()  const { return m_personality; }
    ReflectionEngine*   reflectionEngine()   const { return m_reflection; }
    SkillManager*       skillManager()       const { return m_skills; }
    ModeManager*        modeManager()        const { return m_modes; }
    Esp32HubManager*    esp32Hub()           const { return m_esp32Hub; }

    // ── Слой действий ────────────────────────────────────────────
    // ToolRegistry описывает, что JARVIS умеет делать с машиной;
    // PermissionGate решает, что можно молча, а что — только с
    // подтверждением; AgentLoop крутит "модель просит инструмент →
    // выполняем → отдаём результат модели".
    ToolRegistry*       tools()              const { return m_tools; }
    PermissionGate*     permissions()        const { return m_permissions; }
    AgentLoop*          agent()              const { return m_agent; }

    // «Здесь» и «это»: активное чужое окно, открытый файл, проект.
    ContextTracker*     contextTracker()     const { return m_context; }

    // Право заговорить первым про то, что человек делает прямо сейчас.
    ContextAdvisor*     contextAdvisor()     const { return m_advisor; }

    // Именованные цепочки инструментов: "Development", "Gaming".
    WorkflowManager*    workflows()          const { return m_workflows; }

    // Правила "когда X — сделай Y": события, процессы, устройства,
    // расписание. Здесь же живёт планировщик — это один из видов условия.
    TriggerEngine*      triggers()           const { return m_triggers; }

    // Состояние собственных подсистем: пробы регистрируют владельцы
    // объектов, а владелец всего перечисленного здесь — ядро.
    HealthCenter*       health()             const { return m_health; }

    // ── Слух ─────────────────────────────────────────────────────
    // Говорить ядро умело и раньше (VoiceSynthesisManager — синглтон),
    // а слышать — нет: микрофон принадлежал главному окну и умирал
    // вместе с ним. Теперь оба конца разговора живут здесь, и голос
    // работает при закрытом окне. UI подписывается на сигналы этих
    // объектов, но не владеет ими и не создаёт их заново.
    VoiceInput*         voiceInput()         const { return m_voiceIn; }
    PassiveListener*    passiveListener()    const { return m_passive; }
    AudioManager*       audio()              const { return m_audio; }

    // Запуск распознавания отделён от конструктора намеренно: Vosk
    // при первом запуске просит доустановить модели, и просьба уходит
    // сигналом setupRequired. Позови это после того, как интерфейс
    // (если он вообще есть) успел подписаться — иначе диалог показать
    // будет некому. Повторные вызовы игнорируются.
    void startVoice();

    // Проекты пользователя: путь, IDE, команды сборки. Открытие
    // переключает корень индексатора, а вместе с ним и «этот проект»
    // для git, контекста и советника.
    ProjectRegistry*    projects()           const { return m_projects; }

    // Реестр фразовых команд. Публичный ради PluginBridge: плагин
    // регистрирует команды через хост, а хост живёт рядом, в engine.
    CommandRegistry*    commandRegistry()          { return &m_registry; }

    // Ctrl+K: мгновенный локальный поиск по приложениям, файлам,
    // инструментам, сценариям, профилям, истории и памяти.
    GlobalSearch*       search()             const { return m_search; }

    // Один монитор на приложение: фоном опрашивает редко, открытая
    // панель просит чаще (см. SystemMonitorDialog).
    SystemMonitor*      systemMonitor()      const { return m_sysMonitor; }

    // Общий взгляд на устройства: сам ПК, ESP32-нода, соседи по mesh,
    // Bluetooth. Источники регистрирует Jarvis — хаб про них не знает.
    DeviceHub*          devices()            const { return m_devices; }

    // Выполняет действие результата поиска. Живёт в Jarvis, а не в
    // GlobalSearch: запуск приложения, инструмента и сценария — это
    // уже существующие подсистемы, поисковый слой про них не знает.
    // Возвращает текст для лога (пусто = делать нечего).
    QString activateSearchHit(const SearchHit& hit);

    // Применяет системную часть профиля (разрешения, уведомления,
    // громкость, стартовый сценарий). Публичный, потому что UI умеет
    // переключать режим напрямую через ModeManager.
    void applyModeSystemProfile(const QString& modeId, const QString& previousId);

    // Явный запуск агента: Command Palette, кнопка «выполнить», голос.
    // Ответ придёт через asyncResponseReady(), шаги — сигналами AgentLoop.
    void runAgentTask(const QString& request, bool recordUserMessage = true);

    // ── Распознанная речь ────────────────────────────────────────
    // Единственная дверь для всего, что услышал микрофон: кнопка,
    // активационное слово, push-to-talk. Раньше речь шла напрямую в
    // слот главного окна, и без окна её никто не подхватывал —
    // JARVIS слышал, но выполнить не мог.
    //
    // Кто исполняет — решает ядро. Если есть интерактивная сессия,
    // отдаём ей: там панели вопросов, подтверждения и прикреплённые
    // файлы, и дублировать этот разговор в ядре незачем. Сессии нет —
    // выполняет агент. Дверь при этом одна в обоих случаях.
    using VoiceHandler = std::function<bool(const QString& text, const QString& lang)>;
    void setVoiceHandler(VoiceHandler handler) { m_voiceHandler = std::move(handler); }
    void submitVoiceCommand(const QString& text, const QString& lang);

    // Просьба ДЕЙСТВОВАТЬ, а не поговорить? Используется в
    // processCommand, чтобы не гнать обычную беседу через инструменты.
    static bool looksActionable(const QString& input);

    void setAgentEnabled(bool on) { m_agentEnabled = on; }
    bool agentEnabled() const     { return m_agentEnabled; }

    // FileOrganizer facade — keeps SystemController's full type (defined
    // in pc_controller.h) out of callers like MainWindow/J2JTelegramGateway,
    // which already pull in an unrelated, same-named SystemController from
    // common/systemcontroller.h (a real ODR collision if both headers land
    // in the same translation unit).
    static bool organizePathAllowed(const QString& path);
    QString     organizeApplyPlan(const OrganizePlan& plan);
    bool        organizeUndoLast();

    // Multi-user: switch active user (all data scoped to userId)
    void setCurrentUserId(qint64 id) { m_currentUserId = id; }
    qint64 currentUserId() const     { return m_currentUserId; }

    // Мультиагентный режим: true = Claude для кода, Ollama для бесед
    void setMultiAgentMode(bool enabled);
    bool multiAgentMode() const { return m_multiAgentMode; }

    // Синхронизация языка UI — нужна потому что gUiLanguage() (lang.h)
    // это static inline и в MSVC JarvisCore.lib имеет свой экземпляр,
    // независимый от app/mainwindow.cpp. Без явной передачи IS_EN в core
    // всегда возвращает Russian (дефолт), даже если в UI выбран English.
    void setUiLanguage(bool english);
    bool uiEnglish() const           { return m_uiEnglish; }

    // Следующий processCommand — ответ пользователя на ЭТОТ наш вопрос.
    // Ставит тот, кто уже погасил pending-состояние CuriosityEngine сам:
    // десктопная панель ответа делает consumeAnswer(explicitReply) до
    // вызова ядра, и без этой подсказки ядро об ответе уже не узнает.
    void noteAnsweringQuestion(const QString& question) { m_answeringQuestion = question; }

    // Язык КАННЫХ фраз, которые дописываются к ответу модели. Модель
    // отвечает на языке пользователя (LanguageDetector), а m_uiEnglish —
    // это переключатель интерфейса, поэтому английская врезка над русским
    // ответом выглядела как баг. Смотрим на сам текст; если по нему судить
    // нельзя (цифры, код) — падаем обратно на язык интерфейса.
    bool replyEnglish(const QString& sample) const;

    // When true, asyncResponseReady was triggered by a Telegram request,
    // not local GUI input. MainWindow uses this to avoid showing
    // duplicate responses (Telegram responses appear via conversationResponse signal).
    void setTelegramOrigin(bool v) { m_telegramOrigin = v; }
    bool isTelegramOrigin() const  { return m_telegramOrigin; }

    // Синхронизировать данные индексатора с SessionMemory (system prompt)
    void syncProjectInfoToMemory();

    // Task Manager — exposed for voice/text commands
    qint64 addTask(const QString& title,
                   const QString& category = QStringLiteral("General"),
                   const QString& priority = QStringLiteral("Medium"),
                   const QDateTime& deadline = QDateTime());
    bool   updateTaskStatus(qint64 taskId, const QString& newStatus);
    QString getOverdueTasksSummary() const;
    // Raw list (task ids) behind getOverdueTasksSummary() — used by
    // MainWindow's periodic re-check timer to notify once per task
    // instead of re-announcing the same overdue task every cycle.
    QList<DbTask> getOverdueTasks(int withinHours = 24) const;

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
    // speechText — готовая реплика для TTS (снятый [SPEECH:] маркер).
    // Раньше движок озвучивал её сам, а MainWindow вдобавок выводил
    // собственную из уже очищенного текста — ответ читался дважды, начиная
    // с одного и того же места. Теперь говорит только MainWindow: он один
    // знает про speechAllowed(). Аргумент со значением по умолчанию —
    // остальные emit-точки и подключённые лямбды остаются без изменений.
    void asyncResponseReady(const QString& response,
                            const QString& speechText = QString());
    void asyncResponseError(const QString& error);
    void suggestionAvailable(const QString& description, const QString& action);
    void attachmentsConsumed();
    void agentSelected(const QString& agentName);
    void ideOpened(const QString& message);   // JARVIS открыл проект в IDE
    // Фоновый советник: применил мелкую правку сам либо нашёл что-то
    // важное. Отдельный сигнал, а не asyncResponseReady: это не ответ
    // на реплику пользователя и озвучивать его не нужно.
    void advisorMessage(const QString& message);
    void meshEvent(const QString& message);  // J2J mesh network event
    void diagramRendered(const QImage& image); // diagram extracted and rendered from LLM response

private:
    void registerCommands();

    // Источники для Ctrl+K. Каждый обязан отвечать мгновенно —
    // провайдеры вызываются на каждое нажатие клавиши.
    void registerSearchProviders();

    // Источники устройств для DeviceHub + инструменты list_devices /
    // device_command. Здесь, а не в agent: ESP32 и mesh живут в network.
    void registerDeviceProviders();

    // Пробы для HealthCenter — все здесь, включая голос и микрофон:
    // объекты, которые они проверяют, теперь тоже живут в ядре.
    void registerHealthProbes();

    // === Контекст для Claude (RAG) ===
    static QStringList extractKeywords(const QString& input);
    QString buildProjectContext(const QString& userQuery) const;

    // === Роутинг агентов ===
    bool routeToClaude(const QString& input, const QString& attachmentBlock) const;

    // === Системные команды реестра ===
    QString cmdSetApiKey(const QString& input);
    QString cmdSetOllamaModel(const QString& input);

    // Пытается ответить из локальной памяти при отказе сети.
    // true — ответ отдан (asyncResponseReady), ошибку показывать не нужно.
    bool    emitOfflineAnswer(const QString& query);

    // ── Ответ без сети ───────────────────────────────────────────────
    // Локальные слои (кэш кейсов, граф синапсов, поведенческие паттерны)
    // работают ДО обращения к сети и с жёсткими порогами: пока интернет
    // есть, лучше уйти в Claude, чем выдать слабое совпадение. Когда сеть
    // отвалилась, эта же логика повторяется с ослабленными порогами —
    // слабый ответ по делу полезнее строки "Network error", а выбора всё
    // равно нет. Пустая строка = локально не нашлось ничего.
    QString buildOfflineAnswer(const QString& query) const;

    // Единая точка обработки сбоя API: сначала пробует ответить локально
    // (с явной пометкой, что это офлайн и ответ может быть неточным), и
    // только если нечего — отдаёт ошибку, но с перечнем того, что
    // работает без сети, а не с сырым текстом сбоя.
    void respondOfflineOrError(const QString& query, const QString& apiError);
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
    QString cmdAdvisorReport(const QString& input);
    QString cmdUndoEdits(const QString& input);
    QString cmdEditHistory(const QString& input);
    QString cmdAdvisorScan(const QString& input);
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

    // === Дозапрос контекста ([NEED:...]) ===
    //
    // Проект целиком в промпт не влезает, а угадывать, какие три файла
    // приложить к запросу, по ключевым словам получается не всегда. Поэтому
    // модель может сама попросить недостающее: [NEED:file:src/a.cpp],
    // [NEED:symbol:Jarvis::processCommand], [NEED:grep:setProjectRoot],
    // [NEED:uses:jarvis.h], [NEED:tree:src/engine], [NEED:assets],
    // [NEED:profile]. JARVIS читает запрошенное с диска и отправляет
    // повторный запрос — пользователь видит только итоговый ответ.
    //
    // Ограничения жёсткие: не больше MAX_CONTEXT_REQUESTS запросов за
    // раунд и не больше MAX_CONTEXT_ROUNDS раундов на одно сообщение —
    // иначе модель может ходить по проекту бесконечно за деньги.
    QStringList parseContextRequests(const QString& response) const;
    QString     resolveContextRequest(const QString& request) const;
    bool        tryServeContextRequests(const QString& userInput,
                                        const QString& response,
                                        bool hadAttachments);

    static QString extractArg(const QString& input, const QStringList& prefixes);
    static WORD parseVirtualKey(const QString& name);

    ComInitializer      m_com;
    CommandRegistry     m_registry;
    KeyEmulator*        m_keyEmulator  = nullptr;
    SessionMemory*      m_memory       = nullptr;
    ClaudeApi*          m_claudeApi    = nullptr;
    OllamaApi*          m_ollamaApi    = nullptr;   // Ollama — локальный LLM
    ActionPredictor*    m_predictor    = nullptr;
    QString             m_lastFeedbackAction;
    AutoUpdater*        m_updater      = nullptr;
    ProjectIndexer*     m_indexer      = nullptr;
    ProjectProfile*     m_projectProfile = nullptr; // тип проекта, таргеты, зависимости, ассеты
    DevAdvisor*         m_devAdvisor     = nullptr; // фоновые рекомендации по проекту
    CodeActions*        m_codeActions  = nullptr;
    AttachmentsManager* m_attachments  = nullptr;
    AppLauncher         m_appLauncher;             // запуск приложений/IDE
    UserProfile*        m_profile      = nullptr;  // обучение паттернов/сценариев
    PcCommandRegistry*  m_pcCommands   = nullptr;  // голосовое управление ПК (мышь/окна/система/макросы)
    ToolRegistry*       m_tools        = nullptr;  // что JARVIS умеет делать с машиной
    PermissionGate*     m_permissions  = nullptr;  // что можно молча, а что — только с подтверждением
    AgentLoop*          m_agent        = nullptr;  // цикл "думает → делает → смотрит результат"
    ContextTracker*     m_context      = nullptr;  // что на экране прямо сейчас
    ContextAdvisor*     m_advisor      = nullptr;  // право заговорить первым
    WorkflowManager*    m_workflows    = nullptr;  // повторяемые цепочки действий
    TriggerEngine*      m_triggers     = nullptr;  // "когда X — сделай Y" + расписание
    HealthCenter*       m_health       = nullptr;  // проверка собственных подсистем
    VoiceInput*         m_voiceIn      = nullptr;  // микрофон + Vosk
    VoiceHandler        m_voiceHandler;            // интерактивная сессия, если она есть
    PassiveListener*    m_passive      = nullptr;  // фоновая запись фраз в датасет
    AudioManager*       m_audio        = nullptr;  // звуки интерфейса + «можно ли говорить»
    bool                m_voiceStarted = false;    // startVoice() уже отработал
    ClipboardWatcher*   m_clipboard    = nullptr;  // история буфера, только в памяти
    ProjectRegistry*    m_projects     = nullptr;  // список проектов + активный
    PluginBridge*       m_plugins      = nullptr;  // хост плагинов: команды + инструменты
    GlobalSearch*       m_search       = nullptr;  // Ctrl+K
    SystemMonitor*      m_sysMonitor   = nullptr;  // счётчики железа
    SystemWatcher*      m_sysWatcher   = nullptr;  // счётчики -> события
    DeviceHub*          m_devices      = nullptr;  // все устройства в одном месте
    bool                m_agentEnabled = true;
    QString             m_lastAgentRequest;        // для офлайн-фолбэка и updateContext
    ActivityTracker*    m_activity     = nullptr;  // deep context awareness + knowledge base
    TrainingPipelineController* m_trainingPipeline = nullptr; // background voice→training data
    LocalTrainer*       m_localTrainer = nullptr;  // self-tuning: bakes liked replies into an Ollama Modelfile
    QTimer*             m_autoTrainTimer = nullptr;
    // Опрос состояния Windows для политики голоса (полный экран, DND)
    QTimer*             m_voiceContextTimer = nullptr;
    BackgroundLearner*  m_backgroundLearner = nullptr; // behavior-pattern learning (was dead code — now wired in)
    CaseDistiller*      m_caseDistiller     = nullptr; // Layer 2: nightly case -> heuristic distillation
    SecurityCamera*     m_securityCamera    = nullptr; // shared by desktop Guard menu + Telegram /security
    J2JMeshConnector*   m_mesh             = nullptr;  // P2P mesh network
    TranslationEngine*  m_translator       = nullptr;  // multilingual translation + audio pipeline
    PersonalityEngine*  m_personality      = nullptr;  // emotional state + genetic trait mutation
    ReflectionEngine*   m_reflection       = nullptr;  // behavioral analysis + morning nudge
    SkillManager*       m_skills           = nullptr;  // модульные скиллы (лего-блоки знаний)
    ModeManager*        m_modes            = nullptr;  // режимы работы (профили поведения)
    Esp32HubManager*    m_esp32Hub         = nullptr;  // ESP32 physical node (sensors + LED)
    qint64              m_currentUserId = 1;       // active user (multi-user support)
    PendingFileGeneration m_pendingFile;            // автопродолжение больших файлов
    int  m_contextRounds = 0;   // сколько раз модель уже просила контекст в этом ходе

    bool              m_multiAgentMode    = false;
    bool              m_telegramOrigin    = false;
    // Layer-1/2 owner scoping for the in-flight command: 0 = desktop,
    // otherwise the Telegram chat_id. Set at the top of processCommand()
    // and read later in the async completion handler (same "member set
    // before the call, read during async callback" idiom as m_telegramOrigin
    // above — processCommand doesn't run reentrantly, so this is safe).
    qint64            m_currentChatId     = 0;
    bool              m_uiEnglish         = false;  // synced from MainWindow; default RU until setUiLanguage() runs
    // Вопрос JARVIS, на который отвечает следующее сообщение (см.
    // noteAnsweringQuestion). Живёт ровно один ход processCommand.
    QString           m_answeringQuestion;
    bool              m_ideOpenedThisSession = false; // CLion открыт авто-режимом в этой сессии
    // Зеркалит состояние очереди TTS (см. connect в конструкторе).
    std::atomic<bool> m_speaking{false};

    // Сколько раз подряд можно автоматически просить Claude "продолжай файл"
    // прежде чем сохранить накопленное и сообщить пользователю предупреждение.
    // 8 × MAX_TOKENS(8192) ≈ 65K токенов — покрывает файлы ~8-10 тыс. строк.
    static constexpr int MAX_FILE_CONTINUATIONS = 8;

    // Дозапрос контекста моделью
    static constexpr int MAX_CONTEXT_ROUNDS   = 3;
    static constexpr int MAX_CONTEXT_REQUESTS = 6;
    static constexpr int MAX_CONTEXT_CHARS    = 60000;
};
