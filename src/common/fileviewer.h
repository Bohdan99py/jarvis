#pragma once
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QScrollArea>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QPixmap>
#include <QImageReader>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QCoreApplication>
#include <QDir>
#include <QListWidget>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolBar>
#include <windows.h>
#include <shellapi.h>

// Окно просмотра найденных файлов.
// Показывает список файлов слева, превью справа.
// Кнопки: Открыть (дефолтная программа), Открыть в LibreOffice,
//          Открыть в CLion/Rider (для кода), Показать в проводнике.
class FileViewer : public QDialog {
public:
    explicit FileViewer(const QStringList &filePaths, QWidget *parent = nullptr)
        : QDialog(parent, Qt::Window)
    {
        setWindowTitle("J.A.R.V.I.S. — Найденные файлы");
        setMinimumSize(900, 600);
        setStyleSheet(R"(
            QDialog, QWidget { background: #0d1117; color: #c9d1d9; }
            QListWidget {
                background: #161b22; border: 1px solid #21262d;
                border-radius: 6px; font-size: 13px;
            }
            QListWidget::item { padding: 8px 12px; border-bottom: 1px solid #21262d; }
            QListWidget::item:selected { background: #1f6feb; color: white; }
            QListWidget::item:hover { background: #21262d; }
            QTextEdit {
                background: #0d1117; border: 1px solid #21262d; border-radius: 6px;
                font-family: 'Consolas', monospace; font-size: 13px; color: #c9d1d9;
            }
            QPushButton {
                background: #21262d; border: 1px solid #30363d; border-radius: 6px;
                padding: 8px 16px; color: #c9d1d9; font-size: 13px; min-width: 120px;
            }
            QPushButton:hover { background: #1f6feb; border-color: #1f6feb; color: white; }
            QPushButton:pressed { background: #388bfd; }
            QPushButton#btnOpen { background: #1f6feb; color: white; border-color: #1f6feb; }
            QPushButton#btnOpen:hover { background: #388bfd; }
            QLabel#lblPath {
                color: #8b949e; font-size: 11px; padding: 4px 8px;
                background: #161b22; border-radius: 4px;
            }
            QLabel#lblName { color: #58a6ff; font-size: 15px; font-weight: bold; padding: 4px; }
            QSplitter::handle { background: #21262d; width: 1px; }
        )");

        auto *mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(12, 12, 12, 12);
        mainLayout->setSpacing(8);

        // Заголовок
        auto *header = new QLabel(
            QString("📂 Найдено файлов: <b>%1</b>").arg(filePaths.size()), this);
        header->setStyleSheet("color: #58a6ff; font-size: 14px; padding: 4px;");
        mainLayout->addWidget(header);

        // Сплиттер список | превью
        auto *splitter = new QSplitter(Qt::Horizontal, this);
        splitter->setChildrenCollapsible(false);

        // ── Левая панель — список файлов ──
        m_list = new QListWidget(this);
        m_list->setMinimumWidth(280);
        m_list->setMaximumWidth(380);
        for (const QString &path : filePaths) {
            QFileInfo fi(path);
            auto *item = new QListWidgetItem(fileIcon(fi) + "  " + fi.fileName());
            item->setData(Qt::UserRole, path);
            item->setToolTip(path);
            m_list->addItem(item);
        }
        splitter->addWidget(m_list);

        // ── Правая панель — превью + кнопки ──
        auto *rightWidget = new QWidget(this);
        auto *rightLayout = new QVBoxLayout(rightWidget);
        rightLayout->setContentsMargins(8, 0, 0, 0);
        rightLayout->setSpacing(8);

        // Имя файла и путь
        m_lblName = new QLabel("Выберите файл", this);
        m_lblName->setObjectName("lblName");
        m_lblPath = new QLabel("", this);
        m_lblPath->setObjectName("lblPath");
        m_lblPath->setWordWrap(true);
        rightLayout->addWidget(m_lblName);
        rightLayout->addWidget(m_lblPath);

        // Стек: текст или изображение
        m_stack = new QStackedWidget(this);

        m_textPreview = new QTextEdit(this);
        m_textPreview->setReadOnly(true);
        m_stack->addWidget(m_textPreview); // index 0

        m_imageLabel = new QLabel(this);
        m_imageLabel->setAlignment(Qt::AlignCenter);
        m_imageLabel->setStyleSheet("background: #161b22; border: 1px solid #21262d; border-radius:6px;");
        auto *scrollImg = new QScrollArea(this);
        scrollImg->setWidget(m_imageLabel);
        scrollImg->setWidgetResizable(true);
        m_stack->addWidget(scrollImg); // index 1

        rightLayout->addWidget(m_stack, 1);

        // Кнопки действий
        auto *btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(8);

        m_btnOpen = new QPushButton("🚀 Открыть", this);
        m_btnOpen->setObjectName("btnOpen");
        m_btnOpen->setToolTip("Открыть в программе по умолчанию");
        connect(m_btnOpen, &QPushButton::clicked, this, [this]() { openDefault(); });

        m_btnOffice = new QPushButton("📄 LibreOffice", this);
        m_btnOffice->setToolTip("Открыть в LibreOffice");
        connect(m_btnOffice, &QPushButton::clicked, this, [this]() { openLibreOffice(); });

        m_btnIDE = new QPushButton("💻 IDE", this);
        m_btnIDE->setToolTip("Открыть в CLion или Rider");
        connect(m_btnIDE, &QPushButton::clicked, this, [this]() { openIDE(); });

        m_btnExplorer = new QPushButton("📁 Проводник", this);
        m_btnExplorer->setToolTip("Показать в проводнике");
        connect(m_btnExplorer, &QPushButton::clicked, this, [this]() { showInExplorer(); });

        btnLayout->addWidget(m_btnOpen);
        btnLayout->addWidget(m_btnOffice);
        btnLayout->addWidget(m_btnIDE);
        btnLayout->addWidget(m_btnExplorer);
        btnLayout->addStretch();
        rightLayout->addLayout(btnLayout);

        splitter->addWidget(rightWidget);
        splitter->setSizes({300, 600});
        mainLayout->addWidget(splitter, 1);

        connect(m_list, &QListWidget::currentItemChanged,
                this, [this](QListWidgetItem* cur, QListWidgetItem* prev) {
                    onFileSelected(cur, prev);
                });

        // Выбрать первый файл и явно загрузить превью
        if (m_list->count() > 0) {
            m_list->setCurrentRow(0);
            // setCurrentRow может не сработать если предыдущего выбора не было —
            // вызываем onFileSelected явно для гарантии
            onFileSelected(m_list->item(0), nullptr);
        } else {
            updateButtons(false);
        }
    }

    // Статический хелпер: открыть список файлов в новом окне
    static void showFiles(const QStringList &paths, QWidget *parent = nullptr) {
        if (paths.isEmpty()) return;
        auto *v = new FileViewer(paths, parent);
        v->setAttribute(Qt::WA_DeleteOnClose);
        v->show();
    }

private:
    void onFileSelected(QListWidgetItem *current, QListWidgetItem *) {
        if (!current) return;
        m_currentPath = current->data(Qt::UserRole).toString();
        QFileInfo fi(m_currentPath);

        m_lblName->setText(fi.fileName());
        m_lblPath->setText(m_currentPath);
        loadPreview(fi);
        updateButtons(true);
    }

    void openDefault() {
        if (m_currentPath.isEmpty()) return;
        ShellExecuteW(nullptr, L"open",
                      reinterpret_cast<LPCWSTR>(m_currentPath.utf16()),
                      nullptr, nullptr, SW_SHOWNORMAL);
    }

    void openLibreOffice() {
        if (m_currentPath.isEmpty()) return;
        QStringList paths = {
            "C:/Program Files/LibreOffice/program/soffice.exe",
            "C:/Program Files (x86)/LibreOffice/program/soffice.exe",
        };
        for (const QString &p : paths) {
            if (QFileInfo::exists(p)) {
                QProcess::startDetached(p, {m_currentPath});
                return;
            }
        }
        // Fallback — открыть по умолчанию
        openDefault();
    }

    void openIDE() {
        if (m_currentPath.isEmpty()) return;
        // CLion для C++, Rider для C#/UE5-проектов
        QString ext = QFileInfo(m_currentPath).suffix().toLower();
        QStringList idePaths;
        if (ext == "cs" || ext == "uproject") {
            idePaths = {
                qEnvironmentVariable("LOCALAPPDATA") + "/Programs/Rider/bin/rider64.exe",
                "C:/Program Files/JetBrains/Rider 2026.1/bin/rider64.exe",
            };
        } else {
            idePaths = {
                qEnvironmentVariable("LOCALAPPDATA") + "/Programs/CLion/bin/clion64.exe",
                "C:/Program Files/JetBrains/CLion 2026.1/bin/clion64.exe",
            };
        }
        for (const QString &p : idePaths) {
            if (QFileInfo::exists(p)) {
                QProcess::startDetached(p, {m_currentPath});
                return;
            }
        }
        // Fallback — VSCode
        QString vscode = qEnvironmentVariable("LOCALAPPDATA") + "/Programs/Microsoft VS Code/Code.exe";
        if (QFileInfo::exists(vscode)) {
            QProcess::startDetached(vscode, {m_currentPath});
            return;
        }
        openDefault();
    }

    void showInExplorer() {
        if (m_currentPath.isEmpty()) return;
        // /select — выделить файл в проводнике
        QProcess::startDetached("explorer.exe",
            {"/select,", QDir::toNativeSeparators(m_currentPath)});
    }

private:
    QListWidget    *m_list        = nullptr;
    QStackedWidget *m_stack       = nullptr;
    QTextEdit      *m_textPreview = nullptr;
    QLabel         *m_imageLabel  = nullptr;
    QLabel         *m_lblName     = nullptr;
    QLabel         *m_lblPath     = nullptr;
    QPushButton    *m_btnOpen     = nullptr;
    QPushButton    *m_btnOffice   = nullptr;
    QPushButton    *m_btnIDE      = nullptr;
    QPushButton    *m_btnExplorer = nullptr;
    QString         m_currentPath;

    void loadPreview(const QFileInfo &fi) {
        QString ext = fi.suffix().toLower();

        // Изображения
        static const QSet<QString> imgExts = {"png","jpg","jpeg","bmp","gif","webp","svg","ico","tiff"};
        if (imgExts.contains(ext)) {
            QPixmap px(fi.filePath());
            if (!px.isNull()) {
                m_imageLabel->setPixmap(
                    px.scaled(m_stack->width() - 20, m_stack->height() - 20,
                              Qt::KeepAspectRatio, Qt::SmoothTransformation));
                m_stack->setCurrentIndex(1);
                return;
            }
        }

        // PDF — извлекаем текст через pdftotext (Poppler, уже есть в redist)
        if (ext == "pdf") {
            QString text = extractPdfText(fi.filePath());
            if (!text.isEmpty()) {
                m_textPreview->setFont(QFont("Segoe UI", 11));
                m_textPreview->setPlainText(text);
            } else {
                // pdftotext недоступен или PDF пустой — показываем метаданные
                m_textPreview->setPlainText(
                    QString("📄 %1\n\nРазмер: %2\nПуть: %3\n\n"
                            "Текст не удалось извлечь. Нажмите «Открыть» для просмотра.")
                        .arg(fi.fileName())
                        .arg(formatSize(fi.size()))
                        .arg(fi.filePath()));
            }
            m_stack->setCurrentIndex(0);
            return;
        }

        // Текстовые файлы — только явные расширения, без size-хака
        static const QSet<QString> textExts = {
            "txt","md","log","cpp","h","hpp","c","cs","py","js","ts",
            "json","xml","yaml","yml","ini","cfg","bat","sh","cmake",
            "uproject","uplugin","html","css","rtf"
        };
        if (textExts.contains(ext)) {
            QFile f(fi.filePath());
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream ts(&f);
                ts.setAutoDetectUnicode(true);
                QString text = ts.read(4000);
                if (fi.size() > 4000)
                    text += "\n\n... [файл обрезан для превью] ...";
                m_textPreview->setPlainText(text);
                if (QSet<QString>{"cpp","h","hpp","c","cs","py","js","ts"}.contains(ext))
                    m_textPreview->setFont(QFont("Consolas", 12));
                else
                    m_textPreview->setFont(QFont("Segoe UI", 11));
                m_stack->setCurrentIndex(0);
                return;
            }
        }

        // Всё остальное
        m_textPreview->setPlainText(
            QString("Файл: %1\nТип: %2\nРазмер: %3\nИзменён: %4\n\n"
                    "Превью недоступно. Нажмите «Открыть» для просмотра.")
                .arg(fi.fileName())
                .arg(fi.suffix().toUpper())
                .arg(formatSize(fi.size()))
                .arg(fi.lastModified().toString("dd.MM.yyyy HH:mm")));
        m_stack->setCurrentIndex(0);
    }

    void updateButtons(bool hasFile) {
        m_btnOpen->setEnabled(hasFile);
        m_btnExplorer->setEnabled(hasFile);

        if (!hasFile) {
            m_btnOffice->setEnabled(false);
            m_btnIDE->setEnabled(false);
            return;
        }

        QFileInfo fi(m_currentPath);
        QString ext = fi.suffix().toLower();

        // LibreOffice — для офисных документов
        static const QSet<QString> officeExts = {
            "doc","docx","odt","xls","xlsx","ods","ppt","pptx","odp","pdf","txt","rtf"
        };
        m_btnOffice->setEnabled(officeExts.contains(ext));

        // IDE — для кода
        static const QSet<QString> codeExts = {
            "cpp","h","hpp","c","cs","py","js","ts","json","cmake","uproject","uplugin","md"
        };
        m_btnIDE->setEnabled(codeExts.contains(ext));
    }

    static QString fileIcon(const QFileInfo &fi) {
        QString ext = fi.suffix().toLower();
        if (QSet<QString>{"png","jpg","jpeg","bmp","gif","webp"}.contains(ext)) return "🖼️";
        if (QSet<QString>{"pdf"}.contains(ext))                                 return "📕";
        if (QSet<QString>{"doc","docx","odt","rtf"}.contains(ext))             return "📝";
        if (QSet<QString>{"xls","xlsx","ods","csv"}.contains(ext))             return "📊";
        if (QSet<QString>{"ppt","pptx","odp"}.contains(ext))                   return "📊";
        if (QSet<QString>{"cpp","h","hpp","c","cs","py","js","ts"}.contains(ext)) return "💻";
        if (QSet<QString>{"mp4","avi","mkv","mov"}.contains(ext))              return "🎬";
        if (QSet<QString>{"mp3","wav","flac","ogg"}.contains(ext))             return "🎵";
        if (QSet<QString>{"zip","rar","7z","tar"}.contains(ext))               return "📦";
        return "📄";
    }

    static QString formatSize(qint64 bytes) {
        if (bytes < 1024)        return QString("%1 B").arg(bytes);
        if (bytes < 1024*1024)   return QString("%1 KB").arg(bytes/1024);
        if (bytes < 1024*1024*1024) return QString("%1 MB").arg(bytes/(1024*1024));
        return QString("%1 GB").arg(bytes/(1024*1024*1024));
    }

    // Извлекает текст из PDF через pdftotext (Poppler).
    // pdftotext.exe лежит рядом с jarvis.exe после windeployqt.
    static QString extractPdfText(const QString& pdfPath) {
        // Ищем pdftotext рядом с exe
        QString exeDir = QCoreApplication::applicationDirPath();
        QString pdftotext = exeDir + "/pdftotext.exe";
        if (!QFileInfo::exists(pdftotext)) {
            // Попробуем в PATH
            pdftotext = "pdftotext";
        }

        // Временный файл для вывода
        QString tmpOut = QDir::tempPath() + "/jarvis_pdf_preview.txt";
        QFile::remove(tmpOut);

        QProcess proc;
        proc.start(pdftotext, {
            "-enc", "UTF-8",
            "-l", "3",          // первые 3 страницы достаточно для превью
            pdfPath,
            tmpOut
        });

        if (!proc.waitForFinished(8000)) {
            proc.kill();
            return {};
        }

        if (proc.exitCode() != 0) return {};

        QFile f(tmpOut);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        QString text = ts.readAll().trimmed();
        f.close();
        QFile::remove(tmpOut);

        if (text.isEmpty()) return {};

        // Обрезаем до разумного размера для превью
        if (text.size() > 5000) {
            text = text.left(5000) + "\n\n... [показаны первые 3 страницы] ...";
        }
        return text;
    }
};