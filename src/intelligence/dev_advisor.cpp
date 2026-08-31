// -------------------------------------------------------
// dev_advisor.cpp — фоновый советник по проекту
// -------------------------------------------------------

#include "dev_advisor.h"
#include "project_indexer.h"
#include "project_profile.h"
#include "claude_api.h"
#include "edit_journal.h"
#include "jarvis_paths.h"

#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QSet>
#include <QDebug>

namespace {

QString settingsGroup() { return QStringLiteral("devadvisor"); }

// Содержимое файла как текст + определённый стиль перевода строк.
// Стиль важен: молча превратить CRLF-файл в LF — это «изменение» каждой
// строки в git и мгновенная потеря доверия к автоправкам.
bool readTextFile(const QString& path, QString& text, QString& lineEnding)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray raw = f.readAll();
    f.close();

    text = QString::fromUtf8(raw);
    lineEnding = text.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n")
                                                       : QStringLiteral("\n");
    return true;
}

bool writeTextFile(const QString& path, const QString& text)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(text.toUtf8());
    return f.commit();
}

} // namespace

// ============================================================
// DevFinding
// ============================================================

QString DevFinding::severityIcon() const
{
    switch (severity) {
    case Important: return QStringLiteral("🔴");
    case Warning:   return QStringLiteral("🟡");
    default:        return QStringLiteral("🔵");
    }
}

QJsonObject DevFinding::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")]        = id;
    obj[QStringLiteral("kind")]      = kind;
    obj[QStringLiteral("severity")]  = static_cast<int>(severity);
    obj[QStringLiteral("title")]     = title;
    obj[QStringLiteral("detail")]    = detail;
    obj[QStringLiteral("file")]      = file;
    obj[QStringLiteral("line")]      = line;
    obj[QStringLiteral("autoFixed")] = autoFixed;
    obj[QStringLiteral("fixNote")]   = fixNote;
    obj[QStringLiteral("dismissed")] = dismissed;
    obj[QStringLiteral("foundAt")]   = foundAt.toString(Qt::ISODate);
    return obj;
}

DevFinding DevFinding::fromJson(const QJsonObject& obj)
{
    DevFinding f;
    f.id        = obj[QStringLiteral("id")].toString();
    f.kind      = obj[QStringLiteral("kind")].toString();
    f.severity  = static_cast<Severity>(obj[QStringLiteral("severity")].toInt());
    f.title     = obj[QStringLiteral("title")].toString();
    f.detail    = obj[QStringLiteral("detail")].toString();
    f.file      = obj[QStringLiteral("file")].toString();
    f.line      = obj[QStringLiteral("line")].toInt();
    f.autoFixed = obj[QStringLiteral("autoFixed")].toBool();
    f.fixNote   = obj[QStringLiteral("fixNote")].toString();
    f.dismissed = obj[QStringLiteral("dismissed")].toBool();
    f.foundAt   = QDateTime::fromString(obj[QStringLiteral("foundAt")].toString(),
                                        Qt::ISODate);
    return f;
}

// ============================================================
// Конструктор, настройки, таймер
// ============================================================

DevAdvisor::DevAdvisor(QObject* parent)
    : QObject(parent)
{
    QSettings cfg(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
    cfg.beginGroup(settingsGroup());
    m_enabled      = cfg.value(QStringLiteral("enabled"), true).toBool();
    m_autoFix      = cfg.value(QStringLiteral("autoFix"), true).toBool();
    m_deepAnalysis = cfg.value(QStringLiteral("deepAnalysis"), true).toBool();
    m_lastDeepRun  = cfg.value(QStringLiteral("lastDeepRun")).toDateTime();
    cfg.endGroup();

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &DevAdvisor::onTimerFired);
}

QString DevAdvisor::txt(const char* ru, const char* en) const
{
    return m_english ? QString::fromUtf8(en) : QString::fromUtf8(ru);
}

void DevAdvisor::setEnabled(bool on)
{
    m_enabled = on;
    QSettings cfg(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
    cfg.setValue(settingsGroup() + QStringLiteral("/enabled"), on);
}

void DevAdvisor::setAutoFixEnabled(bool on)
{
    m_autoFix = on;
    QSettings cfg(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
    cfg.setValue(settingsGroup() + QStringLiteral("/autoFix"), on);
}

void DevAdvisor::setDeepAnalysisEnabled(bool on)
{
    m_deepAnalysis = on;
    QSettings cfg(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
    cfg.setValue(settingsGroup() + QStringLiteral("/deepAnalysis"), on);
}

void DevAdvisor::setProjectRoot(const QString& root)
{
    if (m_root == root) return;
    m_root = root;
    m_findings.clear();
    load();
}

void DevAdvisor::start(int intervalMinutes)
{
    if (intervalMinutes < 5) intervalMinutes = 5;
    m_timer->start(intervalMinutes * 60 * 1000);
}

void DevAdvisor::stop()
{
    m_timer->stop();
}

bool DevAdvisor::isRunning() const
{
    return m_timer && m_timer->isActive();
}

void DevAdvisor::onTimerFired()
{
    runScan(false);
}

void DevAdvisor::runNow()
{
    runScan(true);
}

// ============================================================
// Проход
// ============================================================

void DevAdvisor::runScan(bool manual)
{
    if (!manual && !m_enabled)            return;
    if (m_scanning)                       return;
    if (m_root.isEmpty() || !m_indexer)   return;
    if (m_indexer->fileCount() == 0)      return;
    if (m_indexer->isIndexing())          return;

    m_scanning = true;
    m_fresh.clear();
    m_cmakeCache.clear();

    // Всё, что советник поправит сам, ложится в один батч журнала:
    // пользователю достаточно одной команды «отмени правки».
    EditJournal::instance().beginBatch(
        txt("фоновый советник", "background advisor"));

    QSet<QString> knownIds;
    for (const auto& f : m_findings) knownIds.insert(f.id);

    checkFilesNotInBuild();
    checkBrokenIncludes();
    checkResources();
    checkUnusedAssets();
    checkHugeFiles();
    checkTodos();
    checkFormatting();

    EditJournal::instance().endBatch();

    mergeFresh();
    save();
    m_cmakeCache.clear();
    m_scanning = false;

    int newCount = 0;
    const DevFinding* top = nullptr;
    for (const auto& f : m_findings) {
        if (f.dismissed || f.autoFixed) continue;
        if (!knownIds.contains(f.id)) newCount++;
        if (!top || f.severity > top->severity) top = &f;
    }

    qDebug() << "[Advisor] scan done:" << m_findings.size() << "findings,"
             << newCount << "new";

    emit findingsUpdated(newCount);

    if (newCount > 0 && top) {
        emit adviceReady(top->severityIcon() + QChar(' ') + top->title,
                         txt("рекомендации", "recommendations"));
    }

    maybeRunDeepAnalysis(manual);
}

void DevAdvisor::addFinding(DevFinding finding)
{
    if (finding.id.isEmpty()) {
        finding.id = finding.kind + QChar('|') + finding.file
                   + QChar('|') + QString::number(finding.line);
    }
    if (!finding.foundAt.isValid())
        finding.foundAt = QDateTime::currentDateTime();

    for (const auto& existing : m_fresh) {
        if (existing.id == finding.id) return;
    }
    m_fresh.append(finding);
}

// Находки прошлого прохода, которые больше не воспроизводятся, считаются
// исправленными и просто исчезают. Переносим только пользовательские
// решения (dismissed) и результаты разбора через LLM — те не выводятся
// из индекса и иначе пропадали бы на первом же проходе.
void DevAdvisor::mergeFresh()
{
    QHash<QString, DevFinding> old;
    for (const auto& f : m_findings) old.insert(f.id, f);

    QVector<DevFinding> merged;
    merged.reserve(m_fresh.size() + 8);

    for (auto& f : m_fresh) {
        const auto it = old.constFind(f.id);
        if (it != old.cend()) {
            f.foundAt   = it->foundAt;
            f.dismissed = it->dismissed;
            if (it->autoFixed && f.fixNote.isEmpty()) {
                f.autoFixed = it->autoFixed;
                f.fixNote   = it->fixNote;
            }
        }
        merged.append(f);
    }

    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-7);
    for (const auto& f : m_findings) {
        if (f.kind != QStringLiteral("llm")) continue;
        if (f.foundAt.isValid() && f.foundAt < cutoff) continue;
        merged.append(f);
    }

    std::sort(merged.begin(), merged.end(),
              [](const DevFinding& a, const DevFinding& b) {
                  if (a.severity != b.severity) return a.severity > b.severity;
                  return a.file < b.file;
              });

    m_findings = merged;
    m_fresh.clear();
}

// ============================================================
// Эвристика: файл есть, а в сборке его нет
// ============================================================

void DevAdvisor::checkFilesNotInBuild()
{
    if (!m_indexer) return;

    // Проверяем только файлы РЕАЛИЗАЦИИ: заголовки во многих проектах в
    // списки исходников не попадают вовсе, и требовать их там — значит
    // выдать по замечанию на каждый .h в проекте.
    static const QSet<QString> implExtensions = {
        QStringLiteral("cpp"), QStringLiteral("cc"),
        QStringLiteral("cxx"), QStringLiteral("c")
    };

    int found = 0;

    const auto& indexed = m_indexer->indexedFiles();
    for (auto it = indexed.cbegin(); it != indexed.cend(); ++it) {
        if (found >= MAX_FINDINGS_PER_KIND) break;

        const IndexedFile& file = it.value();
        const QFileInfo fi(file.filePath);
        if (!implExtensions.contains(fi.suffix().toLower())) continue;

        const QString absPath  = file.filePath;
        const QString fileName = fi.fileName();

        // Ищем в ближайшем CMakeLists, а если там нет — в корневом.
        const QString cmakePath = cmakeForFile(absPath);
        if (cmakePath.isEmpty()) continue;

        const QString content = cmakeContent(cmakePath);
        if (content.isEmpty()) continue;
        if (content.contains(fileName, Qt::CaseInsensitive)) continue;

        const QString rootCMake = QDir(m_root).filePath(QStringLiteral("CMakeLists.txt"));
        if (cmakePath != rootCMake) {
            const QString rootContent = cmakeContent(rootCMake);
            if (rootContent.contains(fileName, Qt::CaseInsensitive)) continue;
        }

        // Сборка может собирать каталог целиком (GLOB) — тогда «отсутствие»
        // имени в скрипте ничего не значит.
        if (content.contains(QStringLiteral("GLOB"), Qt::CaseInsensitive)) continue;

        found++;

        DevFinding f;
        f.kind     = QStringLiteral("not-in-build");
        f.severity = DevFinding::Important;
        f.file     = file.relativePath;
        f.title    = txt("Файл не добавлен в сборку: ",
                         "File is not in the build: ") + file.relativePath;
        f.detail   = txt("Исходник лежит в проекте, но не перечислен в ",
                         "The source exists but is not listed in ")
                   + QDir(m_root).relativeFilePath(cmakePath)
                   + txt(" — он не компилируется и не линкуется.",
                         " — it is never compiled or linked.");

        // Если рядом лежит заголовок и скрипт вообще перечисляет
        // заголовки — добавляем пару целиком, как принято в этом проекте.
        QStringList toAdd;
        const QString headerName = fi.completeBaseName() + QStringLiteral(".h");
        if (QFile::exists(fi.absolutePath() + QChar(0x2F) + headerName)
            && content.contains(QStringLiteral(".h"))
            && !content.contains(headerName, Qt::CaseInsensitive)) {
            toAdd << headerName;
        }
        toAdd << fileName;

        QString note;
        if (m_autoFix && addFilesToCMake(cmakePath, toAdd, note)) {
            f.autoFixed = true;
            f.fixNote   = note;
            const QString message =
                txt("🛠 Добавил в сборку: ", "🛠 Added to the build: ")
                + toAdd.join(QStringLiteral(", ")) + QStringLiteral(" -> ")
                + QDir(m_root).relativeFilePath(cmakePath);
            logFix(message + QStringLiteral(" | ") + note);
            emit autoFixApplied(message);
            m_cmakeCache.remove(cmakePath);   // содержимое изменилось
        }

        addFinding(f);
    }
}

// ============================================================
// Эвристика: #include в никуда
// ============================================================

void DevAdvisor::checkBrokenIncludes()
{
    if (!m_indexer) return;

    // Индекс знает только файлы проекта, поэтому подозреваем лишь
    // включения В КАВЫЧКАХ и без пути: "foo.h". Всё остальное —
    // системные и библиотечные заголовки — живёт по путям компилятора.
    const auto& indexed = m_indexer->indexedFiles();

    QSet<QString> knownNames;
    for (auto it = indexed.cbegin(); it != indexed.cend(); ++it)
        knownNames.insert(QFileInfo(it.value().filePath).fileName().toLower());

    // Группируем по ИМЕНИ заголовка, а не по файлу: заголовок внешней SDK,
    // подключённый в кавычках, встречается сразу в нескольких файлах —
    // три одинаковых замечания подряд читаются как поломка советника,
    // а не как находка.
    QHash<QString, QStringList> missing;

    for (auto it = indexed.cbegin(); it != indexed.cend(); ++it) {
        const IndexedFile& file = it.value();
        if (file.language != QStringLiteral("cpp")) continue;

        const QString dir = QFileInfo(file.filePath).absolutePath();

        for (const QString& inc : file.localIncludes) {
            if (inc.contains(QChar('/')) || inc.contains(QChar('\\'))) continue;
            if (!inc.endsWith(QStringLiteral(".h"))
                && !inc.endsWith(QStringLiteral(".hpp"))) continue;
            if (knownNames.contains(inc.toLower())) continue;
            if (QFile::exists(dir + QChar('/') + inc)) continue;

            missing[inc].append(file.relativePath);
        }
    }

    int found = 0;
    for (auto it = missing.cbegin(); it != missing.cend(); ++it) {
        if (found >= MAX_FINDINGS_PER_KIND) break;
        found++;

        const QStringList& users = it.value();
        // В одном файле — похоже на след переименования; сразу в нескольких —
        // почти наверняка внешняя библиотека, найденная по include-пути.
        const bool likelyExternal = users.size() > 1;

        DevFinding f;
        f.kind     = QStringLiteral("broken-include");
        f.severity = likelyExternal ? DevFinding::Info : DevFinding::Warning;
        f.file     = users.first();
        f.id       = f.kind + QChar('|') + it.key();
        f.title    = txt("Заголовок не найден в проекте: ",
                         "Header not found in the project: ") + it.key();
        f.detail   = txt("Подключают в кавычках: ", "Included with quotes by: ")
                   + users.mid(0, 5).join(QStringLiteral(", "))
                   + (users.size() > 5
                          ? QStringLiteral(" (+") + QString::number(users.size() - 5)
                                + QChar(')')
                          : QString())
                   + (likelyExternal
                          ? txt(". Скорее всего это внешняя библиотека и путь к ней "
                                "задан в сборке — тогда всё в порядке.",
                                ". Most likely an external library resolved through "
                                "an include path — then nothing is wrong.")
                          : txt(". В проекте такого файла нет — похоже на след "
                                "переименования или удаления.",
                                ". No such file in the project — looks like a "
                                "leftover from a rename or deletion."));
        addFinding(f);
    }
}

// ============================================================
// Эвристика: ресурс в .qrc, которого нет на диске
// ============================================================

void DevAdvisor::checkResources()
{
    if (!m_indexer) return;

    int found = 0;

    const auto& indexed = m_indexer->indexedFiles();
    for (auto it = indexed.cbegin(); it != indexed.cend(); ++it) {
        if (found >= MAX_FINDINGS_PER_KIND) break;

        const IndexedFile& qrc = it.value();
        if (qrc.language != QStringLiteral("qrc")) continue;
        const QString qrcDir = QFileInfo(qrc.filePath).absolutePath();

        for (const auto& sym : qrc.symbols) {
            if (found >= MAX_FINDINGS_PER_KIND) break;
            if (sym.kind != CodeSymbol::Resource) continue;
            if (sym.name.isEmpty()) continue;
            if (QFile::exists(QDir(qrcDir).filePath(sym.name))) continue;

            found++;

            DevFinding f;
            f.kind     = QStringLiteral("missing-resource");
            f.severity = DevFinding::Important;
            f.file     = qrc.relativePath;
            f.line     = sym.lineStart;
            f.id       = f.kind + QChar('|') + qrc.relativePath + QChar('|') + sym.name;
            f.title    = txt("Ресурс из .qrc отсутствует на диске: ",
                             "Resource listed in .qrc is missing on disk: ") + sym.name;
            f.detail   = qrc.relativePath
                       + txt(" перечисляет \"", " lists \"") + sym.name
                       + txt("\", но файла нет. Сборка ресурсов упадёт "
                             "или ресурс молча пропадёт в рантайме.",
                             "\", but the file is not there. The resource "
                             "compiler will fail or the asset silently "
                             "disappears at runtime.");
            addFinding(f);
        }
    }
}

// ============================================================
// Эвристика: ассеты, на которые никто не ссылается
// ============================================================

void DevAdvisor::checkUnusedAssets()
{
    if (!m_profile || !m_profile->isValid()) return;

    const auto unused = m_profile->unusedAssets(MAX_FINDINGS_PER_KIND * 4);
    if (unused.isEmpty()) return;

    qint64 bytes = 0;
    QStringList names;
    for (const auto& asset : unused) {
        bytes += asset.size;
        if (names.size() < 8) names.append(asset.relativePath);
    }

    DevFinding f;
    f.kind     = QStringLiteral("unused-assets");
    f.severity = DevFinding::Info;
    f.id       = f.kind;
    f.title    = txt("Ассеты, на которые никто не ссылается: ",
                     "Assets nothing refers to: ") + QString::number(unused.size());
    f.detail   = txt("Ни в коде, ни в .qrc не встречается: ",
                     "Not referenced from code or .qrc: ")
               + names.join(QStringLiteral(", "))
               + (unused.size() > names.size()
                      ? QStringLiteral(" …") : QString())
               + QStringLiteral(" (~") + QString::number(bytes / 1024)
               + txt(" КБ). Удалять сам не буду — проверь, "
                     "не подключаются ли они динамически.",
                     " KB). I will not delete them — check whether they are "
                     "loaded dynamically.");
    addFinding(f);
}

// ============================================================
// Эвристика: разросшиеся файлы
// ============================================================

void DevAdvisor::checkHugeFiles()
{
    if (!m_indexer) return;

    QVector<QPair<int, QString>> big;
    const auto& indexed = m_indexer->indexedFiles();
    for (auto it = indexed.cbegin(); it != indexed.cend(); ++it) {
        const IndexedFile& file = it.value();
        if (file.lineCount < HUGE_FILE_LINES) continue;
        if (file.language != QStringLiteral("cpp")
            && file.language != QStringLiteral("qml")) continue;
        big.append({file.lineCount, file.relativePath});
    }
    if (big.isEmpty()) return;

    std::sort(big.begin(), big.end(),
              [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
                  return a.first > b.first;
              });

    QStringList names;
    for (int i = 0; i < qMin(5, static_cast<int>(big.size())); ++i) {
        names.append(big[i].second + QStringLiteral(" (")
                     + QString::number(big[i].first) + txt(" строк)", " lines)"));
    }

    DevFinding f;
    f.kind     = QStringLiteral("huge-file");
    f.severity = DevFinding::Info;
    f.id       = f.kind;
    f.file     = big.first().second;
    f.title    = txt("Файлы, которые пора делить: ",
                     "Files that are getting hard to work with: ")
               + QString::number(big.size());
    f.detail   = names.join(QStringLiteral("; "))
               + txt(". В таких файлах правки чаще ломают соседний код. "
                     "Скажи «раздели <файл>» — предложу разбиение.",
                     ". Edits in files this size tend to break neighbouring "
                     "code. Say \"split <file>\" and I will propose a breakdown.");
    addFinding(f);
}

// ============================================================
// Эвристика: накопившиеся TODO/FIXME
// ============================================================

void DevAdvisor::checkTodos()
{
    if (!m_indexer) return;

    const auto todos = m_indexer->todos(60);
    if (todos.size() < 5) return;   // пара пометок — это норма, а не проблема

    QStringList lines;
    for (const auto& todo : todos) {
        if (lines.size() >= 5) break;
        const QString rel = QDir(m_root).relativeFilePath(todo.filePath);
        lines.append(rel + QChar(':') + QString::number(todo.lineStart)
                     + QStringLiteral(" — ") + todo.brief.left(80));
    }

    DevFinding f;
    f.kind     = QStringLiteral("todos");
    f.severity = DevFinding::Info;
    f.id       = f.kind;
    f.title    = txt("Незакрытых TODO/FIXME: ", "Open TODO/FIXME markers: ")
               + QString::number(todos.size());
    f.detail   = lines.join(QChar('\n'));
    addFinding(f);
}

// ============================================================
// Автоправка: хвостовые пробелы и перевод строки в конце
// ============================================================

void DevAdvisor::checkFormatting()
{
    if (!m_indexer) return;

    // Только свежие файлы: пробегать формат по всему проекту — это
    // тысяча изменённых строк в git и ноль пользы.
    const QStringList recent = m_indexer->recentFiles(20);
    int fixes = 0;

    for (const QString& absPath : recent) {
        if (fixes >= MAX_FORMAT_FIXES) break;

        const QString lang = ProjectIndexer::languageForFile(absPath);
        if (lang != QStringLiteral("cpp") && lang != QStringLiteral("qml")
            && lang != QStringLiteral("python") && lang != QStringLiteral("js")
            && lang != QStringLiteral("cmake")) {
            continue;
        }

        const QString rel = QDir(m_root).relativeFilePath(absPath);

        QString text, ending;
        if (!readTextFile(absPath, text, ending)) continue;

        static const QRegularExpression reTrailing(
            QStringLiteral(R"([ \t]+(?=\r?\n))"));
        const bool hasTrailing = reTrailing.match(text).hasMatch();
        const bool noFinalNewline = !text.isEmpty()
                                    && !text.endsWith(QChar('\n'));
        if (!hasTrailing && !noFinalNewline) continue;

        DevFinding f;
        f.kind     = QStringLiteral("formatting");
        f.severity = DevFinding::Info;
        f.file     = rel;
        f.id       = f.kind + QChar('|') + rel;
        f.title    = txt("Мелкое форматирование: ", "Whitespace cleanup: ") + rel;
        f.detail   = txt("Хвостовые пробелы и/или нет перевода строки в конце файла.",
                         "Trailing whitespace and/or missing newline at end of file.");

        QString note;
        if (m_autoFix && fixWhitespace(absPath, note)) {
            fixes++;
            f.autoFixed = true;
            f.fixNote   = note;
            const QString message = txt("🧹 Подчистил форматирование: ",
                                        "🧹 Cleaned up whitespace: ") + rel;
            logFix(message + QStringLiteral(" | ") + note);
            emit autoFixApplied(message);
        }

        addFinding(f);
    }
}

bool DevAdvisor::fixWhitespace(const QString& absPath, QString& note)
{
    QString text, ending;
    if (!readTextFile(absPath, text, ending)) return false;

    const QString original = text;

    static const QRegularExpression reTrailing(
        QStringLiteral(R"([ \t]+(?=\r?\n))"));
    text.remove(reTrailing);

    // Хвост файла: пробелы в самом конце и обязательный перевод строки.
    while (text.endsWith(QChar(' ')) || text.endsWith(QChar('\t')))
        text.chop(1);
    if (!text.isEmpty() && !text.endsWith(QChar('\n')))
        text += ending;

    if (text == original) return false;

    EditJournal::instance().recordModify(absPath);
    const QString backup = EditJournal::instance().lastBackupPath();
    if (backup.isEmpty()) return false;   // без страховки не правим
    if (!writeTextFile(absPath, text)) return false;

    note = txt("бэкап: ", "backup: ") + backup;
    return true;
}

// ============================================================
// Автоправка: добавить файл в CMakeLists
// ============================================================

bool DevAdvisor::addFilesToCMake(const QString& cmakeAbsPath,
                                  const QStringList& fileNames,
                                  QString& note)
{
    if (fileNames.isEmpty()) return false;

    QString text, ending;
    if (!readTextFile(cmakeAbsPath, text, ending)) return false;
    for (const QString& name : fileNames) {
        if (text.contains(name, Qt::CaseInsensitive)) return false;
    }

    QStringList lines = text.split(QChar('\n'));

    static const QRegularExpression reTargetStart(
        QStringLiteral(R"(^\s*(add_library|add_executable|qt_add_library|qt_add_executable)\s*\()"),
        QRegularExpression::CaseInsensitiveOption);
    // Строка-исходник: только имя файла, ничего больше. По ней же берём отступ.
    static const QRegularExpression reSourceLine(
        QStringLiteral(R"(^(\s*)([\w./\\-]+\.(?:cpp|cc|cxx|c|h|hpp|inl))\s*\r?$)"),
        QRegularExpression::CaseInsensitiveOption);

    int start = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (reTargetStart.match(lines[i]).hasMatch()) { start = i; break; }
    }
    if (start < 0) return false;

    // Ищем закрывающую скобку блока и последнюю строку-исходник в нём.
    int depth = 0;
    int closeLine = -1;
    int lastSource = -1;
    QString indent = QStringLiteral("    ");

    for (int i = start; i < lines.size(); ++i) {
        const QString& line = lines[i];
        depth += line.count(QChar('(')) - line.count(QChar(')'));

        const auto m = reSourceLine.match(line);
        if (m.hasMatch()) {
            lastSource = i;
            indent = m.captured(1);
        }

        if (depth <= 0) { closeLine = i; break; }
    }

    // Ни одной обычной строки-исходника: список собирается переменной или
    // GLOB'ом — вслепую туда дописывать нельзя.
    if (closeLine < 0 || lastSource < 0) return false;

    EditJournal::instance().recordModify(cmakeAbsPath);
    const QString backup = EditJournal::instance().lastBackupPath();
    if (backup.isEmpty()) return false;   // без страховки не правим

    for (int i = 0; i < fileNames.size(); ++i)
        lines.insert(lastSource + 1 + i, indent + fileNames[i]);

    if (!writeTextFile(cmakeAbsPath, lines.join(QChar('\n')))) return false;

    note = txt("вставлено после строки ", "inserted after line ")
         + QString::number(lastSource + 1)
         + txt("; бэкап: ", "; backup: ") + backup;
    return true;
}

// ============================================================
// Журнал автоправок
// ============================================================

void DevAdvisor::logFix(const QString& message) const
{
    QFile log(JarvisPaths::subPath(QStringLiteral("dev_advisor_log.txt")));
    if (!log.open(QIODevice::Append | QIODevice::Text)) return;
    log.write((QDateTime::currentDateTime().toString(Qt::ISODate)
               + QStringLiteral("  ") + message + QChar('\n')).toUtf8());
    log.close();
}

// ============================================================
// Разбор через LLM
// ============================================================

void DevAdvisor::maybeRunDeepAnalysis(bool manual)
{
    if (!m_deepAnalysis) return;
    if (!m_claude || !m_claude->hasApiKey()) return;
    // Пользователь ждёт свой ответ — не влезаем в очередь запросов.
    if (m_claude->isRequesting()) return;

    if (!manual && m_lastDeepRun.isValid()
        && m_lastDeepRun.secsTo(QDateTime::currentDateTime())
               < DEEP_ANALYSIS_HOURS * 3600) {
        return;
    }

    const QStringList recent = m_indexer->recentFiles(8);
    if (recent.isEmpty()) return;

    QStringList recentRel;
    for (const QString& abs : recent)
        recentRel.append(QDir(m_root).relativeFilePath(abs));

    QString localFindings;
    for (const auto& f : m_findings) {
        if (f.dismissed || f.autoFixed) continue;
        localFindings += QStringLiteral("- ") + f.title + QChar('\n');
        if (localFindings.size() > 1500) break;
    }

    QString prompt =
        QStringLiteral("[BACKGROUND PROJECT REVIEW — no file operations]\n"
                       "You are reviewing the user's project in the background. "
                       "Do NOT emit [FILE:], [DIFF:], [CMD:] or [NEED:] blocks — "
                       "nothing will be executed and nobody is waiting for an answer.\n\n");

    if (m_profile && m_profile->isValid())
        prompt += QStringLiteral("PROJECT:\n") + m_profile->brief(1500) + QChar('\n');

    prompt += QStringLiteral("\nRECENTLY MODIFIED FILES:\n")
            + recentRel.join(QChar('\n')) + QChar('\n');

    if (!localFindings.isEmpty()) {
        prompt += QStringLiteral("\nALREADY KNOWN (do not repeat):\n") + localFindings;
    }

    prompt += QStringLiteral(
        "\nGive AT MOST 3 concrete recommendations about where this project is "
        "heading: architectural risks, things that will break as it grows, "
        "missing wiring between modules. Skip generic advice ('write tests', "
        "'add documentation') — only what follows from THIS project's structure. "
        "If you see nothing worth saying, answer exactly NOTHING.\n"
        "Format, one per line, no other text:\n"
        "ADVICE :: <one-line title> :: <2-3 sentences of detail>\n"
        "Answer in ")
        + (m_english ? QStringLiteral("English.") : QStringLiteral("Russian."));

    m_lastDeepRun = QDateTime::currentDateTime();
    QSettings cfg(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
    cfg.setValue(settingsGroup() + QStringLiteral("/lastDeepRun"), m_lastDeepRun);

    m_claude->sendMessage(prompt, [this](bool ok, const QString& response) {
        if (!ok) {
            qDebug() << "[Advisor] deep analysis failed:" << response.left(120);
            return;
        }
        applyDeepAnalysis(response);
    });
}

void DevAdvisor::applyDeepAnalysis(const QString& response)
{
    if (response.trimmed().compare(QStringLiteral("NOTHING"),
                                   Qt::CaseInsensitive) == 0) {
        return;
    }

    int added = 0;
    const QStringList lines = response.split(QChar('\n'));
    for (const QString& line : lines) {
        if (added >= 3) break;
        if (!line.trimmed().startsWith(QStringLiteral("ADVICE"))) continue;

        const QStringList parts = line.split(QStringLiteral("::"));
        if (parts.size() < 3) continue;

        const QString title  = parts[1].trimmed();
        const QString detail = parts.mid(2).join(QStringLiteral("::")).trimmed();
        if (title.isEmpty()) continue;

        DevFinding f;
        f.kind     = QStringLiteral("llm");
        f.severity = DevFinding::Warning;
        f.title    = title;
        f.detail   = detail;
        f.foundAt  = QDateTime::currentDateTime();
        // Идентификатор по содержанию: один и тот же совет, полученный
        // повторно, не размножится в списке.
        f.id       = f.kind + QChar('|')
                   + QString::fromLatin1(
                         QCryptographicHash::hash(title.toUtf8(),
                                                  QCryptographicHash::Md5)
                             .toHex()
                             .left(12));

        bool duplicate = false;
        for (const auto& existing : m_findings) {
            if (existing.id == f.id) { duplicate = true; break; }
        }
        if (duplicate) continue;

        m_findings.append(f);
        added++;
    }

    if (added == 0) return;

    save();
    qDebug() << "[Advisor] deep analysis added" << added << "recommendations";
    emit findingsUpdated(added);
}

// ============================================================
// Выдача наружу
// ============================================================

QVector<DevFinding> DevAdvisor::openFindings(int maxItems) const
{
    QVector<DevFinding> result;
    for (const auto& f : m_findings) {
        if (f.dismissed) continue;
        result.append(f);
        if (result.size() >= maxItems) break;
    }
    return result;
}

QString DevAdvisor::report(int maxItems) const
{
    if (m_root.isEmpty()) {
        return txt("Проект не открыт — советовать нечего.",
                   "No project is open — nothing to advise on.");
    }

    const auto open = openFindings(maxItems);
    if (open.isEmpty()) {
        return txt("👌 По проекту замечаний нет: сборка полная, ресурсы на месте.",
                   "👌 Nothing to report: the build is complete, resources are in place.");
    }

    QString out = txt("📋 Что я вижу по проекту:\n\n",
                      "📋 What I see in the project:\n\n");

    int fixedCount = 0;
    for (const auto& f : open) {
        out += f.severityIcon() + QChar(' ') + f.title + QChar('\n');
        if (!f.detail.isEmpty())
            out += QStringLiteral("   ") + f.detail + QChar('\n');
        if (f.autoFixed) {
            fixedCount++;
            out += txt("   ✅ уже исправлено автоматически (",
                       "   ✅ already fixed automatically (")
                 + f.fixNote + QStringLiteral(")\n");
        }
        out += QChar('\n');
    }

    if (fixedCount > 0) {
        out += txt("Исправленное можно откатить: копии лежат в Jarvis Data/advisor_backups.",
                   "Fixes can be rolled back: copies are in Jarvis Data/advisor_backups.");
    }
    return out.trimmed();
}

void DevAdvisor::dismiss(const QString& id)
{
    for (auto& f : m_findings) {
        if (f.id == id) { f.dismissed = true; break; }
    }
    save();
}

void DevAdvisor::dismissAll()
{
    for (auto& f : m_findings) f.dismissed = true;
    save();
}

// ============================================================
// Служебное
// ============================================================

QString DevAdvisor::cmakeForFile(const QString& absPath) const
{
    QDir dir(QFileInfo(absPath).absolutePath());
    const QString rootPath = QDir(m_root).absolutePath();

    for (int depth = 0; depth < 6; ++depth) {
        const QString candidate = dir.filePath(QStringLiteral("CMakeLists.txt"));
        if (QFile::exists(candidate)) return candidate;
        if (dir.absolutePath() == rootPath) break;
        if (!dir.cdUp()) break;
    }
    return QString();
}

QString DevAdvisor::cmakeContent(const QString& absPath)
{
    const auto it = m_cmakeCache.constFind(absPath);
    if (it != m_cmakeCache.cend()) return it.value();

    QString text, ending;
    if (!readTextFile(absPath, text, ending)) text.clear();
    m_cmakeCache.insert(absPath, text);
    return text;
}

QString DevAdvisor::absPathFor(const QString& relPath) const
{
    return QDir(m_root).filePath(relPath);
}

QString DevAdvisor::findingsFilePath() const
{
    const QString hash = QString::number(qHash(m_root));
    return JarvisPaths::subPath(
        QStringLiteral("dev_advisor_") + hash + QStringLiteral(".json"));
}

void DevAdvisor::save() const
{
    if (m_root.isEmpty()) return;

    QJsonArray arr;
    for (const auto& f : m_findings) arr.append(f.toJson());

    QJsonObject root;
    root[QStringLiteral("project")]  = m_root;
    root[QStringLiteral("findings")] = arr;

    QFile out(findingsFilePath());
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        out.close();
    }
}

void DevAdvisor::load()
{
    QFile in(findingsFilePath());
    if (!in.open(QIODevice::ReadOnly)) return;

    const auto doc = QJsonDocument::fromJson(in.readAll());
    in.close();
    if (!doc.isObject()) return;

    const QJsonObject root = doc.object();
    if (root[QStringLiteral("project")].toString() != m_root) return;

    for (const auto& v : root[QStringLiteral("findings")].toArray())
        m_findings.append(DevFinding::fromJson(v.toObject()));
}
