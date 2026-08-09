// ============================================================
// training_center_dialog.cpp — Training & Dataset Center
// ============================================================

#include "training_center_dialog.h"
#include "database_manager.h"
#include "local_trainer.h"
#include "passive_listener.h"
#include "screenshot_learner.h"
#include "notification_manager.h"
#include "synapse_graph.h"
#include "llm_cache_manager.h"
#include "lang.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QCoreApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>

TrainingCenterDialog::TrainingCenterDialog(qint64 userId,
                                           PassiveListener* passive,
                                           ScreenshotLearner* appLearner,
                                           QWidget* parent,
                                           int initialTab)
    : QDialog(parent)
    , m_userId(userId)
    , m_passive(passive)
    , m_appLearner(appLearner)
{
    setWindowTitle(QStringLiteral("JARVIS — Training Center"));
    setMinimumSize(900, 620);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_trainer = new LocalTrainer(this);
    connect(m_trainer, &LocalTrainer::trainingProgress, this, [this](const QString& msg) {
        QQmlContext* ctx = m_view->rootContext();
        const QString cur = ctx->contextProperty(QStringLiteral("trainingLog")).toString();
        ctx->setContextProperty(QStringLiteral("trainingLog"), cur + msg + QStringLiteral("\n"));
    });
    connect(m_trainer, &LocalTrainer::trainingFinished, this, [this](bool success, const QString& msg) {
        QQmlContext* ctx = m_view->rootContext();
        ctx->setContextProperty(QStringLiteral("trainingActive"), false);
        const QString cur = ctx->contextProperty(QStringLiteral("trainingLog")).toString();
        ctx->setContextProperty(QStringLiteral("trainingLog"),
            cur + (success ? QStringLiteral("\n🎉 ") : QStringLiteral("\n❌ ")) + msg);

        NotificationManager::instance().showNotification(
            success ? (IS_EN ? QStringLiteral("Training complete") : QStringLiteral("Обучение завершено"))
                    : (IS_EN ? QStringLiteral("Training failed") : QStringLiteral("Ошибка обучения")),
            msg,
            success ? NotificationManager::Level::Success : NotificationManager::Level::Error);
    });

    m_view = new QQuickWidget(this);
    m_view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_view->setClearColor(QColor(0x0B, 0x0C, 0x10));
    m_view->engine()->addImportPath(QCoreApplication::applicationDirPath()
                                    + QStringLiteral("/qml"));

    // Safe defaults for every property TrainingCenter.qml reads — must exist
    // before setSource() so the initial bindings don't evaluate against
    // undefined. Real data is filled in by refreshStats() right after.
    // NB: bare integer 0 literals must be wrapped in QVariant() below —
    // passed bare, C++ overload resolution prefers QQmlContext::
    // setContextProperty(name, QObject*) over the QVariant overload (0 is
    // a null-pointer-constant), silently registering the property as a
    // null QObject instead of an int. QML then throws "Cannot assign
    // std::nullptr_t to int" the moment anything binds to it strictly-typed.
    QQmlContext* ctx = m_view->rootContext();
    ctx->setContextProperty(QStringLiteral("tcEnglish"), IS_EN);
    ctx->setContextProperty(QStringLiteral("initialTab"), initialTab);
    ctx->setContextProperty(QStringLiteral("datasetTotal"), QVariant(0));
    ctx->setContextProperty(QStringLiteral("datasetLiked"), QVariant(0));
    ctx->setContextProperty(QStringLiteral("datasetGoal"), 500);
    ctx->setContextProperty(QStringLiteral("datasetProgress"), 0.0);
    ctx->setContextProperty(QStringLiteral("journalTotal"), QVariant(0));
    ctx->setContextProperty(QStringLiteral("journalDone"), QVariant(0));
    ctx->setContextProperty(QStringLiteral("journalPending"), QVariant(0));
    ctx->setContextProperty(QStringLiteral("recordingActive"), false);
    ctx->setContextProperty(QStringLiteral("appUsageTotal"), QVariant(0));
    ctx->setContextProperty(QStringLiteral("appLearningEnabled"), false);
    ctx->setContextProperty(QStringLiteral("appPredictions"), QVariantList());
    ctx->setContextProperty(QStringLiteral("appToday"), QVariantList());
    ctx->setContextProperty(QStringLiteral("ollamaAvailable"), false);
    ctx->setContextProperty(QStringLiteral("likedCount"), QVariant(0));
    ctx->setContextProperty(QStringLiteral("modelOptions"), QStringList());
    ctx->setContextProperty(QStringLiteral("selectedModel"), m_selectedModel);
    ctx->setContextProperty(QStringLiteral("maxExamples"), m_maxExamples);
    ctx->setContextProperty(QStringLiteral("trainingActive"), false);
    ctx->setContextProperty(QStringLiteral("trainingLog"), QString());
    ctx->setContextProperty(QStringLiteral("historyResults"), QVariantList());
    ctx->setContextProperty(QStringLiteral("synapseNodeCount"), QVariant(0));
    ctx->setContextProperty(QStringLiteral("synapseEdgeCount"), QVariant(0));
    ctx->setContextProperty(QStringLiteral("synapseTopNodes"), QVariantList());
    ctx->setContextProperty(QStringLiteral("synapseTopEdges"), QVariantList());
    ctx->setContextProperty(QStringLiteral("trainingCenter"), this);

    m_view->setSource(QUrl(QStringLiteral("qrc:/qml/TrainingCenter.qml")));

    root->addWidget(m_view);

    refreshStats();
}

void TrainingCenterDialog::refreshStats()
{
    refreshOverview();
    refreshAppUsage();
    refreshTrainingTab();
    refreshSynapseGraph();
}

void TrainingCenterDialog::refreshOverview()
{
    auto& db = DatabaseManager::instance();
    const int total  = db.trainingLogCount(m_userId);
    const int liked  = db.trainingLogCount(m_userId, 1);
    const int jTotal = db.voiceJournalCount(m_userId, false);
    const int jDone  = db.voiceJournalCount(m_userId, true);
    const bool recording = m_passive && m_passive->isListening();
    const int goal = 500;

    QQmlContext* ctx = m_view->rootContext();
    ctx->setContextProperty(QStringLiteral("datasetTotal"), total);
    ctx->setContextProperty(QStringLiteral("datasetLiked"), liked);
    ctx->setContextProperty(QStringLiteral("datasetGoal"), goal);
    ctx->setContextProperty(QStringLiteral("datasetProgress"), qMin(1.0, double(total) / goal));
    ctx->setContextProperty(QStringLiteral("journalTotal"), jTotal);
    ctx->setContextProperty(QStringLiteral("journalDone"), jDone);
    ctx->setContextProperty(QStringLiteral("journalPending"), qMax(0, jTotal - jDone));
    ctx->setContextProperty(QStringLiteral("recordingActive"), recording);
}

void TrainingCenterDialog::refreshAppUsage()
{
    QQmlContext* ctx = m_view->rootContext();
    const int total = m_appLearner ? m_appLearner->totalRecords() : 0;
    ctx->setContextProperty(QStringLiteral("appUsageTotal"), total);
    ctx->setContextProperty(QStringLiteral("appLearningEnabled"),
                            m_appLearner && m_appLearner->isEnabled());

    QVariantList predictions;
    if (m_appLearner) {
        for (const auto& s : m_appLearner->suggestionsForNow(6)) {
            QVariantMap m;
            m[QStringLiteral("name")]       = s.appName;
            m[QStringLiteral("frequency")]  = s.frequency;
            m[QStringLiteral("confidence")] = double(s.confidence);
            predictions.append(m);
        }
    }
    ctx->setContextProperty(QStringLiteral("appPredictions"), predictions);

    QVariantList today;
    if (m_appLearner) {
        const auto stats = m_appLearner->todayStats();
        int maxMinutes = 1;
        for (const auto& s : stats) maxMinutes = qMax(maxMinutes, s.totalMinutes);
        for (const auto& s : stats) {
            QVariantMap m;
            m[QStringLiteral("name")]     = s.appName;
            m[QStringLiteral("minutes")]  = s.totalMinutes;
            m[QStringLiteral("sessions")] = s.sessionCount;
            m[QStringLiteral("fraction")] = double(s.totalMinutes) / maxMinutes;
            today.append(m);
        }
    }
    ctx->setContextProperty(QStringLiteral("appToday"), today);
}

void TrainingCenterDialog::refreshTrainingTab()
{
    QQmlContext* ctx = m_view->rootContext();
    const bool available = m_trainer && m_trainer->isOllamaAvailable();
    ctx->setContextProperty(QStringLiteral("ollamaAvailable"), available);
    ctx->setContextProperty(QStringLiteral("likedCount"),
                            DatabaseManager::instance().trainingLogCount(m_userId));

    QStringList options = { QStringLiteral("llama3.2:3b"), QStringLiteral("llama3.2:1b"),
                            QStringLiteral("mistral:7b"), QStringLiteral("phi3:mini") };
    if (m_trainer) {
        for (const QString& m : m_trainer->installedModels())
            if (!options.contains(m)) options.prepend(m);
    }
    ctx->setContextProperty(QStringLiteral("modelOptions"), options);
    ctx->setContextProperty(QStringLiteral("selectedModel"), m_selectedModel);
    ctx->setContextProperty(QStringLiteral("maxExamples"), m_maxExamples);
}

void TrainingCenterDialog::refreshSynapseGraph()
{
    QQmlContext* ctx = m_view->rootContext();
    auto& graph = SynapseGraph::instance();
    const auto stats = graph.stats(LlmCacheManager::kDesktopOwnerId);

    ctx->setContextProperty(QStringLiteral("synapseNodeCount"), stats.nodeCount);
    ctx->setContextProperty(QStringLiteral("synapseEdgeCount"), stats.edgeCount);

    QVariantList nodes;
    const auto topNodes = graph.topNodes(LlmCacheManager::kDesktopOwnerId, 12);
    int maxActivations = 1;
    for (const auto& n : topNodes) maxActivations = qMax(maxActivations, n.activations);
    for (const auto& n : topNodes) {
        QVariantMap m;
        m[QStringLiteral("label")]       = n.label;
        m[QStringLiteral("activations")] = n.activations;
        m[QStringLiteral("fraction")]    = double(n.activations) / maxActivations;
        nodes.append(m);
    }
    ctx->setContextProperty(QStringLiteral("synapseTopNodes"), nodes);

    QVariantList edges;
    for (const auto& e : graph.topEdges(LlmCacheManager::kDesktopOwnerId, 12)) {
        QVariantMap m;
        m[QStringLiteral("labelA")]        = e.labelA;
        m[QStringLiteral("labelB")]        = e.labelB;
        m[QStringLiteral("weight")]        = double(e.weight);
        // SynapseGraph::kEdgeWeightMax — kept in sync manually since it's
        // a private tuning constant, not part of the public API surface.
        m[QStringLiteral("fraction")]      = qMin(1.0, double(e.weight) / 3.0);
        m[QStringLiteral("coActivations")] = e.coActivations;
        edges.append(m);
    }
    ctx->setContextProperty(QStringLiteral("synapseTopEdges"), edges);
}

// ============================================================
// Invokable from TrainingCenter.qml
// ============================================================

void TrainingCenterDialog::exportDataset()
{
    auto& db = DatabaseManager::instance();
    const int count = db.trainingLogCount(m_userId);

    if (count == 0) {
        QMessageBox::information(this,
            IS_EN ? QStringLiteral("Export") : QStringLiteral("Экспорт"),
            IS_EN ? QStringLiteral("No training data yet. Like some responses first (👍 button).")
                  : QStringLiteral("Нет данных для экспорта. Сначала лайкните несколько ответов (кнопка 👍)."));
        return;
    }

    const QString defaultName = QStringLiteral("jarvis_dataset_%1.jsonl")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmm")));

    const QString filePath = QFileDialog::getSaveFileName(this,
        IS_EN ? QStringLiteral("Export Training Data") : QStringLiteral("Экспорт датасета"),
        QDir::homePath() + QStringLiteral("/") + defaultName,
        QStringLiteral("JSONL Files (*.jsonl);;All Files (*)"));

    if (filePath.isEmpty()) return;

    if (db.exportToJsonl(m_userId, filePath)) {
        NotificationManager::instance().showNotification(
            IS_EN ? QStringLiteral("Dataset exported") : QStringLiteral("Датасет экспортирован"),
            (IS_EN ? QStringLiteral("%1 pairs → %2").arg(count).arg(filePath)
                   : QStringLiteral("%1 пар → %2").arg(count).arg(filePath)),
            NotificationManager::Level::Success);
    } else {
        QMessageBox::warning(this,
            IS_EN ? QStringLiteral("Export Error") : QStringLiteral("Ошибка экспорта"),
            IS_EN ? QStringLiteral("Failed to write file: ") + filePath
                  : QStringLiteral("Не удалось записать файл: ") + filePath);
    }
}

void TrainingCenterDialog::toggleRecording(bool on)
{
    if (!m_passive) return;
    if (on) m_passive->startListening();
    else    m_passive->stopListening();
    refreshOverview();
}

void TrainingCenterDialog::toggleAppLearning(bool on)
{
    if (!m_appLearner) return;
    m_appLearner->setEnabled(on);
    if (on && !m_appLearner->isRunning()) m_appLearner->start(2);
    refreshAppUsage();
}

void TrainingCenterDialog::clearAppUsageData()
{
    if (!m_appLearner) return;
    const auto r = QMessageBox::question(this,
        IS_EN ? QStringLiteral("Clear Data") : QStringLiteral("Очистить данные"),
        IS_EN ? QStringLiteral("Clear all app usage learning data? This can't be undone.")
              : QStringLiteral("Очистить все данные об использовании приложений? Это необратимо."),
        QMessageBox::Yes | QMessageBox::No);
    if (r != QMessageBox::Yes) return;

    m_appLearner->clearAllData();
    refreshAppUsage();
    NotificationManager::instance().showNotification(
        IS_EN ? QStringLiteral("App usage data cleared") : QStringLiteral("Данные использования очищены"),
        QString(), NotificationManager::Level::Warning);
}

void TrainingCenterDialog::selectModel(const QString& model)
{
    m_selectedModel = model;
    m_view->rootContext()->setContextProperty(QStringLiteral("selectedModel"), m_selectedModel);
}

void TrainingCenterDialog::setMaxExamples(int n)
{
    m_maxExamples = n;
    m_view->rootContext()->setContextProperty(QStringLiteral("maxExamples"), m_maxExamples);
}

void TrainingCenterDialog::startTraining()
{
    if (!m_trainer) return;

    if (!m_trainer->isOllamaAvailable()) {
        QMessageBox::warning(this,
            IS_EN ? QStringLiteral("Ollama Not Found") : QStringLiteral("Ollama не найдена"),
            IS_EN ? QStringLiteral("Ollama is required for local training.\n\n"
                       "Install from: https://ollama.com\nThen restart JARVIS.")
                  : QStringLiteral("Для обучения нужна Ollama.\n\n"
                       "Установите: https://ollama.com\nЗатем перезапустите JARVIS."));
        return;
    }

    QQmlContext* ctx = m_view->rootContext();
    ctx->setContextProperty(QStringLiteral("trainingActive"), true);
    ctx->setContextProperty(QStringLiteral("trainingLog"), QString());

    m_trainer->setBaseModel(m_selectedModel);
    m_trainer->setMaxPairs(m_maxExamples);
    m_trainer->train(&DatabaseManager::instance());
}

void TrainingCenterDialog::searchHistory(const QString& query)
{
    QVariantList results;
    if (!query.trimmed().isEmpty()) {
        const auto logs = DatabaseManager::instance().getTrainingLogs(m_userId, 10000);
        const QString lower = query.toLower();
        for (const DbTrainingLog& log : logs) {
            if (log.userMessage.toLower().contains(lower)
             || log.aiResponse.toLower().contains(lower)) {
                QVariantMap m;
                m[QStringLiteral("userMessage")] = log.userMessage;
                m[QStringLiteral("aiResponse")]  = log.aiResponse;
                results.append(m);
            }
            if (results.size() >= 30) break;
        }
    }
    m_view->rootContext()->setContextProperty(QStringLiteral("historyResults"), results);
}
