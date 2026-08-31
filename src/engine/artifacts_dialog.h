#pragma once
// ============================================================
// artifacts_dialog.h — «Файлы от Джарвиса»
//
// Одно окно на всё, что ассистент создал: схемы KiCad, диаграммы,
// скриншоты, выгрузки, кадры с камеры. Слева список, справа просмотр,
// кнопки «Открыть» и «Показать в папке».
//
// Почему виджеты, а не QML, как Vision/Training Center: здесь нужен
// просмотр произвольных файлов и запуск их системным приложением
// (QDesktopServices), а не собственная визуализация данных. QML дал бы
// красивее список и ничего не дал бы для главного — показать картинку,
// текст схемы и отдать файл в проводник.
// ============================================================

#include <QDialog>
#include <QList>

#include "artifact_registry.h"

class QListWidget;
class QLabel;
class QPlainTextEdit;
class QStackedWidget;
class QPushButton;

class ArtifactsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ArtifactsDialog(QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void reload();
    void showSelected();
    void openSelected();
    void revealSelected();
    void forgetSelected();

    // Текущий выбранный артефакт; id == 0 — ничего не выбрано.
    ArtifactRegistry::Artifact current() const;

    QListWidget*    m_list    = nullptr;
    QStackedWidget* m_preview = nullptr;
    QLabel*         m_image   = nullptr;   // страница 0 — картинки
    QPlainTextEdit* m_text    = nullptr;   // страница 1 — текстовые файлы
    QLabel*         m_info    = nullptr;
    QPushButton*    m_openBtn = nullptr;
    QPushButton*    m_revealBtn = nullptr;
    QPushButton*    m_forgetBtn = nullptr;

    QList<ArtifactRegistry::Artifact> m_items;
    QImage m_currentImage;   // для пересчёта масштаба при изменении размера
};
