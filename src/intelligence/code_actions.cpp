// -------------------------------------------------------
// code_actions.cpp — Парсер и исполнитель файловых операций
// -------------------------------------------------------

#include "code_actions.h"
#include "jarvis_paths.h"
#include "kicad_schematic_builder.h"
#include "applauncher.h"
// applauncher.h drags in <windows.h>/<shellapi.h> without WIN32_LEAN_AND_MEAN,
// which #defines CreateFile/DeleteFile to CreateFileW/DeleteFileW — colliding
// with the pre-existing CodeAction::CreateFile/DeleteFile enum members used
// throughout this file. Undo the macro pollution right after the include.
#undef CreateFile
#undef DeleteFile

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QDebug>

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

    // 6. Парсим [KICAD_SCH:path.kicad_sch]{json}[/KICAD_SCH]
    {
        static const QRegularExpression reKicad(
            QStringLiteral(R"(\[KICAD_SCH:(.+?)\]\s*\n([\s\S]*?)\[/KICAD_SCH\])"),
            QRegularExpression::MultilineOption);

        auto it = reKicad.globalMatch(response);
        while (it.hasNext()) {
            auto match = it.next();
            CodeAction a;
            a.type = CodeAction::KiCadSchematic;
            a.filePath = match.captured(1).trimmed();
            a.content = match.captured(2).trimmed();
            a.description = QStringLiteral("Создать схему KiCad: ") + a.filePath;
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
    case CodeAction::CreateFile:     return doCreateFile(action);
    case CodeAction::DiffReplace:    return doDiffReplace(action);
    case CodeAction::MakeDir:        return doMakeDir(action);
    case CodeAction::DeleteFile:     return doDeleteFile(action);
    case CodeAction::KiCadSchematic: return doCreateKiCadSchematic(action);
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
            case CodeAction::KiCadSchematic:
                emit kicadSchematicCreated(action.filePath);
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
// Автопродолжение больших файлов
// ============================================================

bool CodeActions::detectOpenFileBlock(const QString& response,
                                       QString& filePath,
                                       QString& partialContent) const
{
    const int lastFileStart = response.lastIndexOf(QStringLiteral("[FILE:"));
    if (lastFileStart < 0) return false;

    // Если после последнего [FILE:...] где-то встречается [/FILE] —
    // блок уже закрыт, это нормальный завершённый файл.
    const int lastFileEnd = response.lastIndexOf(QStringLiteral("[/FILE]"));
    if (lastFileEnd > lastFileStart) return false;

    // Маркер [FILE:path] должен быть закрыт хотя бы скобкой ']' —
    // иначе он сам обрезан посередине и извлекать нечего.
    const int markerClose = response.indexOf(QChar(']'), lastFileStart);
    if (markerClose < 0) return false;

    const int pathStart = lastFileStart + 6; // длина "[FILE:"
    filePath = response.mid(pathStart, markerClose - pathStart).trimmed();
    if (filePath.isEmpty()) return false;

    // Содержимое — всё после маркера; пропускаем один \n сразу после ']'
    int contentStart = markerClose + 1;
    if (contentStart < response.size() && response.at(contentStart) == QChar('\n')) {
        ++contentStart;
    }
    partialContent = response.mid(contentStart);
    return true;
}

QString CodeActions::stripOpenFileBlock(const QString& response) const
{
    QString path, partial;
    if (!detectOpenFileBlock(response, path, partial)) return response;

    const int lastFileStart = response.lastIndexOf(QStringLiteral("[FILE:"));
    return response.left(lastFileStart).trimmed();
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

    // Убираем [KICAD_SCH:...]...[/KICAD_SCH] блоки
    static const QRegularExpression reKicadDisplay(
        QStringLiteral(R"(\[KICAD_SCH:.+?\][\s\S]*?\[/KICAD_SCH\])"),
        QRegularExpression::MultilineOption);
    clean.replace(reKicadDisplay, QString());

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

CodeAction CodeActions::doCreateKiCadSchematic(CodeAction action) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(action.content.toUtf8());
    if (!doc.isObject()) {
        action.success = false;
        action.resultMessage = QStringLiteral("Некорректный JSON в блоке KICAD_SCH");
        return action;
    }
    const QJsonObject root = doc.object();

    QList<KiCadPlacement> placements;
    for (const QJsonValue& v : root.value(QStringLiteral("components")).toArray()) {
        const QJsonObject c = v.toObject();
        KiCadPlacement p;
        p.componentType = c.value(QStringLiteral("type")).toString();
        p.reference     = c.value(QStringLiteral("ref")).toString();
        p.value         = c.value(QStringLiteral("value")).toString();
        p.rotationDeg   = c.value(QStringLiteral("rotation")).toInt(0);
        // Координаты необязательны. Проверяем НАЛИЧИЕ ключей, а не
        // значения: x=0 — это законная точка листа, и по одному лишь
        // нулю "не задано" от "задано в нуле" не отличить.
        p.positionGiven = c.contains(QStringLiteral("x"))
                       && c.contains(QStringLiteral("y"));
        if (p.positionGiven) {
            p.x = c.value(QStringLiteral("x")).toDouble();
            p.y = c.value(QStringLiteral("y")).toDouble();
        }
        placements.append(p);
    }

    QList<KiCadWire> wires;
    for (const QJsonValue& v : root.value(QStringLiteral("wires")).toArray()) {
        const QJsonObject w = v.toObject();
        const QJsonObject from = w.value(QStringLiteral("from")).toObject();
        const QJsonObject to   = w.value(QStringLiteral("to")).toObject();
        KiCadWire wire;
        wire.from.componentRef = from.value(QStringLiteral("ref")).toString();
        wire.from.pin          = from.value(QStringLiteral("pin")).toString();
        wire.to.componentRef   = to.value(QStringLiteral("ref")).toString();
        wire.to.pin            = to.value(QStringLiteral("pin")).toString();
        wires.append(wire);
    }

    // Расставляем всё, для чего координаты не пришли — по связям, той же
    // силовой моделью, что раскладывает граф понятий.
    KiCadSchematicBuilder::autoPlace(placements, wires);

    AppLauncher launcher;
    const QString kicadRoot = launcher.kicadInstallRoot();
    KiCadSchematicBuilder builder(kicadRoot);
    const KiCadBuildResult built = builder.build(placements, wires);

    if (!built.success) {
        action.success = false;
        action.resultMessage = QStringLiteral("Схема не собрана: ") + built.errorMessage;
        return action;
    }

    const QString path = fullPath(action.filePath);
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
    out << built.content;
    file.close();

    action.success = true;
    action.resultMessage = QStringLiteral("Схема KiCad создана: ") + action.filePath;
    // From here on, filePath carries the resolved absolute path — the
    // relative form above was only needed for the user-facing message.
    // kicadSchematicCreated() (emitted by the caller) needs the absolute
    // path so a "show in folder" action works regardless of project root.
    action.filePath = path;

    // Validate with kicad-cli's electrical rules check when available —
    // catches genuine electrical-connectivity issues (not file-format
    // ones, those can't happen: the builder only emits real symbol
    // definitions and pin-accurate wiring) and reports them rather than
    // silently leaving the user to discover them by opening KiCad.
    if (!kicadRoot.isEmpty()) {
        const QString cli = QDir(kicadRoot).filePath(QStringLiteral("bin/kicad-cli.exe"));
        if (QFileInfo::exists(cli)) {
            const QString reportPath = path + QStringLiteral(".erc.json");
            QProcess proc;
            proc.start(cli, {QStringLiteral("sch"), QStringLiteral("erc"), path,
                             QStringLiteral("--output"), reportPath,
                             QStringLiteral("--format"), QStringLiteral("json"),
                             QStringLiteral("--severity-error")});
            if (proc.waitForFinished(20000)) {
                QFile report(reportPath);
                if (report.open(QIODevice::ReadOnly)) {
                    const QJsonDocument ercDoc = QJsonDocument::fromJson(report.readAll());
                    report.close();
                    QFile::remove(reportPath);
                    int errorCount = 0;
                    for (const QJsonValue& sheet : ercDoc.object().value(QStringLiteral("sheets")).toArray())
                        errorCount += sheet.toObject().value(QStringLiteral("violations")).toArray().size();
                    action.resultMessage += errorCount > 0
                        ? QStringLiteral(" (ERC: %1 замечани%2 — открой в KiCad, чтобы проверить)")
                              .arg(errorCount).arg(errorCount == 1 ? QStringLiteral("е") : QStringLiteral("й"))
                        : QStringLiteral(" (ERC: без ошибок)");
                }
            }
        }
    }

    // Report line 192 (processResponse) echoes description on success —
    // mirror the ERC pass/fail info into it so the user actually sees it.
    action.description = action.resultMessage;

    return action;
}

// ============================================================
// Утилиты
// ============================================================

QString CodeActions::fullPath(const QString& relativePath) const
{
    // Если путь абсолютный — используем как есть
    if (QFileInfo(relativePath).isAbsolute()) return relativePath;

    if (!m_projectRoot.isEmpty())
        return QDir(m_projectRoot).absoluteFilePath(relativePath);

    // Без открытого проекта относительный путь раньше уходил в текущую
    // рабочую директорию процесса — у установленной версии это Program Files,
    // где запись запрещена ("Отказано в доступе"). Пишем в видимую
    // пользователю папку Documents/Jarvis Data/workspace.
    return JarvisPaths::subPath(QStringLiteral("workspace/") + relativePath);
}
