// -------------------------------------------------------
// search_router.cpp — Реализация роутера поиска
// -------------------------------------------------------

#include "search_router.h"
#include "project_indexer.h"
#include "session_memory.h"
#include "lang.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>
#include <QRegularExpression>
#include <QProcess>
#include <QTextStream>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QJsonObject>
#include <QSet>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#endif

// ============================================================
// Вспомогательные (до первого использования)
// ============================================================

static QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024)         return QString::number(bytes) + QStringLiteral(" B");
    if (bytes < 1024 * 1024)  return QString::number(bytes / 1024) + QStringLiteral(" KB");
    return QString::number(bytes / (1024 * 1024)) + QStringLiteral(" MB");
}

// ============================================================
// SearchResult::format
// ============================================================

QString SearchResult::format() const
{
    QString out;
    out += QStringLiteral("  📄 ") + title;
    if (!path.isEmpty() && path != title) {
        out += QStringLiteral("\n     ") + path;
    }
    if (!snippet.isEmpty()) {
        QString s = snippet.left(180);
        s.replace(QStringLiteral("\n"), QStringLiteral(" "));
        out += QStringLiteral("\n     ") + s;
    }
    return out;
}

// ============================================================
// Конструктор
// ============================================================

SearchRouter::SearchRouter(ProjectIndexer* indexer,
                           SessionMemory*  memory,
                           QObject*        parent)
    : QObject(parent)
    , m_indexer(indexer)
    , m_memory(memory)
{}

// ============================================================
// search() — главный диспетчер
// ============================================================

QString SearchRouter::search(const Intent& intent, const ContextSnapshot& ctx)
{
    const QString query = intent.query.isEmpty() ? intent.targetFile : intent.query;

    if (query.isEmpty()) {
        return IS_EN
            ? QStringLiteral("Please specify what to search for.")
            : QStringLiteral("Укажи что искать.");
    }

    SearchResults results;
    QString domainLabel;

    switch (intent.domain) {

    case Intent::Domain::ProjectFiles:
        results    = searchProjectFiles(query);
        domainLabel = IS_EN ? QStringLiteral("project") : QStringLiteral("проекте");
        break;

    case Intent::Domain::Filesystem:
        // Файловая система — асинхронно, не блокируем UI
        searchAsync(intent, ctx);
        return IS_EN
            ? QStringLiteral("Searching on your PC for «") + query + QStringLiteral("»...")
            : QStringLiteral("Ищу на компьютере: «") + query + QStringLiteral("»...");

    case Intent::Domain::BrowserHistory:
        results    = searchBrowserHistory(query);
        domainLabel = IS_EN ? QStringLiteral("browser history") : QStringLiteral("истории браузера");
        break;

    case Intent::Domain::ChatHistory:
        results    = searchChatHistory(query);
        domainLabel = IS_EN ? QStringLiteral("chat history") : QStringLiteral("нашем разговоре");
        break;

    case Intent::Domain::UE5Logs:
        results    = searchUE5Logs(query);
        domainLabel = QStringLiteral("UE5 logs");
        break;

    case Intent::Domain::Clipboard:
        results    = searchClipboard(query, ctx);
        domainLabel = IS_EN ? QStringLiteral("clipboard") : QStringLiteral("буфере обмена");
        break;

    case Intent::Domain::Memory:
        results    = searchMemory(query);
        domainLabel = IS_EN ? QStringLiteral("memory") : QStringLiteral("памяти");
        break;

    case Intent::Domain::Web:
        return openWebSearch(query);

    default:
        // Fallback: сначала проект (если индексирован), потом весь ПК
        if (m_indexer && m_indexer->fileCount() > 0) {
            results    = searchProjectFiles(query);
            domainLabel = IS_EN ? QStringLiteral("project") : QStringLiteral("проекте");
        }
        if (results.isEmpty()) {
            results    = searchFilesystem(query);
            domainLabel = IS_EN ? QStringLiteral("filesystem") : QStringLiteral("компьютере");
        }
        break;
    }

    return formatResults(results, domainLabel, query);
}

// ============================================================
// Domain::ProjectFiles — через ProjectIndexer
// ============================================================

SearchResults SearchRouter::searchProjectFiles(const QString& query) const
{
    SearchResults results;
    if (!m_indexer || m_indexer->fileCount() == 0) return results;

    // Разбиваем запрос на ключевые слова (каждое ищем отдельно)
    // "функция спавна" → ищем "функция" И "спавна" по отдельности
    const QStringList keywords = query.split(QChar(' '), Qt::SkipEmptyParts);
    QSet<QString> addedPaths;  // чтобы не дублировать

    auto addResult = [&](SearchResult r) {
        const QString key = r.path + r.title;
        if (!addedPaths.contains(key) && results.size() < kMaxResults) {
            addedPaths.insert(key);
            results.append(r);
        }
    };

    // 1. Сначала пробуем весь запрос целиком (точное совпадение)
    {
        auto symbols = m_indexer->findSymbol(query, /*exact=*/false);
        for (const auto& sym : symbols) {
            SearchResult r;
            r.title     = sym.kindToString() + QStringLiteral(" ") + sym.name;
            r.path      = sym.filePath + QStringLiteral(":") + QString::number(sym.lineStart);
            r.snippet   = sym.signature.isEmpty() ? sym.brief : sym.signature;
            r.source    = QStringLiteral("project");
            r.relevance = (sym.name.toLower() == query.toLower()) ? 100 : 75;
            addResult(r);
        }

        auto files = m_indexer->findFile(query);
        for (const auto& f : files) {
            SearchResult r;
            r.title     = QFileInfo(f.filePath).fileName();
            r.path      = f.filePath;
            r.snippet   = QString::number(f.lineCount) + QStringLiteral(" строк, ")
                        + QString::number(f.symbols.size()) + QStringLiteral(" символов");
            r.source    = QStringLiteral("project");
            r.relevance = 80;
            addResult(r);
        }
    }

    // 2. По каждому ключевому слову отдельно (если мало результатов)
    for (const QString& kw : keywords) {
        if (results.size() >= kMaxResults) break;
        if (kw.length() < 3) continue;  // слишком короткое

        auto symbols = m_indexer->findSymbol(kw, /*exact=*/false);
        for (const auto& sym : symbols) {
            SearchResult r;
            r.title     = sym.kindToString() + QStringLiteral(" ") + sym.name;
            r.path      = sym.filePath + QStringLiteral(":") + QString::number(sym.lineStart);
            r.snippet   = sym.signature.isEmpty() ? sym.brief : sym.signature;
            r.source    = QStringLiteral("project");
            r.relevance = (sym.name.toLower() == kw.toLower()) ? 95 : 65;
            addResult(r);
        }

        auto files = m_indexer->findFile(kw);
        for (const auto& f : files) {
            SearchResult r;
            r.title     = QFileInfo(f.filePath).fileName();
            r.path      = f.filePath;
            r.snippet   = QString::number(f.lineCount) + QStringLiteral(" строк, ")
                        + QString::number(f.symbols.size()) + QStringLiteral(" символов");
            r.source    = QStringLiteral("project");
            r.relevance = 70;
            addResult(r);
        }
    }

    // 3. Grep по содержимому — каждое ключевое слово
    if (results.size() < 3) {
        for (const QString& kw : keywords) {
            if (results.size() >= kMaxResults) break;
            if (kw.length() < 3) continue;

            auto hits = m_indexer->grep(kw, kMaxResults - results.size());
            for (const auto& h : hits) {
                SearchResult r;
                r.title     = QFileInfo(h.filePath).fileName()
                            + QStringLiteral(":") + QString::number(h.line);
                r.path      = h.filePath;
                r.snippet   = h.lineText.trimmed();
                r.source    = QStringLiteral("project");
                r.relevance = 55;
                addResult(r);
            }
        }
    }

    // Сортируем по релевантности
    std::sort(results.begin(), results.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.relevance > b.relevance;
              });

    return results;
}

// ============================================================
// Domain::Filesystem — PowerShell поиск + QDirIterator fallback
// ============================================================

SearchResults SearchRouter::searchFilesystem(const QString& query,
                                              const QString& fileTypeHint) const
{
    SearchResults results;

#ifdef Q_OS_WIN
    results = windowsSearch(query);
    if (!results.isEmpty()) return results;
#endif

    // Fallback: обход пользовательских папок
    static const auto getRoots = []() -> QStringList {
        return {
            QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
            QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
            QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
        };
    };

    QStringList filters;
    const QString hint = fileTypeHint.toLower();
    if (hint.contains(QStringLiteral("image")) || hint.contains(QStringLiteral("картинк"))
        || hint.contains(QStringLiteral("фото")) || hint.contains(QStringLiteral("скрин"))) {
        filters = {QStringLiteral("*.png"), QStringLiteral("*.jpg"),
                   QStringLiteral("*.jpeg"), QStringLiteral("*.gif"), QStringLiteral("*.webp")};
    } else if (hint.contains(QStringLiteral("video")) || hint.contains(QStringLiteral("видео"))) {
        filters = {QStringLiteral("*.mp4"), QStringLiteral("*.avi"),
                   QStringLiteral("*.mkv"), QStringLiteral("*.mov")};
    } else if (hint.contains(QStringLiteral("doc")) || hint.contains(QStringLiteral("документ"))) {
        filters = {QStringLiteral("*.docx"), QStringLiteral("*.pdf"),
                   QStringLiteral("*.txt"), QStringLiteral("*.md")};
    }

    const QString queryLower = query.toLower();

    for (const QString& root : getRoots()) {
        if (results.size() >= kMaxResults) break;
        if (root.isEmpty() || !QDir(root).exists()) continue;

        QDirIterator it(root,
                        filters.isEmpty() ? QStringList{QStringLiteral("*")} : filters,
                        QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);

        int depth = 0;
        while (it.hasNext() && results.size() < kMaxResults && depth < 30000) {
            ++depth;
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (!fi.fileName().toLower().contains(queryLower)) continue;

            SearchResult r;
            r.title     = fi.fileName();
            r.path      = fi.absoluteFilePath();
            r.snippet   = fi.lastModified().toString(QStringLiteral("dd.MM.yyyy HH:mm"))
                        + QStringLiteral("  ") + formatFileSize(fi.size());
            r.source    = QStringLiteral("filesystem");
            r.relevance = (fi.baseName().toLower() == queryLower) ? 100 : 70;
            results.append(r);
        }
    }

    std::sort(results.begin(), results.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.relevance > b.relevance;
              });

    return results;
}

// ============================================================
// ============================================================
// Windows Search через системный индекс
// ============================================================

SearchResults SearchRouter::windowsSearch(const QString& query) const
{
    SearchResults results;

#ifdef Q_OS_WIN
    QProcess proc;
    proc.setProgram(QStringLiteral("powershell.exe"));

    const QString safeQuery = QString(query)
        .replace(QStringLiteral("'"),  QStringLiteral("''"))
        .replace(QStringLiteral("\""), QStringLiteral(""));

    // Исправления: UTF-8 кодировка, ограниченная глубина, конкретные папки
    const QString psCmd = QStringLiteral(
        "[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; "
        "$q = '%1'; "
        "$searchDirs = @( "
        "  [Environment]::GetFolderPath('Desktop'), "
        "  [Environment]::GetFolderPath('MyDocuments'), "
        "  (Join-Path $env:USERPROFILE 'Downloads'), "
        "  (Join-Path $env:USERPROFILE 'Pictures'), "
        "  (Join-Path $env:USERPROFILE 'Videos'), "
        "  (Join-Path $env:USERPROFILE 'Music') "
        "); "
        "$found = @(); "
        "foreach ($dir in $searchDirs) { "
        "  if (-not (Test-Path $dir -ErrorAction SilentlyContinue)) { continue } "
        "  $items = Get-ChildItem -Path $dir -Recurse -Depth 5 "
        "    -ErrorAction SilentlyContinue "
        "    | Where-Object { !$_.PSIsContainer -and $_.Name -like ('*' + $q + '*') } "
        "    | Sort-Object LastWriteTime -Descending "
        "    | Select-Object -First 3; "
        "  $found += $items "
        "} "
        "$found "
        "  | Sort-Object LastWriteTime -Descending "
        "  | Select-Object -First 8 "
        "  | ForEach-Object { "
        "      Write-Output ($_.FullName + '|' + "
        "                    $_.LastWriteTime.ToString('dd.MM.yyyy HH:mm') + '|' + "
        "                    $_.Length) "
        "  }"
    ).arg(safeQuery);

    proc.setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-NonInteractive"),
        QStringLiteral("-OutputFormat"), QStringLiteral("Text"),
        QStringLiteral("-Command"), psCmd
    });

    proc.start();
    if (!proc.waitForFinished(10000)) {
        proc.kill();
        return results;
    }

    const QString output = QString::fromUtf8(
        proc.readAllStandardOutput()).trimmed();

    if (output.isEmpty()) return results;

    for (const QString& line : output.split(QChar('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QChar('#'))) continue;

        const QStringList parts = trimmed.split(QChar('|'));
        if (parts.isEmpty()) continue;

        const QString path = parts[0].trimmed();
        if (path.isEmpty() || !QFileInfo::exists(path)) continue;

        const QFileInfo fi(path);
        SearchResult r;
        r.title   = fi.fileName();
        r.path    = fi.absoluteFilePath();
        r.source  = QStringLiteral("filesystem");

        if (parts.size() >= 3) {
            r.snippet = parts[1].trimmed()
                      + QStringLiteral("  ")
                      + formatFileSize(parts[2].trimmed().toLongLong());
        } else {
            r.snippet = fi.lastModified().toString(
                QStringLiteral("dd.MM.yyyy HH:mm"));
        }

        const QString baseLower = fi.baseName().toLower();
        const QString qLower    = query.toLower();
        r.relevance = (baseLower == qLower)           ? 100
                    : baseLower.contains(qLower)      ? 85 : 70;

        results.append(r);
        if (results.size() >= kMaxResults) break;
    }

    std::sort(results.begin(), results.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.relevance > b.relevance;
              });
#endif

    return results;
}

// ============================================================
// Domain::BrowserHistory — SQLite Chrome/Edge/Brave
// ============================================================

SearchResults SearchRouter::searchBrowserHistory(const QString& query) const
{
    SearchResults results;

#ifdef Q_OS_WIN
    const QString appData = QString::fromLocal8Bit(qgetenv("LOCALAPPDATA"));

    const QStringList historyDbs = {
        appData + QStringLiteral("/Google/Chrome/User Data/Default/History"),
        appData + QStringLiteral("/Microsoft/Edge/User Data/Default/History"),
        appData + QStringLiteral("/BraveSoftware/Brave-Browser/User Data/Default/History"),
    };

    for (const QString& dbPath : historyDbs) {
        if (!QFile::exists(dbPath)) continue;
        const auto r = parseBrowserSqlite(dbPath, query);
        results.append(r);
        if (results.size() >= kMaxResults) break;
    }
#endif

    return results;
}

SearchResults SearchRouter::parseBrowserSqlite(const QString& dbPath,
                                                const QString& query) const
{
    SearchResults results;

    // Chrome держит файл заблокированным — копируем во временный
    const QString tmpPath = QDir::tempPath() + QStringLiteral("/jarvis_hist_tmp.db");
    QFile::remove(tmpPath);
    if (!QFile::copy(dbPath, tmpPath)) return results;

    const QString connName = QStringLiteral("jarvis_browser_")
                           + QString::number(reinterpret_cast<qintptr>(this));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(tmpPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));

        if (db.open()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "SELECT title, url, last_visit_time "
                "FROM urls "
                "WHERE title LIKE :q OR url LIKE :q "
                "ORDER BY last_visit_time DESC "
                "LIMIT :lim"
            ));
            q.bindValue(QStringLiteral(":q"),
                        QStringLiteral("%") + query + QStringLiteral("%"));
            q.bindValue(QStringLiteral(":lim"), kMaxResults);

            if (q.exec()) {
                while (q.next() && results.size() < kMaxResults) {
                    SearchResult r;
                    r.title  = q.value(0).toString();
                    r.path   = q.value(1).toString();
                    // Chrome: микросекунды с 1601-01-01
                    const qint64 ct = q.value(2).toLongLong();
                    if (ct > 0) {
                        const qint64 unixMs = (ct - Q_INT64_C(11644473600000000)) / 1000;
                        r.snippet = QDateTime::fromMSecsSinceEpoch(unixMs)
                                        .toString(QStringLiteral("dd.MM.yyyy HH:mm"));
                    }
                    r.source    = QStringLiteral("browser");
                    r.relevance = 80;
                    if (!r.path.isEmpty()) results.append(r);
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connName);
    QFile::remove(tmpPath);
    return results;
}

// ============================================================
// Domain::ChatHistory — SessionMemory
// ============================================================

SearchResults SearchRouter::searchChatHistory(const QString& query) const
{
    SearchResults results;
    if (!m_memory) return results;

    const QString lower   = query.toLower();
    const QString summary = m_memory->sessionSummary();

    for (const QString& line : summary.split(QStringLiteral("\n"))) {
        if (!line.toLower().contains(lower)) continue;

        SearchResult r;
        r.title     = IS_EN ? QStringLiteral("Our conversation")
                            : QStringLiteral("Наш разговор");
        r.snippet   = line.trimmed().left(200);
        r.source    = QStringLiteral("chat");
        r.relevance = 75;
        results.append(r);

        if (results.size() >= kMaxResults) break;
    }

    // Постоянные факты
    const QJsonObject facts = m_memory->allFacts();
    for (auto it = facts.begin(); it != facts.end(); ++it) {
        const QString key = it.key();
        const QString val = it.value().toString();
        if (key.toLower().contains(lower) || val.toLower().contains(lower)) {
            SearchResult r;
            r.title     = (IS_EN ? QStringLiteral("Fact: ")
                                 : QStringLiteral("Факт: ")) + key;
            r.snippet   = val;
            r.source    = QStringLiteral("memory");
            r.relevance = 90;
            results.append(r);
        }
    }

    return results;
}

// ============================================================
// Domain::UE5Logs
// ============================================================

SearchResults SearchRouter::searchUE5Logs(const QString& query) const
{
    SearchResults results;

    const QString localAppData = QString::fromLocal8Bit(qgetenv("LOCALAPPDATA"));

    QStringList logDirs;
    // Корень проекта — первый кандидат
    if (m_indexer && !m_indexer->projectRoot().isEmpty()) {
        logDirs << m_indexer->projectRoot() + QStringLiteral("/Saved/Logs");
    }
    // Системные папки UE5
    for (const QString& ver : {QStringLiteral("5.0"), QStringLiteral("5.1"),
                                QStringLiteral("5.2"), QStringLiteral("5.3"),
                                QStringLiteral("5.4"), QStringLiteral("5.5")}) {
        logDirs << localAppData + QStringLiteral("/UnrealEngine/")
                   + ver + QStringLiteral("/Saved/Logs");
    }

    const QString lower = query.toLower();

    for (const QString& logDir : logDirs) {
        if (!QDir(logDir).exists()) continue;

        const QFileInfoList logs = QDir(logDir).entryInfoList(
            {QStringLiteral("*.log")}, QDir::Files, QDir::Time);

        for (const QFileInfo& fi : logs) {
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

            QTextStream stream(&f);
            int lineNo = 0;
            while (!stream.atEnd() && results.size() < kMaxResults) {
                ++lineNo;
                const QString line = stream.readLine();
                if (!line.toLower().contains(lower)) continue;

                SearchResult r;
                r.title   = fi.fileName() + QStringLiteral(":")
                          + QString::number(lineNo);
                r.path    = fi.absoluteFilePath();
                r.snippet = line.trimmed().left(200);
                r.source  = QStringLiteral("ue5");

                if (line.contains(QStringLiteral("Error"), Qt::CaseInsensitive)
                    || line.contains(QStringLiteral("Fatal"), Qt::CaseInsensitive))
                    r.relevance = 100;
                else if (line.contains(QStringLiteral("Warning"), Qt::CaseInsensitive))
                    r.relevance = 80;
                else
                    r.relevance = 60;

                results.append(r);
            }
            f.close();
            if (results.size() >= kMaxResults) break;
        }
        if (results.size() >= kMaxResults) break;
    }

    std::sort(results.begin(), results.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.relevance > b.relevance;
              });

    return results;
}

// ============================================================
// Domain::Clipboard
// ============================================================

SearchResults SearchRouter::searchClipboard(const QString& query,
                                             const ContextSnapshot& ctx) const
{
    SearchResults results;
    if (ctx.clipboardText.isEmpty()) return results;

    if (query.isEmpty() || ctx.clipboardText.toLower().contains(query.toLower())) {
        SearchResult r;
        r.title     = IS_EN ? QStringLiteral("Clipboard") : QStringLiteral("Буфер обмена");
        r.snippet   = ctx.clipboardText.left(300)
                          .replace(QStringLiteral("\n"), QStringLiteral(" "));
        r.source    = QStringLiteral("clipboard");
        r.relevance = 100;
        results.append(r);
    }

    return results;
}

// ============================================================
// Domain::Memory
// ============================================================

SearchResults SearchRouter::searchMemory(const QString& query) const
{
    SearchResults results;
    if (!m_memory) return results;

    const QString lower = query.toLower();
    const QJsonObject facts = m_memory->allFacts();

    for (auto it = facts.begin(); it != facts.end(); ++it) {
        const QString key = it.key();
        const QString val = it.value().toString();

        if (lower.isEmpty() || key.toLower().contains(lower) || val.toLower().contains(lower)) {
            SearchResult r;
            r.title     = key;
            r.snippet   = val;
            r.source    = QStringLiteral("memory");
            r.relevance = (key.toLower() == lower) ? 100 : 70;
            results.append(r);
        }
    }

    return results;
}

// ============================================================
// Domain::Web — открываем браузер
// ============================================================

QString SearchRouter::openWebSearch(const QString& query) const
{
    const QString url = QStringLiteral("https://www.google.com/search?q=")
                      + QString::fromUtf8(QUrl::toPercentEncoding(query));
    QDesktopServices::openUrl(QUrl(url));

    return IS_EN
        ? QStringLiteral("Opening browser search: «") + query + QStringLiteral("»")
        : QStringLiteral("Открываю поиск в браузере: «") + query + QStringLiteral("»");
}

// ============================================================
// Асинхронный поиск по файловой системе
// ============================================================

void SearchRouter::searchAsync(const Intent& intent, const ContextSnapshot& ctx)
{
    Q_UNUSED(ctx)

    const QString query = intent.query.isEmpty() ? intent.targetFile : intent.query;
    m_fsQuery       = query;
    m_fsDomainLabel = IS_EN ? QStringLiteral("filesystem") : QStringLiteral("компьютере");

    // Удаляем предыдущий watcher если есть
    if (m_fsWatcher) {
        m_fsWatcher->cancel();
        m_fsWatcher->deleteLater();
        m_fsWatcher = nullptr;
    }

    m_fsWatcher = new QFutureWatcher<SearchResults>(this);

    connect(m_fsWatcher, &QFutureWatcher<SearchResults>::finished,
            this, [this]() {
        if (!m_fsWatcher) return;
        const SearchResults results = m_fsWatcher->result();
        const QString formatted = formatResults(results, m_fsDomainLabel, m_fsQuery);
        emit searchFinished(formatted);
        m_fsWatcher->deleteLater();
        m_fsWatcher = nullptr;
    });

    // Запускаем поиск в пуле потоков Qt
    // Копируем query чтобы не обращаться к this из другого потока
    const QString queryCopy = query;
    m_fsWatcher->setFuture(
        QtConcurrent::run([this, queryCopy]() -> SearchResults {
            return searchFilesystem(queryCopy);
        })
    );
}

// ============================================================
// Форматирование результатов
// ============================================================

QString SearchRouter::formatResults(const SearchResults& results,
                                     const QString& domain,
                                     const QString& query)
{
    if (results.isEmpty()) {
        return IS_EN
            ? QStringLiteral("Nothing found in ") + domain
              + QStringLiteral(" for «") + query + QStringLiteral("».\n"
                "Try rephrasing or search in a different place.")
            : QStringLiteral("Ничего не нашёл в ") + domain
              + QStringLiteral(" по запросу «") + query + QStringLiteral("».\n"
                "Попробуй уточнить или поискать в другом месте.");
    }

    QString out = IS_EN
        ? QStringLiteral("Found ") + QString::number(results.size())
          + QStringLiteral(" in ") + domain + QStringLiteral(":\n")
        : QStringLiteral("Нашёл ") + QString::number(results.size())
          + QStringLiteral(" в ") + domain + QStringLiteral(":\n");

    for (const auto& r : results) {
        out += r.format() + QStringLiteral("\n");
    }

    return out.trimmed();
}
