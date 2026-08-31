// ============================================================
// camera_view_dialog.cpp — Живой вид с камеры с разметкой лиц
// ============================================================

#include "camera_view_dialog.h"
#include "security_camera.h"
#include "jarvis_theme.h"
#include "lang.h"
#include "jarvis_paths.h"
#include "artifact_registry.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>
#include <QPixmap>
#include <QDateTime>
#include <QResizeEvent>
#include <QHBoxLayout>
#include <QPushButton>
#include <QListWidget>
#include <QListView>
#include <QIcon>
#include <QDir>
#include <QFileInfo>
#include <QImage>

namespace {

// Опрос камеры. Реже, чем видео (это не видеопоток, а периодическая
// идентификация с распознаванием — каждый кадр стоит прохода детектора),
// но достаточно часто, чтобы окно читалось как «живое», а не как снимок.
constexpr int kPollIntervalMs = 1200;

QString css(const QColor& c) { return JarvisTheme::css(c); }

} // namespace

CameraViewDialog::CameraViewDialog(SecurityCamera* camera, QWidget* parent)
    : QDialog(parent)
    , m_camera(camera)
{
    setWindowTitle(IS_EN ? QStringLiteral("JARVIS — Camera View")
                         : QStringLiteral("JARVIS — Вид с камеры"));
    setMinimumSize(720, 560);
    setStyleSheet(QStringLiteral("background-color: %1;")
                      .arg(css(JarvisTheme::instance().bg())));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    m_status = new QLabel(this);
    m_status->setStyleSheet(
        QStringLiteral("color: %1; font-size: 13px; font-weight: bold;")
            .arg(css(JarvisTheme::instance().accent())));
    m_status->setText(IS_EN ? QStringLiteral("Waiting for the first frame…")
                            : QStringLiteral("Жду первый кадр…"));

    m_view = new QLabel(this);
    m_view->setAlignment(Qt::AlignCenter);
    m_view->setMinimumHeight(360);
    m_view->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid %2; border-radius: %3px;")
            .arg(css(JarvisTheme::instance().surface1()),
                 css(JarvisTheme::instance().outline()))
            .arg(JarvisTheme::instance().radiusMd()));
    m_view->setText(IS_EN ? QStringLiteral("no frame yet")
                          : QStringLiteral("кадра ещё нет"));

    m_details = new QLabel(this);
    m_details->setWordWrap(true);
    m_details->setTextFormat(Qt::RichText);
    m_details->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px;")
            .arg(css(JarvisTheme::instance().onSurfaceVariant())));
    m_details->setText(IS_EN
        ? QStringLiteral("Green box — someone I recognise. Red — a face I don't know yet. "
                         "Teach faces in Vision Center.")
        : QStringLiteral("Зелёная рамка — тот, кого узнаю. Красная — лицо, которое пока "
                         "не знаю. Обучать лица — в Центре зрения."));

    root->addWidget(m_status);
    root->addWidget(m_view, 1);
    root->addWidget(m_details);

    // ── Кнопки ──────────────────────────────────────────────
    const QString btnCss =
        QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                       "border-radius: %4px; padding: 7px 14px; } "
                       "QPushButton:hover { background: %5; } "
                       "QPushButton:disabled { color: %6; }")
            .arg(css(JarvisTheme::instance().surface2()),
                 css(JarvisTheme::instance().accent()),
                 css(JarvisTheme::instance().outlineStrong()))
            .arg(JarvisTheme::instance().radiusSm())
            .arg(css(JarvisTheme::instance().surface3()),
                 css(JarvisTheme::instance().onSurfaceDim()));

    m_shotBtn = new QPushButton(IS_EN ? QStringLiteral("📷 Take shot")
                                      : QStringLiteral("📷 Снять кадр"), this);
    m_liveBtn = new QPushButton(IS_EN ? QStringLiteral("▶ Back to live")
                                      : QStringLiteral("▶ Вернуться к живому"), this);
    for (auto* b : { m_shotBtn, m_liveBtn }) {
        b->setStyleSheet(btnCss);
        b->setCursor(Qt::PointingHandCursor);
    }
    m_liveBtn->setEnabled(false);   // включается, только когда смотрим снимок

    connect(m_shotBtn, &QPushButton::clicked, this, &CameraViewDialog::saveCurrentFrame);
    connect(m_liveBtn, &QPushButton::clicked, this, &CameraViewDialog::resumeLive);

    auto* btnRow = new QHBoxLayout();
    btnRow->addWidget(m_shotBtn);
    btnRow->addWidget(m_liveBtn);
    btnRow->addStretch(1);
    root->addLayout(btnRow);

    // ── Галерея ─────────────────────────────────────────────
    // Лента внизу, а не отдельное окно: снятый кадр нужно сравнить с тем,
    // что камера показывает сейчас, а для этого оба должны быть на экране.
    m_gallery = new QListWidget(this);
    m_gallery->setViewMode(QListView::IconMode);
    m_gallery->setFlow(QListView::LeftToRight);
    m_gallery->setWrapping(false);
    m_gallery->setIconSize(QSize(104, 78));
    m_gallery->setFixedHeight(116);
    m_gallery->setMovement(QListView::Static);
    m_gallery->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_gallery->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_gallery->setStyleSheet(
        QStringLiteral("QListWidget { background: %1; border: 1px solid %2; "
                       "border-radius: %3px; padding: 4px; } "
                       "QListWidget::item { margin: 2px; border-radius: 6px; } "
                       "QListWidget::item:selected { background: %4; }")
            .arg(css(JarvisTheme::instance().surface1()),
                 css(JarvisTheme::instance().outline()))
            .arg(JarvisTheme::instance().radiusMd())
            .arg(css(JarvisTheme::instance().accentSubtle())));
    connect(m_gallery, &QListWidget::currentRowChanged,
            this, &CameraViewDialog::showGalleryItem);
    root->addWidget(m_gallery);

    // Галерея наполняется до проверки камеры: снятые раньше кадры стоит
    // показать даже когда камеры сейчас нет — иначе окно выглядит пустым
    // ровно тогда, когда в него зашли посмотреть старые снимки.
    reloadGallery();

    if (!m_camera) {
        m_status->setText(IS_EN ? QStringLiteral("Camera unavailable — showing saved shots")
                                : QStringLiteral("Камера недоступна — показываю сохранённые кадры"));
        m_shotBtn->setEnabled(false);
        return;
    }

    // Кадр берём сами, а НЕ через checkNow()/facesIdentified.
    //
    // facesIdentified испускается только если в кадре нашлось лицо
    // (см. `if (!observations.isEmpty())` в onSentinelTick), поэтому окно
    // предпросмотра, подписанное на этот сигнал, оставалось пустым всё
    // время, пока перед камерой никого нет — то есть ровно тогда, когда
    // в него и хочется посмотреть.
    //
    // А checkNow() — это тик ОХРАНЫ: тревоги, таймеры деэскалации,
    // автоблокировка экрана. Гонять его ради картинки значит запускать
    // сторожевую логику каждым открытием окна, с её побочными эффектами.
    // Предпросмотр обязан только смотреть.
    m_poll = new QTimer(this);
    m_poll->setInterval(kPollIntervalMs);
    connect(m_poll, &QTimer::timeout, this, &CameraViewDialog::grabFrame);
    m_poll->start();
    grabFrame();      // не ждать первый тик
}

void CameraViewDialog::grabFrame()
{
    // Пока смотрят снимок из галереи, живой кадр не берём: и картинку бы
    // затёрло, и камеру дёргать незачем.
    if (!m_camera || m_paused) return;

    const QImage frame = m_camera->snapshotFullRes();
    if (frame.isNull()) {
        m_status->setText(IS_EN
            ? QStringLiteral("Camera unavailable — is it in use by another app?")
            : QStringLiteral("Камера недоступна — не занята ли другой программой?"));
        return;
    }

    const QList<FaceObservation> faces = m_camera->identifyFaces(frame);
    onFaces(SecurityCamera::annotateFaces(frame, faces), faces);
}

CameraViewDialog::~CameraViewDialog()
{
    // Таймер — ребёнок диалога и умрёт сам, но камера живёт дольше окна:
    // остановить опрос явно, чтобы закрытое окно не продолжало гонять
    // детектор лиц в фоне.
    if (m_poll) m_poll->stop();
}

void CameraViewDialog::onFaces(const QImage& annotated,
                               const QList<FaceObservation>& faces)
{
    m_frame = annotated;
    renderCurrent();

    int known = 0;
    for (const auto& f : faces) if (f.known) ++known;

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    if (faces.isEmpty()) {
        m_status->setText((IS_EN ? QStringLiteral("Nobody in frame · %1")
                                 : QStringLiteral("В кадре никого · %1")).arg(stamp));
    } else {
        m_status->setText((IS_EN
            ? QStringLiteral("%1 face(s) · %2 recognised · %3")
            : QStringLiteral("Лиц в кадре: %1 · узнано: %2 · %3"))
            .arg(faces.size()).arg(known).arg(stamp));
    }

    // Разбор по лицам: рамка на картинке говорит "кто", а этот список —
    // ещё и насколько уверенно, чего на кадре не написать, не захламив его.
    QStringList lines;
    for (const auto& f : faces) {
        const QString colour = f.known ? css(JarvisTheme::instance().success())
                                       : css(JarvisTheme::instance().error());
        QString line = QStringLiteral("<span style='color:%1;'>&#9679;</span> %2")
                           .arg(colour, f.label().toHtmlEscaped());
        if (f.known && f.confidence > 0.0) {
            // LBP-скор: чем МЕНЬШЕ, тем ближе совпадение, поэтому показываем
            // сырое число с подписью, а не выдуманный "процент похожести".
            line += QStringLiteral(" <span style='color:%1;'>(score %2)</span>")
                        .arg(css(JarvisTheme::instance().onSurfaceDim()))
                        .arg(f.confidence, 0, 'f', 1);
        }
        lines << line;
    }
    if (!lines.isEmpty())
        m_details->setText(lines.join(QStringLiteral("<br>")));
}

// ── Галерея ─────────────────────────────────────────────────────────

void CameraViewDialog::reloadGallery()
{
    if (!m_gallery) return;

    const int keepRow = m_gallery->currentRow();
    m_gallery->clear();
    m_galleryPaths.clear();

    // Два источника: образцы, по которым обучалось лицо, и кадры, снятые
    // вручную. Показываем вместе — с точки зрения человека это просто
    // «что камера уже видела», а не две разные сущности.
    const QString base = JarvisPaths::subPath(QStringLiteral("security"));
    const QStringList dirs = {
        base + QStringLiteral("/shots"),
        base + QStringLiteral("/owner_samples"),
    };

    for (const QString& d : dirs) {
        QDir dir(d);
        if (!dir.exists()) continue;
        // Свежие сверху — недавний кадр нужен чаще, чем первый образец,
        // снятый месяц назад.
        const auto files = dir.entryInfoList(QStringList{ QStringLiteral("*.png"),
                                                          QStringLiteral("*.jpg") },
                                             QDir::Files, QDir::Time);
        for (const QFileInfo& fi : files)
            m_galleryPaths << fi.absoluteFilePath();
    }

    for (const QString& p : std::as_const(m_galleryPaths)) {
        QPixmap thumb(p);
        if (thumb.isNull()) continue;
        auto* item = new QListWidgetItem(
            QIcon(thumb.scaled(104, 78, Qt::KeepAspectRatio, Qt::SmoothTransformation)),
            QString());
        item->setToolTip(p);
        m_gallery->addItem(item);
    }

    if (m_galleryPaths.isEmpty()) return;
    if (keepRow >= 0 && keepRow < m_gallery->count())
        m_gallery->setCurrentRow(keepRow);
}

void CameraViewDialog::showGalleryItem(int row)
{
    if (row < 0 || row >= m_galleryPaths.size()) return;

    QImage img(m_galleryPaths[row]);
    if (img.isNull()) return;

    // Пока смотрят снимок — живой вид на паузе, иначе следующий тик
    // таймера затёр бы выбранный кадр через секунду.
    m_paused = true;
    m_liveBtn->setEnabled(true);
    m_frame = img;
    renderCurrent();

    const QFileInfo fi(m_galleryPaths[row]);
    m_status->setText((IS_EN ? QStringLiteral("Saved shot · %1")
                             : QStringLiteral("Снимок · %1"))
                          .arg(fi.lastModified().toString(QStringLiteral("dd MMM HH:mm"))));
    m_details->setText(fi.absoluteFilePath());
}

void CameraViewDialog::resumeLive()
{
    m_paused = false;
    m_liveBtn->setEnabled(false);
    m_gallery->setCurrentRow(-1);
    grabFrame();
}

void CameraViewDialog::saveCurrentFrame()
{
    if (m_frame.isNull()) return;

    const QString dir = JarvisPaths::subPath(QStringLiteral("security/shots"));
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/shot_%1.png")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));

    if (!m_frame.save(path, "PNG")) {
        m_status->setText(IS_EN ? QStringLiteral("Could not save the shot")
                                : QStringLiteral("Не удалось сохранить кадр"));
        return;
    }

    // В общий реестр — чтобы кадр нашёлся и через 🗂, а не только здесь.
    ArtifactRegistry::instance().record(
        path, QString::fromLatin1(ArtifactRegistry::kPhoto),
        IS_EN ? QStringLiteral("Camera shot") : QStringLiteral("Кадр с камеры"));

    reloadGallery();
    m_status->setText(IS_EN ? QStringLiteral("Shot saved")
                            : QStringLiteral("Кадр сохранён"));
}

void CameraViewDialog::renderCurrent()
{
    if (m_frame.isNull() || !m_view) return;
    m_view->setPixmap(QPixmap::fromImage(m_frame)
                          .scaled(m_view->size(), Qt::KeepAspectRatio,
                                  Qt::SmoothTransformation));
}

void CameraViewDialog::resizeEvent(QResizeEvent* e)
{
    QDialog::resizeEvent(e);
    renderCurrent();
}
