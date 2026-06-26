// ============================================================
// proactive_reminder_manager.cpp — Contextual Reminder Engine
// ============================================================
#include "proactive_reminder_manager.h"
#include "j2j_telegram_gateway.h"
#include "voice_synthesis_manager.h"

#include <QRegularExpression>
#include <QDebug>

// ============================================================
//  Singleton
// ============================================================

ProactiveReminderManager& ProactiveReminderManager::instance()
{
    static ProactiveReminderManager inst;
    return inst;
}

ProactiveReminderManager::ProactiveReminderManager(QObject* parent)
    : QObject(parent)
{}

ProactiveReminderManager::~ProactiveReminderManager()
{
    for (auto& r : m_reminders) {
        if (r.timer) {
            r.timer->stop();
            delete r.timer;
        }
    }
    m_reminders.clear();
}

// ============================================================
//  Intent detection from conversation
// ============================================================

bool ProactiveReminderManager::tryDetectAndSchedule(qint64 chatId,
                                                     const QString& userMessage,
                                                     bool english)
{
    const QString lower = userMessage.toLower().trimmed();

    static const QRegularExpression reRuReminder(
        QStringLiteral("(?:напомни|не забыть|надо будет|нужно будет|"
                       "через \\d+\\s*мин|не забудь)"));
    static const QRegularExpression reEnReminder(
        QStringLiteral("(?:remind me|don't forget|need to|"
                       "in \\d+\\s*min|set a reminder)"),
        QRegularExpression::CaseInsensitiveOption);

    bool matched = english ? reEnReminder.match(lower).hasMatch()
                           : reRuReminder.match(lower).hasMatch();
    if (!matched) return false;

    int delayMin = detectDelayMinutes(lower);
    if (delayMin <= 0)
        delayMin = 30;

    addReminder(chatId, userMessage, delayMin, english);
    return true;
}

int ProactiveReminderManager::detectDelayMinutes(const QString& text)
{
    static const QRegularExpression reMinutes(
        QStringLiteral("(?:через|in)\\s*(\\d+)\\s*(?:мин|min)"),
        QRegularExpression::CaseInsensitiveOption);
    auto m = reMinutes.match(text);
    if (m.hasMatch())
        return m.captured(1).toInt();

    static const QRegularExpression reHours(
        QStringLiteral("(?:через|in)\\s*(\\d+)\\s*(?:час|hour|hr)"),
        QRegularExpression::CaseInsensitiveOption);
    m = reHours.match(text);
    if (m.hasMatch())
        return m.captured(1).toInt() * 60;

    return -1;
}

// ============================================================
//  Manual reminder registration
// ============================================================

int ProactiveReminderManager::addReminder(qint64 chatId,
                                           const QString& text,
                                           int delayMinutes,
                                           bool english)
{
    ScheduledReminder r;
    r.chatId = chatId;
    r.text   = text;
    r.fireAt = QDateTime::currentDateTime().addSecs(delayMinutes * 60);

    const QString cleanedTask = text.left(100).trimmed();
    r.spokenText = english
        ? QStringLiteral("Hey, you wanted to: %1. Don't forget!").arg(cleanedTask)
        : QStringLiteral("Хей, ты хотел: %1. Не забыл?").arg(cleanedTask);

    r.timer = new QTimer(this);
    r.timer->setSingleShot(true);
    r.timer->setInterval(delayMinutes * 60 * 1000);

    const int idx = m_reminders.size();
    m_reminders.append(r);

    connect(r.timer, &QTimer::timeout, this, [this, idx]() {
        fireReminder(idx);
    });

    r.timer->start();

    qDebug() << "[ReminderManager] Scheduled reminder #" << idx
             << "for chat" << chatId
             << "in" << delayMinutes << "min:"
             << text.left(60);

    return idx;
}

// ============================================================
//  Fire!
// ============================================================

void ProactiveReminderManager::fireReminder(int index)
{
    if (index < 0 || index >= m_reminders.size()) return;

    const ScheduledReminder& r = m_reminders[index];

    const QString msg = QStringLiteral("⏰ *Напоминание:*\n%1").arg(r.text);

    if (m_gateway && r.chatId != 0)
        m_gateway->sendMessage(r.chatId, msg);

    VoiceSynthesisManager::instance().say(r.spokenText);

    emit reminderFired(r.chatId, r.text);

    qDebug() << "[ReminderManager] Fired reminder #" << index
             << "for chat" << r.chatId;

    if (m_reminders[index].timer) {
        m_reminders[index].timer->deleteLater();
        m_reminders[index].timer = nullptr;
    }
}
