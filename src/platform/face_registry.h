#pragma once
// ============================================================
// face_registry.h — Реестр известных лиц
//
// Хранит "профили лиц": имя, возраст, статус + LBP-гистограммы
// (те же признаки, что использует SecurityCamera для владельца).
// Профили синхронизируются между узлами JARVIS по J2J-мешу
// (пакет FaceSync) — ноутбук девушки "знает" владельца этого ПК
// и наоборот: камера подписывает лицо именем/возрастом/статусом,
// даже если человек обучался на другом компьютере.
//
// Хранение: таблица known_faces в основной SQLite-базе.
// OpenCV НЕ требуется — чистое хранилище векторов признаков.
// ============================================================

#include <QObject>
#include <QString>
#include <QVector>
#include <QJsonArray>
#include <QJsonObject>

struct KnownFace {
    qint64  id = 0;
    QString name;
    int     age = 0;            // 0 = неизвестен
    QString status;             // "владелец", "Developer", "гость"...
    QString originNode;         // nodeId узла, где лицо обучено ("" = локально)
    QVector<QVector<float>> histograms; // LBP-гистограммы образцов

    QJsonObject toJson() const;
    static KnownFace fromJson(const QJsonObject& obj);

    // Подпись для рамки: "Имя, 25, владелец" (пропуская неизвестное)
    QString overlayLabel() const;
};

class FaceRegistry : public QObject
{
    Q_OBJECT

public:
    static FaceRegistry& instance();

    // Все известные лица (локальные + принятые по мешу)
    QList<KnownFace> allFaces() const;

    // Создать/обновить профиль по имени+origin. Возвращает id.
    qint64 upsertFace(const KnownFace& face);

    bool removeFace(qint64 id);

    // Локально обученные лица — для рассылки по мешу
    QJsonArray localFacesJson() const;

    // Импорт профилей, принятых по мешу от узла fromNode.
    // Возвращает число импортированных/обновлённых профилей.
    int importFromJson(const QJsonArray& faces, const QString& fromNode);

signals:
    void registryChanged();

private:
    explicit FaceRegistry(QObject* parent = nullptr);
    void ensureTable();
};
