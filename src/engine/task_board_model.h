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
    // A plain Q_INVOKABLE count() looks like a property in QML
    // (modelData.listModel.count) but isn't one — reading a method
    // reference without calling it just stringifies the function itself
    // ("function() { [native code] }"), and even called correctly with
    // count(), QML's binding engine only tracks *property* reads for
    // auto-reevaluation, not arbitrary method calls, so the label would
    // never update as cards move between columns. A real Q_PROPERTY with
    // a change signal is what TaskBoard.qml actually needs here.
    Q_PROPERTY(int count READ count NOTIFY countChanged)
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

    int count() const { return m_tasks.size(); }

signals:
    void countChanged();

private:
    QList<DbTask> m_tasks;
    bool          m_english = false;

    static QString priorityColor(const QString& priority);
    QString deadlineText(const DbTask& t) const;
};
