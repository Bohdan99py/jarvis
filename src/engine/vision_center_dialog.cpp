// ============================================================
// vision_center_dialog.cpp — Vision Center
// ============================================================

#include "vision_center_dialog.h"
#include "security_camera.h"
#include "camera_agent.h"
#include "face_registry.h"
#include "ocr_extractor.h"
#include "notification_manager.h"
#include "lang.h"

#include "jarvis_theme.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QCoreApplication>
#include <QMessageBox>

VisionCenterDialog::VisionCenterDialog(SecurityCamera* camera,
                                       QWidget* parent,
                                       int initialTab)
    : QDialog(parent)
    , m_camera(camera)
{
    setWindowTitle(QStringLiteral("JARVIS — Vision Center"));
    setMinimumSize(880, 600);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_view = new QQuickWidget(this);
    m_view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_view->setClearColor(QColor(0x08, 0x0A, 0x0F));
    JarvisTheme::prepareEngine(m_view->engine());

    // Значения по умолчанию для каждого свойства, которое читает
    // VisionCenter.qml — до setSource(), иначе стартовые привязки
    // вычисляются по undefined. Целые нули заворачиваем в QVariant:
    // голый 0 — это null-pointer-constant, и перегрузка
    // setContextProperty(name, QObject*) выигрывает у QVariant-версии,
    // после чего QML падает на "Cannot assign std::nullptr_t to int".
    QQmlContext* ctx = m_view->rootContext();
    ctx->setContextProperty(QStringLiteral("vcEnglish"), IS_EN);
    ctx->setContextProperty(QStringLiteral("initialTab"), initialTab);
    ctx->setContextProperty(QStringLiteral("knownFaces"), QVariantList());
    ctx->setContextProperty(QStringLiteral("faceCount"), QVariant(0));
    ctx->setContextProperty(QStringLiteral("ownerEnrolled"), false);
    ctx->setContextProperty(QStringLiteral("webcamPresent"), false);
    ctx->setContextProperty(QStringLiteral("monitoringOn"), false);
    ctx->setContextProperty(QStringLiteral("alertUnknownOn"), false);
    ctx->setContextProperty(QStringLiteral("autoLockOn"), false);
    ctx->setContextProperty(QStringLiteral("motionAlertOn"), false);
    ctx->setContextProperty(QStringLiteral("screenLocked"), false);
    ctx->setContextProperty(QStringLiteral("ocrAvailable"), false);
    ctx->setContextProperty(QStringLiteral("pdfTextAvailable"), false);
    ctx->setContextProperty(QStringLiteral("visionCenter"), this);

    m_view->setSource(QUrl(QStringLiteral("qrc:/qml/VisionCenter.qml")));
    root->addWidget(m_view);

    // Реестр может измениться и не из этого окна — например, лицо
    // приедет по mesh от другого экземпляра Джарвиса.
    connect(&FaceRegistry::instance(), &FaceRegistry::registryChanged,
            this, [this]() { refreshFaces(); });

    refresh();
}

void VisionCenterDialog::refresh()
{
    refreshFaces();
    refreshCamera();
    refreshScreenVision();
}

void VisionCenterDialog::refreshFaces()
{
    QQmlContext* ctx = m_view->rootContext();
    const QList<KnownFace> faces = FaceRegistry::instance().allFaces();

    QVariantList out;
    for (const KnownFace& f : faces) {
        QVariantMap m;
        m[QStringLiteral("id")]      = f.id;
        m[QStringLiteral("name")]    = f.name;
        m[QStringLiteral("age")]     = f.age;
        m[QStringLiteral("status")]  = f.status;
        // Сколько образцов видел — это и есть "насколько уверенно узнаю".
        m[QStringLiteral("samples")] = f.histograms.size();
        // Пустой originNode = обучено здесь; иначе имя узла, откуда
        // профиль приехал по mesh. Разница важная: своё лицо можно
        // дообучить с этой камеры, чужое — нет.
        m[QStringLiteral("local")]   = f.originNode.isEmpty();
        m[QStringLiteral("origin")]  = f.originNode;
        out.append(m);
    }
    ctx->setContextProperty(QStringLiteral("knownFaces"), out);
    ctx->setContextProperty(QStringLiteral("faceCount"), out.size());
    ctx->setContextProperty(QStringLiteral("ownerEnrolled"),
                            m_camera && m_camera->isOwnerEnrolled());
}

void VisionCenterDialog::refreshCamera()
{
    QQmlContext* ctx = m_view->rootContext();

    CameraAgent probe;
    ctx->setContextProperty(QStringLiteral("webcamPresent"), probe.hasWebcam());

    // Все флажки спрашиваются у камеры. Раньше здесь читалось только
    // monitoringOn, а «предупреждать о незнакомце» и «блокировать при
    // угрозе» лишь записывались при щелчке — то есть окно всегда
    // открывалось с выключенными тумблерами независимо от того, что
    // на самом деле включено, и первый щелчок по включённому флажку
    // включал его повторно вместо того, чтобы выключить.
    const bool has = (m_camera != nullptr);
    ctx->setContextProperty(QStringLiteral("monitoringOn"),
                            has && m_camera->isMonitoring());
    ctx->setContextProperty(QStringLiteral("alertUnknownOn"),
                            has && m_camera->alertOnUnknownFace());
    ctx->setContextProperty(QStringLiteral("autoLockOn"),
                            has && m_camera->autoLockOnThreat());
    ctx->setContextProperty(QStringLiteral("motionAlertOn"),
                            has && m_camera->alertOnMotion());
    ctx->setContextProperty(QStringLiteral("screenLocked"),
                            has && m_camera->isScreenLocked());
}

void VisionCenterDialog::refreshScreenVision()
{
    QQmlContext* ctx = m_view->rootContext();
    OcrExtractor ocr;
    ctx->setContextProperty(QStringLiteral("ocrAvailable"), ocr.isTesseractAvailable());
    ctx->setContextProperty(QStringLiteral("pdfTextAvailable"), ocr.isPopplerAvailable());
}

// ============================================================
// Invokable из VisionCenter.qml
// ============================================================

void VisionCenterDialog::teachMyFace()
{
    if (!m_camera) return;

    CameraAgent probe;
    if (!probe.hasWebcam()) {
        QMessageBox::warning(this,
            IS_EN ? QStringLiteral("No camera") : QStringLiteral("Нет камеры"),
            IS_EN ? QStringLiteral("No webcam detected — nothing to learn from.")
                  : QStringLiteral("Веб-камера не найдена — учиться не с чего."));
        return;
    }

    // Дообучение снимает серию кадров: об этом предупреждаем заранее,
    // потому что камера включится сама и это не должно выглядеть как
    // что-то, чего пользователь не просил.
    const auto r = QMessageBox::question(this,
        IS_EN ? QStringLiteral("Teach my face") : QStringLiteral("Обучить моё лицо"),
        IS_EN ? QStringLiteral("I'll take about 10 shots from the webcam and learn "
                               "your face from them. Look at the camera.\n\nStart?")
              : QStringLiteral("Сделаю около 10 кадров с веб-камеры и обучусь "
                               "вашему лицу по ним. Смотрите в камеру.\n\nНачинаем?"),
        QMessageBox::Yes | QMessageBox::No);
    if (r != QMessageBox::Yes) return;

    m_camera->enrollOwnerFace(10);
    NotificationManager::instance().showNotification(
        IS_EN ? QStringLiteral("Learning your face") : QStringLiteral("Учу ваше лицо"),
        IS_EN ? QStringLiteral("Capturing samples from the webcam…")
              : QStringLiteral("Снимаю образцы с веб-камеры…"),
        NotificationManager::Level::Info);
    refreshFaces();
}

void VisionCenterDialog::forgetFace(qint64 faceId)
{
    if (faceId == 0) return;

    const auto r = QMessageBox::question(this,
        IS_EN ? QStringLiteral("Forget this face") : QStringLiteral("Забыть лицо"),
        IS_EN ? QStringLiteral("Delete this face profile? It can't be undone — "
                               "the face would have to be taught again.")
              : QStringLiteral("Удалить профиль лица? Это необратимо — "
                               "лицо придётся обучать заново."),
        QMessageBox::Yes | QMessageBox::No);
    if (r != QMessageBox::Yes) return;

    FaceRegistry::instance().removeFace(faceId);
    refreshFaces();
}

void VisionCenterDialog::toggleMonitoring(bool on)
{
    if (!m_camera) return;
    if (on) m_camera->startMonitoring(60);
    else    m_camera->stopMonitoring();
    refreshCamera();
}

void VisionCenterDialog::setAlertUnknown(bool on)
{
    if (!m_camera) return;
    m_camera->setAlertOnUnknownFace(on);
    m_view->rootContext()->setContextProperty(QStringLiteral("alertUnknownOn"), on);
}

void VisionCenterDialog::setAutoLock(bool on)
{
    if (!m_camera) return;
    m_camera->setAutoLockOnThreat(on);
    m_view->rootContext()->setContextProperty(QStringLiteral("autoLockOn"), on);
}

void VisionCenterDialog::setMotionAlert(bool on)
{
    if (!m_camera) return;
    m_camera->setAlertOnMotion(on);
    m_view->rootContext()->setContextProperty(QStringLiteral("motionAlertOn"), on);
}

void VisionCenterDialog::lockScreen()
{
    if (!m_camera) return;
    m_camera->lockScreen();
    m_view->rootContext()->setContextProperty(QStringLiteral("screenLocked"), true);
}

void VisionCenterDialog::captureNow()
{
    if (!m_camera) return;
    m_camera->checkNow();
    NotificationManager::instance().showNotification(
        IS_EN ? QStringLiteral("Looking now") : QStringLiteral("Смотрю сейчас"),
        IS_EN ? QStringLiteral("Checking the camera frame…")
              : QStringLiteral("Проверяю кадр с камеры…"),
        NotificationManager::Level::Info);
}
