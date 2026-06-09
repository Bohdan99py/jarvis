#pragma once
// -------------------------------------------------------
// brain.h — Центральный класс понимания намерений
//
// Заменяет CommandRegistry как основной диспетчер.
// Вместо жёстких тригеров — трёхуровневый анализ:
//
//   Уровень 1: лингвистический разбор (локально, бесплатно)
//              — структура фразы, глаголы, существительные
//
//   Уровень 2: контекстный анализ (ContextSnapshot)
//              — активное окно, история, режим, буфер обм.
//
//   Уровень 3: уточнение у пользователя
//              — только если confidence < 0.65
//
// CommandRegistry остаётся только для аварийных системных
// команд: смена API-ключа, выход, очистка лога.
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
        Run,          // запустить / выполнить
        Explain,      // объяснить / рассказать
        Modify,       // изменить / отредактировать код/файл
        Create,       // создать новый файл / класс / функцию
        Ask,          // общий вопрос → Claude API
        SystemCmd,    // системная команда (API-ключ, выход и т.д.)
        Clarify,      // нужно уточнение у пользователя
        Unknown       // не удалось определить
    };

    // --- Над чем / где ---
    enum class Domain {
        ProjectFiles,    // файлы текущего проекта (через ProjectIndexer)
        Filesystem,      // весь компьютер (Windows Search / Everything)
        BrowserHistory,  // история / закладки Chrome/Edge
        ChatHistory,     // история нашего разговора (SessionMemory)
        UE5Logs,         // логи Unreal Engine
        Web,             // интернет
        Clipboard,       // буфер обмена
        Memory,          // что Джарвис помнит о пользователе
        Code,            // работа с кодом (вайбкодинг)
        System,          // системные функции ОС
        None             // не применимо (для Ask, SystemCmd)
    };

    Action  action     = Action::Unknown;
    Domain  domain     = Domain::None;

    QString query;           // что ищем / что делаем (очищенный запрос)
    QString targetFile;      // конкретный файл если указан
    QString targetApp;       // конкретное приложение если указано

    float   confidence = 0.0f;  // 0.0–1.0

    // Флаги уточнения
    bool    needsClarification = false;
    QString clarificationQuestion;   // что спросить у пользователя

    // Метаданные
    bool    fromHistory  = false;   // намерение извлечено из истории
    bool    fromContext  = false;   // намерение из контекста окна/процесса
    bool    fromKeyword  = false;   // явное ключевое слово ("в файлах", "в интернете")

    // Удобные проверки
    bool isSearch()   const { return action == Action::Search; }
    bool isModify()   const { return action == Action::Modify; }
    bool isAsk()      const { return action == Action::Ask; }
    bool isSystem()   const { return action == Action::SystemCmd; }
    bool isClarify()  const { return action == Action::Clarify; }
    bool isKnown()    const { return action != Action::Unknown && !needsClarification; }
};

// ============================================================
// Brain
// ============================================================

class Brain : public QObject
{
    Q_OBJECT

public:
    explicit Brain(QObject* parent = nullptr);

    // Главный метод — анализирует ввод + контекст → Intent
    Intent analyze(const QString& input, const ContextSnapshot& ctx) const;

    // Формирует снимок системы (вызывается в Jarvis перед analyze)
    // Вынесен сюда чтобы Jarvis не знал о WinAPI деталях
    static ContextSnapshot captureSnapshot(
        const QStringList&  recentCommands,
        const QString&      lastResponse,
        bool                projectIndexed,
        const QString&      projectRoot,
        const QStringList&  recentProjectFiles,
        bool                vibeCodingMode,
        bool                multiAgentMode
    );

private:
    // --- Уровень 1: лингвистический анализ ---
    Intent::Action  detectAction(const QString& lower) const;
    Intent::Domain  detectDomainByKeywords(const QString& lower) const;
    float           scoreActionConfidence(const QString& lower, Intent::Action action) const;

    // --- Уровень 2: контекстный анализ ---
    Intent::Domain  refineDomainByContext(
                        Intent::Domain      preliminary,
                        const QString&      lower,
                        const ContextSnapshot& ctx) const;
    float           boostConfidenceByContext(
                        float               base,
                        const Intent&       intent,
                        const ContextSnapshot& ctx) const;

    // --- Уровень 3: генерация вопроса для уточнения ---
    QString         buildClarificationQuestion(
                        const Intent&       intent,
                        const QString&      originalInput) const;

    // --- Вспомогательные ---
    QString         extractQuery(const QString& input, Intent::Action action) const;
    QString         extractTargetFile(const QString& lower) const;
    QString         extractTargetApp(const QString& lower) const;
    bool            containsAny(const QString& text, const QStringList& words) const;

    // --- Пороги уверенности ---
    static constexpr float kClarifyThreshold = 0.55f;  // ниже — спрашиваем
    static constexpr float kHighConfidence   = 0.80f;  // выше — действуем без вопросов
};
