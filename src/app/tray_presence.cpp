// -------------------------------------------------------
// tray_presence.cpp
// -------------------------------------------------------

#include "tray_presence.h"

#include "jarvis.h"
#include "jarvis_state.h"
#include "context_advisor.h"
#include "database_manager.h"
#include "event_feed.h"
#include "mode_manager.h"
#include "workflow_manager.h"
#include "system_monitor.h"
#include "lang.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>

namespace {

QString phaseDot(JarvisPhase p)
{
    switch (p) {
    case JarvisPhase::Idle:       return QStringLiteral("○");
    case JarvisPhase::Listening:  return QStringLiteral("◉");
    case JarvisPhase::Error:      return QStringLiteral("✕");
    case JarvisPhase::Waiting:    return QStringLiteral("◐");
    default:                      return QStringLiteral("●");
    }
}

QString levelMark(EventLevel level)
{
    switch (level) {
    case EventLevel::Good:    return QStringLiteral("✓");
    case EventLevel::Warning: return QStringLiteral("!");
    case EventLevel::Error:   return QStringLiteral("✕");
    default:                  return QStringLiteral("·");
    }
}

} // namespace

TrayPresence::TrayPresence(Jarvis* core, QObject* parent)
    : QObject(parent)
    , m_core(core)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    QIcon icon(QStringLiteral(":/jarvis.ico"));
    if (icon.isNull())
        icon = QIcon(QStringLiteral(":/jarvis.png"));
    if (icon.isNull())
        icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);

    m_tray = new QSystemTrayIcon(icon, this);
    m_menu = new QMenu();

    // Пересборка на показ, а не на каждое изменение: меню видно доли
    // секунды в час, а фаза и лента меняются постоянно.
    connect(m_menu, &QMenu::aboutToShow, this, &TrayPresence::rebuildMenu);

    m_tray->setContextMenu(m_menu);

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            emit openWindowRequested();
    });

    // Подсказка — единственное, что человек видит, не открывая меню.
    // Поэтому её держим свежей: фаза и лента событий.
    connect(&JarvisState::instance(), &JarvisState::changed,
            this, &TrayPresence::refreshTooltip);
    connect(&EventFeed::instance(), &EventFeed::changed,
            this, &TrayPresence::refreshTooltip);

    refreshTooltip();
    m_tray->show();
}

TrayPresence::~TrayPresence()
{
    // QMenu — виджет, родителем ему QObject быть не может, поэтому
    // владение ручное. Иконка снимается явно: иначе на выходе она
    // остаётся висеть в трее до наведения мышью.
    if (m_tray)
        m_tray->hide();
    delete m_menu;
}

bool TrayPresence::isAvailable() const
{
    return m_tray != nullptr;
}

QString TrayPresence::statusLine() const
{
    JarvisState& st = JarvisState::instance();

    QString line = phaseDot(st.phase()) + QLatin1Char(' ');
    line += st.phaseName();

    // Фаза без подписи бесполезна ровно так же, как вращающийся
    // кружок: «Выполняю» ничем не лучше «Занят», пока не сказано что.
    if (!st.activity().isEmpty())
        line += QStringLiteral(" — ") + st.activity();

    const QString elapsed = st.elapsedText();
    if (!elapsed.isEmpty())
        line += QStringLiteral(" (") + elapsed + QLatin1Char(')');

    return line;
}

void TrayPresence::refreshTooltip()
{
    if (!m_tray)
        return;

    QString tip = QStringLiteral("J.A.R.V.I.S.\n") + statusLine();

    const int unread = EventFeed::instance().unread();
    if (unread > 0) {
        tip += QLatin1Char('\n');
        tip += IS_EN ? QStringLiteral("%1 new events").arg(unread)
                     : QStringLiteral("новых событий: %1").arg(unread);
    }

    m_tray->setToolTip(tip);
}

void TrayPresence::rebuildMenu()
{
    if (!m_menu)
        return;

    m_menu->clear();

    // ── Состояние ────────────────────────────────────────────
    QAction* status = m_menu->addAction(statusLine());
    status->setEnabled(false);

    if (SystemMonitor* mon = m_core ? m_core->systemMonitor() : nullptr) {
        QAction* load = m_menu->addAction(
            QStringLiteral("CPU %1%   RAM %2%")
                .arg(mon->cpuPercent())
                .arg(mon->ramPercent()));
        load->setEnabled(false);
    }

    m_menu->addSeparator();

    // ── Спросить ─────────────────────────────────────────────
    QAction* ask = m_menu->addAction(
        IS_EN ? QStringLiteral("Ask JARVIS\tCtrl+Space")
              : QStringLiteral("Спросить\tCtrl+Space"));
    connect(ask, &QAction::triggered, this, &TrayPresence::askRequested);

    // ── Режимы ───────────────────────────────────────────────
    if (ModeManager* modes = m_core ? m_core->modeManager() : nullptr) {
        const QVector<ModeInfo> all = modes->modes();
        if (!all.isEmpty()) {
            QMenu* sub = m_menu->addMenu(IS_EN ? QStringLiteral("Mode")
                                               : QStringLiteral("Режим"));
            const QString activeId = modes->activeMode().id;

            for (const ModeInfo& mode : all) {
                const QString label = mode.icon.isEmpty()
                    ? mode.displayName(IS_EN)
                    : mode.icon + QLatin1Char(' ') + mode.displayName(IS_EN);

                QAction* act = sub->addAction(label);
                act->setCheckable(true);
                act->setChecked(mode.id == activeId);

                const QString id = mode.id;
                connect(act, &QAction::triggered, this, [modes, id]() {
                    modes->activate(id);
                });
            }
        }
    }

    // ── Сценарии ─────────────────────────────────────────────
    // Тот же WorkflowManager, что у голоса и у триггеров: меню — ещё
    // один вызывающий, а не ещё одна реализация.
    if (WorkflowManager* wf = m_core ? m_core->workflows() : nullptr) {
        const QStringList names = wf->names();
        if (!names.isEmpty()) {
            QMenu* sub = m_menu->addMenu(IS_EN ? QStringLiteral("Run")
                                               : QStringLiteral("Сценарий"));
            for (const QString& name : names) {
                QAction* act = sub->addAction(name);
                act->setEnabled(!wf->isRunning());
                connect(act, &QAction::triggered, this, [wf, name]() {
                    wf->run(name);
                });
            }
        }
    }

    // ── Что случилось ────────────────────────────────────────
    const QVector<FeedEvent> events = EventFeed::instance().events(kRecentEvents);
    if (!events.isEmpty()) {
        const int unread = EventFeed::instance().unread();
        const QString title = unread > 0
            ? (IS_EN ? QStringLiteral("Events (%1 new)").arg(unread)
                     : QStringLiteral("События (%1 новых)").arg(unread))
            : (IS_EN ? QStringLiteral("Events") : QStringLiteral("События"));

        QMenu* sub = m_menu->addMenu(title);

        // Свежие сверху: в ленте новые в конце, в меню взгляд начинается
        // с первой строки.
        for (int i = events.size() - 1; i >= 0; --i) {
            const FeedEvent& e = events.at(i);
            QString label = QStringLiteral("%1 %2  %3")
                                .arg(e.timeText(), levelMark(e.level), e.title);
            if (e.count > 1)
                label += QStringLiteral("  ×%1").arg(e.count);

            QAction* act = sub->addAction(label);
            act->setEnabled(false);
        }

        sub->addSeparator();
        QAction* seen = sub->addAction(IS_EN ? QStringLiteral("Mark all read")
                                             : QStringLiteral("Отметить прочитанным"));
        connect(seen, &QAction::triggered, this, []() {
            EventFeed::instance().markAllRead();
        });
    }

    m_menu->addSeparator();

    // Выключатель проактивности держим здесь, а не в настройках окна:
    // когда подсказка не вовремя, до неё должно быть два клика, а не
    // «открыть окно, найти вкладку».
    if (ContextAdvisor* advisor = m_core ? m_core->contextAdvisor() : nullptr) {
        QAction* act = m_menu->addAction(IS_EN ? QStringLiteral("Context hints")
                                               : QStringLiteral("Подсказки по контексту"));
        act->setCheckable(true);
        act->setChecked(advisor->isEnabled());
        connect(act, &QAction::triggered, this, [advisor](bool on) {
            advisor->setEnabled(on);
            DatabaseManager::instance().setConfig(QStringLiteral("context_advisor"), on);
        });
    }

    QAction* open = m_menu->addAction(IS_EN ? QStringLiteral("Open JARVIS")
                                            : QStringLiteral("Открыть JARVIS"));
    connect(open, &QAction::triggered, this, &TrayPresence::openWindowRequested);

    QAction* quit = m_menu->addAction(IS_EN ? QStringLiteral("Quit")
                                            : QStringLiteral("Выход"));
    connect(quit, &QAction::triggered, this, &TrayPresence::quitRequested);
}
