// ============================================================
// gourmet_module.cpp — Gourmet & Mixology Assistant
// ============================================================
#include "gourmet_module.h"
#include "jarvis.h"
#include "database_manager.h"

#include <QDebug>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

GourmetModule::GourmetModule(QObject* parent)
    : QObject(parent)
{
    ensureTable();
}

// ============================================================
//  Ingredient parsing — handles comma, newline, "and" separators
// ============================================================

QStringList GourmetModule::parseIngredientList(const QString& raw) const
{
    QString cleaned = raw.trimmed();
    cleaned.replace(QChar('\n'), QChar(','));
    cleaned.replace(QStringLiteral(" and "), QStringLiteral(","));
    cleaned.replace(QStringLiteral(" и "),   QStringLiteral(","));
    cleaned.replace(QStringLiteral(" + "),   QStringLiteral(","));

    QStringList items;
    const QStringList parts = cleaned.split(QChar(','), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        QString item = part.trimmed().toLower();
        item.remove(QRegularExpression(QStringLiteral("^[\\d.,]+\\s*(?:g|kg|ml|l|oz|lb|шт|гр|кг|мл|л)\\.?\\s*")));
        item = item.trimmed();
        if (!item.isEmpty() && item.length() < 60)
            items.append(item);
    }
    return items;
}

// ============================================================
//  Category classification — cocktail vs food vs baking
// ============================================================

QString GourmetModule::classifyCategory(const QStringList& ingredients) const
{
    static const QStringList cocktailMarkers = {
        QStringLiteral("vodka"), QStringLiteral("rum"), QStringLiteral("gin"),
        QStringLiteral("tequila"), QStringLiteral("whiskey"), QStringLiteral("bourbon"),
        QStringLiteral("vermouth"), QStringLiteral("liqueur"), QStringLiteral("bitters"),
        QStringLiteral("водка"), QStringLiteral("ром"), QStringLiteral("джин"),
        QStringLiteral("текила"), QStringLiteral("виски"), QStringLiteral("вермут"),
        QStringLiteral("ликёр"), QStringLiteral("ликер"), QStringLiteral("биттер"),
    };
    static const QStringList bakingMarkers = {
        QStringLiteral("flour"), QStringLiteral("yeast"), QStringLiteral("baking powder"),
        QStringLiteral("мука"), QStringLiteral("дрожжи"), QStringLiteral("разрыхлитель"),
        QStringLiteral("vanilla"), QStringLiteral("ваниль"), QStringLiteral("cocoa"),
    };

    int cocktailHits = 0, bakingHits = 0;
    for (const QString& ing : ingredients) {
        for (const QString& m : cocktailMarkers)
            if (ing.contains(m)) ++cocktailHits;
        for (const QString& m : bakingMarkers)
            if (ing.contains(m)) ++bakingHits;
    }

    if (cocktailHits >= 2) return QStringLiteral("cocktail");
    if (bakingHits >= 2)   return QStringLiteral("baking");
    if (cocktailHits == 1) return QStringLiteral("cocktail");
    return QStringLiteral("cooking");
}

// ============================================================
//  Main entry point — parse, classify, build prompt, dispatch
// ============================================================

QString GourmetModule::processIngredients(const QString& ingredients, bool english)
{
    if (ingredients.trimmed().isEmpty()) {
        return english
            ? QStringLiteral("Usage: `/fridge milk, eggs, flour, sugar`\n"
                             "I'll suggest a recipe or cocktail based on your ingredients.")
            : QStringLiteral("Использование: `/fridge молоко, яйца, мука, сахар`\n"
                             "Я предложу рецепт или коктейль из твоих ингредиентов.");
    }

    const QStringList parsed = parseIngredientList(ingredients);
    if (parsed.isEmpty()) {
        return english
            ? QStringLiteral("I couldn't parse any ingredients. Try: `/fridge chicken, rice, garlic`")
            : QStringLiteral("Не удалось разобрать ингредиенты. Попробуй: `/fridge курица, рис, чеснок`");
    }

    for (const QString& ing : parsed)
        logPreference(ing);

    const QString category = classifyCategory(parsed);
    const QStringList prefs = topPreferences(5);

    QString prefHint;
    if (!prefs.isEmpty()) {
        prefHint = QStringLiteral(
            "\n[USER PREFERENCE CONTEXT: The user frequently uses these ingredients: %1. "
            "Favor recipes that align with these tastes when possible.]"
        ).arg(prefs.join(QStringLiteral(", ")));
    }

    QString categoryInstruction;
    if (category == QStringLiteral("cocktail")) {
        categoryInstruction = QStringLiteral(
            "This looks like a BAR ingredient set. Suggest a COCKTAIL recipe with mixology technique.");
    } else if (category == QStringLiteral("baking")) {
        categoryInstruction = QStringLiteral(
            "This looks like BAKING ingredients. Suggest a baked dish (cake, bread, pastry, cookies).");
    } else {
        categoryInstruction = QStringLiteral(
            "Suggest a COOKING recipe — a proper meal or dish.");
    }

    const QString prompt = QStringLiteral(
        "[GOURMET_MODE]\n"
        "%1\n"
        "The user lists ingredients from their fridge/bar. "
        "Suggest ONE recipe that uses as many listed ingredients as possible.\n\n"
        "FORMAT YOUR RESPONSE EXACTLY AS:\n"
        "## 🍽️ [Dish Name]\n\n"
        "**Category:** [Cooking / Baking / Cocktail]\n"
        "**Difficulty:** [Easy / Medium / Hard]\n"
        "**Time:** [X minutes]\n\n"
        "### Ingredients\n"
        "- [quantity] [ingredient] ✅ (from your list)\n"
        "- [quantity] [ingredient] 🛒 (you'll need to get)\n\n"
        "### Instructions\n"
        "1. [Step with timing and temperature details]\n"
        "2. ...\n\n"
        "### 💡 Chef's Tip\n"
        "[One practical tip for this recipe]\n\n"
        "Be creative but practical. Respond in %2.%3\n\n"
        "My ingredients: %4"
    ).arg(categoryInstruction,
          english ? QStringLiteral("English") : QStringLiteral("Russian"),
          prefHint,
          parsed.join(QStringLiteral(", ")));

    if (m_jarvis) {
        m_jarvis->processCommand(prompt);
        qDebug() << "[GourmetModule] Dispatched" << category << "query:"
                 << parsed.join(QStringLiteral(", ")).left(80);
    } else {
        qWarning() << "[GourmetModule] No Jarvis core attached — cannot dispatch query";
        return english
            ? QStringLiteral("⚠️ Gourmet module not connected to core engine.")
            : QStringLiteral("⚠️ Гурмэ-модуль не подключён к ядру.");
    }

    if (category == QStringLiteral("cocktail")) {
        return english
            ? QStringLiteral("🍸 Mixing a cocktail from your bar... Shaking things up!")
            : QStringLiteral("🍸 Готовлю коктейль из твоего бара... Уже шейкую!");
    }
    if (category == QStringLiteral("baking")) {
        return english
            ? QStringLiteral("🧁 Preheating the oven... Baking recipe incoming!")
            : QStringLiteral("🧁 Разогреваю духовку... Рецепт выпечки уже в пути!");
    }
    return english
        ? QStringLiteral("🍳 Analyzing your ingredients... Recipe incoming!")
        : QStringLiteral("🍳 Анализирую ингредиенты... Рецепт уже в пути!");
}

// ============================================================
//  Post-process: format raw LLM response into styled recipe card
// ============================================================

QString GourmetModule::formatRecipeCard(const QString& rawRecipe, bool english) const
{
    Q_UNUSED(english)
    if (rawRecipe.trimmed().isEmpty())
        return rawRecipe;

    QString formatted = rawRecipe;

    formatted.replace(QStringLiteral("###"), QStringLiteral("\n###"));
    formatted.replace(QStringLiteral("##"),  QStringLiteral("\n##"));

    static const QRegularExpression doubleNewlines(QStringLiteral("\n{3,}"));
    formatted.replace(doubleNewlines, QStringLiteral("\n\n"));

    return formatted.trimmed();
}

// ============================================================
//  Preference tracking — SQLite persistence
// ============================================================

void GourmetModule::ensureTable()
{
    if (!DatabaseManager::instance().isOpen()) return;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS gourmet_preferences ("
        "  ingredient TEXT PRIMARY KEY,"
        "  weight     REAL NOT NULL DEFAULT 1.0,"
        "  use_count  INTEGER NOT NULL DEFAULT 1,"
        "  last_used  TEXT NOT NULL DEFAULT (datetime('now'))"
        ")"));

    if (q.lastError().isValid())
        qWarning() << "[GourmetModule] Table creation error:" << q.lastError().text();
}

void GourmetModule::logPreference(const QString& ingredient, double weight)
{
    if (ingredient.trimmed().isEmpty()) return;
    if (!DatabaseManager::instance().isOpen()) return;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO gourmet_preferences (ingredient, weight, use_count, last_used) "
        "VALUES (:ing, :w, 1, datetime('now')) "
        "ON CONFLICT(ingredient) DO UPDATE SET "
        "  weight = weight + :w2, "
        "  use_count = use_count + 1, "
        "  last_used = datetime('now')"));
    q.bindValue(QStringLiteral(":ing"), ingredient.trimmed().toLower());
    q.bindValue(QStringLiteral(":w"),   weight);
    q.bindValue(QStringLiteral(":w2"),  weight);

    if (!q.exec())
        qWarning() << "[GourmetModule] logPreference error:" << q.lastError().text();
    else
        emit preferenceUpdated(ingredient, weight);
}

QStringList GourmetModule::topPreferences(int maxResults) const
{
    QStringList result;
    if (!DatabaseManager::instance().isOpen()) return result;

    auto db = QSqlDatabase::database(QStringLiteral("jarvis_main"));
    if (!db.isOpen()) return result;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT ingredient FROM gourmet_preferences "
        "ORDER BY weight DESC, use_count DESC "
        "LIMIT :lim"));
    q.bindValue(QStringLiteral(":lim"), maxResults);

    if (q.exec()) {
        while (q.next())
            result.append(q.value(0).toString());
    }
    return result;
}
