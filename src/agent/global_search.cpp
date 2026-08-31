// -------------------------------------------------------
// global_search.cpp — см. global_search.h
// -------------------------------------------------------

#include "global_search.h"
#include "tool_registry.h"

#include <QJsonArray>
#include <algorithm>

GlobalSearch::GlobalSearch(QObject* parent)
    : QObject(parent)
{
}

void GlobalSearch::addProvider(const QString& name, Provider provider)
{
    if (!provider)
        return;
    m_providers.append(Entry{ name, std::move(provider) });
}

int GlobalSearch::matchScore(const QString& text, const QString& queryLower)
{
    if (queryLower.isEmpty() || text.isEmpty())
        return 0;

    const QString lower = text.toLower();

    if (lower == queryLower)
        return 1000;
    if (lower.startsWith(queryLower))
        return 700;

    // Совпадение с началом слова: "sys" находит "system_status" и
    // "Диспетчер систем", но не "filesystem" — там это середина слова
    // и такой хит человек запросом не описывал.
    const int idx = lower.indexOf(queryLower);
    if (idx < 0)
        return 0;

    const QChar before = lower.at(idx - 1);
    if (before == QChar(' ') || before == QChar('_') || before == QChar('-')
        || before == QChar('/') || before == QChar('\\') || before == QChar('.'))
        return 500;

    return 250;
}

bool GlobalSearch::toolNeedsArguments(const ToolRegistry* tools, const QString& toolName)
{
    if (!tools)
        return true;
    const ToolSpec* spec = tools->find(toolName);
    if (!spec)
        return true;

    const QJsonArray required = spec->schema.value(QStringLiteral("required")).toArray();
    return !required.isEmpty();
}

QVector<SearchHit> GlobalSearch::search(const QString& query, int limitPerProvider) const
{
    QVector<SearchHit> all;

    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
        return all;

    for (const Entry& e : m_providers) {
        QVector<SearchHit> hits = e.provider(trimmed, qMax(1, limitPerProvider));
        all += hits;
    }

    // Стабильная сортировка: при равном счёте порядок регистрации
    // провайдеров сохраняется, и список не прыгает между нажатиями.
    std::stable_sort(all.begin(), all.end(),
                     [](const SearchHit& a, const SearchHit& b) { return a.score > b.score; });
    return all;
}
