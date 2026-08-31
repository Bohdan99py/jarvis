#pragma once
// ============================================================
// camera_view_dialog.h — Живой вид с камеры с разметкой лиц
//
// SecurityCamera::annotateFaces() рисовала рамки и подписи на каждом
// кадре с самого начала — зелёная рамка для узнанного, красная для
// незнакомца, подпись «Имя, возраст, статус». Кадр уходил сигналом
// facesIdentified(annotatedFrame, faces), и единственный подписчик
// (mainwindow) картинку игнорировал, оставляя от неё строчку в логе.
// То есть разметка считалась постоянно и не показывалась никогда.
//
// Это окно её показывает и добавляет то, чего в картинке нет: сколько
// лиц в кадре, кто именно узнан и с какой уверенностью, когда был
// последний кадр. Плюс частота обновления — камера опрашивается по
// таймеру, иначе «живой вид» показывал бы один кадр с момента открытия.
// ============================================================

#include <QDialog>
#include <QImage>
#include <QList>

class QLabel;
class QTimer;
class SecurityCamera;
struct FaceObservation;

class CameraViewDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CameraViewDialog(SecurityCamera* camera, QWidget* parent = nullptr);
    ~CameraViewDialog() override;

protected:
    // Перерисовываем масштаб под новый размер окна, не дожидаясь
    // следующего кадра — иначе картинка «прыгает» с задержкой.
    void resizeEvent(QResizeEvent* e) override;

private:
    // Снимает кадр и сам его размечает. Именно снимает, а не слушает
    // камеру: сигнал с кадром приходит только при найденном лице, и
    // подписка давала пустое окно в пустой комнате.
    void grabFrame();
    void onFaces(const QImage& annotated, const QList<FaceObservation>& faces);
    void renderCurrent();

    // ── Галерея ──────────────────────────────────────────────
    void reloadGallery();     // перечитывает папки с кадрами
    void showGalleryItem(int row);
    void saveCurrentFrame();  // «снять кадр» → файл + реестр артефактов
    void resumeLive();        // вернуться из просмотра снимка в живой вид

    SecurityCamera* m_camera = nullptr;
    QLabel*  m_view    = nullptr;
    QLabel*  m_status  = nullptr;
    QLabel*  m_details = nullptr;
    QTimer*  m_poll    = nullptr;
    QImage   m_frame;          // последний аннотированный кадр

    class QListWidget* m_gallery = nullptr;
    class QPushButton* m_shotBtn = nullptr;
    class QPushButton* m_liveBtn = nullptr;
    QStringList m_galleryPaths;

    // Живой вид приостановлен, пока смотрят снимок из галереи: иначе
    // следующий тик таймера затирал бы выбранный кадр через секунду.
    bool m_paused = false;
};
