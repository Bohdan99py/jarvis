#pragma once
// -------------------------------------------------------
// user_profile.h — Профиль предпочтений пользователя
//
// "Обучение без ML": накапливает статистику вида
//   (сценарий, время суток) -> какие команды чаще используются,
// со временным затуханием старых записей (старое забывается,
// недавнее усиливается). Сценарий определяется ЭВРИСТИКОЙ по
// ContextSnapshot (активное окно, запущенные процессы) — это не
// нейросеть, а простой и прозрачный набор правил, который со
// временем "обрастает" реальными данными о пользователе.
//
// Используется:
//   - Jarvis::processCommand — recordObservation() на каждое сообщение
//   - SessionMemory::buildSystemPrompt — buildProfileSummary() в промпте
//   - команда "профиль" / "profile" — показать, что выучил JARVIS
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QVector>
#include <QDate>

#include "context_snapshot.h"
#include "jarvis_core_export.h"

class JARVIS_CORE_EXPORT UserProfile : public QObject
{
    Q_OBJECT

public:
    // Высокоуровневый сценарий активности пользователя.
    // Это НЕ финальная классификация на все случаи жизни — это
    // отправная точка, которая уточняется накопленной статистикой.
    enum class Scenario {
        GameDev,    // CLion/Rider/Visual Studio/VS Code или активен UE5
        Art,        // Krita/Photoshop/Blender
        Gaming,     // активное окно не похоже на рабочее, Steam/Epic запущены
        Browsing,   // активен браузер
        Chat,       // ничего специфического — обычный диалог с JARVIS
        Unknown
    };
    Q_ENUM(Scenario)

    explicit UserProfile(QObject* parent = nullptr);
    ~UserProfile() override;

    // --- Классификация (эвристика, без ML) ---
    static Scenario classifyScenario(const ContextSnapshot& ctx);
    static QString  scenarioName(Scenario s);     // человекочитаемое имя (RU)

    // 4 окна суток: 0=ночь(0-5), 1=утро(6-11), 2=день(12-17), 3=вечер(18-23)
    static int     timeBucket(int hourOfDay);
    static QString timeBucketName(int bucket);

    // --- Обучение ---
    // Записать наблюдение: пользователь ввёл команду в данном контексте.
    // Вызывает классификацию сценария, обновляет веса и сохраняет на диск.
    void recordObservation(const ContextSnapshot& ctx, const QString& userInput);

    // Сценарий "сейчас", сглаженный по последним наблюдениям (без флаппинга)
    Scenario currentScenario() const;

    // Топ команд для конкретного (сценарий, временной слот)
    QStringList topCommands(Scenario scenario, int bucket, int maxResults = 3) const;

    // Текстовая сводка для системного промпта Claude / команды "профиль"
    QString buildProfileSummary(int maxLines = 6) const;

    void load();
    void save() const;

signals:
    // Эмитится из recordObservation, если сглаженный сценарий изменился —
    // можно использовать для бейджа в UI ("🎮"/"💻"/"🎨"...).
    void scenarioChanged(Scenario newScenario);

private:
    static QString scenarioKey(Scenario s);          // ключ для JSON (стабильный)
    static QString commandKeyOf(const QString& input); // первое слово команды, lower-case

    void applyDecayIfNeeded();
    QString filePath() const;

    // m_weights[scenarioKey][bucket(0-3)][commandKey] = вес (double, с затуханием)
    QJsonObject m_weights;
    QDate       m_lastDecay;

    QVector<Scenario> m_recentScenarios;  // для сглаживания currentScenario()

    static constexpr double DECAY_PER_DAY = 0.97; // ~3%/день — старое забывается за недели
    static constexpr double MIN_WEIGHT    = 0.05; // обрезаем шум, чтобы файл не рос бесконечно
    static constexpr int    MAX_RECENT    = 5;    // сколько последних классификаций сглаживаем
};
