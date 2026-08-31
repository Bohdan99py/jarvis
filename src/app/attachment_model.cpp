#include "attachment_model.h"

#include "lang.h"

AttachmentModel::AttachmentModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int AttachmentModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_items.size();
}

QVariant AttachmentModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};

    const Attachment& a = m_items.at(index.row());

    switch (role) {
    case NameRole:
        return a.displayName;
    case SizeTextRole:
        return AttachmentsManager::humanSize(a.sizeBytes);
    case PathRole:
        return a.filePath;
    case GlyphRole:
        // Значок говорит о судьбе файла, а не о его типе: «слишком
        // большой» и «двоичный» — это то, что меняет поведение
        // отправки, и пользователю важно именно это.
        if (a.isTooLarge) return QStringLiteral("⚠");
        if (a.isBinary)   return QStringLiteral("▣");
        return QStringLiteral("📄");
    case ToneRole:
        if (a.isTooLarge) return QStringLiteral("error");
        if (a.isBinary)   return QStringLiteral("warning");
        return QStringLiteral("accent");
    }
    return {};
}

QHash<int, QByteArray> AttachmentModel::roleNames() const
{
    return {
        { NameRole,     QByteArrayLiteral("name")     },
        { SizeTextRole, QByteArrayLiteral("sizeText") },
        { PathRole,     QByteArrayLiteral("path")     },
        { GlyphRole,    QByteArrayLiteral("glyph")    },
        { ToneRole,     QByteArrayLiteral("tone")     },
    };
}

void AttachmentModel::setItems(const QList<Attachment>& items)
{
    beginResetModel();
    m_items = items;
    endResetModel();

    qint64 total = 0;
    for (const Attachment& a : m_items)
        total += a.sizeBytes;

    m_summary = m_items.isEmpty()
        ? QString()
        : QStringLiteral("%1 · %2")
              .arg(m_items.size())
              .arg(AttachmentsManager::humanSize(total));

    emit countChanged();
}

void AttachmentModel::removeAt(int row)
{
    if (row < 0 || row >= m_items.size()) return;
    emit removeRequested(row);
}
