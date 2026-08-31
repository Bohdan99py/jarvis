#pragma once
// -------------------------------------------------------
// mainwindow.h — Главное окно J.A.R.V.I.S.
// ИЗМЕНЕНИЯ:
//   - Убран m_vibeCodingMode и всё связанное с вайбкодингом
//   - Добавлен LearnedCommands (самообучение)
//   - Добавлен ScreenAgent (зрение + клики)
//   - Добавлен API key в меню Settings
// -------------------------------------------------------

#include <QMainWindow>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QPropertyAnimation>
#include <QMenuBar>
#include <QProgressBar>
#include <QCloseEvent>
#include <QSystemTrayIcon>
#include <QStyle>
#include <QDateTime>
#include <QSet>

class Jarvis;
class CommandPalette;
class SystemMonitorDialog;
class NotificationsDialog;
class DeviceHubDialog;
class DashboardDialog;
class ActionRegistry;
struct AppAction;
class ActionModel;
class GlobalHotkey;
class ChatModel;
class ChatController;
class NoticeController;
class AttachmentModel;
class WelcomeController;
class VisualInsightsController;
class QQuickWidget;
struct OrganizePlan;
class VirtualKeyboardWidget;
class QScrollArea;
class QHBoxLayout;
class QDragEnterEvent;
class QDropEvent;

#include "brain.h"
#include "applauncher.h"
#include "systemcontroller.h"
#include "languagedetector.h"
#include "learned_commands.h"
#include "screen_agent.h"
#include "voice_input.h"
#include "passive_listener.h"
#include "VoskSetupDialog.h"
#include "screenshot_learner.h"
#include "activity_tracker.h"
#include "audio_manager.h"
#include "theme_manager.h"
#include "dialog_cues.h"
#include "spinner_widget.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // Ядро и реестр команд создаются в main() и живут дольше окна: окно —
    // один из интерфейсов к ним, а не владелец. Оба указателя окну не
    // принадлежат и вместе с ним не удаляются (см. main.cpp). Реестр окно
    // наполняет своими командами, помечая их собой как владельцем.
    explicit MainWindow(Jarvis* core, ActionRegistry* actions, QWidget* parent = nullptr);

    // Закрытие окна прячет его вместо выхода. Ставит main(), и только
    // если трей действительно поднялся: без трея спрятанное окно —
    // это процесс, который не вернуть.
    void setHideOnClose(bool on) { m_hideOnClose = on; }

public slots:
    // Точка входа для трея. Окно про трей ничего не знает — это он
    // просит окно показаться.
    void showAndRaise();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void keyPressEvent(QKeyEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private slots:
    void onSend();
    void onSpeakingChanged(bool speaking);
    void onTypingStarted();
    void onTypingProgress(int current, int total);
    void onTypingFinished();
    void toggleKeyboard();

    // speechText приходит от Jarvis уже готовым; пусто — выводим сами.
    void onAsyncResponse(const QString& response,
                         const QString& speechText = QString());
    void onAsyncError(const QString& error);
    void onSuggestion(const QString& description, const QString& action);

    void onAgentSelected(const QString& agentName);

    void onAttachClicked();
    void onAttachmentsChanged();
    void onAttachmentsConsumed();

    void onClarificationChoice(int choice);

    // Самообучение: подтверждение выученной команды
    void onCommandLearned(const LearnedCommand& cmd);

    // Голосовой ввод
    void onMicButtonClicked();
    void onVoiceReady();
    void onVoiceText(const QString& text, const QString& lang);
    void onWakeWord(const QString& word);
    void onWhisperMode(bool isWhisper);

    // Перебивание: пользователь заговорил поверх реплики — она должна
    // оборваться на полуслове, а не договориться до точки.
    void maybeBargeIn(float micDb);

    // Fine-tuning: лайк и экспорт
    void onLikeLastResponse();
    void onSaveInsight(const QString& suggestedName);
    void onExportTrainingData();

private:
    void buildUI();
    void buildMenuBar();
    void appendLog(const QString& who, const QString& text, const QString& color);

    // Слой действий: шаги агента видно в логе, а всё, что требует
    // разрешения, спрашивается модальным вопросом. Без этой привязки
    // PermissionGate останется неинтерактивным и будет отклонять
    // любое действие с ненулевым риском.
    void setupAgentUi();

    // Команды приложения как данные: меню, поиск Ctrl+K и ActionModel
    // для QML строятся из одного списка (см. action_registry.h).
    void registerAppActions();

    // Юридические тексты. Отдельные методы, а не лямбды в меню:
    // вдвоём они занимали в buildMenuBar под двести строк.
    void buildLanguageMenu(class QMenu* parent);
    void buildTranslationPairMenu(class QMenu* parent);
    void showAudioFileDialog();
    // Камера и охрана: разовая проводка сигналов камеры и обучение
    // чужого лица по фотографиям (владельца учит Центр зрения, с камеры).

    // Единственная точка открытия Центра зрения: он теперь и есть меню
    // камеры, поэтому подписки на его сигналы («обучить по фото»,
    // «скриншот» — их выполняет окно, а не диалог) должны ставиться
    // в одном месте, а не копироваться в каждый вызов.
    // initialTab: 0=лица, 1=камера, 2=экранное зрение.
    void openVisionCenter(int initialTab);
    void wireSecurityCameraUi();
    void enrollFaceFromPhoto();
    void takeScreenshotToArtifacts();

    void showMobileSyncDialog();
    void showWakeOnLanDialog();
    void showTelegramDialog();
    void showAnalyticsDialog();
    void showVoiceModelsDialog();

    void showEulaDialog();
    void showPrivacyDialog();

    // Ctrl+Space из любого места системы — палитра команд. Регистрируется
    // на HWND главного окна, поэтому создаётся после его показа.
    // Все команды окна регистрируются через это: обработчики захватывают
    // окно, поэтому реестр должен знать, чьи они, и выбросить их, когда
    // окна не станет (см. ActionRegistry::add).
    void addOwnedAction(const AppAction& action);
    void setThinkingState(bool thinking);
    void rebuildAttachmentsBar();

    // If a filesystem search turned up exactly one image file, also push it
    // into the attachments panel (m_visualCtl) so it is actually shown,
    // not just listed as a filename — and stays reachable via the 📎 button
    // after the FileViewer dialog is closed.
    void previewIfSingleImage(const QStringList& filePaths);

    void showWelcomeDashboard();

    // Профиль: мастер первого запуска (редактирование — UserCenterDialog)
    void runFirstRunProfileSetup();

    void showClarification(const QString& question, const QStringList& options);
    void hideClarification();

    // ── Оценка неуверенного ответа из памяти ─────────────────
    // «Верно/Неверно» на два положения не покрывали то, что происходит
    // чаще всего: ответ по теме, но не на тот вопрос. Almost = «близко,
    // спроси нормально», Wrong = «не то, сейчас объясню чем».
    // Разбор самих реплик — в DialogCues (чистые функции, покрыты тестом).
    //
    // Закрывает сомнение, обновляет связи в памяти и, если ответ не
    // подошёл, перезадаёт исходный вопрос с учётом объяснения.
    void applyDoubtVerdict(qint64 doubtId, const QString& query,
                           DialogCues::Verdict verdict,
                           const QString& explanation);

    // ── Ответ на вопрос, которым Джарвис закончил свою реплику ─
    // Вопрос в конце ответа модели («а тебе для чего схема?») ничем не
    // отличался от обычного текста: следующая реплика человека уходила в
    // роутер как новый запрос — могла совпасть с чужим кейсом и уж точно
    // теряла связь с вопросом. Запоминаем вопрос и помечаем ответ на него.
    void noteJarvisReply(const QString& reply);

    // ── "You're answering Jarvis's question" bar ──────────────
    // Jarvis asks things on his own initiative (CuriosityEngine). Desktop
    // chat has no reply-to-message gesture, so without a visible marker the
    // only way a reply could be recognised as an answer was a 15-minute
    // timer — miss it and the reply reached the LLM as a brand-new topic
    // with no sign a question had ever been asked. This bar names the
    // question being answered and routes the next message to it explicitly,
    // for as long as it stays on screen.
    void showAnswerPrompt(const QString& question);
    void hideAnswerPrompt();

    // Автономная организация файлов (FileOrganizer) — показывает план
    // и выполняет его только после подтверждения пользователем.
    void showOrganizePlanDialog(const OrganizePlan& plan);

    void showUpdateBar(const QString& version);
    void hideUpdateBar();

    void applyLanguage(bool english);
    void applyTheme(int index);
    void applyThemeStyleSheet(int index);

    bool tryOpenApp(const QString& userText, const Intent& intent);
    bool trySystemControl(const QString& userText);

    // Визуальные команды через ScreenAgent
    void handleVisualCommand(const QString& userText);

    // ── Основные виджеты ──────────────────────────────────
    Jarvis*                 m_jarvis     = nullptr;
    // Палитра и её хоткеи живут в main(), не здесь: они переживают окно.
    // Панели держим как QDialog*: конкретный тип нужен только в момент
    // создания, а общий тип позволяет открывать их одним помощником
    // вместо каста ссылки на указатель.
    QDialog*                m_monitorDialog = nullptr;
    QDialog*                m_notificationsDialog = nullptr;
    QDialog*                m_devicesDialog = nullptr;
    QDialog*                m_dashboardDialog = nullptr;

    ActionRegistry*         m_actions     = nullptr;   // создан в main(), окну не принадлежит
    ActionModel*            m_actionModel = nullptr;   // тот же список для QML

    // Главный экран целиком — один QQuickWidget с MainScreen.qml.
    // Всё, что он показывает, приходит из этих моделей и
    // контроллеров: виджетов у чата, ввода и полос больше нет.
    QQuickWidget*           m_screen     = nullptr;
    ChatModel*              m_chat       = nullptr;
    ChatController*         m_chatCtl    = nullptr;
    NoticeController*       m_noticeCtl  = nullptr;
    AttachmentModel*        m_attachModel = nullptr;
    WelcomeController*      m_welcomeCtl  = nullptr;
    VisualInsightsController* m_visualCtl = nullptr;

    // Куда ведёт кнопка полосы обновления: пусто — «скачать»,
    // заполнено — «открыть папку с загруженным файлом».
    QString                 m_downloadedUpdatePath;

    // Periodic deadline re-check — notifies once per overdue/approaching
    // task per session instead of re-announcing it every cycle.
    QTimer*                 m_deadlineTimer = nullptr;
    QSet<qint64>            m_notifiedDeadlineTaskIds;

    VirtualKeyboardWidget*  m_keyboard    = nullptr;
    QWidget*                m_kbContainer = nullptr;
    QPropertyAnimation*     m_kbAnim      = nullptr;
    bool                    m_kbVisible   = false;
    QString                 m_pendingSuggestionAction;
    QString                 m_pendingInput;

    // Answer bar (see showAnswerPrompt). m_answerQuestion is the question
    // text it is currently offering to answer — non-empty exactly while the
    // bar is up, and the flag onSend checks to route a message as an answer.
    QString                 m_answerQuestion;

    // Praise/scold feedback for the last uncertain local/cached answer
    // (see Intent::doubtId). Two redundant paths resolve the same doubt:
    // a typed confirm/deny phrase (checked in onSend) or the clarify-bar
    // buttons (see onClarificationChoice, "doubt_feedback:" prefix) —
    // whichever the user reaches for first.
    // Одноразовый обход кэша после кнопки «Неверно» — см. onClarificationChoice.
    bool                    m_bypassCacheOnce = false;
    qint64                  m_pendingDoubtId = 0;
    QDateTime               m_pendingDoubtSetAt;

    // Взводится кнопкой «Не то — объясню»: следующая реплика читается
    // целиком как объяснение, а не как новый запрос. m_doubtQuery держит
    // исходный вопрос, который надо будет перезадать.
    bool                    m_awaitingDoubtExplanation = false;
    QString                 m_doubtQuery;

    // Вопрос, которым Джарвис закончил свою последнюю реплику (см.
    // noteJarvisReply). Живёт до ответа пользователя или до таймаута.
    QString                 m_lastJarvisQuestion;
    QDateTime               m_lastJarvisQuestionAt;

    // Есть ли куда прятаться по Alt+F4 — ставится из main(), когда трей
    // действительно создался (см. setHideOnClose).
    bool                    m_hideOnClose     = false;

    // ── Новые модули ──────────────────────────────────────
    AppLauncher             m_appLauncher;
    LanguageDetector        m_langDetector;
    LearnedCommands*        m_learnedCmds  = nullptr;  // самообучение
    ScreenAgent*            m_screenAgent  = nullptr;  // зрение + клики
    class SecurityCamera*   m_securityCam  = nullptr;  // shared instance, owned by Jarvis
    bool                    m_guardUiWired = false;    // desktop Guard signal connections wired once
    ScreenshotLearner*      m_appLearner   = nullptr;  // паттерны использования ПК
    QString                 m_lastUserInput;               // для самообучения

    // Голосовой ввод. Владелец — ядро (Jarvis::voiceInput), здесь только
    // ссылка: окно подписывается на сигналы, но не создаёт и не удаляет.
    VoiceInput*             m_voiceInput   = nullptr;
    QPushButton*            m_micBtn       = nullptr;
    bool                    m_voiceActive  = false;

    // --- Перебивание речи ---
    // Порог заметно выше VAD (-42 дБ): микрофон слышит и сам JARVIS из
    // колонок, а эхо собственного голоса не должно его затыкать. При
    // громких колонках без AEC надёжный режим — наушники.
    int                     m_bargeInFrames = 0;
    static constexpr float  kBargeInDb       = -30.0f;
    // Начало реплики пропускаем: там эхо сильнее всего.
    static constexpr qint64 kBargeInGraceMs  = 400;
    // Одиночный хлопок дверью — не перебивание.
    static constexpr int    kBargeInMinFrames = 2;

    // Fine-tuning: лайк последнего ответа
    QPushButton*            m_likeBtn      = nullptr;
    QString                 m_lastAiResponse;   // последний ответ AI для сохранения
    QString                 m_lastAiModel;      // модель которая ответила
    QString                 m_lastSessionId;    // сессия
    int                     m_trainingCount = 0; // счётчик лайков в этой сессии

    // Пассивная запись голоса → датасет. Тоже принадлежит ядру.
    PassiveListener*        m_passiveListener = nullptr;

    // Флаги для определения источника ввода (голос/текст)
    bool                    m_lastInputWasVoice = false;
    QString                 m_lastVoiceLanguage;

    // Авто-скриншот для датасета
    QTimer*                 m_screenshotTimer  = nullptr;

    // Тема интерфейса: 0=Cyberpunk, 1=Soft Light, 2=Glass(legacy)
    int                     m_themeIndex = 0;

    // Audio notification system. Принадлежит ядру (Jarvis::audio).
    AudioManager*           m_audioManager  = nullptr;
    QPushButton*            m_audioModeBtn  = nullptr;

    // Visual diagram dashboard
};