#pragma once
// -------------------------------------------------------
// action_predictor.h — Предугадывание действий пользователя
//                      на основе паттернов использования
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QPair>

class SessionMemory;

// Предложение действия
struct ActionSuggestion
{
    QString action;       // Команда для выполнения
    QString description;  // Человекочитаемое описание
    double confidence;    // 0.0 - 1.0, уверенность

    bool isValid() const { return !action.isEmpty() && confidence > 0.0; }
};

class ActionPredictor : public QObject
{
    Q_OBJECT

public:
    explicit ActionPredictor(SessionMemory* memory, QObject* parent = nullptr);

    // Получить предложения на основе текущего контекста
    QVector<ActionSuggestion> suggest(int maxSuggestions = 3) const;

    // Получить предложение после конкретной команды
    // (если есть устойчивый паттерн "команда A → команда B")
    ActionSuggestion suggestAfter(const QString& lastCommand) const;

    // Записать последовательность команд (для обучения паттернов)
    void recordSequence(const QString& command);

    // Загрузить / сохранить паттерны
    void loadPatterns();
    void savePatterns();

    signals:
        void suggestionReady(const ActionSuggestion& suggestion);

private:
    // Правила IF/THEN для быстрых паттернов
    struct PatternRule {
        QString trigger;           // После какой команды
        QString suggestedAction;   // Что предложить
        QString description;
        int hitCount = 0;          // Сколько раз сработало
    };

    void initDefaultRules();
    void learnFromHistory();

    SessionMemory* m_memory = nullptr;
    QVector<PatternRule> m_rules;
    QStringList m_recentCommands;   // Последние 10 команд сессии

    static constexpr int MAX_RECENT = 10;
    static constexpr double MIN_CONFIDENCE = 0.3;
};