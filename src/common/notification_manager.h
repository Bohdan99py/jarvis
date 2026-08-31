#pragma once
// -------------------------------------------------------
// notification_manager.h — Неоновые toast-уведомления
//
// NotificationManager::instance().showNotification(
//     "System Update", "Database migration successful!");
//
// NotificationManager::instance().askQuestion(
//     "J.A.R.V.I.S.", "Как продвигается проект?",
//     [](const QString& answer) { ... remember it ... },
//     {"Да", "Нет"});
//
// Every toast is its own frameless, translucent, always-on-top
// QQuickView window — one per notification, stacked in the
// bottom-right corner. All visual design (glass panel, rounded
// corners, blurred neon breathing border, the answer field) lives
// in NotificationToast.qml; this header only owns lifecycle,
// thread-safety and screen positioning.
//
// NB: this is QQuickView (a QWindow), not QQuickWidget. A standalone,
// parentless, translucent, frameless top-level QQuickWidget does not
// reliably composite on Windows — its RHI surface negotiates its own
// format separately from the QWidget's WA_TranslucentBackground
// attribute, so it can render fully but never blend against the desktop.
// QQuickWidget is designed for *embedding* Quick content inside a normal
// widget hierarchy (see TaskBoard.qml/TrainingCenter.qml/UserCenter.qml,
// which are all embedded in a QDialog and render fine) — QQuickView is
// Qt's actual supported primitive for a standalone top-level Quick popup.
// -------------------------------------------------------

#include <QObject>
#include <QQuickView>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>

class QTimer;

// ── Одно всплывающее окошко ─────────────────────────────
class NotificationToast : public QQuickView
{
    Q_OBJECT
    // QWindow has x/y as separate properties but no combined QPoint one —
    // this lets slideIn()/glideTo() keep animating a single "pos"
    // property exactly like the old QWidget-based version did.
    Q_PROPERTY(QPoint pos READ position WRITE setPosition)
public:
    enum class Level { Info, Success, Warning, Error, Question };

    NotificationToast(const QString& title, const QString& message, Level level,
                      const QStringList& quickOptions = {},
                      bool answerable = false,
                      std::function<void(const QString&)> onAnswer = nullptr);

    void slideIn(const QPoint& target);   // первое появление
    void glideTo(const QPoint& target);   // плавная перестановка в столбике
    void dismiss();                       // запустить растворение

    // Вызывается из QML (см. NotificationToast.qml, context property "toast")
    Q_INVOKABLE void submitAnswer(const QString& text);
    Q_INVOKABLE void requestDismiss();

signals:
    void closed(NotificationToast* toast);

protected:
    bool event(QEvent* e) override; // QWindow has no enterEvent/leaveEvent virtuals

private:
    QTimer* m_lifeTimer  = nullptr;
    bool    m_dismissing = false;
    std::function<void(const QString&)> m_onAnswer;
};

// ── Синглтон-менеджер ───────────────────────────────────
class NotificationManager : public QObject
{
    Q_OBJECT
public:
    using Level = NotificationToast::Level;

    static NotificationManager& instance();

    // Насколько JARVIS вправе отвлекать. Ставится профилем (режимом):
    // Focus глушит всё, кроме предупреждений, Gaming — вообще всё.
    // Вопросы (askQuestion) пропускаются везде, кроме None: вопрос —
    // это не фоновый шум, а ожидание ответа.
    enum class Policy { All, Minimal, None };

    void   setPolicy(Policy p) { m_policy = p; }
    Policy policy() const      { return m_policy; }

    static QString policyName(Policy p);
    static Policy  policyFromString(const QString& name, Policy fallback = Policy::All);

    // Потокобезопасно: из фонового потока вызов перекидывается в GUI-поток.
    void showNotification(const QString& title, const QString& message,
                          Level level = Level::Info);

    // Тост с полем свободного ответа (+ опциональные кнопки быстрого
    // ответа). onAnswer вызывается ровно один раз в GUI-потоке с тем,
    // что пользователь напечатал или на что нажал.
    void askQuestion(const QString& title, const QString& question,
                     std::function<void(const QString&)> onAnswer,
                     const QStringList& quickOptions = {});

private:
    NotificationManager() = default;

    void spawnToast(const QString& title, const QString& message, Level level,
                    const QStringList& quickOptions, bool answerable,
                    std::function<void(const QString&)> onAnswer);
    void layoutToasts(NotificationToast* newcomer);

    QList<QPointer<NotificationToast>> m_toasts;
    Policy m_policy = Policy::All;
};
