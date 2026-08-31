// -------------------------------------------------------
// notifications_dialog.cpp — см. notifications_dialog.h
// -------------------------------------------------------

#include "notifications_dialog.h"

#include "event_feed.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QString colorFor(EventLevel level)
{
    switch (level) {
    case EventLevel::Good:    return QStringLiteral("#00e676");
    case EventLevel::Warning: return QStringLiteral("#ffb300");
    case EventLevel::Error:   return QStringLiteral("#ff5252");
    case EventLevel::Info:
    default:                  return QStringLiteral("#8fa7bf");
    }
}

} // namespace

NotificationsDialog::NotificationsDialog(bool english, QWidget* parent)
    : QDialog(parent)
    , m_english(english)
{
    setWindowTitle(english ? QStringLiteral("Notifications")
                           : QStringLiteral("Центр уведомлений"));
    resize(760, 560);
    buildUi();

    connect(&EventFeed::instance(), &EventFeed::changed,
            this, &NotificationsDialog::reload);
}

void NotificationsDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    auto* topRow = new QHBoxLayout();

    m_onlyImportant = new QCheckBox(
        m_english ? QStringLiteral("Only warnings and errors")
                  : QStringLiteral("Только важное"), this);
    connect(m_onlyImportant, &QCheckBox::toggled, this, &NotificationsDialog::reload);
    topRow->addWidget(m_onlyImportant);

    topRow->addStretch(1);

    auto* clearBtn = new QPushButton(
        m_english ? QStringLiteral("Clear") : QStringLiteral("Очистить"), this);
    connect(clearBtn, &QPushButton::clicked, this, []() { EventFeed::instance().clear(); });
    topRow->addWidget(clearBtn);

    root->addLayout(topRow);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setWordWrap(true);
    root->addWidget(m_list, 1);

    m_empty = new QLabel(
        m_english ? QStringLiteral("Nothing has happened yet.")
                  : QStringLiteral("Пока ничего не происходило."), this);
    m_empty->setAlignment(Qt::AlignCenter);
    root->addWidget(m_empty);

    setStyleSheet(QStringLiteral(R"(
        QDialog   { background-color: #080a12; }
        QLabel    { color: #3a4a5e; font-family: "Segoe UI", sans-serif; }
        QCheckBox { color: #8fa7bf; font-family: "Segoe UI", sans-serif; }
        QPushButton {
            background-color: rgba(255, 255, 255, 10);
            border: 1px solid rgba(0, 212, 255, 60);
            border-radius: 7px;
            color: #c0d8ee;
            padding: 6px 16px;
        }
        QPushButton:hover { border: 1px solid #00d4ff; }
        QListWidget {
            background-color: rgba(255, 255, 255, 6);
            border: 1px solid rgba(0, 212, 255, 35);
            border-radius: 10px;
            color: #c0d8ee;
            font-family: "Consolas", "Segoe UI", monospace;
            font-size: 13px;
        }
        QListWidget::item { padding: 7px 6px; }
    )"));
}

void NotificationsDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    reload();
    EventFeed::instance().markAllRead();
}

void NotificationsDialog::reload()
{
    if (!m_list)
        return;

    const bool onlyImportant = m_onlyImportant && m_onlyImportant->isChecked();

    m_list->clear();

    // Новые сверху: лента читается сверху вниз, а интересно прежде
    // всего последнее.
    const QVector<FeedEvent> all = EventFeed::instance().events();
    int shown = 0;
    for (int i = all.size() - 1; i >= 0; --i) {
        const FeedEvent& e = all[i];
        if (onlyImportant
            && e.level != EventLevel::Warning && e.level != EventLevel::Error)
            continue;

        QString text = QStringLiteral("%1   %2   %3")
                           .arg(e.timeText(), e.source.leftJustified(9), e.title);
        if (e.count > 1)
            text += QStringLiteral("   ×%1").arg(e.count);
        if (!e.detail.isEmpty())
            text += QChar('\n') + QStringLiteral("                    ") + e.detail;

        auto* item = new QListWidgetItem(text, m_list);
        item->setForeground(QColor(colorFor(e.level)));
        item->setFlags(Qt::NoItemFlags);
        ++shown;
    }

    m_empty->setVisible(shown == 0);
    m_list->setVisible(shown > 0);
}
