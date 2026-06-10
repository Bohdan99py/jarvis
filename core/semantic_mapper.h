#pragma once
// -------------------------------------------------------
// semantic_mapper.h — Семантический маппер запросов
//
// Переводит пользовательский запрос в набор паттернов
// для поиска файлов по имени и содержимому.
//
// Пример:
//   "резюме" → ["cv", "resume", "резюме", "curriculum", "leontovych"]
//   "договор" → ["contract", "договор", "contrat", "agreement"]
//
// Также умеет учиться: если пользователь нашёл файл через
// запрос — связь запрос→файл запоминается в SessionMemory.
// -------------------------------------------------------

#include <QString>
#include <QStringList>
#include <QMap>
#include <QObject>

class SessionMemory;

// ============================================================
// SemanticMatch — результат маппинга
// ============================================================

struct SemanticMatch
{
    QStringList fileNamePatterns;  // паттерны для поиска по имени файла
    QStringList contentKeywords;   // слова для поиска внутри файла
    QStringList extensions;        // приоритетные расширения (пусто = все)
    bool        learned = false;   // true = из памяти Джарвиса, не из словаря
};

// ============================================================
// SemanticMapper
// ============================================================

class SemanticMapper
{
public:
    explicit SemanticMapper(SessionMemory* memory = nullptr);

    // Главный метод: по запросу возвращает паттерны поиска.
    // Возвращает пустой SemanticMatch если запрос уже
    // конкретный (одно слово, похоже на имя файла).
    SemanticMatch map(const QString& query) const;

    // Записывает в память: запрос→найденный файл
    void learnFromResult(const QString& query, const QString& foundFilePath);

    // Проверяет: нужен ли семантический поиск для этого запроса
    // (false = запрос уже конкретный, искать напрямую)
    bool needsSemanticExpansion(const QString& query) const;

private:
    // Строит маппинг из встроенного словаря
    SemanticMatch fromDictionary(const QString& lowerQuery) const;

    // Достаёт выученные связи из памяти
    SemanticMatch fromMemory(const QString& lowerQuery) const;

    // Добавляет имя пользователя из памяти к паттернам
    void enrichWithUserName(SemanticMatch& match) const;

    SessionMemory* m_memory = nullptr;

    // Ключ памяти для выученных связей
    static constexpr const char* kMemoryPrefix = "semantic_link:";
};
