// ============================================================
// face_registry.cpp — Реестр известных лиц
// ============================================================

#include "face_registry.h"

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QJsonDocument>
#include <QDebug>

// ── Сериализация гистограмм ─────────────────────────────────

static QJsonArray histogramsToJson(const QVector<QVector<float>>& hists)
{
    QJsonArray arr;
    for (const auto& h : hists) {
        QJsonArray inner;
        for (float v : h) inner.append(static_cast<double>(v));
        arr.append(inner);
    }
    return arr;
}

static QVector<QVector<float>> histogramsFromJson(const QJsonArray& arr)
{
    QVector<QVector<float>> hists;
    hists.reserve(arr.size());
    for (const auto& v : arr) {
        const QJsonArray inner = v.toArray();
        QVector<float> h;
        h.reserve(inner.size());
        for (const auto& x : inner) h.append(static_cast<float>(x.toDouble()));
        if (!h.isEmpty()) hists.append(h);
    }
    return hists;
}

QJsonObject KnownFace::toJson() const
{
    return QJsonObject{
        {QStringLiteral("name"),       name},
        {QStringLiteral("age"),        age},
        {QStringLiteral("status"),     status},
        {QStringLiteral("originNode"), originNode},
        {QStringLiteral("histograms"), histogramsToJson(histograms)},
    };
}

KnownFace KnownFace::fromJson(const QJsonObject& obj)
{
    KnownFace f;
    f.name       = obj[QStringLiteral("name")].toString();
    f.age        = obj[QStringLiteral("age")].toInt();
    f.status     = obj[QStringLiteral("status")].toString();
    f.originNode = obj[QStringLiteral("originNode")].toString();
    f.histograms = histogramsFromJson(obj[QStringLiteral("histograms")].toArray());
    return f;
}

QString KnownFace::overlayLabel() const
{
    QStringList parts;
    parts << (name.isEmpty() ? QStringLiteral("?") : name);
    if (age > 0) parts << QString::number(age);
    if (!status.isEmpty()) parts << status;
    return parts.join(QStringLiteral(", "));
}

// ── FaceRegistry ────────────────────────────────────────────

FaceRegistry& FaceRegistry::instance()
{
    static FaceRegistry reg;
    return reg;
}

FaceRegistry::FaceRegistry(QObject* parent)
    : QObject(parent)
{
    ensureTable();
}

void FaceRegistry::ensureTable()
{
    QSqlQuery q(QSqlDatabase::database());
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS known_faces ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name        TEXT NOT NULL,"
        "  age         INTEGER NOT NULL DEFAULT 0,"
        "  status      TEXT NOT NULL DEFAULT '',"
        "  origin_node TEXT NOT NULL DEFAULT '',"
        "  histograms  TEXT NOT NULL DEFAULT '[]',"
        "  updated_at  TEXT NOT NULL DEFAULT (datetime('now')),"
        "  UNIQUE(name, origin_node)"
        ")"));
}

QList<KnownFace> FaceRegistry::allFaces() const
{
    QList<KnownFace> result;
    QSqlQuery q(QSqlDatabase::database());
    if (!q.exec(QStringLiteral(
            "SELECT id, name, age, status, origin_node, histograms "
            "FROM known_faces")))
        return result;

    while (q.next()) {
        KnownFace f;
        f.id         = q.value(0).toLongLong();
        f.name       = q.value(1).toString();
        f.age        = q.value(2).toInt();
        f.status     = q.value(3).toString();
        f.originNode = q.value(4).toString();
        const QJsonDocument doc =
            QJsonDocument::fromJson(q.value(5).toString().toUtf8());
        f.histograms = histogramsFromJson(doc.array());
        result.append(f);
    }
    return result;
}

qint64 FaceRegistry::upsertFace(const KnownFace& face)
{
    if (face.name.isEmpty()) return -1;

    // Сливаем с уже накопленными образцами того же человека (тот же
    // name+origin), а не затираем их — иначе повторное обучение по фото
    // после обучения с камеры (или наоборот) стирает прежние образцы.
    // Кап на общее число образцов защищает строку от неограниченного роста.
    QVector<QVector<float>> merged = face.histograms;
    {
        QSqlQuery sel(QSqlDatabase::database());
        sel.prepare(QStringLiteral(
            "SELECT histograms FROM known_faces WHERE name = :name AND origin_node = :origin"));
        sel.bindValue(QStringLiteral(":name"),   face.name);
        sel.bindValue(QStringLiteral(":origin"), face.originNode);
        if (sel.exec() && sel.next()) {
            const QJsonDocument doc =
                QJsonDocument::fromJson(sel.value(0).toString().toUtf8());
            merged = histogramsFromJson(doc.array()) + merged;
        }
    }
    constexpr int MAX_SAMPLES = 60;
    if (merged.size() > MAX_SAMPLES)
        merged = merged.mid(merged.size() - MAX_SAMPLES);

    const QString histJson = QString::fromUtf8(
        QJsonDocument(histogramsToJson(merged))
            .toJson(QJsonDocument::Compact));

    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "INSERT INTO known_faces (name, age, status, origin_node, histograms) "
        "VALUES (:name, :age, :status, :origin, :hists) "
        "ON CONFLICT(name, origin_node) DO UPDATE SET "
        "  age        = excluded.age, "
        "  status     = excluded.status, "
        "  histograms = excluded.histograms, "
        "  updated_at = datetime('now')"));
    q.bindValue(QStringLiteral(":name"),   face.name);
    q.bindValue(QStringLiteral(":age"),    face.age);
    q.bindValue(QStringLiteral(":status"), face.status);
    q.bindValue(QStringLiteral(":origin"), face.originNode);
    q.bindValue(QStringLiteral(":hists"),  histJson);
    if (!q.exec()) {
        qWarning() << "[FaceRegistry] upsert failed:" << q.lastError().text();
        return -1;
    }

    emit registryChanged();
    emit faceEnrolled(face.name, face.status);
    qDebug() << "[FaceRegistry] Upserted face:" << face.name
             << "samples:" << merged.size()
             << "origin:" << (face.originNode.isEmpty()
                              ? QStringLiteral("local") : face.originNode);
    return q.lastInsertId().toLongLong();
}

bool FaceRegistry::removeFace(qint64 id)
{
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral("DELETE FROM known_faces WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    const bool ok = q.exec() && q.numRowsAffected() > 0;
    if (ok) emit registryChanged();
    return ok;
}

QJsonArray FaceRegistry::localFacesJson() const
{
    QJsonArray arr;
    for (const auto& f : allFaces()) {
        if (f.originNode.isEmpty())
            arr.append(f.toJson());
    }
    return arr;
}

int FaceRegistry::importFromJson(const QJsonArray& faces, const QString& fromNode)
{
    int imported = 0;
    for (const auto& v : faces) {
        KnownFace f = KnownFace::fromJson(v.toObject());
        if (f.name.isEmpty() || f.histograms.isEmpty()) continue;
        // Профиль числится за узлом-источником, чтобы:
        //  a) не конфликтовать с локальными именами;
        //  b) не рассылать чужие профили дальше (localFacesJson).
        f.originNode = f.originNode.isEmpty() ? fromNode : f.originNode;
        if (upsertFace(f) >= 0) ++imported; // при UPDATE lastInsertId может быть 0
    }
    if (imported > 0)
        qDebug() << "[FaceRegistry] Imported" << imported
                 << "face profile(s) from" << fromNode;
    return imported;
}
