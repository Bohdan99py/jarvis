#pragma once
// -------------------------------------------------------
// project_profile.h — «Профиль проекта»: что это вообще за проект
//
// ProjectIndexer отвечает на вопрос «где лежит символ X». Этого мало,
// чтобы понимать проект от А до Я: ассистент всё равно не знал, чем
// собирается проект, какие у него таргеты и зависимости, где точка
// входа, какие есть ассеты и подключены ли они к сборке.
//
// ProjectProfile строит эту вторую половину картины — один раз после
// индексации — и умеет отдать её компактной выжимкой в system prompt
// (brief()) либо подробным отчётом по запросу модели (assetsReport()).
//
// Профиль намеренно собирается ЛОКАЛЬНО, без обращения к LLM: это
// дёшево, работает офлайн и не тратит токены на то, что можно просто
// прочитать с диска.
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QDateTime>

#include "jarvis_core_export.h"

class ProjectIndexer;

// Ассет — всё, что проект использует, но что не является кодом:
// картинки, иконки, звуки, шрифты, модели, переводы.
struct ProjectAsset
{
    QString relativePath;
    QString kind;        // "image", "vector", "audio", "font", "model", "translation", "data"
    qint64  size = 0;
    // false = имя файла не встречается ни в .qrc, ни в коде, ни в конфигах.
    // Кандидат на удаление либо забытый ассет — это ловит DevAdvisor.
    bool    referenced = false;
};

struct ProjectBuildTarget
{
    QString name;
    QString type;        // "add_executable", "add_library", "qmake target", ...
    QString definedIn;   // относительный путь к CMakeLists.txt
};

class JARVIS_CORE_EXPORT ProjectProfile : public QObject
{
    Q_OBJECT

public:
    explicit ProjectProfile(QObject* parent = nullptr);

    // Индексатор нужен для языковой статистики и списка таргетов —
    // профиль не парсит код повторно, а переиспользует индекс.
    void setIndexer(const ProjectIndexer* indexer) { m_indexer = indexer; }

    // Полное пересканирование. Дорогая операция (обход дерева + чтение
    // текстовых файлов для поиска ссылок на ассеты) — вызывается после
    // индексации проекта, а не на каждый запрос.
    void scan(const QString& projectRoot);

    bool    isValid()      const { return !m_root.isEmpty() && m_scannedAt.isValid(); }
    QString root()         const { return m_root; }
    QString name()         const { return m_name; }
    QString kind()         const { return m_kind; }         // "Qt/CMake C++ desktop app"
    QString buildSystem()  const { return m_buildSystem; }
    QDateTime scannedAt()  const { return m_scannedAt; }

    QStringList entryPoints()  const { return m_entryPoints; }
    QStringList dependencies() const { return m_dependencies; }
    QStringList docs()         const { return m_docs; }
    QString     readmeSummary() const { return m_readmeSummary; }
    QString     gitBranch()    const { return m_gitBranch; }
    QStringList recentCommits() const { return m_recentCommits; }

    const QVector<ProjectBuildTarget>& targets() const { return m_targets; }
    const QVector<ProjectAsset>&       assets()  const { return m_assets; }

    qint64 assetBytes() const { return m_assetBytes; }
    QVector<ProjectAsset> unusedAssets(int maxItems = 20) const;
    QVector<ProjectAsset> findAssets(const QString& query, int maxItems = 20) const;

    // Компактная выжимка для system prompt: тип проекта, сборка, таргеты,
    // зависимости, точки входа, языки, ассеты, git. Ужимается до maxChars.
    QString brief(int maxChars = 3500) const;

    // Подробный отчёт по ассетам — отдаётся модели по запросу
    // [NEED:assets], чтобы не держать его в промпте постоянно.
    QString assetsReport(int maxItems = 40) const;

    // Кэш на диске: профиль переживает перезапуск приложения, как и индекс.
    void save() const;
    bool load(const QString& projectRoot);

signals:
    void profileChanged();

private:
    void reset();
    void scanTree();                 // ассеты + признаки типа проекта
    void detectKind();
    void collectFromIndex();         // таргеты, языки, точки входа
    void collectBuildDependencies();
    void markReferencedAssets();
    void readDocs();
    void readGitInfo();

    QString cacheFilePath() const;
    static QString assetKindFor(const QString& suffix);
    static QString humanSize(qint64 bytes);

    const ProjectIndexer* m_indexer = nullptr;

    QString   m_root;
    QString   m_name;
    QString   m_kind;
    QString   m_buildSystem;
    QDateTime m_scannedAt;

    QStringList m_entryPoints;
    QStringList m_dependencies;
    QStringList m_docs;
    QString     m_readmeSummary;
    QString     m_gitBranch;
    QStringList m_recentCommits;

    QVector<ProjectBuildTarget> m_targets;
    QVector<ProjectAsset>       m_assets;
    qint64                      m_assetBytes = 0;

    // Признаки, найденные при обходе дерева (используются detectKind()).
    bool m_hasCMake      = false;
    bool m_hasQmake      = false;
    bool m_hasUproject   = false;
    bool m_hasPackageJson = false;
    bool m_hasPython     = false;
    bool m_hasSolution   = false;
    bool m_hasQml        = false;

    // Потолки: профиль должен строиться за секунды даже на большом дереве.
    static constexpr int    MAX_ASSETS          = 4000;
    static constexpr int    MAX_SCAN_ENTRIES    = 60000;
    static constexpr int    MAX_REF_SCAN_FILES  = 1500;
    static constexpr qint64 MAX_REF_SCAN_BYTES  = 400000;
};
