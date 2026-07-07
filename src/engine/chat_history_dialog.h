#pragma once
// ============================================================
// chat_history_dialog.h — Chronological chat history browser
//
// Complements SearchRouter's keyword search: lets the user page
// through past conversations by date instead of having to guess
// a word Jarvis will match.
// ============================================================

#include <QDialog>
#include <QListWidget>
#include <QTextEdit>

class ChatHistoryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ChatHistoryDialog(qint64 userId, bool english, QWidget* parent = nullptr);

private slots:
    void onSessionSelected(int row);

private:
    void reload();

    qint64        m_userId;
    bool          m_english;
    QListWidget*  m_sessionList = nullptr;
    QTextEdit*    m_transcript  = nullptr;
    QStringList   m_sessionIds;
};
