// ============================================================
// gourmet_module.cpp — Gourmet & Mixology Assistant
// ============================================================
#include "gourmet_module.h"
#include "jarvis.h"

#include <QDebug>

GourmetModule::GourmetModule(QObject* parent)
    : QObject(parent)
{}

QString GourmetModule::processIngredients(const QString& ingredients, bool english)
{
    if (ingredients.trimmed().isEmpty()) {
        return english
            ? QStringLiteral("Usage: `/fridge milk, eggs, flour, sugar`\n"
                             "I'll suggest a recipe or cocktail based on your ingredients.")
            : QStringLiteral("Использование: `/fridge молоко, яйца, мука, сахар`\n"
                             "Я предложу рецепт или коктейль из твоих ингредиентов.");
    }

    const QString prompt = QStringLiteral(
        "[GOURMET_MODE: The user lists ingredients from their fridge/bar. "
        "Suggest ONE recipe or cocktail that uses as many listed ingredients as possible. "
        "Format as stylized markdown: dish name, ingredient list with quantities, "
        "step-by-step instructions. If ingredients suggest a cocktail — give a mixology recipe. "
        "If ingredients suggest food — give a cooking recipe. "
        "Be creative but practical. "
        "Respond in %1.]\n\n"
        "My ingredients: %2"
    ).arg(english ? QStringLiteral("English") : QStringLiteral("Russian"),
          ingredients);

    if (m_jarvis) {
        m_jarvis->processCommand(prompt);
        qDebug() << "[GourmetModule] Dispatched ingredient query:"
                 << ingredients.left(80);
    }

    return english
        ? QStringLiteral("🍳 Analyzing your ingredients... Recipe incoming!")
        : QStringLiteral("🍳 Анализирую ингредиенты... Рецепт уже в пути!");
}
