// -------------------------------------------------------
// action_registry.cpp — см. action_registry.h
// -------------------------------------------------------

#include "action_registry.h"

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QWidget>
#include <QDebug>

// ============================================================
//  ActionRegistry
// ============================================================

ActionRegistry::ActionRegistry(QObject* parent)
    : QObject(parent)
{
}

void ActionRegistry::add(AppAction action, QObject* owner)
{
    if (action.id.isEmpty() || !action.run) {
        qWarning() << "[Actions] rejected malformed action:" << action.id;
        return;
    }

    if (owner) {
        m_owners.insert(action.id, owner);
        if (!m_watched.contains(owner)) {
            m_watched.insert(owner);
            connect(owner, &QObject::destroyed, this, [this](QObject* gone) {
                m_watched.remove(gone);
                removeOwnedBy(gone);
            });
        }
    } else {
        // Перерегистрация без владельца снимает прежнюю привязку: иначе
        // команда, однажды сменившая владельца, ушла бы вместе со старым.
        m_owners.remove(action.id);
    }

    for (int i = 0; i < m_actions.size(); ++i) {
        if (m_actions[i].id == action.id) {
            m_actions[i] = std::move(action);   // перерегистрация — замена
            emit changed();
            return;
        }
    }
    m_actions.append(std::move(action));
    emit changed();
}

void ActionRegistry::removeOwnedBy(QObject* owner)
{
    if (!owner)
        return;

    int removed = 0;
    for (int i = m_actions.size() - 1; i >= 0; --i) {
        if (m_owners.value(m_actions[i].id) != owner)
            continue;
        m_owners.remove(m_actions[i].id);
        m_actions.removeAt(i);
        ++removed;
    }

    if (removed > 0)
        emit changed();
}

const AppAction* ActionRegistry::find(const QString& id) const
{
    for (const AppAction& a : m_actions) {
        if (a.id == id)
            return &a;
    }
    return nullptr;
}

QVector<AppAction> ActionRegistry::inGroup(const QString& group) const
{
    QVector<AppAction> out;
    for (const AppAction& a : m_actions) {
        if (a.group == group)
            out.append(a);
    }
    return out;
}

QStringList ActionRegistry::groups() const
{
    QStringList out;
    for (const AppAction& a : m_actions) {
        if (!a.group.isEmpty() && !out.contains(a.group))
            out << a.group;
    }
    return out;
}

bool ActionRegistry::run(const QString& id)
{
    const AppAction* action = find(id);
    if (!action || !action->run)
        return false;
    if (!action->isEnabled())
        return false;

    action->run();
    return true;
}

void ActionRegistry::populateMenu(QMenu* menu, const QString& group)
{
    if (!menu)
        return;

    for (const AppAction& a : inGroup(group)) {
        QString text = (a.icon.isEmpty() ? QString() : a.icon + QChar(' ')) + a.title;
        if (!a.shortcut.isEmpty())
            text += QChar('\t') + a.shortcut;   // QMenu рисует это как колонку

        QAction* act = menu->addAction(text);
        if (!a.hint.isEmpty())
            act->setToolTip(a.hint);
        act->setEnabled(a.isEnabled());
        if (a.checkable) {
            act->setCheckable(true);
            act->setChecked(a.isChecked());
        }

        const QString id = a.id;
        connect(act, &QAction::triggered, this, [this, id]() { run(id); });
    }
}

void ActionRegistry::installShortcuts(QWidget* host)
{
    if (!host)
        return;

    for (const AppAction& a : m_actions) {
        if (a.shortcut.isEmpty())
            continue;

        auto* act = new QAction(a.title, host);
        act->setShortcut(QKeySequence(a.shortcut));
        act->setShortcutContext(Qt::ApplicationShortcut);

        const QString id = a.id;
        connect(act, &QAction::triggered, this, [this, id]() { run(id); });
        host->addAction(act);
    }
}

// ============================================================
//  ActionModel
// ============================================================

ActionModel::ActionModel(ActionRegistry* registry, QObject* parent)
    : QAbstractListModel(parent)
    , m_registry(registry)
{
    if (m_registry) {
        connect(m_registry, &ActionRegistry::changed, this, &ActionModel::reload);
        reload();
    }
}

void ActionModel::setGroupFilter(const QString& group)
{
    if (m_group == group)
        return;
    m_group = group;
    reload();
}

bool ActionModel::run(const QString& id)
{
    return m_registry && m_registry->run(id);
}

void ActionModel::reload()
{
    beginResetModel();
    m_rows = m_registry ? (m_group.isEmpty() ? m_registry->all()
                                             : m_registry->inGroup(m_group))
                        : QVector<AppAction>();
    endResetModel();
}

int ActionModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant ActionModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();

    const AppAction& a = m_rows[index.row()];
    switch (role) {
    case IdRole:       return a.id;
    case TitleRole:    return a.title;
    case IconRole:     return a.icon;
    case ShortcutRole: return a.shortcut;
    case GroupRole:    return a.group;
    case HintRole:     return a.hint;
    case EnabledRole:  return a.isEnabled();
    case CheckableRole:return a.checkable;
    case CheckedRole:  return a.isChecked();
    default:           return QVariant();
    }
}

QHash<int, QByteArray> ActionModel::roleNames() const
{
    return {
        { IdRole,       QByteArrayLiteral("actionId") },
        { TitleRole,    QByteArrayLiteral("title") },
        { IconRole,     QByteArrayLiteral("icon") },
        { ShortcutRole, QByteArrayLiteral("shortcut") },
        { GroupRole,    QByteArrayLiteral("group") },
        { HintRole,     QByteArrayLiteral("hint") },
        { EnabledRole,   QByteArrayLiteral("enabled") },
        { CheckableRole, QByteArrayLiteral("checkable") },
        { CheckedRole,   QByteArrayLiteral("checked") }
    };
}
