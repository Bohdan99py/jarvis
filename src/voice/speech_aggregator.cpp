// ============================================================
// speech_aggregator.cpp — Collapsing bursts of events into one line
// ============================================================
#include "speech_aggregator.h"

#include <QDebug>
#include <QTimer>

SpeechAggregator::SpeechAggregator(QObject* parent)
    : QObject(parent)
{
}

bool SpeechAggregator::shouldAggregate(const SpeechRequest& req)
{
    if (req.aggregateGroup.isEmpty() || req.aggregateWindowMs <= 0)
        return false;

    // Критическое идёт мимо очереди склейки — задержать его на полторы
    // секунды ради аккуратности формулировки нельзя.
    return req.priority != SpeechPriority::Critical;
}

void SpeechAggregator::absorb(const SpeechRequest& req)
{
    // say() зовут из сетевых потоков; таблица всплесков и таймеры
    // остаются в потоке агрегатора, поэтому синхронизировать нечего.
    QMetaObject::invokeMethod(this, [this, req]() {
        absorbHere(req);
    }, Qt::QueuedConnection);
}

void SpeechAggregator::absorbHere(const SpeechRequest& req)
{
    const QString group = req.aggregateGroup;

    auto it = m_bursts.find(group);
    if (it == m_bursts.end()) {
        Burst burst;
        burst.winner = req;
        burst.count  = 1;
        m_bursts.insert(group, burst);

        // Один одноразовый таймер на всплеск. Окно отсчитывается от
        // первого события и не сдвигается — см. заголовок.
        QTimer::singleShot(req.aggregateWindowMs, this, [this, group]() {
            flush(group);
        });
        return;
    }

    ++it->count;

    // Побеждает более важное, при равенстве — пришедшее позже.
    if (static_cast<int>(req.priority) >= static_cast<int>(it->winner.priority))
        it->winner = req;
}

void SpeechAggregator::flush(const QString& group)
{
    const auto it = m_bursts.constFind(group);
    if (it == m_bursts.constEnd())
        return;

    const Burst burst = it.value();
    m_bursts.remove(group);

    if (burst.count > 1) {
        qDebug() << "[SpeechAggregator]" << burst.count << "events in group"
                 << group << "→" << burst.winner.text.left(60);
    }

    emit ready(burst.winner, burst.count);
}

void SpeechAggregator::discardPending()
{
    // Как и absorb(): зовут снаружи, работаем у себя.
    QMetaObject::invokeMethod(this, [this]() {
        if (m_bursts.isEmpty())
            return;
        qDebug() << "[SpeechAggregator] dropped" << m_bursts.size() << "pending group(s)";
        m_bursts.clear();
    }, Qt::QueuedConnection);
}
