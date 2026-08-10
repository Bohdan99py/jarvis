#pragma once
// ============================================================
// vision_center_dialog.h — Vision Center
//
// Компьютерное зрение у Джарвиса было, но увидеть его было негде:
// распознавание лиц с дообучением (LBP-гистограммы на профиль),
// перенос выученных лиц между экземплярами по mesh, экранное зрение с
// OCR — всё это жило внутри «охранной камеры» и настроек, разбросанных
// по меню. Функция, о которой нельзя спросить «что ты сейчас видишь и
// кого знаешь», для пользователя всё равно что отсутствует.
//
// Тот же раздел ответственности, что у TrainingCenterDialog: этот класс
// владеет данными и побочными эффектами (реестр лиц, дообучение с
// камеры, переключатели наблюдения), VisionCenter.qml — внешним видом.
// ============================================================

#include <QDialog>
#include <QVBoxLayout>

class QQuickWidget;
class SecurityCamera;
class CameraAgent;

class VisionCenterDialog : public QDialog
{
    Q_OBJECT
public:
    // initialTab: 0=Known faces, 1=Camera, 2=Screen vision
    explicit VisionCenterDialog(SecurityCamera* camera,
                                QWidget* parent = nullptr,
                                int initialTab = 0);

    // Вызывается из VisionCenter.qml через контекстное свойство
    // "visionCenter".
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void teachMyFace();
    Q_INVOKABLE void forgetFace(qint64 faceId);
    Q_INVOKABLE void toggleMonitoring(bool on);
    Q_INVOKABLE void setAlertUnknown(bool on);
    Q_INVOKABLE void setAutoLock(bool on);
    Q_INVOKABLE void captureNow();

private:
    void refreshFaces();
    void refreshCamera();
    void refreshScreenVision();

    SecurityCamera* m_camera = nullptr;
    QQuickWidget*   m_view   = nullptr;
};
