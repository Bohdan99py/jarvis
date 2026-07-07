// ============================================================
// translation_engine.cpp — Multilingual Translation & Audio Pipeline
// ============================================================

#include "translation_engine.h"
#include "claude_api.h"
#include "activity_tracker.h"
#include "database_manager.h"
#include "voice_input.h"

#ifdef JARVIS_VOSK_AVAILABLE
#include "vosk_api.h"
#endif

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QDebug>
#include <QAudioDecoder>
#include <QAudioBuffer>
#include <QAudioFormat>
#include <QEventLoop>
#include <QUrl>
#include <QTimer>

// ============================================================
//  Универсальное декодирование аудио в 16kHz mono PCM16
//
// Раньше здесь просто читались сырые байты файла и (в лучшем случае)
// отрезался 44-байтовый WAV-заголовок — для MP3/M4A/OGG (сжатые
// форматы, как голосовые Telegram-сообщения) это подавало на вход
// Vosk мусор вместо PCM и распознавание либо падало, либо не находило
// текст. QAudioDecoder (бэкенд FFmpeg, уже используется Qt Multimedia)
// декодирует ЛЮБОЙ поддерживаемый контейнер/кодек в нужный PCM-формат.
// ============================================================

static QByteArray decodeAudioToPcm16Mono16k(const QString& filePath, QString* errorOut)
{
    QAudioFormat targetFormat;
    targetFormat.setSampleRate(16000);
    targetFormat.setChannelCount(1);
    targetFormat.setSampleFormat(QAudioFormat::Int16);

    QAudioDecoder decoder;
    decoder.setAudioFormat(targetFormat);
    decoder.setSource(QUrl::fromLocalFile(filePath));

    QByteArray pcm;
    QEventLoop loop;
    bool failed = false;

    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &decoder, [&]() {
        const QAudioBuffer buffer = decoder.read();
        if (buffer.isValid() && buffer.constData<char>()) {
            pcm.append(reinterpret_cast<const char*>(buffer.constData<char>()),
                       static_cast<int>(buffer.byteCount()));
        }
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    // QAudioDecoder::error is both a signal AND an accessor method with the
    // same name — needs QOverload to disambiguate at the pointer-to-member
    // step (plain &QAudioDecoder::error is ambiguous).
    QObject::connect(&decoder,
                     QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error),
                     &loop, [&](QAudioDecoder::Error) {
        failed = true;
        if (errorOut) *errorOut = decoder.errorString();
        loop.quit();
    });

    // Защита от зависания декодера на повреждённом/неподдерживаемом файле
    QTimer safetyTimer;
    safetyTimer.setSingleShot(true);
    QObject::connect(&safetyTimer, &QTimer::timeout, &loop, [&]() {
        failed = true;
        if (errorOut) *errorOut = QStringLiteral("Decoder timeout");
        loop.quit();
    });
    safetyTimer.start(60000);

    decoder.start();
    loop.exec();

    if (failed) return {};
    return pcm;
}

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
    // Универсальное декодирование: WAV/MP3/M4A/OGG/... → 16kHz mono PCM16.
    // Раньше здесь читались сырые байты файла с отрезанием 44-байтового
    // WAV-заголовка — для сжатых форматов (mp3/m4a — именно то, что
    // присылает Telegram и что просил поддержать пользователь) это
    // кормило Vosk мусором вместо PCM, транскрипция либо падала, либо
    // возвращала пустой текст.
    QString decodeError;
    QByteArray audioData = decodeAudioToPcm16Mono16k(filePath, &decodeError);
    if (audioData.isEmpty()) {
        emit audioProcessingError(decodeError.isEmpty()
            ? QStringLiteral("Could not decode audio file (unsupported or corrupt format)")
            : QStringLiteral("Audio decode error: ") + decodeError);
        return;
    }

    // Модели ищем там, где их реально ставит VoiceInput/VoskSetupDialog
    // (AppData/vosk) — не в вымышленном "redist/vosk/model-fr" (такой
    // модели не существует ни в каталоге, ни в установщике; French
    // вообще не поддерживается VoskModels::catalog()).
    const QString installDir = VoiceInput::voskInstallDir();

    VoskModel* model = nullptr;
    QString detectedLang;
    for (const QString& id : {QStringLiteral("ru-small"), QStringLiteral("en-small")}) {
        const auto info = VoskModels::findById(id);
        if (info.id.isEmpty() || !info.isInstalled(installDir)) continue;
        VoskModel* m = vosk_model_new(info.fullPath(installDir).toUtf8().constData());
        if (m) { model = m; detectedLang = info.language; break; }
    }

    if (!model) {
        emit audioProcessingError(QStringLiteral(
            "No Vosk model installed — install one via Settings → Voice."));
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
