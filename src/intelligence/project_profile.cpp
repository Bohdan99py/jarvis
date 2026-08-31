// -------------------------------------------------------
// project_profile.cpp — сбор профиля проекта
// -------------------------------------------------------

#include "project_profile.h"
#include "project_indexer.h"
#include "jarvis_paths.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QDebug>

namespace {

// Один фильтр с индексатором: иначе профиль считал бы ассеты и точки
// входа вендоренных зависимостей своими.
bool isSkippedDir(const QString& path, const QString& root)
{
    return ProjectIndexer::isIgnoredPath(path, root);
}

// Текстовые файлы, в которых имеет смысл искать ссылки на ассеты.
const QStringList kTextGlobs = {
    QStringLiteral("*.h"), QStringLiteral("*.hpp"), QStringLiteral("*.cpp"),
    QStringLiteral("*.cc"), QStringLiteral("*.c"),
    QStringLiteral("*.qml"), QStringLiteral("*.qrc"), QStringLiteral("*.ui"),
    QStringLiteral("*.js"), QStringLiteral("*.ts"), QStringLiteral("*.py"),
    QStringLiteral("*.cs"), QStringLiteral("*.json"), QStringLiteral("*.html"),
    QStringLiteral("*.css"), QStringLiteral("*.md"), QStringLiteral("*.iss"),
    QStringLiteral("CMakeLists.txt"), QStringLiteral("*.cmake"),
    QStringLiteral("*.pro"), QStringLiteral("*.pri")
};

} // namespace

// ============================================================
// Конструктор / сброс
// ============================================================

ProjectProfile::ProjectProfile(QObject* parent)
    : QObject(parent)
{
}

void ProjectProfile::reset()
{
    m_name.clear();
    m_kind.clear();
    m_buildSystem.clear();
    m_entryPoints.clear();
    m_dependencies.clear();
    m_docs.clear();
    m_readmeSummary.clear();
    m_gitBranch.clear();
    m_recentCommits.clear();
    m_targets.clear();
    m_assets.clear();
    m_assetBytes = 0;
    m_scannedAt  = QDateTime();

    m_hasCMake = m_hasQmake = m_hasUproject = false;
    m_hasPackageJson = m_hasPython = m_hasSolution = m_hasQml = false;
}

// ============================================================
// Полное сканирование
// ============================================================

void ProjectProfile::scan(const QString& projectRoot)
{
    if (projectRoot.isEmpty()) return;

    reset();
    m_root = projectRoot;
    m_name = QFileInfo(projectRoot).fileName();

    scanTree();
    detectKind();
    collectFromIndex();
    collectBuildDependencies();
    markReferencedAssets();
    readDocs();
    readGitInfo();

    m_scannedAt = QDateTime::currentDateTime();
    save();

    qDebug() << "[Profile]" << m_name << m_kind
             << "targets:" << m_targets.size()
             << "assets:"  << m_assets.size();

    emit profileChanged();
}

// ============================================================
// Обход дерева: ассеты, точки входа, признаки типа проекта
// ============================================================

void ProjectProfile::scanTree()
{
    QDirIterator it(m_root, QDir::Files | QDir::Readable,
                    QDirIterator::Subdirectories);

    int entries = 0;

    static const QRegularExpression reAssetExt(
        QStringLiteral(R"(^(png|jpg|jpeg|gif|bmp|webp|ico|svg|wav|mp3|ogg|flac|)"
                       R"(ttf|otf|woff|woff2|fbx|glb|gltf|blend|qm|csv)$)"),
        QRegularExpression::CaseInsensitiveOption);

    while (it.hasNext()) {
        const QString path = it.next();
        if (++entries > MAX_SCAN_ENTRIES) break;
        if (isSkippedDir(path, m_root)) continue;

        const QFileInfo fi(path);
        const QString name = fi.fileName();
        const QString lower = name.toLower();
        const QString suffix = fi.suffix().toLower();

        // --- Признаки типа проекта ---
        if (lower == QStringLiteral("cmakelists.txt"))      m_hasCMake = true;
        else if (suffix == QStringLiteral("pro"))           m_hasQmake = true;
        else if (suffix == QStringLiteral("uproject"))      m_hasUproject = true;
        else if (lower == QStringLiteral("package.json"))   m_hasPackageJson = true;
        else if (lower == QStringLiteral("requirements.txt")
                 || lower == QStringLiteral("pyproject.toml")) m_hasPython = true;
        else if (suffix == QStringLiteral("sln")
                 || suffix == QStringLiteral("csproj"))     m_hasSolution = true;
        else if (suffix == QStringLiteral("qml"))           m_hasQml = true;

        // --- Точки входа ---
        if (lower == QStringLiteral("main.cpp") || lower == QStringLiteral("main.py")
            || lower == QStringLiteral("main.qml") || lower == QStringLiteral("index.js")
            || lower == QStringLiteral("app.qml") || lower == QStringLiteral("program.cs")) {
            if (m_entryPoints.size() < 8) {
                m_entryPoints.append(QDir(m_root).relativeFilePath(path));
            }
        }

        // --- Документация ---
        if (suffix == QStringLiteral("md") && m_docs.size() < 12) {
            m_docs.append(QDir(m_root).relativeFilePath(path));
        }

        // --- Ассеты ---
        if (m_assets.size() < MAX_ASSETS && reAssetExt.match(suffix).hasMatch()) {
            ProjectAsset asset;
            asset.relativePath = QDir(m_root).relativeFilePath(path);
            asset.kind = assetKindFor(suffix);
            asset.size = fi.size();
            m_assets.append(asset);
            m_assetBytes += asset.size;
        }
    }

    std::sort(m_assets.begin(), m_assets.end(),
              [](const ProjectAsset& a, const ProjectAsset& b) {
                  return a.relativePath < b.relativePath;
              });
}

QString ProjectProfile::assetKindFor(const QString& suffix)
{
    static const QSet<QString> images = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("webp"),
        QStringLiteral("ico")
    };
    static const QSet<QString> audio = {
        QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("ogg"),
        QStringLiteral("flac")
    };
    static const QSet<QString> fonts = {
        QStringLiteral("ttf"), QStringLiteral("otf"),
        QStringLiteral("woff"), QStringLiteral("woff2")
    };
    static const QSet<QString> models = {
        QStringLiteral("fbx"), QStringLiteral("glb"),
        QStringLiteral("gltf"), QStringLiteral("blend")
    };

    if (images.contains(suffix)) return QStringLiteral("image");
    if (audio.contains(suffix))  return QStringLiteral("audio");
    if (fonts.contains(suffix))  return QStringLiteral("font");
    if (models.contains(suffix)) return QStringLiteral("model");
    if (suffix == QStringLiteral("svg")) return QStringLiteral("vector");
    if (suffix == QStringLiteral("qm"))  return QStringLiteral("translation");
    return QStringLiteral("data");
}

// ============================================================
// Тип проекта и система сборки
// ============================================================

void ProjectProfile::detectKind()
{
    if (m_hasUproject) {
        m_kind        = QStringLiteral("Unreal Engine project (C++ / Blueprints)");
        m_buildSystem = QStringLiteral("Unreal Build Tool");
    } else if (m_hasCMake) {
        m_kind = m_hasQml ? QStringLiteral("Qt desktop app (C++ + QML)")
                          : QStringLiteral("C++ project (CMake)");
        m_buildSystem = QStringLiteral("CMake");
    } else if (m_hasQmake) {
        m_kind        = QStringLiteral("Qt project (qmake)");
        m_buildSystem = QStringLiteral("qmake");
    } else if (m_hasSolution) {
        m_kind        = QStringLiteral(".NET / Visual Studio solution");
        m_buildSystem = QStringLiteral("MSBuild");
    } else if (m_hasPackageJson) {
        m_kind        = QStringLiteral("JavaScript / Node.js project");
        m_buildSystem = QStringLiteral("npm");
    } else if (m_hasPython) {
        m_kind        = QStringLiteral("Python project");
        m_buildSystem = QStringLiteral("pip / pyproject");
    } else {
        m_kind        = QStringLiteral("Generic source tree");
        m_buildSystem = QStringLiteral("unknown");
    }
}

// ============================================================
// Данные из индекса: таргеты сборки
// ============================================================

void ProjectProfile::collectFromIndex()
{
    if (!m_indexer) return;

    const QStringList buildFiles = m_indexer->filesByLanguage(QStringLiteral("cmake"))
                                 + m_indexer->filesByLanguage(QStringLiteral("qmake"));

    for (const QString& rel : buildFiles) {
        const auto files = m_indexer->findFile(rel);
        for (const auto& f : files) {
            for (const auto& sym : f.symbols) {
                if (sym.kind != CodeSymbol::BuildTarget) continue;
                if (m_targets.size() >= 40) return;

                ProjectBuildTarget target;
                target.name      = sym.name;
                target.type      = sym.signature;
                target.definedIn = f.relativePath;

                bool duplicate = false;
                for (const auto& existing : m_targets) {
                    if (existing.name == target.name) { duplicate = true; break; }
                }
                if (!duplicate) m_targets.append(target);
            }
        }
    }
}

// ============================================================
// Зависимости сборки
// ============================================================

void ProjectProfile::collectBuildDependencies()
{
    static const QRegularExpression reFindPackage(
        QStringLiteral(R"(find_package\s*\(\s*([\w\-]+))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reNamespaced(
        QStringLiteral(R"(\b([A-Z][\w\-]+::[A-Z][\w\-]*)\b)"));

    QSet<QString> deps;
    int filesRead = 0;

    QDirIterator it(m_root,
                    QStringList{QStringLiteral("CMakeLists.txt"),
                                QStringLiteral("*.cmake"),
                                QStringLiteral("*.pro")},
                    QDir::Files | QDir::Readable,
                    QDirIterator::Subdirectories);

    while (it.hasNext() && filesRead < 40) {
        const QString path = it.next();
        if (isSkippedDir(path, m_root)) continue;

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString content = QString::fromUtf8(f.read(200000));
        f.close();
        filesRead++;

        // Комментарии выбрасываем: в них зависимости упоминаются, но
        // ссылкой на реальный таргет сборки не являются.
        QStringList codeLines;
        for (const QString& line : content.split(QChar('\n'))) {
            if (line.trimmed().startsWith(QChar('#'))) continue;
            codeLines.append(line);
        }
        const QString code = codeLines.join(QChar('\n'));

        auto pkgIt = reFindPackage.globalMatch(code);
        while (pkgIt.hasNext()) deps.insert(pkgIt.next().captured(1));

        auto nsIt = reNamespaced.globalMatch(code);
        while (nsIt.hasNext()) deps.insert(nsIt.next().captured(1));
    }

    // package.json / requirements.txt — те же зависимости, другой мир.
    QFile pkg(QDir(m_root).filePath(QStringLiteral("package.json")));
    if (pkg.open(QIODevice::ReadOnly)) {
        const auto doc = QJsonDocument::fromJson(pkg.readAll());
        pkg.close();
        const auto obj = doc.object().value(QStringLiteral("dependencies")).toObject();
        for (auto k = obj.begin(); k != obj.end(); ++k) deps.insert(k.key());
    }

    QFile req(QDir(m_root).filePath(QStringLiteral("requirements.txt")));
    if (req.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&req);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QChar('#'))) continue;
            deps.insert(line.section(QRegularExpression(QStringLiteral("[<>=!\\[]")), 0, 0));
        }
        req.close();
    }

    m_dependencies = deps.values();
    m_dependencies.sort();
    if (m_dependencies.size() > 30) m_dependencies = m_dependencies.mid(0, 30);
}

// ============================================================
// Какие ассеты вообще где-то используются
// ============================================================

void ProjectProfile::markReferencedAssets()
{
    if (m_assets.isEmpty()) return;

    // Из всех текстовых файлов вытаскиваем токены вида "name.png" и
    // складываем в множество. Обратный порядок (искать каждый ассет в
    // каждом файле) — это тысячи подстрочных поисков на мегабайтах.
    static const QRegularExpression reRef(
        QStringLiteral(R"([\w\-]+\.(?:png|jpg|jpeg|gif|bmp|webp|ico|svg|wav|mp3|)"
                       R"(ogg|flac|ttf|otf|woff2?|fbx|glb|gltf|blend|qm|csv))"),
        QRegularExpression::CaseInsensitiveOption);

    QSet<QString> referenced;
    int filesRead = 0;

    QDirIterator it(m_root, kTextGlobs, QDir::Files | QDir::Readable,
                    QDirIterator::Subdirectories);

    while (it.hasNext() && filesRead < MAX_REF_SCAN_FILES) {
        const QString path = it.next();
        if (isSkippedDir(path, m_root)) continue;

        QFile f(path);
        if (f.size() > MAX_REF_SCAN_BYTES) continue;
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString content = QString::fromUtf8(f.readAll());
        f.close();
        filesRead++;

        auto refIt = reRef.globalMatch(content);
        while (refIt.hasNext()) {
            referenced.insert(refIt.next().captured(0).toLower());
        }
    }

    for (auto& asset : m_assets) {
        const QString base = asset.relativePath.section(QChar('/'), -1).toLower();
        asset.referenced = referenced.contains(base);
    }
}

// ============================================================
// README и git
// ============================================================

void ProjectProfile::readDocs()
{
    QString readmePath;
    for (const QString& doc : m_docs) {
        if (doc.section(QChar('/'), -1).toLower().startsWith(QStringLiteral("readme"))) {
            readmePath = QDir(m_root).filePath(doc);
            break;
        }
    }
    if (readmePath.isEmpty()) return;

    QFile f(readmePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&f);
    QString summary;
    while (!in.atEnd() && summary.size() < 600) {
        const QString line = in.readLine().trimmed();
        // Пропускаем заголовки, бейджи и картинки — смысла в них ноль.
        if (line.isEmpty() || line.startsWith(QChar('#'))
            || line.startsWith(QStringLiteral("[!["))
            || line.startsWith(QStringLiteral("!["))
            || line.startsWith(QStringLiteral("---"))) {
            continue;
        }
        summary += line + QChar(' ');
    }
    f.close();

    m_readmeSummary = summary.simplified().left(600);
}

void ProjectProfile::readGitInfo()
{
    QFile head(QDir(m_root).filePath(QStringLiteral(".git/HEAD")));
    if (head.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString line = QString::fromUtf8(head.readLine()).trimmed();
        head.close();
        if (line.startsWith(QStringLiteral("ref:"))) {
            m_gitBranch = line.section(QChar('/'), -1);
        }
    }

    // Читаем журнал рефлога вместо запуска git: без внешнего процесса,
    // работает даже когда git не в PATH.
    QFile log(QDir(m_root).filePath(QStringLiteral(".git/logs/HEAD")));
    if (!log.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QStringList lines;
    QTextStream in(&log);
    while (!in.atEnd()) lines.append(in.readLine());
    log.close();

    for (int i = lines.size() - 1; i >= 0 && m_recentCommits.size() < 3; --i) {
        const QString entry = lines[i].section(QChar('\t'), 1);
        if (!entry.startsWith(QStringLiteral("commit"))) continue;
        const QString message = entry.section(QStringLiteral(": "), 1).trimmed();
        if (!message.isEmpty()) m_recentCommits.append(message.left(80));
    }
}

// ============================================================
// Выборки по ассетам
// ============================================================

QVector<ProjectAsset> ProjectProfile::unusedAssets(int maxItems) const
{
    QVector<ProjectAsset> result;
    for (const auto& asset : m_assets) {
        if (asset.referenced) continue;
        result.append(asset);
        if (result.size() >= maxItems) break;
    }
    return result;
}

QVector<ProjectAsset> ProjectProfile::findAssets(const QString& query, int maxItems) const
{
    QVector<ProjectAsset> result;
    const QString needle = query.trimmed().toLower();
    if (needle.isEmpty()) return result;

    for (const auto& asset : m_assets) {
        if (!asset.relativePath.toLower().contains(needle)
            && !asset.kind.contains(needle)) {
            continue;
        }
        result.append(asset);
        if (result.size() >= maxItems) break;
    }
    return result;
}

// ============================================================
// Текстовые представления
// ============================================================

QString ProjectProfile::humanSize(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 1) + QStringLiteral(" GB");
    if (bytes >= 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024), 'f', 1) + QStringLiteral(" MB");
    if (bytes >= 1024)
        return QString::number(bytes / 1024.0, 'f', 0) + QStringLiteral(" KB");
    return QString::number(bytes) + QStringLiteral(" B");
}

QString ProjectProfile::brief(int maxChars) const
{
    if (!isValid()) return QString();

    QString out;
    out += QStringLiteral("Name: ") + m_name
         + QStringLiteral(" | Kind: ") + m_kind
         + QStringLiteral(" | Build: ") + m_buildSystem + QChar('\n');

    if (!m_gitBranch.isEmpty()) {
        out += QStringLiteral("Git branch: ") + m_gitBranch;
        if (!m_recentCommits.isEmpty()) {
            out += QStringLiteral(" | recent: ")
                 + m_recentCommits.join(QStringLiteral(" / "));
        }
        out += QChar('\n');
    }

    if (m_indexer) {
        const auto stats = m_indexer->languageStats();
        QStringList parts;
        for (auto it = stats.cbegin(); it != stats.cend(); ++it) {
            parts.append(it.key() + QStringLiteral(": ")
                         + QString::number(it.value().first) + QStringLiteral(" files, ")
                         + QString::number(it.value().second) + QStringLiteral(" lines"));
        }
        if (!parts.isEmpty())
            out += QStringLiteral("Languages: ") + parts.join(QStringLiteral("; ")) + QChar('\n');
    }

    if (!m_targets.isEmpty()) {
        QStringList parts;
        for (const auto& t : m_targets) {
            parts.append(t.name + QStringLiteral(" (") + t.type + QStringLiteral(", ")
                         + t.definedIn + QChar(')'));
            if (parts.size() >= 15) break;
        }
        out += QStringLiteral("Build targets: ") + parts.join(QStringLiteral("; ")) + QChar('\n');
    }

    if (!m_dependencies.isEmpty())
        out += QStringLiteral("Dependencies: ") + m_dependencies.join(QStringLiteral(", ")) + QChar('\n');

    if (!m_entryPoints.isEmpty())
        out += QStringLiteral("Entry points: ") + m_entryPoints.join(QStringLiteral(", ")) + QChar('\n');

    if (!m_assets.isEmpty()) {
        QMap<QString, int> byKind;
        for (const auto& a : m_assets) byKind[a.kind]++;
        QStringList kinds;
        for (auto it = byKind.cbegin(); it != byKind.cend(); ++it)
            kinds.append(it.key() + QChar(' ') + QString::number(it.value()));

        const int unused = unusedAssets(9999).size();
        out += QStringLiteral("Assets: ") + QString::number(m_assets.size())
             + QStringLiteral(" files, ") + humanSize(m_assetBytes)
             + QStringLiteral(" (") + kinds.join(QStringLiteral(", ")) + QChar(')');
        if (unused > 0) {
            out += QStringLiteral("; ") + QString::number(unused)
                 + QStringLiteral(" not referenced anywhere");
        }
        out += QChar('\n');
    }

    if (!m_docs.isEmpty())
        out += QStringLiteral("Docs: ") + m_docs.join(QStringLiteral(", ")) + QChar('\n');

    if (!m_readmeSummary.isEmpty())
        out += QStringLiteral("README says: ") + m_readmeSummary + QChar('\n');

    if (out.size() > maxChars)
        out = out.left(maxChars) + QStringLiteral("\n...(truncated)\n");

    return out;
}

QString ProjectProfile::assetsReport(int maxItems) const
{
    if (m_assets.isEmpty())
        return QStringLiteral("No assets found in the project.");

    QString out;
    out += QStringLiteral("Assets: ") + QString::number(m_assets.size())
         + QStringLiteral(" files, ") + humanSize(m_assetBytes) + QStringLiteral("\n");

    int shown = 0;
    for (const auto& asset : m_assets) {
        if (shown >= maxItems) break;
        out += QStringLiteral("  ") + asset.relativePath
             + QStringLiteral(" [") + asset.kind + QStringLiteral(", ")
             + humanSize(asset.size) + QChar(']');
        if (!asset.referenced) out += QStringLiteral(" — NOT referenced");
        out += QChar('\n');
        shown++;
    }

    if (m_assets.size() > shown) {
        out += QStringLiteral("  ...(") + QString::number(m_assets.size() - shown)
             + QStringLiteral(" more)\n");
    }
    return out;
}

// ============================================================
// Кэш профиля
// ============================================================

QString ProjectProfile::cacheFilePath() const
{
    const QString hash = QString::number(qHash(m_root));
    return JarvisPaths::subPath(
        QStringLiteral("project_profile_") + hash + QStringLiteral(".json"));
}

void ProjectProfile::save() const
{
    if (!isValid()) return;

    QJsonObject root;
    root[QStringLiteral("root")]        = m_root;
    root[QStringLiteral("name")]        = m_name;
    root[QStringLiteral("kind")]        = m_kind;
    root[QStringLiteral("build")]       = m_buildSystem;
    root[QStringLiteral("scannedAt")]   = m_scannedAt.toString(Qt::ISODate);
    root[QStringLiteral("readme")]      = m_readmeSummary;
    root[QStringLiteral("gitBranch")]   = m_gitBranch;
    root[QStringLiteral("assetBytes")]  = m_assetBytes;

    auto toArray = [](const QStringList& list) {
        QJsonArray arr;
        for (const auto& s : list) arr.append(s);
        return arr;
    };
    root[QStringLiteral("entryPoints")]   = toArray(m_entryPoints);
    root[QStringLiteral("dependencies")]  = toArray(m_dependencies);
    root[QStringLiteral("docs")]          = toArray(m_docs);
    root[QStringLiteral("recentCommits")] = toArray(m_recentCommits);

    QJsonArray targets;
    for (const auto& t : m_targets) {
        QJsonObject obj;
        obj[QStringLiteral("name")] = t.name;
        obj[QStringLiteral("type")] = t.type;
        obj[QStringLiteral("in")]   = t.definedIn;
        targets.append(obj);
    }
    root[QStringLiteral("targets")] = targets;

    QJsonArray assets;
    for (const auto& a : m_assets) {
        QJsonObject obj;
        obj[QStringLiteral("p")] = a.relativePath;
        obj[QStringLiteral("k")] = a.kind;
        obj[QStringLiteral("s")] = a.size;
        obj[QStringLiteral("r")] = a.referenced;
        assets.append(obj);
    }
    root[QStringLiteral("assets")] = assets;

    QFile out(cacheFilePath());
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        out.close();
    }
}

bool ProjectProfile::load(const QString& projectRoot)
{
    if (projectRoot.isEmpty()) return false;

    reset();
    m_root = projectRoot;

    QFile in(cacheFilePath());
    if (!in.open(QIODevice::ReadOnly)) return false;

    const auto doc = QJsonDocument::fromJson(in.readAll());
    in.close();
    if (!doc.isObject()) return false;

    const QJsonObject root = doc.object();
    if (root[QStringLiteral("root")].toString() != m_root) return false;

    m_name          = root[QStringLiteral("name")].toString();
    m_kind          = root[QStringLiteral("kind")].toString();
    m_buildSystem   = root[QStringLiteral("build")].toString();
    m_scannedAt     = QDateTime::fromString(root[QStringLiteral("scannedAt")].toString(),
                                            Qt::ISODate);
    m_readmeSummary = root[QStringLiteral("readme")].toString();
    m_gitBranch     = root[QStringLiteral("gitBranch")].toString();
    m_assetBytes    = root[QStringLiteral("assetBytes")].toInteger();

    auto toList = [&root](const QString& key) {
        QStringList list;
        for (const auto& v : root[key].toArray()) list.append(v.toString());
        return list;
    };
    m_entryPoints   = toList(QStringLiteral("entryPoints"));
    m_dependencies  = toList(QStringLiteral("dependencies"));
    m_docs          = toList(QStringLiteral("docs"));
    m_recentCommits = toList(QStringLiteral("recentCommits"));

    for (const auto& v : root[QStringLiteral("targets")].toArray()) {
        const QJsonObject obj = v.toObject();
        ProjectBuildTarget t;
        t.name      = obj[QStringLiteral("name")].toString();
        t.type      = obj[QStringLiteral("type")].toString();
        t.definedIn = obj[QStringLiteral("in")].toString();
        m_targets.append(t);
    }

    for (const auto& v : root[QStringLiteral("assets")].toArray()) {
        const QJsonObject obj = v.toObject();
        ProjectAsset a;
        a.relativePath = obj[QStringLiteral("p")].toString();
        a.kind         = obj[QStringLiteral("k")].toString();
        a.size         = obj[QStringLiteral("s")].toInteger();
        a.referenced   = obj[QStringLiteral("r")].toBool();
        m_assets.append(a);
    }

    if (isValid()) emit profileChanged();
    return isValid();
}
