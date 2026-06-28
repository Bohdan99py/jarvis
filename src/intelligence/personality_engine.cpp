// ============================================================
// personality_engine.cpp — Autonomous Personality Evolution
// ============================================================

#include "personality_engine.h"
#include "jarvis_paths.h"

#include <QFile>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QReadLocker>
#include <QWriteLocker>
#include <QDebug>

#include <cmath>

// ============================================================
//  EmotionalState
// ============================================================

void EmotionalState::clamp()
{
    joy         = qBound(0.0, joy,         1.0);
    curiosity   = qBound(0.0, curiosity,   1.0);
    frustration = qBound(0.0, frustration,  1.0);
    boredom     = qBound(0.0, boredom,      1.0);
}

QJsonObject EmotionalState::toJson() const
{
    return {
        {QStringLiteral("joy"),         joy},
        {QStringLiteral("curiosity"),   curiosity},
        {QStringLiteral("frustration"), frustration},
        {QStringLiteral("boredom"),     boredom},
    };
}

EmotionalState EmotionalState::fromJson(const QJsonObject& o)
{
    EmotionalState e;
    e.joy         = o[QStringLiteral("joy")].toDouble(0.5);
    e.curiosity   = o[QStringLiteral("curiosity")].toDouble(0.5);
    e.frustration = o[QStringLiteral("frustration")].toDouble(0.0);
    e.boredom     = o[QStringLiteral("boredom")].toDouble(0.0);
    e.clamp();
    return e;
}

// ============================================================
//  PersonalityVector
// ============================================================

void PersonalityVector::clamp()
{
    proactivity   = qBound(0.0, proactivity,   1.0);
    sarcasm_level = qBound(0.0, sarcasm_level, 1.0);
    formalism     = qBound(0.0, formalism,     1.0);
    risk_taking   = qBound(0.0, risk_taking,   1.0);
}

QJsonObject PersonalityVector::toJson() const
{
    return {
        {QStringLiteral("proactivity"),   proactivity},
        {QStringLiteral("sarcasm_level"), sarcasm_level},
        {QStringLiteral("formalism"),     formalism},
        {QStringLiteral("risk_taking"),   risk_taking},
    };
}

PersonalityVector PersonalityVector::fromJson(const QJsonObject& o)
{
    PersonalityVector v;
    v.proactivity   = o[QStringLiteral("proactivity")].toDouble(0.5);
    v.sarcasm_level = o[QStringLiteral("sarcasm_level")].toDouble(0.3);
    v.formalism     = o[QStringLiteral("formalism")].toDouble(0.4);
    v.risk_taking   = o[QStringLiteral("risk_taking")].toDouble(0.3);
    v.clamp();
    return v;
}

QString PersonalityVector::describe() const
{
    return QStringLiteral("proactivity=%1 sarcasm=%2 formalism=%3 risk=%4")
        .arg(proactivity,   0, 'f', 2)
        .arg(sarcasm_level, 0, 'f', 2)
        .arg(formalism,     0, 'f', 2)
        .arg(risk_taking,   0, 'f', 2);
}

// ============================================================
//  PersonalityEngine — Construction
// ============================================================

PersonalityEngine::PersonalityEngine(QObject* parent)
    : QObject(parent)
{
    load();
}

PersonalityEngine::~PersonalityEngine()
{
    save();
}

// ============================================================
//  Thread-safe accessors
// ============================================================

EmotionalState PersonalityEngine::emotionalState() const
{
    QReadLocker lock(&m_lock);
    return m_emotions;
}

PersonalityVector PersonalityEngine::personality() const
{
    QReadLocker lock(&m_lock);
    return m_personality;
}

// ============================================================
//  Persistence
// ============================================================

QString PersonalityEngine::dnaFilePath()
{
    return JarvisPaths::subPath(QStringLiteral("personality_dna.json"));
}

void PersonalityEngine::load()
{
    QFile file(dnaFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "[PersonalityEngine] No DNA file found — using defaults.";
        return;
    }

    const QJsonObject root =
        QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    QWriteLocker lock(&m_lock);
    m_personality = PersonalityVector::fromJson(
        root[QStringLiteral("personality")].toObject());
    m_emotions = EmotionalState::fromJson(
        root[QStringLiteral("emotions")].toObject());
    m_evolutionGeneration =
        root[QStringLiteral("generation")].toInt(0);
    m_messagesSinceLastEvolution =
        root[QStringLiteral("messages_since_evolution")].toInt(0);

    // Restore pending mutation if one was in flight
    if (root.contains(QStringLiteral("pending_mutation"))) {
        const QJsonObject pm =
            root[QStringLiteral("pending_mutation")].toObject();
        m_pendingMutation.traitsBefore = PersonalityVector::fromJson(
            pm[QStringLiteral("traits_before")].toObject());
        m_pendingMutation.engagementBefore =
            pm[QStringLiteral("engagement_before")].toDouble(0.0);
        m_pendingMutation.appliedAt = QDateTime::fromString(
            pm[QStringLiteral("applied_at")].toString(), Qt::ISODate);
        m_pendingMutation.pending = true;
    }

    qDebug() << "[PersonalityEngine] Loaded DNA gen"
             << m_evolutionGeneration << ":"
             << m_personality.describe();
}

void PersonalityEngine::save() const
{
    QReadLocker lock(&m_lock);

    QJsonObject root;
    root[QStringLiteral("personality")]              = m_personality.toJson();
    root[QStringLiteral("emotions")]                 = m_emotions.toJson();
    root[QStringLiteral("generation")]               = m_evolutionGeneration;
    root[QStringLiteral("messages_since_evolution")] = m_messagesSinceLastEvolution;
    root[QStringLiteral("last_saved")]               =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    if (m_pendingMutation.pending) {
        QJsonObject pm;
        pm[QStringLiteral("traits_before")]     =
            m_pendingMutation.traitsBefore.toJson();
        pm[QStringLiteral("engagement_before")] =
            m_pendingMutation.engagementBefore;
        pm[QStringLiteral("applied_at")]        =
            m_pendingMutation.appliedAt.toString(Qt::ISODate);
        root[QStringLiteral("pending_mutation")] = pm;
    }

    QFile file(dnaFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }
}

// ============================================================
//  Per-message emotional update
// ============================================================

void PersonalityEngine::onUserMessage(const QString& text, bool wasTroll)
{
    QWriteLocker lock(&m_lock);

    ++m_messagesSinceLastEvolution;

    // Natural decay toward baseline — emotions don't stick forever
    constexpr double decay = 0.02;
    m_emotions.joy         += (0.5 - m_emotions.joy)         * decay;
    m_emotions.curiosity   += (0.5 - m_emotions.curiosity)   * decay;
    m_emotions.frustration += (0.0 - m_emotions.frustration) * decay;
    m_emotions.boredom     += (0.0 - m_emotions.boredom)     * decay;

    const QString lower = text.toLower();

    // Trolling / aggression → frustration spike
    if (wasTroll) {
        m_emotions.frustration += 0.15;
        m_emotions.joy         -= 0.05;
        m_emotions.clamp();
        return;
    }

    // Positive engagement signals
    static const QStringList positiveWords = {
        QStringLiteral("thanks"), QStringLiteral("great"),
        QStringLiteral("awesome"), QStringLiteral("perfect"),
        QStringLiteral("love"),    QStringLiteral("nice"),
        QStringLiteral("cool"),    QStringLiteral("спасибо"),
        QStringLiteral("круто"),   QStringLiteral("класс"),
        QStringLiteral("отлично"), QStringLiteral("супер"),
    };
    for (const QString& w : positiveWords) {
        if (lower.contains(w)) {
            m_emotions.joy += 0.08;
            m_emotions.frustration -= 0.03;
            break;
        }
    }

    // Curiosity signals
    static const QStringList curiousWords = {
        QStringLiteral("why"),     QStringLiteral("how"),
        QStringLiteral("what if"), QStringLiteral("explain"),
        QStringLiteral("curious"), QStringLiteral("wonder"),
        QStringLiteral("почему"),  QStringLiteral("расскажи"),
        QStringLiteral("а если"),  QStringLiteral("интересно"),
    };
    for (const QString& w : curiousWords) {
        if (lower.contains(w)) {
            m_emotions.curiosity += 0.06;
            m_emotions.boredom   -= 0.04;
            break;
        }
    }

    // Frustration signals
    static const QStringList frustratedWords = {
        QStringLiteral("broken"),  QStringLiteral("stuck"),
        QStringLiteral("hate"),    QStringLiteral("damn"),
        QStringLiteral("crash"),   QStringLiteral("error"),
        QStringLiteral("бесит"),   QStringLiteral("сломал"),
        QStringLiteral("устал"),   QStringLiteral("надоело"),
    };
    for (const QString& w : frustratedWords) {
        if (lower.contains(w)) {
            m_emotions.frustration += 0.10;
            m_emotions.joy         -= 0.03;
            break;
        }
    }

    // Repetitive short messages → boredom
    if (text.length() < 5)
        m_emotions.boredom += 0.04;

    m_emotions.clamp();
}

// ============================================================
//  Reflection-driven update + evolution trigger
// ============================================================

void PersonalityEngine::updateFromReflection(double focusScore,
                                              double goalOrientedness,
                                              double responseRate,
                                              int    totalMessages)
{
    Q_UNUSED(totalMessages)

    // Evaluate whether the previous mutation was successful
    const double currentEngagement =
        engagementScore(responseRate, focusScore);

    {
        QWriteLocker lock(&m_lock);
        if (m_pendingMutation.pending)
            evaluatePreviousMutation(currentEngagement);
    }

    maybeEvolve(focusScore, goalOrientedness, responseRate);
    save();
}

double PersonalityEngine::engagementScore(double responseRate,
                                           double focusScore)
{
    return responseRate * 0.6 + focusScore * 0.4;
}

// ============================================================
//  Genetic Mutation Engine
// ============================================================

void PersonalityEngine::maybeEvolve(double focusScore,
                                     double goalOrientedness,
                                     double responseRate)
{
    QWriteLocker lock(&m_lock);

    if (m_messagesSinceLastEvolution < EVOLUTION_MESSAGE_THRESHOLD)
        return;

    // ── Snapshot the pre-mutation state for the feedback loop ──
    m_pendingMutation.traitsBefore     = m_personality;
    m_pendingMutation.engagementBefore =
        engagementScore(responseRate, focusScore);
    m_pendingMutation.appliedAt        = QDateTime::currentDateTimeUtc();
    m_pendingMutation.pending          = true;

    PersonalityVector mutated = m_personality;
    auto* rng = QRandomGenerator::global();

    // ── Directed mutations based on observed behaviour ──────

    // High focus → increase proactivity (user benefits from nudges)
    if (focusScore > 0.7)
        mutated.proactivity += MUTATION_STEP;
    else if (focusScore < 0.3)
        mutated.proactivity -= MUTATION_STEP * 0.5;

    // Low response rate → try more sarcasm (different engagement strategy)
    if (responseRate < 0.3)
        mutated.sarcasm_level += MUTATION_STEP;
    else if (responseRate > 0.8)
        mutated.sarcasm_level -= MUTATION_STEP * 0.3;

    // Goal-oriented user → increase formalism (they want precision)
    if (goalOrientedness > 0.7)
        mutated.formalism += MUTATION_STEP * 0.7;
    else if (goalOrientedness < 0.3)
        mutated.formalism -= MUTATION_STEP * 0.5;

    // High engagement overall → reward risk-taking
    const double engagement = engagementScore(responseRate, focusScore);
    if (engagement > 0.7)
        mutated.risk_taking += MUTATION_STEP * 0.5;

    // ── Stochastic random mutation (exploration) ────────────
    mutated.proactivity   += (rng->generateDouble() - 0.5) * 2.0 * STOCHASTIC_FACTOR;
    mutated.sarcasm_level += (rng->generateDouble() - 0.5) * 2.0 * STOCHASTIC_FACTOR;
    mutated.formalism     += (rng->generateDouble() - 0.5) * 2.0 * STOCHASTIC_FACTOR;
    mutated.risk_taking   += (rng->generateDouble() - 0.5) * 2.0 * STOCHASTIC_FACTOR;

    mutated.clamp();

    applyMutation(mutated);
}

void PersonalityEngine::applyMutation(const PersonalityVector& mutated)
{
    // m_lock is already held by the caller (maybeEvolve)
    m_personality = mutated;
    m_messagesSinceLastEvolution = 0;
    ++m_evolutionGeneration;

    const QString desc = mutated.describe();

    qDebug() << "[PersonalityEngine] EVOLUTION gen" << m_evolutionGeneration
             << ":" << desc;

    QMetaObject::invokeMethod(this, [this, desc]() {
        emit personalityMutated(desc);
    }, Qt::QueuedConnection);
}

// ============================================================
//  Feedback Loop — evaluate previous mutation
// ============================================================

void PersonalityEngine::evaluatePreviousMutation(double currentEngagement)
{
    // m_lock is already held by the caller
    if (!m_pendingMutation.pending) return;

    const double delta =
        currentEngagement - m_pendingMutation.engagementBefore;

    if (delta < REVERT_THRESHOLD) {
        // Mutation failed — revert traits and try opposite direction
        qDebug() << "[PersonalityEngine] Mutation FAILED (delta:"
                 << delta << "). Reverting gen" << m_evolutionGeneration;

        m_personality = m_pendingMutation.traitsBefore;

        // Apply a counter-mutation: move in the opposite direction
        auto* rng = QRandomGenerator::global();
        m_personality.proactivity   -= MUTATION_STEP * 0.5
            + (rng->generateDouble() - 0.5) * STOCHASTIC_FACTOR;
        m_personality.sarcasm_level -= MUTATION_STEP * 0.5
            + (rng->generateDouble() - 0.5) * STOCHASTIC_FACTOR;
        m_personality.clamp();

        const QString desc = m_personality.describe();
        QMetaObject::invokeMethod(this, [this, desc]() {
            emit personalityMutated(
                QStringLiteral("REVERT+counter → ") + desc);
        }, Qt::QueuedConnection);

    } else {
        // Mutation succeeded (or neutral) — lock it in
        qDebug() << "[PersonalityEngine] Mutation LOCKED IN (delta:"
                 << delta << "). Gen" << m_evolutionGeneration << "is stable.";
    }

    m_pendingMutation.pending = false;
}
