#pragma once
// -------------------------------------------------------
// content_search.h — Поиск по содержимому файлов
//
// Поддерживаемые форматы:
//   TXT / MD / LOG / CSV / любой текст  — QFile напрямую
//   CPP / H / PY / JS / и др. исходники — QFile напрямую
//   PDF   — байтовый поиск потоков текста (без libpdf)
//   DOCX / DOCM                          — ZIP → word/document.xml
//   XLSX / XLSM                          — ZIP → xl/sharedStrings.xml
//   PPTX                                 — ZIP → ppt/slides/slide*.xml
//
// Использование:
//   ContentSearch cs;
//   auto results = cs.search(filePaths, {"contract", "договор"}, 5);
// -------------------------------------------------------

#include <QString>
#include <QStringList>
#include <QVector>

// Один результат поиска по содержимому
struct ContentHit
{
    QString filePath;
    QString fileName;
    QString matchedLine;   // строка/фрагмент где нашли
    QString matchedWord;   // какое именно ключевое слово совпало
    int     lineNumber = 0;
    int     relevance  = 0; // 0–100

    QString format() const;
};

class ContentSearch
{
public:
    ContentSearch() = default;

    // Ищет ключевые слова в списке файлов.
    // Возвращает до maxResults совпадений, отсортированных по релевантности.
    QVector<ContentHit> search(const QStringList& filePaths,
                                const QStringList& keywords,
                                int maxResults = 5) const;

    // Проверяет один файл — можно ли искать по содержимому
    static bool isSupportedFormat(const QString& filePath);

    // Извлекает текст из файла (до maxChars символов)
    static QString extractText(const QString& filePath, int maxChars = 200000);

private:
    static QString extractTextPlain(const QString& filePath, int maxChars);
    static QString extractTextPdf(const QString& filePath, int maxChars);
    static QString extractTextZipXml(const QString& filePath,
                                      const QStringList& xmlEntries,
                                      int maxChars);

    // Ищет совпадения в тексте, возвращает фрагменты с контекстом
    static QVector<ContentHit> findInText(const QString& text,
                                           const QString& filePath,
                                           const QStringList& keywords,
                                           int maxHits);

    static constexpr int kContextChars = 120;  // символов вокруг совпадения
};
