#pragma once
// -------------------------------------------------------
// skills_dialog.h — Управление модульными скиллами JARVIS
//
// Дерево скиллов в стиле «Workloads» Visual Studio:
//   Разработка
//     ├─ Unreal Engine
//     │    ├─ [x] C++ API
//     │    ├─ [ ] Blueprints
//     │    └─ [ ] Editor / Pipeline
//     ├─ Unity
//     └─ Godot
//   Электроника
//     └─ [x] KiCad + ESP32
//
// Чекбоксы работают на любом уровне: отметив категорию/подкатегорию,
// пользователь включает/выключает все скиллы внутри одним кликом.
// -------------------------------------------------------

#include <QDialog>

class SkillManager;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;

class SkillsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SkillsDialog(SkillManager* skills, QWidget* parent = nullptr);

private:
    void reloadTree();
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onSelectionChanged();
    void onImportClicked();

    // Пересчёт чекбокса группы по состояниям детей (checked / unchecked /
    // partially checked). Вызывается после смены листового элемента.
    void refreshGroupCheckState(QTreeWidgetItem* group);

    SkillManager* m_skills = nullptr;
    QTreeWidget*  m_tree   = nullptr;
    QLabel*       m_description = nullptr;
    bool          m_reloading   = false; // guard: itemChanged во время reloadTree
    bool          m_syncing     = false; // guard: рекурсия при каскаде check-state
};
