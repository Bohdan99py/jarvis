#pragma once
// -------------------------------------------------------
// ocr_extractor.h — Извлечение текста из PDF через OCR
//
// Схема:
//   PDF → pdftoppm.exe (Poppler) → PNG страниц
//       → tesseract.exe → текст
//
// Языки: RU + EN + FR одновременно (rus+eng+fra)
//
// Кэш: результат OCR сохраняется рядом с PDF как
//   filename.pdf.jarvis_ocr_cache.txt
// Повторный запрос не запускает OCR заново.
//
// Зависимости: redist/poppler/bin/pdftoppm.exe
//              redist/tesseract/tesseract.exe
//              redist/tesseract/tessdata/{rus,eng,fra}.traineddata
// -------------------------------------------------------

#include <QString>
#include <QStringList>

class OcrExtractor
{
public:
    OcrExtractor() = default;

    // Извлекает текст из PDF файла.
    // Автоматически использует кэш если он свежее PDF.
    // maxPages: сколько страниц обрабатывать (0 = все, но медленно)
    QString extractFromPdf(const QString& pdfPath, int maxPages = 5) const;

    // Запускает OCR на одном изображении
    QString ocrImage(const QString& imagePath,
                     const QString& languages = QStringLiteral("rus+eng+fra")) const;

    // Проверяет доступность инструментов
    bool isPopplerAvailable() const;
    bool isTesseractAvailable() const;
    bool isAvailable() const { return isPopplerAvailable() && isTesseractAvailable(); }

    // Пути к исполняемым файлам (определяются автоматически)
    static QString pdftoppmPath();
    static QString tesseractPath();
    static QString tessdataPath();
    static QString buildLanguageString(const QString& tessdataDir);

private:
    QString readCache(const QString& pdfPath) const;
    void    writeCache(const QString& pdfPath, const QString& text) const;
    static QString cachePath(const QString& pdfPath);

    static constexpr int kOcrTimeoutMs   = 30000;  // 30 сек на страницу
    static constexpr int kPdfTimeoutMs   = 10000;  // 10 сек на конвертацию
};
