#pragma once
// -------------------------------------------------------
// project_indexer.h — Локальный индексатор C++ проектов
//
// Сканирует папку проекта, парсит .h/.cpp файлы,
// создаёт индекс классов/функций/переменных.
// Поиск по индексу — мгновенный и бесплатный.
// В API отправляется только нужный фрагмент кода.
//
// Поддерживает:
//   - C++ (.h, .hpp, .cpp, .cc, .cxx)
//   - Unreal Engine (.h, .cpp с UCLASS/UFUNCTION)
//   - Blueprint-like структуры
//   - Авто-переиндексация при изменении файлов
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QVector>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileSystemWatcher>

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
        Unknown
    };

    Kind    kind      = Unknown;
    QString name;             // Имя символа
    QString parentClass;      // Для методов — имя класса
    QString signature;        // Полная сигнатура (для функций)
    QString filePath;         // Абсолютный путь к файлу
    int     lineStart = 0;    // Начальная строка
    int     lineEnd   = 0;    // Конечная строка (приблизительно)
    QString brief;            // Краткое описание (из комментариев)

    // Для JSON
    QJsonObject toJson() const;
    static CodeSymbol fromJson(const QJsonObject& obj);

    // Человекочитаемый вид
    QString kindToString() const;
};

// Информация о файле
struct IndexedFile
{
    QString filePath;
    QString relativePath;     // Относительный путь от корня проекта
    qint64  fileSize = 0;
    int     lineCount = 0;
    QDateTime lastModified;
    QDateTime lastIndexed;
    QStringList includes;     // #include в файле
    QVector<CodeSymbol> symbols;

    QJsonObject toJson() const;
    static IndexedFile fromJson(const QJsonObject& obj);
};

class ProjectIndexer : public QObject
{
    Q_OBJECT

public:
    explicit ProjectIndexer(QObject* parent = nullptr);
    ~ProjectIndexer() override;

    // === Управление проектом ===

    // Установить корневую папку проекта
    void setProjectRoot(const QString& path);
    QString projectRoot() const { return m_projectRoot; }

    // Полная индексация (вызвать один раз или для refresh)
    void indexProject();

    // Индексировать один файл
    void indexFile(const QString& filePath);

    // === Поиск ===

    // Найти символ по имени (точный или partial match)
    QVector<CodeSymbol> findSymbol(const QString& name, bool exact = false) const;

    // Найти файл по имени
    QVector<IndexedFile> findFile(const QString& name) const;

    // Поиск по содержимому (grep)
    struct GrepResult {
        QString filePath;
        int     line;
        QString lineText;
    };
    QVector<GrepResult> grep(const QString& pattern, int maxResults = 20) const;

    // Получить фрагмент кода вокруг символа (для отправки в API)
    QString getCodeSnippet(const CodeSymbol& symbol, int contextLines = 10) const;

    // Получить фрагмент файла по строкам
    QString getFileLines(const QString& filePath, int startLine, int endLine) const;

    // === Статистика ===

    int fileCount() const { return m_files.size(); }
    int symbolCount() const;
    QStringList allClasses() const;
    QStringList allFiles() const;

    // Краткая карта проекта (для system prompt)
    QString projectMap() const;

    // Детальная карта (для отладки)
    QString detailedMap() const;

    // === Сохранение/загрузка индекса ===

    void saveIndex() const;
    void loadIndex();

    // === Автообновление ===

    // Включить слежение за изменениями файлов
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
    // Парсинг файла
    IndexedFile parseFile(const QString& filePath) const;
    void parseSymbols(IndexedFile& file, const QStringList& lines) const;

    // Утилиты
    QStringList collectSourceFiles(const QString& dir) const;
    QString relativePath(const QString& absPath) const;
    QString indexFilePath() const;

    QString m_projectRoot;
    QMap<QString, IndexedFile> m_files;  // filePath → IndexedFile
    QFileSystemWatcher* m_watcher = nullptr;
    bool m_indexing = false;
    bool m_watcherEnabled = false;

    // Расширения файлов для индексации
    static const QStringList s_sourceExtensions;
};
