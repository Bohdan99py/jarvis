// -------------------------------------------------------
// event_feed.cpp — см. event_feed.h
// -------------------------------------------------------

#include "event_feed.h"

#include "notification_manager.h"

#include <QDebug>

QString eventLevelName(EventLevel level)
{
    switch (level) {
    case EventLevel::Info:    return QStringLiteral("info");
    case EventLevel::Good:    return QStringLiteral("ok");
    case EventLevel::Warning: return QStringLiteral("warning");
    case EventLevel::Error:   return QStringLiteral("error");
    }
    return QStringLiteral("info");
}

EventFeed& EventFeed::instance()
{
    static EventFeed feed;
    return feed;
}

void EventFeed::post(const QString& source,
                     EventLevel level,
                     const QString& title,
                     const QString& detail,
                     const QString& dedupKey)
{
    const QString cleanTitle = QString(title).replace(QChar('\n'), QChar(' ')).trimmed();
    if (cleanTitle.isEmpty())
        return;

    // Без явного ключа считаем событием "то же самое" совпадение
    // источника и заголовка: этого хватает, чтобы серия одинаковых
    // ошибок не расползлась на двадцать строк.
    const QString key = dedupKey.isEmpty() ? source + QChar('|') + cleanTitle : dedupKey;
    const QDateTime now = QDateTime::currentDateTime();

    for (int i = m_events.size() - 1; i >= 0; --i) {
        FeedEvent& existing = m_events[i];
        if (existing.dedupKey != key)
            continue;
        if (existing.at.secsTo(now) > kDedupWindowSec)
            break;   // события отсортированы, дальше только старее

        existing.count++;
        existing.at     = now;
        existing.detail = detail.isEmpty() ? existing.detail : detail;
        emit eventUpdated(existing);
        emit changed();
        return;
    }

    FeedEvent e;
    e.id       = m_nextId++;
    e.at       = now;
    e.source   = source.toUpper();
    e.level    = level;
    e.title    = cleanTitle;
    e.detail   = detail;
    e.dedupKey = key;

    m_events.append(e);
    while (m_events.size() > kMaxEvents)
        m_events.removeFirst();

    ++m_unread;

    // Тостом всплывает только то, ради чего стоит отвлекать. Info и
    // Good копятся в ленте: их читают, когда сами захотят.
    // NotificationManager дополнительно фильтрует по политике профиля,
    // так что в Focus не пролезет и предупреждение.
    if (level == EventLevel::Warning || level == EventLevel::Error) {
        NotificationManager::instance().showNotification(
            e.source, detail.isEmpty() ? cleanTitle : cleanTitle + QStringLiteral("\n") + detail,
            level == EventLevel::Error ? NotificationToast::Level::Error
                                       : NotificationToast::Level::Warning);
    }

    qDebug() << "[Feed]" << e.source << eventLevelName(level) << cleanTitle;

    emit eventPosted(e);
    emit changed();
}

QVector<FeedEvent> EventFeed::events(int limit) const
{
    if (limit <= 0 || m_events.size() <= limit)
        return m_events;
    return m_events.mid(m_events.size() - limit);
}

void EventFeed::markAllRead()
{
    if (m_unread == 0)
        return;
    m_unread = 0;
    emit changed();
}

void EventFeed::clear()
{
    m_events.clear();
    m_unread = 0;
    emit changed();
}

QString EventFeed::summaryForModel(int maxEvents) const
{
    if (m_events.isEmpty())
        return QString();

    QStringList lines;
    const int from = qMax(0, m_events.size() - maxEvents);
    for (int i = from; i < m_events.size(); ++i) {
        const FeedEvent& e = m_events[i];
        QString line = QStringLiteral("%1 [%2/%3] %4")
                           .arg(e.timeText(), e.source, eventLevelName(e.level), e.title);
        if (e.count > 1)
            line += QStringLiteral(" (x%1)").arg(e.count);
        lines << line;
    }
    return lines.join(QChar('\n'));
}
