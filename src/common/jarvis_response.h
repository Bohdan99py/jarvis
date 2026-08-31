#pragma once
// ============================================================
// jarvis_response.h — Dual-Response Payload Structure
//
// Every LLM response is split into two channels:
//   fullText   — complete technical output (markdown, code, logs)
//                routed to Telegram + UI log
//   speechText — 1-2 sentence conversational summary
//                routed to VoiceSynthesisManager for TTS
//
// The LLM is instructed to prepend [SPEECH: ...] at the top.
// If missing, a heuristic fallback generates spoken text.
// ============================================================

#include <QString>
#include <QRegularExpression>

struct JarvisResponse
{
    QString fullText;
    QString speechText;

    bool hasSpeech() const { return !speechText.isEmpty(); }

    static JarvisResponse parse(const QString& rawResponse)
    {
        JarvisResponse r;

        // Try to extract [SPEECH: ...] tag from the first line.
        // Без DotMatchesEverything: маркер занимает ровно одну строку, а с
        // ним «.» перескакивала перевод строки, и при отсутствующей
        // закрывающей скобке в реплику уходил кусок ответа до первой ']'
        // где-нибудь в ссылке или коде.
        static const QRegularExpression re(
            QStringLiteral(R"(^\[SPEECH:\s*([^\]\n]+)\]\s*\n?)"));

        const QRegularExpressionMatch m = re.match(rawResponse);
        if (m.hasMatch()) {
            r.speechText = m.captured(1).trimmed();
            r.fullText   = rawResponse.mid(m.capturedEnd()).trimmed();
        } else {
            r.fullText   = rawResponse;
            r.speechText = generateFallbackSpeech(rawResponse);
        }

        return r;
    }

    static QString generateFallbackSpeech(const QString& text)
    {
        if (text.length() <= 120
            && !text.contains(QStringLiteral("```"))
            && !text.contains(QStringLiteral("[FILE:")))
        {
            return text;
        }

        const bool russian = text.contains(QRegularExpression(
            QStringLiteral("[а-яА-ЯёЁ]{3,}")));

        if (text.contains(QStringLiteral("```"))) {
            return russian
                ? QStringLiteral("Готово, я подготовил код. Посмотри в чате.")
                : QStringLiteral("Done, I've prepared the code. Check the chat.");
        }
        if (text.contains(QStringLiteral("[FILE:"))
            || text.contains(QStringLiteral("✅")))
        {
            return russian
                ? QStringLiteral("Файлы обновлены, всё применил.")
                : QStringLiteral("Files updated, changes applied.");
        }

        // Extract first sentence
        for (int i = 20; i < qMin(text.length(), 150); ++i) {
            QChar c = text[i];
            if ((c == '.' || c == '!' || c == '?')
                && i + 1 < text.length() && text[i + 1].isSpace())
            {
                return text.left(i + 1).trimmed();
            }
        }

        return russian
            ? QStringLiteral("Вот что я нашёл. Детали в чате.")
            : QStringLiteral("Here's what I found. Details in the chat.");
    }
};
