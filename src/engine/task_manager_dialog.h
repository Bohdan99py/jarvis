#pragma once
// ============================================================
// task_manager_dialog.h — Cyberpunk Kanban Task Manager
//
// The board itself (columns, cards, progress bar, move/add/
// remove animations) is QML (TaskBoard.qml, hosted in a
// QQuickWidget) — this class owns the data (three TaskListModel
// instances, one per column) and persistence, and still uses
// plain QDialog forms for Add/Edit (no reason to rebuild a
// perfectly good form in QML just for the sake of it).
// ============================================================

#include <QDialog>
#include <QVBoxLayout>
#include "database_manager.h"

class QQuickWidget;
class TaskListModel;

class TaskManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TaskManagerDialog(qint64 userId, QWidget* parent = nullptr);

    // Invoked from TaskBoard.qml via the "taskBoard" context property.
    Q_INVOKABLE void openTask(qint64 id);
    Q_INVOKABLE void moveTask(qint64 id, const QString& fromStatus, const QString& toStatus);
    Q_INVOKABLE void requestAdd();

signals:
    void taskChanged();

private:
    void rebuild();
    void showAddDialog();
    void showEditDialog(const DbTask& task);

    qint64          m_userId;
    QQuickWidget*   m_board          = nullptr;
    TaskListModel*  m_todoModel      = nullptr;
    TaskListModel*  m_inProgressModel = nullptr;
    TaskListModel*  m_doneModel      = nullptr;
};

// Deadline notification helper — returns alert text (empty if nothing urgent)
namespace TaskNotifications {
    QString checkDeadlines(qint64 userId, bool english);
    QString buildTaskContext(qint64 userId);
}
