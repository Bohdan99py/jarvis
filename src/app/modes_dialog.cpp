// ============================================================
// modes_dialog.cpp — Work modes dialog (QML).
// ============================================================

#include "modes_dialog.h"

#include "jarvis.h"
#include "mode_manager.h"
#include "skill_manager.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QCoreApplication>
#include <QVBoxLayout>
#include <QVariantList>
#include <QVariantMap>

ModesDialog::ModesDialog(Jarvis* jarvis, bool english, QWidget* parent)
    : QDialog(parent)
    , m_jarvis(jarvis)
    , m_english(english)
{
    setWindowTitle(english ? QStringLiteral("JARVIS — Work Modes")
                           : QStringLiteral("JARVIS — Режимы работы"));
    setMinimumSize(880, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_view = new QQuickWidget(this);
    m_view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_view->setClearColor(QColor(0x0B, 0x0C, 0x10));
    m_view->engine()->addImportPath(QCoreApplication::applicationDirPath()
                                    + QStringLiteral("/qml"));

    QQmlContext* ctx = m_view->rootContext();
    ctx->setContextProperty(QStringLiteral("modesEnglish"), m_english);
    ctx->setContextProperty(QStringLiteral("modesList"), QVariantList());
    ctx->setContextProperty(QStringLiteral("activeId"), QString());
    ctx->setContextProperty(QStringLiteral("modesCtl"), this);

    m_view->setSource(QUrl(QStringLiteral("qrc:/qml/Modes.qml")));

    root->addWidget(m_view);

    // Первичное заполнение + реакция на изменения снаружи (например,
    // если режим сменили из другой части UI).
    pushToQml();
    if (auto* mm = m_jarvis ? m_jarvis->modeManager() : nullptr) {
        connect(mm, &ModeManager::modeChanged, this, &ModesDialog::pushToQml);
    }
}

void ModesDialog::refresh()
{
    if (auto* mm = m_jarvis ? m_jarvis->modeManager() : nullptr) {
        mm->scan();
    }
    pushToQml();
}

void ModesDialog::activate(const QString& modeId)
{
    if (auto* mm = m_jarvis ? m_jarvis->modeManager() : nullptr) {
        mm->activate(modeId);
    }
    // pushToQml() будет вызван через modeChanged().
}

void ModesDialog::pushToQml()
{
    auto* mm = m_jarvis ? m_jarvis->modeManager() : nullptr;
    if (!mm || !m_view) return;

    QVariantList list;
    for (const ModeInfo& m : mm->modes()) {
        QVariantMap x;
        x[QStringLiteral("id")]            = m.id;
        x[QStringLiteral("name")]          = m.displayName(m_english);
        x[QStringLiteral("description")]   = m.description(m_english);
        x[QStringLiteral("icon")]          = m.icon;
        x[QStringLiteral("accent")]        = m.accentColor;
        x[QStringLiteral("enableSkills")]  = QVariant::fromValue(m.enableSkills);
        x[QStringLiteral("disableSkills")] = QVariant::fromValue(m.disableSkills);
        x[QStringLiteral("exclusive")]     = m.exclusive;
        x[QStringLiteral("active")]        = (m.id == mm->activeId());
        list.append(x);
    }

    QQmlContext* ctx = m_view->rootContext();
    ctx->setContextProperty(QStringLiteral("modesList"), list);
    ctx->setContextProperty(QStringLiteral("activeId"),  mm->activeId());
}
