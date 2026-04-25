#pragma once
// -------------------------------------------------------
// code_actions.h — Парсер и исполнитель файловых операций
//
// Парсит ответ Claude на наличие команд:
//   [FILE:path/to/file.cpp]...[/FILE]
//   [DIFF:path/to/file.cpp][FIND]...[REPLACE]...[/DIFF]
//   [MKDIR:path/to/directory]
//   [DELETE:path/to/file.cpp]
//   [CMD:системная команда]
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "jarvis_core_export.h"

struct CodeAction
{
    enum Type {
        CreateFile,
        DiffReplace,
        MakeDir,
        DeleteFile,
        SystemCmd,
        Unknown
    };

    Type    type = Unknown;
    QString filePath;
    QString content;
    QString findText;
    QString replaceText;
    QString description;

    bool    success = false;
    QString resultMessage;
};

class JARVIS_CORE_EXPORT CodeActions : public QObject
{
    Q_OBJECT

public:
    explicit CodeActions(QObject* parent = nullptr);

    void setProjectRoot(const QString& path) { m_projectRoot = path; }
    QString projectRoot() const { return m_projectRoot; }

    QVector<CodeAction> parseResponse(const QString& response) const;
    CodeAction executeAction(CodeAction action) const;
    QString processResponse(const QString& response);
    QString cleanResponseForDisplay(const QString& response) const;

signals:
    void fileCreated(const QString& path);
    void fileModified(const QString& path);
    void directoryCreated(const QString& path);
    void fileDeleted(const QString& path);
    void actionError(const QString& path, const QString& error);

private:
    CodeAction doCreateFile(CodeAction action) const;
    CodeAction doDiffReplace(CodeAction action) const;
    CodeAction doMakeDir(CodeAction action) const;
    CodeAction doDeleteFile(CodeAction action) const;
    QString fullPath(const QString& relativePath) const;

    QString m_projectRoot;
};
