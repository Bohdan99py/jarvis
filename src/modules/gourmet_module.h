#pragma once
// ============================================================
// gourmet_module.h — Gourmet & Mixology Assistant
//
// Activated via /fridge — parses ingredient lists and queries
// the LLM for recipes or cocktail breakdowns.
// Tracks culinary preferences in SQLite for personalization.
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>

class Jarvis;

class GourmetModule : public QObject
{
    Q_OBJECT

public:
    explicit GourmetModule(QObject* parent = nullptr);

    void setJarvisCore(Jarvis* jarvis) { m_jarvis = jarvis; }

    QString processIngredients(const QString& ingredients, bool english);

    QString formatRecipeCard(const QString& rawRecipe, bool english) const;

    void logPreference(const QString& ingredient, double weight = 1.0);
    QStringList topPreferences(int maxResults = 10) const;

    void ensureTable();

signals:
    void preferenceUpdated(const QString& ingredient, double newWeight);

private:
    QStringList parseIngredientList(const QString& raw) const;
    QString     classifyCategory(const QStringList& ingredients) const;

    Jarvis* m_jarvis = nullptr;
};
