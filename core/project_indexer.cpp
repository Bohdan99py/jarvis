// -------------------------------------------------------
// project_indexer.cpp — Локальный индексатор C++ проектов
// -------------------------------------------------------

#include "project_indexer.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>

// Расширения для индексации
const QStringList ProjectIndexer::s_sourceExtensions = {
    QStringLiteral("*.h"), QStringLiteral("*.hpp"), QStringLiteral("*.hxx"),
    QStringLiteral("*.cpp"), QStringLiteral("*.cc"), QStringLiteral("*.cxx"),
    QStringLiteral("*.c"), QStringLiteral("*.inl")
};

// ============================================================
// CodeSymbol
// ============================================================

QString CodeSymbol::kindToString() const
{
    switch (kind) {
    case Class:     return QStringLiteral("class");
    case Struct:    return QStringLiteral("struct");
    case Enum:      return QStringLiteral("enum");
    case Function:  return QStringLiteral("function");
    case Method:    return QStringLiteral("method");
    case Variable:  return QStringLiteral("variable");
    case Macro:     return QStringLiteral("macro");
    case Include:   return QStringLiteral("include");
    case UClass:    return QStringLiteral("UCLASS");
    case UFunction: return QStringLiteral("UFUNCTION");
    case UProperty: return QStringLiteral("UPROPERTY");
    default:        return QStringLiteral("unknown");
    }
}

QJsonObject CodeSymbol::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("kind")]   = static_cast<int>(kind);
    obj[QStringLiteral("name")]   = name;
    obj[QStringLiteral("parent")] = parentClass;
    obj[QStringLiteral("sig")]    = signature;
    obj[QStringLiteral("file")]   = filePath;
    obj[QStringLiteral("line")]   = lineStart;
    obj[QStringLiteral("end")]    = lineEnd;
    obj[QStringLiteral("brief")]  = brief;
    return obj;
}

CodeSymbol CodeSymbol::fromJson(const QJsonObject& obj)
{
    CodeSymbol s;
    s.kind        = static_cast<Kind>(obj[QStringLiteral("kind")].toInt());
    s.name        = obj[QStringLiteral("name")].toString();
    s.parentClass = obj[QStringLiteral("parent")].toString();
    s.signature   = obj[QStringLiteral("sig")].toString();
    s.filePath    = obj[QStringLiteral("file")].toString();
    s.lineStart   = obj[QStringLiteral("line")].toInt();
    s.lineEnd     = obj[QStringLiteral("end")].toInt();
    s.brief       = obj[QStringLiteral("brief")].toString();
    return s;
}

// ============================================================
// IndexedFile
// ============================================================

QJsonObject IndexedFile::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("path")]     = filePath;
    obj[QStringLiteral("rel")]      = relativePath;
    obj[QStringLiteral("size")]     = fileSize;
    obj[QStringLiteral("lines")]    = lineCount;
    obj[QStringLiteral("modified")] = lastModified.toString(Qt::ISODate);
    obj[QStringLiteral("indexed")]  = lastIndexed.toString(Qt::ISODate);

    QJsonArray incArr;
    for (const auto& inc : includes) incArr.append(inc);
    obj[QStringLiteral("includes")] = incArr;

    QJsonArray symArr;
    for (const auto& sym : symbols) symArr.append(sym.toJson());
    obj[QStringLiteral("symbols")] = symArr;

    return obj;
}

IndexedFile IndexedFile::fromJson(const QJsonObject& obj)
{
    IndexedFile f;
    f.filePath     = obj[QStringLiteral("path")].toString();
    f.relativePath = obj[QStringLiteral("rel")].toString();
    f.fileSize     = obj[QStringLiteral("size")].toInteger();
    f.lineCount    = obj[QStringLiteral("lines")].toInt();
    f.lastModified = QDateTime::fromString(obj[QStringLiteral("modified")].toString(), Qt::ISODate);
    f.lastIndexed  = QDateTime::fromString(obj[QStringLiteral("indexed")].toString(), Qt::ISODate);

    for (const auto& v : obj[QStringLiteral("includes")].toArray())
        f.includes.append(v.toString());

    for (const auto& v : obj[QStringLiteral("symbols")].toArray())
        f.symbols.append(CodeSymbol::fromJson(v.toObject()));

    return f;
}

// ============================================================
// Конструктор / Деструктор
// ============================================================

ProjectIndexer::ProjectIndexer(QObject* parent)
    : QObject(parent)
{
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &ProjectIndexer::onFileChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &ProjectIndexer::onDirectoryChanged);
}

ProjectIndexer::~ProjectIndexer()
{
    saveIndex();
}

// ============================================================
// Установка проекта
// ============================================================

void ProjectIndexer::setProjectRoot(const QString& path)
{
    if (m_projectRoot == path) return;

    m_projectRoot = path;
    m_files.clear();

    // Пробуем загрузить кэшированный индекс
    loadIndex();
}

// ============================================================
// Полная индексация
// ============================================================

void ProjectIndexer::indexProject()
{
    if (m_projectRoot.isEmpty() || m_indexing) return;

    m_indexing = true;
    QStringList files = collectSourceFiles(m_projectRoot);

    emit indexingStarted(files.size());

    m_files.clear();

    for (int i = 0; i < files.size(); ++i) {
        IndexedFile indexed = parseFile(files[i]);
        m_files[files[i]] = indexed;

        emit indexingProgress(i + 1, files.size());
    }

    // Настраиваем watcher
    if (m_watcherEnabled) {
        enableFileWatcher(true);
    }

    m_indexing = false;

    emit indexingFinished(m_files.size(), symbolCount());

    // Сохраняем индекс на диск
    saveIndex();
}

// ============================================================
// Индексация одного файла
// ============================================================

void ProjectIndexer::indexFile(const QString& filePath)
{
    if (!QFile::exists(filePath)) {
        m_files.remove(filePath);
        return;
    }

    IndexedFile indexed = parseFile(filePath);
    m_files[filePath] = indexed;

    emit fileReindexed(filePath);
}

// ============================================================
// Парсинг файла
// ============================================================

IndexedFile ProjectIndexer::parseFile(const QString& filePath) const
{
    IndexedFile result;
    result.filePath = filePath;
    result.relativePath = relativePath(filePath);

    QFileInfo fi(filePath);
    result.fileSize = fi.size();
    result.lastModified = fi.lastModified();
    result.lastIndexed = QDateTime::currentDateTime();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    QStringList lines;

    while (!in.atEnd()) {
        lines.append(in.readLine());
    }
    file.close();

    result.lineCount = lines.size();

    // Парсим символы
    parseSymbols(result, lines);

    return result;
}

void ProjectIndexer::parseSymbols(IndexedFile& file, const QStringList& lines) const
{
    // Регулярки для парсинга C++
    static const QRegularExpression reInclude(
        QStringLiteral(R"(^\s*#include\s*[<"](.+?)[>"])"));

    static const QRegularExpression reClass(
        QStringLiteral(R"(^\s*(?:class|struct)\s+(?:\w+\s+)*(\w+)\s*(?::|\{|$))"));

    static const QRegularExpression reEnum(
        QStringLiteral(R"(^\s*enum\s+(?:class\s+)?(\w+))"));

    static const QRegularExpression reFunction(
        QStringLiteral(R"(^\s*(?:virtual\s+|static\s+|inline\s+|explicit\s+|constexpr\s+)*)"
                       R"((?:[\w:*&<>]+\s+)+(\w+)\s*\(([^)]*)\)\s*(?:const\s*)?(?:override\s*)?(?:=\s*\w+\s*)?\{?)"));

    static const QRegularExpression reMethodImpl(
        QStringLiteral(R"(^\s*(?:[\w:*&<>]+\s+)+(\w+)::(\w+)\s*\(([^)]*)\))"));

    static const QRegularExpression reMacro(
        QStringLiteral(R"(^\s*#define\s+(\w+))"));

    // Unreal Engine специфика
    static const QRegularExpression reUClass(
        QStringLiteral(R"(UCLASS\s*\()"));
    static const QRegularExpression reUFunction(
        QStringLiteral(R"(UFUNCTION\s*\()"));
    static const QRegularExpression reUProperty(
        QStringLiteral(R"(UPROPERTY\s*\()"));

    QString currentClass;
    QString prevComment;
    bool inBlockComment = false;

    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines[i];
        int lineNum = i + 1;

        // Блочные комментарии
        if (inBlockComment) {
            if (line.contains(QStringLiteral("*/"))) {
                inBlockComment = false;
            }
            continue;
        }
        if (line.contains(QStringLiteral("/*"))) {
            if (!line.contains(QStringLiteral("*/")))
                inBlockComment = true;
        }

        // Собираем однострочные комментарии для brief
        if (line.trimmed().startsWith(QStringLiteral("//"))) {
            QString comment = line.trimmed().mid(2).trimmed();
            if (!comment.isEmpty() && comment.length() < 120) {
                prevComment = comment;
            }
            continue;
        }

        // #include
        auto matchInc = reInclude.match(line);
        if (matchInc.hasMatch()) {
            file.includes.append(matchInc.captured(1));
            continue;
        }

        // UCLASS
        if (reUClass.match(line).hasMatch()) {
            // Следующая строка обычно содержит class name
            if (i + 1 < lines.size()) {
                auto classMatch = reClass.match(lines[i + 1]);
                if (classMatch.hasMatch()) {
                    CodeSymbol sym;
                    sym.kind = CodeSymbol::UClass;
                    sym.name = classMatch.captured(1);
                    sym.filePath = file.filePath;
                    sym.lineStart = lineNum;
                    sym.brief = prevComment;
                    file.symbols.append(sym);
                    currentClass = sym.name;
                }
            }
            prevComment.clear();
            continue;
        }

        // UFUNCTION
        if (reUFunction.match(line).hasMatch()) {
            if (i + 1 < lines.size()) {
                auto funcMatch = reFunction.match(lines[i + 1]);
                if (funcMatch.hasMatch()) {
                    CodeSymbol sym;
                    sym.kind = CodeSymbol::UFunction;
                    sym.name = funcMatch.captured(1);
                    sym.parentClass = currentClass;
                    sym.signature = lines[i + 1].trimmed();
                    sym.filePath = file.filePath;
                    sym.lineStart = lineNum + 1;
                    sym.brief = prevComment;
                    file.symbols.append(sym);
                }
            }
            prevComment.clear();
            continue;
        }

        // UPROPERTY
        if (reUProperty.match(line).hasMatch()) {
            if (i + 1 < lines.size()) {
                CodeSymbol sym;
                sym.kind = CodeSymbol::UProperty;
                sym.name = lines[i + 1].trimmed();
                sym.parentClass = currentClass;
                sym.filePath = file.filePath;
                sym.lineStart = lineNum + 1;
                sym.brief = prevComment;
                file.symbols.append(sym);
            }
            prevComment.clear();
            continue;
        }

        // class / struct
        auto classMatch = reClass.match(line);
        if (classMatch.hasMatch()) {
            QString name = classMatch.captured(1);
            // Пропускаем forward declarations
            if (!line.trimmed().endsWith(QChar(';'))) {
                CodeSymbol sym;
                sym.kind = line.trimmed().startsWith(QStringLiteral("struct"))
                               ? CodeSymbol::Struct : CodeSymbol::Class;
                sym.name = name;
                sym.filePath = file.filePath;
                sym.lineStart = lineNum;
                sym.brief = prevComment;
                file.symbols.append(sym);
                currentClass = name;
            }
            prevComment.clear();
            continue;
        }

        // enum
        auto enumMatch = reEnum.match(line);
        if (enumMatch.hasMatch()) {
            CodeSymbol sym;
            sym.kind = CodeSymbol::Enum;
            sym.name = enumMatch.captured(1);
            sym.filePath = file.filePath;
            sym.lineStart = lineNum;
            sym.brief = prevComment;
            file.symbols.append(sym);
            prevComment.clear();
            continue;
        }

        // Реализация метода: ClassName::MethodName(...)
        auto methodMatch = reMethodImpl.match(line);
        if (methodMatch.hasMatch()) {
            CodeSymbol sym;
            sym.kind = CodeSymbol::Method;
            sym.parentClass = methodMatch.captured(1);
            sym.name = methodMatch.captured(2);
            sym.signature = line.trimmed();
            sym.filePath = file.filePath;
            sym.lineStart = lineNum;
            sym.brief = prevComment;

            // Пробуем найти конец функции (ищем закрывающую скобку)
            int braceCount = 0;
            bool foundOpen = false;
            for (int j = i; j < qMin(i + 500, lines.size()); ++j) {
                for (QChar ch : lines[j]) {
                    if (ch == QChar('{')) { braceCount++; foundOpen = true; }
                    if (ch == QChar('}')) braceCount--;
                }
                if (foundOpen && braceCount <= 0) {
                    sym.lineEnd = j + 1;
                    break;
                }
            }

            file.symbols.append(sym);
            prevComment.clear();
            continue;
        }

        // Свободная функция
        auto funcMatch = reFunction.match(line);
        if (funcMatch.hasMatch() && !line.trimmed().endsWith(QChar(';'))) {
            QString name = funcMatch.captured(1);
            // Пропускаем ключевые слова
            static const QStringList keywords = {
                QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("for"),
                QStringLiteral("while"), QStringLiteral("switch"), QStringLiteral("return"),
                QStringLiteral("delete"), QStringLiteral("new"), QStringLiteral("throw"),
                QStringLiteral("catch"), QStringLiteral("emit")
            };
            if (keywords.contains(name)) {
                prevComment.clear();
                continue;
            }

            CodeSymbol sym;
            sym.kind = CodeSymbol::Function;
            sym.name = name;
            sym.parentClass = currentClass;
            sym.signature = line.trimmed();
            sym.filePath = file.filePath;
            sym.lineStart = lineNum;
            sym.brief = prevComment;
            file.symbols.append(sym);
            prevComment.clear();
            continue;
        }

        // #define
        auto macroMatch = reMacro.match(line);
        if (macroMatch.hasMatch()) {
            CodeSymbol sym;
            sym.kind = CodeSymbol::Macro;
            sym.name = macroMatch.captured(1);
            sym.filePath = file.filePath;
            sym.lineStart = lineNum;
            file.symbols.append(sym);
            prevComment.clear();
            continue;
        }

        // Сбрасываем комментарий если между ним и символом пустая строка
        if (!line.trimmed().isEmpty()) {
            prevComment.clear();
        }
    }
}

// ============================================================
// Поиск
// ============================================================

QVector<CodeSymbol> ProjectIndexer::findSymbol(const QString& name, bool exact) const
{
    QVector<CodeSymbol> results;
    QString lower = name.toLower();

    for (const auto& file : m_files) {
        for (const auto& sym : file.symbols) {
            if (exact) {
                if (sym.name.compare(name, Qt::CaseInsensitive) == 0)
                    results.append(sym);
            } else {
                if (sym.name.toLower().contains(lower))
                    results.append(sym);
            }
        }
    }

    return results;
}

QVector<IndexedFile> ProjectIndexer::findFile(const QString& name) const
{
    QVector<IndexedFile> results;
    QString lower = name.toLower();

    for (const auto& file : m_files) {
        if (file.relativePath.toLower().contains(lower))
            results.append(file);
    }

    return results;
}

QVector<ProjectIndexer::GrepResult> ProjectIndexer::grep(const QString& pattern,
                                                          int maxResults) const
{
    QVector<GrepResult> results;
    QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);

    for (const auto& indexed : m_files) {
        QFile file(indexed.filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

        QTextStream in(&file);
        int lineNum = 0;
        while (!in.atEnd()) {
            QString line = in.readLine();
            lineNum++;

            if (re.match(line).hasMatch() || line.contains(pattern, Qt::CaseInsensitive)) {
                GrepResult r;
                r.filePath = indexed.relativePath;
                r.line = lineNum;
                r.lineText = line.trimmed();
                results.append(r);

                if (results.size() >= maxResults) {
                    file.close();
                    return results;
                }
            }
        }
        file.close();
    }

    return results;
}

// ============================================================
// Извлечение фрагментов кода
// ============================================================

QString ProjectIndexer::getCodeSnippet(const CodeSymbol& symbol, int contextLines) const
{
    int start = qMax(1, symbol.lineStart - contextLines);
    int end = symbol.lineEnd > 0
                  ? symbol.lineEnd + contextLines
                  : symbol.lineStart + contextLines + 30;

    return getFileLines(symbol.filePath, start, end);
}

QString ProjectIndexer::getFileLines(const QString& filePath, int startLine, int endLine) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    QTextStream in(&file);
    QString result;
    int lineNum = 0;

    while (!in.atEnd()) {
        QString line = in.readLine();
        lineNum++;

        if (lineNum >= startLine && lineNum <= endLine) {
            result += QString::number(lineNum) + QStringLiteral("  ") + line + QChar('\n');
        }

        if (lineNum > endLine) break;
    }

    file.close();
    return result;
}

// ============================================================
// Статистика и карта проекта
// ============================================================

int ProjectIndexer::symbolCount() const
{
    int count = 0;
    for (const auto& file : m_files) {
        count += file.symbols.size();
    }
    return count;
}

QStringList ProjectIndexer::allClasses() const
{
    QStringList classes;
    for (const auto& file : m_files) {
        for (const auto& sym : file.symbols) {
            if (sym.kind == CodeSymbol::Class || sym.kind == CodeSymbol::Struct
                || sym.kind == CodeSymbol::UClass) {
                if (!classes.contains(sym.name))
                    classes.append(sym.name);
            }
        }
    }
    classes.sort();
    return classes;
}

QStringList ProjectIndexer::allFiles() const
{
    QStringList files;
    for (const auto& file : m_files) {
        files.append(file.relativePath);
    }
    files.sort();
    return files;
}

QString ProjectIndexer::projectMap() const
{
    if (m_files.isEmpty())
        return QStringLiteral("Проект не проиндексирован.");

    QString map;
    map += QStringLiteral("Проект: ") + QFileInfo(m_projectRoot).fileName() + QStringLiteral("\n");
    map += QStringLiteral("Файлов: ") + QString::number(m_files.size())
         + QStringLiteral(", Символов: ") + QString::number(symbolCount()) + QStringLiteral("\n\n");

    // Группируем по файлам
    for (const auto& file : m_files) {
        map += QStringLiteral("--- ") + file.relativePath
             + QStringLiteral(" (") + QString::number(file.lineCount)
             + QStringLiteral(" строк) ---\n");

        for (const auto& sym : file.symbols) {
            if (sym.kind == CodeSymbol::Include || sym.kind == CodeSymbol::Macro)
                continue;

            map += QStringLiteral("  ");
            if (!sym.parentClass.isEmpty() && sym.kind == CodeSymbol::Method) {
                map += sym.parentClass + QStringLiteral("::");
            }
            map += sym.name;
            map += QStringLiteral(" [") + sym.kindToString() + QStringLiteral(", L")
                 + QString::number(sym.lineStart) + QStringLiteral("]");

            if (!sym.brief.isEmpty()) {
                map += QStringLiteral(" — ") + sym.brief;
            }
            map += QChar('\n');
        }
        map += QChar('\n');
    }

    return map;
}

QString ProjectIndexer::detailedMap() const
{
    return projectMap(); // Для простоты пока одинаковы
}

// ============================================================
// Сохранение / загрузка индекса
// ============================================================

QString ProjectIndexer::indexFilePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);

    // Используем хэш пути проекта для уникального имени
    QString hash = QString::number(qHash(m_projectRoot));
    return dir + QStringLiteral("/project_index_") + hash + QStringLiteral(".json");
}

void ProjectIndexer::saveIndex() const
{
    if (m_files.isEmpty()) return;

    QJsonObject root;
    root[QStringLiteral("projectRoot")] = m_projectRoot;
    root[QStringLiteral("timestamp")]   = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonArray filesArr;
    for (const auto& file : m_files) {
        filesArr.append(file.toJson());
    }
    root[QStringLiteral("files")] = filesArr;

    QFile out(indexFilePath());
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        out.close();
    }
}

void ProjectIndexer::loadIndex()
{
    QFile in(indexFilePath());
    if (!in.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(in.readAll());
    in.close();

    if (!doc.isObject()) return;

    QJsonObject root = doc.object();

    // Проверяем что это индекс для нашего проекта
    if (root[QStringLiteral("projectRoot")].toString() != m_projectRoot)
        return;

    QJsonArray filesArr = root[QStringLiteral("files")].toArray();
    for (const auto& val : filesArr) {
        IndexedFile f = IndexedFile::fromJson(val.toObject());

        // Проверяем актуальность: если файл изменился — переиндексируем
        QFileInfo fi(f.filePath);
        if (fi.exists() && fi.lastModified() > f.lastIndexed) {
            f = parseFile(f.filePath);
        }

        m_files[f.filePath] = f;
    }
}

// ============================================================
// File Watcher
// ============================================================

void ProjectIndexer::enableFileWatcher(bool enable)
{
    m_watcherEnabled = enable;

    // Убираем старые watch
    if (!m_watcher->files().isEmpty())
        m_watcher->removePaths(m_watcher->files());
    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());

    if (!enable || m_projectRoot.isEmpty()) return;

    // Добавляем все файлы и директории
    m_watcher->addPath(m_projectRoot);

    for (const auto& file : m_files) {
        m_watcher->addPath(file.filePath);
    }

    // Добавляем поддиректории
    QDirIterator dirIt(m_projectRoot, QDir::Dirs | QDir::NoDotAndDotDot,
                       QDirIterator::Subdirectories);
    while (dirIt.hasNext()) {
        QString dir = dirIt.next();
        // Пропускаем build, .git и т.д.
        QString name = QFileInfo(dir).fileName();
        if (name.startsWith(QChar('.')) || name == QStringLiteral("build")
            || name == QStringLiteral("Binaries") || name == QStringLiteral("Intermediate")
            || name == QStringLiteral("Saved") || name == QStringLiteral("DerivedDataCache")) {
            continue;
        }
        m_watcher->addPath(dir);
    }
}

void ProjectIndexer::onFileChanged(const QString& path)
{
    if (m_indexing) return;

    // Переиндексируем файл
    indexFile(path);

    // Re-добавляем в watcher (Qt убирает после изменения)
    if (QFile::exists(path) && !m_watcher->files().contains(path)) {
        m_watcher->addPath(path);
    }
}

void ProjectIndexer::onDirectoryChanged(const QString& path)
{
    Q_UNUSED(path)
    if (m_indexing) return;

    // Проверяем новые файлы
    QStringList currentFiles = collectSourceFiles(m_projectRoot);
    for (const auto& filePath : currentFiles) {
        if (!m_files.contains(filePath)) {
            indexFile(filePath);
            if (m_watcherEnabled) {
                m_watcher->addPath(filePath);
            }
        }
    }
}

// ============================================================
// Утилиты
// ============================================================

QStringList ProjectIndexer::collectSourceFiles(const QString& dir) const
{
    QStringList result;

    QDirIterator it(dir, s_sourceExtensions,
                    QDir::Files | QDir::Readable,
                    QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString path = it.next();

        // Пропускаем build, .git, промежуточные
        if (path.contains(QStringLiteral("/build/"))
            || path.contains(QStringLiteral("\\build\\"))
            || path.contains(QStringLiteral("/.git/"))
            || path.contains(QStringLiteral("\\.git\\"))
            || path.contains(QStringLiteral("/cmake-build"))
            || path.contains(QStringLiteral("\\cmake-build"))
            || path.contains(QStringLiteral("/Intermediate/"))
            || path.contains(QStringLiteral("\\Intermediate\\"))
            || path.contains(QStringLiteral("/DerivedDataCache"))
            || path.contains(QStringLiteral("\\DerivedDataCache"))) {
            continue;
        }

        result.append(path);
    }

    return result;
}

QString ProjectIndexer::relativePath(const QString& absPath) const
{
    if (m_projectRoot.isEmpty()) return absPath;

    QDir root(m_projectRoot);
    return root.relativeFilePath(absPath);
}
