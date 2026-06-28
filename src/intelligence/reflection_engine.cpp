// ============================================================
// reflection_engine.cpp — Autonomous Self-Reflection Loop
// ============================================================

#include "reflection_engine.h"
#include "memory_manager.h"
#include "social_presence.h"
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
#include <QtConcurrent>
#include <QDebug>

#include <algorithm>
#include <cmath>

// ============================================================
//  UserSummary JSON serialization
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
    QJsonArray signals;
    for (const auto& s : emotion.signals)
        signals.append(s);
    emo[QStringLiteral("signals")] = signals;
    obj[QStringLiteral("emotion")] = emo;

    // Productivity
    QJsonObject prod;
    prod[QStringLiteral("total_messages")]   = productivity.totalMessages;
    prod[QStringLiteral("question_count")]   = productivity.questionCount;
    prod[QStringLiteral("command_count")]    = productivity.commandCount;
    prod[QStringLiteral("topic_switches")]   = productivity.topicSwitches;
    prod[QStringLiteral("focus_score")]      = productivity.focusScore;
    prod[QStringLiteral("dominant_topic")]   = productivity.dominantTopic;
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

    if (!morningNudge.isEmpty())
        obj[QStringLiteral("morning_nudge")] = morningNudge;

    return obj;
}

// ============================================================
//  Emotion Lexicon
// ============================================================

const QList<ReflectionEngine::EmotionSignal>& ReflectionEngine::emotionLexicon()
{
    static const QList<EmotionSignal> lex = {
        // Positive
        {QStringLiteral("great"),      QStringLiteral("positive"), 0.8},
        {QStringLiteral("awesome"),    QStringLiteral("positive"), 0.9},
        {QStringLiteral("love"),       QStringLiteral("positive"), 0.7},
        {QStringLiteral("nice"),       QStringLiteral("positive"), 0.5},
        {QStringLiteral("perfect"),    QStringLiteral("positive"), 0.9},
        {QStringLiteral("thanks"),     QStringLiteral("positive"), 0.6},
        {QStringLiteral("cool"),       QStringLiteral("positive"), 0.5},
        {QStringLiteral("excellent"),  QStringLiteral("positive"), 0.9},
        {QStringLiteral("happy"),      QStringLiteral("positive"), 0.7},
        {QStringLiteral("класс"),      QStringLiteral("positive"), 0.7},
        {QStringLiteral("отлично"),    QStringLiteral("positive"), 0.8},
        {QStringLiteral("круто"),      QStringLiteral("positive"), 0.7},
        {QStringLiteral("спасибо"),    QStringLiteral("positive"), 0.6},
        {QStringLiteral("супер"),      QStringLiteral("positive"), 0.8},
        {QStringLiteral("кайф"),       QStringLiteral("positive"), 0.6},

        // Stressed / Frustrated
        {QStringLiteral("frustrated"), QStringLiteral("frustrated"), 0.9},
        {QStringLiteral("annoying"),   QStringLiteral("frustrated"), 0.7},
        {QStringLiteral("broken"),     QStringLiteral("frustrated"), 0.6},
        {QStringLiteral("hate"),       QStringLiteral("frustrated"), 0.8},
        {QStringLiteral("damn"),       QStringLiteral("frustrated"), 0.6},
        {QStringLiteral("stuck"),      QStringLiteral("stressed"),   0.7},
        {QStringLiteral("can't"),      QStringLiteral("stressed"),   0.4},
        {QStringLiteral("problem"),    QStringLiteral("stressed"),   0.4},
        {QStringLiteral("error"),      QStringLiteral("stressed"),   0.5},
        {QStringLiteral("bug"),        QStringLiteral("stressed"),   0.5},
        {QStringLiteral("crash"),      QStringLiteral("stressed"),   0.7},
        {QStringLiteral("бесит"),      QStringLiteral("frustrated"), 0.8},
        {QStringLiteral("сломал"),     QStringLiteral("frustrated"), 0.6},
        {QStringLiteral("задолбал"),   QStringLiteral("frustrated"), 0.9},
        {QStringLiteral("ошибка"),     QStringLiteral("stressed"),   0.5},
        {QStringLiteral("баг"),        QStringLiteral("stressed"),   0.5},
        {QStringLiteral("устал"),      QStringLiteral("stressed"),   0.7},
        {QStringLiteral("надоело"),    QStringLiteral("frustrated"), 0.7},

        // Curious
        {QStringLiteral("why"),        QStringLiteral("curious"), 0.5},
        {QStringLiteral("how"),        QStringLiteral("curious"), 0.4},
        {QStringLiteral("what if"),    QStringLiteral("curious"), 0.7},
        {QStringLiteral("wonder"),     QStringLiteral("curious"), 0.6},
        {QStringLiteral("interesting"),QStringLiteral("curious"), 0.6},
        {QStringLiteral("curious"),    QStringLiteral("curious"), 0.8},
        {QStringLiteral("explain"),    QStringLiteral("curious"), 0.5},
        {QStringLiteral("почему"),     QStringLiteral("curious"), 0.5},
        {QStringLiteral("интересно"), QStringLiteral("curious"), 0.6},
        {QStringLiteral("а если"),     QStringLiteral("curious"), 0.7},
        {QStringLiteral("расскажи"),   QStringLiteral("curious"), 0.5},
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
    qDebug() << "[ReflectionEngine] Stopped.";
}

bool ReflectionEngine::isRunning() const
{
    return m_checkTimer->isActive();
}

UserSummary ReflectionEngine::lastSummary() const
{
    QMutexLocker lock(&m_summaryMutex);
    return m_lastSummary;
}

QString ReflectionEngine::profileJsonPath()
{
    return JarvisPaths::subPath(QStringLiteral("user_profile_evolving.json"));
}

// ============================================================
//  Timer Check — decides whether to reflect
// ============================================================

void ReflectionEngine::onCheckTimer()
{
    if (!shouldReflect()) return;

    // Run reflection in a background thread to never block main loop
    (void)QtConcurrent::run([this]() {
        runReflectionCycle();
    });
}

bool ReflectionEngine::shouldReflect() const
{
    // Only reflect when user is inactive
    if (m_presence) {
        const auto status = m_presence->currentStatus();
        if (status == UserState::FREE)
            return false; // user is actively engaging
    }

    // Respect minimum cooldown between reflections
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
    summary.generatedAt = QDateTime::currentDateTimeUtc();
    summary.emotion       = analyzeEmotions(dialogue);
    summary.productivity  = analyzeProductivity(dialogue);
    summary.cognitiveShifts = detectCognitiveShifts(dialogue);
    summary.morningNudge  = composeMorningNudge(summary);

    // Store results
    {
        QMutexLocker lock(&m_summaryMutex);
        m_lastSummary = summary;
    }
    m_lastReflectionTime = QDateTime::currentDateTime();

    // Update profile JSON (thread-safe file I/O)
    updateProfileJson(summary);

    // Store reflection insights into vector memory
    if (m_memory)
        storeReflectionMemories(summary);

    // Check if we should queue a morning nudge
    const int today = QDate::currentDate().dayOfYear();
    if (m_lastReflectionDay != today) {
        m_lastReflectionDay = today;
        m_morningNudgePending = true;

        QMetaObject::invokeMethod(this, [this, summary]() {
            emit morningNudgeReady(summary.morningNudge);
        }, Qt::QueuedConnection);
    }

    QMetaObject::invokeMethod(this, [this, summary]() {
        emit reflectionComplete(summary);
        emit profileUpdated(profileJsonPath());
    }, Qt::QueuedConnection);

    qDebug() << "[ReflectionEngine] Reflection complete."
             << "Emotion:" << summary.emotion.dominant
             << "Focus:" << summary.productivity.focusScore
             << "Shifts:" << summary.cognitiveShifts.size();
}

// ============================================================
//  Fetch Last 24h Dialogue from DB
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

    while (q.next()) {
        dialogue.append({
            q.value(0).toString(),   // role
            q.value(1).toString()    // content
        });
    }

    return dialogue;
}

// ============================================================
//  Emotion Analysis
// ============================================================

EmotionTrend ReflectionEngine::analyzeEmotions(
    const QList<QPair<QString, QString>>& dialogue) const
{
    EmotionTrend result;
    result.dominant   = QStringLiteral("neutral");
    result.confidence = 0.0;

    QMap<QString, double> emotionScores;
    int userMsgCount = 0;

    for (const auto& [role, content] : dialogue) {
        if (role != QStringLiteral("user")) continue;
        ++userMsgCount;

        const QString lower = content.toLower();
        for (const auto& signal : emotionLexicon()) {
            if (lower.contains(signal.keyword)) {
                emotionScores[signal.emotion] += signal.weight;
                if (result.signals.size() < 10)
                    result.signals.append(signal.keyword);
            }
        }
    }

    result.sampleCount = userMsgCount;
    if (userMsgCount == 0) return result;

    // Find dominant emotion
    double maxScore = 0.0;
    for (auto it = emotionScores.constBegin();
         it != emotionScores.constEnd(); ++it) {
        if (it.value() > maxScore) {
            maxScore = it.value();
            result.dominant = it.key();
        }
    }

    // Confidence = normalized score (0–1)
    const double totalWeight = userMsgCount * 2.0; // theoretical max ~2 signals/msg
    result.confidence = qMin(1.0, maxScore / qMax(1.0, totalWeight));

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

    static const QRegularExpression questionRx(
        QStringLiteral("[?？]"));
    static const QRegularExpression commandRx(
        QStringLiteral("^/\\w+"));

    for (const auto& [role, content] : dialogue) {
        ++result.totalMessages;

        if (role != QStringLiteral("user")) continue;

        // Count questions
        if (content.contains(questionRx))
            ++result.questionCount;

        // Count commands
        if (content.trimmed().startsWith(QLatin1Char('/')))
            ++result.commandCount;

        // Extract topic tokens (top 3 meaningful words)
        const QStringList tokens = MemoryManager::tokenize(content);
        QString topic;
        if (!tokens.isEmpty())
            topic = tokens.first();

        if (!topic.isEmpty()) {
            topicFreq[topic] += 1;
            if (!prevTopic.isEmpty() && topic != prevTopic)
                ++result.topicSwitches;
            prevTopic = topic;
        }
    }

    // Determine dominant topic and topics list
    int maxFreq = 0;
    for (auto it = topicFreq.constBegin(); it != topicFreq.constEnd(); ++it) {
        if (result.topicsDiscussed.size() < 10)
            result.topicsDiscussed.append(it.key());
        if (it.value() > maxFreq) {
            maxFreq = it.value();
            result.dominantTopic = it.key();
        }
    }

    // Focus score: inverse of topic switch rate
    const int userMsgs = result.totalMessages / 2; // rough estimate
    if (userMsgs > 1) {
        const double switchRate = static_cast<double>(result.topicSwitches) / userMsgs;
        result.focusScore = qMax(0.0, 1.0 - switchRate);
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

    // Detect opinion reversals via contradiction markers
    static const QStringList reversalMarkers = {
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
        for (const QString& marker : reversalMarkers) {
            if (lower.contains(marker)) {
                CognitiveShift cs;
                cs.topic = MemoryManager::tokenize(content).value(0, QStringLiteral("general"));
                cs.previousStance = prevUserMsg.left(100);
                cs.currentStance  = content.left(100);
                cs.shiftMagnitude = 0.5; // baseline
                cs.detectedAt = QDateTime::currentDateTimeUtc();

                // Higher magnitude if the reversal is more explicit
                if (lower.contains(QStringLiteral("wrong"))
                    || lower.contains(QStringLiteral("ошибся")))
                    cs.shiftMagnitude = 0.8;

                if (cs.shiftMagnitude >= COGNITIVE_SHIFT_THRESHOLD)
                    shifts.append(cs);

                break; // one shift per message
            }
        }
        prevUserMsg = content;
    }

    return shifts;
}

// ============================================================
//  Morning Nudge Composition
// ============================================================

QString ReflectionEngine::composeMorningNudge(const UserSummary& summary) const
{
    QString nudge;
    nudge += QStringLiteral("Good morning! Here's what I noticed from yesterday:\n\n");

    // Emotion summary
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

    // Productivity insight
    if (summary.productivity.focusScore > 0.7)
        nudge += QStringLiteral("Your focus was *excellent* — sustained deep work. Impressive.\n");
    else if (summary.productivity.focusScore < 0.3)
        nudge += QStringLiteral("Yesterday was pretty scattered topic-wise. Want to pick *one big thing* to tackle today?\n");

    if (!summary.productivity.dominantTopic.isEmpty())
        nudge += QStringLiteral("Main area: *%1* (%2 messages).\n")
            .arg(summary.productivity.dominantTopic)
            .arg(summary.productivity.totalMessages);

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

    // Metadata
    profile[QStringLiteral("last_updated")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    profile[QStringLiteral("schema_version")] = 1;

    // Emotion history (rolling window of last 30 entries)
    QJsonArray emotionHistory = profile[QStringLiteral("emotion_history")].toArray();
    QJsonObject emotionEntry;
    emotionEntry[QStringLiteral("date")]       = summary.generatedAt.toString(Qt::ISODate);
    emotionEntry[QStringLiteral("dominant")]    = summary.emotion.dominant;
    emotionEntry[QStringLiteral("confidence")]  = summary.emotion.confidence;
    emotionHistory.append(emotionEntry);
    while (emotionHistory.size() > 30)
        emotionHistory.removeFirst();
    profile[QStringLiteral("emotion_history")] = emotionHistory;

    // Productivity history (rolling window of last 30)
    QJsonArray prodHistory = profile[QStringLiteral("productivity_history")].toArray();
    QJsonObject prodEntry;
    prodEntry[QStringLiteral("date")]          = summary.generatedAt.toString(Qt::ISODate);
    prodEntry[QStringLiteral("focus_score")]   = summary.productivity.focusScore;
    prodEntry[QStringLiteral("messages")]      = summary.productivity.totalMessages;
    prodEntry[QStringLiteral("questions")]     = summary.productivity.questionCount;
    prodEntry[QStringLiteral("topic_switches")]= summary.productivity.topicSwitches;
    prodEntry[QStringLiteral("dominant_topic")]= summary.productivity.dominantTopic;
    prodHistory.append(prodEntry);
    while (prodHistory.size() > 30)
        prodHistory.removeFirst();
    profile[QStringLiteral("productivity_history")] = prodHistory;

    // Cognitive shifts (append all, cap at 50)
    QJsonArray shiftHistory = profile[QStringLiteral("cognitive_shifts")].toArray();
    for (const auto& cs : summary.cognitiveShifts) {
        QJsonObject s;
        s[QStringLiteral("topic")]     = cs.topic;
        s[QStringLiteral("from")]      = cs.previousStance;
        s[QStringLiteral("to")]        = cs.currentStance;
        s[QStringLiteral("magnitude")] = cs.shiftMagnitude;
        s[QStringLiteral("date")]      = cs.detectedAt.toString(Qt::ISODate);
        shiftHistory.append(s);
    }
    while (shiftHistory.size() > 50)
        shiftHistory.removeFirst();
    profile[QStringLiteral("cognitive_shifts")] = shiftHistory;

    // Aggregate stats
    QJsonObject stats = profile[QStringLiteral("aggregate_stats")].toObject();
    stats[QStringLiteral("total_reflections")] =
        stats[QStringLiteral("total_reflections")].toInt(0) + 1;
    stats[QStringLiteral("total_messages_analyzed")] =
        stats[QStringLiteral("total_messages_analyzed")].toInt(0)
        + summary.productivity.totalMessages;
    stats[QStringLiteral("total_cognitive_shifts")] =
        stats[QStringLiteral("total_cognitive_shifts")].toInt(0)
        + summary.cognitiveShifts.size();

    // Running average focus score
    const int reflCount = stats[QStringLiteral("total_reflections")].toInt(1);
    const double prevAvg = stats[QStringLiteral("avg_focus_score")].toDouble(0.5);
    stats[QStringLiteral("avg_focus_score")] =
        prevAvg + (summary.productivity.focusScore - prevAvg) / reflCount;

    // Dominant emotion frequency
    QJsonObject emoFreq = stats[QStringLiteral("emotion_frequency")].toObject();
    emoFreq[summary.emotion.dominant] =
        emoFreq[summary.emotion.dominant].toInt(0) + 1;
    stats[QStringLiteral("emotion_frequency")] = emoFreq;

    profile[QStringLiteral("aggregate_stats")] = stats;

    // Latest summary snapshot
    profile[QStringLiteral("latest_summary")] = summary.toJson();

    saveProfile(profile);

    qDebug() << "[ReflectionEngine] Profile JSON updated:"
             << profileJsonPath();
}

// ============================================================
//  Store Reflection into Vector Memory
// ============================================================

void ReflectionEngine::storeReflectionMemories(const UserSummary& summary)
{
    if (!m_memory) return;

    // Store emotion trend as a memory chunk
    const QString emotionContent = QStringLiteral(
        "User emotional state: %1 (confidence: %2). "
        "Sample size: %3 messages. Signals: %4")
        .arg(summary.emotion.dominant)
        .arg(summary.emotion.confidence, 0, 'f', 2)
        .arg(summary.emotion.sampleCount)
        .arg(summary.emotion.signals.join(QStringLiteral(", ")));

    m_memory->store(QStringLiteral("emotion"),
                    emotionContent,
                    0.6 + summary.emotion.confidence * 0.3);

    // Store productivity pattern
    const QString prodContent = QStringLiteral(
        "Productivity analysis: %1 messages, focus score %2, "
        "dominant topic: %3, %4 topic switches, %5 questions asked")
        .arg(summary.productivity.totalMessages)
        .arg(summary.productivity.focusScore, 0, 'f', 2)
        .arg(summary.productivity.dominantTopic)
        .arg(summary.productivity.topicSwitches)
        .arg(summary.productivity.questionCount);

    m_memory->store(QStringLiteral("reflection"),
                    prodContent, 0.5);

    // Store each cognitive shift
    for (const auto& cs : summary.cognitiveShifts) {
        const QString shiftContent = QStringLiteral(
            "Cognitive shift detected: user changed stance on '%1' "
            "from '%2' to '%3' (magnitude: %4)")
            .arg(cs.topic, cs.previousStance, cs.currentStance)
            .arg(cs.shiftMagnitude, 0, 'f', 2);

        m_memory->store(QStringLiteral("insight"),
                        shiftContent,
                        0.7 + cs.shiftMagnitude * 0.2);
    }
}
