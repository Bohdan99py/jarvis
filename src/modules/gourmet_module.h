#pragma once
// ============================================================
// gourmet_module.h — Gourmet & Mixology Assistant
//
// Activated via /fridge — parses ingredient lists and queries
// the LLM for recipes or cocktail breakdowns.
// ============================================================

#include <QObject>
#include <QString>

class Jarvis;

class GourmetModule : public QObject
{
    Q_OBJECT

public:
    explicit GourmetModule(QObject* parent = nullptr);

    void setJarvisCore(Jarvis* jarvis) { m_jarvis = jarvis; }

    QString processIngredients(const QString& ingredients, bool english);

private:
    Jarvis* m_jarvis = nullptr;
};
