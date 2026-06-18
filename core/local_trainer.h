#pragma once
// ============================================================
// local_trainer.h — J.A.R.V.I.S. Local Model Training
//
// Создаёт персонализированную Ollama-модель из лайкнутых ответов.
// Не требует Python, GPU, Colab — только работающий Ollama.
//
// Как это работает:
//   1. Читает training_logs (rating > 0) из SQLite
//   2. Генерирует Ollama Modelfile с system prompt + MESSAGE pairs
//   3. Запускает `ollama create jarvis-personal -f Modelfile`
//   4. Модель сразу доступна через Ollama API
//
// Это NOT fine-tuning (LoRA/QLoRA), а few-shot personalization.
// Ollama "запекает" примеры в контекст модели, и она начинает
// отвечать в стиле тех ответов которые пользователь лайкнул.
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QProcess>

class DatabaseManager;

// ============================================================
//  TrainingPair — одна пара вопрос/ответ
// ============================================================

struct TrainingPair {
    QString userMessage;
    QString aiResponse;
    QString model;        // какая модель ответила
    int     rating = 0;   // 1 = лайк
};

// ============================================================
//  LocalTrainer
// ============================================================

class LocalTrainer : public QObject
{
    Q_OBJECT
public:
    explicit LocalTrainer(QObject* parent = nullptr);
    ~LocalTrainer() override;

    // ── Настройки ────────────────────────────────────────────
    // Базовая модель Ollama (по умолчанию llama3.2:3b — маленькая и быстрая)
    void setBaseModel(const QString& model);
    QString baseModel() const { return m_baseModel; }

    // Имя создаваемой модели (по умолчанию jarvis-personal)
    void setOutputModel(const QString& name);
    QString outputModel() const { return m_outputModel; }

    // Максимум пар в Modelfile (больше = дольше inference, лучше качество)
    void setMaxPairs(int n);
    int maxPairs() const { return m_maxPairs; }

    // Системный промпт
    void setSystemPrompt(const QString& prompt);

    // ── Действия ─────────────────────────────────────────────
    // Проверить что Ollama доступна
    bool isOllamaAvailable() const;

    // Получить список установленных моделей Ollama
    QStringList installedModels() const;

    // Собрать лайкнутые пары из БД
    QVector<TrainingPair> collectTrainingData(DatabaseManager* db) const;

    // Сгенерировать текст Modelfile
    QString buildModelfile(const QVector<TrainingPair>& pairs) const;

    // Запустить обучение (асинхронно)
    void train(DatabaseManager* db);

    // Удалить созданную модель
    void removeModel();

signals:
    void trainingStarted();
    void trainingProgress(const QString& message);
    void trainingFinished(bool success, const QString& message);
    void modelRemoved(bool success);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    QString escapeForModelfile(const QString& text) const;

    QString   m_baseModel    = QStringLiteral("llama3.2:3b");
    QString   m_outputModel  = QStringLiteral("jarvis-personal");
    int       m_maxPairs     = 80;
    QString   m_systemPrompt;
    QProcess* m_process      = nullptr;
    QString   m_modelfilePath;
};
