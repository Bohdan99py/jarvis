#pragma once
// ============================================================
// proactive_reminder_manager.h — Contextual Reminder Engine
//
// Detects future intents in conversation and schedules
// non-blocking background timers. On timeout, proactively
// pings the user via Telegram and speaks aloud.
// ============================================================

#include <QObject>
#include <QString>
#include <QTimer>
#include <QList>
#include <QDateTime>

class J2JTelegramGateway;

struct ScheduledReminder {
    QString  text;
    QString  spokenText;
    qint64   chatId     = 0;
    QDateTime fireAt;
    QTimer*  timer      = nullptr;
};

class ProactiveReminderManager : public QObject
{
    Q_OBJECT

public:
    static ProactiveReminderManager& instance();

    ProactiveReminderManager(const ProactiveReminderManager&)            = delete;
    ProactiveReminderManager& operator=(const ProactiveReminderManager&) = delete;

    void setTelegramGateway(J2JTelegramGateway* gw) { m_gateway = gw; }

    // Detects reminder intent in free text ("remind me in 30 min",
    // "напомни через 30 минут"...) and schedules it if found. Returns the
    // human-readable confirmation to show the user, or an empty string if
    // no reminder intent was detected.
    QString tryDetectAndSchedule(qint64 chatId,
                                 const QString& userMessage,
                                 bool english);

    int addReminder(qint64 chatId,
                    const QString& text,
                    int delayMinutes,
                    bool english);

    int activeCount() const { return m_reminders.size(); }

signals:
    void reminderFired(qint64 chatId, const QString& text);

private:
    explicit ProactiveReminderManager(QObject* parent = nullptr);
    ~ProactiveReminderManager() override;

    void fireReminder(int index);

    static int detectDelayMinutes(const QString& text);

    J2JTelegramGateway*     m_gateway = nullptr;
    QList<ScheduledReminder> m_reminders;
};
