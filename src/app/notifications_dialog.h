#pragma once
// -------------------------------------------------------
// notifications_dialog.h — Центр уведомлений
//
// Тост живёт четыре секунды и не листается. Всё, что
// случилось, пока человек смотрел в другое окно, должно
// оставаться где-то ещё — здесь.
//
// Показывает ленту EventFeed и умеет ровно две вещи:
// отфильтровать до важного и очистить. Никакой своей
// логики: правила, по которым событие вообще появляется,
// живут в SystemWatcher и в тех, кто постит события.
// -------------------------------------------------------

#include <QDialog>

class QCheckBox;
class QLabel;
class QListWidget;

class NotificationsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NotificationsDialog(bool english, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void buildUi();
    void reload();

    bool         m_english = false;
    QListWidget* m_list    = nullptr;
    QCheckBox*   m_onlyImportant = nullptr;
    QLabel*      m_empty   = nullptr;
};
