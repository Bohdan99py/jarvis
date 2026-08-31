#pragma once
// -------------------------------------------------------
// global_search.h — Ctrl+K: найти, а не сделать
//
// Отличие от палитры команд (Ctrl+Space): та отправляет
// запрос агенту, ждёт модель и что-то делает. Здесь всё
// локально и мгновенно — на каждое нажатие клавиши, без
// сети и без LLM. Поэтому провайдеры обязаны быть быстрыми:
// они работают по уже готовым спискам (индекс проекта,
// алиасы приложений, реестр инструментов, сценарии, БД),
// а не обходят диск.
//
// SearchRouter из intelligence решает другую задачу —
// «модель попросила найти X в домене Y» — и возвращает
// готовый текст для лога. Здесь нужен список объектов с
// действием на Enter, поэтому это отдельный слой, а не
// обёртка над ним.
// -------------------------------------------------------

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class ToolRegistry;

// ============================================================
//  Один результат
// ============================================================
struct SearchHit
{
    // Что произойдёт по Enter
    enum class Action {
        None,        // просто показать (память, история)
        LaunchApp,   // payload — имя приложения
        OpenPath,    // payload — путь к файлу/папке
        RunTool,     // payload — имя инструмента, args — аргументы
        RunWorkflow, // payload — имя сценария
        SetProfile,  // payload — id режима
        AskAgent,    // payload — готовый запрос для командной палитры
        RunCommand   // payload — id команды из ActionRegistry (выполняет UI)
    };

    QString     category;      // "Приложения", "Файлы", ...
    QString     icon;          // эмодзи категории
    QString     title;
    QString     subtitle;      // путь, фрагмент, описание
    Action      action = Action::None;
    QString     payload;
    QJsonObject args;
    int         score = 0;     // больше — выше в списке

    bool isActionable() const { return action != Action::None; }
};

// ============================================================
//  GlobalSearch
// ============================================================
class GlobalSearch : public QObject
{
    Q_OBJECT

public:
    explicit GlobalSearch(QObject* parent = nullptr);

    // Провайдер обязан вернуть управление быстро: он вызывается
    // на каждое нажатие клавиши.
    using Provider = std::function<QVector<SearchHit>(const QString& query, int limit)>;
    void addProvider(const QString& name, Provider provider);

    // Поиск по всем провайдерам. limitPerProvider ограничивает выдачу
    // каждого, чтобы один разговорчивый источник не вытеснил остальные.
    QVector<SearchHit> search(const QString& query, int limitPerProvider = 5) const;

    // Оценка совпадения строки с запросом. Публичная: провайдеры
    // считают ею же, иначе категории будут ранжироваться по-разному.
    //   точное совпадение          1000
    //   начинается с запроса        700
    //   слово начинается с запроса  500
    //   просто содержит             250
    //   не совпало                    0
    static int matchScore(const QString& text, const QString& queryLower);

    // true, если у инструмента нет обязательных аргументов — такой
    // можно запускать прямо из поиска, остальные только подставлять
    // текстом в командную палитру.
    static bool toolNeedsArguments(const ToolRegistry* tools, const QString& toolName);

private:
    struct Entry {
        QString  name;
        Provider provider;
    };
    QVector<Entry> m_providers;
};
