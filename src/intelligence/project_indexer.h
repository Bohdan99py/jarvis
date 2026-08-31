#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QVector>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileSystemWatcher>
#include <QSet>
#include <QMultiHash>
#include <QPair>

#include "jarvis_core_export.h"

// Один символ (класс, функция, переменная, макрос и т.д.)
struct CodeSymbol
{
    enum Kind {
        Class,
        Struct,
        Enum,
        Function,
        Method,       // метод класса
        Variable,
        Macro,
        Include,
        UClass,       // Unreal Engine UCLASS
        UFunction,    // Unreal Engine UFUNCTION
        UProperty,    // Unreal Engine UPROPERTY

        // --- Не-C++ языки (индексатор понимает весь проект, а не только C++) ---
        QmlComponent, // QML: тип компонента / id инстанса
        QmlProperty,  // QML: property
        QmlSignal,    // QML: signal
        BuildTarget,  // CMake: add_executable/add_library
        Resource,     // .qrc: файл ресурса
        Todo,         // TODO/FIXME/HACK-пометка в коде

        Unknown
    };

    Kind    kind      = Unknown;
    QString name;
    QString parentClass;
    QString signature;
    QString filePath;
    int     lineStart = 0;
    int     lineEnd   = 0;
    QString brief;

    QJsonObject toJson() const;
    static CodeSymbol fromJson(const QJsonObject& obj);

    QString kindToString() const;
};

// Информация о файле
struct IndexedFile
{
    QString filePath;
    QString relativePath;
    // Язык файла: "cpp", "qml", "python", "js", "cmake", "qrc", "ui",
    // "md", "config", "shader", "cs", "script", "other". Определяется по
    // расширению/имени в ProjectIndexer::languageForFile().
    QString language;
    qint64  fileSize = 0;
    int     lineCount = 0;
    QDateTime lastModified;
    QDateTime lastIndexed;
    QStringList includes;
    // Только включения в КАВЫЧКАХ — то есть файлы самого проекта.
    // Без этого разделения #include <windows.h> неотличим от
    // #include "windows.h", и любой системный заголовок выглядит как
    // ссылка на несуществующий файл проекта.
    QStringList localIncludes;
    QVector<CodeSymbol> symbols;

    QJsonObject toJson() const;
    static IndexedFile fromJson(const QJsonObject& obj);
};

class JARVIS_CORE_EXPORT ProjectIndexer : public QObject
{
    Q_OBJECT

public:
    explicit ProjectIndexer(QObject* parent = nullptr);
    ~ProjectIndexer() override;

    // === Управление проектом ===
    void setProjectRoot(const QString& path);
    QString projectRoot() const { return m_projectRoot; }

    void indexProject();
    void indexFile(const QString& filePath);

    // === Поиск ===
    QVector<CodeSymbol> findSymbol(const QString& name, bool exact = false) const;
    QVector<IndexedFile> findFile(const QString& name) const;

    // Прямой доступ к индексу для потребителей, которым нужны ВСЕ файлы
    // (DevAdvisor). Через findFile() такой обход получается квадратичным:
    // подстрочное сравнение с каждым путём на каждой итерации.
    const QMap<QString, IndexedFile>& indexedFiles() const { return m_files; }

    struct GrepResult {
        QString filePath;
        int     line;
        QString lineText;
    };
    QVector<GrepResult> grep(const QString& pattern, int maxResults = 20) const;

    // === Понимание проекта целиком ===
    // Обратный граф include: кто подключает этот заголовок. Принимает и
    // имя файла ("jarvis.h"), и относительный путь ("src/engine/jarvis.h").
    // Отвечает на вопрос «что сломается, если тронуть этот файл».
    QStringList whoIncludes(const QString& headerName) const;

    // Относительные пути всех файлов одного языка ("qml", "cmake", ...)
    QStringList filesByLanguage(const QString& lang) const;

    // Язык -> (файлов, строк). Основа для «профиля проекта».
    QMap<QString, QPair<int, int>> languageStats() const;

    // TODO/FIXME/HACK, собранные при индексации (для фоновых рекомендаций)
    QVector<CodeSymbol> todos(int maxItems = 40) const;

    // Язык файла по расширению/имени; пустая строка = не индексируем
    static QString languageForFile(const QString& filePath);

    // Каталоги, которые не являются кодом проекта: вывод сборки, вендоренные
    // зависимости, кэши. Публичный static — тем же фильтром пользуется
    // ProjectProfile, иначе профиль насчитывал бы ассеты OpenCV как свои.
    // projectRoot нужен, чтобы проверять только путь ВНУТРИ проекта:
    // иначе проект, лежащий в каталоге вроде ".config/app", целиком
    // считался бы скрытым и не индексировался вовсе.
    static bool isIgnoredPath(const QString& path,
                              const QString& projectRoot = QString());

    QString getCodeSnippet(const CodeSymbol& symbol, int contextLines = 10) const;
    QString getFileLines(const QString& filePath, int startLine, int endLine) const;

    // === Статистика ===
    int fileCount() const { return m_files.size(); }
    int symbolCount() const;
    QStringList allClasses() const;
    QStringList allFiles() const;

    // Для Brain::captureSnapshot — файлы отсортированные по дате изменения
    QStringList recentFiles(int n = 10) const;

    QString projectMap() const;
    QString detailedMap() const;

    // === Сохранение/загрузка индекса ===
    void saveIndex() const;
    void loadIndex();

    // === Автообновление ===
    void enableFileWatcher(bool enable = true);

    bool isIndexing() const { return m_indexing; }

signals:
    void indexingStarted(int totalFiles);
    void indexingProgress(int current, int total);
    void indexingFinished(int fileCount, int symbolCount);
    void fileReindexed(const QString& filePath);

private slots:
    void onFileChanged(const QString& path);
    void onDirectoryChanged(const QString& path);

private:
    IndexedFile parseFile(const QString& filePath) const;

    // Диспетчер по языку: C++ парсер остался прежним, остальные языки
    // разбираются лёгкими построчными парсерами — цель не компилятор,
    // а карта «что где лежит».
    void parseSymbols(IndexedFile& file, const QStringList& lines) const;
    void parseCppSymbols(IndexedFile& file, const QStringList& lines) const;
    void parseQmlSymbols(IndexedFile& file, const QStringList& lines) const;
    void parsePythonSymbols(IndexedFile& file, const QStringList& lines) const;
    void parseJsSymbols(IndexedFile& file, const QStringList& lines) const;
    void parseCMakeSymbols(IndexedFile& file, const QStringList& lines) const;
    void parseQrcSymbols(IndexedFile& file, const QStringList& lines) const;
    void parseUiSymbols(IndexedFile& file, const QStringList& lines) const;
    void parseMarkdownSymbols(IndexedFile& file, const QStringList& lines) const;
    static void collectTodos(IndexedFile& file, const QStringList& lines);
    void rebuildCaches();
    bool shouldSkipPath(const QString& path) const;

    QStringList collectSourceFiles(const QString& dir) const;
    QString relativePath(const QString& absPath) const;
    QString indexFilePath() const;

    QString m_projectRoot;
    QMap<QString, IndexedFile> m_files;

    // Обратный граф include: "jarvis.h" -> [файлы, которые его включают].
    // Ключ — только имя файла: в #include пишут и "jarvis.h", и
    // "engine/jarvis.h", а нам важна связь, а не форма записи.
    QMultiHash<QString, QString> m_includedBy;
    QFileSystemWatcher* m_watcher = nullptr;
    bool m_indexing = false;
    bool m_watcherEnabled = false;
    int m_symbolCount = 0;

    static const QStringList s_sourceExtensions;

    // Потолки подняты вместе с переходом на мультиязычную индексацию:
    // Qt-проект средних размеров — это 1000+ файлов вместе с QML, CMake
    // и ресурсами, а на 500 «понимание от А до Я» упиралось в обрезку.
    static constexpr int MAX_INDEXED_FILES = 2000;
    static constexpr int MAX_SYMBOLS_PER_FILE = 250;

    // Файлы крупнее не парсим: обычно это сгенерированный код или
    // выгрузки данных — символов из них ноль, а память и время едят.
    static constexpr qint64 MAX_FILE_BYTES = 1500000;

    // Версия формата индекса. Не совпала — индекс на диске игнорируется
    // и проект переиндексируется (иначе старые int-коды Kind поедут).
    static constexpr int INDEX_FORMAT_VERSION = 3;
};
