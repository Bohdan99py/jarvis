#pragma once
// ============================================================
// pdf_distiller.h — Background PDF Knowledge Extractor
//
// Scans D:/Jarvis/knowledge_base/ for PDFs, extracts text
// via Poppler CLI (pdftotext) or Qt fallback, breaks content
// into semantic chunks, scores confidence, and pushes
// distilled knowledge into Tier 2 local SQLite.
//
// Each chunk gets a confidence score based on:
//   - Structural clarity (headers, lists, code blocks)
//   - Consistency with existing knowledge base
//   - Source quality signals (page count, OCR artifacts)
//
// Low-confidence chunks are tagged 'Unverified_Doubt' and
// routed to the SelfJournal + CuriosityEngine for human
// verification during idle windows.
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QDateTime>

// ============================================================
//  KnowledgeChunk — one semantic unit extracted from a PDF
// ============================================================

struct KnowledgeChunk
{
    QString   sourceFile;        // PDF path
    int       pageNumber = 0;
    QString   heading;           // section heading if detected
    QString   content;           // distilled text (max ~500 chars)
    QStringList keywords;        // extracted semantic tokens
    double    confidence = 0.5;  // 0.0–1.0

    enum class State { Verified, Unverified, Unverified_Doubt };
    State     state = State::Unverified;

    bool needsVerification() const { return state == State::Unverified_Doubt; }
    QString doubtReason;         // why confidence is low
};

// ============================================================
//  PdfDistiller
// ============================================================

class PdfDistiller : public QObject
{
    Q_OBJECT

public:
    static PdfDistiller& instance();

    PdfDistiller(const PdfDistiller&)            = delete;
    PdfDistiller& operator=(const PdfDistiller&) = delete;

    void setScanRoot(const QString& path);
    QString scanRoot() const { return m_scanRoot; }

    void startBackgroundScan(int intervalMinutes = 30);
    void stopBackgroundScan();
    bool isScanning() const { return m_scanning; }

    // Single-pass: scan for new PDFs, extract, score, store
    int processNewPdfs();

    // Query distilled knowledge
    QList<KnowledgeChunk> recentChunks(int limit = 20) const;
    QList<KnowledgeChunk> doubtChunks(int limit = 10) const;
    QList<KnowledgeChunk> searchChunks(const QString& keyword,
                                        int limit = 10) const;

    // Mark a chunk as verified (user confirmed understanding)
    void verifyChunk(qint64 chunkId, bool correct);

    int totalChunks() const;
    int doubtCount() const;

    void ensureTable();

signals:
    void chunkExtracted(const QString& heading, double confidence);
    void doubtRegistered(const QString& content, const QString& reason);
    void scanComplete(int newChunks, int newDoubts);

private:
    explicit PdfDistiller(QObject* parent = nullptr);
    ~PdfDistiller() override;

    void onScanTick();

    // PDF text extraction
    QString extractPdfText(const QString& pdfPath) const;
    int     estimatePageCount(const QString& pdfPath) const;

    // Chunking + scoring
    QList<KnowledgeChunk> chunkDocument(const QString& text,
                                         const QString& sourcePath) const;
    double scoreConfidence(const QString& chunk,
                           const QString& heading,
                           int pageCount) const;
    QString detectDoubtReason(const QString& chunk,
                               double confidence) const;
    QStringList extractKeywords(const QString& text) const;

    // Dedup
    bool isAlreadyProcessed(const QString& pdfPath) const;

    QString  m_scanRoot;
    QTimer*  m_scanTimer = nullptr;
    bool     m_scanning  = false;

    static constexpr int MAX_CHUNK_CHARS = 500;
    static constexpr double DOUBT_THRESHOLD = 0.75;
};
