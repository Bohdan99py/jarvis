// -------------------------------------------------------
// edit_journal.cpp — журнал правок и откат
// -------------------------------------------------------

#include "edit_journal.h"
#include "jarvis_paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace {

QString opTypeToString(EditOp::Type type)
{
    switch (type) {
    case EditOp::Created:  return QStringLiteral("created");
    case EditOp::Deleted:  return QStringLiteral("deleted");
    case EditOp::Moved:    return QStringLiteral("moved");
    case EditOp::External: return QStringLiteral("external");
    default:               return QStringLiteral("modified");
    }
}

EditOp::Type opTypeFromString(const QString& s)
{
    if (s == QStringLiteral("created"))  return EditOp::Created;
    if (s == QStringLiteral("deleted"))  return EditOp::Deleted;
    if (s == QStringLiteral("moved"))    return EditOp::Moved;
    if (s == QStringLiteral("external")) return EditOp::External;
    return EditOp::Modified;
}

QString tr2(bool english, const char* ru, const char* en)
{
    return english ? QString::fromUtf8(en) : QString::fromUtf8(ru);
}

} // namespace

// ============================================================
// Инициализация
// ============================================================

EditJournal& EditJournal::instance()
{
    static EditJournal journal;
    return journal;
}

EditJournal::EditJournal()
{
    load();
}

QString EditJournal::journalPath()
{
    return JarvisPaths::subPath(QStringLiteral("edit_journal.json"));
}

QString EditJournal::backupsRoot()
{
    return JarvisPaths::subPath(QStringLiteral("edit_backups"));
}

// ============================================================
// Батчи
// ============================================================

void EditJournal::beginBatch(const QString& label)
{
    if (m_depth++ > 0) return;   // вложенный вызов — продолжаем текущий батч

    m_current = EditBatch{};
    m_current.id    = QDateTime::currentDateTime()
                          .toString(QStringLiteral("yyyyMMdd_hhmmss_zzz"));
    m_current.label = label;
    m_current.at    = QDateTime::currentDateTime();
}

void EditJournal::endBatch()
{
    if (m_depth == 0) return;
    if (--m_depth > 0) return;

    // Пустой батч (модель ничего не изменила) в историю не попадает —
    // иначе «отмени» откатывал бы ничего и выглядел бы сломанным.
    if (m_current.ops.isEmpty()) {
        m_current = EditBatch{};
        return;
    }

    m_batches.append(m_current);
    m_current = EditBatch{};
    pruneOldBatches();
    save();
}

// ============================================================
// Запись операций
// ============================================================

QString EditJournal::backupFor(const QString& absPath) const
{
    if (m_current.id.isEmpty()) return QString();
    if (!QFile::exists(absPath)) return QString();

    const QString dir = backupsRoot() + QChar('/') + m_current.id;
    QDir().mkpath(dir);

    // Имя бэкапа должно быть уникальным в пределах батча: один и тот же
    // файл может правиться несколько раз подряд, и первая (самая ценная)
    // копия не должна затираться последующими.
    const QFileInfo fi(absPath);
    QString base = fi.fileName();
    QString target = dir + QChar('/') + base;
    int counter = 1;
    while (QFile::exists(target)) {
        target = dir + QChar('/') + QString::number(counter++)
               + QChar('_') + base;
    }

    if (!QFile::copy(absPath, target)) {
        qWarning() << "[EditJournal] backup failed:" << absPath;
        return QString();
    }
    return target;
}

void EditJournal::recordCreate(const QString& absPath)
{
    if (!inBatch()) return;

    // Файл уже существует — значит это перезапись, а не создание:
    // содержимое надо сохранить, иначе откат его не вернёт.
    if (QFile::exists(absPath)) {
        recordModify(absPath);
        return;
    }

    EditOp op;
    op.type = EditOp::Created;
    op.path = absPath;
    m_current.ops.append(op);
}

void EditJournal::recordModify(const QString& absPath)
{
    if (!inBatch()) return;

    EditOp op;
    op.type   = EditOp::Modified;
    op.path   = absPath;
    op.backup = backupFor(absPath);
    m_lastBackup = op.backup;
    m_current.ops.append(op);
}

void EditJournal::recordDelete(const QString& absPath)
{
    if (!inBatch()) return;

    EditOp op;
    op.type   = EditOp::Deleted;
    op.path   = absPath;
    op.backup = backupFor(absPath);
    m_lastBackup = op.backup;
    m_current.ops.append(op);
}

void EditJournal::recordExternal(const QString& description)
{
    if (!inBatch()) return;
    if (description.trimmed().isEmpty()) return;

    EditOp op;
    op.type   = EditOp::External;
    op.detail = description.trimmed();
    m_current.ops.append(op);
}

void EditJournal::recordMove(const QString& fromAbs, const QString& toAbs)
{
    if (!inBatch()) return;

    EditOp op;
    op.type         = EditOp::Moved;
    op.path         = toAbs;
    op.previousPath = fromAbs;
    // Если на месте назначения что-то было — его содержимое тоже спасаем.
    op.backup       = backupFor(toAbs);
    m_current.ops.append(op);
}

void EditJournal::discardLastOp()
{
    if (!inBatch() || m_current.ops.isEmpty()) return;

    // Копию с диска не трогаем: она уйдёт вместе с батчем при ротации,
    // а удалять файл, на который могла остаться ссылка, опаснее.
    m_current.ops.removeLast();
}

// ============================================================
// Откат
// ============================================================

QString EditJournal::undoLast(bool english)
{
    if (m_batches.isEmpty()) {
        return tr2(english, "Откатывать нечего — правок в журнале нет.",
                            "Nothing to undo — the journal is empty.");
    }

    const EditBatch batch = m_batches.takeLast();

    int restored = 0;
    int failed   = 0;
    QStringList lines;
    QStringList irreversible;   // то, что откатить нельзя ничем

    // В обратном порядке: последняя операция отменяется первой, иначе
    // переименование, сделанное после правки, вернёт файл не туда.
    for (int i = batch.ops.size() - 1; i >= 0; --i) {
        const EditOp& op = batch.ops[i];
        bool ok = false;

        switch (op.type) {
        case EditOp::External:
            // Единственная операция, которую откат не трогает. Пропустить
            // её молча было бы хуже всего: человек решил бы, что вернулось
            // всё, включая убитый процесс.
            irreversible.append(op.detail);
            continue;

        case EditOp::Created:
            if (QFileInfo(op.path).isDir()) {
                // Созданная папка удаляется, только если пуста: внутрь
                // могли положить что-то помимо нас.
                ok = QDir().rmdir(op.path) || !QFileInfo::exists(op.path);
                lines.append(tr2(english, "удалена созданная папка ",
                                          "removed created folder ")
                             + QFileInfo(op.path).fileName());
                break;
            }
            ok = !QFile::exists(op.path) || QFile::remove(op.path);
            lines.append(tr2(english, "удалён созданный ", "removed created ")
                         + QFileInfo(op.path).fileName());
            break;

        case EditOp::Modified:
        case EditOp::Deleted:
            if (op.backup.isEmpty() || !QFile::exists(op.backup)) {
                ok = false;
                break;
            }
            QDir().mkpath(QFileInfo(op.path).absolutePath());
            if (QFile::exists(op.path)) QFile::remove(op.path);
            ok = QFile::copy(op.backup, op.path);
            lines.append(tr2(english, "восстановлен ", "restored ")
                         + QFileInfo(op.path).fileName());
            break;

        case EditOp::Moved:
            QDir().mkpath(QFileInfo(op.previousPath).absolutePath());
            if (QFile::exists(op.previousPath)) QFile::remove(op.previousPath);
            ok = QFile::rename(op.path, op.previousPath);
            // На месте назначения мог лежать перезаписанный файл — вернём его.
            if (ok && !op.backup.isEmpty() && QFile::exists(op.backup))
                QFile::copy(op.backup, op.path);
            lines.append(tr2(english, "возвращён ", "moved back ")
                         + QFileInfo(op.previousPath).fileName());
            break;
        }

        if (ok) restored++;
        else    failed++;
    }

    save();

    // Батч без единой обратимой операции — это не «откатили ноль правок»,
    // а «последнее действие вернуть нечем». Разница важная: следующий
    // «отмени» доберётся до предыдущего батча, и человек должен об этом
    // узнать сразу, а не после третьей попытки.
    if (restored == 0 && failed == 0 && !irreversible.isEmpty()) {
        QString report = tr2(english,
                             "Последнее действие откатить нельзя (",
                             "The last action cannot be undone (")
                       + batch.at.toString(QStringLiteral("HH:mm")) + QStringLiteral("):\n  ")
                       + irreversible.mid(0, 10).join(QStringLiteral("\n  "));
        if (!m_batches.isEmpty()) {
            report += tr2(english,
                          "\nПредыдущая правка файлов — ",
                          "\nThe previous file change was at ")
                    + m_batches.last().at.toString(QStringLiteral("HH:mm"))
                    + tr2(english,
                          ", повтори «отмени», чтобы вернуть её.",
                          "; repeat \"undo\" to revert it.");
        }
        return report;
    }

    QString report = tr2(english, "↩ Откатил правки (", "↩ Undid the changes (")
                   + batch.at.toString(QStringLiteral("HH:mm")) + QStringLiteral("): ")
                   + QString::number(restored)
                   + tr2(english, " операций", " operations");
    if (failed > 0) {
        report += tr2(english, ", ", ", ") + QString::number(failed)
                + tr2(english, " не удалось", " failed");
    }
    if (!lines.isEmpty()) {
        report += QStringLiteral("\n  ") + lines.mid(0, 10).join(QStringLiteral("\n  "));
    }
    if (!irreversible.isEmpty()) {
        report += tr2(english, "\nНе откатывается:\n  ", "\nCannot be undone:\n  ")
                + irreversible.mid(0, 10).join(QStringLiteral("\n  "));
    }
    return report;
}

QString EditJournal::history(bool english, int maxBatches) const
{
    if (m_batches.isEmpty()) {
        return tr2(english, "Журнал правок пуст.", "The edit journal is empty.");
    }

    QString out = tr2(english, "🗂 Последние правки:\n", "🗂 Recent changes:\n");

    const int from = qMax(0, m_batches.size() - maxBatches);
    for (int i = m_batches.size() - 1; i >= from; --i) {
        const EditBatch& b = m_batches[i];

        int reversible = 0;
        for (const EditOp& op : b.ops) {
            if (op.isReversible())
                ++reversible;
        }

        out += QStringLiteral("  ") + b.at.toString(QStringLiteral("dd.MM HH:mm"))
             + QStringLiteral(" — ") + b.label + QStringLiteral(", ")
             + QString::number(b.ops.size())
             + tr2(english, " операций", " operations");

        // Батч из одних действий над системой в списке «что можно
        // откатить» выглядел бы обещанием, которого журнал не сдержит.
        if (reversible == 0)
            out += tr2(english, " (откату не подлежат)", " (not undoable)");
        else if (reversible < b.ops.size())
            out += tr2(english, ", из них откатятся ", ", of which undoable: ")
                 + QString::number(reversible);

        if (i == m_batches.size() - 1 && reversible > 0)
            out += tr2(english, "  ← откатится по «отмени правки»",
                                "  ← \"undo edits\" reverts this one");
        out += QChar('\n');
    }
    return out.trimmed();
}

// ============================================================
// Хранение
// ============================================================

void EditJournal::pruneOldBatches()
{
    while (m_batches.size() > MAX_BATCHES) {
        const EditBatch old = m_batches.takeFirst();
        const QString dir = backupsRoot() + QChar('/') + old.id;
        if (QFileInfo::exists(dir)) QDir(dir).removeRecursively();
    }
}

void EditJournal::save() const
{
    QJsonArray batches;
    for (const auto& b : m_batches) {
        QJsonObject batch;
        batch[QStringLiteral("id")]    = b.id;
        batch[QStringLiteral("label")] = b.label;
        batch[QStringLiteral("at")]    = b.at.toString(Qt::ISODate);

        QJsonArray ops;
        for (const auto& op : b.ops) {
            QJsonObject o;
            o[QStringLiteral("type")]   = opTypeToString(op.type);
            o[QStringLiteral("path")]   = op.path;
            o[QStringLiteral("from")]   = op.previousPath;
            o[QStringLiteral("backup")] = op.backup;
            o[QStringLiteral("detail")] = op.detail;
            ops.append(o);
        }
        batch[QStringLiteral("ops")] = ops;
        batches.append(batch);
    }

    QJsonObject root;
    root[QStringLiteral("batches")] = batches;

    QFile out(journalPath());
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        out.close();
    }
}

void EditJournal::load()
{
    QFile in(journalPath());
    if (!in.open(QIODevice::ReadOnly)) return;

    const auto doc = QJsonDocument::fromJson(in.readAll());
    in.close();
    if (!doc.isObject()) return;

    for (const auto& v : doc.object()[QStringLiteral("batches")].toArray()) {
        const QJsonObject obj = v.toObject();
        EditBatch b;
        b.id    = obj[QStringLiteral("id")].toString();
        b.label = obj[QStringLiteral("label")].toString();
        b.at    = QDateTime::fromString(obj[QStringLiteral("at")].toString(),
                                        Qt::ISODate);

        for (const auto& ov : obj[QStringLiteral("ops")].toArray()) {
            const QJsonObject o = ov.toObject();
            EditOp op;
            op.type         = opTypeFromString(o[QStringLiteral("type")].toString());
            op.path         = o[QStringLiteral("path")].toString();
            op.previousPath = o[QStringLiteral("from")].toString();
            op.backup       = o[QStringLiteral("backup")].toString();
            op.detail       = o[QStringLiteral("detail")].toString();
            b.ops.append(op);
        }
        m_batches.append(b);
    }
}
