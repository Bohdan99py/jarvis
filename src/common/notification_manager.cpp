// -------------------------------------------------------
// notification_manager.cpp — Неоновые toast-уведомления
// -------------------------------------------------------

#include "notification_manager.h"
#include "lang.h"

#include <QApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTimer>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QScreen>
#include <QThread>

namespace {
constexpr int kMargin          = 18;    // отступ от краёв рабочей области экрана
constexpr int kSpacing         = 10;    // между тостами в столбике
constexpr int kLifetimeMs      = 5000;  // обычное уведомление
constexpr int kQuestionLifeMs  = 25000; // на вопрос нужно время, чтобы напечатать ответ
constexpr int kSlideMs         = 380;
constexpr int kFadeMs          = 450;
constexpr int kMaxToasts       = 5;

QColor levelAccent(NotificationToast::Level level)
{
    switch (level) {
    case NotificationToast::Level::Success:  return QColor(0x00, 0xe6, 0x76);
    case NotificationToast::Level::Warning:  return QColor(0xff, 0xb3, 0x00);
    case NotificationToast::Level::Error:    return QColor(0xff, 0x52, 0x52);
    case NotificationToast::Level::Question: return QColor(0x7c, 0x4d, 0xff);
    case NotificationToast::Level::Info:
    default:                                 return QColor(0x00, 0xd4, 0xff);
    }
}
}

// ============================================================
// NotificationToast
// ============================================================

NotificationToast::NotificationToast(const QString& title, const QString& message,
                                     Level level, const QStringList& quickOptions,
                                     bool answerable,
                                     std::function<void(const QString&)> onAnswer)
    : QQuickWidget()
    , m_onAnswer(std::move(onAnswer))
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose);
    setClearColor(Qt::transparent);
    setResizeMode(QQuickWidget::SizeViewToRootObject);

    // windeployqt drops QML plugin dependencies (QtQuick, QtQuick.Effects)
    // into bin/qml/ next to the exe — that path isn't part of the engine's
    // default import search, so it has to be added explicitly.
    engine()->addImportPath(QCoreApplication::applicationDirPath()
                            + QStringLiteral("/qml"));

    QQmlContext* ctx = rootContext();
    ctx->setContextProperty(QStringLiteral("toastTitle"), title);
    ctx->setContextProperty(QStringLiteral("toastMessage"), message);
    ctx->setContextProperty(QStringLiteral("toastAccentColor"), levelAccent(level));
    ctx->setContextProperty(QStringLiteral("toastAnswerable"), answerable);
    ctx->setContextProperty(QStringLiteral("toastQuickOptions"), quickOptions);
    ctx->setContextProperty(QStringLiteral("toastPlaceholder"),
        IS_EN ? QStringLiteral("Type your answer...") : QStringLiteral("Ответь мне..."));
    ctx->setContextProperty(QStringLiteral("toast"), this);

    setSource(QUrl(QStringLiteral("qrc:/qml/NotificationToast.qml")));

    m_lifeTimer = new QTimer(this);
    m_lifeTimer->setSingleShot(true);
    m_lifeTimer->setInterval(answerable ? kQuestionLifeMs : kLifetimeMs);
    connect(m_lifeTimer, &QTimer::timeout, this, &NotificationToast::dismiss);
}

void NotificationToast::slideIn(const QPoint& target)
{
    move(target + QPoint(0, 48));
    setWindowOpacity(0.0);
    show();

    auto* group = new QParallelAnimationGroup(this);

    auto* pos = new QPropertyAnimation(this, "pos", group);
    pos->setDuration(kSlideMs);
    pos->setEndValue(target);
    pos->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(pos);

    auto* fade = new QPropertyAnimation(this, "windowOpacity", group);
    fade->setDuration(kSlideMs);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    group->addAnimation(fade);

    group->start(QAbstractAnimation::DeleteWhenStopped);
    m_lifeTimer->start();
}

void NotificationToast::glideTo(const QPoint& target)
{
    if (m_dismissing) return;
    auto* pos = new QPropertyAnimation(this, "pos", this);
    pos->setDuration(220);
    pos->setEndValue(target);
    pos->setEasingCurve(QEasingCurve::OutCubic);
    pos->start(QAbstractAnimation::DeleteWhenStopped);
}

void NotificationToast::dismiss()
{
    if (m_dismissing) return;
    m_dismissing = true;
    m_lifeTimer->stop();

    auto* group = new QParallelAnimationGroup(this);

    auto* pos = new QPropertyAnimation(this, "pos", group);
    pos->setDuration(kFadeMs);
    pos->setEndValue(this->pos() + QPoint(0, 24));
    pos->setEasingCurve(QEasingCurve::InCubic);
    group->addAnimation(pos);

    auto* fade = new QPropertyAnimation(this, "windowOpacity", group);
    fade->setDuration(kFadeMs);
    fade->setEndValue(0.0);
    group->addAnimation(fade);

    connect(group, &QParallelAnimationGroup::finished, this, [this]() {
        emit closed(this);
        close();   // WA_DeleteOnClose → deleteLater
    });
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void NotificationToast::submitAnswer(const QString& text)
{
    if (m_dismissing) return;
    if (m_onAnswer) m_onAnswer(text);
    dismiss();
}

void NotificationToast::requestDismiss()
{
    dismiss();
}

void NotificationToast::enterEvent(QEnterEvent* /*event*/)
{
    m_lifeTimer->stop();   // пока мышь над тостом — не исчезает
}

void NotificationToast::leaveEvent(QEvent* /*event*/)
{
    if (!m_dismissing) m_lifeTimer->start();
}

// ============================================================
// NotificationManager
// ============================================================

NotificationManager& NotificationManager::instance()
{
    static NotificationManager mgr;
    return mgr;
}

void NotificationManager::showNotification(const QString& title, const QString& message,
                                           Level level)
{
    spawnToast(title, message, level, {}, false, nullptr);
}

void NotificationManager::askQuestion(const QString& title, const QString& question,
                                      std::function<void(const QString&)> onAnswer,
                                      const QStringList& quickOptions)
{
    spawnToast(title, question, Level::Question, quickOptions, true, std::move(onAnswer));
}

void NotificationManager::spawnToast(const QString& title, const QString& message, Level level,
                                     const QStringList& quickOptions, bool answerable,
                                     std::function<void(const QString&)> onAnswer)
{
    if (!qApp) return;

    // GUI-объекты можно создавать только в главном потоке
    if (QThread::currentThread() != qApp->thread()) {
        QMetaObject::invokeMethod(qApp,
            [this, title, message, level, quickOptions, answerable, onAnswer]() mutable {
                spawnToast(title, message, level, quickOptions, answerable, std::move(onAnswer));
            }, Qt::QueuedConnection);
        return;
    }

    m_toasts.removeAll(nullptr);

    // Столбик не растёт бесконечно — старейший уходит досрочно
    while (m_toasts.size() >= kMaxToasts) {
        if (NotificationToast* oldest = m_toasts.first().data())
            oldest->dismiss();
        m_toasts.removeFirst();
    }

    auto* toast = new NotificationToast(title, message, level, quickOptions,
                                        answerable, std::move(onAnswer));
    connect(toast, &NotificationToast::closed, this, [this](NotificationToast* t) {
        m_toasts.removeAll(t);
        layoutToasts(nullptr);
    });
    m_toasts.append(toast);

    // QQuickWidget's SizeViewToRootObject resize happens once the scene
    // graph has processed the QML root item's implicit size — not
    // synchronously within the constructor. Give it one event-loop tick
    // before reading height() to position the slide-in target.
    QPointer<NotificationToast> guard(toast);
    QTimer::singleShot(0, this, [this, guard]() {
        if (guard) layoutToasts(guard);
    });
}

void NotificationManager::layoutToasts(NotificationToast* newcomer)
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    const QRect area = screen->availableGeometry();

    // Новейший — внизу у угла, остальные сдвигаются вверх
    int y = area.bottom() - kMargin;
    for (int i = m_toasts.size() - 1; i >= 0; --i) {
        NotificationToast* t = m_toasts[i].data();
        if (!t) continue;
        y -= t->height();
        const QPoint target(area.right() - kMargin - t->width(), y);
        if (t == newcomer) t->slideIn(target);
        else               t->glideTo(target);
        y -= kSpacing;
    }
}
