#pragma once
// -------------------------------------------------------
// attachments_manager.h — Менеджер прикреплений файлов
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QDateTime>

#include "jarvis_core_export.h"

struct Attachment
{
    QString filePath;
    QString displayName;
    qint64  sizeBytes = 0;
    bool    isBinary  = false;
    bool    isTooLarge = false;
    QDateTime addedAt;
};

class JARVIS_CORE_EXPORT AttachmentsManager : public QObject
{
    Q_OBJECT

public:
    explicit AttachmentsManager(QObject* parent = nullptr);

    int addFiles(const QStringList& paths);
    int addFile(const QString& path);
    void removeAt(int index);
    void clear();

    const QVector<Attachment>& items() const { return m_items; }
    int  count() const                       { return m_items.size(); }
    bool isEmpty() const                     { return m_items.isEmpty(); }

    qint64 totalSize() const;
    QString totalSizeHuman() const;

    void setKeepAfterSend(bool keep) { m_keepAfterSend = keep; }
    bool keepAfterSend() const       { return m_keepAfterSend; }

    QString buildAttachmentBlock(int totalCharsBudget = 60000) const;

    static bool isTextFile(const QString& path);
    static QString humanSize(qint64 bytes);

signals:
    void changed();

private:
    QVector<Attachment> m_items;
    bool m_keepAfterSend = false;

    static constexpr qint64 MAX_FILE_SIZE   = 2 * 1024 * 1024;
    static constexpr qint64 WARN_FILE_SIZE  = 100 * 1024;
    static constexpr int    MAX_ATTACHMENTS = 20;
};
