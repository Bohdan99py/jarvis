#pragma once
// -------------------------------------------------------
// code_actions.h — Парсер и исполнитель файловых операций
//
// Парсит ответ Claude на наличие команд:
//   [FILE:path/to/file.cpp]
//   ...код...
//   [/FILE]
//
//   [DIFF:path/to/file.cpp]
//   [FIND]
//   ...старый код...
//   [REPLACE]
//   ...новый код...
//   [/DIFF]
//
//   [MKDIR:path/to/directory]
//
//   [DELETE:path/to/file.cpp]
//
//   [CMD:системная команда]
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct CodeAction
{
    enum Type {
        CreateFile,     // Создать/перезаписать файл
        DiffReplace,    // Заменить фрагмент в файле
        MakeDir,        // Создать папку
        DeleteFile,     // Удалить файл
        SystemCmd,      // Системная команда [CMD:...]
        Unknown
    };

    Type    type = Unknown;
    QString filePath;       // Путь к файлу (относительный)
    QString content;        // Содержимое (для CreateFile)
    QString findText;       // Что искать (для DiffReplace)
    QString replaceText;    // На что заменить (для DiffReplace)
    QString description;    // Описание действия

    // Результат выполнения
    bool    success = false;
    QString resultMessage;
};

class CodeActions : public QObject
{
    Q_OBJECT

public:
    explicit CodeActions(QObject* parent = nullptr);

    // Установить корневую папку проекта (все пути относительно неё)
    void setProjectRoot(const QString& path) { m_projectRoot = path; }
    QString projectRoot() const { return m_projectRoot; }

    // Парсит ответ Claude и возвращает список действий
    QVector<CodeAction> parseResponse(const QString& response) const;

    // Выполняет действие
    CodeAction executeAction(CodeAction action) const;

    // Выполняет все действия из ответа и возвращает отчёт
    QString processResponse(const QString& response);

    // Очищает ответ от блоков [FILE]...[/FILE] для показа в чате
    // (возвращает только текстовую часть)
    QString cleanResponseForDisplay(const QString& response) const;

signals:
    void fileCreated(const QString& path);
    void fileModified(const QString& path);
    void directoryCreated(const QString& path);
    void fileDeleted(const QString& path);
    void actionError(const QString& path, const QString& error);

private:
    // Создать файл
    CodeAction doCreateFile(CodeAction action) const;

    // Применить diff
    CodeAction doDiffReplace(CodeAction action) const;

    // Создать директорию
    CodeAction doMakeDir(CodeAction action) const;

    // Удалить файл
    CodeAction doDeleteFile(CodeAction action) const;

    // Полный путь из относительного
    QString fullPath(const QString& relativePath) const;

    QString m_projectRoot;
};
