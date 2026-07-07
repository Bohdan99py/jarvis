// ============================================================
// chat_history_dialog.cpp — Chronological chat history browser
// ============================================================

#include "chat_history_dialog.h"
#include "database_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QDateTime>

ChatHistoryDialog::ChatHistoryDialog(qint64 userId, bool english, QWidget* parent)
    : QDialog(parent)
    , m_userId(userId)
    , m_english(english)
{
    setWindowTitle(m_english ? QStringLiteral("Chat History") : QStringLiteral("История чатов"));
    resize(760, 520);

    auto* layout = new QVBoxLayout(this);

    auto* title = new QLabel(m_english
        ? QStringLiteral("Browse past conversations by date — no need to remember a keyword.")
        : QStringLiteral("Просмотр прошлых разговоров по датам — не нужно вспоминать ключевое слово."),
        this);
    title->setWordWrap(true);
    layout->addWidget(title);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    m_sessionList = new QListWidget(splitter);
    m_sessionList->setMinimumWidth(220);

    m_transcript = new QTextEdit(splitter);
    m_transcript->setReadOnly(true);

    splitter->addWidget(m_sessionList);
    splitter->addWidget(m_transcript);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    layout->addWidget(splitter, 1);

    connect(m_sessionList, &QListWidget::currentRowChanged,
            this, &ChatHistoryDialog::onSessionSelected);

    reload();
}

void ChatHistoryDialog::reload()
{
    m_sessionList->clear();
    m_sessionIds.clear();

    const auto sessions = DatabaseManager::instance().getSessions(m_userId, 200);
    for (const auto& row : sessions) {
        const QString sid      = row.value(QStringLiteral("session_id"));
        const QString lastAt   = row.value(QStringLiteral("last_at"));
        const QString msgCount = row.value(QStringLiteral("msg_count"));

        const QDateTime dt = QDateTime::fromString(lastAt, Qt::ISODate);
        const QString label = (dt.isValid() ? dt.toString(QStringLiteral("yyyy-MM-dd")) : sid)
            + QStringLiteral("  —  ")
            + (m_english ? QStringLiteral("%1 messages").arg(msgCount)
                         : QStringLiteral("сообщений: %1").arg(msgCount));

        m_sessionIds.append(sid);
        m_sessionList->addItem(label);
    }

    if (m_sessionList->count() == 0) {
        m_transcript->setPlainText(m_english
            ? QStringLiteral("No chat history yet.")
            : QStringLiteral("История чатов пока пуста."));
    } else {
        m_sessionList->setCurrentRow(0);
    }
}

void ChatHistoryDialog::onSessionSelected(int row)
{
    if (row < 0 || row >= m_sessionIds.size()) return;

    const QString sid = m_sessionIds[row];
    const auto messages = DatabaseManager::instance().getSession(sid);

    QString text;
    for (const auto& m : messages) {
        const QString who = (m.role == QStringLiteral("user"))
            ? (m_english ? QStringLiteral("You") : QStringLiteral("Вы"))
            : (m.role == QStringLiteral("assistant")
                   ? QStringLiteral("J.A.R.V.I.S.")
                   : m.role);
        const QString ts = m.createdAt.isValid()
            ? m.createdAt.toString(QStringLiteral("HH:mm"))
            : QString();
        text += QStringLiteral("[%1] %2: %3\n\n").arg(ts, who, m.content);
    }
    m_transcript->setPlainText(text);
}
