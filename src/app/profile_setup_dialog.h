#pragma once
// -------------------------------------------------------
// profile_setup_dialog.h — Мастер профиля пользователя
//
// Используется в двух режимах:
//  - FirstRun: первый запуск на новом ПК — спрашиваем имя и роль
//    вместо дефолтного имени автора.
//  - Edit: редактирование профиля текущего пользователя из меню.
// -------------------------------------------------------

#include <QDialog>
#include <QList>
#include <QPair>

#include "database_manager.h"

class QLineEdit;
class QComboBox;
class QCheckBox;
class SkillManager;

class ProfileSetupDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode { FirstRun, Edit };

    // skills != nullptr в режиме FirstRun добавляет секцию выбора
    // скиллов (лего-блоков знаний): пользователь сразу решает, какие
    // модули ему нужны. Выбор применяется при нажатии "Начать".
    explicit ProfileSetupDialog(Mode mode, bool english,
                                SkillManager* skills = nullptr,
                                QWidget* parent = nullptr);

    void setProfile(const DbUserProfile& profile);
    DbUserProfile profile() const;

    // Роли: (значение, отображаемое имя). Это тот же набор, что раньше
    // жил в отдельном меню "Switch Role" (currentRole — реально влияет
    // на тон ответов, метку роли в P2P-меше и т.д.), а не декоративный
    // DbUserProfile::scenario. Один выбор роли пишется в оба поля.
    struct RoleEntry { QString value; QString label; };
    static QList<RoleEntry> knownRoles(bool english);

private:
    Mode          m_mode;
    bool          m_english;
    DbUserProfile m_profile;   // id/created сохраняются между set/get

    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_roleBox  = nullptr;
    QComboBox* m_langBox  = nullptr;

    // Выбор скиллов при первом запуске: (id скилла, чекбокс)
    SkillManager* m_skills = nullptr;
    QList<QPair<QString, QCheckBox*>> m_skillChecks;
};
