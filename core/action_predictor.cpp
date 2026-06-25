// -------------------------------------------------------
// action_predictor.cpp — Предугадывание действий
// -------------------------------------------------------

#include "action_predictor.h"
#include "session_memory.h"
#include "jarvis_paths.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <algorithm>

// ============================================================
// Конструктор
// ============================================================

ActionPredictor::ActionPredictor(SessionMemory* memory, QObject* parent)
    : QObject(parent)
    , m_memory(memory)
{
    initDefaultRules();
    loadPatterns();
}

// ============================================================
// Правила по умолчанию (IF/THEN)
// ============================================================

void ActionPredictor::initDefaultRules()
{
    m_rules = {
        // После запуска IDE → вероятно, нужен поиск или файл
        {QStringLiteral("rider"),
         QStringLiteral("open_app:Rider"),
         QStringLiteral("Open Rider?"), 0},

        {QStringLiteral("clion"),
         QStringLiteral("open_app:CLion"),
         QStringLiteral("Open CLion?"), 0},

        // После Steam → вероятно, хочет поиграть
        {QStringLiteral("steam"),
         QStringLiteral("open_app:Steam"),
         QStringLiteral("Open Steam?"), 0},

        // После Discord → вероятно, хочет общаться
        {QStringLiteral("discord"),
         QStringLiteral("open_app:Discord"),
         QStringLiteral("Open Discord?"), 0},

        // После Chrome/браузера → вероятно, нужен YouTube
        {QStringLiteral("chrome"),
         QStringLiteral("open_app:YouTube"),
         QStringLiteral("Open YouTube?"), 0},

        // После блокнота → возможно, нужно напечатать
        {QStringLiteral("notepad"),
         QStringLiteral("напечатай "),
         QStringLiteral("Type some text?"), 0},

        // После поиска → возможно, нужен YouTube
        {QStringLiteral("найди"),
         QStringLiteral("open_app:YouTube"),
         QStringLiteral("Search on YouTube?"), 0},

        // Вечер → предложить заблокировать
        {QStringLiteral("время"),
         QStringLiteral("заблокируй"),
         QStringLiteral("Lock the screen?"), 0},

        // После нескольких команд → предложить статистику
        {QStringLiteral("помощь"),
         QStringLiteral("статистика"),
         QStringLiteral("Check usage stats?"), 0},
    };
}

// ============================================================
// Запись последовательности команд
// ============================================================

void ActionPredictor::recordSequence(const QString& command)
{
    QString lower = command.trimmed().toLower();
    QString key = lower.split(QChar(' ')).first();

    m_recentCommands.prepend(key);
    while (m_recentCommands.size() > MAX_RECENT) {
        m_recentCommands.removeLast();
    }

    // Обучение: если есть паттерн A→B, увеличиваем hitCount
    if (m_recentCommands.size() >= 2) {
        QString prev = m_recentCommands[1];  // Предыдущая команда
        for (auto& rule : m_rules) {
            if (prev.contains(rule.trigger) &&
                key.contains(rule.suggestedAction.split(QChar(' ')).first())) {
                rule.hitCount++;
            }
        }
    }

    // Обучение: если B часто следует за A, создаём новое правило
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

        // Проверяем, встречалась ли эта пара раньше в статистике
        if (!ruleExists) {
            QJsonObject stats = m_memory->commandStats();
            int prevCount = stats[prev].toInt(0);
            int currCount = stats[curr].toInt(0);

            // Если обе команды часто используются, создаём правило
            if (prevCount >= 3 && currCount >= 3) {
                PatternRule newRule;
                newRule.trigger = prev;

                // Если текущая команда начинается с глагола открытия —
                // сохраняем как open_app: чтобы кнопка Yes её выполнила
                // напрямую, не отправляя название приложения в AI как текст.
                static const QStringList openPrefixes = {
                    QStringLiteral("открой "), QStringLiteral("запусти "),
                    QStringLiteral("open "),   QStringLiteral("launch "),
                    QStringLiteral("start "),  QStringLiteral("run "),
                };
                bool isOpenCmd = false;
                for (const QString& prefix : openPrefixes) {
                    if (curr.startsWith(prefix)) {
                        QString appName = curr.mid(prefix.length()).trimmed();
                        // Капитализируем первую букву для красивого отображения
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
// Предложения
// ============================================================

ActionSuggestion ActionPredictor::suggestAfter(const QString& lastCommand) const
{
    QString lower = lastCommand.trimmed().toLower();

    ActionSuggestion best;
    best.confidence = 0.0;

    for (const auto& rule : m_rules) {
        if (lower.contains(rule.trigger)) {
            double confidence = 0.3 + qMin(0.7, rule.hitCount * 0.1);
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

    // 1. Паттерн-предложения на основе последней команды
    if (!m_recentCommands.isEmpty()) {
        auto suggestion = suggestAfter(m_recentCommands.first());
        if (suggestion.isValid()) {
            suggestions.append(suggestion);
        }
    }

    // 2. Самые частые команды (за вычетом уже предложенных)
    QJsonObject stats = m_memory->commandStats();
    QVector<QPair<QString, int>> sorted;
    for (auto it = stats.begin(); it != stats.end(); ++it) {
        sorted.append({it.key(), it.value().toInt()});
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    for (const auto& [cmd, count] : sorted) {
        if (suggestions.size() >= maxSuggestions) break;

        // Не предлагать то, что уже в списке
        bool alreadySuggested = false;
        for (const auto& s : suggestions) {
            if (s.action.startsWith(cmd)) {
                alreadySuggested = true;
                break;
            }
        }
        if (alreadySuggested) continue;

        // Не предлагать слишком редкие
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
// Сохранение / загрузка паттернов
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

        // Обновляем существующие или добавляем новые
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