#pragma once
// ============================================================
// task_board_model.h — QAbstractListModel backing one Kanban
// column (Todo / InProgress / Done) for TaskBoard.qml.
//
// A real QAbstractListModel (rather than reassigning a plain
// QVariantList on every change) is what lets QML's ListView
// play proper add/remove/move transitions: insertTask()/takeById()
// emit structural begin/end signals for exactly the rows that
// changed, instead of forcing a full-model reset that would
// replay the "add" animation for every card whenever anything
// changes.
// ============================================================

#include <QAbstractListModel>
#include <QList>
#include <optional>
#include "database_manager.h"

class TaskListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        CategoryRole,
        PriorityRole,
        PriorityColorRole,
        DeadlineTextRole,
        OverdueRole,
    };

    explicit TaskListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Full reload (dialog open / after an edit-dialog save) — fine to
    // reset wholesale here since those are rare, deliberate refreshes,
    // not the interactive drag-and-drop move (see takeById/insertTask).
    void setTasks(const QList<DbTask>& tasks);
    void setEnglish(bool english) { m_english = english; }

    // Structural single-row ops for drag-and-drop moves between columns.
    std::optional<DbTask> takeById(qint64 id);
    void insertTask(const DbTask& task);

    Q_INVOKABLE int count() const { return m_tasks.size(); }

private:
    QList<DbTask> m_tasks;
    bool          m_english = false;

    static QString priorityColor(const QString& priority);
    QString deadlineText(const DbTask& t) const;
};
