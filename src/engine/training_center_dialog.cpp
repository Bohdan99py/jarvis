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

#include "jarvis_theme.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QCoreApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <QVector>

#include <algorithm>
#include <cmath>

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
    JarvisTheme::prepareEngine(m_view->engine());

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
    ctx->setContextProperty(QStringLiteral("synapseGraphNodes"), QVariantList());
    ctx->setContextProperty(QStringLiteral("synapseGraphEdges"), QVariantList());
    ctx->setContextProperty(QStringLiteral("synapseLegend"), QVariantList());
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

// ── Force-directed layout (Fruchterman–Reingold) ─────────────────────
// Runs in C++ rather than QML for two reasons: the whole simulation is
// ~300 iterations over ≤40 nodes (microseconds, once per refresh), and
// keeping it here leaves the QML side purely declarative — it renders
// finished coordinates instead of stepping a physics loop on the UI thread
// every frame.
//
// Seeding is a deterministic circle, never random: reopening the dialog on
// an unchanged graph must draw the same picture, otherwise the layout reads
// as noise rather than as structure the user can learn to recognize.
namespace {

struct LayoutPoint { double x = 0.0; double y = 0.0; };

QVector<LayoutPoint> computeLayout(const SynapseGraph::GraphView& g)
{
    const int n = g.nodes.size();
    QVector<LayoutPoint> pos(n);
    if (n == 0) return pos;
    if (n == 1) { pos[0] = { 0.5, 0.5 }; return pos; }

    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * M_PI * i / n;
        pos[i] = { 0.5 + 0.32 * std::cos(a), 0.5 + 0.32 * std::sin(a) };
    }

    constexpr int    kIterations = 300;
    const double     k = std::sqrt(1.0 / n);   // ideal separation in a unit square
    QVector<LayoutPoint> disp(n);

    for (int iter = 0; iter < kIterations; ++iter) {
        // Cooling schedule: large early moves untangle the initial circle,
        // small late ones settle it without jitter.
        const double temp = 0.08 * (1.0 - double(iter) / kIterations) + 0.0005;
        disp.fill({ 0.0, 0.0 });

        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                double dx = pos[i].x - pos[j].x;
                double dy = pos[i].y - pos[j].y;
                double len = std::sqrt(dx * dx + dy * dy);
                if (len < 1e-6) { dx = 1e-3 * (i + 1); dy = 1e-3 * (j + 1); len = std::sqrt(dx*dx + dy*dy); }
                const double f = (k * k) / len;
                disp[i].x += dx / len * f;  disp[i].y += dy / len * f;
                disp[j].x -= dx / len * f;  disp[j].y -= dy / len * f;
            }
        }

        for (const auto& e : g.edges) {
            if (e.a < 0 || e.b < 0) continue;
            double dx = pos[e.a].x - pos[e.b].x;
            double dy = pos[e.a].y - pos[e.b].y;
            const double len = std::max(std::sqrt(dx * dx + dy * dy), 1e-6);
            // Heavier synapses pull harder, so strongly-associated concepts
            // end up visibly clustered — that clustering IS the information.
            const double pull = g.maxWeight > 0.0f
                ? 0.5 + 0.5 * (double(e.weight) / double(g.maxWeight)) : 1.0;
            const double f = (len * len) / k * pull;
            disp[e.a].x -= dx / len * f;  disp[e.a].y -= dy / len * f;
            disp[e.b].x += dx / len * f;  disp[e.b].y += dy / len * f;
        }

        for (int i = 0; i < n; ++i) {
            const double len = std::sqrt(disp[i].x * disp[i].x + disp[i].y * disp[i].y);
            if (len > 1e-9) {
                const double step = std::min(len, temp);
                pos[i].x += disp[i].x / len * step;
                pos[i].y += disp[i].y / len * step;
            }
            // Margin keeps labels and node circles inside the viewport.
            pos[i].x = std::clamp(pos[i].x, 0.06, 0.94);
            pos[i].y = std::clamp(pos[i].y, 0.06, 0.94);
        }
    }
    return pos;
}

} // namespace

void TrainingCenterDialog::refreshSynapseGraph()
{
    QQmlContext* ctx = m_view->rootContext();
    auto& graph = SynapseGraph::instance();
    const auto stats = graph.stats(LlmCacheManager::kDesktopOwnerId);

    ctx->setContextProperty(QStringLiteral("synapseNodeCount"), stats.nodeCount);
    ctx->setContextProperty(QStringLiteral("synapseEdgeCount"), stats.edgeCount);

    const auto view = graph.graphView(LlmCacheManager::kDesktopOwnerId, 40);
    const auto pos  = computeLayout(view);

    // Where each node should appear FROM. brain-map spawns a new node at an
    // already-born neighbour's position, which is what makes the network look
    // like it grows out of itself rather than fading in as a finished
    // picture. Same idea here, but resolved once against the precomputed
    // layout instead of a live physics loop: the earliest-indexed neighbour
    // (nodes are ordered by activation, so that is the more established
    // concept) becomes the origin, and the node travels from there to its
    // real position.
    QVector<int> spawnFrom(view.nodes.size(), -1);
    for (const auto& e : view.edges) {
        const int lo = qMin(e.a, e.b);
        const int hi = qMax(e.a, e.b);
        if (spawnFrom[hi] < 0) spawnFrom[hi] = lo;
    }

    QVariantList nodes;
    QHash<QString, int> sourceCounts;
    for (int i = 0; i < view.nodes.size(); ++i) {
        const auto& n = view.nodes[i];
        const double heat = view.maxActivations > 0
            ? double(n.activations) / view.maxActivations : 0.0;
        QVariantMap m;
        // id нужен, чтобы по клику можно было спросить досье именно
        // на этот узел — без него картинка остаётся только картинкой.
        m[QStringLiteral("id")]          = n.id;
        m[QStringLiteral("label")]       = n.label;
        m[QStringLiteral("activations")] = n.activations;
        m[QStringLiteral("degree")]      = n.degree;
        m[QStringLiteral("x")]           = pos[i].x;
        m[QStringLiteral("y")]           = pos[i].y;
        m[QStringLiteral("heat")]        = heat;
        // Colour comes from the learning channel that first taught the
        // concept — a real column in the table, not a tier invented here.
        m[QStringLiteral("source")]      = n.source;
        // "major" borrows the idea from brain-map's group styling: with 40
        // nodes, 40 permanent labels are a wall of text. Only concepts that
        // carry the structure — well-connected or frequently fired — are
        // labelled at rest; the rest name themselves on hover.
        m[QStringLiteral("major")]       = (n.degree >= 3 || heat >= 0.5);
        // Growth origin — its own position when nothing anchors it, so an
        // unanchored node simply scales up in place.
        const int from = spawnFrom[i];
        m[QStringLiteral("fromX")]       = from >= 0 ? pos[from].x : pos[i].x;
        m[QStringLiteral("fromY")]       = from >= 0 ? pos[from].y : pos[i].y;
        nodes.append(m);
        sourceCounts[n.source]++;
    }
    ctx->setContextProperty(QStringLiteral("synapseGraphNodes"), nodes);

    // Per-channel legend with live counts, in fixed order so the legend
    // doesn't reshuffle between refreshes.
    const QVector<QPair<QString, QString>> channels = {
        { QString::fromLatin1(SynapseGraph::kSourceDialogue),
          IS_EN ? QStringLiteral("Told me")   : QStringLiteral("Сказано мне") },
        { QString::fromLatin1(SynapseGraph::kSourceWatched),
          IS_EN ? QStringLiteral("Watched")   : QStringLiteral("Увидено в работе") },
        { QString::fromLatin1(SynapseGraph::kSourceFact),
          IS_EN ? QStringLiteral("Learned facts") : QStringLiteral("Усвоенные факты") },
    };
    QVariantList legend;
    for (const auto& ch : channels) {
        QVariantMap m;
        m[QStringLiteral("source")] = ch.first;
        m[QStringLiteral("label")]  = ch.second;
        m[QStringLiteral("count")]  = sourceCounts.value(ch.first, 0);
        legend.append(m);
    }
    ctx->setContextProperty(QStringLiteral("synapseLegend"), legend);

    QVariantList edges;
    for (const auto& e : view.edges) {
        QVariantMap m;
        // Endpoint indices travel alongside the coordinates so the view can
        // answer "what is this concept wired to" without re-deriving
        // adjacency from float positions.
        m[QStringLiteral("a")]      = e.a;
        m[QStringLiteral("b")]      = e.b;
        m[QStringLiteral("x1")]     = pos[e.a].x;
        m[QStringLiteral("y1")]     = pos[e.a].y;
        m[QStringLiteral("x2")]     = pos[e.b].x;
        m[QStringLiteral("y2")]     = pos[e.b].y;
        m[QStringLiteral("weight")] = double(e.weight);
        m[QStringLiteral("norm")]   = view.maxWeight > 0.0f
            ? double(e.weight) / double(view.maxWeight) : 0.0;
        edges.append(m);
    }
    ctx->setContextProperty(QStringLiteral("synapseGraphEdges"), edges);

    // Compact "strongest associations" list beside the picture — the graph
    // shows structure, this names it.
    QVariantList top;
    for (const auto& e : graph.topEdges(LlmCacheManager::kDesktopOwnerId, 10)) {
        QVariantMap m;
        m[QStringLiteral("labelA")]        = e.labelA;
        m[QStringLiteral("labelB")]        = e.labelB;
        m[QStringLiteral("weight")]        = double(e.weight);
        m[QStringLiteral("coActivations")] = e.coActivations;
        top.append(m);
    }
    ctx->setContextProperty(QStringLiteral("synapseTopEdges"), top);
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

QVariantMap TrainingCenterDialog::synapseNodeDetail(qint64 nodeId)
{
    QVariantMap out;

    const auto d = SynapseGraph::instance().nodeDetail(
        LlmCacheManager::kDesktopOwnerId, nodeId);
    out[QStringLiteral("found")] = d.found;
    if (!d.found)
        return out;

    out[QStringLiteral("label")]         = d.label;
    out[QStringLiteral("activations")]   = d.activations;
    out[QStringLiteral("firstSeen")]     = d.firstSeen;
    out[QStringLiteral("lastActivated")] = d.lastActivated;
    out[QStringLiteral("source")]        = d.source;
    out[QStringLiteral("sourceText")]    =
        SynapseGraph::sourceDescription(d.source, IS_EN);

    // Одна фраза, отвечающая на "почему это здесь". Собирается тут, а не
    // в QML: числа и склонения — не работа разметки.
    const QString times = IS_EN
        ? QStringLiteral("%1 time(s)").arg(d.activations)
        : QStringLiteral("%1 раз").arg(d.activations);
    QString why = IS_EN
        ? QStringLiteral("I %1 — it has come up %2 since %3.")
              .arg(SynapseGraph::sourceDescription(d.source, true), times, d.firstSeen)
        : QStringLiteral("Я %1 — с %2 это всплывало %3.")
              .arg(SynapseGraph::sourceDescription(d.source, false), d.firstSeen, times);

    if (!d.neighbours.isEmpty()) {
        QStringList names;
        for (int i = 0; i < d.neighbours.size() && i < 3; ++i)
            names << d.neighbours[i].labelB;
        why += IS_EN
            ? QStringLiteral(" Strongest links: %1.").arg(names.join(QStringLiteral(", ")))
            : QStringLiteral(" Крепче всего связано с: %1.").arg(names.join(QStringLiteral(", ")));
    }
    if (d.origins.isEmpty()) {
        why += IS_EN
            ? QStringLiteral(" No stored case is attached to it yet — "
                             "the concept is known, the story behind it is not.")
            : QStringLiteral(" Ни один сохранённый случай к нему пока не привязан — "
                             "понятие знакомо, а история за ним нет.");
    }
    out[QStringLiteral("why")] = why;

    QVariantList neighbours;
    for (const auto& e : d.neighbours) {
        QVariantMap m;
        m[QStringLiteral("label")]         = e.labelB;
        m[QStringLiteral("weight")]        = e.weight;
        m[QStringLiteral("coActivations")] = e.coActivations;
        neighbours.append(m);
    }
    out[QStringLiteral("neighbours")] = neighbours;

    QVariantList origins;
    for (const auto& o : d.origins) {
        QVariantMap m;
        // Реплики бывают длинными, а панель узкая: режем здесь, чтобы
        // QML не занимался обрезкой текста.
        m[QStringLiteral("query")]    = o.query.left(180);
        m[QStringLiteral("response")] = o.response.left(240).replace(QChar('\n'), QChar(' '));
        m[QStringLiteral("weight")]   = o.weight;
        origins.append(m);
    }
    out[QStringLiteral("origins")] = origins;

    return out;
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
