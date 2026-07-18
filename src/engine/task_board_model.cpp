// ============================================================
// task_board_model.cpp
// ============================================================
#include "task_board_model.h"
#include <QDateTime>
#include <algorithm>

TaskListModel::TaskListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int TaskListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_tasks.size();
}

QString TaskListModel::priorityColor(const QString& priority)
{
    if (priority == QStringLiteral("High"))   return QStringLiteral("#ff5252");
    if (priority == QStringLiteral("Medium")) return QStringLiteral("#FFD740");
    return QStringLiteral("#45A29E");
}

QString TaskListModel::deadlineText(const DbTask& t) const
{
    if (!t.deadline.isValid()) return QString();

    const qint64 secsTo = QDateTime::currentDateTime().secsTo(t.deadline);
    if (secsTo < 0)
        return m_english ? QStringLiteral("OVERDUE") : QStringLiteral("ПРОСРОЧЕНО");
    if (secsTo < 3600)
        return m_english ? QStringLiteral("%1m left").arg(secsTo / 60)
                          : QStringLiteral("%1м осталось").arg(secsTo / 60);
    if (secsTo < 86400)
        return m_english ? QStringLiteral("%1h left").arg(secsTo / 3600)
                          : QStringLiteral("%1ч осталось").arg(secsTo / 3600);
    return t.deadline.toString(QStringLiteral("MMM dd"));
}

QVariant TaskListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tasks.size())
        return QVariant();

    const DbTask& t = m_tasks[index.row()];
    switch (role) {
    case IdRole:             return t.id;
    case TitleRole:          return t.title;
    case CategoryRole:       return t.category;
    case PriorityRole:       return t.priority;
    case PriorityColorRole:  return priorityColor(t.priority);
    case DeadlineTextRole:   return deadlineText(t);
    case OverdueRole:        return t.deadline.isValid()
                                     && QDateTime::currentDateTime() > t.deadline;
    default:                 return QVariant();
    }
}

QHash<int, QByteArray> TaskListModel::roleNames() const
{
    return {
        { IdRole,            "taskId" },
        { TitleRole,         "title" },
        { CategoryRole,      "category" },
        { PriorityRole,      "priority" },
        { PriorityColorRole, "priorityColor" },
        { DeadlineTextRole,  "deadlineText" },
        { OverdueRole,       "overdue" },
    };
}

void TaskListModel::setTasks(const QList<DbTask>& tasks)
{
    beginResetModel();
    m_tasks = tasks;
    endResetModel();
    emit countChanged();
}

std::optional<DbTask> TaskListModel::takeById(qint64 id)
{
    const int idx = std::find_if(m_tasks.begin(), m_tasks.end(),
        [id](const DbTask& t) { return t.id == id; }) - m_tasks.begin();
    if (idx < 0 || idx >= m_tasks.size()) return std::nullopt;

    beginRemoveRows(QModelIndex(), idx, idx);
    DbTask removed = m_tasks.takeAt(idx);
    endRemoveRows();
    emit countChanged();
    return removed;
}

void TaskListModel::insertTask(const DbTask& task)
{
    const int idx = m_tasks.size();
    beginInsertRows(QModelIndex(), idx, idx);
    m_tasks.append(task);
    endInsertRows();
    emit countChanged();
}
