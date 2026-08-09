#pragma once
// ============================================================
// training_center_dialog.h — Training & Dataset Center
//
// Consolidates what used to be three separate ad-hoc QDialogs
// (Dataset Statistics, Train Local Model, App Usage Patterns)
// plus chat-history search into one QML-driven dashboard —
// same split as TaskManagerDialog: this class owns the data and
// side effects (DB queries, LocalTrainer, export, toggles),
// TrainingCenter.qml owns everything about how it looks.
// ============================================================

#include <QDialog>
#include <QVBoxLayout>

class QQuickWidget;
class PassiveListener;
class ScreenshotLearner;
class LocalTrainer;

class TrainingCenterDialog : public QDialog
{
    Q_OBJECT
public:
    // initialTab: 0=Overview, 1=App Usage, 2=Local Training, 3=History,
    // 4=Synapse Graph — lets each old menu entry land on the tab it used
    // to be its own dialog.
    explicit TrainingCenterDialog(qint64 userId,
                                  PassiveListener* passive,
                                  ScreenshotLearner* appLearner,
                                  QWidget* parent = nullptr,
                                  int initialTab = 0);

    // Invoked from TrainingCenter.qml via the "trainingCenter" context property.
    Q_INVOKABLE void refreshStats();
    Q_INVOKABLE void exportDataset();
    Q_INVOKABLE void toggleRecording(bool on);
    Q_INVOKABLE void toggleAppLearning(bool on);
    Q_INVOKABLE void clearAppUsageData();
    Q_INVOKABLE void selectModel(const QString& model);
    Q_INVOKABLE void setMaxExamples(int n);
    Q_INVOKABLE void startTraining();
    Q_INVOKABLE void searchHistory(const QString& query);

private:
    void refreshOverview();
    void refreshAppUsage();
    void refreshTrainingTab();
    void refreshSynapseGraph();

    qint64             m_userId;
    PassiveListener*   m_passive    = nullptr;
    ScreenshotLearner* m_appLearner = nullptr;
    LocalTrainer*      m_trainer    = nullptr;
    QQuickWidget*      m_view       = nullptr;
    QString            m_selectedModel = QStringLiteral("llama3.2:3b");
    int                m_maxExamples   = 80;
};
