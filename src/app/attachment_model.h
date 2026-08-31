#pragma once
// ============================================================
// attachment_model.h — модель прикреплённых файлов для полосы
// вложений.
//
// Полоса перестраивалась целиком на каждое изменение: цикл
// takeAt(0) + deleteLater() сносил все чипы и создавал их заново
// — то есть удаление одного файла из пяти пересоздавало пять
// виджетов. Модель сообщает ровно то, что изменилось, и ListView
// переиспользует делегаты.
// ============================================================

#include <QAbstractListModel>

#include "attachments_manager.h"

class AttachmentModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        SizeTextRole,
        PathRole,
        GlyphRole,
        ToneRole,     // "error" | "warning" | "accent" — тему знает QML
    };

    explicit AttachmentModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QList<Attachment>& items);

    int count() const { return m_items.size(); }
    QString summary() const { return m_summary; }

    Q_INVOKABLE void removeAt(int row);

signals:
    void countChanged();
    void removeRequested(int row);

private:
    QList<Attachment> m_items;
    QString m_summary;
};
