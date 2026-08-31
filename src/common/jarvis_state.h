#pragma once
// -------------------------------------------------------
// jarvis_state.h — Одно состояние на всю систему
//
// До сих пор «чем занят JARVIS» существовало в трёх местах и в
// трёх видах: ChatController::busy для поля ввода, statusTone для
// цвета точки, и отдельно — анимация ядра, которая крутилась
// независимо от того, происходит что-нибудь на самом деле или нет.
// Три источника правды означают, что они расходятся: ввод уже
// разблокирован, точка ещё «думает», ядро крутится третьи сутки.
//
// Здесь состояние одно, и оно фазовое:
//
//     IDLE ──> THINKING ──> EXECUTING ──> VERIFYING ──> IDLE
//                 ^              │
//                 └──────────────┘   (модель просит ещё шаг)
//
// Кто пишет: агентный цикл, голос, сценарии, триггеры.
// Кто читает: QML, лента, уведомления, анимации.
//
// Фаза несёт с собой две вещи, без которых она бесполезна:
//   • activity — ЧТО именно сейчас делается («Открываю Unreal»),
//     иначе EXECUTING ничем не лучше вращающегося кружка;
//   • elapsed — сколько это уже длится. Отсчёт ведётся здесь, а не
//     в каждом потребителе: иначе «18 секунд» в подписи и «5 секунд»
//     в тултипе будут двумя разными таймерами про одно событие.
// -------------------------------------------------------

#include "jarvis_core_export.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class QTimer;

// ============================================================
//  Фаза
// ============================================================
enum class JarvisPhase {
    Idle = 0,     // ничего не происходит
    Listening,    // микрофон открыт, ждём речь
    Thinking,     // ждём ответ модели
    Planning,     // строим план (пока не используется — место под goal-based)
    Executing,    // выполняется инструмент
    Verifying,    // проверяем, что инструмент действительно сработал
    Waiting,      // ждём человека: подтверждение, ответ на вопрос
    Recovering,   // пробуем обходной путь после неудачи
    Error         // сорвалось, ждём следующей команды
};

// ============================================================
//  JarvisState
// ============================================================
class JARVIS_CORE_EXPORT JarvisState : public QObject
{
    Q_OBJECT

public:
    // Синглтон по той же причине, что EventFeed и NotificationManager:
    // фазу выставляют из агента, голоса, сценариев и триггеров, и
    // протаскивать указатель через полпроекта ради этого не стоит.
    static JarvisState& instance();

    JarvisPhase phase()     const { return m_phase; }
    QString     phaseName() const { return phaseName(m_phase); }
    QString     activity()  const { return m_activity; }

    // Занят = что-то происходит прямо сейчас и человеку стоит подождать.
    // Listening сюда не входит: ждёт как раз JARVIS, а не человек.
    bool isBusy() const;

    // Сколько длится текущая фаза. В Idle — 0.
    int     elapsedMs()   const;
    QString elapsedText() const;   // "18s", "2m 04s"; в Idle — пусто

    // Сколько инструментов отработало в текущем прогоне агента.
    // Обнуляется входом в Idle или Error.
    int toolsRun() const { return m_toolsRun; }

    static QString phaseName(JarvisPhase p);

public slots:
    // Главный вход. Смена фазы перезапускает отсчёт времени; повторный
    // вход в ТУ ЖЕ фазу с тем же текстом не делает ничего — иначе
    // "думаю" на каждой итерации агента сбрасывало бы секундомер и
    // «сколько уже идёт» показывало бы вечные 0 секунд.
    void enter(JarvisPhase phase, const QString& activity = QString());

    // Сменить подпись, не трогая фазу и секундомер: EXECUTING переходит
    // от инструмента к инструменту, оставаясь одним и тем же ожиданием.
    void setActivity(const QString& activity);

    void noteToolRun();

    // Микрофон — не отдельная фаза, а модификатор: слушать во время
    // EXECUTING можно, и сбивать выполнение на LISTENING нельзя.
    // Поэтому Listening выставляется только поверх Idle.
    void setListening(bool on);

    void toIdle() { enter(JarvisPhase::Idle); }

signals:
    // int, а не enum: так сигнал доступен из QML без регистрации типа.
    void phaseChanged(int phase, const QString& phaseName);
    void activityChanged(const QString& activity);

    // Раз в секунду, пока фаза занятая. Отдельно от changed(), потому
    // что перерисовывать по нему нужно только счётчик времени.
    void elapsedChanged(int ms);

    // Любое изменение — для подписчиков, которым не важно, какое именно.
    void changed();

private:
    explicit JarvisState(QObject* parent = nullptr);

    void updateTimer();

    JarvisPhase   m_phase = JarvisPhase::Idle;
    QString       m_activity;
    QElapsedTimer m_since;
    QTimer*       m_tick     = nullptr;
    int           m_toolsRun = 0;
    bool          m_listening = false;
};
