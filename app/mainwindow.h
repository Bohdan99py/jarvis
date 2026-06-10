#pragma once
// -------------------------------------------------------
// mainwindow.h — Главное окно J.A.R.V.I.S.
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

// ── Новые модули ──────────────────────────────────────────
#include "brain.h"
#include "applauncher.h"
#include "systemcontroller.h"
#include "languagedetector.h"
// fileviewer.h подключается только в .cpp (тяжёлый Qt-виджет,
// не нужен в заголовке — избегаем лишних зависимостей)

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

    // ── Обработка команд (новые) ───────────────────────────
    // Возвращает true если команда обработана и не нужно идти в Claude
    bool tryOpenApp(const QString& userText, const Intent& intent);
    bool trySystemControl(const QString& userText);

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

    bool                    m_vibeCodingMode  = false;

    QSystemTrayIcon*        m_trayIcon        = nullptr;

    // ── Новые члены ───────────────────────────────────────
    AppLauncher             m_appLauncher;      // умный запуск приложений
    LanguageDetector        m_langDetector;     // авто-определение языка
};