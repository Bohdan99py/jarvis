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
    qint64  fileSize = 0;
    int     lineCount = 0;
    QDateTime lastModified;
    QDateTime lastIndexed;
    QStringList includes;
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

    struct GrepResult {
        QString filePath;
        int     line;
        QString lineText;
    };
    QVector<GrepResult> grep(const QString& pattern, int maxResults = 20) const;

    QString getCodeSnippet(const CodeSymbol& symbol, int contextLines = 10) const;
    QString getFileLines(const QString& filePath, int startLine, int endLine) const;

    // === Статистика ===
    int fileCount() const { return m_files.size(); }
    int symbolCount() const;
    QStringList allClasses() const;
    QStringList allFiles() const;

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
    void parseSymbols(IndexedFile& file, const QStringList& lines) const;
    void rebuildCaches();
    bool shouldSkipPath(const QString& path) const;

    QStringList collectSourceFiles(const QString& dir) const;
    QString relativePath(const QString& absPath) const;
    QString indexFilePath() const;

    QString m_projectRoot;
    QMap<QString, IndexedFile> m_files;
    QFileSystemWatcher* m_watcher = nullptr;
    bool m_indexing = false;
    bool m_watcherEnabled = false;
    int m_symbolCount = 0;

    static const QStringList s_sourceExtensions;
};
