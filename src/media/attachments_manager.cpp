// -------------------------------------------------------
// attachments_manager.cpp
// -------------------------------------------------------

#include "attachments_manager.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QSet>

// ============================================================
AttachmentsManager::AttachmentsManager(QObject* parent)
    : QObject(parent)
{
}

// ============================================================
// Добавление файлов
// ============================================================

int AttachmentsManager::addFiles(const QStringList& paths)
{
    int added = 0;
    for (const auto& p : paths) {
        added += addFile(p);
    }
    return added;
}

int AttachmentsManager::addFile(const QString& path)
{
    if (path.isEmpty()) return 0;
    if (m_items.size() >= MAX_ATTACHMENTS) return 0;

    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile() || !fi.isReadable()) return 0;

    const QString abs = fi.absoluteFilePath();

    // Проверка дубликата
    for (const auto& it : m_items) {
        if (it.filePath == abs) return 0;
    }

    Attachment a;
    a.filePath    = abs;
    a.displayName = fi.fileName();
    a.sizeBytes   = fi.size();
    a.isBinary    = !isTextFile(abs);
    a.isTooLarge  = (a.sizeBytes > MAX_FILE_SIZE);
    a.addedAt     = QDateTime::currentDateTime();

    m_items.append(a);
    emit changed();
    return 1;
}

void AttachmentsManager::removeAt(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    m_items.removeAt(index);
    emit changed();
}

void AttachmentsManager::clear()
{
    if (m_items.isEmpty()) return;
    m_items.clear();
    emit changed();
}

// ============================================================
// Размеры
// ============================================================

qint64 AttachmentsManager::totalSize() const
{
    qint64 sum = 0;
    for (const auto& it : m_items) sum += it.sizeBytes;
    return sum;
}

QString AttachmentsManager::totalSizeHuman() const
{
    return humanSize(totalSize());
}

QString AttachmentsManager::humanSize(qint64 bytes)
{
    if (bytes < 1024)                return QString::number(bytes) + QStringLiteral(" Б");
    if (bytes < 1024LL * 1024)       return QString::number(bytes / 1024.0, 'f', 1)
                                            + QStringLiteral(" КБ");
    if (bytes < 1024LL * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024.0), 'f', 1)
                                            + QStringLiteral(" МБ");
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + QStringLiteral(" ГБ");
}

// ============================================================
// Проверка: текстовый файл или бинарный
// ============================================================

bool AttachmentsManager::isTextFile(const QString& path)
{
    // 1. По расширению — быстрый путь
    static const QSet<QString> textExts = {
        // Код
        QStringLiteral("cpp"), QStringLiteral("cxx"), QStringLiteral("cc"),
        QStringLiteral("c"),   QStringLiteral("h"),   QStringLiteral("hpp"),
        QStringLiteral("hxx"), QStringLiteral("inl"), QStringLiteral("ipp"),
        QStringLiteral("cs"),  QStringLiteral("java"),QStringLiteral("kt"),
        QStringLiteral("py"),  QStringLiteral("pyw"), QStringLiteral("rb"),
        QStringLiteral("js"),  QStringLiteral("jsx"), QStringLiteral("ts"),
        QStringLiteral("tsx"), QStringLiteral("vue"), QStringLiteral("svelte"),
        QStringLiteral("go"),  QStringLiteral("rs"),  QStringLiteral("swift"),
        QStringLiteral("php"), QStringLiteral("lua"), QStringLiteral("pl"),
        QStringLiteral("sh"),  QStringLiteral("bash"),QStringLiteral("zsh"),
        QStringLiteral("ps1"), QStringLiteral("bat"), QStringLiteral("cmd"),
        QStringLiteral("r"),   QStringLiteral("m"),   QStringLiteral("mm"),
        QStringLiteral("sql"), QStringLiteral("dart"),
        // Разметка / конфиг
        QStringLiteral("txt"), QStringLiteral("md"),  QStringLiteral("rst"),
        QStringLiteral("json"),QStringLiteral("yaml"),QStringLiteral("yml"),
        QStringLiteral("toml"),QStringLiteral("ini"), QStringLiteral("cfg"),
        QStringLiteral("conf"),QStringLiteral("env"), QStringLiteral("properties"),
        QStringLiteral("xml"), QStringLiteral("html"),QStringLiteral("htm"),
        QStringLiteral("css"), QStringLiteral("scss"),QStringLiteral("sass"),
        QStringLiteral("less"),QStringLiteral("svg"),
        // Билд-системы
        QStringLiteral("cmake"),QStringLiteral("gradle"),QStringLiteral("make"),
        QStringLiteral("mk"),   QStringLiteral("pro"),   QStringLiteral("pri"),
        // Логи / данные
        QStringLiteral("log"),  QStringLiteral("csv"),  QStringLiteral("tsv"),
        // Unreal / Unity
        QStringLiteral("uproject"),QStringLiteral("uplugin"),QStringLiteral("uasset"),
        QStringLiteral("ush"),     QStringLiteral("usf"),
    };

    const QString ext = QFileInfo(path).suffix().toLower();
    if (textExts.contains(ext)) return true;

    // Спецфайлы без расширения
    const QString name = QFileInfo(path).fileName().toLower();
    if (name == QStringLiteral("cmakelists.txt") ||
        name == QStringLiteral("dockerfile")     ||
        name == QStringLiteral("makefile")       ||
        name == QStringLiteral("license")        ||
        name == QStringLiteral("readme")         ||
        name == QStringLiteral(".gitignore")     ||
        name == QStringLiteral(".gitattributes") ||
        name == QStringLiteral(".editorconfig")) {
        return true;
    }

    // 2. Эвристика по содержимому: первые 2КБ не содержат NUL?
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray head = f.read(2048);
    f.close();
    if (head.isEmpty()) return true; // пустой файл — не считаем бинарным
    if (head.contains('\0')) return false;

    return true;
}

// ============================================================
// Сборка блока прикреплений для запроса
// ============================================================

QString AttachmentsManager::buildAttachmentBlock(int totalCharsBudget) const
{
    if (m_items.isEmpty()) return QString();

    QString block;
    block.reserve(8192);

    block += QStringLiteral("\n\n--- Прикреплённые файлы (от пользователя через «скрепку») ---\n");
    block += QStringLiteral("# Это АВТОРИТЕТНЫЕ источники. Используй их содержимое напрямую, "
                            "НЕ проси пользователя повторно прислать код из этих файлов.\n");

    // Делим бюджет равномерно между текстовыми файлами
    QVector<int> textIdx;
    for (int i = 0; i < m_items.size(); ++i) {
        if (!m_items[i].isBinary && !m_items[i].isTooLarge) {
            textIdx.append(i);
        }
    }

    const int perFileBudget = textIdx.isEmpty()
                            ? totalCharsBudget
                            : qMax(2000, totalCharsBudget / textIdx.size());

    int remaining = totalCharsBudget;

    for (int i = 0; i < m_items.size(); ++i) {
        const auto& a = m_items[i];

        if (a.isTooLarge) {
            block += QStringLiteral("\n### ATTACHED: ") + a.displayName
                   + QStringLiteral(" — (пропущен: ") + humanSize(a.sizeBytes)
                   + QStringLiteral(" > лимит 2 МБ)\n");
            continue;
        }

        if (a.isBinary) {
            block += QStringLiteral("\n### ATTACHED: ") + a.displayName
                   + QStringLiteral(" — (бинарный, ") + humanSize(a.sizeBytes)
                   + QStringLiteral(", содержимое не читается; путь: ")
                   + a.filePath + QStringLiteral(")\n");
            continue;
        }

        QFile f(a.filePath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            block += QStringLiteral("\n### ATTACHED: ") + a.displayName
                   + QStringLiteral(" — (не удалось открыть)\n");
            continue;
        }

        QTextStream in(&f);
        in.setEncoding(QStringConverter::Utf8);
        QString content = in.readAll();
        f.close();

        const int budget = qMin(perFileBudget, remaining);
        if (budget < 500) {
            block += QStringLiteral("\n### ATTACHED: ") + a.displayName
                   + QStringLiteral(" — (пропущен: исчерпан суммарный лимит контекста)\n");
            continue;
        }

        bool trimmed = false;
        if (content.size() > budget) {
            content = content.left(budget);
            trimmed = true;
        }

        block += QStringLiteral("\n### ATTACHED: ") + a.displayName
               + QStringLiteral("  (путь: ") + a.filePath
               + QStringLiteral(", ") + humanSize(a.sizeBytes) + QStringLiteral(")\n");
        block += QStringLiteral("```\n");
        block += content;
        if (trimmed) {
            block += QStringLiteral("\n// ... (файл обрезан по лимиту контекста) ...");
        }
        block += QStringLiteral("\n```\n");

        remaining -= content.size();
        if (remaining <= 500) break;
    }

    block += QStringLiteral("--- Конец прикреплённых файлов ---\n");
    return block;
}
