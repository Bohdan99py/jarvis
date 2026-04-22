// -------------------------------------------------------
// code_actions.cpp — Парсер и исполнитель файловых операций
// -------------------------------------------------------

#include "code_actions.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>

CodeActions::CodeActions(QObject* parent)
    : QObject(parent)
{
}

// ============================================================
// Парсинг ответа Claude
// ============================================================

QVector<CodeAction> CodeActions::parseResponse(const QString& response) const
{
    QVector<CodeAction> actions;

    // 1. Парсим [FILE:path]...[/FILE]
    {
        static const QRegularExpression reFile(
            QStringLiteral(R"(\[FILE:(.+?)\]\s*\n([\s\S]*?)\[/FILE\])"),
            QRegularExpression::MultilineOption);

        auto it = reFile.globalMatch(response);
        while (it.hasNext()) {
            auto match = it.next();
            CodeAction a;
            a.type = CodeAction::CreateFile;
            a.filePath = match.captured(1).trimmed();
            a.content = match.captured(2);
            // Убираем начальные/конечные пустые строки
            while (a.content.startsWith(QChar('\n'))) a.content = a.content.mid(1);
            while (a.content.endsWith(QChar('\n'))) a.content.chop(1);
            a.content += QChar('\n'); // Один перенос в конце
            a.description = QStringLiteral("Создать файл: ") + a.filePath;
            actions.append(a);
        }
    }

    // 2. Парсим [DIFF:path] [FIND]...[REPLACE]...[/DIFF]
    {
        static const QRegularExpression reDiff(
            QStringLiteral(R"(\[DIFF:(.+?)\]\s*\n\[FIND\]\s*\n([\s\S]*?)\[REPLACE\]\s*\n([\s\S]*?)\[/DIFF\])"),
            QRegularExpression::MultilineOption);

        auto it = reDiff.globalMatch(response);
        while (it.hasNext()) {
            auto match = it.next();
            CodeAction a;
            a.type = CodeAction::DiffReplace;
            a.filePath = match.captured(1).trimmed();
            a.findText = match.captured(2);
            a.replaceText = match.captured(3);
            // Убираем trailing newlines
            while (a.findText.endsWith(QChar('\n'))) a.findText.chop(1);
            while (a.replaceText.endsWith(QChar('\n'))) a.replaceText.chop(1);
            a.description = QStringLiteral("Изменить файл: ") + a.filePath;
            actions.append(a);
        }
    }

    // 3. Парсим [MKDIR:path]
    {
        static const QRegularExpression reMkdir(
            QStringLiteral(R"(\[MKDIR:(.+?)\])"));

        auto it = reMkdir.globalMatch(response);
        while (it.hasNext()) {
            auto match = it.next();
            CodeAction a;
            a.type = CodeAction::MakeDir;
            a.filePath = match.captured(1).trimmed();
            a.description = QStringLiteral("Создать папку: ") + a.filePath;
            actions.append(a);
        }
    }

    // 4. Парсим [DELETE:path]
    {
        static const QRegularExpression reDel(
            QStringLiteral(R"(\[DELETE:(.+?)\])"));

        auto it = reDel.globalMatch(response);
        while (it.hasNext()) {
            auto match = it.next();
            CodeAction a;
            a.type = CodeAction::DeleteFile;
            a.filePath = match.captured(1).trimmed();
            a.description = QStringLiteral("Удалить файл: ") + a.filePath;
            actions.append(a);
        }
    }

    // 5. Парсим [CMD:команда]
    {
        static const QRegularExpression reCmd(
            QStringLiteral(R"(\[CMD:(.+?)\])"));

        auto it = reCmd.globalMatch(response);
        while (it.hasNext()) {
            auto match = it.next();
            CodeAction a;
            a.type = CodeAction::SystemCmd;
            a.content = match.captured(1).trimmed();
            a.description = QStringLiteral("Команда: ") + a.content;
            actions.append(a);
        }
    }

    return actions;
}

// ============================================================
// Выполнение действий
// ============================================================

CodeAction CodeActions::executeAction(CodeAction action) const
{
    switch (action.type) {
    case CodeAction::CreateFile:  return doCreateFile(action);
    case CodeAction::DiffReplace: return doDiffReplace(action);
    case CodeAction::MakeDir:     return doMakeDir(action);
    case CodeAction::DeleteFile:  return doDeleteFile(action);
    case CodeAction::SystemCmd:
        // Системные команды выполняются через Jarvis::processCommand
        action.success = true;
        action.resultMessage = QStringLiteral("Команда передана на выполнение");
        return action;
    default:
        action.success = false;
        action.resultMessage = QStringLiteral("Неизвестный тип действия");
        return action;
    }
}

QString CodeActions::processResponse(const QString& response)
{
    auto actions = parseResponse(response);
    if (actions.isEmpty()) return QString();

    QString report;
    int success = 0;
    int failed = 0;

    for (auto& action : actions) {
        if (action.type == CodeAction::SystemCmd) continue; // CMD обрабатывается отдельно

        action = executeAction(action);

        if (action.success) {
            success++;
            report += QStringLiteral("  + ") + action.description + QStringLiteral("\n");

            // Сигналы
            switch (action.type) {
            case CodeAction::CreateFile:
                emit fileCreated(action.filePath);
                break;
            case CodeAction::DiffReplace:
                emit fileModified(action.filePath);
                break;
            case CodeAction::MakeDir:
                emit directoryCreated(action.filePath);
                break;
            case CodeAction::DeleteFile:
                emit fileDeleted(action.filePath);
                break;
            default:
                break;
            }
        } else {
            failed++;
            report += QStringLiteral("  ! ") + action.description
                    + QStringLiteral(": ") + action.resultMessage + QStringLiteral("\n");
            emit actionError(action.filePath, action.resultMessage);
        }
    }

    if (success == 0 && failed == 0) return QString();

    QString header = QStringLiteral("Файловые операции: ")
                   + QString::number(success) + QStringLiteral(" выполнено");
    if (failed > 0) {
        header += QStringLiteral(", ") + QString::number(failed) + QStringLiteral(" ошибок");
    }

    return header + QStringLiteral("\n") + report;
}

// ============================================================
// Очистка ответа для отображения
// ============================================================

QString CodeActions::cleanResponseForDisplay(const QString& response) const
{
    QString clean = response;

    // Убираем [FILE:...]...[/FILE] блоки
    static const QRegularExpression reFile(
        QStringLiteral(R"(\[FILE:.+?\][\s\S]*?\[/FILE\])"),
        QRegularExpression::MultilineOption);
    clean.replace(reFile, QString());

    // Убираем [DIFF:...]...[/DIFF] блоки
    static const QRegularExpression reDiff(
        QStringLiteral(R"(\[DIFF:.+?\][\s\S]*?\[/DIFF\])"),
        QRegularExpression::MultilineOption);
    clean.replace(reDiff, QString());

    // Убираем [MKDIR:...], [DELETE:...], [CMD:...]
    static const QRegularExpression reSingle(
        QStringLiteral(R"(\[(MKDIR|DELETE|CMD):.+?\])"));
    clean.replace(reSingle, QString());

    // Убираем лишние пустые строки
    static const QRegularExpression reEmptyLines(
        QStringLiteral(R"(\n{3,})"));
    clean.replace(reEmptyLines, QStringLiteral("\n\n"));

    return clean.trimmed();
}

// ============================================================
// Реализация операций
// ============================================================

CodeAction CodeActions::doCreateFile(CodeAction action) const
{
    QString path = fullPath(action.filePath);

    // Создаём директории
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        action.success = false;
        action.resultMessage = QStringLiteral("Не удалось создать файл: ") + file.errorString();
        return action;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << action.content;
    file.close();

    action.success = true;
    action.resultMessage = QStringLiteral("Файл создан: ") + action.filePath
                         + QStringLiteral(" (") + QString::number(action.content.size())
                         + QStringLiteral(" байт)");
    return action;
}

CodeAction CodeActions::doDiffReplace(CodeAction action) const
{
    QString path = fullPath(action.filePath);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        action.success = false;
        action.resultMessage = QStringLiteral("Файл не найден: ") + action.filePath;
        return action;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    QString content = in.readAll();
    file.close();

    // Ищем фрагмент
    int pos = content.indexOf(action.findText);
    if (pos < 0) {
        // Пробуем с нормализацией пробелов/переносов
        QString normalizedContent = content;
        QString normalizedFind = action.findText;
        normalizedContent.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        normalizedFind.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));

        pos = normalizedContent.indexOf(normalizedFind);
        if (pos < 0) {
            action.success = false;
            action.resultMessage = QStringLiteral("Фрагмент для замены не найден в файле");
            return action;
        }
        content = normalizedContent;
        action.findText = normalizedFind;
    }

    // Заменяем
    content.replace(pos, action.findText.length(), action.replaceText);

    // Сохраняем
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        action.success = false;
        action.resultMessage = QStringLiteral("Не удалось записать файл");
        return action;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << content;
    file.close();

    action.success = true;
    action.resultMessage = QStringLiteral("Файл изменён: ") + action.filePath;
    return action;
}

CodeAction CodeActions::doMakeDir(CodeAction action) const
{
    QString path = fullPath(action.filePath);

    if (QDir().mkpath(path)) {
        action.success = true;
        action.resultMessage = QStringLiteral("Папка создана");
    } else {
        action.success = false;
        action.resultMessage = QStringLiteral("Не удалось создать папку");
    }
    return action;
}

CodeAction CodeActions::doDeleteFile(CodeAction action) const
{
    QString path = fullPath(action.filePath);

    QFileInfo fi(path);
    if (!fi.exists()) {
        action.success = false;
        action.resultMessage = QStringLiteral("Файл не найден");
        return action;
    }

    if (fi.isDir()) {
        action.success = QDir(path).removeRecursively();
    } else {
        action.success = QFile::remove(path);
    }

    action.resultMessage = action.success
        ? QStringLiteral("Удалено")
        : QStringLiteral("Не удалось удалить");
    return action;
}

// ============================================================
// Утилиты
// ============================================================

QString CodeActions::fullPath(const QString& relativePath) const
{
    if (m_projectRoot.isEmpty()) return relativePath;

    // Если путь абсолютный — используем как есть
    if (QFileInfo(relativePath).isAbsolute()) return relativePath;

    return QDir(m_projectRoot).absoluteFilePath(relativePath);
}
