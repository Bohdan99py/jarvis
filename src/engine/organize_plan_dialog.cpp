// ============================================================
// organize_plan_dialog.cpp — File Organizer plan review + rules editor
// ============================================================

#include "organize_plan_dialog.h"
#include "jarvis.h"
#include "notification_manager.h"
#include "lang.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QCoreApplication>
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>

// ============================================================

OrganizePlanDialog::OrganizePlanDialog(Jarvis* jarvis, const OrganizePlan& plan,
                                       QWidget* parent, int initialTab)
    : QDialog(parent), m_jarvis(jarvis), m_plan(plan)
{
    setWindowTitle(IS_EN ? QStringLiteral("JARVIS — Organize Files")
                          : QStringLiteral("JARVIS — Организация файлов"));
    setMinimumSize(760, 540);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_view = new QQuickWidget(this);
    m_view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_view->setClearColor(QColor(0x0B, 0x0C, 0x10));

    // See notification_manager.cpp for why this is needed: windeployqt
    // drops QML plugin deps under bin/qml/, which isn't on the engine's
    // default import search path.
    m_view->engine()->addImportPath(QCoreApplication::applicationDirPath()
                                    + QStringLiteral("/qml"));

    QQmlContext* ctx = m_view->rootContext();
    ctx->setContextProperty(QStringLiteral("opEnglish"), IS_EN);
    ctx->setContextProperty(QStringLiteral("initialTab"), QVariant(initialTab));
    ctx->setContextProperty(QStringLiteral("organizePanel"), this);

    m_view->setSource(QUrl(QStringLiteral("qrc:/qml/OrganizePanel.qml")));

    root->addWidget(m_view);

    refreshContext();
}

// ============================================================
// Context refresh
// ============================================================

void OrganizePlanDialog::refreshContext()
{
    refreshPlanContext();
    refreshRulesContext();
}

void OrganizePlanDialog::refreshPlanContext()
{
    QVariantList items;
    for (const auto& item : m_plan.items) {
        QVariantMap m;
        m[QStringLiteral("fileName")]    = item.fileName;
        m[QStringLiteral("category")]    = item.category;
        m[QStringLiteral("subcategory")] = item.subcategory;
        m[QStringLiteral("confident")]   = item.confident;
        m[QStringLiteral("sizeBytes")]   = item.sizeBytes;
        items.append(m);
    }

    QVariantList counts;
    for (const auto& pair : m_plan.categoryCounts()) {
        QVariantMap m;
        m[QStringLiteral("category")] = pair.first;
        m[QStringLiteral("count")]    = pair.second;
        counts.append(m);
    }

    QStringList categoryNames;
    for (const auto& r : FileOrganizer::instance().rules())
        categoryNames.append(r.category);
    categoryNames.append(QStringLiteral("Нераспознано"));

    QQmlContext* ctx = m_view->rootContext();
    ctx->setContextProperty(QStringLiteral("opTargetFolder"), m_plan.targetFolder);
    ctx->setContextProperty(QStringLiteral("opPlanItems"), items);
    ctx->setContextProperty(QStringLiteral("opCategoryCounts"), counts);
    ctx->setContextProperty(QStringLiteral("opCategoryNames"), categoryNames);
}

void OrganizePlanDialog::refreshRulesContext()
{
    QVariantList rules;
    for (const auto& r : FileOrganizer::instance().rules()) {
        QVariantMap m;
        m[QStringLiteral("category")]      = r.category;
        m[QStringLiteral("extensions")]    = r.extensions.join(QStringLiteral(", "));
        m[QStringLiteral("contextAware")]  = r.contextAware;
        m[QStringLiteral("subcategories")] = r.subcategories.join(QStringLiteral(", "));
        rules.append(m);
    }

    m_view->rootContext()->setContextProperty(QStringLiteral("opRules"), rules);
}

// ============================================================
// Invokable from OrganizePanel.qml
// ============================================================

void OrganizePlanDialog::apply()
{
    if (!m_jarvis) { reject(); return; }

    const QString batchId = m_jarvis->organizeApplyPlan(m_plan);
    NotificationManager::instance().showNotification(
        !batchId.isEmpty()
            ? (IS_EN ? QStringLiteral("Files organized") : QStringLiteral("Файлы организованы"))
            : (IS_EN ? QStringLiteral("Nothing moved") : QStringLiteral("Ничего не перемещено")),
        m_plan.targetFolder,
        !batchId.isEmpty() ? NotificationManager::Level::Success : NotificationManager::Level::Info);

    accept();
}

void OrganizePlanDialog::cancelDialog()
{
    reject();
}

void OrganizePlanDialog::undoLast()
{
    if (!m_jarvis) return;
    const bool ok = m_jarvis->organizeUndoLast();
    NotificationManager::instance().showNotification(
        IS_EN ? QStringLiteral("Undo") : QStringLiteral("Отмена"),
        ok ? (IS_EN ? QStringLiteral("Last organize batch undone.")
                    : QStringLiteral("Последняя организация отменена."))
           : (IS_EN ? QStringLiteral("Nothing to undo.") : QStringLiteral("Нечего отменять.")),
        ok ? NotificationManager::Level::Success : NotificationManager::Level::Info);
}

void OrganizePlanDialog::setItemCategory(int index, const QString& category)
{
    if (index < 0 || index >= m_plan.items.size()) return;

    OrganizeItem& item = m_plan.items[index];
    item.category    = category;
    item.subcategory.clear();
    item.confident    = (category != QStringLiteral("Нераспознано"));

    refreshPlanContext();
}

void OrganizePlanDialog::addRule(const QString& category)
{
    const QString trimmed = category.trimmed();
    if (trimmed.isEmpty()) return;

    auto rules = FileOrganizer::instance().rules();
    for (const auto& r : rules) {
        if (r.category == trimmed) return; // already exists
    }
    OrganizeRule r;
    r.category = trimmed;
    rules.append(r);
    FileOrganizer::instance().setRules(rules);
    refreshRulesContext();
}

void OrganizePlanDialog::removeRule(const QString& category)
{
    auto rules = FileOrganizer::instance().rules();
    for (int i = 0; i < rules.size(); ++i) {
        if (rules[i].category == category) { rules.remove(i); break; }
    }
    FileOrganizer::instance().setRules(rules);
    refreshRulesContext();
}

void OrganizePlanDialog::updateRule(const QString& category, const QString& extensionsCsv,
                                    bool contextAware, const QString& subcategoriesCsv)
{
    auto splitCsv = [](const QString& csv) {
        QStringList out;
        for (const QString& part : csv.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString t = part.trimmed();
            if (!t.isEmpty()) out.append(t);
        }
        return out;
    };

    auto rules = FileOrganizer::instance().rules();
    for (auto& r : rules) {
        if (r.category != category) continue;
        QStringList exts = splitCsv(extensionsCsv);
        for (QString& e : exts) e = e.toLower();
        r.extensions    = exts;
        r.contextAware  = contextAware;
        r.subcategories = contextAware ? splitCsv(subcategoriesCsv) : QStringList();
        break;
    }
    FileOrganizer::instance().setRules(rules);
    refreshRulesContext();
}

void OrganizePlanDialog::resetRules()
{
    FileOrganizer::instance().resetRulesToDefault();
    refreshRulesContext();
}

void OrganizePlanDialog::rescan()
{
    if (!m_jarvis || m_plan.targetFolder.isEmpty()) return;

    FileOrganizer::instance().setLlmApi(m_jarvis->claudeApi());
    FileOrganizer::instance().buildPlan(m_plan.targetFolder, [this](const OrganizePlan& plan) {
        m_plan = plan;
        refreshPlanContext();
    });
}
