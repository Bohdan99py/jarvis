#pragma once
// -------------------------------------------------------
// mainwindow.h — Главное окно J.A.R.V.I.S.
// ИЗМЕНЕНИЯ:
//   - Убран m_vibeCodingMode и всё связанное с вайбкодингом
//   - Добавлен LearnedCommands (самообучение)
//   - Добавлен ScreenAgent (зрение + клики)
//   - Добавлен Gemini API key в меню Settings
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

class Jarvis;
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

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
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

    void onAsyncResponse(const QString& response);
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

    // Fine-tuning: лайк и экспорт
    void onLikeLastResponse();
    void onExportTrainingData();

private:
    void buildUI();
    void buildMenuBar();
    void appendLog(const QString& who, const QString& text, const QString& color);
    void setThinkingState(bool thinking);
    void rebuildAttachmentsBar();

    void showClarification(const QString& question, const QStringList& options);
    void hideClarification();

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
    QTextEdit*              m_log        = nullptr;
    QLineEdit*              m_input      = nullptr;
    QLabel*                 m_dot        = nullptr;
    QLabel*                 m_status     = nullptr;
    QLabel*                 m_agentLabel = nullptr;
    QTimer*                 m_pulseTimer = nullptr;
    bool                    m_pulse      = false;

    VirtualKeyboardWidget*  m_keyboard    = nullptr;
    QWidget*                m_kbContainer = nullptr;
    QPropertyAnimation*     m_kbAnim      = nullptr;
    bool                    m_kbVisible   = false;

    QWidget*                m_suggestionBar  = nullptr;
    QLabel*                 m_suggestionText = nullptr;
    QPushButton*            m_suggestionBtn  = nullptr;
    QString                 m_pendingSuggestionAction;

    QWidget*                m_clarifyBar     = nullptr;
    QLabel*                 m_clarifyText    = nullptr;
    QHBoxLayout*            m_clarifyBtnLay  = nullptr;
    QString                 m_pendingInput;

    QWidget*                m_updateBar       = nullptr;
    QLabel*                 m_updateLabel     = nullptr;
    QPushButton*            m_updateBtn       = nullptr;
    QPushButton*            m_updateDismiss   = nullptr;
    QProgressBar*           m_updateProgress  = nullptr;

    QWidget*                m_attachBar       = nullptr;
    QHBoxLayout*            m_attachLayout    = nullptr;
    QScrollArea*            m_attachScroll    = nullptr;
    QLabel*                 m_attachSummary   = nullptr;
    QPushButton*            m_attachBtn       = nullptr;

    QSystemTrayIcon*        m_trayIcon        = nullptr;

    // ── Новые модули ──────────────────────────────────────
    AppLauncher             m_appLauncher;
    LanguageDetector        m_langDetector;
    LearnedCommands*        m_learnedCmds  = nullptr;  // самообучение
    ScreenAgent*            m_screenAgent  = nullptr;  // зрение + клики
    ScreenshotLearner*      m_appLearner   = nullptr;  // паттерны использования ПК
    QString                 m_lastUserInput;               // для самообучения

    // Голосовой ввод
    VoiceInput*             m_voiceInput   = nullptr;
    QPushButton*            m_micBtn       = nullptr;
    bool                    m_voiceActive  = false;

    // Fine-tuning: лайк последнего ответа
    QPushButton*            m_likeBtn      = nullptr;
    QString                 m_lastAiResponse;   // последний ответ AI для сохранения
    QString                 m_lastAiModel;      // модель которая ответила
    QString                 m_lastSessionId;    // сессия
    int                     m_trainingCount = 0; // счётчик лайков в этой сессии

    // Пассивная запись голоса → датасет
    PassiveListener*        m_passiveListener = nullptr;
    QAction*                m_passiveAction   = nullptr;  // пункт меню вкл/выкл

    // Флаги для определения источника ввода (голос/текст)
    bool                    m_lastInputWasVoice = false;
    QString                 m_lastVoiceLanguage;

    // Авто-скриншот для датасета
    QTimer*                 m_screenshotTimer  = nullptr;

    // Тема интерфейса: 0=dark, 1=glass, 2=light
    int                     m_themeIndex = 0;
};