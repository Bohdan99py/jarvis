// ============================================================
// media_analyzer_module.cpp — Media Summary Module
// ============================================================
#include "media_analyzer_module.h"
#include "jarvis.h"

#include <QRegularExpression>
#include <QDebug>

MediaAnalyzerModule::MediaAnalyzerModule(QObject* parent)
    : QObject(parent)
{}

bool MediaAnalyzerModule::containsUrl(const QString& text)
{
    static const QRegularExpression reUrl(
        QStringLiteral("https?://\\S+"));
    return reUrl.match(text).hasMatch();
}

QString MediaAnalyzerModule::summarize(const QString& content, bool english)
{
    if (content.trimmed().isEmpty()) {
        return english
            ? QStringLiteral("Usage: `/summarize <article text or URL>`\n"
                             "I'll extract key points and timestamps.")
            : QStringLiteral("Использование: `/summarize <текст статьи или ссылка>`\n"
                             "Я выделю ключевые моменты и таймкоды.");
    }

    const bool hasUrl = containsUrl(content);

    const QString prompt = QStringLiteral(
        "[SUMMARIZE_MODE: The user wants a structured summary. "
        "%1"
        "Output format:\n"
        "• 📌 **Key Points** — 3-5 bullet points with the core ideas\n"
        "• 🕐 **Timestamps/Sections** — if applicable, key moments with time marks\n"
        "• 💡 **Takeaway** — one-sentence conclusion\n"
        "Be concise. Respond in %2.]\n\n"
        "%3"
    ).arg(hasUrl ? QStringLiteral("The input contains a URL — describe what the link likely covers. ")
                 : QStringLiteral("The input is raw text — summarize it directly. "),
          english ? QStringLiteral("English") : QStringLiteral("Russian"),
          content);

    if (m_jarvis) {
        m_jarvis->processCommand(prompt);
        qDebug() << "[MediaAnalyzer] Dispatched summarize request ("
                 << content.length() << "chars)";
    }

    return english
        ? QStringLiteral("📝 Analyzing content... Summary incoming!")
        : QStringLiteral("📝 Анализирую контент... Сводка уже в пути!");
}
