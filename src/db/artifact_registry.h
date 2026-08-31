#pragma once
// ============================================================
// artifact_registry.h — что Джарвис создал
//
// Ассистент постоянно порождает файлы: схемы KiCad, отрисованные
// диаграммы, скриншоты, экспорт датасета. До сих пор такой файл жил
// ровно одно сообщение — путь уходил строкой в чат и там растворялся.
// Через десяток реплик вернуться к схеме можно было, только вспомнив
// имя файла и порывшись в проводнике.
//
// Реестр делает результат работы вещью, а не событием: файл можно
// найти, открыть и посмотреть спустя дни.
//
// Реестр НЕ копирует и не перемещает файлы — он только помнит, где
// они лежат. Поэтому запись может пережить сам файл (пользователь
// вправе его удалить), и любой потребитель обязан проверять
// существование, а не доверять пути вслепую — см. Artifact::exists().
// ============================================================

#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>

class ArtifactRegistry : public QObject
{
    Q_OBJECT

public:
    static ArtifactRegistry& instance();

    ArtifactRegistry(const ArtifactRegistry&)            = delete;
    ArtifactRegistry& operator=(const ArtifactRegistry&) = delete;

    // Виды артефактов. Строкой, а не enum: значение уходит в SQLite и
    // читается человеком при отладке, а новый вид не должен требовать
    // миграции схемы.
    static constexpr const char* kSchematic  = "schematic";  // .kicad_sch
    static constexpr const char* kDiagram    = "diagram";    // отрисованная mermaid
    static constexpr const char* kScreenshot = "screenshot";
    static constexpr const char* kExport     = "export";     // .jsonl и прочие выгрузки
    static constexpr const char* kPhoto      = "photo";      // кадры с камеры
    static constexpr const char* kOther      = "other";

    struct Artifact {
        qint64    id = 0;
        QString   path;
        QString   kind;
        QString   title;
        QString   query;      // запрос, из которого файл родился
        QDateTime createdAt;

        bool exists() const;              // файл ещё на диске?
        qint64 sizeBytes() const;         // 0, если файла нет
        bool isImage() const;             // можно показать превью как картинку
    };

    // Запоминает файл. Повторная запись того же пути обновляет заголовок
    // и время — перегенерация схемы не должна плодить дубли в списке.
    void record(const QString& path, const QString& kind,
                const QString& title = QString(),
                const QString& query = QString());

    // Свежие сверху. limit <= 0 — все.
    QList<Artifact> recent(int limit = 100) const;

    // Убирает запись из реестра. Файл на диске НЕ трогает: реестр не
    // владеет файлами, и «убрать из списка» не должно означать
    // «удалить с диска» — это разные намерения.
    bool forget(qint64 id);

    // Чистит записи, чьи файлы исчезли. Возвращает число убранных.
    int pruneMissing();

    // Подбирает файлы, созданные ДО появления реестра или в обход него.
    // Реестр наполняется в момент создания файла, поэтому всё, что
    // Джарвис сделал раньше, для окна «Файлы» не существует — включая
    // образцы лица, снятые до того, как их начали регистрировать.
    // Повторный вызов безвреден: record() обновляет запись по пути, а не
    // плодит дубликаты. Возвращает число добавленных.
    int scanKnownFolders();

    int count() const;

signals:
    void changed();

private:
    explicit ArtifactRegistry(QObject* parent = nullptr);
};
