// -------------------------------------------------------
// project_indexer.cpp — Локальный индексатор C++ проектов
// -------------------------------------------------------

#include "project_indexer.h"
#include "jarvis_paths.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QSet>
#include <QDebug>

// Расширения для индексации.
//
// Индексатор перестал быть «C++-only»: настоящий проект — это ещё QML,
// сборочные скрипты, ресурсы, конфиги и документация. Пока их не было в
// индексе, ассистент видел половину картины: предлагал добавить .cpp и
// забывал про CMakeLists.txt, правил QML вслепую и не знал про .qrc.
const QStringList ProjectIndexer::s_sourceExtensions = {
    // C/C++
    QStringLiteral("*.h"), QStringLiteral("*.hpp"), QStringLiteral("*.hxx"),
    QStringLiteral("*.cpp"), QStringLiteral("*.cc"), QStringLiteral("*.cxx"),
    QStringLiteral("*.c"), QStringLiteral("*.inl"),
    // Qt: интерфейсы, ресурсы, формы
    QStringLiteral("*.qml"), QStringLiteral("*.qrc"), QStringLiteral("*.ui"),
    QStringLiteral("*.pro"), QStringLiteral("*.pri"),
    // Сборка
    QStringLiteral("CMakeLists.txt"), QStringLiteral("*.cmake"),
    // Другие языки и скрипты
    QStringLiteral("*.py"), QStringLiteral("*.js"), QStringLiteral("*.mjs"),
    QStringLiteral("*.ts"), QStringLiteral("*.cs"), QStringLiteral("*.java"),
    QStringLiteral("*.bat"), QStringLiteral("*.ps1"), QStringLiteral("*.sh"),
    // Шейдеры
    QStringLiteral("*.glsl"), QStringLiteral("*.vert"), QStringLiteral("*.frag"),
    QStringLiteral("*.hlsl"), QStringLiteral("*.usf"),
    // Конфиги и документация
    QStringLiteral("*.json"), QStringLiteral("*.yml"), QStringLiteral("*.yaml"),
    QStringLiteral("*.ini"), QStringLiteral("*.toml"), QStringLiteral("*.iss"),
    QStringLiteral("*.md")
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
    case QmlComponent: return QStringLiteral("qml-component");
    case QmlProperty:  return QStringLiteral("qml-property");
    case QmlSignal:    return QStringLiteral("qml-signal");
    case BuildTarget:  return QStringLiteral("build-target");
    case Resource:     return QStringLiteral("resource");
    case Todo:         return QStringLiteral("todo");
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
    obj[QStringLiteral("lang")]     = language;
    obj[QStringLiteral("size")]     = fileSize;
    obj[QStringLiteral("lines")]    = lineCount;
    obj[QStringLiteral("modified")] = lastModified.toString(Qt::ISODate);
    obj[QStringLiteral("indexed")]  = lastIndexed.toString(Qt::ISODate);

    QJsonArray incArr;
    for (const auto& inc : includes) incArr.append(inc);
    obj[QStringLiteral("includes")] = incArr;

    QJsonArray localArr;
    for (const auto& inc : localIncludes) localArr.append(inc);
    obj[QStringLiteral("linc")] = localArr;

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
    f.language     = obj[QStringLiteral("lang")].toString();
    f.fileSize     = obj[QStringLiteral("size")].toInteger();
    f.lineCount    = obj[QStringLiteral("lines")].toInt();
    f.lastModified = QDateTime::fromString(obj[QStringLiteral("modified")].toString(), Qt::ISODate);
    f.lastIndexed  = QDateTime::fromString(obj[QStringLiteral("indexed")].toString(), Qt::ISODate);

    for (const auto& v : obj[QStringLiteral("includes")].toArray())
        f.includes.append(v.toString());

    for (const auto& v : obj[QStringLiteral("linc")].toArray())
        f.localIncludes.append(v.toString());

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
    m_symbolCount = 0;

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
    const QStringList files = collectSourceFiles(m_projectRoot);

    // Limit total files to avoid excessive memory usage
    QStringList filesToIndex = files;
    if (filesToIndex.size() > MAX_INDEXED_FILES) {
        qDebug() << "[Indexer] Capping at" << MAX_INDEXED_FILES << "files (total:" << files.size() << ")";
        filesToIndex = filesToIndex.mid(0, MAX_INDEXED_FILES);
    }

    QMap<QString, IndexedFile> newFiles;

    emit indexingStarted(filesToIndex.size());

    for (int i = 0; i < filesToIndex.size(); ++i) {
        const QString& filePath = filesToIndex[i];
        const QFileInfo currentInfo(filePath);

        auto existing = m_files.constFind(filePath);
        if (existing != m_files.cend()
            && currentInfo.exists()
            && existing->lastModified >= currentInfo.lastModified()) {
            newFiles.insert(filePath, *existing);
        } else {
            newFiles.insert(filePath, parseFile(filePath));
        }

        emit indexingProgress(i + 1, filesToIndex.size());
    }

    m_files = std::move(newFiles);
    rebuildCaches();

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
        rebuildCaches();
        return;
    }

    IndexedFile indexed = parseFile(filePath);
    m_files[filePath] = indexed;
    rebuildCaches();

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
    result.language = languageForFile(filePath);

    QFileInfo fi(filePath);
    result.fileSize = fi.size();
    result.lastModified = fi.lastModified();
    result.lastIndexed = QDateTime::currentDateTime();

    // Гигантский файл — почти наверняка генерат или дамп данных: символов
    // из него ноль, а память и время индексации он съест целиком.
    if (result.fileSize > MAX_FILE_BYTES)
        return result;

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

// Диспетчер: у каждого языка свой лёгкий построчный парсер. Цель — не
// компилятор, а карта «что где лежит»: имена, строки, краткие описания.
void ProjectIndexer::parseSymbols(IndexedFile& file, const QStringList& lines) const
{
    const QString lang = file.language.isEmpty()
                             ? languageForFile(file.filePath) : file.language;

    if (lang == QStringLiteral("cpp") || lang == QStringLiteral("cs")
        || lang == QStringLiteral("java") || lang == QStringLiteral("shader")) {
        parseCppSymbols(file, lines);
    } else if (lang == QStringLiteral("qml")) {
        parseQmlSymbols(file, lines);
    } else if (lang == QStringLiteral("python")) {
        parsePythonSymbols(file, lines);
    } else if (lang == QStringLiteral("js")) {
        parseJsSymbols(file, lines);
    } else if (lang == QStringLiteral("cmake") || lang == QStringLiteral("qmake")) {
        parseCMakeSymbols(file, lines);
    } else if (lang == QStringLiteral("qrc")) {
        parseQrcSymbols(file, lines);
    } else if (lang == QStringLiteral("ui")) {
        parseUiSymbols(file, lines);
    } else if (lang == QStringLiteral("md")) {
        parseMarkdownSymbols(file, lines);
    }

    // TODO/FIXME собираем во всех текстовых языках — это сырьё для
    // фоновых рекомендаций (DevAdvisor), а не только для поиска.
    collectTodos(file, lines);
}

void ProjectIndexer::parseCppSymbols(IndexedFile& file, const QStringList& lines) const
{
    // Регулярки для парсинга C++
    static const QRegularExpression reInclude(
        QStringLiteral(R"(^\s*#include\s*(["<])(.+?)[">])"));

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

    file.symbols.reserve(64);

    for (int i = 0; i < lines.size(); ++i) {
        if (file.symbols.size() >= MAX_SYMBOLS_PER_FILE) break;

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
            const QString header = matchInc.captured(2);
            file.includes.append(header);
            if (matchInc.captured(1) == QLatin1Char('"'))
                file.localIncludes.append(header);
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
    return m_symbolCount;
}

QStringList ProjectIndexer::allClasses() const
{
    QSet<QString> classes;
    for (const auto& file : m_files) {
        for (const auto& sym : file.symbols) {
            if (sym.kind == CodeSymbol::Class || sym.kind == CodeSymbol::Struct
                || sym.kind == CodeSymbol::UClass) {
                classes.insert(sym.name);
            }
        }
    }

    QStringList result = classes.values();
    result.sort();
    return result;
}

QStringList ProjectIndexer::allFiles() const
{
    QStringList files;
    files.reserve(m_files.size());
    for (const auto& file : m_files) {
        files.append(file.relativePath);
    }
    files.sort();
    return files;
}

QString ProjectIndexer::detailedMap() const
{
    if (m_files.isEmpty())
        return QStringLiteral("No project indexed.");

    QString map;
    map += QStringLiteral("Project: ") + QFileInfo(m_projectRoot).fileName() + QStringLiteral("\n");
    map += QStringLiteral("Files: ") + QString::number(m_files.size())
         + QStringLiteral(", Symbols: ") + QString::number(symbolCount()) + QStringLiteral("\n\n");

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

// Компактная карта проекта: не перечисление всех символов (оно в
// detailedMap()), а структура — папки, языки, ключевые типы. Именно эта
// карта уходит в system prompt, поэтому она должна оставаться читаемой
// целиком, а не обрываться на середине первой папки.
QString ProjectIndexer::projectMap() const
{
    if (m_files.isEmpty())
        return QStringLiteral("No project indexed.");

    struct DirInfo {
        int files = 0;
        int lines = 0;
        QMap<QString, int> langs;      // язык -> файлов
        QStringList        keyTypes;   // классы/компоненты/таргеты
    };

    QMap<QString, DirInfo> dirs;

    for (const auto& file : m_files) {
        const QString rel = file.relativePath.isEmpty()
                                ? QFileInfo(file.filePath).fileName()
                                : file.relativePath;
        QString dir = rel.section(QChar('/'), 0, -2);
        if (dir.isEmpty()) dir = QStringLiteral(".");

        DirInfo& info = dirs[dir];
        info.files++;
        info.lines += file.lineCount;
        info.langs[file.language.isEmpty() ? QStringLiteral("other") : file.language]++;

        for (const auto& sym : file.symbols) {
            if (info.keyTypes.size() >= 12) break;
            if (sym.kind == CodeSymbol::Class || sym.kind == CodeSymbol::UClass
                || sym.kind == CodeSymbol::Struct
                || sym.kind == CodeSymbol::QmlComponent
                || sym.kind == CodeSymbol::BuildTarget) {
                if (!info.keyTypes.contains(sym.name))
                    info.keyTypes.append(sym.name);
            }
        }
    }

    QString map;
    map += QStringLiteral("Project: ") + QFileInfo(m_projectRoot).fileName()
         + QStringLiteral("\n");
    map += QStringLiteral("Files: ") + QString::number(m_files.size())
         + QStringLiteral(", symbols: ") + QString::number(symbolCount())
         + QStringLiteral("\n");

    const auto stats = languageStats();
    if (!stats.isEmpty()) {
        QStringList parts;
        for (auto it = stats.cbegin(); it != stats.cend(); ++it) {
            parts.append(it.key() + QStringLiteral(" ")
                         + QString::number(it.value().first) + QStringLiteral("f/")
                         + QString::number(it.value().second) + QStringLiteral("l"));
        }
        map += QStringLiteral("Languages: ") + parts.join(QStringLiteral(", "))
             + QStringLiteral("\n");
    }
    map += QStringLiteral("\nLayout (dir — files, lines, key types):\n");

    for (auto it = dirs.cbegin(); it != dirs.cend(); ++it) {
        map += QStringLiteral("  ") + it.key()
             + QStringLiteral(" — ") + QString::number(it.value().files)
             + QStringLiteral(" files, ") + QString::number(it.value().lines)
             + QStringLiteral(" lines");

        QStringList langParts;
        for (auto l = it.value().langs.cbegin(); l != it.value().langs.cend(); ++l)
            langParts.append(l.key());
        if (!langParts.isEmpty())
            map += QStringLiteral(" [") + langParts.join(QStringLiteral(",")) + QChar(']');

        if (!it.value().keyTypes.isEmpty())
            map += QStringLiteral(": ") + it.value().keyTypes.join(QStringLiteral(", "));

        map += QChar('\n');
    }

    return map;
}

// ============================================================
// Сохранение / загрузка индекса
// ============================================================

QString ProjectIndexer::indexFilePath() const
{
    // Используем хэш пути проекта для уникального имени
    QString hash = QString::number(qHash(m_projectRoot));
    return JarvisPaths::subPath(
        QStringLiteral("project_index_") + hash + QStringLiteral(".json"));
}

void ProjectIndexer::saveIndex() const
{
    if (m_files.isEmpty()) return;

    QJsonObject root;
    root[QStringLiteral("version")]     = INDEX_FORMAT_VERSION;
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

    // Формат индекса изменился (новые виды символов, поле языка) — старый
    // кэш читать нельзя: числовые коды Kind в нём означают уже другое.
    // Молча игнорируем — проект переиндексируется заново.
    if (root[QStringLiteral("version")].toInt() != INDEX_FORMAT_VERSION)
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

    rebuildCaches();
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

    QStringList filePaths;
    filePaths.reserve(m_files.size());
    for (const auto& file : m_files) {
        if (QFile::exists(file.filePath)) {
            filePaths.append(file.filePath);
        }
    }
    if (!filePaths.isEmpty()) {
        m_watcher->addPaths(filePaths);
    }

    // Добавляем поддиректории
    QStringList directories;
    QDirIterator dirIt(m_projectRoot, QDir::Dirs | QDir::NoDotAndDotDot,
                       QDirIterator::Subdirectories);
    while (dirIt.hasNext()) {
        QString dir = dirIt.next();
        if (shouldSkipPath(dir)) {
            continue;
        }
        directories.append(dir);
    }
    if (!directories.isEmpty()) {
        m_watcher->addPaths(directories);
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
    const QStringList currentFiles = collectSourceFiles(m_projectRoot);
    QSet<QString> currentSet;
    for (const QString& filePath : currentFiles) {
        currentSet.insert(filePath);
    }

    for (const auto& filePath : currentFiles) {
        if (!m_files.contains(filePath)) {
            indexFile(filePath);
            if (m_watcherEnabled) {
                m_watcher->addPath(filePath);
            }
        }
    }

    QStringList removedFiles;
    for (auto it = m_files.cbegin(); it != m_files.cend(); ++it) {
        if (!currentSet.contains(it.key())) {
            removedFiles.append(it.key());
        }
    }

    if (!removedFiles.isEmpty()) {
        for (const QString& filePath : removedFiles) {
            m_files.remove(filePath);
        }
        rebuildCaches();
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

        if (shouldSkipPath(path)) {
            continue;
        }

        result.append(path);
    }

    // Сортировка по глубине пути: если файлов больше потолка, обрезается
    // хвост — и это должны быть глубоко закопанные файлы, а не src/app.
    std::sort(result.begin(), result.end(),
              [](const QString& a, const QString& b) {
                  const int da = a.count(QChar('/'));
                  const int db = b.count(QChar('/'));
                  if (da != db) return da < db;
                  return a < b;
              });

    return result;
}

QString ProjectIndexer::relativePath(const QString& absPath) const
{
    if (m_projectRoot.isEmpty()) return absPath;

    QDir root(m_projectRoot);
    return root.relativeFilePath(absPath);
}

void ProjectIndexer::rebuildCaches()
{
    m_symbolCount = 0;
    m_includedBy.clear();

    for (const auto& file : m_files) {
        m_symbolCount += file.symbols.size();

        // Обратный граф: ключ — только имя файла, потому что в #include
        // пишут и "jarvis.h", и "engine/jarvis.h" — связь одна и та же.
        for (const auto& inc : file.includes) {
            const QString key = inc.section(QChar('/'), -1).trimmed().toLower();
            if (key.isEmpty()) continue;
            const QString from = file.relativePath.isEmpty()
                                     ? file.filePath : file.relativePath;
            m_includedBy.insert(key, from);
        }
    }
}

bool ProjectIndexer::shouldSkipPath(const QString& path) const
{
    return isIgnoredPath(path, m_projectRoot);
}

// Что НЕ является кодом проекта. Список важнее, чем кажется: пока в нём
// не было build_release/ и redist/, потолок в 2000 файлов целиком уходил
// на исходники вендоренного OpenCV и артефакты сборки — до src/app с QML
// индексатор просто не доходил, и «понимание проекта» начиналось с чужого
// кода.
bool ProjectIndexer::isIgnoredPath(const QString& path, const QString& projectRoot)
{
    QString normalized = QDir::fromNativeSeparators(path);

    // Всё, что выше корня проекта, нас не касается.
    const QString root = QDir::fromNativeSeparators(projectRoot);
    if (!root.isEmpty() && normalized.startsWith(root, Qt::CaseInsensitive))
        normalized = normalized.mid(root.size());

    // Любой компонент пути, начинающийся с точки (.git, .venv, .vs, .idea).
    const QStringList parts = normalized.split(QChar('/'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        if (part.startsWith(QChar('.')) && part.size() > 1) return true;
    }

    static const QStringList ignoredDirs = {
        // вывод сборки
        QStringLiteral("build"), QStringLiteral("builds"),
        QStringLiteral("out"), QStringLiteral("dist"),
        QStringLiteral("bin-release"), QStringLiteral("debug"),
        QStringLiteral("release"), QStringLiteral("obj"),
        // вендоренные зависимости
        QStringLiteral("redist"), QStringLiteral("vendor"),
        QStringLiteral("vendored"), QStringLiteral("third_party"),
        QStringLiteral("thirdparty"), QStringLiteral("3rdparty"),
        QStringLiteral("external"), QStringLiteral("externals"),
        QStringLiteral("node_modules"), QStringLiteral("packages"),
        QStringLiteral("site-packages"), QStringLiteral("__pycache__"),
        QStringLiteral("venv"),
        // Unreal / Qt / прочие артефакты
        QStringLiteral("binaries"), QStringLiteral("intermediate"),
        QStringLiteral("saved"), QStringLiteral("derivedatacache"),
        QStringLiteral("derivedcache"), QStringLiteral("deriveddatacache")
    };

    for (const QString& part : parts) {
        const QString lower = part.toLower();
        if (ignoredDirs.contains(lower)) return true;
        // build_release, cmake-build-debug, build-Desktop_Qt... — один и
        // тот же каталог сборки под разными именами.
        if (lower.startsWith(QStringLiteral("build_"))
            || lower.startsWith(QStringLiteral("build-"))
            || lower.startsWith(QStringLiteral("cmake-build"))) {
            return true;
        }
    }

    return false;
}
QStringList ProjectIndexer::recentFiles(int n) const
{
    if (m_files.isEmpty()) return {};

    // Собираем пары (lastModified, filePath) и сортируем по убыванию даты
    QVector<QPair<QDateTime, QString>> dated;
    dated.reserve(m_files.size());
    for (auto it = m_files.cbegin(); it != m_files.cend(); ++it) {
        dated.append({it.value().lastModified, it.value().filePath});
    }

    std::sort(dated.begin(), dated.end(),
              [](const QPair<QDateTime, QString>& a,
                 const QPair<QDateTime, QString>& b) {
                  return a.first > b.first;  // убывание — самые свежие первые
              });

    QStringList result;
    const int count = qMin(n, static_cast<int>(dated.size()));
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.append(dated[i].second);
    }
    return result;
}
// ============================================================
// Определение языка файла
// ============================================================

QString ProjectIndexer::languageForFile(const QString& filePath)
{
    const QFileInfo fi(filePath);
    const QString name = fi.fileName().toLower();
    const QString ext  = fi.suffix().toLower();

    if (name == QStringLiteral("cmakelists.txt") || ext == QStringLiteral("cmake"))
        return QStringLiteral("cmake");
    if (ext == QStringLiteral("pro") || ext == QStringLiteral("pri"))
        return QStringLiteral("qmake");

    static const QSet<QString> cppExt = {
        QStringLiteral("h"), QStringLiteral("hpp"), QStringLiteral("hxx"),
        QStringLiteral("cpp"), QStringLiteral("cc"), QStringLiteral("cxx"),
        QStringLiteral("c"), QStringLiteral("inl")
    };
    static const QSet<QString> shaderExt = {
        QStringLiteral("glsl"), QStringLiteral("vert"), QStringLiteral("frag"),
        QStringLiteral("hlsl"), QStringLiteral("usf")
    };
    static const QSet<QString> configExt = {
        QStringLiteral("json"), QStringLiteral("yml"), QStringLiteral("yaml"),
        QStringLiteral("ini"), QStringLiteral("toml"), QStringLiteral("iss")
    };
    static const QSet<QString> scriptExt = {
        QStringLiteral("bat"), QStringLiteral("ps1"), QStringLiteral("sh")
    };

    if (cppExt.contains(ext))    return QStringLiteral("cpp");
    if (shaderExt.contains(ext)) return QStringLiteral("shader");
    if (configExt.contains(ext)) return QStringLiteral("config");
    if (scriptExt.contains(ext)) return QStringLiteral("script");

    if (ext == QStringLiteral("qml"))  return QStringLiteral("qml");
    if (ext == QStringLiteral("qrc"))  return QStringLiteral("qrc");
    if (ext == QStringLiteral("ui"))   return QStringLiteral("ui");
    if (ext == QStringLiteral("py"))   return QStringLiteral("python");
    if (ext == QStringLiteral("cs"))   return QStringLiteral("cs");
    if (ext == QStringLiteral("java")) return QStringLiteral("java");
    if (ext == QStringLiteral("js") || ext == QStringLiteral("mjs")
        || ext == QStringLiteral("ts"))
        return QStringLiteral("js");
    if (ext == QStringLiteral("md"))   return QStringLiteral("md");

    return QStringLiteral("other");
}

// ============================================================
// Парсеры не-C++ языков
//
// Все они — построчные и намеренно простые: задача не разобрать язык
// как компилятор, а построить карту «что где лежит», по которой модель
// найдёт нужный файл и строку.
// ============================================================

void ProjectIndexer::parseQmlSymbols(IndexedFile& file, const QStringList& lines) const
{
    static const QRegularExpression reImport(
        QStringLiteral(R"(^\s*import\s+([\w\."/]+))"));
    static const QRegularExpression reComponent(
        QStringLiteral(R"(^(\s*)([A-Z]\w*)\s*\{\s*$)"));
    static const QRegularExpression reId(
        QStringLiteral(R"(^\s*id:\s*(\w+))"));
    static const QRegularExpression reProperty(
        QStringLiteral(R"(^\s*(?:readonly\s+|default\s+|required\s+)*property\s+([\w<>\.]+)\s+(\w+))"));
    static const QRegularExpression reSignal(
        QStringLiteral(R"(^\s*signal\s+(\w+))"));
    static const QRegularExpression reFunction(
        QStringLiteral(R"(^\s*function\s+(\w+)\s*\()"));

    QString currentComponent;
    QString prevComment;

    for (int i = 0; i < lines.size(); ++i) {
        if (file.symbols.size() >= MAX_SYMBOLS_PER_FILE) break;

        const QString& line = lines[i];
        const int lineNum = i + 1;
        const QString trimmed = line.trimmed();

        if (trimmed.startsWith(QStringLiteral("//"))) {
            const QString c = trimmed.mid(2).trimmed();
            if (!c.isEmpty() && c.length() < 120) prevComment = c;
            continue;
        }

        auto mImport = reImport.match(line);
        if (mImport.hasMatch()) {
            file.includes.append(mImport.captured(1));
            continue;
        }

        auto mComp = reComponent.match(line);
        if (mComp.hasMatch()) {
            CodeSymbol sym;
            sym.kind      = CodeSymbol::QmlComponent;
            sym.name      = mComp.captured(2);
            sym.filePath  = file.filePath;
            sym.lineStart = lineNum;
            sym.brief     = prevComment;
            // Корневой компонент (нулевой отступ) задаёт тип всего файла —
            // именно его ищут по имени файла («что такое Modes.qml»).
            if (mComp.captured(1).isEmpty()) currentComponent = sym.name;
            else                             sym.parentClass  = currentComponent;
            file.symbols.append(sym);
            prevComment.clear();
            continue;
        }

        auto mId = reId.match(line);
        if (mId.hasMatch()) {
            CodeSymbol sym;
            sym.kind        = CodeSymbol::QmlComponent;
            sym.name        = mId.captured(1);
            sym.parentClass = currentComponent;
            sym.signature   = QStringLiteral("id");
            sym.filePath    = file.filePath;
            sym.lineStart   = lineNum;
            file.symbols.append(sym);
            prevComment.clear();
            continue;
        }

        auto mProp = reProperty.match(line);
        if (mProp.hasMatch()) {
            CodeSymbol sym;
            sym.kind        = CodeSymbol::QmlProperty;
            sym.name        = mProp.captured(2);
            sym.signature   = mProp.captured(1);
            sym.parentClass = currentComponent;
            sym.filePath    = file.filePath;
            sym.lineStart   = lineNum;
            sym.brief       = prevComment;
            file.symbols.append(sym);
            prevComment.clear();
            continue;
        }

        auto mSig = reSignal.match(line);
        if (mSig.hasMatch()) {
            CodeSymbol sym;
            sym.kind        = CodeSymbol::QmlSignal;
            sym.name        = mSig.captured(1);
            sym.parentClass = currentComponent;
            sym.signature   = trimmed;
            sym.filePath    = file.filePath;
            sym.lineStart   = lineNum;
            sym.brief       = prevComment;
            file.symbols.append(sym);
            prevComment.clear();
            continue;
        }

        auto mFunc = reFunction.match(line);
        if (mFunc.hasMatch()) {
            CodeSymbol sym;
            sym.kind        = CodeSymbol::Function;
            sym.name        = mFunc.captured(1);
            sym.parentClass = currentComponent;
            sym.signature   = trimmed;
            sym.filePath    = file.filePath;
            sym.lineStart   = lineNum;
            sym.brief       = prevComment;
            file.symbols.append(sym);
            prevComment.clear();
            continue;
        }

        if (!trimmed.isEmpty()) prevComment.clear();
    }
}

void ProjectIndexer::parsePythonSymbols(IndexedFile& file, const QStringList& lines) const
{
    static const QRegularExpression reImport(
        QStringLiteral(R"(^\s*(?:from\s+([\w\.]+)\s+import|import\s+([\w\.]+)))"));
    static const QRegularExpression reClass(
        QStringLiteral(R"(^\s*class\s+(\w+))"));
    static const QRegularExpression reDef(
        QStringLiteral(R"(^(\s*)(?:async\s+)?def\s+(\w+)\s*\(([^)]*)\))"));

    QString currentClass;
    QString prevComment;

    for (int i = 0; i < lines.size(); ++i) {
        if (file.symbols.size() >= MAX_SYMBOLS_PER_FILE) break;

        const QString& line = lines[i];
        const int lineNum = i + 1;
        const QString trimmed = line.trimmed();

        if (trimmed.startsWith(QChar('#'))) {
            const QString c = trimmed.mid(1).trimmed();
            if (!c.isEmpty() && c.length() < 120) prevComment = c;
            continue;
        }

        auto mImp = reImport.match(line);
        if (mImp.hasMatch()) {
            const QString mod = mImp.captured(1).isEmpty()
                                    ? mImp.captured(2) : mImp.captured(1);
            if (!mod.isEmpty()) file.includes.append(mod);
            continue;
        }

        auto mClass = reClass.match(line);
        if (mClass.hasMatch()) {
            CodeSymbol sym;
            sym.kind      = CodeSymbol::Class;
            sym.name      = mClass.captured(1);
            sym.filePath  = file.filePath;
            sym.lineStart = lineNum;
            sym.brief     = prevComment;
            file.symbols.append(sym);
            currentClass  = sym.name;
            prevComment.clear();
            continue;
        }

        auto mDef = reDef.match(line);
        if (mDef.hasMatch()) {
            CodeSymbol sym;
            // Отступ внутри класса — метод, на верхнем уровне — функция.
            const bool nested = !mDef.captured(1).isEmpty() && !currentClass.isEmpty();
            sym.kind        = nested ? CodeSymbol::Method : CodeSymbol::Function;
            sym.name        = mDef.captured(2);
            sym.parentClass = nested ? currentClass : QString();
            sym.signature   = trimmed;
            sym.filePath    = file.filePath;
            sym.lineStart   = lineNum;
            sym.brief       = prevComment;
            file.symbols.append(sym);
            if (!nested) currentClass.clear();
            prevComment.clear();
            continue;
        }

        if (!trimmed.isEmpty()) prevComment.clear();
    }
}

void ProjectIndexer::parseJsSymbols(IndexedFile& file, const QStringList& lines) const
{
    static const QRegularExpression reImport(
        QStringLiteral(R"(^\s*(?:import\s.*?from\s*['"](.+?)['"]|(?:const|let|var)\s+\w+\s*=\s*require\(\s*['"](.+?)['"]))"));
    static const QRegularExpression reClass(
        QStringLiteral(R"(^\s*(?:export\s+)?(?:default\s+)?class\s+(\w+))"));
    static const QRegularExpression reFunc(
        QStringLiteral(R"(^\s*(?:export\s+)?(?:default\s+)?(?:async\s+)?function\s*\*?\s*(\w+)\s*\()"));
    static const QRegularExpression reArrow(
        QStringLiteral(R"(^\s*(?:export\s+)?(?:const|let|var)\s+(\w+)\s*=\s*(?:async\s*)?\(?[^;]*?=>)"));

    QString prevComment;

    for (int i = 0; i < lines.size(); ++i) {
        if (file.symbols.size() >= MAX_SYMBOLS_PER_FILE) break;

        const QString& line = lines[i];
        const int lineNum = i + 1;
        const QString trimmed = line.trimmed();

        if (trimmed.startsWith(QStringLiteral("//"))) {
            const QString c = trimmed.mid(2).trimmed();
            if (!c.isEmpty() && c.length() < 120) prevComment = c;
            continue;
        }

        auto mImp = reImport.match(line);
        if (mImp.hasMatch()) {
            const QString mod = mImp.captured(1).isEmpty()
                                    ? mImp.captured(2) : mImp.captured(1);
            if (!mod.isEmpty()) file.includes.append(mod);
            continue;
        }

        auto mClass = reClass.match(line);
        if (mClass.hasMatch()) {
            CodeSymbol sym;
            sym.kind      = CodeSymbol::Class;
            sym.name      = mClass.captured(1);
            sym.filePath  = file.filePath;
            sym.lineStart = lineNum;
            sym.brief     = prevComment;
            file.symbols.append(sym);
            prevComment.clear();
            continue;
        }

        auto mFunc = reFunc.match(line);
        if (!mFunc.hasMatch()) mFunc = reArrow.match(line);
        if (mFunc.hasMatch()) {
            CodeSymbol sym;
            sym.kind      = CodeSymbol::Function;
            sym.name      = mFunc.captured(1);
            sym.signature = trimmed.left(160);
            sym.filePath  = file.filePath;
            sym.lineStart = lineNum;
            sym.brief     = prevComment;
            file.symbols.append(sym);
            prevComment.clear();
            continue;
        }

        if (!trimmed.isEmpty()) prevComment.clear();
    }
}

void ProjectIndexer::parseCMakeSymbols(IndexedFile& file, const QStringList& lines) const
{
    static const QRegularExpression reTarget(
        QStringLiteral(R"(^\s*(add_executable|add_library|qt_add_executable|qt_add_library|qt_add_qml_module)\s*\(\s*([\w\$\{\}]+))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reSubdir(
        QStringLiteral(R"(^\s*add_subdirectory\s*\(\s*([^\s\)]+))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rePackage(
        QStringLiteral(R"(^\s*find_package\s*\(\s*([\w\-]+))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reOption(
        QStringLiteral(R"(^\s*(?:option|set)\s*\(\s*([A-Z][A-Z0-9_]{3,}))"));
    static const QRegularExpression reQmakeTarget(
        QStringLiteral(R"(^\s*TARGET\s*=\s*(\S+))"));

    QString prevComment;

    for (int i = 0; i < lines.size(); ++i) {
        if (file.symbols.size() >= MAX_SYMBOLS_PER_FILE) break;

        const QString& line = lines[i];
        const int lineNum = i + 1;
        const QString trimmed = line.trimmed();

        if (trimmed.startsWith(QChar('#'))) {
            const QString c = trimmed.mid(1).trimmed();
            if (!c.isEmpty() && c.length() < 120) prevComment = c;
            continue;
        }

        auto mTarget = reTarget.match(line);
        if (mTarget.hasMatch()) {
            CodeSymbol sym;
            sym.kind      = CodeSymbol::BuildTarget;
            sym.name      = mTarget.captured(2);
            sym.signature = mTarget.captured(1).toLower();
            sym.filePath  = file.filePath;
            sym.lineStart = lineNum;
            sym.brief     = prevComment;
            file.symbols.append(sym);
            prevComment.clear();
            continue;
        }

        auto mQmake = reQmakeTarget.match(line);
        if (mQmake.hasMatch()) {
            CodeSymbol sym;
            sym.kind      = CodeSymbol::BuildTarget;
            sym.name      = mQmake.captured(1);
            sym.signature = QStringLiteral("qmake target");
            sym.filePath  = file.filePath;
            sym.lineStart = lineNum;
            sym.brief     = prevComment;
            file.symbols.append(sym);
            prevComment.clear();
            continue;
        }

        auto mSub = reSubdir.match(line);
        if (mSub.hasMatch()) {
            file.includes.append(mSub.captured(1));
            continue;
        }

        auto mPkg = rePackage.match(line);
        if (mPkg.hasMatch()) {
            file.includes.append(mPkg.captured(1));
            continue;
        }

        auto mOpt = reOption.match(line);
        if (mOpt.hasMatch()) {
            CodeSymbol sym;
            sym.kind      = CodeSymbol::Variable;
            sym.name      = mOpt.captured(1);
            sym.filePath  = file.filePath;
            sym.lineStart = lineNum;
            sym.brief     = prevComment;
            file.symbols.append(sym);
            prevComment.clear();
            continue;
        }

        if (!trimmed.isEmpty()) prevComment.clear();
    }
}

void ProjectIndexer::parseQrcSymbols(IndexedFile& file, const QStringList& lines) const
{
    static const QRegularExpression rePrefix(
        QStringLiteral(R"RX(<qresource[^>]*prefix\s*=\s*"([^"]*)")RX"));
    static const QRegularExpression reFileEntry(
        QStringLiteral(R"RX(<file(?:\s+alias\s*=\s*"([^"]*)")?\s*>([^<]+)</file>)RX"));

    QString prefix;

    for (int i = 0; i < lines.size(); ++i) {
        if (file.symbols.size() >= MAX_SYMBOLS_PER_FILE) break;

        auto mPrefix = rePrefix.match(lines[i]);
        if (mPrefix.hasMatch()) prefix = mPrefix.captured(1);

        auto it = reFileEntry.globalMatch(lines[i]);
        while (it.hasNext()) {
            const auto m = it.next();
            CodeSymbol sym;
            sym.kind      = CodeSymbol::Resource;
            sym.name      = m.captured(2).trimmed();
            sym.signature = prefix;
            sym.brief     = m.captured(1);   // alias, если задан
            sym.filePath  = file.filePath;
            sym.lineStart = i + 1;
            file.symbols.append(sym);
            // Ресурс — это зависимость .qrc от файла на диске: так вопрос
            // «кто использует этот ассет» отвечается тем же графом include.
            file.includes.append(sym.name);
        }
    }
}

void ProjectIndexer::parseUiSymbols(IndexedFile& file, const QStringList& lines) const
{
    static const QRegularExpression reClass(
        QStringLiteral(R"(<class>([^<]+)</class>)"));
    static const QRegularExpression reWidget(
        QStringLiteral(R"RX(<widget\s+class\s*=\s*"([^"]+)"\s+name\s*=\s*"([^"]+)")RX"));

    for (int i = 0; i < lines.size(); ++i) {
        if (file.symbols.size() >= MAX_SYMBOLS_PER_FILE) break;

        auto mClass = reClass.match(lines[i]);
        if (mClass.hasMatch()) {
            CodeSymbol sym;
            sym.kind      = CodeSymbol::Class;
            sym.name      = mClass.captured(1);
            sym.signature = QStringLiteral("Qt Designer form");
            sym.filePath  = file.filePath;
            sym.lineStart = i + 1;
            file.symbols.append(sym);
            continue;
        }

        auto mWidget = reWidget.match(lines[i]);
        if (mWidget.hasMatch()) {
            CodeSymbol sym;
            sym.kind      = CodeSymbol::Variable;
            sym.name      = mWidget.captured(2);
            sym.signature = mWidget.captured(1);
            sym.filePath  = file.filePath;
            sym.lineStart = i + 1;
            file.symbols.append(sym);
        }
    }
}

void ProjectIndexer::parseMarkdownSymbols(IndexedFile& file, const QStringList& lines) const
{
    static const QRegularExpression reHeading(
        QStringLiteral(R"(^(#{1,3})\s+(.+?)\s*$)"));

    for (int i = 0; i < lines.size(); ++i) {
        if (file.symbols.size() >= MAX_SYMBOLS_PER_FILE) break;

        auto m = reHeading.match(lines[i]);
        if (!m.hasMatch()) continue;

        CodeSymbol sym;
        sym.kind      = CodeSymbol::Variable;
        sym.name      = m.captured(2).left(120);
        sym.signature = QStringLiteral("h") + QString::number(m.captured(1).size());
        sym.filePath  = file.filePath;
        sym.lineStart = i + 1;
        file.symbols.append(sym);
    }
}

void ProjectIndexer::collectTodos(IndexedFile& file, const QStringList& lines)
{
    static const QRegularExpression reTodo(
        QStringLiteral(R"(\b(TODO|FIXME|HACK|XXX)\b[:\s]*(.*))"));

    constexpr int MAX_TODOS_PER_FILE = 8;
    int found = 0;

    for (int i = 0; i < lines.size() && found < MAX_TODOS_PER_FILE; ++i) {
        const QString& line = lines[i];
        if (!line.contains(QStringLiteral("//")) && !line.contains(QChar('#'))
            && !line.contains(QStringLiteral("/*")) && !line.contains(QStringLiteral("<!--"))) {
            continue;
        }

        const auto m = reTodo.match(line);
        if (!m.hasMatch()) continue;

        CodeSymbol sym;
        sym.kind      = CodeSymbol::Todo;
        sym.name      = m.captured(1);
        sym.brief     = m.captured(2).trimmed().left(160);
        sym.filePath  = file.filePath;
        sym.lineStart = i + 1;
        file.symbols.append(sym);
        found++;
    }
}

// ============================================================
// Запросы «про проект целиком»
// ============================================================

QStringList ProjectIndexer::whoIncludes(const QString& headerName) const
{
    const QString key = headerName.trimmed()
                            .section(QChar('/'), -1)
                            .section(QChar('\\'), -1)
                            .toLower();
    if (key.isEmpty()) return {};

    QStringList result = m_includedBy.values(key);
    result.removeDuplicates();
    result.sort();
    return result;
}

QStringList ProjectIndexer::filesByLanguage(const QString& lang) const
{
    QStringList result;
    for (const auto& file : m_files) {
        if (file.language.compare(lang, Qt::CaseInsensitive) == 0) {
            result.append(file.relativePath.isEmpty()
                              ? file.filePath : file.relativePath);
        }
    }
    result.sort();
    return result;
}

QMap<QString, QPair<int, int>> ProjectIndexer::languageStats() const
{
    QMap<QString, QPair<int, int>> stats;
    for (const auto& file : m_files) {
        const QString lang = file.language.isEmpty()
                                 ? QStringLiteral("other") : file.language;
        auto& entry = stats[lang];
        entry.first  += 1;
        entry.second += file.lineCount;
    }
    return stats;
}

QVector<CodeSymbol> ProjectIndexer::todos(int maxItems) const
{
    QVector<CodeSymbol> result;
    for (const auto& file : m_files) {
        for (const auto& sym : file.symbols) {
            if (sym.kind != CodeSymbol::Todo) continue;
            result.append(sym);
            if (result.size() >= maxItems) return result;
        }
    }
    return result;
}
