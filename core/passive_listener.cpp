// ============================================================
// passive_listener.cpp — J.A.R.V.I.S. пассивная запись голоса
// ============================================================
#include "passive_listener.h"
#include "database_manager.h"

#ifdef JARVIS_VOSK_AVAILABLE
#include "vosk_api.h"
#endif

#include <QAudioSource>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>
#include <QJsonObject>
#include <QJsonDocument>
#include <cmath>
#include <vector>

// ============================================================
//  PassiveRecorder
// ============================================================

PassiveRecorder::PassiveRecorder(QObject* parent) : QObject(parent)
{
    m_silenceTimer = new QTimer(this);
    m_silenceTimer->setSingleShot(true);
    connect(m_silenceTimer, &QTimer::timeout,
            this,           &PassiveRecorder::onSilenceTimeout);
}

PassiveRecorder::~PassiveRecorder() { stop(); }

bool PassiveRecorder::start(const PassiveListenerConfig& config)
{
    if (m_recording.load()) return true;
    m_config = config;

    // 16kHz mono 16-bit — оптимально для Whisper
    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        emit error(QStringLiteral("No microphone found"));
        return false;
    }

    // Fallback на 44100 если 16000 не поддерживается
    if (!inputDevice.isFormatSupported(m_format)) {
        m_format.setSampleRate(44100);
        if (!inputDevice.isFormatSupported(m_format)) {
            emit error(QStringLiteral("Microphone format not supported"));
            return false;
        }
    }

    m_audioSource = new QAudioSource(inputDevice, m_format, this);
    m_audioSource->setBufferSize(8192);  // больше буфер = меньше CPU в пассивном режиме
    m_audioDevice = m_audioSource->start();

    if (!m_audioDevice) {
        emit error(QStringLiteral("Failed to open microphone"));
        return false;
    }

    connect(m_audioDevice, &QIODevice::readyRead,
            this,          &PassiveRecorder::onAudioDataReady);

    m_recording.store(true);
    m_speaking = false;
    m_buffer.clear();

    qDebug() << "[Passive] Recorder started, device:" << inputDevice.description();
    return true;
}

void PassiveRecorder::stop()
{
    if (!m_recording.load()) return;
    m_recording.store(false);
    m_silenceTimer->stop();
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
    }
    m_audioDevice = nullptr;
    m_buffer.clear();
    m_speaking = false;
}

void PassiveRecorder::onAudioDataReady()
{
    if (!m_audioDevice || !m_recording.load()) return;

    QByteArray raw = m_audioDevice->readAll();
    if (raw.isEmpty()) return;

    // Простой downsampler 44100 → 16000 если нужно
    QByteArray pcm16 = raw;
    if (m_format.sampleRate() == 44100) {
        const int16_t* in = reinterpret_cast<const int16_t*>(raw.constData());
        int inCount  = raw.size() / 2;
        int outCount = static_cast<int>(inCount * 16000.0 / 44100.0);
        QByteArray out(outCount * 2, '\0');
        int16_t* outPtr = reinterpret_cast<int16_t*>(out.data());
        for (int i = 0; i < outCount; ++i) {
            float srcIdx = i * 44100.0f / 16000.0f;
            int idx0 = static_cast<int>(srcIdx);
            int idx1 = qMin(idx0 + 1, inCount - 1);
            float frac = srcIdx - idx0;
            outPtr[i] = static_cast<int16_t>(in[idx0] * (1.0f - frac) + in[idx1] * frac);
        }
        pcm16 = out;
    }

    float db = computeRmsDb(pcm16);

    if (db > m_config.silenceDbThreshold) {
        // Голос обнаружен
        if (!m_speaking) {
            m_speaking = true;
            m_buffer.clear();
        }
        m_buffer.append(pcm16);
        m_silenceTimer->start(m_config.silenceAfterSpeechMs);

        // Защита от слишком длинной записи
        int durationMs = (m_buffer.size() / 2) * 1000 / 16000;
        if (durationMs >= m_config.maxRecordingMs) {
            onSilenceTimeout();
        }
    } else if (m_speaking) {
        m_buffer.append(pcm16); // добавляем хвост тишины
    }
}

void PassiveRecorder::onSilenceTimeout()
{
    if (!m_speaking) return;

    int durationMs = (m_buffer.size() / 2) * 1000 / 16000;
    if (durationMs < m_config.minSpeechMs) {
        // Слишком короткий звук — шум
        m_buffer.clear();
        m_speaking = false;
        return;
    }

    qDebug() << "[Passive] Speech captured:" << durationMs << "ms";
    emit speechCaptured(m_buffer, QDateTime::currentDateTime());
    m_buffer.clear();
    m_speaking = false;
}

float PassiveRecorder::computeRmsDb(const QByteArray& data) const
{
    if (data.size() < 2) return -100.0f;
    const int16_t* samples = reinterpret_cast<const int16_t*>(data.constData());
    int count = data.size() / 2;
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        double s = static_cast<double>(samples[i]) / 32768.0;
        sum += s * s;
    }
    double rms = std::sqrt(sum / count);
    if (rms < 1e-10) return -100.0f;
    return static_cast<float>(20.0 * std::log10(rms));
}

// ============================================================
//  PassiveTranscriber
// ============================================================

PassiveTranscriber::PassiveTranscriber(QObject* parent) : QObject(parent) {}

PassiveTranscriber::~PassiveTranscriber()
{
#ifdef JARVIS_VOSK_AVAILABLE
    if (m_recoRu) vosk_recognizer_free(static_cast<VoskRecognizer*>(m_recoRu));
    if (m_recoEn) vosk_recognizer_free(static_cast<VoskRecognizer*>(m_recoEn));
    if (m_modelRu) vosk_model_free(static_cast<VoskModel*>(m_modelRu));
    if (m_modelEn) vosk_model_free(static_cast<VoskModel*>(m_modelEn));
#endif
}

void PassiveTranscriber::loadModel(const QString& modelPath)
{
    Q_UNUSED(modelPath)  // для совместимости — используем loadModels(ru, en)
    // Вызывается из PassiveListener::initialize → там передаём оба пути
}

void PassiveTranscriber::loadModels(const QString& modelPathRu, const QString& modelPathEn)
{
    if (m_loaded) { emit modelLoaded(true); return; }

#ifdef JARVIS_VOSK_AVAILABLE
    vosk_set_log_level(-1);
    bool anyLoaded = false;

    if (QDir(modelPathRu).exists()) {
        m_modelRu = vosk_model_new(modelPathRu.toUtf8().constData());
        if (m_modelRu) {
            m_recoRu = vosk_recognizer_new(
                static_cast<VoskModel*>(m_modelRu), 16000.0f);
            if (m_recoRu) anyLoaded = true;
        }
    }

    if (QDir(modelPathEn).exists()) {
        m_modelEn = vosk_model_new(modelPathEn.toUtf8().constData());
        if (m_modelEn) {
            m_recoEn = vosk_recognizer_new(
                static_cast<VoskModel*>(m_modelEn), 16000.0f);
            if (m_recoEn) anyLoaded = true;
        }
    }

    m_loaded = anyLoaded;
    emit modelLoaded(anyLoaded);
    qDebug() << "[Passive] Vosk models loaded:" << anyLoaded;
#else
    emit modelLoaded(false);
#endif
}

void PassiveTranscriber::transcribe(QByteArray pcmData, QDateTime capturedAt, QString language)
{
#ifdef JARVIS_VOSK_AVAILABLE
    if (!m_loaded) { emit error(QStringLiteral("Vosk not loaded")); return; }

    auto tryRecognize = [&](void* reco) -> QString {
        if (!reco) return QString();
        auto* r = static_cast<VoskRecognizer*>(reco);
        vosk_recognizer_reset(r);
        vosk_recognizer_accept_waveform(r, pcmData.constData(), pcmData.size());
        const char* json = vosk_recognizer_final_result(r);
        if (!json) return QString();
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json));
        return doc.isObject()
            ? doc.object().value(QStringLiteral("text")).toString().trimmed()
            : QString();
    };

    QString resultRu = tryRecognize(m_recoRu);
    QString resultEn = tryRecognize(m_recoEn);

    // Выбираем непустой результат с приоритетом на язык настроек
    QString text, detectedLang;
    if (language == "ru" || language == "auto") {
        if (!resultRu.isEmpty()) { text = resultRu; detectedLang = "ru"; }
        else if (!resultEn.isEmpty()) { text = resultEn; detectedLang = "en"; }
    } else {
        if (!resultEn.isEmpty()) { text = resultEn; detectedLang = "en"; }
        else if (!resultRu.isEmpty()) { text = resultRu; detectedLang = "ru"; }
    }

    text = text.simplified().trimmed();
    if (text.isEmpty() || text.length() < 3) return;

    VoiceJournalEntry entry;
    entry.userId     = 1;
    entry.transcript = text;
    entry.language   = detectedLang;
    entry.confidence = 0.8f;  // Vosk не даёт общую уверенность — ставим дефолт
    entry.createdAt  = capturedAt;
    entry.processed  = false;

    qDebug() << "[Passive] Transcribed [" << detectedLang << "]:" << text;
    emit transcribed(entry);
#else
    Q_UNUSED(pcmData) Q_UNUSED(capturedAt) Q_UNUSED(language)
    emit error(QStringLiteral("Vosk not available (stub)"));
#endif
}

// ============================================================
//  PassiveListener
// ============================================================

PassiveListener::PassiveListener(QObject* parent) : QObject(parent)
{
    m_thread      = new QThread(this);
    m_transcriber = new PassiveTranscriber();
    m_transcriber->moveToThread(m_thread);

    m_recorder = new PassiveRecorder(this);

    m_cleanupTimer = new QTimer(this);
    m_cleanupTimer->setSingleShot(false);

    connect(m_transcriber, &PassiveTranscriber::modelLoaded,
            this,          &PassiveListener::onModelLoaded);
    connect(m_transcriber, &PassiveTranscriber::transcribed,
            this,          &PassiveListener::onTranscribed);
    connect(m_transcriber, &PassiveTranscriber::error,
            this,          &PassiveListener::errorOccurred);

    connect(m_recorder, &PassiveRecorder::speechCaptured,
            this,       &PassiveListener::onSpeechCaptured);
    connect(m_recorder, &PassiveRecorder::error,
            this,       &PassiveListener::errorOccurred);

    connect(this,          &PassiveListener::requestLoadModel,
            m_transcriber, &PassiveTranscriber::loadModel,
            Qt::QueuedConnection);
    connect(this,          &PassiveListener::requestTranscribe,
            m_transcriber, &PassiveTranscriber::transcribe,
            Qt::QueuedConnection);

    connect(m_cleanupTimer, &QTimer::timeout,
            this,           &PassiveListener::onWeeklyCleanupTimer);

    m_thread->start(QThread::IdlePriority);  // самый низкий приоритет
}

PassiveListener::~PassiveListener()
{
    stopListening();
    m_thread->quit();
    m_thread->wait(3000);
    delete m_transcriber;
}

void PassiveListener::initialize(const PassiveListenerConfig& config)
{
    m_config = config;
    ensureDatasetDir();

    // Подключаем loadModels вместо loadModel
    connect(this,          &PassiveListener::requestLoadModels,
            m_transcriber, &PassiveTranscriber::loadModels,
            Qt::QueuedConnection);

    emit requestLoadModels(config.modelPathRu, config.modelPathEn);
}

void PassiveListener::setConfig(const PassiveListenerConfig& config)
{
    m_config = config;
    ensureDatasetDir();
}

void PassiveListener::startListening()
{
    if (!m_initialized) {
        emit errorOccurred(QStringLiteral("Passive listener not initialized"));
        return;
    }
    if (m_listening) return;

    if (!m_recorder->start(m_config)) return;

    m_listening = true;
    emit listeningStarted();
    emit statusChanged(QStringLiteral("👂"));
    qDebug() << "[Passive] Listening started. Dataset path:" << m_config.datasetPath;
}

void PassiveListener::stopListening()
{
    if (!m_listening) return;
    m_recorder->stop();
    m_listening = false;
    emit listeningStopped();
    emit statusChanged(QStringLiteral("💤"));
}

bool PassiveListener::isListening() const
{
    return m_listening && m_recorder->isRecording();
}

void PassiveListener::onModelLoaded(bool ok)
{
    m_initialized = ok;
    if (ok) {
        emit ready();
        scheduleWeeklyCleanup();
        qDebug() << "[Passive] Ready";
    }
}

void PassiveListener::onSpeechCaptured(QByteArray pcmData, QDateTime capturedAt)
{
    // Передаём в Whisper поток
    emit requestTranscribe(pcmData, capturedAt, m_config.language);
}

void PassiveListener::onTranscribed(const VoiceJournalEntry& entry)
{
    saveToJournal(entry);
}

void PassiveListener::saveToJournal(const VoiceJournalEntry& entry)
{
    auto& db = DatabaseManager::instance();

    // Сохраняем в voice_journal таблицу
    db.addVoiceJournalEntry(entry.transcript, entry.language, entry.confidence, entry.createdAt);

    emit entrySaved(entry);
    qDebug() << "[Passive] Journal entry saved:" << entry.transcript.left(50);
}

void PassiveListener::processJournal()
{
    auto& db = DatabaseManager::instance();

    // Получаем необработанные записи как QList<QMap<QString,QVariant>>
    auto entries = db.getUnprocessedJournalEntries(1);
    if (entries.isEmpty()) {
        qDebug() << "[Passive] No unprocessed journal entries";
        return;
    }

    qDebug() << "[Passive] Processing" << entries.size() << "journal entries";

    int pairsCreated = 0;

    for (int i = 0; i < entries.size() - 1; ++i) {
        const QMap<QString,QVariant>& e1 = entries[i];
        const QMap<QString,QVariant>& e2 = entries[i + 1];

        QDateTime t1 = e1["created_at"].toDateTime();
        QDateTime t2 = e2["created_at"].toDateTime();
        qint64 diffSec = t1.secsTo(t2);

        // Пары фраз в окне 2-90 секунд — вероятный диалог
        if (diffSec >= 2 && diffSec <= 90) {
            DbTrainingLog log;
            log.userId      = 1;
            log.userMessage = e1["transcript"].toString();
            log.aiResponse  = e2["transcript"].toString();
            log.model       = QStringLiteral("voice_self");
            log.sessionId   = QStringLiteral("passive_%1").arg(
                t1.toString(QStringLiteral("yyyyMMdd")));
            log.rating      = 1;

            qint64 id = db.addTrainingLog(log);
            if (id > 0) ++pairsCreated;
        }

        db.markJournalEntryProcessed(e1["id"].toLongLong());
    }

    if (!entries.isEmpty())
        db.markJournalEntryProcessed(entries.last()["id"].toLongLong());

    qDebug() << "[Passive] Created" << pairsCreated << "training pairs";
    emit journalProcessed(pairsCreated);
}

void PassiveListener::scheduleWeeklyCleanup()
{
    // Запускаем каждые 24 часа — проверяем нужна ли очистка
    m_cleanupTimer->start(24 * 60 * 60 * 1000);
    qDebug() << "[Passive] Weekly cleanup scheduled";
}

void PassiveListener::onWeeklyCleanupTimer()
{
    auto& db = DatabaseManager::instance();
    int deleted = db.cleanupOldJournalEntries(1, m_config.cleanupAfterDays);
    if (deleted > 0) {
        qDebug() << "[Passive] Weekly cleanup: deleted" << deleted << "old entries";
        emit weeklyCleanupDone(deleted);
    }
}

void PassiveListener::ensureDatasetDir()
{
    if (m_config.datasetPath.isEmpty()) {
        m_config.datasetPath = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation) + QStringLiteral("/voice_dataset");
    }
    QDir().mkpath(m_config.datasetPath);
}

int PassiveListener::journalEntriesCount() const
{
    return DatabaseManager::instance().voiceJournalCount(1, false);
}

int PassiveListener::processedEntriesCount() const
{
    return DatabaseManager::instance().voiceJournalCount(1, true);
}
