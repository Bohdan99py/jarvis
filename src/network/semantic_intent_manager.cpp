// ============================================================
// semantic_intent_manager.cpp — Goal-Oriented Intent Classifier
// ============================================================

#include "semantic_intent_manager.h"
#include "j2j_telegram_gateway.h"
#include "applauncher.h"

#include <QDesktopServices>
#include <QUrl>
#include <QRandomGenerator>
#include <QDebug>

#include <algorithm>
#include <cmath>

// ============================================================
//  Construction + Goal Registry
// ============================================================

SemanticIntentManager::SemanticIntentManager(J2JTelegramGateway* gateway,
                                             QObject* parent)
    : QObject(parent)
    , m_gateway(gateway)
{
    buildGoalRegistry();
    qDebug() << "[SemanticIntent] Initialized with"
             << m_goals.size() << "goal mappings.";
}

void SemanticIntentManager::buildGoalRegistry()
{
    // ── Job Search ──────────────────────────────────────────
    m_goals.append({
        QStringLiteral("job_search"),
        QStringLiteral("Job Search"),
        {
            QStringLiteral("найди работу"),
            QStringLiteral("ищу работу"),
            QStringLiteral("поиск работы"),
            QStringLiteral("вакансии"),
            QStringLiteral("find work"),
            QStringLiteral("find a job"),
            QStringLiteral("job search"),
            QStringLiteral("job hunting"),
            QStringLiteral("linkedin"),
            QStringLiteral("hh.ru"),
        },
        {
            {IntentAction::OpenUrl, QStringLiteral("https://hh.ru"), QStringLiteral("HeadHunter")},
            {IntentAction::OpenUrl, QStringLiteral("https://www.linkedin.com/jobs/"), QStringLiteral("LinkedIn Jobs")},
        }
    });

    // ── Music ───────────────────────────────────────────────
    m_goals.append({
        QStringLiteral("music"),
        QStringLiteral("Music"),
        {
            QStringLiteral("хочу музыки"),
            QStringLiteral("включи музыку"),
            QStringLiteral("музыка"),
            QStringLiteral("музон"),
            QStringLiteral("play music"),
            QStringLiteral("want music"),
            QStringLiteral("put on music"),
            QStringLiteral("youtube music"),
            QStringLiteral("spotify"),
        },
        {
            {IntentAction::OpenUrl, QStringLiteral("https://music.youtube.com"), QStringLiteral("YouTube Music")},
        }
    });

    // ── Relax / Entertainment ───────────────────────────────
    m_goals.append({
        QStringLiteral("relax"),
        QStringLiteral("Relax"),
        {
            QStringLiteral("отдохнуть"),
            QStringLiteral("расслабиться"),
            QStringLiteral("хочу отдохнуть"),
            QStringLiteral("отдых"),
            QStringLiteral("залипнуть"),
            QStringLiteral("relax"),
            QStringLiteral("chill"),
            QStringLiteral("want to relax"),
            QStringLiteral("take a break"),
        },
        {
            {IntentAction::OpenUrl, QStringLiteral("https://www.youtube.com"), QStringLiteral("YouTube")},
        }
    });

    // ── Productivity / Deep Work ────────────────────────────
    m_goals.append({
        QStringLiteral("productivity"),
        QStringLiteral("Productivity Mode"),
        {
            QStringLiteral("продуктивность"),
            QStringLiteral("режим работы"),
            QStringLiteral("deep work"),
            QStringLiteral("надо поработать"),
            QStringLiteral("время кодить"),
            QStringLiteral("productivity"),
            QStringLiteral("focus mode"),
            QStringLiteral("work mode"),
            QStringLiteral("time to code"),
        },
        {
            {IntentAction::LaunchApp, QStringLiteral("clion"), QStringLiteral("CLion IDE")},
        }
    });

    // ── News / Current Events ───────────────────────────────
    m_goals.append({
        QStringLiteral("news"),
        QStringLiteral("News"),
        {
            QStringLiteral("новости"),
            QStringLiteral("что в мире"),
            QStringLiteral("what's happening"),
            QStringLiteral("news"),
            QStringLiteral("current events"),
        },
        {
            {IntentAction::OpenUrl, QStringLiteral("https://news.google.com"), QStringLiteral("Google News")},
        }
    });

    // ── Email ───────────────────────────────────────────────
    m_goals.append({
        QStringLiteral("email"),
        QStringLiteral("Email"),
        {
            QStringLiteral("почта"),
            QStringLiteral("проверь почту"),
            QStringLiteral("email"),
            QStringLiteral("check email"),
            QStringLiteral("check mail"),
            QStringLiteral("gmail"),
        },
        {
            {IntentAction::OpenUrl, QStringLiteral("https://mail.google.com"), QStringLiteral("Gmail")},
        }
    });

    // ── Social Media ────────────────────────────────────────
    m_goals.append({
        QStringLiteral("social"),
        QStringLiteral("Social Media"),
        {
            QStringLiteral("соцсети"),
            QStringLiteral("инстаграм"),
            QStringLiteral("instagram"),
            QStringLiteral("social media"),
            QStringLiteral("twitter"),
            QStringLiteral("x.com"),
        },
        {
            {IntentAction::OpenUrl, QStringLiteral("https://www.instagram.com"), QStringLiteral("Instagram")},
        }
    });

    // ── Gaming ──────────────────────────────────────────────
    m_goals.append({
        QStringLiteral("gaming"),
        QStringLiteral("Gaming"),
        {
            QStringLiteral("поиграть"),
            QStringLiteral("хочу играть"),
            QStringLiteral("запусти игры"),
            QStringLiteral("gaming"),
            QStringLiteral("play games"),
            QStringLiteral("want to play"),
            QStringLiteral("steam"),
        },
        {
            {IntentAction::LaunchApp, QStringLiteral("steam"), QStringLiteral("Steam")},
        }
    });

    // ── Search / Research ───────────────────────────────────
    m_goals.append({
        QStringLiteral("search"),
        QStringLiteral("Web Search"),
        {
            QStringLiteral("загугли"),
            QStringLiteral("погугли"),
            QStringLiteral("найди в интернете"),
            QStringLiteral("поиск в сети"),
            QStringLiteral("google it"),
            QStringLiteral("search for"),
            QStringLiteral("look up"),
        },
        {
            {IntentAction::OpenUrl, QStringLiteral("https://www.google.com"), QStringLiteral("Google Search")},
        }
    });

    // ── Video Call ──────────────────────────────────────────
    m_goals.append({
        QStringLiteral("video_call"),
        QStringLiteral("Video Call"),
        {
            QStringLiteral("видеозвонок"),
            QStringLiteral("созвон"),
            QStringLiteral("video call"),
            QStringLiteral("zoom"),
            QStringLiteral("google meet"),
            QStringLiteral("teams"),
        },
        {
            {IntentAction::OpenUrl, QStringLiteral("https://meet.google.com"), QStringLiteral("Google Meet")},
        }
    });
}

// ============================================================
//  Intent Classification
// ============================================================

IntentMatch SemanticIntentManager::classifyIntent(const QString& text) const
{
    const QString lower = text.toLower().trimmed();

    // Skip very short messages or slash commands
    if (lower.length() < 3 || lower.startsWith(QLatin1Char('/')))
        return {};

    IntentMatch bestMatch;

    for (const auto& goal : m_goals) {
        const double score = scoreMatch(lower, goal);
        if (score > bestMatch.confidence) {
            bestMatch.matched    = true;
            bestMatch.goalId     = goal.goalId;
            bestMatch.goalLabel  = goal.goalLabel;
            bestMatch.confidence = score;
            bestMatch.actions    = goal.actions;
        }
    }

    // Below 0.50 is noise — treat as conversational
    if (bestMatch.confidence < 0.50)
        bestMatch.matched = false;

    return bestMatch;
}

double SemanticIntentManager::scoreMatch(const QString& lower,
                                          const GoalEntry& goal) const
{
    double bestPhraseScore = 0.0;

    for (const QString& phrase : goal.triggerPhrases) {
        if (lower.contains(phrase)) {
            // Exact containment — high base score
            const double lengthRatio =
                static_cast<double>(phrase.length()) / qMax(1, lower.length());

            // Longer phrase match relative to message = higher confidence.
            // A message that IS the trigger phrase scores ~0.95.
            // A message that just contains it in a larger sentence scores lower.
            double score = 0.55 + lengthRatio * 0.40;

            // Bonus if the phrase starts at the beginning
            if (lower.startsWith(phrase))
                score += 0.05;

            bestPhraseScore = qMax(bestPhraseScore, score);
        }
    }

    return qMin(bestPhraseScore, 1.0);
}

bool SemanticIntentManager::isConversational(const IntentMatch& match)
{
    return !match.matched || match.confidence < 0.50;
}

// ============================================================
//  Action Execution
// ============================================================

QString SemanticIntentManager::executeActions(const IntentMatch& match)
{
    if (!match.matched || match.actions.isEmpty())
        return QString();

    int successes = 0;
    QStringList launched;

    AppLauncher launcher;

    for (const auto& action : match.actions) {
        switch (action.type) {

        case IntentAction::OpenUrl: {
            const bool ok = QDesktopServices::openUrl(QUrl(action.target));
            if (ok) {
                ++successes;
                launched.append(action.label);
            }
            qDebug() << "[SemanticIntent] OpenUrl:" << action.target
                     << (ok ? "OK" : "FAILED");
            break;
        }

        case IntentAction::LaunchApp: {
            auto result = launcher.launch(action.target);
            if (result.success) {
                ++successes;
                launched.append(action.label);
            }
            qDebug() << "[SemanticIntent] LaunchApp:" << action.target
                     << (result.success ? "OK" : result.errorMessage);
            break;
        }

        case IntentAction::SystemCommand:
            // Future expansion point
            break;
        }
    }

    emit intentExecuted(match.goalId, successes);

    // Build an acknowledgement with personality
    if (successes == 0)
        return QStringLiteral("Tried, failed. The system has opinions today.");

    static const QStringList acks = {
        QStringLiteral("Done. You're welcome, genius."),
        QStringLiteral("Task initiated. Try not to get distracted."),
        QStringLiteral("Launched. I expect a thank-you note."),
        QStringLiteral("On it. Your wish is my `ShellExecuteW`."),
        QStringLiteral("Готово. Не благодари — я и так знаю, что незаменим."),
        QStringLiteral("Запустил. Постарайся не отвлекаться."),
        QStringLiteral("Сделано. Можешь начинать аплодировать."),
        QStringLiteral("Принято к исполнению. Как всегда, без 'пожалуйста'."),
    };

    const int idx = QRandomGenerator::global()->bounded(acks.size());
    QString reply = acks.at(idx);

    reply += QStringLiteral("\n\n📎 ") + launched.join(QStringLiteral(", "));

    return reply;
}
