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
// QQuickWidget window — one per notification, stacked in the
// bottom-right corner. All visual design (glass panel, rounded
// corners, blurred neon breathing border, the answer field) lives
// in NotificationToast.qml; this header only owns lifecycle,
// thread-safety and screen positioning.
// -------------------------------------------------------

#include <QObject>
#include <QQuickWidget>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>

class QTimer;

// ── Одно всплывающее окошко ─────────────────────────────
class NotificationToast : public QQuickWidget
{
    Q_OBJECT
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
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

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
};
