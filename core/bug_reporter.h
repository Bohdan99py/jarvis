#pragma once
// =============================================================================
// bug_reporter.h — Баг-репортер через GitHub Issues
// =============================================================================
// Помощь → Сообщить о баге → описание + скриншот → открывает GitHub Issues
// с предзаполненным текстом в браузере.
// =============================================================================

#include <QDialog>
#include <QString>
#include <QPixmap>

class QTextEdit;
class QLabel;
class QPushButton;
class QProgressBar;

class BugReporter : public QDialog {
    Q_OBJECT
public:
    explicit BugReporter(QWidget* parent = nullptr);

    // Открыть диалог
    static void showDialog(QWidget* parent);

private slots:
    void onAttachScreenshot();
    void onTakeScreenshot();
    void onSend();
    void onClearAttachment();

private:
    void setupUi();

    QTextEdit*   m_textEdit       = nullptr;
    QLabel*      m_previewLbl     = nullptr;
    QPushButton* m_sendBtn        = nullptr;
    QPushButton* m_attachBtn      = nullptr;
    QPushButton* m_screenshotBtn  = nullptr;
    QPushButton* m_clearBtn       = nullptr;
    QProgressBar* m_progress      = nullptr;
    QLabel*      m_statusLbl      = nullptr;

    QPixmap      m_attachment;
};