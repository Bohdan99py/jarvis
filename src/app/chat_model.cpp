// ============================================================
// chat_model.cpp — реализация ленты сообщений.
// ============================================================

#include "chat_model.h"

#include "jarvis_theme.h"

ChatModel::ChatModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ChatModel::rowCount(const QModelIndex& parent) const
{
    // У плоского списка дети есть только у корня; без этой проверки
    // ListView получил бы rowCount и для каждого элемента.
    if (parent.isValid()) return 0;
    return m_messages.size();
}

QVariant ChatModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.size())
        return {};

    const Message& m = m_messages.at(index.row());
    const JarvisTheme& t = JarvisTheme::instance();

    switch (role) {
    case KindRole: return int(m.kind);
    case WhoRole:  return m.who;
    case TextRole: return m.text;
    case TimeRole: return m.at.toString(QStringLiteral("HH:mm"));
    case AccentRole:
        switch (m.kind) {
        case Jarvis: return t.roleJarvis();
        case User:   return t.roleUser();
        case System: return t.roleSystem();
        case Error:  return t.error();
        }
        return t.onSurface();
    }
    return {};
}

QHash<int, QByteArray> ChatModel::roleNames() const
{
    return {
        { KindRole,   QByteArrayLiteral("kind")   },
        { WhoRole,    QByteArrayLiteral("who")    },
        { TextRole,   QByteArrayLiteral("text")   },
        { TimeRole,   QByteArrayLiteral("time")   },
        { AccentRole, QByteArrayLiteral("accent") },
    };
}

void ChatModel::append(Kind kind, const QString& who, const QString& text)
{
    // Вытесняем ДО вставки, а не после: иначе ListView успевает
    // проиграть анимацию появления для строки, которую в том же
    // такте удаляют с другого конца.
    if (m_maxMessages > 0 && m_messages.size() >= m_maxMessages) {
        const int excess = m_messages.size() - m_maxMessages + 1;
        beginRemoveRows(QModelIndex(), 0, excess - 1);
        m_messages.remove(0, excess);
        endRemoveRows();
    }

    beginInsertRows(QModelIndex(), m_messages.size(), m_messages.size());
    m_messages.append(Message{ kind, who, text, QDateTime::currentDateTime() });
    endInsertRows();

    emit countChanged();
    emit messageAppended();
}

void ChatModel::setMaxMessages(int n)
{
    m_maxMessages = n;
}

void ChatModel::clear()
{
    if (m_messages.isEmpty()) return;

    beginResetModel();
    m_messages.clear();
    endResetModel();

    emit countChanged();
}
