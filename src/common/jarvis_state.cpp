// -------------------------------------------------------
// jarvis_state.cpp — см. jarvis_state.h
// -------------------------------------------------------

#include "jarvis_state.h"

#include <QTimer>

JarvisState& JarvisState::instance()
{
    static JarvisState state;
    return state;
}

JarvisState::JarvisState(QObject* parent)
    : QObject(parent)
{
    m_tick = new QTimer(this);
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, [this]() {
        emit elapsedChanged(elapsedMs());
    });
}

QString JarvisState::phaseName(JarvisPhase p)
{
    switch (p) {
    case JarvisPhase::Idle:       return QStringLiteral("idle");
    case JarvisPhase::Listening:  return QStringLiteral("listening");
    case JarvisPhase::Thinking:   return QStringLiteral("thinking");
    case JarvisPhase::Planning:   return QStringLiteral("planning");
    case JarvisPhase::Executing:  return QStringLiteral("executing");
    case JarvisPhase::Verifying:  return QStringLiteral("verifying");
    case JarvisPhase::Waiting:    return QStringLiteral("waiting");
    case JarvisPhase::Recovering: return QStringLiteral("recovering");
    case JarvisPhase::Error:      return QStringLiteral("error");
    }
    return QStringLiteral("idle");
}

bool JarvisState::isBusy() const
{
    switch (m_phase) {
    case JarvisPhase::Thinking:
    case JarvisPhase::Planning:
    case JarvisPhase::Executing:
    case JarvisPhase::Verifying:
    case JarvisPhase::Waiting:
    case JarvisPhase::Recovering:
        return true;
    default:
        return false;
    }
}

int JarvisState::elapsedMs() const
{
    if (!m_since.isValid() || m_phase == JarvisPhase::Idle)
        return 0;
    return static_cast<int>(m_since.elapsed());
}

QString JarvisState::elapsedText() const
{
    const int ms = elapsedMs();
    if (ms <= 0)
        return QString();

    const int totalSec = ms / 1000;
    if (totalSec < 60)
        return QString::number(totalSec) + QStringLiteral("s");

    return QStringLiteral("%1m %2s")
        .arg(totalSec / 60)
        .arg(totalSec % 60, 2, 10, QLatin1Char('0'));
}

void JarvisState::updateTimer()
{
    // Тикать в Idle незачем: это будильник, который будит, чтобы
    // сообщить, что ничего не произошло.
    if (isBusy()) {
        if (!m_tick->isActive())
            m_tick->start();
    } else {
        m_tick->stop();
    }
}

void JarvisState::enter(JarvisPhase phase, const QString& activity)
{
    // Повторный вход в ту же фазу с той же подписью — это не событие.
    // Агент зовёт enter(Thinking) на каждой итерации; если сбрасывать
    // секундомер, «идёт уже 40 секунд» никогда не покажется.
    if (m_phase == phase && m_activity == activity)
        return;

    const bool phaseIsNew = (m_phase != phase);
    m_phase = phase;

    if (phaseIsNew) {
        m_since.start();
        if (phase == JarvisPhase::Idle || phase == JarvisPhase::Error)
            m_toolsRun = 0;
        emit phaseChanged(static_cast<int>(phase), phaseName(phase));
        emit elapsedChanged(0);
    }

    if (m_activity != activity) {
        m_activity = activity;
        emit activityChanged(m_activity);
    }

    updateTimer();
    emit changed();

    // Микрофон мог остаться открытым, пока агент работал: как только
    // работа кончилась, состояние обязано вернуться к «слушаю», а не
    // к «ничего не делаю».
    if (phase == JarvisPhase::Idle && m_listening)
        enter(JarvisPhase::Listening);
}

void JarvisState::setActivity(const QString& activity)
{
    if (m_activity == activity)
        return;
    m_activity = activity;
    emit activityChanged(m_activity);
    emit changed();
}

void JarvisState::noteToolRun()
{
    ++m_toolsRun;
    emit changed();
}

void JarvisState::setListening(bool on)
{
    if (m_listening == on)
        return;
    m_listening = on;

    // Слушать во время выполнения можно — прерывать выполнение ради
    // этого нельзя. Поэтому фаза меняется только на границе с покоем.
    if (on) {
        if (m_phase == JarvisPhase::Idle)
            enter(JarvisPhase::Listening);
        else
            emit changed();
    } else {
        if (m_phase == JarvisPhase::Listening)
            enter(JarvisPhase::Idle);
        else
            emit changed();
    }
}
