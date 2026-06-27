#pragma once
// ============================================================
// task_manager_dialog.h — Cyberpunk Kanban Task Manager
// ============================================================

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QLineEdit>
#include <QComboBox>
#include <QDateTimeEdit>
#include "database_manager.h"

class TaskManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TaskManagerDialog(qint64 userId, QWidget* parent = nullptr);

signals:
    void taskChanged();

private:
    void rebuild();
    QWidget* buildColumn(const QString& title, const QString& status,
                         const QList<DbTask>& tasks);
    QWidget* buildCard(const DbTask& task);
    void showAddDialog();
    void showEditDialog(const DbTask& task);

    qint64 m_userId;
};

// Deadline notification helper — returns alert text (empty if nothing urgent)
namespace TaskNotifications {
    QString checkDeadlines(qint64 userId, bool english);
    QString buildTaskContext(qint64 userId);
}
