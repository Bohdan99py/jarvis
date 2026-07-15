#pragma once
// -------------------------------------------------------
// skills_dialog.h — Управление модульными скиллами JARVIS
//
// Список лего-блоков знаний с чекбоксами: включение/выключение
// на лету, импорт новых скиллов из папки, открытие папки скиллов.
// -------------------------------------------------------

#include <QDialog>

class SkillManager;
class QListWidget;
class QListWidgetItem;
class QLabel;

class SkillsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SkillsDialog(SkillManager* skills, QWidget* parent = nullptr);

private:
    void reloadList();
    void onItemChanged(QListWidgetItem* item);
    void onSelectionChanged();
    void onImportClicked();

    SkillManager* m_skills = nullptr;
    QListWidget*  m_list   = nullptr;
    QLabel*       m_description = nullptr;
    bool          m_reloading   = false; // guard: itemChanged во время reloadList
};
