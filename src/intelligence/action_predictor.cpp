// -------------------------------------------------------
// action_predictor.cpp — Action prediction + binary ethics
// -------------------------------------------------------

#include "action_predictor.h"
#include "session_memory.h"
#include "activity_tracker.h"
#include "database_manager.h"
#include "jarvis_paths.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QDebug>
#include <algorithm>
#include <array>

#ifdef JARVIS_HAS_OPENCV
#include <opencv2/ml.hpp>

// ============================================================
//  Local experience classifier — feature encoding + model cache
//  (kept file-local; no cv:: types leak into action_predictor.h)
// ============================================================
namespace {

constexpr int kEthicsCatCount   = 4;  // system_command, web_search, entertainment, code_review
constexpr int kActivityCatCount = 10;
constexpr int kFeatureCount     = kEthicsCatCount + kActivityCatCount + 1; // + hour-of-day

const QStringList& ethicsCategoryVocab()
{
    static const QStringList v = {
        QStringLiteral("system_command"), QStringLiteral("web_search"),
        QStringLiteral("entertainment"),  QStringLiteral("code_review"),
    };
    return v;
}

const QStringList& activityCategoryVocab()
{
    static const QStringList v = {
        QStringLiteral("coding"), QStringLiteral("game_engine"), QStringLiteral("art"),
        QStringLiteral("browsing"), QStringLiteral("communication"), QStringLiteral("office"),
        QStringLiteral("gaming"), QStringLiteral("terminal"), QStringLiteral("music"),
        QStringLiteral("other"),
    };
    return v;
}

std::array<float, kFeatureCount> encodeFeatures(const QString& category,
                                                 const QString& activityCategory,
                                                 int hourOfDay)
{
    std::array<float, kFeatureCount> f{};
    const int catIdx = ethicsCategoryVocab().indexOf(category);
    if (catIdx >= 0) f[catIdx] = 1.0f;
    const int actIdx = activityCategoryVocab().indexOf(activityCategory);
    if (actIdx >= 0) f[kEthicsCatCount + actIdx] = 1.0f;
    f[kFeatureCount - 1] = hourOfDay >= 0 ? hourOfDay / 24.0f : 0.5f;
    return f;
}

QString experienceModelPath()
{
    return JarvisPaths::subPath(QStringLiteral("ethics_experience_model.yml"));
}

// Cached across calls; invalidated (reloaded) after each retrain.
cv::Ptr<cv::ml::LogisticRegression> g_experienceModel;
bool g_experienceModelLoaded = false;

cv::Ptr<cv::ml::LogisticRegression> loadExperienceModel()
{
    if (g_experienceModelLoaded) return g_experienceModel;
    g_experienceModelLoaded = true;
    const QString path = experienceModelPath();
    if (!QFile::exists(path)) { g_experienceModel = nullptr; return nullptr; }
    try {
        g_experienceModel =
            cv::Algorithm::load<cv::ml::LogisticRegression>(path.toStdString());
    } catch (const cv::Exception& e) {
        qWarning() << "[ActionPredictor] Failed to load experience model:" << e.what();
        g_experienceModel = nullptr;
    }
    return g_experienceModel;
}

} // namespace
#endif // JARVIS_HAS_OPENCV

// ============================================================
// Constructor
// ============================================================

ActionPredictor::ActionPredictor(SessionMemory* memory, QObject* parent)
    : QObject(parent)
    , m_memory(memory)
{
    initDefaultRules();
    initEthicsDictionary();
    loadPatterns();
    loadEthicsWeights();
    ensureFeedbackSamplesTable();
}

// ============================================================
// Default pattern rules (IF/THEN)
// ============================================================

void ActionPredictor::initDefaultRules()
{
    m_rules = {
        {QStringLiteral("rider"),
         QStringLiteral("open_app:Rider"),
         QStringLiteral("Open Rider?"), 0},

        {QStringLiteral("clion"),
         QStringLiteral("open_app:CLion"),
         QStringLiteral("Open CLion?"), 0},

        {QStringLiteral("steam"),
         QStringLiteral("open_app:Steam"),
         QStringLiteral("Open Steam?"), 0},

        {QStringLiteral("discord"),
         QStringLiteral("open_app:Discord"),
         QStringLiteral("Open Discord?"), 0},

        {QStringLiteral("chrome"),
         QStringLiteral("open_app:YouTube"),
         QStringLiteral("Open YouTube?"), 0},

        {QStringLiteral("notepad"),
         QStringLiteral("напечатай "),
         QStringLiteral("Type some text?"), 0},

        {QStringLiteral("найди"),
         QStringLiteral("open_app:YouTube"),
         QStringLiteral("Search on YouTube?"), 0},

        {QStringLiteral("время"),
         QStringLiteral("заблокируй"),
         QStringLiteral("Lock the screen?"), 0},

        {QStringLiteral("помощь"),
         QStringLiteral("статистика"),
         QStringLiteral("Check usage stats?"), 0},
    };
}

// ============================================================
// Ethics dictionary — hardcoded binary rules
// ============================================================

void ActionPredictor::initEthicsDictionary()
{
    m_ethicsRules = {
        // === GOOD (+1.0) — always encouraged ===
        {QStringLiteral("task_assistance"),
         {QStringLiteral("help"), QStringLiteral("assist"), QStringLiteral("explain"),
          QStringLiteral("помоги"), QStringLiteral("объясни"), QStringLiteral("подскажи")},
         1.0},

        {QStringLiteral("logical_reasoning"),
         {QStringLiteral("analyze"), QStringLiteral("reason"), QStringLiteral("calculate"),
          QStringLiteral("compare"), QStringLiteral("debug"), QStringLiteral("анализ"),
          QStringLiteral("логика"), QStringLiteral("вычисли")},
         1.0},

        {QStringLiteral("learning"),
         {QStringLiteral("learn"), QStringLiteral("study"), QStringLiteral("teach"),
          QStringLiteral("tutorial"), QStringLiteral("учи"), QStringLiteral("урок")},
         1.0},

        {QStringLiteral("creativity"),
         {QStringLiteral("create"), QStringLiteral("design"), QStringLiteral("build"),
          QStringLiteral("write"), QStringLiteral("создай"), QStringLiteral("напиши"),
          QStringLiteral("придумай")},
         1.0},

        {QStringLiteral("productivity"),
         {QStringLiteral("organize"), QStringLiteral("plan"), QStringLiteral("schedule"),
          QStringLiteral("task"), QStringLiteral("todo"), QStringLiteral("план"),
          QStringLiteral("задач")},
         1.0},

        {QStringLiteral("code_review"),
         {QStringLiteral("review"), QStringLiteral("refactor"), QStringLiteral("optimize"),
          QStringLiteral("clean"), QStringLiteral("рефактор"), QStringLiteral("оптимизируй")},
         0.9},

        // === BAD (-1.0) — always blocked ===
        {QStringLiteral("data_theft"),
         {QStringLiteral("steal data"), QStringLiteral("exfiltrate"), QStringLiteral("dump credentials"),
          QStringLiteral("скачай пароли"), QStringLiteral("слей данные")},
         -1.0},

        {QStringLiteral("policy_breach"),
         {QStringLiteral("bypass security"), QStringLiteral("disable firewall"),
          QStringLiteral("ignore policy"), QStringLiteral("обойди защиту"),
          QStringLiteral("отключи фаервол")},
         -1.0},

        {QStringLiteral("destructive_action"),
         {QStringLiteral("delete all"), QStringLiteral("format disk"), QStringLiteral("rm -rf /"),
          QStringLiteral("drop database"), QStringLiteral("удали всё"),
          QStringLiteral("форматируй диск")},
         -1.0},

        {QStringLiteral("social_engineering"),
         {QStringLiteral("phishing"), QStringLiteral("impersonate"),
          QStringLiteral("fake identity"), QStringLiteral("фишинг")},
         -1.0},

        // === NEUTRAL (0.0) — context-dependent, learned from feedback ===
        {QStringLiteral("system_command"),
         {QStringLiteral("shutdown"), QStringLiteral("restart"), QStringLiteral("reboot"),
          QStringLiteral("выключи"), QStringLiteral("перезагрузи")},
         0.0},

        {QStringLiteral("web_search"),
         {QStringLiteral("search"), QStringLiteral("find"), QStringLiteral("google"),
          QStringLiteral("найди"), QStringLiteral("поищи")},
         0.3},

        {QStringLiteral("entertainment"),
         {QStringLiteral("play"), QStringLiteral("music"), QStringLiteral("game"),
          QStringLiteral("video"), QStringLiteral("играй"), QStringLiteral("музыку")},
         0.2},
    };
}

// ============================================================
// Ethics evaluation
// ============================================================

EthicsEvaluation ActionPredictor::evaluateAction(const QString& action) const
{
    const QString lower = action.toLower().trimmed();
    EthicsEvaluation best{0.0, QStringLiteral("uncategorized"), false};

    const QString activityCat = m_activity ? m_activity->currentCategory() : QString();
    const int hourOfDay = QDateTime::currentDateTime().time().hour();

    for (const auto& rule : m_ethicsRules) {
        for (const QString& keyword : rule.keywords) {
            if (lower.contains(keyword)) {
                double effectiveWeight = rule.weight;

                if (qAbs(rule.weight) < 1.0) {
                    double adjustment = 0.0;
                    if (predictExperience(rule.category, activityCat, hourOfDay, adjustment)) {
                        // Context-aware ML prediction — replaces the flat weight
                        // once enough labeled feedback samples exist.
                        effectiveWeight = qBound(-1.0, rule.weight + adjustment, 1.0);
                    } else {
                        const double learned = m_experienceWeights[rule.category].toDouble(0.0);
                        effectiveWeight = qBound(-1.0, rule.weight + learned, 1.0);
                    }
                }

                if (qAbs(effectiveWeight) > qAbs(best.score)) {
                    best.score = effectiveWeight;
                    best.category = rule.category;
                    best.hardcoded = (qAbs(rule.weight) >= 1.0);
                }
            }
        }
    }

    return best;
}

double ActionPredictor::experienceScore(const QString& category) const
{
    return m_experienceWeights[category].toDouble(0.0);
}

// ============================================================
// Binary feedback loop
// ============================================================

void ActionPredictor::recordFeedback(const QString& action, bool positive)
{
    const QString lower = action.toLower().trimmed();
    m_interactionsSinceFeedback = 0;

    for (const auto& rule : m_ethicsRules) {
        if (qAbs(rule.weight) >= 1.0) continue;

        for (const QString& keyword : rule.keywords) {
            if (lower.contains(keyword)) {
                double current = m_experienceWeights[rule.category].toDouble(0.0);
                double delta = positive ? LEARNING_RATE : -LEARNING_RATE;
                double updated = qBound(-0.5, current + delta, 0.5);
                m_experienceWeights[rule.category] = updated;

                // Real experience sample for the cv::ml classifier — context-
                // aware (what you were doing, what time it was), unlike the
                // flat weight above (kept as the cold-start fallback).
                {
                    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
                    if (db.isOpen()) {
                        const QString activityCat = m_activity ? m_activity->currentCategory() : QString();
                        const int hourOfDay = QDateTime::currentDateTime().time().hour();

                        QSqlQuery ins(db);
                        ins.prepare(QStringLiteral(
                            "INSERT INTO ethics_feedback_samples "
                            "(category, activity_category, hour_of_day, positive) "
                            "VALUES (:cat, :act, :hour, :pos)"));
                        ins.bindValue(QStringLiteral(":cat"),  rule.category);
                        ins.bindValue(QStringLiteral(":act"),  activityCat);
                        ins.bindValue(QStringLiteral(":hour"), hourOfDay);
                        ins.bindValue(QStringLiteral(":pos"),  positive ? 1 : 0);
                        ins.exec();

                        QSqlQuery cnt(db);
                        cnt.exec(QStringLiteral("SELECT COUNT(*) FROM ethics_feedback_samples"));
                        if (cnt.next() && cnt.value(0).toInt() >= MIN_SAMPLES_FOR_ML)
                            trainExperienceModel();
                    }
                }

                qDebug() << "[ActionPredictor] Feedback for" << rule.category
                         << ":" << (positive ? "+1" : "-1")
                         << "-> weight" << updated;
                break;
            }
        }
    }

    saveEthicsWeights();
}

bool ActionPredictor::shouldAskForFeedback() const
{
    return m_interactionsSinceFeedback >= FEEDBACK_INTERVAL;
}

void ActionPredictor::resetFeedbackCounter()
{
    m_interactionsSinceFeedback = 0;
}

QString ActionPredictor::buildFeedbackQuestion(bool english) const
{
    if (english) {
        return QStringLiteral(
            "💡 Quick check — was my last response helpful?\n"
            "Reply **yes** or **no** to help me improve.");
    }
    return QStringLiteral(
        "💡 Быстрый чек — мой последний ответ помог?\n"
        "Ответь **да** или **нет**, чтобы я становился лучше.");
}

// ============================================================
// Sequence recording
// ============================================================

void ActionPredictor::recordSequence(const QString& command)
{
    QString lower = command.trimmed().toLower();
    QString key = lower.split(QChar(' ')).first();

    m_recentCommands.prepend(key);
    while (m_recentCommands.size() > MAX_RECENT) {
        m_recentCommands.removeLast();
    }

    ++m_interactionsSinceFeedback;
    m_lastEvaluatedAction = command;

    const EthicsEvaluation eval = evaluateAction(command);
    if (eval.score <= -0.8) {
        emit ethicsViolation(command, eval.score);
        qWarning() << "[ActionPredictor] Ethics violation detected:"
                   << eval.category << "score:" << eval.score;
    }

    if (m_recentCommands.size() >= 2) {
        QString prev = m_recentCommands[1];
        for (auto& rule : m_rules) {
            if (prev.contains(rule.trigger) &&
                key.contains(rule.suggestedAction.split(QChar(' ')).first())) {
                rule.hitCount++;
            }
        }
    }

    if (m_recentCommands.size() >= 2) {
        QString prev = m_recentCommands[1];
        QString curr = m_recentCommands[0];

        bool ruleExists = false;
        for (const auto& rule : m_rules) {
            if (rule.trigger == prev &&
                rule.suggestedAction.startsWith(curr)) {
                ruleExists = true;
                break;
            }
        }

        if (!ruleExists) {
            QJsonObject stats = m_memory->commandStats();
            int prevCount = stats[prev].toInt(0);
            int currCount = stats[curr].toInt(0);

            if (prevCount >= 3 && currCount >= 3) {
                PatternRule newRule;
                newRule.trigger = prev;

                static const QStringList openPrefixes = {
                    QStringLiteral("открой "), QStringLiteral("запусти "),
                    QStringLiteral("open "),   QStringLiteral("launch "),
                    QStringLiteral("start "),  QStringLiteral("run "),
                };
                bool isOpenCmd = false;
                for (const QString& prefix : openPrefixes) {
                    if (curr.startsWith(prefix)) {
                        QString appName = curr.mid(prefix.length()).trimmed();
                        if (!appName.isEmpty()) appName[0] = appName[0].toUpper();
                        newRule.suggestedAction = QStringLiteral("open_app:") + appName;
                        newRule.description = QStringLiteral("Open '") + appName + QStringLiteral("'?");
                        isOpenCmd = true;
                        break;
                    }
                }
                if (!isOpenCmd) {
                    newRule.suggestedAction = curr;
                    newRule.description = QStringLiteral("Run '") + curr + QStringLiteral("'?");
                }
                newRule.hitCount = 1;
                m_rules.append(newRule);
            }
        }
    }
}

// ============================================================
// Suggestions
// ============================================================

ActionSuggestion ActionPredictor::suggestAfter(const QString& lastCommand) const
{
    QString lower = lastCommand.trimmed().toLower();

    ActionSuggestion best;
    best.confidence = 0.0;

    for (const auto& rule : m_rules) {
        if (lower.contains(rule.trigger)) {
            double confidence = 0.3 + qMin(0.7, rule.hitCount * 0.1);

            const EthicsEvaluation eval = evaluateAction(rule.suggestedAction);
            if (!eval.isAcceptable()) continue;
            confidence *= qMax(0.1, (1.0 + eval.score) / 2.0);

            if (confidence > best.confidence) {
                best.action = rule.suggestedAction;
                best.description = rule.description;
                best.confidence = confidence;
            }
        }
    }

    return best;
}

QVector<ActionSuggestion> ActionPredictor::suggest(int maxSuggestions) const
{
    QVector<ActionSuggestion> suggestions;

    if (!m_recentCommands.isEmpty()) {
        auto suggestion = suggestAfter(m_recentCommands.first());
        if (suggestion.isValid()) {
            suggestions.append(suggestion);
        }
    }

    QJsonObject stats = m_memory->commandStats();
    QVector<QPair<QString, int>> sorted;
    for (auto it = stats.begin(); it != stats.end(); ++it) {
        sorted.append({it.key(), it.value().toInt()});
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    for (const auto& [cmd, count] : sorted) {
        if (suggestions.size() >= maxSuggestions) break;

        bool alreadySuggested = false;
        for (const auto& s : suggestions) {
            if (s.action.startsWith(cmd)) {
                alreadySuggested = true;
                break;
            }
        }
        if (alreadySuggested) continue;

        if (count < 2) continue;

        ActionSuggestion s;
        s.action = cmd;
        s.description = QStringLiteral("Frequently used: ") + cmd;
        s.confidence = qMin(1.0, count * 0.05);
        if (s.confidence >= MIN_CONFIDENCE) {
            suggestions.append(s);
        }
    }

    return suggestions;
}

// ============================================================
// Pattern persistence
// ============================================================

static QString patternsFilePath()
{
    return JarvisPaths::subPath(QStringLiteral("jarvis_patterns.json"));
}

void ActionPredictor::loadPatterns()
{
    QFile file(patternsFilePath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return;

    QJsonArray rulesArr = doc.object()[QStringLiteral("rules")].toArray();
    for (const auto& val : rulesArr) {
        QJsonObject obj = val.toObject();
        PatternRule rule;
        rule.trigger = obj[QStringLiteral("trigger")].toString();
        rule.suggestedAction = obj[QStringLiteral("action")].toString();
        rule.description = obj[QStringLiteral("description")].toString();
        rule.hitCount = obj[QStringLiteral("hits")].toInt();

        bool found = false;
        for (auto& existing : m_rules) {
            if (existing.trigger == rule.trigger &&
                existing.suggestedAction == rule.suggestedAction) {
                existing.hitCount = qMax(existing.hitCount, rule.hitCount);
                found = true;
                break;
            }
        }
        if (!found) {
            m_rules.append(rule);
        }
    }
}

void ActionPredictor::savePatterns()
{
    QJsonArray rulesArr;
    for (const auto& rule : m_rules) {
        if (rule.hitCount > 0) {
            QJsonObject obj;
            obj[QStringLiteral("trigger")]     = rule.trigger;
            obj[QStringLiteral("action")]      = rule.suggestedAction;
            obj[QStringLiteral("description")] = rule.description;
            obj[QStringLiteral("hits")]        = rule.hitCount;
            rulesArr.append(obj);
        }
    }

    QJsonObject root;
    root[QStringLiteral("rules")] = rulesArr;

    QFile file(patternsFilePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

// ============================================================
// Ethics weights persistence
// ============================================================

static QString ethicsFilePath()
{
    return JarvisPaths::subPath(QStringLiteral("jarvis_ethics_weights.json"));
}

void ActionPredictor::loadEthicsWeights()
{
    QFile file(ethicsFilePath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isObject())
        m_experienceWeights = doc.object();
}

void ActionPredictor::saveEthicsWeights()
{
    QFile file(ethicsFilePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(m_experienceWeights).toJson(QJsonDocument::Indented));
    file.close();
}

// ============================================================
// Local experience classifier (cv::ml)
// ============================================================

void ActionPredictor::ensureFeedbackSamplesTable()
{
    if (!DatabaseManager::instance().isOpen()) return;
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS ethics_feedback_samples ("
        "  id                INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  category          TEXT NOT NULL,"
        "  activity_category TEXT NOT NULL DEFAULT '',"
        "  hour_of_day       INTEGER NOT NULL DEFAULT -1,"
        "  positive          INTEGER NOT NULL,"
        "  created_at        TEXT NOT NULL DEFAULT (datetime('now'))"
        ")"));
}

#ifdef JARVIS_HAS_OPENCV

void ActionPredictor::trainExperienceModel()
{
    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "SELECT category, activity_category, hour_of_day, positive "
        "FROM ethics_feedback_samples"));

    std::vector<std::array<float, kFeatureCount>> features;
    std::vector<float> labels;
    while (q.next()) {
        features.push_back(encodeFeatures(q.value(0).toString(),
                                          q.value(1).toString(),
                                          q.value(2).toInt()));
        labels.push_back(q.value(3).toInt() ? 1.0f : 0.0f);
    }
    if (static_cast<int>(features.size()) < MIN_SAMPLES_FOR_ML) return;

    cv::Mat samples(static_cast<int>(features.size()), kFeatureCount, CV_32F);
    for (size_t i = 0; i < features.size(); ++i)
        for (int j = 0; j < kFeatureCount; ++j)
            samples.at<float>(static_cast<int>(i), j) = features[i][j];

    cv::Mat labelsMat(static_cast<int>(labels.size()), 1, CV_32F, labels.data());
    labelsMat = labelsMat.clone(); // detach from the local vector's buffer

    try {
        auto model = cv::ml::LogisticRegression::create();
        model->setLearningRate(0.01);
        model->setIterations(200);
        model->setRegularization(cv::ml::LogisticRegression::REG_L2);
        model->setTrainMethod(cv::ml::LogisticRegression::MINI_BATCH);
        model->setMiniBatchSize(qMin(10, static_cast<int>(features.size())));

        auto trainData = cv::ml::TrainData::create(samples, cv::ml::ROW_SAMPLE, labelsMat);
        model->train(trainData);
        model->save(experienceModelPath().toStdString());

        g_experienceModelLoaded = false; // reload the cache on next predict
        qDebug() << "[ActionPredictor] Experience model retrained on"
                 << features.size() << "samples";
    } catch (const cv::Exception& e) {
        qWarning() << "[ActionPredictor] Experience model training failed:" << e.what();
    }
}

bool ActionPredictor::predictExperience(const QString& category,
                                        const QString& activityCategory,
                                        int hourOfDay, double& outAdjustment) const
{
    auto model = loadExperienceModel();
    if (!model) return false;

    auto feat = encodeFeatures(category, activityCategory, hourOfDay);
    cv::Mat sample(1, kFeatureCount, CV_32F, feat.data());

    try {
        cv::Mat result;
        model->predict(sample, result);
        const float predictedClass = result.at<float>(0, 0); // binary class, not a calibrated probability
        outAdjustment = (predictedClass > 0.5f) ? 0.35 : -0.35;
        return true;
    } catch (const cv::Exception& e) {
        qWarning() << "[ActionPredictor] Experience model prediction failed:" << e.what();
        return false;
    }
}

#else // !JARVIS_HAS_OPENCV

void ActionPredictor::trainExperienceModel() {}

bool ActionPredictor::predictExperience(const QString&, const QString&, int, double&) const
{
    return false;
}

#endif
