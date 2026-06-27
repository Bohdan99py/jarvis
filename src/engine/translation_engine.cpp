// ============================================================
// translation_engine.cpp — Multilingual Translation & Audio Pipeline
// ============================================================

#include "translation_engine.h"
#include "claude_api.h"
#include "activity_tracker.h"
#include "database_manager.h"

#ifdef JARVIS_VOSK_AVAILABLE
#include "vosk_api.h"
#endif

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QDebug>

TranslationEngine::TranslationEngine(QObject* parent)
    : QObject(parent)
{}

QStringList TranslationEngine::supportedLanguages()
{
    return { QStringLiteral("en"), QStringLiteral("ru"), QStringLiteral("fr") };
}

// ============================================================
//  Text Translation via LLM
// ============================================================

void TranslationEngine::translateText(const QString& text, const QString& targetLang,
                                       std::function<void(bool, const QString&)> callback)
{
    if (!m_api) {
        if (callback) callback(false, QStringLiteral("No LLM API configured"));
        return;
    }

    static const QMap<QString, QString> langNames = {
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("ru"), QStringLiteral("Russian")},
        {QStringLiteral("fr"), QStringLiteral("French")},
    };

    const QString langName = langNames.value(targetLang, targetLang);

    const QString prompt = QStringLiteral(
        "[TRANSLATION TASK — STRICT RULES]\n"
        "Translate the following text to %1.\n"
        "Output ONLY the translation — no preamble, no explanation, no quotes.\n"
        "Preserve formatting, paragraphs, and technical terms.\n\n"
        "Text to translate:\n%2").arg(langName, text);

    m_api->sendMessage(prompt, [this, text, targetLang, callback](bool ok, const QString& resp) {
        if (ok && !resp.trimmed().isEmpty()) {
            emit translationReady(text, resp.trimmed(), targetLang);
            if (callback) callback(true, resp.trimmed());
        } else {
            if (callback) callback(false, ok ? QStringLiteral("Empty response") : resp);
        }
    });
}

// ============================================================
//  Audio Pipeline: Transcribe → Translate → Summarize
// ============================================================

void TranslationEngine::processAudioFile(const QString& filePath,
                                          const QString& targetLang,
                                          const QString& knowledgeTag)
{
    QFileInfo fi(filePath);
    if (!fi.exists()) {
        emit audioProcessingError(QStringLiteral("File not found: ") + filePath);
        return;
    }

    emit audioProcessingProgress(QStringLiteral("Transcribing audio..."));

    // Run transcription in background thread
    const QString path = filePath;
    const QString tgtLang = targetLang;
    const QString tag = knowledgeTag;

    QThread* thread = QThread::create([this, path, tgtLang, tag]() {
        transcribeAudioAsync(path);
    });
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    // Store params for use after transcription
    m_targetLang = tgtLang;

    connect(this, &TranslationEngine::audioTranscribed, this,
            [this, tgtLang, tag](const QString& transcript, const QString& detectedLang) {
        translateAndSummarize(transcript, detectedLang, tgtLang, tag);
    }, Qt::SingleShotConnection);

    thread->start();
}

void TranslationEngine::transcribeAudioAsync(const QString& filePath)
{
#ifdef JARVIS_VOSK_AVAILABLE
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit audioProcessingError(QStringLiteral("Cannot open: ") + filePath);
        return;
    }

    QByteArray audioData = file.readAll();
    file.close();

    // Skip WAV header (44 bytes) if present
    if (audioData.size() > 44 && audioData.startsWith("RIFF")) {
        audioData = audioData.mid(44);
    }

    if (audioData.isEmpty()) {
        emit audioProcessingError(QStringLiteral("Empty audio file"));
        return;
    }

    // Try loading models from standard paths
    auto tryModel = [](const QString& path) -> VoskModel* {
        QFileInfo fi(path);
        if (!fi.exists()) return nullptr;
        return vosk_model_new(path.toUtf8().constData());
    };

    VoskModel* model = tryModel(QStringLiteral("redist/vosk/model-fr"));
    QString detectedLang = QStringLiteral("fr");

    if (!model) {
        model = tryModel(QStringLiteral("redist/vosk/model-en"));
        detectedLang = QStringLiteral("en");
    }
    if (!model) {
        model = tryModel(QStringLiteral("redist/vosk/model-ru"));
        detectedLang = QStringLiteral("ru");
    }

    if (!model) {
        emit audioProcessingError(QStringLiteral("No Vosk model available for transcription"));
        return;
    }

    VoskRecognizer* reco = vosk_recognizer_new(model, 16000.0f);
    if (!reco) {
        vosk_model_free(model);
        emit audioProcessingError(QStringLiteral("Failed to create Vosk recognizer"));
        return;
    }

    emit audioProcessingProgress(QStringLiteral("Processing audio frames..."));

    // Feed audio in chunks
    constexpr int CHUNK = 8000;
    for (int offset = 0; offset < audioData.size(); offset += CHUNK) {
        int len = qMin(CHUNK, audioData.size() - offset);
        vosk_recognizer_accept_waveform(reco, audioData.constData() + offset, len);
    }

    const char* json = vosk_recognizer_final_result(reco);
    QString transcript;
    if (json) {
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json));
        transcript = doc.object().value(QStringLiteral("text")).toString().trimmed();
    }

    vosk_recognizer_free(reco);
    vosk_model_free(model);

    if (transcript.isEmpty()) {
        emit audioProcessingError(QStringLiteral("Transcription produced no text"));
        return;
    }

    qDebug() << "[Translation] Transcribed" << audioData.size() / 1000
             << "KB audio →" << transcript.length() << "chars";

    QMetaObject::invokeMethod(this, [this, transcript, detectedLang]() {
        emit audioTranscribed(transcript, detectedLang);
    }, Qt::QueuedConnection);

#else
    Q_UNUSED(filePath)
    emit audioProcessingError(QStringLiteral("Vosk not available — cannot transcribe audio offline. "
                                             "Send the audio file as an attachment and ask JARVIS to transcribe it."));
#endif
}

void TranslationEngine::translateAndSummarize(const QString& transcript,
                                               const QString& detectedLang,
                                               const QString& targetLang,
                                               const QString& knowledgeTag)
{
    if (!m_api) {
        emit audioProcessingError(QStringLiteral("No LLM API for translation"));
        return;
    }

    emit audioProcessingProgress(QStringLiteral("Translating and summarizing..."));

    static const QMap<QString, QString> langNames = {
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("ru"), QStringLiteral("Russian")},
        {QStringLiteral("fr"), QStringLiteral("French")},
    };

    const QString srcName = langNames.value(detectedLang, detectedLang);
    const QString tgtName = langNames.value(targetLang, targetLang);

    const QString prompt = QStringLiteral(
        "[AUDIO PROCESSING PIPELINE]\n\n"
        "Original transcript (%1):\n\"\"\"\n%2\n\"\"\"\n\n"
        "Perform these steps:\n"
        "1. TRANSLATE the full transcript to %3. Output under heading '## Translation'\n"
        "2. SUMMARIZE the content as structured Markdown under heading '## Summary' with sections:\n"
        "   - **Key Concepts** (bullet list)\n"
        "   - **Action Items** (bullet list, if any)\n"
        "   - **Core Definitions** (bullet list of terms defined)\n\n"
        "Output the Translation first, then the Summary. No preamble."
    ).arg(srcName, transcript.left(6000), tgtName);

    const qint64 userId = m_userId;
    const QString tag = knowledgeTag;

    m_api->sendMessage(prompt, [this, transcript, targetLang, userId, tag]
                       (bool ok, const QString& resp) {
        if (!ok || resp.trimmed().isEmpty()) {
            emit audioProcessingError(QStringLiteral("LLM translation failed: ") + resp);
            return;
        }

        // Extract translation section
        int transIdx = resp.indexOf(QStringLiteral("## Translation"));
        int sumIdx   = resp.indexOf(QStringLiteral("## Summary"));

        QString translated = resp;
        QString summary;

        if (transIdx >= 0 && sumIdx > transIdx) {
            translated = resp.mid(transIdx + 14, sumIdx - transIdx - 14).trimmed();
            summary    = resp.mid(sumIdx).trimmed();
        } else if (sumIdx >= 0) {
            summary = resp.mid(sumIdx).trimmed();
        }

        emit audioTranslated(translated, targetLang);

        if (!summary.isEmpty()) {
            emit audioSummaryReady(summary);

            // Store key concepts in knowledge_base
            if (m_tracker) {
                QStringList lines = summary.split('\n');
                int conceptCount = 0;
                for (const QString& line : lines) {
                    QString trimmed = line.trimmed();
                    if (trimmed.startsWith(QStringLiteral("- ")) ||
                        trimmed.startsWith(QStringLiteral("* "))) {
                        QString fact = trimmed.mid(2).trimmed();
                        if (fact.length() >= 5 && fact.length() <= 200) {
                            m_tracker->learnFact(userId, tag.toLower(),
                                QStringLiteral("audio_concept_%1_%2")
                                    .arg(tag.toLower().left(10))
                                    .arg(conceptCount),
                                fact, 0.65f);
                            ++conceptCount;
                        }
                        if (conceptCount >= 15) break;
                    }
                }
                qDebug() << "[Translation] Stored" << conceptCount
                         << "concepts under tag:" << tag;
            }
        }
    });
}
