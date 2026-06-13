// -------------------------------------------------------
// user_profile.cpp — Профиль предпочтений пользователя
// -------------------------------------------------------

#include "user_profile.h"

#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QMap>
#include <algorithm>
#include <cmath>

// ============================================================
// Конструктор / Деструктор
// ============================================================

UserProfile::UserProfile(QObject* parent)
    : QObject(parent)
{
    load();
}

UserProfile::~UserProfile()
{
    save();
}

// ============================================================
// Классификация сценария (эвристика, без ML)
// ============================================================

UserProfile::Scenario UserProfile::classifyScenario(const ContextSnapshot& ctx)
{
    // 1. Unreal Engine активен — однозначно разработка игр
    if (ctx.isInUE5Context()) return Scenario::GameDev;

    const QString proc = ctx.activeWindowProcess.toLower();

    // 2. IDE / редакторы кода — разработка
    static const QStringList devProcs = {
        QStringLiteral("clion64.exe"),   QStringLiteral("clion.exe"),
        QStringLiteral("rider64.exe"),   QStringLiteral("rider.exe"),
        QStringLiteral("devenv.exe"),    QStringLiteral("code.exe"),
        QStringLiteral("pycharm64.exe"), QStringLiteral("webstorm64.exe"),
    };
    if (devProcs.contains(proc))  return Scenario::GameDev;
    if (ctx.isInCodingContext())  return Scenario::GameDev;

    // 3. Графика / 3D — художник
    static const QStringList artProcs = {
        QStringLiteral("krita.exe"), QStringLiteral("photoshop.exe"),
        QStringLiteral("blender.exe"),
    };
    if (artProcs.contains(proc)) return Scenario::Art;

    // 4. Браузер активен
    if (ctx.isInBrowserContext()) return Scenario::Browsing;

    // 5. Активное окно не похоже на известное служебное приложение,
    //    а Steam/Epic запущены в фоне — вероятно, сейчас игра.
    static const QStringList nonGameProcs = {
        QStringLiteral("explorer.exe"), QStringLiteral("notepad.exe"),
        QStringLiteral("cmd.exe"),      QStringLiteral("powershell.exe"),
        QStringLiteral("taskmgr.exe"),  QStringLiteral("jarvis.exe"),
        QStringLiteral("steam.exe"),    QStringLiteral("epicgameslauncher.exe"),
        QStringLiteral("discord.exe"),  QStringLiteral("obs64.exe"),
        QStringLiteral("telegram.exe"), QStringLiteral("spotify.exe"),
    };

    const bool launcherRunning =
        ctx.runningApps.contains(QStringLiteral("steam.exe"))
        || ctx.runningApps.filter(QStringLiteral("epicgames"), Qt::CaseInsensitive).size() > 0;

    if (launcherRunning && !proc.isEmpty()
        && !nonGameProcs.contains(proc)
        && !devProcs.contains(proc)
        && !artProcs.contains(proc))
    {
        return Scenario::Gaming;
    }

    // 6. Ничего конкретного — обычный диалог
    return Scenario::Chat;
}

QString UserProfile::scenarioName(Scenario s)
{
    switch (s) {
    case Scenario::GameDev:  return QStringLiteral("Разработка");
    case Scenario::Art:      return QStringLiteral("Рисование/3D");
    case Scenario::Gaming:   return QStringLiteral("Игра");
    case Scenario::Browsing: return QStringLiteral("Браузер");
    case Scenario::Chat:     return QStringLiteral("Общение");
    default:                 return QStringLiteral("Неопределено");
    }
}

QString UserProfile::scenarioKey(Scenario s)
{
    switch (s) {
    case Scenario::GameDev:  return QStringLiteral("gamedev");
    case Scenario::Art:      return QStringLiteral("art");
    case Scenario::Gaming:   return QStringLiteral("gaming");
    case Scenario::Browsing: return QStringLiteral("browsing");
    case Scenario::Chat:     return QStringLiteral("chat");
    default:                 return QStringLiteral("unknown");
    }
}

// ============================================================
// Временные слоты
// ============================================================

int UserProfile::timeBucket(int hourOfDay)
{
    if (hourOfDay < 6)  return 0; // ночь
    if (hourOfDay < 12) return 1; // утро
    if (hourOfDay < 18) return 2; // день
    return 3;                     // вечер
}

QString UserProfile::timeBucketName(int bucket)
{
    switch (bucket) {
    case 0: return QStringLiteral("ночь");
    case 1: return QStringLiteral("утро");
    case 2: return QStringLiteral("день");
    case 3: return QStringLiteral("вечер");
    default: return QStringLiteral("?");
    }
}

// ============================================================
// Утилиты
// ============================================================

QString UserProfile::commandKeyOf(const QString& input)
{
    const QString lower = input.trimmed().toLower();
    if (lower.isEmpty()) return QString();

    const int sp = lower.indexOf(QChar(' '));
    return sp > 0 ? lower.left(sp) : lower;
}

// ============================================================
// Обучение
// ============================================================

void UserProfile::recordObservation(const ContextSnapshot& ctx, const QString& userInput)
{
    const QString key = commandKeyOf(userInput);
    if (key.isEmpty()) return;

    applyDecayIfNeeded();

    const Scenario newScenario = classifyScenario(ctx);
    const Scenario before      = currentScenario();

    m_recentScenarios.append(newScenario);
    while (m_recentScenarios.size() > MAX_RECENT) {
        m_recentScenarios.removeFirst();
    }

    const QString scKey = scenarioKey(newScenario);
    const QString bKey  = QString::number(timeBucket(ctx.hourOfDay));

    QJsonObject scenarioObj = m_weights[scKey].toObject();
    QJsonObject bucketObj   = scenarioObj[bKey].toObject();

    const double w = bucketObj[key].toDouble(0.0);
    bucketObj[key] = w + 1.0;

    scenarioObj[bKey] = bucketObj;
    m_weights[scKey]  = scenarioObj;

    save();

    const Scenario after = currentScenario();
    if (after != before) {
        emit scenarioChanged(after);
    }
}

UserProfile::Scenario UserProfile::currentScenario() const
{
    if (m_recentScenarios.isEmpty()) return Scenario::Unknown;

    QMap<Scenario, int> counts;
    for (const auto& s : m_recentScenarios) {
        counts[s] = counts.value(s, 0) + 1;
    }

    Scenario best  = m_recentScenarios.last(); // на равных — отдаём приоритет последнему
    int bestCount  = 0;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > bestCount) {
            bestCount = it.value();
            best      = it.key();
        }
    }
    return best;
}

QStringList UserProfile::topCommands(Scenario scenario, int bucket, int maxResults) const
{
    QStringList result;

    const QJsonObject scenarioObj = m_weights[scenarioKey(scenario)].toObject();
    const QJsonObject bucketObj   = scenarioObj[QString::number(bucket)].toObject();

    QVector<QPair<QString, double>> sorted;
    sorted.reserve(bucketObj.size());
    for (auto it = bucketObj.begin(); it != bucketObj.end(); ++it) {
        sorted.append({it.key(), it.value().toDouble()});
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    for (const auto& [cmd, weight] : sorted) {
        if (weight < MIN_WEIGHT) continue;
        result.append(cmd);
        if (result.size() >= maxResults) break;
    }
    return result;
}

QString UserProfile::buildProfileSummary(int maxLines) const
{
    struct Entry {
        Scenario    scenario;
        int         bucket;
        double      total;
        QStringList cmds;
    };

    static const Scenario kAllScenarios[] = {
        Scenario::GameDev, Scenario::Art, Scenario::Gaming,
        Scenario::Browsing, Scenario::Chat
    };

    QVector<Entry> entries;
    for (Scenario sc : kAllScenarios) {
        const QJsonObject scenarioObj = m_weights[scenarioKey(sc)].toObject();
        for (int bucket = 0; bucket < 4; ++bucket) {
            const QJsonObject bucketObj = scenarioObj[QString::number(bucket)].toObject();
            if (bucketObj.isEmpty()) continue;

            double total = 0.0;
            for (auto it = bucketObj.begin(); it != bucketObj.end(); ++it) {
                total += it.value().toDouble();
            }
            if (total < 1.0) continue; // слишком мало наблюдений — пока не показываем

            entries.append({sc, bucket, total, topCommands(sc, bucket, 3)});
        }
    }

    if (entries.isEmpty()) {
        return QStringLiteral(
            "Профиль пока пуст — JARVIS только начал учиться на твоих командах.");
    }

    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.total > b.total; });

    QString text = QStringLiteral("Текущий сценарий: ") + scenarioName(currentScenario())
                 + QStringLiteral("\nЧастые паттерны:\n");

    int shown = 0;
    for (const auto& e : entries) {
        if (e.cmds.isEmpty()) continue;
        text += QStringLiteral("- ") + scenarioName(e.scenario)
              + QStringLiteral(" (") + timeBucketName(e.bucket) + QStringLiteral("): ")
              + e.cmds.join(QStringLiteral(", ")) + QStringLiteral("\n");
        if (++shown >= maxLines) break;
    }

    return text.trimmed();
}

// ============================================================
// Затухание (старое забывается со временем)
// ============================================================

void UserProfile::applyDecayIfNeeded()
{
    const QDate today = QDate::currentDate();
    if (!m_lastDecay.isValid()) {
        m_lastDecay = today;
        return;
    }

    const qint64 days = m_lastDecay.daysTo(today);
    if (days <= 0) return;

    const double factor = std::pow(DECAY_PER_DAY, days);

    QJsonObject newWeights;
    for (auto scIt = m_weights.begin(); scIt != m_weights.end(); ++scIt) {
        const QJsonObject scenarioObj = scIt.value().toObject();
        QJsonObject newScenarioObj;

        for (auto bIt = scenarioObj.begin(); bIt != scenarioObj.end(); ++bIt) {
            const QJsonObject bucketObj = bIt.value().toObject();
            QJsonObject newBucketObj;

            for (auto cIt = bucketObj.begin(); cIt != bucketObj.end(); ++cIt) {
                const double w = cIt.value().toDouble() * factor;
                if (w >= MIN_WEIGHT) newBucketObj[cIt.key()] = w;
            }
            if (!newBucketObj.isEmpty()) newScenarioObj[bIt.key()] = newBucketObj;
        }
        if (!newScenarioObj.isEmpty()) newWeights[scIt.key()] = newScenarioObj;
    }

    m_weights   = newWeights;
    m_lastDecay = today;
}

// ============================================================
// Сохранение / загрузка
// ============================================================

QString UserProfile::filePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/jarvis_profile.json");
}

void UserProfile::load()
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastDecay = QDate::currentDate();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        m_lastDecay = QDate::currentDate();
        return;
    }

    const QJsonObject root = doc.object();
    m_weights = root[QStringLiteral("weights")].toObject();

    m_lastDecay = QDate::fromString(root[QStringLiteral("lastDecay")].toString(), Qt::ISODate);
    if (!m_lastDecay.isValid()) m_lastDecay = QDate::currentDate();

    applyDecayIfNeeded();
}

void UserProfile::save() const
{
    QJsonObject root;
    root[QStringLiteral("weights")]   = m_weights;
    root[QStringLiteral("lastDecay")] = m_lastDecay.toString(Qt::ISODate);

    QFile file(filePath());
    if (!file.open(QIODevice::WriteOnly)) return;

    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
}
