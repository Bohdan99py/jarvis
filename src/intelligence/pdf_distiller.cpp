// ============================================================
// pdf_distiller.cpp — Background PDF Knowledge Extractor
// ============================================================

#include "pdf_distiller.h"
#include "self_journal.h"
#include "database_manager.h"
#include "memory_consolidation.h"

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QDebug>

// ============================================================
//  Singleton
// ============================================================

PdfDistiller& PdfDistiller::instance()
{
    static PdfDistiller inst;
    return inst;
}

PdfDistiller::PdfDistiller(QObject* parent)
    : QObject(parent)
{
    m_scanTimer = new QTimer(this);
    m_scanTimer->setSingleShot(false);
    connect(m_scanTimer, &QTimer::timeout, this, &PdfDistiller::onScanTick);

    // Default scan root on external drive
    m_scanRoot = QStringLiteral("D:/Jarvis/knowledge_base");
}

PdfDistiller::~PdfDistiller()
{
    stopBackgroundScan();
}

// ============================================================
//  Lifecycle
// ============================================================

void PdfDistiller::setScanRoot(const QString& path)
{
    m_scanRoot = path;
}

void PdfDistiller::startBackgroundScan(int intervalMinutes)
{
    ensureTable();
    m_scanTimer->setInterval(intervalMinutes * 60 * 1000);
    m_scanTimer->start();
    qDebug() << "[PdfDistiller] Background scan started, interval:"
             << intervalMinutes << "min, root:" << m_scanRoot;
}

void PdfDistiller::stopBackgroundScan()
{
    m_scanTimer->stop();
}

void PdfDistiller::onScanTick()
{
    if (m_scanning) return;
    if (!QDir(m_scanRoot).exists()) return;

    const int result = processNewPdfs();
    if (result > 0)
        qDebug() << "[PdfDistiller] Processed" << result << "new chunks";
}

// ============================================================
//  Database table
// ============================================================

void PdfDistiller::ensureTable()
{
    if (!DatabaseManager::instance().isOpen()) return;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS pdf_knowledge ("
        "  id             INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  source_file    TEXT NOT NULL,"
        "  page_number    INTEGER DEFAULT 0,"
        "  heading        TEXT DEFAULT '',"
        "  content        TEXT NOT NULL,"
        "  keywords       TEXT DEFAULT '',"
        "  confidence     REAL NOT NULL DEFAULT 0.5,"
        "  state          TEXT NOT NULL DEFAULT 'Unverified',"
        "  doubt_reason   TEXT DEFAULT '',"
        "  created_at     TEXT NOT NULL DEFAULT (datetime('now')),"
        "  verified_at    TEXT"
        ")"));

    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_pdf_state "
        "ON pdf_knowledge(state)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_pdf_source "
        "ON pdf_knowledge(source_file)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_pdf_conf "
        "ON pdf_knowledge(confidence)"));
}

// ============================================================
//  PDF text extraction — via pdftotext (Poppler CLI)
// ============================================================

QString PdfDistiller::extractPdfText(const QString& pdfPath) const
{
    // Try Poppler's pdftotext shipped alongside the binary
    const QString appDir = QCoreApplication::applicationDirPath();
    QString pdfToText = appDir + QStringLiteral("/pdftotext.exe");

    if (!QFileInfo::exists(pdfToText))
        pdfToText = QStringLiteral("pdftotext"); // PATH fallback

    QProcess proc;
    proc.setProgram(pdfToText);
    proc.setArguments({QStringLiteral("-layout"), pdfPath, QStringLiteral("-")});
    proc.start();

    if (!proc.waitForFinished(30000)) {
        qWarning() << "[PdfDistiller] pdftotext timeout for:" << pdfPath;
        return QString();
    }

    if (proc.exitCode() != 0) {
        qWarning() << "[PdfDistiller] pdftotext failed:"
                   << proc.readAllStandardError().left(200);
        return QString();
    }

    return QString::fromUtf8(proc.readAllStandardOutput());
}

int PdfDistiller::estimatePageCount(const QString& pdfPath) const
{
    QFile f(pdfPath);
    if (!f.open(QIODevice::ReadOnly)) return 1;

    // Quick heuristic: count "/Type /Page" occurrences in PDF binary
    const QByteArray data = f.read(1024 * 1024); // first 1MB
    int count = 0;
    int pos = 0;
    while ((pos = data.indexOf("/Type /Page", pos)) != -1) {
        ++count;
        pos += 11;
    }
    return qMax(1, count);
}

// ============================================================
//  Chunking — break extracted text into semantic units
// ============================================================

QList<KnowledgeChunk> PdfDistiller::chunkDocument(const QString& text,
                                                    const QString& sourcePath) const
{
    QList<KnowledgeChunk> chunks;
    if (text.trimmed().isEmpty()) return chunks;

    const int pageCount = estimatePageCount(sourcePath);
    const QStringList lines = text.split(QChar('\n'));

    QString currentHeading;
    QString currentBlock;
    int currentPage = 1;
    int emptyLineRun = 0;

    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();

        // Page break detection (form feed or long empty runs)
        if (rawLine.contains(QChar('\f'))) {
            ++currentPage;
            emptyLineRun = 0;
        }

        if (line.isEmpty()) {
            ++emptyLineRun;
            // Paragraph break after 2+ empty lines
            if (emptyLineRun >= 2 && !currentBlock.isEmpty()) {
                KnowledgeChunk chunk;
                chunk.sourceFile  = sourcePath;
                chunk.pageNumber  = currentPage;
                chunk.heading     = currentHeading;
                chunk.content     = currentBlock.left(MAX_CHUNK_CHARS);
                chunk.keywords    = extractKeywords(currentBlock);
                chunk.confidence  = scoreConfidence(currentBlock, currentHeading, pageCount);

                if (chunk.confidence < DOUBT_THRESHOLD) {
                    chunk.state       = KnowledgeChunk::State::Unverified_Doubt;
                    chunk.doubtReason = detectDoubtReason(currentBlock, chunk.confidence);
                }

                if (chunk.content.length() >= 20)
                    chunks.append(chunk);

                currentBlock.clear();
            }
            continue;
        }

        emptyLineRun = 0;

        // Heading detection: ALL CAPS line, or short line followed by content
        const bool isShortLine = line.length() < 80;
        const bool isUpperCase = line == line.toUpper() && line.length() > 3;
        const bool hasNumberPrefix = QRegularExpression(
            QStringLiteral("^\\d+\\.\\d*\\s+")).match(line).hasMatch();

        if (isShortLine && (isUpperCase || hasNumberPrefix)
            && currentBlock.length() > 50) {
            // Flush previous block
            if (!currentBlock.isEmpty()) {
                KnowledgeChunk chunk;
                chunk.sourceFile  = sourcePath;
                chunk.pageNumber  = currentPage;
                chunk.heading     = currentHeading;
                chunk.content     = currentBlock.left(MAX_CHUNK_CHARS);
                chunk.keywords    = extractKeywords(currentBlock);
                chunk.confidence  = scoreConfidence(currentBlock, currentHeading, pageCount);

                if (chunk.confidence < DOUBT_THRESHOLD) {
                    chunk.state       = KnowledgeChunk::State::Unverified_Doubt;
                    chunk.doubtReason = detectDoubtReason(currentBlock, chunk.confidence);
                }

                if (chunk.content.length() >= 20)
                    chunks.append(chunk);

                currentBlock.clear();
            }
            currentHeading = line;
            continue;
        }

        // Accumulate content
        if (!currentBlock.isEmpty()) currentBlock += QChar(' ');
        currentBlock += line;

        // Force chunk if too long
        if (currentBlock.length() > MAX_CHUNK_CHARS * 2) {
            KnowledgeChunk chunk;
            chunk.sourceFile  = sourcePath;
            chunk.pageNumber  = currentPage;
            chunk.heading     = currentHeading;
            chunk.content     = currentBlock.left(MAX_CHUNK_CHARS);
            chunk.keywords    = extractKeywords(currentBlock);
            chunk.confidence  = scoreConfidence(currentBlock, currentHeading, pageCount);

            if (chunk.confidence < DOUBT_THRESHOLD) {
                chunk.state       = KnowledgeChunk::State::Unverified_Doubt;
                chunk.doubtReason = detectDoubtReason(currentBlock, chunk.confidence);
            }

            chunks.append(chunk);
            currentBlock = currentBlock.mid(MAX_CHUNK_CHARS);
        }
    }

    // Flush final block
    if (currentBlock.length() >= 20) {
        KnowledgeChunk chunk;
        chunk.sourceFile  = sourcePath;
        chunk.pageNumber  = currentPage;
        chunk.heading     = currentHeading;
        chunk.content     = currentBlock.left(MAX_CHUNK_CHARS);
        chunk.keywords    = extractKeywords(currentBlock);
        chunk.confidence  = scoreConfidence(currentBlock, currentHeading, pageCount);

        if (chunk.confidence < DOUBT_THRESHOLD) {
            chunk.state       = KnowledgeChunk::State::Unverified_Doubt;
            chunk.doubtReason = detectDoubtReason(currentBlock, chunk.confidence);
        }

        chunks.append(chunk);
    }

    return chunks;
}

// ============================================================
//  Confidence scoring — how much should JARVIS trust this chunk
// ============================================================

double PdfDistiller::scoreConfidence(const QString& chunk,
                                      const QString& heading,
                                      int pageCount) const
{
    double score = 0.5;

    // Structural signals boost confidence
    if (!heading.isEmpty()) score += 0.1;          // Has a heading
    if (chunk.contains(QChar(':'))) score += 0.05; // Definition-like
    if (chunk.length() > 100) score += 0.05;       // Substantial content

    // Code blocks / technical markers = higher trust
    static const QRegularExpression codeRx(
        QStringLiteral("\\b(class|void|int|return|function|def |import )\\b"));
    if (codeRx.match(chunk).hasMatch()) score += 0.15;

    // Bullet/numbered lists = structured = trustworthy
    if (chunk.contains(QStringLiteral("• "))
        || chunk.contains(QStringLiteral("- "))
        || QRegularExpression(QStringLiteral("^\\d+\\.\\s"))
               .match(chunk).hasMatch())
        score += 0.1;

    // OCR artifact signals = reduce confidence
    int garbageChars = 0;
    for (const QChar& ch : chunk) {
        if (ch.unicode() > 0x2000 && ch.unicode() < 0x2100) ++garbageChars;
        if (ch == QChar(0xFFFD)) garbageChars += 3; // replacement char
    }
    if (garbageChars > 5) score -= 0.2;

    // Very short or very long documents = less context reliability
    if (pageCount > 200) score -= 0.1;
    if (chunk.length() < 40) score -= 0.15;

    // Contradictory or vague language
    static const QStringList vagueMarkers = {
        QStringLiteral("maybe"), QStringLiteral("perhaps"),
        QStringLiteral("possibly"), QStringLiteral("unclear"),
        QStringLiteral("возможно"), QStringLiteral("вероятно"),
        QStringLiteral("не ясно"), QStringLiteral("неточно"),
    };
    for (const QString& vague : vagueMarkers) {
        if (chunk.toLower().contains(vague)) {
            score -= 0.1;
            break;
        }
    }

    return qBound(0.0, score, 1.0);
}

QString PdfDistiller::detectDoubtReason(const QString& chunk,
                                          double confidence) const
{
    QStringList reasons;

    if (confidence < 0.4)
        reasons.append(QStringLiteral("very low structural clarity"));
    if (chunk.length() < 40)
        reasons.append(QStringLiteral("fragment too short for reliable understanding"));

    // OCR artifacts
    int oddChars = 0;
    for (const QChar& ch : chunk)
        if (ch == QChar(0xFFFD) || (ch.unicode() > 0x2000 && ch.unicode() < 0x2100))
            ++oddChars;
    if (oddChars > 3)
        reasons.append(QStringLiteral("possible OCR artifacts detected"));

    // Vague language
    const QString lower = chunk.toLower();
    if (lower.contains(QStringLiteral("unclear"))
        || lower.contains(QStringLiteral("не ясно")))
        reasons.append(QStringLiteral("source text itself expresses uncertainty"));

    if (lower.contains(QStringLiteral("deprecated"))
        || lower.contains(QStringLiteral("устарел")))
        reasons.append(QStringLiteral("content may be outdated"));

    if (reasons.isEmpty())
        reasons.append(QStringLiteral("insufficient context for high confidence"));

    return reasons.join(QStringLiteral("; "));
}

// ============================================================
//  Keyword extraction — lightweight TF-style
// ============================================================

QStringList PdfDistiller::extractKeywords(const QString& text) const
{
    QStringList keywords;
    static const QRegularExpression wordRx(QStringLiteral("[A-Za-zА-Яа-яёЁ]{4,}"));

    // Count word frequencies
    QHash<QString, int> freq;
    auto it = wordRx.globalMatch(text);
    while (it.hasNext()) {
        const QString word = it.next().captured().toLower();
        ++freq[word];
    }

    // Filter: skip stopwords, keep top-N by frequency
    static const QSet<QString> stopwords = {
        QStringLiteral("this"), QStringLiteral("that"), QStringLiteral("with"),
        QStringLiteral("from"), QStringLiteral("have"), QStringLiteral("been"),
        QStringLiteral("will"), QStringLiteral("they"), QStringLiteral("their"),
        QStringLiteral("what"), QStringLiteral("when"), QStringLiteral("which"),
        QStringLiteral("more"), QStringLiteral("also"), QStringLiteral("each"),
        QStringLiteral("если"), QStringLiteral("этот"), QStringLiteral("быть"),
        QStringLiteral("можно"), QStringLiteral("нужно"), QStringLiteral("который"),
    };

    QVector<QPair<QString, int>> sorted;
    for (auto fi = freq.constBegin(); fi != freq.constEnd(); ++fi) {
        if (!stopwords.contains(fi.key()) && fi.value() >= 1)
            sorted.append({fi.key(), fi.value()});
    }

    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    for (int i = 0; i < qMin(8, sorted.size()); ++i)
        keywords.append(sorted[i].first);

    return keywords;
}

// ============================================================
//  Main pipeline — scan, extract, chunk, score, store
// ============================================================

bool PdfDistiller::isAlreadyProcessed(const QString& pdfPath) const
{
    if (!DatabaseManager::instance().isOpen()) return false;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return false;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT 1 FROM pdf_knowledge WHERE source_file = :sf LIMIT 1"));
    q.bindValue(QStringLiteral(":sf"), pdfPath);
    return q.exec() && q.next();
}

int PdfDistiller::processNewPdfs()
{
    if (m_scanning) return 0;
    if (!QDir(m_scanRoot).exists()) return 0;

    m_scanning = true;
    int newChunks = 0;
    int newDoubts = 0;

    QDirIterator it(m_scanRoot,
                    {QStringLiteral("*.pdf"), QStringLiteral("*.PDF")},
                    QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        const QString pdfPath = it.next();
        if (isAlreadyProcessed(pdfPath)) continue;

        qDebug() << "[PdfDistiller] Processing:" << pdfPath;

        const QString text = extractPdfText(pdfPath);
        if (text.trimmed().isEmpty()) {
            qDebug() << "[PdfDistiller] No text extracted, skipping:" << pdfPath;
            continue;
        }

        const QList<KnowledgeChunk> chunks = chunkDocument(text, pdfPath);

        auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
        if (!db.isOpen()) break;

        for (const KnowledgeChunk& chunk : chunks) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "INSERT INTO pdf_knowledge "
                "(source_file, page_number, heading, content, keywords, "
                " confidence, state, doubt_reason) "
                "VALUES (:sf, :pg, :hd, :ct, :kw, :conf, :st, :dr)"));
            q.bindValue(QStringLiteral(":sf"),   chunk.sourceFile);
            q.bindValue(QStringLiteral(":pg"),   chunk.pageNumber);
            q.bindValue(QStringLiteral(":hd"),   chunk.heading);
            q.bindValue(QStringLiteral(":ct"),   chunk.content);
            q.bindValue(QStringLiteral(":kw"),   chunk.keywords.join(QStringLiteral(",")));
            q.bindValue(QStringLiteral(":conf"), chunk.confidence);

            QString stateStr;
            switch (chunk.state) {
            case KnowledgeChunk::State::Verified:          stateStr = QStringLiteral("Verified"); break;
            case KnowledgeChunk::State::Unverified:        stateStr = QStringLiteral("Unverified"); break;
            case KnowledgeChunk::State::Unverified_Doubt:  stateStr = QStringLiteral("Unverified_Doubt"); break;
            }
            q.bindValue(QStringLiteral(":st"), stateStr);
            q.bindValue(QStringLiteral(":dr"), chunk.doubtReason);

            if (q.exec()) {
                ++newChunks;
                emit chunkExtracted(chunk.heading, chunk.confidence);

                if (chunk.needsVerification()) {
                    ++newDoubts;
                    emit doubtRegistered(chunk.content, chunk.doubtReason);

                    // Log doubt to SelfJournal
                    SelfJournal::instance().logDoubt(
                        chunk.content.left(200),
                        chunk.doubtReason,
                        chunk.confidence,
                        chunk.sourceFile);
                }
            }
        }
    }

    m_scanning = false;
    emit scanComplete(newChunks, newDoubts);
    return newChunks;
}

// ============================================================
//  Query helpers
// ============================================================

QList<KnowledgeChunk> PdfDistiller::recentChunks(int limit) const
{
    QList<KnowledgeChunk> result;
    if (!DatabaseManager::instance().isOpen()) return result;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, source_file, page_number, heading, content, keywords, "
        "       confidence, state, doubt_reason "
        "FROM pdf_knowledge ORDER BY created_at DESC LIMIT :lim"));
    q.bindValue(QStringLiteral(":lim"), limit);

    if (q.exec()) {
        while (q.next()) {
            KnowledgeChunk c;
            c.sourceFile  = q.value(1).toString();
            c.pageNumber  = q.value(2).toInt();
            c.heading     = q.value(3).toString();
            c.content     = q.value(4).toString();
            c.keywords    = q.value(5).toString().split(QChar(','), Qt::SkipEmptyParts);
            c.confidence  = q.value(6).toDouble();
            const QString st = q.value(7).toString();
            if (st == QStringLiteral("Verified"))
                c.state = KnowledgeChunk::State::Verified;
            else if (st == QStringLiteral("Unverified_Doubt"))
                c.state = KnowledgeChunk::State::Unverified_Doubt;
            else
                c.state = KnowledgeChunk::State::Unverified;
            c.doubtReason = q.value(8).toString();
            result.append(c);
        }
    }
    return result;
}

QList<KnowledgeChunk> PdfDistiller::doubtChunks(int limit) const
{
    QList<KnowledgeChunk> result;
    if (!DatabaseManager::instance().isOpen()) return result;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, source_file, page_number, heading, content, keywords, "
        "       confidence, state, doubt_reason "
        "FROM pdf_knowledge WHERE state = 'Unverified_Doubt' "
        "ORDER BY confidence ASC LIMIT :lim"));
    q.bindValue(QStringLiteral(":lim"), limit);

    if (q.exec()) {
        while (q.next()) {
            KnowledgeChunk c;
            c.sourceFile  = q.value(1).toString();
            c.pageNumber  = q.value(2).toInt();
            c.heading     = q.value(3).toString();
            c.content     = q.value(4).toString();
            c.keywords    = q.value(5).toString().split(QChar(','), Qt::SkipEmptyParts);
            c.confidence  = q.value(6).toDouble();
            c.state       = KnowledgeChunk::State::Unverified_Doubt;
            c.doubtReason = q.value(8).toString();
            result.append(c);
        }
    }
    return result;
}

QList<KnowledgeChunk> PdfDistiller::searchChunks(const QString& keyword,
                                                   int limit) const
{
    QList<KnowledgeChunk> result;
    if (!DatabaseManager::instance().isOpen()) return result;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, source_file, page_number, heading, content, keywords, "
        "       confidence, state, doubt_reason "
        "FROM pdf_knowledge "
        "WHERE content LIKE :kw OR keywords LIKE :kw2 OR heading LIKE :kw3 "
        "ORDER BY confidence DESC LIMIT :lim"));
    const QString pat = QStringLiteral("%") + keyword + QStringLiteral("%");
    q.bindValue(QStringLiteral(":kw"),  pat);
    q.bindValue(QStringLiteral(":kw2"), pat);
    q.bindValue(QStringLiteral(":kw3"), pat);
    q.bindValue(QStringLiteral(":lim"), limit);

    if (q.exec()) {
        while (q.next()) {
            KnowledgeChunk c;
            c.sourceFile  = q.value(1).toString();
            c.pageNumber  = q.value(2).toInt();
            c.heading     = q.value(3).toString();
            c.content     = q.value(4).toString();
            c.keywords    = q.value(5).toString().split(QChar(','), Qt::SkipEmptyParts);
            c.confidence  = q.value(6).toDouble();
            const QString st = q.value(7).toString();
            c.state = (st == QStringLiteral("Verified"))
                          ? KnowledgeChunk::State::Verified
                          : (st == QStringLiteral("Unverified_Doubt"))
                                ? KnowledgeChunk::State::Unverified_Doubt
                                : KnowledgeChunk::State::Unverified;
            c.doubtReason = q.value(8).toString();
            result.append(c);
        }
    }
    return result;
}

void PdfDistiller::verifyChunk(qint64 chunkId, bool correct)
{
    if (!DatabaseManager::instance().isOpen()) return;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    QSqlQuery q(db);

    if (correct) {
        q.prepare(QStringLiteral(
            "UPDATE pdf_knowledge SET state = 'Verified', "
            "confidence = MIN(1.0, confidence + 0.2), "
            "verified_at = datetime('now') WHERE id = :id"));
    } else {
        q.prepare(QStringLiteral(
            "UPDATE pdf_knowledge SET state = 'Unverified_Doubt', "
            "confidence = MAX(0.0, confidence - 0.3), "
            "doubt_reason = 'User corrected: understanding was wrong' "
            "WHERE id = :id"));
    }
    q.bindValue(QStringLiteral(":id"), chunkId);
    q.exec();
}

int PdfDistiller::totalChunks() const
{
    if (!DatabaseManager::instance().isOpen()) return 0;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    QSqlQuery q(db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pdf_knowledge"));
    return q.next() ? q.value(0).toInt() : 0;
}

int PdfDistiller::doubtCount() const
{
    if (!DatabaseManager::instance().isOpen()) return 0;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM pdf_knowledge WHERE state = 'Unverified_Doubt'"));
    return q.next() ? q.value(0).toInt() : 0;
}
