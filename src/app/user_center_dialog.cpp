// ============================================================
// user_center_dialog.cpp — User Center (QML)
// ============================================================

#include "user_center_dialog.h"
#include "database_manager.h"
#include "user_profile_extended.h"
#include "profile_setup_dialog.h"
#include "jarvis.h"
#include "activity_tracker.h"
#include "session_memory.h"
#include "lang.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QCoreApplication>
#include <QMessageBox>
#include <QSettings>
#include <QSqlQuery>
#include <QSqlDatabase>

UserCenterDialog::UserCenterDialog(Jarvis* jarvis, bool english, QWidget* parent)
    : QDialog(parent)
    , m_jarvis(jarvis)
    , m_english(english)
{
    setWindowTitle(QStringLiteral("JARVIS — User Center"));
    setMinimumSize(820, 580);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_view = new QQuickWidget(this);
    m_view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_view->setClearColor(QColor(0x0B, 0x0C, 0x10));
    m_view->engine()->addImportPath(QCoreApplication::applicationDirPath()
                                    + QStringLiteral("/qml"));

    // Safe defaults for every property UserCenter.qml reads — must exist
    // before setSource() so the initial bindings don't evaluate against
    // undefined/null. Real data is filled in by refresh() right after.
    // NB: bare integer 0 literals below are wrapped in QVariant() — passed
    // bare, C++ overload resolution prefers QQmlContext::setContextProperty
    // (name, QObject*) over the QVariant overload (0 is a
    // null-pointer-constant), silently registering the property as a null
    // QObject instead of an int.
    QQmlContext* ctx = m_view->rootContext();
    ctx->setContextProperty(QStringLiteral("ucEnglish"), m_english);
    ctx->setContextProperty(QStringLiteral("users"), QVariantList());
    ctx->setContextProperty(QStringLiteral("currentUserId"), QVariant(0));
    ctx->setContextProperty(QStringLiteral("currentUserName"), QString());
    ctx->setContextProperty(QStringLiteral("currentUserRole"), QString());
    ctx->setContextProperty(QStringLiteral("currentUserLanguage"), QString());
    ctx->setContextProperty(QStringLiteral("detectedRole"), QString());
    ctx->setContextProperty(QStringLiteral("activitySummary"), QString());
    ctx->setContextProperty(QStringLiteral("knowledgeSummary"), QString());
    ctx->setContextProperty(QStringLiteral("nickname"), QString());
    ctx->setContextProperty(QStringLiteral("devStyle"), QString());
    ctx->setContextProperty(QStringLiteral("accentColor"), QStringLiteral("#66FCF1"));
    ctx->setContextProperty(QStringLiteral("activeStart"), 9);
    ctx->setContextProperty(QStringLiteral("activeEnd"), 18);
    ctx->setContextProperty(QStringLiteral("meshRole"), QStringLiteral("primary"));
    ctx->setContextProperty(QStringLiteral("userCenter"), this);

    m_view->setSource(QUrl(QStringLiteral("qrc:/qml/UserCenter.qml")));

    root->addWidget(m_view);

    refresh();

    connect(&UserProfileExtended::instance(), &UserProfileExtended::profileChanged,
            this, [this](const QString&) { refreshMyProfile(); });
}

void UserCenterDialog::refresh()
{
    refreshUsers();
    refreshMyProfile();
}

void UserCenterDialog::refreshUsers()
{
    auto& db = DatabaseManager::instance();
    const auto allUsers = db.getAllUsers();
    const qint64 curId = m_jarvis->currentUserId();

    QVariantList list;
    for (const auto& u : allUsers) {
        QVariantMap m;
        m[QStringLiteral("id")]       = u.id;
        m[QStringLiteral("name")]     = u.name;
        m[QStringLiteral("role")]     = u.scenario;
        m[QStringLiteral("language")] = u.language;
        m[QStringLiteral("isCurrent")] = (u.id == curId);
        m[QStringLiteral("lastSeen")] = u.lastSeen.isValid()
            ? u.lastSeen.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString();
        list.append(m);
    }

    QQmlContext* ctx = m_view->rootContext();
    ctx->setContextProperty(QStringLiteral("users"), list);
    ctx->setContextProperty(QStringLiteral("currentUserId"), curId);
}

void UserCenterDialog::refreshMyProfile()
{
    QQmlContext* ctx = m_view->rootContext();
    auto& db = DatabaseManager::instance();
    auto user = db.getUser(m_jarvis->currentUserId());

    ctx->setContextProperty(QStringLiteral("currentUserName"), user ? user->name : QString());
    ctx->setContextProperty(QStringLiteral("currentUserRole"), user ? user->scenario : QString());
    ctx->setContextProperty(QStringLiteral("currentUserLanguage"), user ? user->language : QString());

    if (m_jarvis->activityTracker()) {
        ctx->setContextProperty(QStringLiteral("detectedRole"),
            m_jarvis->activityTracker()->detectUserRole());
        ctx->setContextProperty(QStringLiteral("activitySummary"),
            m_jarvis->activityTracker()->recentActivitySummary(60));
        ctx->setContextProperty(QStringLiteral("knowledgeSummary"),
            m_jarvis->activityTracker()->knowledgeSummary(m_jarvis->currentUserId(), 20));
    }

    auto& ext = UserProfileExtended::instance();
    ctx->setContextProperty(QStringLiteral("nickname"), ext.nickname());
    ctx->setContextProperty(QStringLiteral("devStyle"), ext.devStyle());
    ctx->setContextProperty(QStringLiteral("accentColor"),
        ext.uiAccentColor().isEmpty() ? QStringLiteral("#66FCF1") : ext.uiAccentColor());
    ctx->setContextProperty(QStringLiteral("activeStart"), ext.activeHoursStart());
    ctx->setContextProperty(QStringLiteral("activeEnd"), ext.activeHoursEnd());
    ctx->setContextProperty(QStringLiteral("meshRole"),
        ext.meshRole().isEmpty() ? QStringLiteral("primary") : ext.meshRole());
}

// ============================================================
// Invokable from UserCenter.qml
// ============================================================

void UserCenterDialog::switchUser(qint64 userId)
{
    auto& db = DatabaseManager::instance();
    auto user = db.getUser(userId);
    if (!user) return;

    m_jarvis->setCurrentUserId(user->id);
    m_jarvis->memory()->setActiveUserName(user->name);
    db.updateUser(*user); // touch last_seen
    QSettings(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"))
        .setValue(QStringLiteral("user/currentId"), user->id);

    refresh();
    emit userSwitched();
}

void UserCenterDialog::deleteUser(qint64 userId)
{
    if (userId == 1) return; // guarded in QML too, but never trust the client
    auto& db = DatabaseManager::instance();
    if (db.getAllUsers().size() <= 1) return;

    auto user = db.getUser(userId);
    const auto r = QMessageBox::question(this,
        m_english ? QStringLiteral("Delete User") : QStringLiteral("Удалить пользователя"),
        (m_english ? QStringLiteral("Delete \"%1\"? This can't be undone.")
                   : QStringLiteral("Удалить «%1»? Это необратимо."))
            .arg(user ? user->name : QString::number(userId)),
        QMessageBox::Yes | QMessageBox::No);
    if (r != QMessageBox::Yes) return;

    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral("DELETE FROM users WHERE id=:id"));
    q.bindValue(QStringLiteral(":id"), userId);
    q.exec();

    if (m_jarvis->currentUserId() == userId) {
        m_jarvis->setCurrentUserId(1);
        m_jarvis->memory()->setActiveUserName(QString());
    }
    refresh();
    emit userSwitched();
}

void UserCenterDialog::addUser(const QString& name, const QString& role)
{
    if (name.trimmed().isEmpty() || role.trimmed().isEmpty()) return;

    DbUserProfile newUser;
    newUser.name        = name.trimmed();
    newUser.scenario     = role;
    newUser.language     = QStringLiteral("auto");
    newUser.preferences  = QStringLiteral("{}");

    auto& db = DatabaseManager::instance();
    const qint64 newId = db.addUser(newUser);
    if (newId <= 0) return;

    m_jarvis->setCurrentUserId(newId);
    m_jarvis->memory()->setActiveUserName(newUser.name);
    QSettings(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"))
        .setValue(QStringLiteral("user/currentId"), newId);

    refresh();
    emit userSwitched();
}

void UserCenterDialog::openEditProfile()
{
    auto& db = DatabaseManager::instance();
    auto user = db.getUser(m_jarvis->currentUserId());
    if (!user) return;

    ProfileSetupDialog dlg(ProfileSetupDialog::Mode::Edit, m_english, nullptr, this);
    dlg.setProfile(*user);
    if (dlg.exec() != QDialog::Accepted) return;

    DbUserProfile updated = dlg.profile();
    if (updated.name.isEmpty()) return;

    db.updateUser(updated);
    m_jarvis->memory()->setActiveUserName(updated.name);
    refresh();
    emit userSwitched();
}

void UserCenterDialog::setNickname(const QString& v)
{
    UserProfileExtended::instance().setNickname(v);
    refreshMyProfile();
}

void UserCenterDialog::setDevStyle(const QString& v)
{
    UserProfileExtended::instance().setDevStyle(v);
    refreshMyProfile();
}

void UserCenterDialog::setAccentColor(const QString& v)
{
    UserProfileExtended::instance().setUiAccentColor(v);
    refreshMyProfile();
}

void UserCenterDialog::setActiveHours(int start, int end)
{
    UserProfileExtended::instance().setActiveHoursStart(start);
    UserProfileExtended::instance().setActiveHoursEnd(end);
    refreshMyProfile();
}

void UserCenterDialog::setMeshRole(const QString& v)
{
    UserProfileExtended::instance().setMeshRole(v);
    refreshMyProfile();
}
