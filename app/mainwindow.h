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

class Jarvis;
class VirtualKeyboardWidget;
class QScrollArea;
class QHBoxLayout;
class QDragEnterEvent;
class QDropEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent* e) override;

    // Drag-n-drop файлов (работает как прикрепление через «скрепку»)
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private slots:
    void onSend();
    void onSpeakingChanged(bool speaking);
    void onTypingStarted();
    void onTypingProgress(int current, int total);
    void onTypingFinished();
    void toggleKeyboard();

    // Мозги
    void onAsyncResponse(const QString& response);
    void onAsyncError(const QString& error);
    void onSuggestion(const QString& description, const QString& action);

    // Прикрепления
    void onAttachClicked();
    void onAttachmentsChanged();
    void onAttachmentsConsumed();

private:
    void buildUI();
    void buildMenuBar();
    void appendLog(const QString& who, const QString& text, const QString& color);
    void setThinkingState(bool thinking);
    void rebuildAttachmentsBar();

    // Обновление UI
    void showUpdateBar(const QString& version);
    void hideUpdateBar();

    Jarvis*                 m_jarvis     = nullptr;
    QTextEdit*              m_log        = nullptr;
    QLineEdit*              m_input      = nullptr;
    QLabel*                 m_dot        = nullptr;
    QLabel*                 m_status     = nullptr;
    QTimer*                 m_pulseTimer = nullptr;
    bool                    m_pulse      = false;

    VirtualKeyboardWidget*  m_keyboard    = nullptr;
    QWidget*                m_kbContainer = nullptr;
    QPropertyAnimation*     m_kbAnim      = nullptr;
    bool                    m_kbVisible   = false;

    // Панель предложений
    QWidget*                m_suggestionBar  = nullptr;
    QLabel*                 m_suggestionText = nullptr;
    QPushButton*            m_suggestionBtn  = nullptr;
    QString                 m_pendingSuggestionAction;

    // Панель обновления
    QWidget*                m_updateBar       = nullptr;
    QLabel*                 m_updateLabel     = nullptr;
    QPushButton*            m_updateBtn       = nullptr;
    QPushButton*            m_updateDismiss   = nullptr;
    QProgressBar*           m_updateProgress  = nullptr;

    // Панель прикреплений (над полем ввода)
    QWidget*                m_attachBar       = nullptr;
    QHBoxLayout*            m_attachLayout    = nullptr;
    QScrollArea*            m_attachScroll    = nullptr;
    QLabel*                 m_attachSummary   = nullptr;
    QPushButton*            m_attachBtn       = nullptr;  // скрепка в input bar

    // Вайбкодинг
    bool                    m_vibeCodingMode  = false;
};
