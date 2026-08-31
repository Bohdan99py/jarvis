// ============================================================
// artifacts_dialog.cpp — «Файлы от Джарвиса»
// ============================================================

#include "artifacts_dialog.h"
#include "jarvis_theme.h"
#include "lang.h"

#include <QListWidget>
#include <QLabel>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QResizeEvent>
#include <QLocale>

namespace {

QString css(const QColor& c) { return JarvisTheme::css(c); }

// Значок вида — беглый ориентир в списке; строка kind приходит из
// ArtifactRegistry и специально не enum (см. комментарий там).
QString iconFor(const QString& kind)
{
    if (kind == QLatin1String(ArtifactRegistry::kSchematic))  return QStringLiteral("🔌");
    if (kind == QLatin1String(ArtifactRegistry::kDiagram))    return QStringLiteral("📊");
    if (kind == QLatin1String(ArtifactRegistry::kScreenshot)) return QStringLiteral("🖼");
    if (kind == QLatin1String(ArtifactRegistry::kExport))     return QStringLiteral("📤");
    if (kind == QLatin1String(ArtifactRegistry::kPhoto))      return QStringLiteral("📷");
    return QStringLiteral("📄");
}

// Текстовые расширения, которые осмысленно показывать прямо в окне.
// .kicad_sch — S-выражения, читаемый текст; смотреть его тут полезно,
// чтобы убедиться, что схема не пустая, не открывая KiCad.
bool isTextual(const QString& path)
{
    static const QStringList exts = {
        QStringLiteral("kicad_sch"), QStringLiteral("txt"), QStringLiteral("md"),
        QStringLiteral("json"),      QStringLiteral("jsonl"), QStringLiteral("csv"),
        QStringLiteral("log"),       QStringLiteral("svg"), QStringLiteral("mmd"),
    };
    return exts.contains(QFileInfo(path).suffix().toLower());
}

} // namespace

ArtifactsDialog::ArtifactsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(IS_EN ? QStringLiteral("JARVIS — Files")
                         : QStringLiteral("JARVIS — Файлы"));
    setMinimumSize(940, 620);
    setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
                      .arg(css(JarvisTheme::instance().bg()),
                           css(JarvisTheme::instance().onSurface())));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* title = new QLabel(IS_EN ? QStringLiteral("FILES JARVIS MADE")
                                   : QStringLiteral("ФАЙЛЫ ОТ ДЖАРВИСА"), this);
    title->setStyleSheet(QStringLiteral("color: %1; font-size: 16px; font-weight: bold;")
                             .arg(css(JarvisTheme::instance().accent())));
    root->addWidget(title);

    auto* split = new QSplitter(Qt::Horizontal, this);

    // ── Список ──────────────────────────────────────────────
    m_list = new QListWidget(split);
    m_list->setStyleSheet(
        QStringLiteral("QListWidget { background: %1; border: 1px solid %2; "
                       "border-radius: %3px; padding: 4px; } "
                       "QListWidget::item { padding: 7px 8px; border-radius: 6px; } "
                       "QListWidget::item:selected { background: %4; color: %5; }")
            .arg(css(JarvisTheme::instance().surface1()),
                 css(JarvisTheme::instance().outline()))
            .arg(JarvisTheme::instance().radiusMd())
            .arg(css(JarvisTheme::instance().accentSubtle()),
                 css(JarvisTheme::instance().accent())));
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { showSelected(); });
    // Двойной клик — самый ожидаемый жест для «открыть файл».
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        openSelected();
    });

    // ── Просмотр ────────────────────────────────────────────
    auto* right = new QWidget(split);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(8);

    m_preview = new QStackedWidget(right);
    m_preview->setStyleSheet(
        QStringLiteral("background: %1; border: 1px solid %2; border-radius: %3px;")
            .arg(css(JarvisTheme::instance().surface1()),
                 css(JarvisTheme::instance().outline()))
            .arg(JarvisTheme::instance().radiusMd()));

    m_image = new QLabel(m_preview);
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setText(IS_EN ? QStringLiteral("select a file")
                           : QStringLiteral("выберите файл"));
    m_image->setStyleSheet(QStringLiteral("color: %1; border: none;")
                               .arg(css(JarvisTheme::instance().onSurfaceDim())));

    m_text = new QPlainTextEdit(m_preview);
    m_text->setReadOnly(true);
    m_text->setStyleSheet(
        QStringLiteral("background: transparent; color: %1; border: none; "
                       "font-family: Consolas, monospace; font-size: 11px;")
            .arg(css(JarvisTheme::instance().onSurfaceVariant())));

    m_preview->addWidget(m_image);   // 0
    m_preview->addWidget(m_text);    // 1

    m_info = new QLabel(right);
    m_info->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;")
                              .arg(css(JarvisTheme::instance().onSurfaceVariant())));

    rl->addWidget(m_preview, 1);
    rl->addWidget(m_info);

    split->addWidget(m_list);
    split->addWidget(right);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 5);
    root->addWidget(split, 1);

    // ── Кнопки ──────────────────────────────────────────────
    const QString btnCss =
        QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                       "border-radius: %4px; padding: 7px 14px; } "
                       "QPushButton:hover { background: %5; } "
                       "QPushButton:disabled { color: %6; border-color: %7; }")
            .arg(css(JarvisTheme::instance().surface2()),
                 css(JarvisTheme::instance().accent()),
                 css(JarvisTheme::instance().outlineStrong()))
            .arg(JarvisTheme::instance().radiusSm())
            .arg(css(JarvisTheme::instance().surface3()),
                 css(JarvisTheme::instance().onSurfaceDim()),
                 css(JarvisTheme::instance().outline()));

    m_openBtn = new QPushButton(IS_EN ? QStringLiteral("Open") : QStringLiteral("Открыть"), this);
    m_revealBtn = new QPushButton(IS_EN ? QStringLiteral("Show in folder")
                                        : QStringLiteral("Показать в папке"), this);
    m_forgetBtn = new QPushButton(IS_EN ? QStringLiteral("Remove from list")
                                        : QStringLiteral("Убрать из списка"), this);
    for (auto* b : { m_openBtn, m_revealBtn, m_forgetBtn }) {
        b->setStyleSheet(btnCss);
        b->setCursor(Qt::PointingHandCursor);
        b->setEnabled(false);
    }
    // Формулировка «убрать из списка», а не «удалить»: реестр не владеет
    // файлами и с диска ничего не стирает — кнопка не должна обещать
    // больше, чем делает.
    m_forgetBtn->setToolTip(IS_EN
        ? QStringLiteral("Removes the entry only — the file stays on disk")
        : QStringLiteral("Убирает только запись — файл остаётся на диске"));

    connect(m_openBtn,   &QPushButton::clicked, this, &ArtifactsDialog::openSelected);
    connect(m_revealBtn, &QPushButton::clicked, this, &ArtifactsDialog::revealSelected);
    connect(m_forgetBtn, &QPushButton::clicked, this, &ArtifactsDialog::forgetSelected);

    auto* row = new QHBoxLayout();
    row->addWidget(m_openBtn);
    row->addWidget(m_revealBtn);
    row->addStretch(1);
    row->addWidget(m_forgetBtn);
    root->addLayout(row);

    connect(&ArtifactRegistry::instance(), &ArtifactRegistry::changed,
            this, [this]() { reload(); });

    // Подбираем файлы, созданные до появления реестра (и снятые в обход
    // него) — иначе окно встречает пустотой человека, у которого на диске
    // уже лежат его собственные 15 фотографий.
    ArtifactRegistry::instance().scanKnownFolders();

    reload();
}

void ArtifactsDialog::reload()
{
    const qint64 keepId = current().id;

    m_items = ArtifactRegistry::instance().recent(200);
    m_list->clear();

    int restoreRow = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        const auto& a = m_items[i];
        QString label = iconFor(a.kind) + QStringLiteral("  ") + a.title;
        // Пропавший файл не прячем: запись показывает, что он был, и это
        // честнее, чем молча укоротить список.
        if (!a.exists())
            label += IS_EN ? QStringLiteral("   (missing)") : QStringLiteral("   (нет файла)");
        m_list->addItem(label);
        if (a.id == keepId) restoreRow = i;
    }

    if (m_items.isEmpty()) {
        m_image->setText(IS_EN
            ? QStringLiteral("Nothing yet.\nSchematics, diagrams and screenshots\n"
                             "Jarvis makes will show up here.")
            : QStringLiteral("Пока пусто.\nЗдесь появятся схемы, диаграммы\n"
                             "и скриншоты, которые сделает Джарвис."));
        m_preview->setCurrentIndex(0);
        m_info->clear();
        for (auto* b : { m_openBtn, m_revealBtn, m_forgetBtn }) b->setEnabled(false);
        return;
    }

    m_list->setCurrentRow(restoreRow >= 0 ? restoreRow : 0);
}

ArtifactRegistry::Artifact ArtifactsDialog::current() const
{
    const int row = m_list ? m_list->currentRow() : -1;
    if (row < 0 || row >= m_items.size()) return {};
    return m_items[row];
}

void ArtifactsDialog::showSelected()
{
    const auto a = current();
    m_currentImage = QImage();

    const bool have = a.id != 0;
    const bool onDisk = have && a.exists();
    m_openBtn->setEnabled(onDisk);
    m_revealBtn->setEnabled(onDisk);
    m_forgetBtn->setEnabled(have);

    if (!have) return;

    QString info = QFileInfo(a.path).absoluteFilePath();
    if (onDisk) {
        info += QStringLiteral("  ·  ")
              + QLocale().formattedDataSize(a.sizeBytes());
    } else {
        info += IS_EN ? QStringLiteral("  ·  file no longer on disk")
                      : QStringLiteral("  ·  файла больше нет на диске");
    }
    if (a.createdAt.isValid())
        info += QStringLiteral("  ·  ") + a.createdAt.toString(QStringLiteral("dd MMM HH:mm"));
    if (!a.query.isEmpty())
        info += QStringLiteral("\n") + (IS_EN ? QStringLiteral("from: ")
                                              : QStringLiteral("из запроса: ")) + a.query;
    m_info->setText(info);

    if (!onDisk) {
        m_preview->setCurrentIndex(0);
        m_image->setText(IS_EN ? QStringLiteral("file no longer on disk")
                               : QStringLiteral("файла больше нет на диске"));
        return;
    }

    if (a.isImage()) {
        m_currentImage.load(a.path);
        m_preview->setCurrentIndex(0);
        resizeEvent(nullptr);   // масштабируем под текущий размер
        return;
    }

    if (isTextual(a.path)) {
        QFile f(a.path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // Ограничиваем чтение: .kicad_sch на крупную схему —
            // это мегабайты S-выражений, и грузить их целиком ради
            // беглого взгляда незачем.
            const QByteArray head = f.read(200 * 1024);
            m_text->setPlainText(QString::fromUtf8(head)
                + (f.bytesAvailable() > 0
                       ? (IS_EN ? QStringLiteral("\n\n… truncated for preview")
                                : QStringLiteral("\n\n… превью обрезано"))
                       : QString()));
            f.close();
            m_preview->setCurrentIndex(1);
            return;
        }
    }

    m_preview->setCurrentIndex(0);
    m_image->setText(IS_EN ? QStringLiteral("no preview — use Open")
                           : QStringLiteral("превью нет — нажмите «Открыть»"));
}

void ArtifactsDialog::openSelected()
{
    const auto a = current();
    if (a.id == 0 || !a.exists()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(a.path));
}

void ArtifactsDialog::revealSelected()
{
    const auto a = current();
    if (a.id == 0 || !a.exists()) return;
    // /select, показывает файл выделенным в проводнике — открыть просто
    // папку было бы менее полезно, когда в ней сотня файлов.
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            { QStringLiteral("/select,"),
                              QDir::toNativeSeparators(a.path) });
}

void ArtifactsDialog::forgetSelected()
{
    const auto a = current();
    if (a.id == 0) return;
    ArtifactRegistry::instance().forget(a.id);   // сигнал changed() перезагрузит список
}

void ArtifactsDialog::resizeEvent(QResizeEvent* e)
{
    if (e) QDialog::resizeEvent(e);
    if (m_currentImage.isNull() || !m_image) return;
    m_image->setPixmap(QPixmap::fromImage(m_currentImage)
                           .scaled(m_image->size(), Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation));
}
