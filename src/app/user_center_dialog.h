#pragma once
// ============================================================
// user_center_dialog.h — User Center (QML)
//
// Consolidates what used to be three separate menu flows —
// "Switch User..." (QInputDialog picker), "My Profile"
// (QMessageBox text dump), "Delete User..." (another picker) —
// plus UserProfileExtended's nickname/dev-style/accent-color/
// active-hours/mesh-role fields, which previously had NO direct
// UI at all (only ever set indirectly through CuriosityEngine's
// occasional calibration questions). One QML dashboard: user
// cards to switch/add/delete, and a profile tab with live stats
// + editable preferences. "Edit Profile..." (name/role/language/
// skills) still opens the existing ProfileSetupDialog form —
// no reason to rebuild a working multi-field form in QML.
// ============================================================

#include <QDialog>
#include <QVBoxLayout>

class QQuickWidget;
class Jarvis;

class UserCenterDialog : public QDialog
{
    Q_OBJECT
public:
    explicit UserCenterDialog(Jarvis* jarvis, bool english, QWidget* parent = nullptr);

    // Invoked from UserCenter.qml via the "userCenter" context property.
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void switchUser(qint64 userId);
    Q_INVOKABLE void deleteUser(qint64 userId);
    Q_INVOKABLE void addUser(const QString& name, const QString& role);
    Q_INVOKABLE void openEditProfile();
    Q_INVOKABLE void setNickname(const QString& v);
    Q_INVOKABLE void setDevStyle(const QString& v);
    Q_INVOKABLE void setAccentColor(const QString& v);
    Q_INVOKABLE void setActiveHours(int start, int end);
    Q_INVOKABLE void setMeshRole(const QString& v);

signals:
    void userSwitched();

private:
    void refreshUsers();
    void refreshMyProfile();

    Jarvis*       m_jarvis;
    bool          m_english;
    QQuickWidget* m_view = nullptr;
};
