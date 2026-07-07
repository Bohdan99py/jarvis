#pragma once
// ============================================================
// translation_engine.h — Multilingual Translation & Audio Pipeline
//
// LLM-backed translation (FR/EN/RU) + audio file ingestion:
//   1. Transcribe audio via Vosk offline pipeline
//   2. Translate transcript to target language via LLM
//   3. Generate structured Markdown summary
//   4. Store insights in knowledge_base
// ============================================================

#include <QObject>
#include <QString>
#include <QThread>
#include <functional>

class ClaudeApi;
class ActivityTracker;
class J2JMeshConnector;

class TranslationEngine : public QObject
{
    Q_OBJECT
public:
    explicit TranslationEngine(QObject* parent = nullptr);

    void setLlmApi(ClaudeApi* api)        { m_api = api; }
    void setTracker(ActivityTracker* t)    { m_tracker = t; }
    void setUserId(qint64 id)             { m_userId = id; }
    void setMeshConnector(J2JMeshConnector* mesh) { m_mesh = mesh; }

    // Active translation pair
    void setSourceLang(const QString& l)  { m_sourceLang = l; }
    void setTargetLang(const QString& l)  { m_targetLang = l; }
    QString sourceLang() const            { return m_sourceLang; }
    QString targetLang() const            { return m_targetLang; }

    static QStringList supportedLanguages();

    // Real-time text translation via LLM
    void translateText(const QString& text, const QString& targetLang,
                       std::function<void(bool, const QString&)> callback);

    // Audio pipeline: transcribe → translate → summarize → store
    void processAudioFile(const QString& filePath, const QString& targetLang,
                          const QString& knowledgeTag = QStringLiteral("University"));

    // Heard/read a phrase in a language Jarvis doesn't recognize. First
    // asks mesh peers ("has anyone seen this before?"); if none answer
    // in time, falls back to an LLM translation call. Either way, the
    // result is remembered via ActivityTracker so the same phrase is
    // recognized instantly next time — a pragmatic lookup-and-remember
    // loop, not full language acquisition.
    void learnUnknownLanguageSnippet(const QString& text);

signals:
    void translationReady(const QString& original, const QString& translated,
                          const QString& targetLang);
    void audioTranscribed(const QString& transcript, const QString& language);
    void audioTranslated(const QString& translated, const QString& targetLang);
    void audioSummaryReady(const QString& summary);
    void audioProcessingError(const QString& error);
    void audioProcessingProgress(const QString& stage);

private:
    void transcribeAudioAsync(const QString& filePath);
    void translateAndSummarize(const QString& transcript,
                               const QString& detectedLang,
                               const QString& targetLang,
                               const QString& knowledgeTag);

    ClaudeApi*        m_api     = nullptr;
    ActivityTracker*  m_tracker = nullptr;
    J2JMeshConnector*  m_mesh    = nullptr;
    qint64           m_userId  = 1;
    QString          m_sourceLang = QStringLiteral("auto");
    QString          m_targetLang = QStringLiteral("en");
};
