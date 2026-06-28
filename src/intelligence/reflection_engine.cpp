// ============================================================
// reflection_engine.cpp — Autonomous Self-Reflection Loop
// ============================================================

#include "reflection_engine.h"
#include "memory_manager.h"
#include "social_presence.h"
#include "personality_engine.h"
#include "database_manager.h"
#include "jarvis_paths.h"

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDate>
#include <QTime>
#include <QRegularExpression>
#include <QReadLocker>
#include <QWriteLocker>
#include <QtConcurrent>
#include <QDebug>

#include <algorithm>
#include <cmath>

// ============================================================
//  BehavioralMetrics JSON
// ============================================================

QJsonObject BehavioralMetrics::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("goal_orientedness")]     = goalOrientedness;
    obj[QStringLiteral("context_switch_rate")]    = contextSwitchRate;
    obj[QStringLiteral("focus_evolution_delta")]  = focusEvolutionDelta;
    obj[QStringLiteral("interaction_style")]      = interactionStyle;

    QJsonArray dwells;
    for (const auto& [topic, count] : topicDwellTimes) {
        QJsonObject d;
        d[QStringLiteral("topic")] = topic;
        d[QStringLiteral("messages")] = count;
        dwells.append(d);
    }
    obj[QStringLiteral("topic_dwell_times")] = dwells;
    return obj;
}

// ============================================================
//  UserSummary JSON
// ============================================================

QJsonObject UserSummary::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("generated_at")] = generatedAt.toString(Qt::ISODate);

    // Emotion
    QJsonObject emo;
    emo[QStringLiteral("dominant")]     = emotion.dominant;
    emo[QStringLiteral("confidence")]   = emotion.confidence;
    emo[QStringLiteral("sample_count")] = emotion.sampleCount;
    QJsonArray sigs;
    for (const auto& s : emotion.evidenceWords)
        sigs.append(s);
    emo[QStringLiteral("signals")] = sigs;
    obj[QStringLiteral("emotion")] = emo;

    // Productivity
    QJsonObject prod;
    prod[QStringLiteral("total_messages")]  = productivity.totalMessages;
    prod[QStringLiteral("question_count")]  = productivity.questionCount;
    prod[QStringLiteral("command_count")]   = productivity.commandCount;
    prod[QStringLiteral("topic_switches")]  = productivity.topicSwitches;
    prod[QStringLiteral("focus_score")]     = productivity.focusScore;
    prod[QStringLiteral("dominant_topic")]  = productivity.dominantTopic;
    QJsonArray topics;
    for (const auto& t : productivity.topicsDiscussed)
        topics.append(t);
    prod[QStringLiteral("topics")] = topics;
    obj[QStringLiteral("productivity")] = prod;

    // Cognitive shifts
    QJsonArray shifts;
    for (const auto& cs : cognitiveShifts) {
        QJsonObject s;
        s[QStringLiteral("topic")]           = cs.topic;
        s[QStringLiteral("previous_stance")] = cs.previousStance;
        s[QStringLiteral("current_stance")]  = cs.currentStance;
        s[QStringLiteral("magnitude")]       = cs.shiftMagnitude;
        s[QStringLiteral("detected_at")]     = cs.detectedAt.toString(Qt::ISODate);
        shifts.append(s);
    }
    obj[QStringLiteral("cognitive_shifts")] = shifts;

    // Behavioral metrics
    obj[QStringLiteral("behavioral_metrics")] = behavior.toJson();

    if (!morningNudge.isEmpty())
        obj[QStringLiteral("morning_nudge")] = morningNudge;

    return obj;
}

// ============================================================
//  Emotion Lexicon
// ============================================================

const QList<EmotionSignal>& ReflectionEngine::emotionLexicon()
{
    static const QList<EmotionSignal> lex = {
        // Positive
        {QStringLiteral("great"),      QStringLiteral("positive"),    0.8},
        {QStringLiteral("awesome"),    QStringLiteral("positive"),    0.9},
        {QStringLiteral("love"),       QStringLiteral("positive"),    0.7},
        {QStringLiteral("nice"),       QStringLiteral("positive"),    0.5},
        {QStringLiteral("perfect"),    QStringLiteral("positive"),    0.9},
        {QStringLiteral("thanks"),     QStringLiteral("positive"),    0.6},
        {QStringLiteral("cool"),       QStringLiteral("positive"),    0.5},
        {QStringLiteral("excellent"),  QStringLiteral("positive"),    0.9},
        {QStringLiteral("happy"),      QStringLiteral("positive"),    0.7},
        {QStringLiteral("класс"),      QStringLiteral("positive"),    0.7},
        {QStringLiteral("отлично"),    QStringLiteral("positive"),    0.8},
        {QStringLiteral("круто"),      QStringLiteral("positive"),    0.7},
        {QStringLiteral("спасибо"),    QStringLiteral("positive"),    0.6},
        {QStringLiteral("супер"),      QStringLiteral("positive"),    0.8},
        {QStringLiteral("кайф"),       QStringLiteral("positive"),    0.6},

        // Stressed / Frustrated
        {QStringLiteral("frustrated"), QStringLiteral("frustrated"),  0.9},
        {QStringLiteral("annoying"),   QStringLiteral("frustrated"),  0.7},
        {QStringLiteral("broken"),     QStringLiteral("frustrated"),  0.6},
        {QStringLiteral("hate"),       QStringLiteral("frustrated"),  0.8},
        {QStringLiteral("damn"),       QStringLiteral("frustrated"),  0.6},
        {QStringLiteral("stuck"),      QStringLiteral("stressed"),    0.7},
        {QStringLiteral("can't"),      QStringLiteral("stressed"),    0.4},
        {QStringLiteral("problem"),    QStringLiteral("stressed"),    0.4},
        {QStringLiteral("error"),      QStringLiteral("stressed"),    0.5},
        {QStringLiteral("bug"),        QStringLiteral("stressed"),    0.5},
        {QStringLiteral("crash"),      QStringLiteral("stressed"),    0.7},
        {QStringLiteral("бесит"),      QStringLiteral("frustrated"),  0.8},
        {QStringLiteral("сломал"),     QStringLiteral("frustrated"),  0.6},
        {QStringLiteral("задолбал"),   QStringLiteral("frustrated"),  0.9},
        {QStringLiteral("ошибка"),     QStringLiteral("stressed"),    0.5},
        {QStringLiteral("баг"),        QStringLiteral("stressed"),    0.5},
        {QStringLiteral("устал"),      QStringLiteral("stressed"),    0.7},
        {QStringLiteral("надоело"),    QStringLiteral("frustrated"),  0.7},

        // Curious
        {QStringLiteral("why"),        QStringLiteral("curious"),     0.5},
        {QStringLiteral("how"),        QStringLiteral("curious"),     0.4},
        {QStringLiteral("what if"),    QStringLiteral("curious"),     0.7},
        {QStringLiteral("wonder"),     QStringLiteral("curious"),     0.6},
        {QStringLiteral("interesting"),QStringLiteral("curious"),     0.6},
        {QStringLiteral("curious"),    QStringLiteral("curious"),     0.8},
        {QStringLiteral("explain"),    QStringLiteral("curious"),     0.5},
        {QStringLiteral("почему"),     QStringLiteral("curious"),     0.5},
        {QStringLiteral("интересно"),  QStringLiteral("curious"),     0.6},
        {QStringLiteral("а если"),     QStringLiteral("curious"),     0.7},
        {QStringLiteral("расскажи"),   QStringLiteral("curious"),     0.5},
    };
    return lex;
}

// ============================================================
//  Construction / Destruction
// ============================================================

ReflectionEngine::ReflectionEngine(QObject* parent)
    : QObject(parent)
{
    m_checkTimer = new QTimer(this);
    connect(m_checkTimer, &QTimer::timeout,
            this, &ReflectionEngine::onCheckTimer);
}

ReflectionEngine::~ReflectionEngine()
{
    stop();
}

void ReflectionEngine::setMemoryManager(MemoryManager* mm)
{
    m_memory = mm;
}

void ReflectionEngine::setSocialPresence(SocialPresenceEngine* sp)
{
    m_presence = sp;
}

void ReflectionEngine::setPersonalityEngine(PersonalityEngine* pe)
{
    m_personality = pe;
}

// ============================================================
//  Lifecycle
// ============================================================

void ReflectionEngine::start(int checkIntervalMinutes)
{
    m_checkTimer->setInterval(checkIntervalMinutes * 60 * 1000);
    m_checkTimer->start();
    qDebug() << "[ReflectionEngine] Started. Check interval:"
             << checkIntervalMinutes << "min";
}

void ReflectionEngine::stop()
{
    m_checkTimer->stop();
}

bool ReflectionEngine::isRunning() const
{
    return m_checkTimer->isActive();
}

UserSummary ReflectionEngine::lastSummary() const
{
    QReadLocker lock(&m_summaryLock);
    return m_lastSummary;
}

BehavioralMetrics ReflectionEngine::currentBehavior() const
{
    QReadLocker lock(&m_summaryLock);
    return m_currentBehavior;
}

QString ReflectionEngine::profileJsonPath()
{
    return JarvisPaths::subPath(QStringLiteral("user_profile_evolving.json"));
}

// ============================================================
//  Timer Check
// ============================================================

void ReflectionEngine::onCheckTimer()
{
    if (!shouldReflect()) return;

    (void)QtConcurrent::run([this]() {
        runReflectionCycle();
    });
}

bool ReflectionEngine::shouldReflect() const
{
    if (m_presence) {
        const auto status = m_presence->currentStatus();
        if (status == UserState::FREE)
            return false;
    }

    if (m_lastReflectionTime.isValid()) {
        const qint64 hoursSince = m_lastReflectionTime.secsTo(
            QDateTime::currentDateTime()) / 3600;
        if (hoursSince < MIN_HOURS_BETWEEN_REFLECTIONS)
            return false;
    }

    return true;
}

void ReflectionEngine::triggerReflection()
{
    (void)QtConcurrent::run([this]() {
        runReflectionCycle();
    });
}

// ============================================================
//  Core Reflection Pipeline
// ============================================================

void ReflectionEngine::runReflectionCycle()
{
    qDebug() << "[ReflectionEngine] Starting reflection cycle...";

    const auto dialogue = fetchLast24hDialogue();
    if (dialogue.size() < MIN_MESSAGES_FOR_REFLECTION) {
        qDebug() << "[ReflectionEngine] Not enough dialogue ("
                 << dialogue.size() << "msgs). Skipping.";
        return;
    }

    UserSummary summary;
    summary.generatedAt     = QDateTime::currentDateTimeUtc();
    summary.emotion         = analyzeEmotions(dialogue);
    summary.productivity    = analyzeProductivity(dialogue);
    summary.cognitiveShifts = detectCognitiveShifts(dialogue);
    summary.behavior        = analyzeBehavior(dialogue);
    summary.behavior.focusEvolutionDelta = calculateBehavioralEvolution(summary.behavior);
    summary.morningNudge    = composeMorningNudge(summary);

    {
        QWriteLocker lock(&m_summaryLock);
        m_lastSummary     = summary;
        m_currentBehavior = summary.behavior;
    }
    m_lastReflectionTime = QDateTime::currentDateTime();

    updateProfileJson(summary);

    if (m_memory)
        storeReflectionMemories(summary);

    // Feed the personality evolution engine
    if (m_personality) {
        const double responseRate =
            (summary.productivity.totalMessages > 0)
                ? static_cast<double>(summary.productivity.questionCount)
                  / summary.productivity.totalMessages
                : 0.5;
        m_personality->updateFromReflection(
            summary.productivity.focusScore,
            summary.behavior.goalOrientedness,
            responseRate,
            summary.productivity.totalMessages);
    }

    const int today = QDate::currentDate().dayOfYear();
    if (m_lastReflectionDay != today) {
        m_lastReflectionDay = today;
        QMetaObject::invokeMethod(this, [this, summary]() {
            emit morningNudgeReady(summary.morningNudge);
        }, Qt::QueuedConnection);
    }

    QMetaObject::invokeMethod(this, [this, summary]() {
        emit reflectionComplete(summary);
        emit profileUpdated(profileJsonPath());
        emit behaviorUpdated(summary.behavior);
    }, Qt::QueuedConnection);

    qDebug() << "[ReflectionEngine] Reflection complete."
             << "Emotion:" << summary.emotion.dominant
             << "Focus:" << summary.productivity.focusScore
             << "GoalOriented:" << summary.behavior.goalOrientedness
             << "CtxSwitchRate:" << summary.behavior.contextSwitchRate
             << "FocusDelta:" << summary.behavior.focusEvolutionDelta;
}

// ============================================================
//  Fetch Last 24h Dialogue
// ============================================================

QList<QPair<QString, QString>> ReflectionEngine::fetchLast24hDialogue() const
{
    QList<QPair<QString, QString>> dialogue;
    if (!DatabaseManager::instance().isOpen()) return dialogue;

    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "SELECT role, content FROM chat_history "
        "WHERE user_id = 1 "
        "  AND created_at >= datetime('now', '-24 hours') "
        "ORDER BY created_at ASC "
        "LIMIT 500"));

    if (!q.exec()) {
        qWarning() << "[ReflectionEngine] Dialogue fetch failed:"
                   << q.lastError().text();
        return dialogue;
    }

    while (q.next())
        dialogue.append({q.value(0).toString(), q.value(1).toString()});

    return dialogue;
}

// ============================================================
//  Emotion Analysis
// ============================================================

EmotionTrend ReflectionEngine::analyzeEmotions(
    const QList<QPair<QString, QString>>& dialogue) const
{
    EmotionTrend result;

    QMap<QString, double> scores;
    int userMsgCount = 0;

    for (const auto& [role, content] : dialogue) {
        if (role != QStringLiteral("user")) continue;
        ++userMsgCount;

        const QString lower = content.toLower();
        for (const auto& sig : emotionLexicon()) {
            if (lower.contains(sig.keyword)) {
                scores[sig.emotion] += sig.weight;
                if (result.evidenceWords.size() < 10)
                    result.evidenceWords.append(sig.keyword);
            }
        }
    }

    result.sampleCount = userMsgCount;
    if (userMsgCount == 0) return result;

    double maxScore = 0.0;
    for (auto it = scores.constBegin(); it != scores.constEnd(); ++it) {
        if (it.value() > maxScore) {
            maxScore = it.value();
            result.dominant = it.key();
        }
    }

    const double ceiling = userMsgCount * 2.0;
    result.confidence = qMin(1.0, maxScore / qMax(1.0, ceiling));

    if (maxScore < 0.5)
        result.dominant = QStringLiteral("neutral");

    return result;
}

// ============================================================
//  Productivity Analysis
// ============================================================

ProductivityPattern ReflectionEngine::analyzeProductivity(
    const QList<QPair<QString, QString>>& dialogue) const
{
    ProductivityPattern result;
    QMap<QString, int> topicFreq;
    QString prevTopic;

    static const QRegularExpression questionRx(QStringLiteral("[?]"));

    for (const auto& [role, content] : dialogue) {
        ++result.totalMessages;
        if (role != QStringLiteral("user")) continue;

        if (content.contains(questionRx))
            ++result.questionCount;

        if (content.trimmed().startsWith(QLatin1Char('/')))
            ++result.commandCount;

        const QStringList tokens = MemoryManager::tokenize(content);
        const QString topic = tokens.isEmpty() ? QString() : tokens.first();

        if (!topic.isEmpty()) {
            topicFreq[topic] += 1;
            if (!prevTopic.isEmpty() && topic != prevTopic)
                ++result.topicSwitches;
            prevTopic = topic;
        }
    }

    int maxFreq = 0;
    for (auto it = topicFreq.constBegin(); it != topicFreq.constEnd(); ++it) {
        if (result.topicsDiscussed.size() < 10)
            result.topicsDiscussed.append(it.key());
        if (it.value() > maxFreq) {
            maxFreq = it.value();
            result.dominantTopic = it.key();
        }
    }

    const int userMsgs = qMax(1, result.totalMessages / 2);
    if (userMsgs > 1) {
        const double rate = static_cast<double>(result.topicSwitches) / userMsgs;
        result.focusScore = qMax(0.0, 1.0 - rate);
    } else {
        result.focusScore = 1.0;
    }

    return result;
}

// ============================================================
//  Cognitive Shift Detection
// ============================================================

QList<CognitiveShift> ReflectionEngine::detectCognitiveShifts(
    const QList<QPair<QString, QString>>& dialogue) const
{
    QList<CognitiveShift> shifts;

    static const QStringList markers = {
        QStringLiteral("actually"),
        QStringLiteral("i changed my mind"),
        QStringLiteral("i was wrong"),
        QStringLiteral("now i think"),
        QStringLiteral("on second thought"),
        QStringLiteral("i take that back"),
        QStringLiteral("nevermind"),
        QStringLiteral("wait no"),
        QStringLiteral("я передумал"),
        QStringLiteral("я ошибся"),
        QStringLiteral("на самом деле"),
        QStringLiteral("передумал"),
        QStringLiteral("нет подожди"),
        QStringLiteral("хотя нет"),
    };

    QString prevUserMsg;
    for (const auto& [role, content] : dialogue) {
        if (role != QStringLiteral("user")) continue;

        const QString lower = content.toLower();
        for (const QString& marker : markers) {
            if (lower.contains(marker)) {
                CognitiveShift cs;
                cs.topic = MemoryManager::tokenize(content)
                               .value(0, QStringLiteral("general"));
                cs.previousStance = prevUserMsg.left(100);
                cs.currentStance  = content.left(100);
                cs.shiftMagnitude = 0.5;
                cs.detectedAt     = QDateTime::currentDateTimeUtc();

                if (lower.contains(QStringLiteral("wrong"))
                    || lower.contains(QStringLiteral("ошибся")))
                    cs.shiftMagnitude = 0.8;

                if (cs.shiftMagnitude >= COGNITIVE_SHIFT_THRESHOLD)
                    shifts.append(cs);
                break;
            }
        }
        prevUserMsg = content;
    }

    return shifts;
}

// ============================================================
//  Behavioral Topology Analysis
// ============================================================

BehavioralMetrics ReflectionEngine::analyzeBehavior(
    const QList<QPair<QString, QString>>& dialogue) const
{
    BehavioralMetrics bm;

    // -- Goal-oriented markers (user messages that drive toward a task) --
    static const QStringList goalMarkers = {
        QStringLiteral("how do i"),   QStringLiteral("how to"),
        QStringLiteral("fix"),        QStringLiteral("solve"),
        QStringLiteral("implement"),  QStringLiteral("create"),
        QStringLiteral("build"),      QStringLiteral("make"),
        QStringLiteral("run"),        QStringLiteral("deploy"),
        QStringLiteral("compile"),    QStringLiteral("install"),
        QStringLiteral("configure"),  QStringLiteral("debug"),
        QStringLiteral("test"),       QStringLiteral("write"),
        QStringLiteral("add"),        QStringLiteral("remove"),
        QStringLiteral("update"),     QStringLiteral("change"),
        QStringLiteral("сделай"),     QStringLiteral("исправь"),
        QStringLiteral("собери"),     QStringLiteral("запусти"),
        QStringLiteral("создай"),     QStringLiteral("напиши"),
        QStringLiteral("добавь"),     QStringLiteral("удали"),
        QStringLiteral("настрой"),    QStringLiteral("помоги"),
    };

    // -- Exploratory markers --
    static const QStringList exploratoryMarkers = {
        QStringLiteral("what do you think"),
        QStringLiteral("what if"),
        QStringLiteral("opinion"),
        QStringLiteral("philosophy"),
        QStringLiteral("curious"),
        QStringLiteral("wonder"),
        QStringLiteral("random"),
        QStringLiteral("tell me about"),
        QStringLiteral("interesting"),
        QStringLiteral("как думаешь"),
        QStringLiteral("а что если"),
        QStringLiteral("расскажи"),
        QStringLiteral("интересно"),
        QStringLiteral("любопытно"),
    };

    int goalHits = 0;
    int exploratoryHits = 0;
    int userMsgCount = 0;

    // Topic tracking for context-switch analysis and dwell times
    QMap<QString, int> topicDwell;
    QStringList topicSequence;

    for (const auto& [role, content] : dialogue) {
        if (role != QStringLiteral("user")) continue;
        ++userMsgCount;

        const QString lower = content.toLower();

        for (const QString& gm : goalMarkers) {
            if (lower.contains(gm)) { ++goalHits; break; }
        }
        for (const QString& em : exploratoryMarkers) {
            if (lower.contains(em)) { ++exploratoryHits; break; }
        }

        // Extract dominant topic token for dwell/switch tracking
        const QStringList tokens = MemoryManager::tokenize(content);
        if (!tokens.isEmpty()) {
            const QString topic = tokens.first();
            topicDwell[topic] += 1;
            topicSequence.append(topic);
        }
    }

    if (userMsgCount == 0) return bm;

    // -- Goal-orientedness score --
    const double totalHits = goalHits + exploratoryHits;
    if (totalHits > 0)
        bm.goalOrientedness = static_cast<double>(goalHits) / totalHits;
    else
        bm.goalOrientedness = 0.5; // ambiguous

    // -- Context-switching rate (switches per 10 messages) --
    int switches = 0;
    for (int i = 1; i < topicSequence.size(); ++i) {
        if (topicSequence[i] != topicSequence[i - 1])
            ++switches;
    }
    bm.contextSwitchRate = (userMsgCount > 1)
        ? (static_cast<double>(switches) / userMsgCount) * 10.0
        : 0.0;

    // -- Interaction style classification --
    if (bm.goalOrientedness > 0.7)
        bm.interactionStyle = QStringLiteral("task-driven");
    else if (bm.goalOrientedness < 0.3)
        bm.interactionStyle = QStringLiteral("exploratory");
    else
        bm.interactionStyle = QStringLiteral("mixed");

    // -- Topic dwell times (sorted by frequency) --
    QList<QPair<QString, int>> sortedDwells;
    for (auto it = topicDwell.constBegin(); it != topicDwell.constEnd(); ++it)
        sortedDwells.append({it.key(), it.value()});
    std::sort(sortedDwells.begin(), sortedDwells.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (sortedDwells.size() > 15)
        sortedDwells.resize(15);
    bm.topicDwellTimes = sortedDwells;

    return bm;
}

// ============================================================
//  Behavioral Evolution — trend detection over time
// ============================================================

double ReflectionEngine::calculateBehavioralEvolution(
    const BehavioralMetrics& current) const
{
    const QJsonObject profile = loadExistingProfile();
    const QJsonArray history = profile[QStringLiteral("behavioral_history")].toArray();

    if (history.size() < 2)
        return 0.0; // not enough data to detect a trend

    // Compute average focus score from last 5 entries
    double historicalFocusSum = 0.0;
    double historicalGoalSum  = 0.0;
    int count = 0;
    const int windowStart = qMax(0, history.size() - 5);

    for (int i = windowStart; i < history.size(); ++i) {
        const QJsonObject entry = history[i].toObject();
        const double ctxRate = entry[QStringLiteral("context_switch_rate")].toDouble(5.0);
        const double goal    = entry[QStringLiteral("goal_orientedness")].toDouble(0.5);

        // Convert context_switch_rate into a focus score: lower rate = higher focus
        historicalFocusSum += qMax(0.0, 1.0 - ctxRate / 10.0);
        historicalGoalSum  += goal;
        ++count;
    }

    if (count == 0) return 0.0;

    const double avgHistFocus = historicalFocusSum / count;
    const double avgHistGoal  = historicalGoalSum  / count;

    const double currentFocus = qMax(0.0, 1.0 - current.contextSwitchRate / 10.0);

    // Weighted delta: 60% focus change + 40% goal-orientedness change
    const double focusDelta = currentFocus - avgHistFocus;
    const double goalDelta  = current.goalOrientedness - avgHistGoal;
    const double combined   = focusDelta * 0.6 + goalDelta * 0.4;

    // Clamp to [-1, 1]
    return qBound(-1.0, combined, 1.0);
}

// ============================================================
//  Morning Nudge Composition
// ============================================================

QString ReflectionEngine::composeMorningNudge(const UserSummary& summary) const
{
    QString nudge;
    nudge += QStringLiteral("Good morning! Here's what I noticed from yesterday:\n\n");

    // Emotion
    if (summary.emotion.dominant != QStringLiteral("neutral")) {
        if (summary.emotion.dominant == QStringLiteral("positive"))
            nudge += QStringLiteral("You seemed to be in a *great mood* yesterday — keep that energy!\n");
        else if (summary.emotion.dominant == QStringLiteral("stressed"))
            nudge += QStringLiteral("I noticed some *stress signals* yesterday. Everything OK? Maybe start today with something lighter.\n");
        else if (summary.emotion.dominant == QStringLiteral("frustrated"))
            nudge += QStringLiteral("Yesterday had some *frustrating moments*. Fresh start today — those bugs won't know what hit them.\n");
        else if (summary.emotion.dominant == QStringLiteral("curious"))
            nudge += QStringLiteral("Your *curiosity was off the charts* yesterday — love to see it.\n");
    }

    // Productivity
    if (summary.productivity.focusScore > 0.7)
        nudge += QStringLiteral("Your focus was *excellent* — sustained deep work. Impressive.\n");
    else if (summary.productivity.focusScore < 0.3)
        nudge += QStringLiteral("Yesterday was pretty scattered topic-wise. Want to pick *one big thing* to tackle today?\n");

    if (!summary.productivity.dominantTopic.isEmpty())
        nudge += QStringLiteral("Main area: *%1* (%2 messages).\n")
            .arg(summary.productivity.dominantTopic)
            .arg(summary.productivity.totalMessages);

    // Behavioral insights
    if (summary.behavior.focusEvolutionDelta > 0.15)
        nudge += QStringLiteral("You're getting *more focused* over time. The trend is clear.\n");
    else if (summary.behavior.focusEvolutionDelta < -0.15)
        nudge += QStringLiteral("Your attention seems more *scattered* lately. Maybe a deep-work block would help?\n");

    if (summary.behavior.interactionStyle == QStringLiteral("task-driven"))
        nudge += QStringLiteral("Yesterday was very *task-driven* — you were in execution mode.\n");
    else if (summary.behavior.interactionStyle == QStringLiteral("exploratory"))
        nudge += QStringLiteral("Yesterday was mostly *exploratory* — lots of open questions and curiosity.\n");

    // Cognitive shifts
    if (!summary.cognitiveShifts.isEmpty()) {
        nudge += QStringLiteral("\nI also noticed you *changed your mind* about ");
        nudge += summary.cognitiveShifts.first().topic;
        nudge += QStringLiteral(" — growth in action.\n");
    }

    nudge += QStringLiteral("\nWhat's the plan for today?");
    return nudge;
}

// ============================================================
//  Evolving UserProfile JSON
// ============================================================

QJsonObject ReflectionEngine::loadExistingProfile() const
{
    QFile file(profileJsonPath());
    if (!file.open(QIODevice::ReadOnly))
        return QJsonObject();
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    return doc.isObject() ? doc.object() : QJsonObject();
}

void ReflectionEngine::saveProfile(const QJsonObject& profile) const
{
    const QString path = profileJsonPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(profile).toJson(QJsonDocument::Indented));
        file.close();
    }
}

void ReflectionEngine::updateProfileJson(const UserSummary& summary)
{
    QJsonObject profile = loadExistingProfile();

    profile[QStringLiteral("last_updated")]   =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    profile[QStringLiteral("schema_version")] = 2;

    // -- Emotion history (rolling 30) --
    QJsonArray emoHist = profile[QStringLiteral("emotion_history")].toArray();
    QJsonObject emoEntry;
    emoEntry[QStringLiteral("date")]       = summary.generatedAt.toString(Qt::ISODate);
    emoEntry[QStringLiteral("dominant")]    = summary.emotion.dominant;
    emoEntry[QStringLiteral("confidence")]  = summary.emotion.confidence;
    emoHist.append(emoEntry);
    while (emoHist.size() > 30) emoHist.removeFirst();
    profile[QStringLiteral("emotion_history")] = emoHist;

    // -- Productivity history (rolling 30) --
    QJsonArray prodHist = profile[QStringLiteral("productivity_history")].toArray();
    QJsonObject prodEntry;
    prodEntry[QStringLiteral("date")]           = summary.generatedAt.toString(Qt::ISODate);
    prodEntry[QStringLiteral("focus_score")]    = summary.productivity.focusScore;
    prodEntry[QStringLiteral("messages")]       = summary.productivity.totalMessages;
    prodEntry[QStringLiteral("questions")]      = summary.productivity.questionCount;
    prodEntry[QStringLiteral("topic_switches")] = summary.productivity.topicSwitches;
    prodEntry[QStringLiteral("dominant_topic")] = summary.productivity.dominantTopic;
    prodHist.append(prodEntry);
    while (prodHist.size() > 30) prodHist.removeFirst();
    profile[QStringLiteral("productivity_history")] = prodHist;

    // -- Behavioral history (rolling 30) --
    QJsonArray behHist = profile[QStringLiteral("behavioral_history")].toArray();
    QJsonObject behEntry;
    behEntry[QStringLiteral("date")]                 = summary.generatedAt.toString(Qt::ISODate);
    behEntry[QStringLiteral("goal_orientedness")]    = summary.behavior.goalOrientedness;
    behEntry[QStringLiteral("context_switch_rate")]  = summary.behavior.contextSwitchRate;
    behEntry[QStringLiteral("focus_evolution_delta")]= summary.behavior.focusEvolutionDelta;
    behEntry[QStringLiteral("interaction_style")]    = summary.behavior.interactionStyle;
    behHist.append(behEntry);
    while (behHist.size() > 30) behHist.removeFirst();
    profile[QStringLiteral("behavioral_history")] = behHist;

    // -- Cognitive shifts (cap 50) --
    QJsonArray shiftHist = profile[QStringLiteral("cognitive_shifts")].toArray();
    for (const auto& cs : summary.cognitiveShifts) {
        QJsonObject s;
        s[QStringLiteral("topic")]     = cs.topic;
        s[QStringLiteral("from")]      = cs.previousStance;
        s[QStringLiteral("to")]        = cs.currentStance;
        s[QStringLiteral("magnitude")] = cs.shiftMagnitude;
        s[QStringLiteral("date")]      = cs.detectedAt.toString(Qt::ISODate);
        shiftHist.append(s);
    }
    while (shiftHist.size() > 50) shiftHist.removeFirst();
    profile[QStringLiteral("cognitive_shifts")] = shiftHist;

    // -- Aggregate stats --
    QJsonObject stats = profile[QStringLiteral("aggregate_stats")].toObject();
    const int reflCount = stats[QStringLiteral("total_reflections")].toInt(0) + 1;
    stats[QStringLiteral("total_reflections")]       = reflCount;
    stats[QStringLiteral("total_messages_analyzed")]  =
        stats[QStringLiteral("total_messages_analyzed")].toInt(0)
        + summary.productivity.totalMessages;
    stats[QStringLiteral("total_cognitive_shifts")]   =
        stats[QStringLiteral("total_cognitive_shifts")].toInt(0)
        + summary.cognitiveShifts.size();

    const double prevFocus = stats[QStringLiteral("avg_focus_score")].toDouble(0.5);
    stats[QStringLiteral("avg_focus_score")] =
        prevFocus + (summary.productivity.focusScore - prevFocus) / reflCount;

    const double prevGoal = stats[QStringLiteral("avg_goal_orientedness")].toDouble(0.5);
    stats[QStringLiteral("avg_goal_orientedness")] =
        prevGoal + (summary.behavior.goalOrientedness - prevGoal) / reflCount;

    QJsonObject emoFreq = stats[QStringLiteral("emotion_frequency")].toObject();
    emoFreq[summary.emotion.dominant] =
        emoFreq[summary.emotion.dominant].toInt(0) + 1;
    stats[QStringLiteral("emotion_frequency")] = emoFreq;
    profile[QStringLiteral("aggregate_stats")] = stats;

    // -- Latest behavioral metrics snapshot --
    profile[QStringLiteral("behavioral_metrics")] = summary.behavior.toJson();

    // -- Latest full summary --
    profile[QStringLiteral("latest_summary")] = summary.toJson();

    saveProfile(profile);
    qDebug() << "[ReflectionEngine] Profile JSON updated:" << profileJsonPath();
}

// ============================================================
//  Store Reflection into Vector Memory
// ============================================================

void ReflectionEngine::storeReflectionMemories(const UserSummary& summary)
{
    if (!m_memory) return;

    // Emotion trend
    m_memory->store(QStringLiteral("emotion"),
        QStringLiteral("User emotional state: %1 (confidence: %2). "
                        "Sample: %3 msgs. Signals: %4")
            .arg(summary.emotion.dominant)
            .arg(summary.emotion.confidence, 0, 'f', 2)
            .arg(summary.emotion.sampleCount)
            .arg(summary.emotion.evidenceWords.join(QStringLiteral(", "))),
        0.6 + summary.emotion.confidence * 0.3);

    // Productivity
    m_memory->store(QStringLiteral("reflection"),
        QStringLiteral("Productivity: %1 msgs, focus %2, dominant topic: %3, "
                        "%4 switches, %5 questions")
            .arg(summary.productivity.totalMessages)
            .arg(summary.productivity.focusScore, 0, 'f', 2)
            .arg(summary.productivity.dominantTopic)
            .arg(summary.productivity.topicSwitches)
            .arg(summary.productivity.questionCount),
        0.5);

    // Behavioral insight (synthesized conclusion)
    m_memory->storeInsight(
        QStringLiteral("User interaction style is '%1'. "
                        "Goal-orientedness: %2, context-switch rate: %3/10 msgs. "
                        "Focus trend: %4 (positive = improving).")
            .arg(summary.behavior.interactionStyle)
            .arg(summary.behavior.goalOrientedness, 0, 'f', 2)
            .arg(summary.behavior.contextSwitchRate, 0, 'f', 1)
            .arg(summary.behavior.focusEvolutionDelta, 0, 'f', 3),
        0.75);

    // Cognitive shifts
    for (const auto& cs : summary.cognitiveShifts) {
        m_memory->storeInsight(
            QStringLiteral("Cognitive shift: user changed stance on '%1' "
                            "from '%2' to '%3' (magnitude: %4)")
                .arg(cs.topic, cs.previousStance, cs.currentStance)
                .arg(cs.shiftMagnitude, 0, 'f', 2),
            0.7 + cs.shiftMagnitude * 0.2);
    }
}
