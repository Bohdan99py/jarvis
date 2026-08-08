#pragma once
// ============================================================
// organize_plan_dialog.h — File Organizer plan review + rules editor
//
// Same split as TaskManagerDialog/TrainingCenterDialog: this class owns
// the data (the OrganizePlan being reviewed, FileOrganizer's rule set)
// and side effects (apply/undo, rule persistence); OrganizePanel.qml
// (hosted in a QQuickWidget) owns everything about how it looks. Talks
// back to C++ only through the "organizePanel" context property.
// ============================================================

#include <QDialog>
#include <QVBoxLayout>
#include "file_organizer.h"

class QQuickWidget;
class Jarvis;

class OrganizePlanDialog : public QDialog
{
    Q_OBJECT
public:
    // initialTab: 0 = Plan, 1 = Rules. plan may be empty (e.g. opened from
    // the "Настроить категории..." menu entry, with no folder scanned yet).
    explicit OrganizePlanDialog(Jarvis* jarvis, const OrganizePlan& plan,
                                QWidget* parent = nullptr, int initialTab = 0);

    // Invoked from OrganizePanel.qml via the "organizePanel" context property.
    Q_INVOKABLE void apply();
    Q_INVOKABLE void cancelDialog();
    Q_INVOKABLE void undoLast();
    Q_INVOKABLE void setItemCategory(int index, const QString& category);
    Q_INVOKABLE void addRule(const QString& category);
    Q_INVOKABLE void removeRule(const QString& category);
    Q_INVOKABLE void updateRule(const QString& category, const QString& extensionsCsv,
                                bool contextAware, const QString& subcategoriesCsv);
    Q_INVOKABLE void resetRules();
    Q_INVOKABLE void rescan();

private:
    void refreshContext();
    void refreshPlanContext();
    void refreshRulesContext();

    Jarvis*       m_jarvis = nullptr;
    OrganizePlan  m_plan;
    QQuickWidget* m_view   = nullptr;
};
