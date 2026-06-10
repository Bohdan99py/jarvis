#pragma once
// -------------------------------------------------------
// search_router.h — Роутер поиска по доменам
// -------------------------------------------------------

#include "brain.h"
#include "semantic_mapper.h"
#include <QString>
#include <QStringList>
#include <QVector>
#include <QObject>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>

class ProjectIndexer;
class SessionMemory;

// ============================================================
// SearchResult — один результат поиска
// ============================================================

struct SearchResult
{
    QString title;
    QString path;
    QString snippet;
    QString source;   // "project", "filesystem", "browser", "chat", "ue5", "memory"
    int     relevance = 0;

    QString format() const;
};

using SearchResults = QVector<SearchResult>;

// ============================================================
// SearchRouter
// ============================================================

class SearchRouter : public QObject
{
    Q_OBJECT

public:
    explicit SearchRouter(ProjectIndexer* indexer,
                          SessionMemory*  memory,
                          QObject*        parent = nullptr);

    // Синхронный поиск — для ProjectFiles, Chat, Clipboard, Memory, UE5, Web
    // Возвращает готовую строку для appendLog.
    QString search(const Intent& intent, const ContextSnapshot& ctx);

    // Асинхронный поиск по файловой системе — не блокирует UI.
    // Сразу возвращает пустую строку, результат придёт через searchFinished.
    void searchAsync(const Intent& intent, const ContextSnapshot& ctx);

    // Пути к реальным файлам из последнего поиска.
    // Заполняются внутри formatResults() после каждого search().
    // Используются в MainWindow для открытия FileViewer.
    QStringList lastFoundFilePaths() const { return m_lastFoundPaths; }

signals:
    void searchFinished(const QString& formattedResult);

private:
    SearchResults searchProjectFiles(const QString& query) const;
    SearchResults searchFilesystem(const QString& query,
                                   const QString& fileTypeHint = QString()) const;
    SearchResults searchByContent(const QStringList& keywords,
                                   const QStringList& extensions,
                                   int maxResults = 5) const;
    SearchResults searchBrowserHistory(const QString& query) const;
    SearchResults parseBrowserSqlite(const QString& dbPath,
                                      const QString& query) const;
    SearchResults searchChatHistory(const QString& query) const;
    SearchResults searchUE5Logs(const QString& query) const;
    SearchResults searchClipboard(const QString& query,
                                   const ContextSnapshot& ctx) const;
    SearchResults searchMemory(const QString& query) const;
    SearchResults windowsSearch(const QString& query) const;

    QString openWebSearch(const QString& query) const;

    // formatResults теперь НЕ static — нужен доступ к m_lastFoundPaths
    QString formatResults(const SearchResults& results,
                          const QString& domain,
                          const QString& query);

    ProjectIndexer* m_indexer = nullptr;
    SessionMemory*  m_memory  = nullptr;

    // Watcher для асинхронного поиска по ФС
    QFutureWatcher<SearchResults>* m_fsWatcher = nullptr;
    QString                         m_fsQuery;
    QString                         m_fsDomainLabel;

    // Пути к найденным файлам на диске (для FileViewer в MainWindow)
    QStringList                     m_lastFoundPaths;

    static constexpr int kMaxResults = 8;
};