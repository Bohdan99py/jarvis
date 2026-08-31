// -------------------------------------------------------
// ocr_extractor.cpp — Извлечение текста из PDF через OCR
// -------------------------------------------------------

#include "ocr_extractor.h"

#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QCoreApplication>
#include <QStandardPaths>

// ============================================================
// Пути к инструментам
// ============================================================

static QString findTool(const QStringList& candidates)
{
    for (const QString& p : candidates) {
        if (QFileInfo::exists(p)) return p;
    }
    return {};
}

QString OcrExtractor::pdftoppmPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return findTool({
        // После деплоя — pdftoppm.exe скопирован прямо в bin/
        appDir + QStringLiteral("/pdftoppm.exe"),
        // При разработке — из redist (поддерживаем разные версии)
        appDir + QStringLiteral("/redist/poppler-26.02.0/Library/bin/pdftoppm.exe"),
        appDir + QStringLiteral("/../redist/poppler-26.02.0/Library/bin/pdftoppm.exe"),
        appDir + QStringLiteral("/redist/poppler/bin/pdftoppm.exe"),
        appDir + QStringLiteral("/../redist/poppler/bin/pdftoppm.exe"),
        QStringLiteral("pdftoppm.exe"),
    });
}

QString OcrExtractor::tesseractPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return findTool({
        // После деплоя — копируется в bin/Tesseract-OCR/
        appDir + QStringLiteral("/Tesseract-OCR/tesseract.exe"),
        // Из redist при разработке
        appDir + QStringLiteral("/redist/Tesseract-OCR/tesseract.exe"),
        appDir + QStringLiteral("/../redist/Tesseract-OCR/tesseract.exe"),
        appDir + QStringLiteral("/redist/tesseract/tesseract.exe"),
        appDir + QStringLiteral("/../redist/tesseract/tesseract.exe"),
        QStringLiteral("C:/Program Files/Tesseract-OCR/tesseract.exe"),
        QStringLiteral("tesseract.exe"),
    });
}

QString OcrExtractor::tessdataPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        // После деплоя
        appDir + QStringLiteral("/Tesseract-OCR/tessdata"),
        // При разработке
        appDir + QStringLiteral("/redist/Tesseract-OCR/tessdata"),
        appDir + QStringLiteral("/../redist/Tesseract-OCR/tessdata"),
        appDir + QStringLiteral("/redist/tesseract/tessdata"),
        appDir + QStringLiteral("/../redist/tesseract/tessdata"),
        QStringLiteral("C:/Program Files/Tesseract-OCR/tessdata"),
    };
    for (const QString& p : candidates) {
        if (QDir(p).exists()) return p;
    }
    return {};
}

// ============================================================
// Проверка доступности
// ============================================================

bool OcrExtractor::isPopplerAvailable() const
{
    return !pdftoppmPath().isEmpty();
}

bool OcrExtractor::isTesseractAvailable() const
{
    return !tesseractPath().isEmpty() && !tessdataPath().isEmpty();
}

// ============================================================
// Кэш
// ============================================================

QString OcrExtractor::cachePath(const QString& pdfPath)
{
    return pdfPath + QStringLiteral(".jarvis_ocr_cache.txt");
}

QString OcrExtractor::readCache(const QString& pdfPath) const
{
    const QString cp = cachePath(pdfPath);
    if (!QFileInfo::exists(cp)) return {};

    // Кэш устарел если PDF новее
    const QDateTime pdfModified  = QFileInfo(pdfPath).lastModified();
    const QDateTime cacheModified = QFileInfo(cp).lastModified();
    if (pdfModified > cacheModified) return {};

    QFile f(cp);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QTextStream(&f).readAll();
}

void OcrExtractor::writeCache(const QString& pdfPath, const QString& text) const
{
    if (text.isEmpty()) return;
    QFile f(cachePath(pdfPath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream(&f) << text;
}

// ============================================================
// extractFromPdf — главный метод
// ============================================================

QString OcrExtractor::extractFromPdf(const QString& pdfPath, int maxPages) const
{
    if (!QFileInfo::exists(pdfPath)) return {};

    // 1. Проверяем кэш
    const QString cached = readCache(pdfPath);
    if (!cached.isEmpty()) return cached;

    // 2. Проверяем инструменты
    const QString pdftoppm  = pdftoppmPath();
    const QString tesseract = tesseractPath();
    const QString tessdata  = tessdataPath();

    if (pdftoppm.isEmpty() || tesseract.isEmpty() || tessdata.isEmpty()) {
        return {};
    }

    // 3. Создаём временную папку для PNG страниц
    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) return {};

    const QString prefix = tmpDir.filePath(QStringLiteral("page"));

    // 4. Конвертируем PDF → PNG через pdftoppm
    {
        QProcess proc;
        QStringList args = {
            QStringLiteral("-png"),
            QStringLiteral("-r"), QStringLiteral("200"),  // 200 DPI — баланс качество/скорость
        };

        if (maxPages > 0) {
            args << QStringLiteral("-l") << QString::number(maxPages);
        }

        args << pdfPath << prefix;

        proc.start(pdftoppm, args);
        if (!proc.waitForFinished(kPdfTimeoutMs)) {
            proc.kill();
            return {};
        }

        if (proc.exitCode() != 0) return {};
    }

    // 5. Ищем сгенерированные PNG файлы
    QDir tmpDirObj(tmpDir.path());
    const QStringList pngFiles = tmpDirObj.entryList(
        {QStringLiteral("*.png")}, QDir::Files, QDir::Name);

    if (pngFiles.isEmpty()) return {};

    // 6. OCR каждой страницы
    QString fullText;
    int pageCount = 0;

    for (const QString& pngName : pngFiles) {
        if (maxPages > 0 && pageCount >= maxPages) break;

        const QString pngPath = tmpDir.filePath(pngName);
        const QString pageText = ocrImage(pngPath, buildLanguageString(tessdata));

        if (!pageText.isEmpty()) {
            fullText += pageText + QStringLiteral("\n\n");
        }
        ++pageCount;
    }

    fullText = fullText.trimmed();

    // 7. Сохраняем в кэш
    if (!fullText.isEmpty()) {
        writeCache(pdfPath, fullText);
    }

    return fullText;
}

// ============================================================
// ocrImage — OCR одного изображения
// ============================================================

QString OcrExtractor::ocrImage(const QString& imagePath,
                                const QString& languages) const
{
    const QString tesseract = tesseractPath();
    const QString tessdata  = tessdataPath();
    if (tesseract.isEmpty() || tessdata.isEmpty()) return {};

    // Tesseract пишет результат в файл output.txt
    // Используем stdout через "-" как выходной файл
    QProcess proc;

    // Устанавливаем TESSDATA_PREFIX через environment
    QStringList env = QProcess::systemEnvironment();
    env.append(QStringLiteral("TESSDATA_PREFIX=") + tessdata);
    proc.setEnvironment(env);

    proc.start(tesseract, {
        imagePath,
        QStringLiteral("stdout"),   // вывод в stdout
        QStringLiteral("-l"), languages,
        QStringLiteral("--oem"), QStringLiteral("3"),   // LSTM + legacy
        QStringLiteral("--psm"), QStringLiteral("3"),   // автоопределение страницы
    });

    if (!proc.waitForFinished(kOcrTimeoutMs)) {
        proc.kill();
        return {};
    }

    const QString text = QString::fromUtf8(
        proc.readAllStandardOutput()).trimmed();

    return text;
}

// ============================================================
// buildLanguageString — определяем доступные языки
// ============================================================

QString OcrExtractor::buildLanguageString(const QString& tessdataDir)
{
    // Проверяем какие traineddata файлы реально есть
    // чтобы не падать если пользователь скачал не все языки
    QStringList available;

    // Языки, которые реально нужны владельцу. Каждый добавленный сюда
    // язык — это не только вес в установщике, но и время распознавания:
    // Tesseract прогоняет страницу по всем перечисленным моделям, поэтому
    // «на всякий случай» здесь стоит дорого.
    static const QStringList wanted = {
        QStringLiteral("rus"),
        QStringLiteral("eng"),
        QStringLiteral("fra"),
        QStringLiteral("ron"),  // румынский
    };

    for (const QString& lang : wanted) {
        const QString trainedDataPath = tessdataDir + QChar('/')
                                      + lang + QStringLiteral(".traineddata");
        if (QFileInfo::exists(trainedDataPath)) {
            available.append(lang);
        }
    }

    if (available.isEmpty()) {
        return QStringLiteral("eng");  // хотя бы английский
    }

    return available.join(QChar('+'));
}