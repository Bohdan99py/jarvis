#pragma once
// =============================================================================
// learned_commands.h — Система самообучения J.A.R.V.I.S.
// =============================================================================
// Принцип: первый раз → AI API решает задачу и объясняет шаги.
// JARVIS анализирует ответ, извлекает исполняемые шаги и сохраняет их.
// Следующий раз — выполняет сам, без API.
//
// Хранилище: %AppData%/Jarvis/learned_commands.json
//
// Структура записи:
// {
//   "trigger_patterns": ["открой браузер с youtube", "зайди на ютуб"],
//   "steps": [
//     { "type": "shell", "cmd": "start https://youtube.com" },
//     { "type": "key",   "key": "ctrl+l" }
//   ],
//   "confidence": 0.9,
//   "use_count": 5,
//   "last_used": "2026-06-14T10:00:00"
// }
// =============================================================================

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QObject>

// ---------------------------------------------------------------------------
// Один шаг выполнения
// ---------------------------------------------------------------------------
struct LearnedStep {
    enum class Type {
        Shell,      // QProcess / ShellExecuteW
        KeyPress,   // SendInput клавиша
        TypeText,   // SendInput текст
        Click,      // SetCursorPos + mouse_event
        OpenUrl,    // ShellExecuteW URL
        WinCommand  // WinAPI команда (lock, minimize, etc.)
    };

    Type    type    = Type::Shell;
    QString value;  // команда / клавиша / текст / URL / координаты "x,y"
    int     delayMs = 200; // задержка после шага
};

// ---------------------------------------------------------------------------
// Одна выученная команда
// ---------------------------------------------------------------------------
struct LearnedCommand {
    QString      id;               // UUID
    QStringList  triggerPatterns;  // фразы которые активируют команду
    QList<LearnedStep> steps;      // шаги выполнения
    QString      description;      // человекочитаемое описание
    float        confidence = 0.9f;
    int          useCount   = 0;
    QDateTime    learnedAt;
    QDateTime    lastUsed;

    bool isValid() const { return !triggerPatterns.isEmpty() && !steps.isEmpty(); }
};

// ---------------------------------------------------------------------------
// Менеджер выученных команд
// ---------------------------------------------------------------------------
class LearnedCommands : public QObject {
    Q_OBJECT
public:
    explicit LearnedCommands(QObject* parent = nullptr);

    // Поиск команды по вводу пользователя (fuzzy match)
    // Возвращает nullptr если не нашли
    const LearnedCommand* findMatch(const QString& input) const;

    // Выполнить выученную команду
    // Возвращает описание того что сделано
    QString execute(const LearnedCommand& cmd);

    // Добавить/обновить команду (вызывается после ответа AI)
    void learnFromApiResponse(const QString& userInput,
                              const QString& aiResponse,
                              const QString& actionTaken);

    // Явно сохранить команду (из диалога подтверждения)
    void saveCommand(const LearnedCommand& cmd);

    // Удалить команду
    void removeCommand(const QString& id);

    // Все команды (для UI управления)
    QList<LearnedCommand> allCommands() const;

    // Статистика
    int count() const { return m_commands.size(); }

    // Сигнал: выучена новая команда
signals:
    void commandLearned(const LearnedCommand& cmd);
    void commandExecuted(const QString& id, bool success);

private:
    void load();
    void save();

    // Извлечь исполняемые шаги из текста ответа AI
    QList<LearnedStep> extractStepsFromResponse(const QString& response) const;

    // Fuzzy matching: схожесть двух строк (0.0 - 1.0)
    float similarity(const QString& a, const QString& b) const;

    // Нормализация ввода для сравнения
    QString normalize(const QString& input) const;

    QString persistPath() const;

    QList<LearnedCommand> m_commands;

    static constexpr float kMatchThreshold = 0.70f; // порог для срабатывания
};
