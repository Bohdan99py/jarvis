#pragma once
// -------------------------------------------------------
// brain.h — Центральный класс понимания намерений
//
// ИСПРАВЛЕНО v2:
//  - Intent::localResponse — готовый ответ без API
//  - Intent::Domain::System — для open/close/lock команд
//  - Brain::tryLocalAnswer() — локальный движок:
//      приветствия, помощь, время, математика,
//      открытие/закрытие 35+ приложений, lock/shutdown
//  - analyze() проверяет локальный ответ ПЕРВЫМ
//    (до лингвистического/контекстного анализа)
// -------------------------------------------------------

#include "context_snapshot.h"
#include <QString>
#include <QStringList>
#include <QObject>
#include <functional>

// ============================================================
// Intent — структурированное намерение
// ============================================================

struct Intent
{
    // --- Что делаем ---
    enum class Action {
        Search,       // найти что-то
        Open,         // открыть файл / приложение / URL
        Run,          // запустить / выполнить / закрыть
        Explain,      // объяснить / рассказать
        Modify,       // изменить / отредактировать код/файл
        Create,       // создать новый файл / класс / функцию
        Ask,          // общий вопрос → Claude API (или локальный ответ)
        SystemCmd,    // системная команда (lock/shutdown/restart)
        Clarify,      // нужно уточнение у пользователя
        Unknown       // не удалось определить
    };

    // --- Над чем / где ---
    enum class Domain {
        ProjectFiles,        // файлы текущего проекта
        Filesystem,          // весь компьютер
        BrowserHistory,      // история / закладки браузера
        ChatHistory,         // история разговора
        UE5Logs,             // логи Unreal Engine
        Web,                 // интернет
        Clipboard,           // буфер обмена
        Memory,              // память о пользователе
        Code,                // работа с кодом
        System,              // системные функции ОС (open app, lock, etc.)
        Philosophy_Chitchat, // философия, мораль, открытые вопросы, болталка
        None                 // не применимо
    };

    Action  action     = Action::Unknown;
    Domain  domain     = Domain::None;

    QString query;           // очищенный запрос для поиска/API
    QString targetFile;      // конкретный файл если указан
    QString targetApp;       // приложение / системная команда ("close:steam", "lock")

    float   confidence = 0.0f;  // 0.0–1.0

    // Готовый локальный ответ (заполняется при action==Ask без API)
    // Если не пусто — вызывающий должен отобразить ЭТО вместо вызова API
    QString localResponse;

    // Флаги уточнения
    bool    needsClarification    = false;
    QString clarificationQuestion;

    // Метаданные
    bool    fromHistory  = false;
    bool    fromContext  = false;
    bool    fromKeyword  = false;

    // Удобные проверки
    bool isSearch()    const { return action == Action::Search; }
    bool isOpen()      const { return action == Action::Open; }
    bool isRun()       const { return action == Action::Run; }
    bool isModify()    const { return action == Action::Modify; }
    bool isAsk()       const { return action == Action::Ask; }
    bool isSystem()    const { return action == Action::SystemCmd; }
    bool isClarify()   const { return action == Action::Clarify; }
    bool isKnown()     const { return action != Action::Unknown && !needsClarification; }

    // Возвращает true если ответ уже готов локально (не нужен API)
    bool hasLocalAnswer() const { return !localResponse.isEmpty(); }
};

// ============================================================
// Brain
// ============================================================

class Brain : public QObject
{
    Q_OBJECT

public:
    explicit Brain(QObject* parent = nullptr);

    // --- Браузерный routing (вызывается из MainWindow) ---
    QString         resolveWebTarget(const QString& lower) const;
    QString         suggestWebTarget(const QString& lower) const;

    // Главный метод — анализирует ввод + контекст → Intent.
    // Если intent.hasLocalAnswer() == true — отображай localResponse напрямую,
    // НЕ вызывая dispatchToApi().
    Intent analyze(const QString& input, const ContextSnapshot& ctx) const;

    // Формирует снимок состояния системы
    static ContextSnapshot captureSnapshot(
        const QStringList&  recentCommands,
        const QString&      lastResponse,
        bool                projectIndexed,
        const QString&      projectRoot,
        const QStringList&  recentProjectFiles,
        bool                vibeCodingMode,
        bool                multiAgentMode = false
    );

private:
    // --- Локальный движок (без API) ---
    // Возвращает true если справились локально, заполняет intent
    bool tryLocalAnswer(const QString& input, Intent& intent) const;

    // --- Уровень 1: лингвистический анализ ---
    Intent::Action  detectAction(const QString& lower) const;
    Intent::Domain  detectDomainByKeywords(const QString& lower) const;
    float           scoreActionConfidence(const QString& lower, Intent::Action action) const;

    // --- Уровень 2: контекстный анализ ---
    Intent::Domain  refineDomainByContext(
                        Intent::Domain      preliminary,
                        Intent::Action      action,
                        const QString&      lower,
                        const ContextSnapshot& ctx) const;
    float           boostConfidenceByContext(
                        float               base,
                        const Intent&       intent,
                        const ContextSnapshot& ctx) const;

    // --- Уровень 3: вопрос для уточнения ---
    QString         buildClarificationQuestion(
                        const Intent&       intent,
                        const QString&      originalInput) const;

    // --- Вспомогательные ---
    QString         extractQuery(const QString& input, Intent::Action action) const;
    QString         extractTargetFile(const QString& lower) const;
    QString         extractTargetApp(const QString& lower) const;
    bool            containsAny(const QString& text, const QStringList& words) const;
    bool            startsWithAny(const QString& text, const QStringList& words) const;
    bool            isConversational(const QString& lower) const;

    // --- Пороги уверенности ---
    static constexpr float kClarifyThreshold = 0.55f;
    static constexpr float kHighConfidence   = 0.80f;
    static constexpr int   kShortPhraseWords = 6;
};