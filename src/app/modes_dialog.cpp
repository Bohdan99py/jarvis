// ============================================================
// modes_dialog.cpp — Work modes dialog (QML).
// ============================================================

#include "modes_dialog.h"

#include "jarvis.h"
#include "mode_manager.h"
#include "skill_manager.h"

#include "jarvis_theme.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QLabel>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>

ModesDialog::ModesDialog(Jarvis* jarvis, bool english, QWidget* parent)
    : QDialog(parent)
    , m_jarvis(jarvis)
    , m_english(english)
{
    setWindowTitle(english ? QStringLiteral("JARVIS — Work Modes")
                           : QStringLiteral("JARVIS — Режимы работы"));
    setMinimumSize(960, 620);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Данные готовим ДО setSource: QML на старте сразу видит настоящий
    // список, а не пустой, который потом «догоняется».
    reload();

    m_view = new QQuickWidget(this);
    m_view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_view->setClearColor(QColor(0x08, 0x0A, 0x0F));
    JarvisTheme::prepareEngine(m_view->engine());

    m_view->rootContext()->setContextProperty(QStringLiteral("modesCtl"), this);
    m_view->setSource(QUrl(QStringLiteral("qrc:/qml/Modes.qml")));

    // Ошибка в QML раньше выглядела как «экран не открывается»: виджет
    // молча оставался пустым, а текст ошибки уходил только в qDebug,
    // которого в релизной сборке никто не видит. Теперь она на экране.
    if (m_view->status() == QQuickWidget::Error) {
        QStringList lines;
        for (const QQmlError& e : m_view->errors())
            lines << e.toString();
        const QString text = lines.join(QStringLiteral("\n"));
        qWarning() << "[Modes] QML failed to load:" << text;

        m_error = new QLabel(this);
        m_error->setWordWrap(true);
        m_error->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_error->setContentsMargins(16, 16, 16, 16);
        m_error->setText((english
                              ? QStringLiteral("The modes screen failed to load:\n\n")
                              : QStringLiteral("Экран режимов не загрузился:\n\n"))
                         + text);
        root->addWidget(m_error);
        m_view->hide();
        return;
    }

    root->addWidget(m_view);

    // Реакция на изменения снаружи (например, если режим сменили из
    // другой части UI).
    if (auto* mm = m_jarvis ? m_jarvis->modeManager() : nullptr) {
        connect(mm, &ModeManager::modeChanged, this, [this]() {
            reload();
            emit modesChanged();
        });
    }
}

QString ModesDialog::modesDir() const
{
    return QDir::toNativeSeparators(ModeManager::userModesDir());
}

void ModesDialog::refresh()
{
    if (auto* mm = m_jarvis ? m_jarvis->modeManager() : nullptr) {
        mm->scan();   // scan() эмитит modeChanged() → reload() + сигнал в QML
        return;
    }
    reload();
    emit modesChanged();
}

void ModesDialog::activate(const QString& modeId)
{
    if (auto* mm = m_jarvis ? m_jarvis->modeManager() : nullptr) {
        mm->activate(modeId);
        return;   // modeChanged() догонит QML
    }
    reload();
    emit modesChanged();
}

void ModesDialog::openModesFolder()
{
    const QString dir = ModeManager::userModesDir();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void ModesDialog::reload()
{
    m_modes.clear();
    m_activeId.clear();

    auto* mm = m_jarvis ? m_jarvis->modeManager() : nullptr;
    if (!mm) return;

    m_activeId = mm->activeId();
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
        x[QStringLiteral("active")]        = (m.id == m_activeId);
        m_modes.append(x);
    }
}
