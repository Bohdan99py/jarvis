// ============================================================
// voice_policy.cpp — When JARVIS is allowed to speak
// ============================================================
#include "voice_policy.h"

#include <QDebug>
#include <QMutexLocker>
#include <QTime>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

VoicePolicyManager& VoicePolicyManager::instance()
{
    static VoicePolicyManager inst;
    return inst;
}

// ============================================================
//  Политика
// ============================================================

void VoicePolicyManager::setPolicy(VoicePolicy p)
{
    QMutexLocker lock(&m_mutex);
    if (m_policy == p)
        return;
    m_policy = p;
    qDebug() << "[VoicePolicy] policy →" << policyName(p);
}

VoicePolicy VoicePolicyManager::policy() const
{
    QMutexLocker lock(&m_mutex);
    return m_policy;
}

QString VoicePolicyManager::policyName(VoicePolicy p)
{
    switch (p) {
    case VoicePolicy::Silent:  return QStringLiteral("silent");
    case VoicePolicy::Minimal: return QStringLiteral("minimal");
    case VoicePolicy::Normal:  return QStringLiteral("normal");
    case VoicePolicy::Verbose: return QStringLiteral("verbose");
    }
    return QStringLiteral("normal");
}

VoicePolicy VoicePolicyManager::policyFromString(const QString& name, VoicePolicy fallback)
{
    const QString n = name.trimmed().toLower();
    if (n == QStringLiteral("silent")  || n == QStringLiteral("off")
        || n == QStringLiteral("none"))                     return VoicePolicy::Silent;
    if (n == QStringLiteral("minimal") || n == QStringLiteral("quiet"))
        return VoicePolicy::Minimal;
    if (n == QStringLiteral("normal")  || n == QStringLiteral("all"))
        return VoicePolicy::Normal;
    if (n == QStringLiteral("verbose") || n == QStringLiteral("full"))
        return VoicePolicy::Verbose;
    return fallback;
}

// ============================================================
//  Контекст
// ============================================================

void VoicePolicyManager::setActivity(const QString& category)
{
    QMutexLocker lock(&m_mutex);
    if (m_context.activityCategory == category)
        return;   // отсчёт «сколько уже в этом занятии» не сбрасываем зря
    m_context.activityCategory  = category;
    m_context.activityStartedAt = QDateTime::currentDateTime();
}

void VoicePolicyManager::noteUserSpeech()
{
    QMutexLocker lock(&m_mutex);
    m_context.lastUserSpeech = QDateTime::currentDateTime();
}

void VoicePolicyManager::setNightMode(bool on)
{
    QMutexLocker lock(&m_mutex);
    m_context.nightMode = on;
}

void VoicePolicyManager::setDoNotDisturb(bool on)
{
    QMutexLocker lock(&m_mutex);
    m_context.doNotDisturb = on;
}

void VoicePolicyManager::setFullscreen(bool on)
{
    QMutexLocker lock(&m_mutex);
    m_context.fullscreen = on;
}

VoiceContext VoicePolicyManager::context() const
{
    QMutexLocker lock(&m_mutex);
    return m_context;
}

void VoicePolicyManager::setNightHours(int fromHour, int toHour)
{
    QMutexLocker lock(&m_mutex);
    m_nightFrom = fromHour;
    m_nightTo   = toHour;
}

bool VoicePolicyManager::isNightNow() const
{
    QMutexLocker lock(&m_mutex);

    if (m_context.nightMode)
        return true;
    if (m_nightFrom < 0 || m_nightTo < 0)
        return false;

    const int hour = QTime::currentTime().hour();

    // Интервал переходит через полночь: 23→7 это «час >= 23 ИЛИ < 7».
    return (m_nightFrom > m_nightTo) ? (hour >= m_nightFrom || hour < m_nightTo)
                                      : (hour >= m_nightFrom && hour < m_nightTo);
}

// Windows сам знает, что человек в полноэкранной игре или включил
// «не беспокоить». Спрашивать его дешевле и честнее, чем угадывать по
// заголовкам окон.
void VoicePolicyManager::refreshSystemState()
{
#ifdef Q_OS_WIN
    QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;
    if (FAILED(SHQueryUserNotificationState(&state)))
        return;

    const bool fullscreen = (state == QUNS_BUSY)
                         || (state == QUNS_RUNNING_D3D_FULL_SCREEN)
                         || (state == QUNS_PRESENTATION_MODE);
    const bool quiet = (state == QUNS_QUIET_TIME);

    QMutexLocker lock(&m_mutex);
    if (m_context.fullscreen != fullscreen || m_context.doNotDisturb != quiet) {
        qDebug() << "[VoicePolicy] system state: fullscreen" << fullscreen
                 << "quiet" << quiet;
    }
    m_context.fullscreen   = fullscreen;
    m_context.doNotDisturb = quiet;
#endif
}

// ============================================================
//  Решение
// ============================================================

// Потолок по выбору человека. Ниже него контекст опустить может,
// выше — нет.
VoiceDecision VoicePolicyManager::baseDecision(VoicePolicy policy, SpeechPriority priority)
{
    switch (policy) {
    case VoicePolicy::Silent:
        // Голос выключен — но о перегреве человек всё равно узнает,
        // просто глазами.
        return (priority == SpeechPriority::Critical) ? VoiceDecision::Notify
                                                       : VoiceDecision::Silent;
    case VoicePolicy::Minimal:
        if (priority == SpeechPriority::Background) return VoiceDecision::Silent;
        if (priority == SpeechPriority::Normal)     return VoiceDecision::Notify;
        return VoiceDecision::Speak;

    case VoicePolicy::Normal:
    case VoicePolicy::Verbose:
        return VoiceDecision::Speak;
    }
    return VoiceDecision::Speak;
}

VoiceDecision VoicePolicyManager::demote(VoiceDecision current, VoiceDecision floor)
{
    return (static_cast<int>(current) < static_cast<int>(floor)) ? current : floor;
}

VoiceDecision VoicePolicyManager::decide(const SpeechRequest& req) const
{
    VoicePolicy  policy;
    VoiceContext ctx;
    {
        QMutexLocker lock(&m_mutex);
        policy = m_policy;
        ctx    = m_context;
    }

    VoiceDecision decision = baseDecision(policy, req.priority);

    if (decision == VoiceDecision::Silent)
        return decision;

    // Verbose — это явное «говори всё»; обстановку в этом режиме не
    // спрашивают. Critical не приглушается ничем, кроме Silent, который
    // уже отработал выше.
    if (policy == VoicePolicy::Verbose || req.priority == SpeechPriority::Critical)
        return decision;

    const bool background = (req.priority == SpeechPriority::Background);
    const bool important  = (req.priority == SpeechPriority::Important);

    // «Не беспокоить» — это ответ человека, а не догадка системы:
    // молчим обо всём, кроме критического.
    if (ctx.doNotDisturb)
        decision = demote(decision, background ? VoiceDecision::Silent
                                                : VoiceDecision::Notify);

    // Полный экран и игра: событие видно в интерфейсе, звук туда лезть
    // не должен. Важное (сборка упала) — исключение, ради него человек
    // и держит JARVIS запущенным.
    if ((ctx.fullscreen || ctx.isGaming()) && !important)
        decision = demote(decision, background ? VoiceDecision::Silent
                                                : VoiceDecision::Notify);

    // Глубокая работа: фоновую болтовню убираем совсем, остальное
    // оставляем — прерывать работу ради дела можно, ради «пора попить
    // воды» нельзя.
    if (ctx.isDeepFocus() && background)
        decision = demote(decision, VoiceDecision::Silent);

    // Пока человек говорит — не говорим поверх. Важное подождёт своей
    // паузы в очереди, фоновое не нужно вовсе.
    if (ctx.isUserSpeaking() && !important)
        decision = demote(decision, background ? VoiceDecision::Silent
                                                : VoiceDecision::Notify);

    // Ночь: фоновое молчит, остальное звучит — но шёпотом
    // (см. applyContextStyle).
    if (isNightNow() && background)
        decision = demote(decision, VoiceDecision::Silent);

    return decision;
}

void VoicePolicyManager::applyContextStyle(SpeechRequest& req) const
{
    if (req.style == SpeechStyle::Critical)
        return;   // ночью перегрев должен разбудить, а не убаюкать

    if (isNightNow())
        req.style = SpeechStyle::Whisper;
}
