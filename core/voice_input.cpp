// ============================================================
// voice_input.cpp — J.A.R.V.I.S. голосовой ввод (Whisper.cpp)
// ============================================================
#include "voice_input.h"

// whisper.cpp — заголовок в include/ (новая структура) или корне (старая)
// CMakeLists добавляет правильный include path автоматически
#ifndef JARVIS_WHISPER_STUB
#include "whisper.h"
#endif

#include <QAudioSource>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <cmath>
#include <cstring>

// ============================================================
//  VoiceRecorder
// ============================================================

VoiceRecorder::VoiceRecorder(QObject* parent) : QObject(parent)
{
    m_silenceTimer = new QTimer(this);
    m_silenceTimer->setSingleShot(true);
    connect(m_silenceTimer, &QTimer::timeout,
            this,           &VoiceRecorder::onSilenceTimeout);
}

VoiceRecorder::~VoiceRecorder()
{
    stop();
}

bool VoiceRecorder::start(const WhisperConfig& config)
{
    if (m_recording.load()) return true;
    m_config = config;

    // Формат: 16-bit signed PCM, 16kHz, mono — именно то что ест Whisper
    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    // Ищем устройство ввода
    QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        emit error(QStringLiteral("No audio input device found"));
        return false;
    }

    // Проверяем поддержку формата, иначе fallback
    if (!inputDevice.isFormatSupported(m_format)) {
        // Пробуем 44100 с конвертацией
        m_format.setSampleRate(44100);
        if (!inputDevice.isFormatSupported(m_format)) {
            emit error(QStringLiteral("Audio format not supported by device"));
            return false;
        }
        qDebug() << "[Voice] Using 44100Hz with downsampling to 16kHz";
    }

    m_audioSource = new QAudioSource(inputDevice, m_format, this);
    // Маленький буфер = малая задержка обнаружения речи
    m_audioSource->setBufferSize(4096);

    m_audioDevice = m_audioSource->start();
    if (!m_audioDevice) {
        emit error(QStringLiteral("Failed to open audio device"));
        return false;
    }

    connect(m_audioDevice, &QIODevice::readyRead,
            this,          &VoiceRecorder::onAudioDataReady);

    m_recording.store(true);
    m_speaking = false;
    m_currentBuffer.clear();

    qDebug() << "[Voice] Recorder started. Device:" << inputDevice.description()
             << "| Silence threshold:" << config.silenceDbThreshold << "dB";
    return true;
}

void VoiceRecorder::stop()
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
    m_currentBuffer.clear();
    m_speaking = false;
    qDebug() << "[Voice] Recorder stopped";
}

void VoiceRecorder::onAudioDataReady()
{
    if (!m_audioDevice || !m_recording.load()) return;

    QByteArray raw = m_audioDevice->readAll();
    if (raw.isEmpty()) return;

    // Конвертируем если нужно (44100 → 16000 downsampling)
    QByteArray pcm16 = (m_format.sampleRate() == 16000)
                       ? raw
                       : convertToFloat32_16kHz(raw);

    float db = computeRmsDb(pcm16);

    if (db > m_config.silenceDbThreshold) {
        // Звук есть
        if (!m_speaking) {
            m_speaking = true;
            m_currentBuffer.clear();
            emit speechStarted();
            qDebug() << "[Voice] Speech started, level:" << db << "dB"
                     << (db < m_config.whisperDbThreshold ? "[WHISPER]" : "[VOICE]");
        }
        m_currentBuffer.append(pcm16);
        m_silenceTimer->start(m_config.silenceAfterSpeechMs);

        // Защита от слишком долгой записи
        int recordedMs = (m_currentBuffer.size() / 2) * 1000 / 16000;
        if (recordedMs >= m_config.maxRecordingMs) {
            onSilenceTimeout();
        }
    } else if (m_speaking) {
        // Добиваем буфер тишиной чтобы Whisper не обрезал конец
        m_currentBuffer.append(pcm16);
    }
}

void VoiceRecorder::onSilenceTimeout()
{
    if (!m_speaking) return;

    int durationMs = (m_currentBuffer.size() / 2) * 1000 / 16000;
    if (durationMs < m_config.minSpeechMs) {
        // Слишком короткий звук — шум, игнорируем
        qDebug() << "[Voice] Too short (" << durationMs << "ms), ignoring";
        m_currentBuffer.clear();
        m_speaking = false;
        return;
    }

    qDebug() << "[Voice] Speech ended, duration:" << durationMs << "ms,"
             << "buffer:" << m_currentBuffer.size() << "bytes";

    emit speechEnded(m_currentBuffer);
    m_currentBuffer.clear();
    m_speaking = false;
}

// ── Вычисление RMS уровня в dB ───────────────────────────────

float VoiceRecorder::computeRmsDb(const QByteArray& data) const
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

// ── Простой downsampler 44100 → 16000 ────────────────────────
// (линейная интерполяция, достаточно для голоса)

QByteArray VoiceRecorder::convertToFloat32_16kHz(const QByteArray& src) const
{
    const int16_t* in    = reinterpret_cast<const int16_t*>(src.constData());
    int inCount          = src.size() / 2;
    int outCount         = static_cast<int>(inCount * 16000.0 / 44100.0);

    QByteArray out(outCount * 2, '\0');
    int16_t* outPtr = reinterpret_cast<int16_t*>(out.data());

    for (int i = 0; i < outCount; ++i) {
        float srcIdx = i * 44100.0f / 16000.0f;
        int   idx0   = static_cast<int>(srcIdx);
        int   idx1   = qMin(idx0 + 1, inCount - 1);
        float frac   = srcIdx - idx0;
        outPtr[i] = static_cast<int16_t>(
            in[idx0] * (1.0f - frac) + in[idx1] * frac);
    }
    return out;
}

// ============================================================
//  WhisperWorker
// ============================================================

WhisperWorker::WhisperWorker(QObject* parent) : QObject(parent) {}

WhisperWorker::~WhisperWorker()
{
#ifndef JARVIS_WHISPER_STUB
    if (m_ctx) {
        whisper_free(m_ctx);
        m_ctx = nullptr;
    }
#endif
}

void WhisperWorker::loadModel(const QString& modelPath, int threads, bool useGpu)
{
#ifdef JARVIS_WHISPER_STUB
    Q_UNUSED(modelPath) Q_UNUSED(threads) Q_UNUSED(useGpu)
    emit modelLoaded(false, QStringLiteral("Whisper stub — model not compiled in"));
    return;
#else
    if (m_loaded) { emit modelLoaded(true, QString()); return; }

    if (!QFile::exists(modelPath)) {
        emit modelLoaded(false, QStringLiteral(
            "Whisper model not found: %1\n"
            "Download: https://huggingface.co/ggerganov/whisper.cpp\n"
            "Place to: redist/whisper/ggml-medium.bin").arg(modelPath));
        return;
    }

    qDebug() << "[Whisper] Loading model:" << modelPath;

    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = useGpu;

    m_ctx = whisper_init_from_file_with_params(
        modelPath.toUtf8().constData(), cparams);

    if (!m_ctx) {
        emit modelLoaded(false, QStringLiteral("Failed to load Whisper model"));
        return;
    }

    m_loaded = true;
    qDebug() << "[Whisper] Model loaded. Threads:" << threads << "| GPU:" << useGpu;
    emit modelLoaded(true, QString());
#endif
}

void WhisperWorker::transcribe(QByteArray pcmData, QString language)
{
#ifdef JARVIS_WHISPER_STUB
    Q_UNUSED(pcmData) Q_UNUSED(language)
    emit error(QStringLiteral("Whisper not available (stub build)"));
    return;
#else
    if (!m_loaded || !m_ctx) {
        emit error(QStringLiteral("Whisper model not loaded"));
        return;
    }

    int sampleCount = pcmData.size() / 2;
    std::vector<float> samples(sampleCount);
    const int16_t* raw = reinterpret_cast<const int16_t*>(pcmData.constData());
    for (int i = 0; i < sampleCount; ++i)
        samples[i] = raw[i] / 32768.0f;

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    bool autoLang = (language == "auto" || language.isEmpty());
    params.language        = autoLang ? nullptr : language.toUtf8().constData();
    params.detect_language = autoLang;
    params.temperature         = 0.0f;
    params.temperature_inc     = 0.1f;
    params.no_speech_thold     = 0.4f;
    params.logprob_thold       = -1.2f;
    params.print_timestamps    = false;
    params.print_progress      = false;
    params.print_special       = false;
    params.print_realtime      = false;
    params.n_threads           = 4;

    int ret = whisper_full(m_ctx, params, samples.data(), static_cast<int>(samples.size()));
    if (ret != 0) {
        emit error(QStringLiteral("Whisper inference failed (code %1)").arg(ret));
        return;
    }

    QString result;
    int segCount = whisper_full_n_segments(m_ctx);
    for (int i = 0; i < segCount; ++i)
        result += QString::fromUtf8(whisper_full_get_segment_text(m_ctx, i));
    result = result.simplified().trimmed();

    // В новом whisper.cpp API язык определяется через whisper_full_lang_id()
    QString detectedLang = language;
    if (autoLang) {
        int langId = whisper_full_lang_id(m_ctx);
        if (langId >= 0)
            detectedLang = QString::fromUtf8(whisper_lang_str(langId));
    }

    float confidence = 0.0f;
    if (segCount > 0) {
        for (int i = 0; i < segCount; ++i)
            confidence += (1.0f - whisper_full_get_segment_no_speech_prob(m_ctx, i));
        confidence /= segCount;
    }

    bool isWhisperMode = (confidence < 0.75f && !result.isEmpty());

    qDebug() << "[Whisper] Result:" << result
             << "| Lang:" << detectedLang
             << "| Confidence:" << confidence
             << (isWhisperMode ? "[WHISPER]" : "");

    if (!result.isEmpty())
        emit transcribed(result, detectedLang, confidence, isWhisperMode);
#endif
}

// ============================================================
//  VoiceInput — главный контроллер
// ============================================================

VoiceInput::VoiceInput(QObject* parent) : QObject(parent)
{
    // Whisper в отдельном потоке — не блокирует UI
    m_whisperThread = new QThread(this);
    m_worker        = new WhisperWorker();
    m_worker->moveToThread(m_whisperThread);

    // Рекордер в главном потоке (Qt Audio требует event loop)
    m_recorder = new VoiceRecorder(this);

    // Сигналы worker → контроллер
    connect(m_worker,   &WhisperWorker::modelLoaded,
            this,       &VoiceInput::onModelLoaded);
    connect(m_worker,   &WhisperWorker::transcribed,
            this,       &VoiceInput::onTranscribed);
    connect(m_worker,   &WhisperWorker::error,
            this,       &VoiceInput::errorOccurred);

    // Сигналы рекордера → контроллер
    connect(m_recorder, &VoiceRecorder::speechStarted,
            this,       &VoiceInput::speechDetected);
    connect(m_recorder, &VoiceRecorder::speechEnded,
            this,       &VoiceInput::onSpeechEnded);
    connect(m_recorder, &VoiceRecorder::error,
            this,       &VoiceInput::onRecorderError);

    // Управление worker через очередь (thread-safe)
    connect(this,       &VoiceInput::requestLoadModel,
            m_worker,   &WhisperWorker::loadModel,
            Qt::QueuedConnection);
    connect(this,       &VoiceInput::requestTranscribe,
            m_worker,   &WhisperWorker::transcribe,
            Qt::QueuedConnection);

    m_whisperThread->start(QThread::LowPriority);
}

VoiceInput::~VoiceInput()
{
    stopListening();
    m_whisperThread->quit();
    m_whisperThread->wait(3000);
    delete m_worker;
}

// ============================================================
//  Резолвинг пути к модели — ищем в нескольких местах
// ============================================================

QString VoiceInput::resolveModelPath(const QString& hint)
{
    const QString modelName = QStringLiteral("ggml-medium.bin");

    // Список мест где может лежать модель — от приоритетного к запасному
    QStringList candidates;

    // 1. Явно указанный путь
    if (!hint.isEmpty()) candidates << hint;

    // 2. Рядом с exe (для установленной версии через Inno Setup)
    QString exeDir = QCoreApplication::applicationDirPath();
    candidates << exeDir + QStringLiteral("/whisper/") + modelName;
    candidates << exeDir + QStringLiteral("/redist/whisper/") + modelName;

    // 3. AppData/Roaming/JARVIS/whisper/ (скачанная модель)
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    candidates << appData + QStringLiteral("/whisper/") + modelName;

    // 4. Путь разработчика (redist/whisper/ относительно CWD)
    candidates << QStringLiteral("redist/whisper/") + modelName;

    for (const QString& path : candidates) {
        if (QFile::exists(path)) {
            qDebug() << "[Voice] Model found at:" << path;
            return path;
        }
    }

    // Не нашли — возвращаем целевой путь для скачивания (AppData)
    QDir().mkpath(appData + QStringLiteral("/whisper"));
    return appData + QStringLiteral("/whisper/") + modelName;
}

void VoiceInput::initialize(const WhisperConfig& config)
{
    m_config = config;

    // Резолвим реальный путь к модели
    QString modelPath = resolveModelPath(config.modelPath);
    m_config.modelPath = modelPath;

    if (QFile::exists(modelPath)) {
        // Модель есть — сразу загружаем
        qDebug() << "[Voice] Initializing Whisper, model:" << modelPath;
        emit requestLoadModel(modelPath, config.threads, config.useGpu);
    } else {
        // Модели нет — скачиваем автоматически
        qDebug() << "[Voice] Model not found, downloading...";
        downloadModelIfNeeded();
    }
}

void VoiceInput::downloadModelIfNeeded()
{
    if (m_downloading) return;
    m_downloading = true;

    const QString modelPath = m_config.modelPath;
    const QString modelUrl  = QStringLiteral(
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin");

    qDebug() << "[Voice] Downloading model to:" << modelPath;
    emit modelDownloadProgress(0);

    auto* nam   = new QNetworkAccessManager(this);
    auto* reply = nam->get(QNetworkRequest(QUrl(modelUrl)));

    // Прогресс скачивания
    connect(reply, &QNetworkReply::downloadProgress,
            this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            int pct = static_cast<int>(received * 100 / total);
            emit modelDownloadProgress(pct);
        }
    });

    // Завершение скачивания
    connect(reply, &QNetworkReply::finished, this, [this, reply, modelPath, nam]() {
        m_downloading = false;

        if (reply->error() != QNetworkReply::NoError) {
            QString err = QStringLiteral("Model download failed: ") + reply->errorString()
                + QStringLiteral("\nDownload manually:\n")
                + QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin\n")
                + QStringLiteral("Save to: ") + modelPath;
            emit modelDownloadFinished(false, err);
            emit initError(err);
            reply->deleteLater();
            nam->deleteLater();
            return;
        }

        // Сохраняем файл
        QFile f(modelPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(reply->readAll());
            f.close();
            qDebug() << "[Voice] Model downloaded to:" << modelPath;
            emit modelDownloadFinished(true, QString());
            // Загружаем модель
            emit requestLoadModel(modelPath, m_config.threads, m_config.useGpu);
        } else {
            QString err = QStringLiteral("Cannot save model to: ") + modelPath;
            emit modelDownloadFinished(false, err);
            emit initError(err);
        }

        reply->deleteLater();
        nam->deleteLater();
    });
}

void VoiceInput::onModelLoaded(bool success, const QString& err)
{
    if (!success) {
        qWarning() << "[Voice] Model load failed:" << err;
        emit initError(err);
        return;
    }
    m_initialized = true;
    qDebug() << "[Voice] Ready. Whisper model loaded.";
    emit ready();
}

void VoiceInput::startListening()
{
    if (!m_initialized) {
        emit errorOccurred(QStringLiteral("Voice input not initialized"));
        return;
    }
    if (m_listening) return;

    if (!m_recorder->start(m_config)) return;

    m_listening = true;
    emit listeningStarted();
    qDebug() << "[Voice] Listening started"
             << (m_wakeWordMode ? "(wake word mode)" : "(always-on mode)");
}

void VoiceInput::stopListening()
{
    if (!m_listening) return;
    m_recorder->stop();
    m_listening = false;
    qDebug() << "[Voice] Listening stopped";
}

bool VoiceInput::isListening() const
{
    return m_listening && m_recorder->isRecording();
}

void VoiceInput::setConfig(const WhisperConfig& config)
{
    m_config = config;
}

void VoiceInput::onSpeechEnded(QByteArray pcmData)
{
    // Отправляем на распознавание в Whisper поток
    emit requestTranscribe(pcmData, m_config.language);
}

void VoiceInput::onTranscribed(const QString& text, const QString& lang,
                                float confidence, bool isWhisper)
{
    Q_UNUSED(confidence)

    if (text.isEmpty()) return;

    emit whisperModeDetected(isWhisper);

    // Проверяем wake word если режим активации
    if (m_wakeWordMode) {
        QString lower = text.toLower();
        for (const QString& ww : m_config.wakeWords) {
            if (lower.contains(ww)) {
                emit wakeWordDetected(ww);
                // Убираем wake word из текста и отправляем остаток
                QString cmd = lower;
                cmd.remove(ww);
                cmd = cmd.simplified();
                if (!cmd.isEmpty())
                    emit textRecognized(cmd, lang);
                return;
            }
        }
        // Wake word не найден — игнорируем (фоновый шум)
        qDebug() << "[Voice] No wake word in:" << text;
        return;
    }

    // Режим always-on — отправляем всё
    emit textRecognized(text, lang);
}

void VoiceInput::onRecorderError(const QString& message)
{
    emit errorOccurred(message);
}

bool VoiceInput::isModelAvailable(const QString& modelPath)
{
    if (!modelPath.isEmpty() && QFile::exists(modelPath))
        return true;
    // Проверяем все известные места
    return !resolveModelPath(modelPath).isEmpty()
           && QFile::exists(resolveModelPath(modelPath));
}

QString VoiceInput::defaultModelPath()
{
    return resolveModelPath();
}
