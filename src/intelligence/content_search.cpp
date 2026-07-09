// -------------------------------------------------------
// content_search.cpp — Поиск по содержимому файлов
// -------------------------------------------------------

#include "content_search.h"
#include "ocr_extractor.h"
#include "synonym_learner.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QByteArray>
#include <QRegularExpression>
#include <QBuffer>
#include <algorithm>

// zlib через Qt (Qt6 поставляется со своим zlib)
#include <QtZlib/zlib.h>

namespace ZipUtil {

// Читает ZIP из байтов и возвращает содержимое нужного файла
static QByteArray extractEntry(const QByteArray& zipData, const QString& entryName)
{
    const QByteArray target = entryName.toUtf8();
    const uchar* data = reinterpret_cast<const uchar*>(zipData.constData());
    const int    size = zipData.size();
    int i = 0;

    while (i + 30 < size) {
        // Local file header signature
        if (data[i]   != 0x50 || data[i+1] != 0x4B ||
            data[i+2] != 0x03 || data[i+3] != 0x04) {
            ++i;
            continue;
        }

        const quint16 comprMethod  = data[i+8]  | (data[i+9]  << 8);
        const quint32 compSize     = data[i+18] | (data[i+19] << 8)
                                   | (data[i+20] << 16) | (data[i+21] << 24);
        const quint32 uncompSize   = data[i+22] | (data[i+23] << 8)
                                   | (data[i+24] << 16) | (data[i+25] << 24);
        const quint16 nameLen      = data[i+26] | (data[i+27] << 8);
        const quint16 extraLen     = data[i+28] | (data[i+29] << 8);

        if (i + 30 + nameLen > size) break;

        const QByteArray name(reinterpret_cast<const char*>(data + i + 30), nameLen);
        const int dataOffset = i + 30 + nameLen + extraLen;

        if (name == target && dataOffset + static_cast<int>(compSize) <= size) {
            if (comprMethod == 0) {
                // Store — без сжатия
                return QByteArray(reinterpret_cast<const char*>(data + dataOffset),
                                  static_cast<int>(uncompSize));
            } else if (comprMethod == 8) {
                // Deflate — распаковываем через zlib
                QByteArray out(static_cast<int>(uncompSize), '\0');
                z_stream zs{};
                zs.next_in   = const_cast<uchar*>(data + dataOffset);
                zs.avail_in  = compSize;
                zs.next_out  = reinterpret_cast<uchar*>(out.data());
                zs.avail_out = uncompSize;

                if (inflateInit2(&zs, -MAX_WBITS) == Z_OK) {
                    if (inflate(&zs, Z_FINISH) == Z_STREAM_END) {
                        inflateEnd(&zs);
                        return out;
                    }
                    inflateEnd(&zs);
                }
            }
        }

        i = dataOffset + static_cast<int>(compSize);
    }
    return {};
}

// Перечисляет все файлы в ZIP (для PPTX где нужно найти slide*.xml)
static QStringList listEntries(const QByteArray& zipData)
{
    QStringList entries;
    const uchar* data = reinterpret_cast<const uchar*>(zipData.constData());
    const int    size = zipData.size();
    int i = 0;

    while (i + 30 < size) {
        if (data[i]   != 0x50 || data[i+1] != 0x4B ||
            data[i+2] != 0x03 || data[i+3] != 0x04) {
            ++i;
            continue;
        }
        const quint32 compSize = data[i+18] | (data[i+19] << 8)
                               | (data[i+20] << 16) | (data[i+21] << 24);
        const quint16 nameLen  = data[i+26] | (data[i+27] << 8);
        const quint16 extraLen = data[i+28] | (data[i+29] << 8);

        if (i + 30 + nameLen > size) break;

        entries.append(QString::fromUtf8(
            reinterpret_cast<const char*>(data + i + 30), nameLen));

        i += 30 + nameLen + extraLen + static_cast<int>(compSize);
    }
    return entries;
}

} // namespace ZipUtil

// ============================================================
// ContentHit::format
// ============================================================

QString ContentHit::format() const
{
    QString out = QStringLiteral("  📄 ") + fileName;
    if (lineNumber > 0) {
        out += QStringLiteral(":") + QString::number(lineNumber);
    }
    if (!matchedLine.isEmpty()) {
        out += QStringLiteral("\n     ") + matchedLine;
    }
    return out;
}

// ============================================================
// isSupportedFormat
// ============================================================

bool ContentSearch::isSupportedFormat(const QString& filePath)
{
    const QString ext = QFileInfo(filePath).suffix().toLower();
    static const QStringList supported = {
        // Текстовые
        QStringLiteral("txt"),  QStringLiteral("md"),   QStringLiteral("log"),
        QStringLiteral("csv"),  QStringLiteral("json"), QStringLiteral("yaml"),
        QStringLiteral("yml"),  QStringLiteral("xml"),  QStringLiteral("ini"),
        QStringLiteral("toml"), QStringLiteral("cfg"),  QStringLiteral("conf"),
        // Исходники
        QStringLiteral("cpp"),  QStringLiteral("h"),    QStringLiteral("hpp"),
        QStringLiteral("c"),    QStringLiteral("py"),   QStringLiteral("js"),
        QStringLiteral("ts"),   QStringLiteral("cs"),   QStringLiteral("java"),
        QStringLiteral("rs"),   QStringLiteral("go"),   QStringLiteral("swift"),
        QStringLiteral("kt"),   QStringLiteral("rb"),   QStringLiteral("php"),
        QStringLiteral("sh"),   QStringLiteral("bat"),  QStringLiteral("ps1"),
        QStringLiteral("cmake"),QStringLiteral("qml"),
        // Документы
        QStringLiteral("pdf"),
        QStringLiteral("docx"), QStringLiteral("docm"),
        QStringLiteral("xlsx"), QStringLiteral("xlsm"),
        QStringLiteral("pptx"), QStringLiteral("pptm"),
        QStringLiteral("odt"),  QStringLiteral("ods"),
    };
    return supported.contains(ext);
}

// ============================================================
// extractText — диспетчер по формату
// ============================================================

QString ContentSearch::extractText(const QString& filePath, int maxChars)
{
    const QString ext = QFileInfo(filePath).suffix().toLower();

    if (ext == QStringLiteral("pdf")) {
        // Сначала пробуем байтовый поиск текста
        QString text = extractTextPdf(filePath, maxChars);

        // Если ничего не нашли (скан) — запускаем OCR
        if (text.trimmed().length() < 50) {
            OcrExtractor ocr;
            if (ocr.isAvailable()) {
                text = ocr.extractFromPdf(filePath, /*maxPages=*/5);
            }
        }
        return text;
    }

    if (ext == QStringLiteral("docx") || ext == QStringLiteral("docm")) {
        return extractTextZipXml(filePath,
            {QStringLiteral("word/document.xml")}, maxChars);
    }

    if (ext == QStringLiteral("xlsx") || ext == QStringLiteral("xlsm")) {
        return extractTextZipXml(filePath,
            {QStringLiteral("xl/sharedStrings.xml"),
             QStringLiteral("xl/worksheets/sheet1.xml")}, maxChars);
    }

    if (ext == QStringLiteral("pptx") || ext == QStringLiteral("pptm")) {
        // Слайды ppt/slides/slide1.xml, slide2.xml...
        QStringList entries;
        for (int i = 1; i <= 50; ++i) {
            entries.append(QStringLiteral("ppt/slides/slide")
                           + QString::number(i) + QStringLiteral(".xml"));
        }
        return extractTextZipXml(filePath, entries, maxChars);
    }

    if (ext == QStringLiteral("odt") || ext == QStringLiteral("ods")) {
        return extractTextZipXml(filePath,
            {QStringLiteral("content.xml")}, maxChars);
    }

    // Всё остальное — читаем как текст
    return extractTextPlain(filePath, maxChars);
}

// ============================================================
// extractTextPlain — любой текстовый файл
// ============================================================

QString ContentSearch::extractTextPlain(const QString& filePath, int maxChars)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};

    // Пробуем UTF-8, потом Latin-1 как fallback
    QByteArray raw = file.read(maxChars);
    file.close();

    QString text = QString::fromUtf8(raw);

    // Если много кракозябр (нулей / управляющих символов) — бинарник, пропускаем
    int nonPrintable = 0;
    for (int i = 0; i < qMin(1000, text.size()); ++i) {
        if (text[i] < QChar(32) && text[i] != QChar('\n')
                                && text[i] != QChar('\r')
                                && text[i] != QChar('\t')) {
            ++nonPrintable;
        }
    }
    if (nonPrintable > 50) return {};  // явно бинарник

    return text;
}

// ============================================================
// extractTextPdf — байтовый поиск текста в PDF
//
// PDF хранит текст в потоках как "(Hello World) Tj"
// Мы извлекаем строки между скобками из BT...ET блоков.
// Это работает для большинства обычных PDF без шифрования.
// ============================================================

QString ContentSearch::extractTextPdf(const QString& filePath, int maxChars)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};

    const QByteArray data = file.read(qMin(static_cast<qint64>(maxChars * 3),
                                           file.size()));
    file.close();

    QString result;
    result.reserve(maxChars / 2);

    // Паттерн 1: (text) Tj  или [(text)] TJ
    // Паттерн 2: /T* (text)
    int i = 0;
    while (i < data.size() - 3 && result.size() < maxChars) {
        if (data[i] == '(') {
            // Ищем закрывающую скобку (с учётом экранирования)
            int j = i + 1;
            QByteArray fragment;
            bool escaped = false;
            while (j < data.size()) {
                const char c = data[j];
                if (escaped) {
                    escaped = false;
                    if (c == 'n')       fragment += '\n';
                    else if (c == 'r')  fragment += '\r';
                    else if (c == 't')  fragment += '\t';
                    else                fragment += c;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == ')') {
                    break;
                } else {
                    fragment += c;
                }
                ++j;
            }

            // Проверяем что после ) идёт Tj или TJ (с пробелами)
            int k = j + 1;
            while (k < data.size() && (data[k] == ' ' || data[k] == '\n'
                                       || data[k] == '\r')) ++k;

            bool isTj = (k + 1 < data.size()
                         && data[k] == 'T'
                         && (data[k+1] == 'j' || data[k+1] == 'J'));

            if (isTj && fragment.size() > 1) {
                // PDF может использовать Latin-1 или PDFDocEncoding
                const QString word = QString::fromLatin1(fragment).trimmed();
                if (!word.isEmpty()) {
                    result += word + QChar(' ');
                }
            }
            i = j + 1;
        } else {
            ++i;
        }
    }

    return result.trimmed();
}

// ============================================================
// extractTextZipXml — DOCX / XLSX / PPTX (ZIP + XML)
// ============================================================

QString ContentSearch::extractTextZipXml(const QString& filePath,
                                          const QStringList& xmlEntries,
                                          int maxChars)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray zipData = file.readAll();
    file.close();

    if (zipData.isEmpty()) return {};

    QString result;
    result.reserve(maxChars / 2);

    // Определяем какие именно entries искать
    QStringList entriesToRead = xmlEntries;

    // Для PPTX — сканируем список файлов чтобы найти все slide*.xml
    if (xmlEntries.size() > 1 && xmlEntries.first().contains(QStringLiteral("slides/slide"))) {
        const QStringList allEntries = ZipUtil::listEntries(zipData);
        entriesToRead.clear();
        for (const QString& e : allEntries) {
            if (e.startsWith(QStringLiteral("ppt/slides/slide"))
                && e.endsWith(QStringLiteral(".xml"))) {
                entriesToRead.append(e);
            }
        }
    }

    for (const QString& entry : entriesToRead) {
        if (result.size() >= maxChars) break;

        const QByteArray xmlData = ZipUtil::extractEntry(zipData, entry);
        if (xmlData.isEmpty()) continue;

        QString xml = QString::fromUtf8(xmlData);

        // Убираем XML теги — между тегами остаётся только текст
        static const QRegularExpression tagRe(QStringLiteral("<[^>]+>"));
        xml.replace(tagRe, QStringLiteral(" "));

        // Декодируем XML entities
        xml.replace(QStringLiteral("&amp;"),  QStringLiteral("&"));
        xml.replace(QStringLiteral("&lt;"),   QStringLiteral("<"));
        xml.replace(QStringLiteral("&gt;"),   QStringLiteral(">"));
        xml.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
        xml.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
        xml.replace(QStringLiteral("&#xA;"),  QStringLiteral("\n"));
        xml.replace(QStringLiteral("&#x9;"),  QStringLiteral("\t"));

        xml = xml.simplified();
        result += xml + QChar('\n');

        if (result.size() > maxChars) {
            result.truncate(maxChars);
            break;
        }
    }

    return result.trimmed();
}

// ============================================================
// findInText — ищем ключевые слова в тексте
// ============================================================

QVector<ContentHit> ContentSearch::findInText(const QString& text,
                                               const QString& filePath,
                                               const QStringList& keywords,
                                               int maxHits)
{
    QVector<ContentHit> hits;
    if (text.isEmpty() || keywords.isEmpty()) return hits;

    const QString textLower = text.toLower();
    const QStringList lines = text.split(QChar('\n'));
    const QString fileName  = QFileInfo(filePath).fileName();

    // Считаем сколько раз каждое ключевое слово встречается
    for (const QString& kw : keywords) {
        if (hits.size() >= maxHits) break;
        const QString kwLower = kw.toLower();
        if (kwLower.length() < 2) continue;

        // Ищем первое вхождение для превью
        int pos = textLower.indexOf(kwLower);
        if (pos == -1) continue;

        // Считаем все вхождения для релевантности
        int count = 0;
        int searchFrom = 0;
        while ((searchFrom = textLower.indexOf(kwLower, searchFrom)) != -1) {
            ++count;
            searchFrom += kwLower.length();
        }

        // Формируем фрагмент с контекстом
        const int start = qMax(0, pos - kContextChars / 2);
        const int end   = qMin(text.size(), pos + kContextChars / 2);
        QString snippet = text.mid(start, end - start).simplified();

        // Убираем перенос строк из снипета
        snippet.replace(QChar('\n'), QChar(' '));
        snippet.replace(QChar('\r'), QChar(' '));

        // Добавляем "..." если обрезали
        if (start > 0)           snippet.prepend(QStringLiteral("..."));
        if (end < text.size())   snippet.append(QStringLiteral("..."));

        // Ищем номер строки
        int lineNo = 0;
        if (lines.size() < 50000) {  // только для не очень больших файлов
            int charCount = 0;
            for (int i = 0; i < lines.size(); ++i) {
                charCount += lines[i].size() + 1;
                if (charCount >= pos) { lineNo = i + 1; break; }
            }
        }

        ContentHit hit;
        hit.filePath    = filePath;
        hit.fileName    = fileName;
        hit.matchedLine = snippet;
        hit.matchedWord = kw;
        hit.lineNumber  = lineNo;
        // Релевантность: частота + бонус за точное совпадение целого слова
        hit.relevance   = qMin(100, 40 + count * 5);
        hits.append(hit);
    }

    return hits;
}

// ============================================================
// search — главный метод
// ============================================================

QVector<ContentHit> ContentSearch::search(const QStringList& filePaths,
                                            const QStringList& keywords,
                                            int maxResults) const
{
    QVector<ContentHit> allHits;

    // Expand with any synonyms Jarvis has learned ("отчёт" also matches
    // files mentioning "репорт" if the user taught that mapping before) —
    // pure local dictionary lookup, no model/LLM call.
    const QStringList expandedKeywords = SynonymLearner::instance().expandAll(keywords);

    for (const QString& path : filePaths) {
        if (allHits.size() >= maxResults) break;
        if (!isSupportedFormat(path)) continue;

        // Ограничиваем размер файла — не читаем огромные файлы
        const qint64 fileSize = QFileInfo(path).size();
        if (fileSize > 50 * 1024 * 1024) continue;  // > 50 MB — пропускаем
        if (fileSize == 0) continue;

        const QString text = extractText(path, 200000);
        if (text.isEmpty()) continue;

        const auto hits = findInText(text, path, expandedKeywords,
                                      maxResults - allHits.size());
        allHits.append(hits);
    }

    // Сортируем по релевантности
    std::sort(allHits.begin(), allHits.end(),
              [](const ContentHit& a, const ContentHit& b) {
                  return a.relevance > b.relevance;
              });

    return allHits;
}