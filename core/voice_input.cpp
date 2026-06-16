// ============================================================
// voice_input.cpp — J.A.R.V.I.S. голосовой ввод (Vosk)
// Автоматическая установка: DLL + модели при первом запуске
// ============================================================
#include "voice_input.h"

#ifdef JARVIS_VOSK_AVAILABLE
#include "vosk_api.h"
#endif

#include <QAudioSource>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QDebug>
#include <cmath>

// ============================================================
//  URLs для скачивания
// ============================================================

// Vosk SDK для Windows x64 — содержит libvosk.dll + libvosk.lib + vosk_api.h
static const QString VOSK_DLL_URL =
    QStringLiteral("https://github.com/alphacep/vosk-api/releases/download/v0.3.45/vosk-win64-0.3.45.zip");

// Модели
static const QString VOSK_MODEL_EN_URL =
    QStringLiteral("https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip");
static const QString VOSK_MODEL_RU_URL =
    QStringLiteral("https://alphacephei.com/vosk/models/vosk-model-ru-0.42.zip");

// Имена папок внутри ZIP (для strip prefix при распаковке)
static const QString VOSK_MODEL_EN_PREFIX = QStringLiteral("vosk-model-small-en-us-0.15");
static const QString VOSK_MODEL_RU_PREFIX = QStringLiteral("vosk-model-ru-0.42");
static const QString VOSK_DLL_PREFIX      = QStringLiteral("vosk-win64-0.3.45");

// ============================================================
//  VoskSetupStatus — проверка установки
// ============================================================

VoskSetupStatus VoskDownloader::checkStatus(const QString& installDir)
{
    VoskSetupStatus s;
    s.dllReady     = QFile::exists(installDir + QStringLiteral("/libvosk.dll"));
    s.modelRuReady = QDir(installDir + QStringLiteral("/model-ru")).exists()
                  && QFile::exists(installDir + QStringLiteral("/model-ru/am/final.mdl"));
    s.modelEnReady = QDir(installDir + QStringLiteral("/model-en")).exists()
                  && QFile::exists(installDir + QStringLiteral("/model-en/am/final.mdl"));
    return s;
}

// ============================================================
//  VoskDownloader
// ============================================================

VoskDownloader::VoskDownloader(QObject* parent) : QObject(parent) {}

void VoskDownloader::setupVosk(const QString& installDir)
{
    m_installDir = installDir;
    QDir().mkpath(installDir);

    auto status = checkStatus(installDir);

    emit logMessage(QStringLiteral("🔍 Checking Vosk installation in: %1").arg(installDir));

    // Скачиваем DLL если нет
    if (!status.dllReady) {
        emit logMessage(QStringLiteral("📥 Downloading Vosk runtime (libvosk.dll)..."));
        downloadAndExtract(
            QStringLiteral("dll"),
            VOSK_DLL_URL,
            installDir,
            VOSK_DLL_PREFIX
        );
        // Перепроверяем
        status = checkStatus(installDir);
        if (!status.dllReady) {
            emit setupFinished(false,
                QStringLiteral("Failed to install libvosk.dll.\n"
                               "Manual install: https://github.com/alphacep/vosk-api/releases"));
            return;
        }
    } else {
        emit logMessage(QStringLiteral("✅ libvosk.dll — already installed"));
        emit componentReady(QStringLiteral("dll"));
    }

    // Скачиваем EN модель первой (маленькая, быстрый старт)
    if (!status.modelEnReady) {
        emit logMessage(QStringLiteral("📥 Downloading English model (~40 MB)..."));
        downloadAndExtract(
            QStringLiteral("model-en"),
            VOSK_MODEL_EN_URL,
            installDir + QStringLiteral("/model-en"),
            VOSK_MODEL_EN_PREFIX
        );
    } else {
        emit logMessage(QStringLiteral("✅ English model — already installed"));
        emit componentReady(QStringLiteral("model-en"));
    }

    // Скачиваем RU модель (большая)
    status = checkStatus(installDir);
    if (!status.modelRuReady) {
        emit logMessage(QStringLiteral("📥 Downloading Russian model (~1.8 GB, please wait)..."));
        downloadAndExtract(
            QStringLiteral("model-ru"),
            VOSK_MODEL_RU_URL,
            installDir + QStringLiteral("/model-ru"),
            VOSK_MODEL_RU_PREFIX
        );
    } else {
        emit logMessage(QStringLiteral("✅ Russian model — already installed"));
        emit componentReady(QStringLiteral("model-ru"));
    }

    status = checkStatus(installDir);
    if (status.anyModelReady()) {
        emit logMessage(QStringLiteral("🎉 Vosk setup complete! Voice input is ready."));
        emit setupFinished(true, QString());
    } else {
        emit setupFinished(false,
            QStringLiteral("No Vosk models could be installed.\n"
                           "Check internet connection and try again."));
    }
}

void VoskDownloader::downloadAndExtract(const QString& name,
                                         const QString& url,
                                         const QString& extractTo,
                                         const QString& stripPrefix)
{
    emit downloadStarted(name);

    // Временный файл для ZIP
    QString tempPath = QDir::tempPath() + QStringLiteral("/jarvis_vosk_%1.zip").arg(name);

    QNetworkAccessManager nam;
    QUrl qurl(url);                          // без Most Vexing Parse
    QNetworkRequest req(qurl);
    // Qt6: RedirectPolicy задаётся через setTransferTimeout или напрямую
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QVariant::fromValue(QNetworkRequest::NoLessSafeRedirectPolicy));

    QNetworkReply* reply = nam.get(req);

    // Синхронное ожидание (мы в отдельном потоке)
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::downloadProgress,
            this, [this, name](qint64 received, qint64 total) {
        int pct = (total > 0) ? static_cast<int>(received * 100 / total) : 0;
        emit downloadProgress(name, pct, total);
    });
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        emit logMessage(QStringLiteral("❌ Download failed [%1]: %2")
                        .arg(name, reply->errorString()));
        reply->deleteLater();
        return;
    }

    // Сохраняем ZIP
    QFile zipFile(tempPath);
    if (!zipFile.open(QIODevice::WriteOnly)) {
        emit logMessage(QStringLiteral("❌ Cannot write temp file: %1").arg(tempPath));
        reply->deleteLater();
        return;
    }
    zipFile.write(reply->readAll());
    zipFile.close();
    reply->deleteLater();

    emit logMessage(QStringLiteral("📦 Extracting %1...").arg(name));
    emit extracting(name);

    // Распаковываем через PowerShell
    bool ok = extractZipPowerShell(tempPath, extractTo, stripPrefix);

    QFile::remove(tempPath);

    if (ok) {
        emit logMessage(QStringLiteral("✅ %1 installed to: %2").arg(name, extractTo));
        emit componentReady(name);
    } else {
        emit logMessage(QStringLiteral("❌ Extraction failed for: %1").arg(name));
    }
}

bool VoskDownloader::extractZipPowerShell(const QString& zipPath,
                                           const QString& targetDir,
                                           const QString& stripPrefix)
{
    QDir().mkpath(targetDir);

    // Используем PowerShell Expand-Archive (встроен в Windows 10+)
    // stripPrefix — убираем верхнюю папку из архива
    QString script;
    if (stripPrefix.isEmpty()) {
        script = QStringLiteral(
            "Expand-Archive -Path '%1' -DestinationPath '%2' -Force"
        ).arg(zipPath, targetDir);
    } else {
        // Распаковываем во временную папку, потом перемещаем содержимое
        QString tempExtract = targetDir + QStringLiteral("_tmp_extract");
        script = QStringLiteral(
            "$tmp = '%1'; "
            "Expand-Archive -Path '%2' -DestinationPath $tmp -Force; "
            "$src = Join-Path $tmp '%3'; "
            "if (Test-Path $src) { "
            "  Get-ChildItem $src | Move-Item -Destination '%4' -Force; "
            "  Remove-Item $tmp -Recurse -Force "
            "} else { "
            "  Get-ChildItem $tmp | Move-Item -Destination '%4' -Force; "
            "  Remove-Item $tmp -Recurse -Force "
            "}"
        ).arg(tempExtract, zipPath, stripPrefix, targetDir);
    }

    QProcess ps;
    ps.start(QStringLiteral("powershell.exe"),
             { QStringLiteral("-NoProfile"),
               QStringLiteral("-NonInteractive"),
               QStringLiteral("-Command"),
               script });

    if (!ps.waitForStarted(5000)) {
        qWarning() << "[Vosk] PowerShell not available";
        return false;
    }

    ps.waitForFinished(300000);  // 5 минут максимум

    if (ps.exitCode() != 0) {
        qWarning() << "[Vosk] PowerShell extract error:"
                   << ps.readAllStandardError();
        return false;
    }

    return true;
}

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

VoiceRecorder::~VoiceRecorder() { stop(); }

bool VoiceRecorder::start(const WhisperConfig& config)
{
    if (m_recording.load()) return true;
    m_config = config;

    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        emit error(QStringLiteral("No audio input device found"));
        return false;
    }

    if (!inputDevice.isFormatSupported(m_format)) {
        m_format.setSampleRate(44100);
        if (!inputDevice.isFormatSupported(m_format)) {
            emit error(QStringLiteral("Audio format not supported"));
            return false;
        }
    }

    m_audioSource = new QAudioSource(inputDevice, m_format, this);
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
    qDebug() << "[Voice] Recorder started:" << inputDevice.description();
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
}

void VoiceRecorder::onAudioDataReady()
{
    if (!m_audioDevice || !m_recording.load()) return;
    QByteArray raw = m_audioDevice->readAll();
    if (raw.isEmpty()) return;

    QByteArray pcm16 = (m_format.sampleRate() == 16000)
                       ? raw : downsample44to16(raw);

    emit audioChunkReady(pcm16);

    float db = computeRmsDb(pcm16);
    emit volumeLevel(db);   // для VAD индикатора в UI
    if (db > m_config.silenceDbThreshold) {
        if (!m_speaking) {
            m_speaking = true;
            m_currentBuffer.clear();
            emit speechStarted();
        }
        m_currentBuffer.append(pcm16);
        m_silenceTimer->start(m_config.silenceAfterSpeechMs);
        int ms = (m_currentBuffer.size() / 2) * 1000 / 16000;
        if (ms >= m_config.maxRecordingMs) onSilenceTimeout();
    } else if (m_speaking) {
        m_currentBuffer.append(pcm16);
    }
}

void VoiceRecorder::onSilenceTimeout()
{
    if (!m_speaking) return;
    int ms = (m_currentBuffer.size() / 2) * 1000 / 16000;
    if (ms < m_config.minSpeechMs) {
        m_currentBuffer.clear();
        m_speaking = false;
        return;
    }
    emit speechEnded(m_currentBuffer);
    m_currentBuffer.clear();
    m_speaking = false;
}

float VoiceRecorder::computeRmsDb(const QByteArray& data) const
{
    if (data.size() < 2) return -100.0f;
    const int16_t* s = reinterpret_cast<const int16_t*>(data.constData());
    int n = data.size() / 2;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) { double v = s[i] / 32768.0; sum += v * v; }
    double rms = std::sqrt(sum / n);
    return (rms < 1e-10) ? -100.0f : static_cast<float>(20.0 * std::log10(rms));
}

QByteArray VoiceRecorder::downsample44to16(const QByteArray& src) const
{
    const int16_t* in = reinterpret_cast<const int16_t*>(src.constData());
    int inN  = src.size() / 2;
    int outN = static_cast<int>(inN * 16000.0 / 44100.0);
    QByteArray out(outN * 2, '\0');
    int16_t* o = reinterpret_cast<int16_t*>(out.data());
    for (int i = 0; i < outN; ++i) {
        float fi = i * 44100.0f / 16000.0f;
        int i0 = static_cast<int>(fi), i1 = qMin(i0 + 1, inN - 1);
        float f = fi - i0;
        o[i] = static_cast<int16_t>(in[i0] * (1.0f - f) + in[i1] * f);
    }
    return out;
}

// ============================================================
//  VoskWorker
// ============================================================

VoskWorker::VoskWorker(QObject* parent) : QObject(parent) {}

VoskWorker::~VoskWorker()
{
#ifdef JARVIS_VOSK_AVAILABLE
    if (m_recoRu) vosk_recognizer_free(static_cast<VoskRecognizer*>(m_recoRu));
    if (m_recoEn) vosk_recognizer_free(static_cast<VoskRecognizer*>(m_recoEn));
    if (m_modelRu) vosk_model_free(static_cast<VoskModel*>(m_modelRu));
    if (m_modelEn) vosk_model_free(static_cast<VoskModel*>(m_modelEn));
#endif
}

void VoskWorker::loadModels(const QString& modelPathRu,
                             const QString& modelPathEn,
                             int threads)
{
    if (m_loaded) { emit modelsLoaded(true, QString()); return; }

#ifdef JARVIS_VOSK_AVAILABLE
    vosk_set_log_level(-1);
    bool any = false;

    if (QDir(modelPathRu).exists()) {
        m_modelRu = vosk_model_new(modelPathRu.toUtf8().constData());
        if (m_modelRu) {
            m_recoRu = vosk_recognizer_new(
                static_cast<VoskModel*>(m_modelRu), 16000.0f);
            if (m_recoRu) {
                vosk_recognizer_set_words(static_cast<VoskRecognizer*>(m_recoRu), 1);
                any = true;
                qDebug() << "[Vosk] RU model loaded";
            }
        }
    }
    if (QDir(modelPathEn).exists()) {
        m_modelEn = vosk_model_new(modelPathEn.toUtf8().constData());
        if (m_modelEn) {
            m_recoEn = vosk_recognizer_new(
                static_cast<VoskModel*>(m_modelEn), 16000.0f);
            if (m_recoEn) {
                vosk_recognizer_set_words(static_cast<VoskRecognizer*>(m_recoEn), 1);
                any = true;
                qDebug() << "[Vosk] EN model loaded";
            }
        }
    }
    Q_UNUSED(threads)
    m_loaded = any;
    emit modelsLoaded(any, any ? QString()
        : QStringLiteral("Failed to load any Vosk model"));
#else
    Q_UNUSED(modelPathRu) Q_UNUSED(modelPathEn) Q_UNUSED(threads)
    emit modelsLoaded(false, QStringLiteral("Vosk stub build"));
#endif
}

void VoskWorker::recognize(QByteArray pcmData, QString preferredLang)
{
#ifdef JARVIS_VOSK_AVAILABLE
    if (!m_loaded) { emit error(QStringLiteral("Models not loaded")); return; }

    bool whisper = isWhisperLevel(pcmData);

    auto tryReco = [&](void* reco) -> QString {
        if (!reco) return {};
        auto* r = static_cast<VoskRecognizer*>(reco);
        vosk_recognizer_reset(r);
        vosk_recognizer_accept_waveform(r, pcmData.constData(), pcmData.size());
        const char* j = vosk_recognizer_final_result(r);
        if (!j) return {};
        QJsonDocument d = QJsonDocument::fromJson(QByteArray(j));
        return d.isObject()
            ? d.object().value(QStringLiteral("text")).toString().trimmed()
            : QString();
    };

    QString ru = tryReco(m_recoRu);
    QString en = tryReco(m_recoEn);

    QString text, lang;
    if (preferredLang == "ru" || preferredLang == "auto") {
        if (!ru.isEmpty()) { text = ru; lang = "ru"; }
        else if (!en.isEmpty()) { text = en; lang = "en"; }
    } else {
        if (!en.isEmpty()) { text = en; lang = "en"; }
        else if (!ru.isEmpty()) { text = ru; lang = "ru"; }
    }

    text = text.simplified().trimmed();
    if (text.isEmpty()) return;

    qDebug() << "[Vosk] [" << lang << "]:" << text << (whisper ? "[WHISPER]" : "");
    emit recognized(text, lang, whisper);
#else
    Q_UNUSED(pcmData) Q_UNUSED(preferredLang)
    emit error(QStringLiteral("Vosk not available"));
#endif
}

QString VoskWorker::tryRecognize(void* recognizer, const QByteArray& pcmData) const
{
#ifdef JARVIS_VOSK_AVAILABLE
    auto* r = static_cast<VoskRecognizer*>(recognizer);
    vosk_recognizer_reset(r);
    vosk_recognizer_accept_waveform(r, pcmData.constData(), pcmData.size());
    const char* j = vosk_recognizer_final_result(r);
    if (!j) return {};
    QJsonDocument d = QJsonDocument::fromJson(QByteArray(j));
    return d.isObject()
        ? d.object().value(QStringLiteral("text")).toString().trimmed()
        : QString();
#else
    Q_UNUSED(recognizer) Q_UNUSED(pcmData) return {};
#endif
}

bool VoskWorker::isWhisperLevel(const QByteArray& data) const
{
    if (data.size() < 2) return false;
    const int16_t* s = reinterpret_cast<const int16_t*>(data.constData());
    int n = data.size() / 2;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) { double v = s[i] / 32768.0; sum += v * v; }
    double rms = std::sqrt(sum / n);
    if (rms < 1e-10) return false;
    return (20.0 * std::log10(rms)) < -35.0;
}

// ============================================================
//  VoiceInput
// ============================================================

VoiceInput::VoiceInput(QObject* parent) : QObject(parent)
{
    m_thread = new QThread(this);
    m_worker = new VoskWorker();
    m_worker->moveToThread(m_thread);
    m_recorder = new VoiceRecorder(this);

    connect(m_worker,   &VoskWorker::modelsLoaded, this, &VoiceInput::onModelsLoaded);
    connect(m_worker,   &VoskWorker::recognized,   this, &VoiceInput::onRecognized);
    connect(m_worker,   &VoskWorker::error,        this, &VoiceInput::errorOccurred);
    connect(m_recorder, &VoiceRecorder::speechStarted, this, &VoiceInput::speechDetected);
    connect(m_recorder, &VoiceRecorder::volumeLevel,    this, &VoiceInput::volumeLevel);
    connect(m_recorder, &VoiceRecorder::speechEnded,   this, &VoiceInput::onSpeechEnded);
    connect(m_recorder, &VoiceRecorder::error,         this, &VoiceInput::onRecorderError);
    connect(this, &VoiceInput::requestLoadModels, m_worker, &VoskWorker::loadModels,
            Qt::QueuedConnection);
    connect(this, &VoiceInput::requestRecognize,  m_worker, &VoskWorker::recognize,
            Qt::QueuedConnection);

    m_thread->start(QThread::LowPriority);
}

VoiceInput::~VoiceInput()
{
    stopListening();
    m_thread->quit();
    m_thread->wait(3000);
    delete m_worker;
    if (m_setupThread) {
        m_setupThread->quit();
        m_setupThread->wait(3000);
        delete m_downloader;
    }
}

QString VoiceInput::voskInstallDir()
{
    // 1. Рядом с exe (для установленной версии — windeployqt скопирует DLL)
    QString exeDir = QCoreApplication::applicationDirPath();
    if (QFile::exists(exeDir + QStringLiteral("/libvosk.dll")))
        return exeDir;

    // 2. AppData/JARVIS/vosk (скачанная версия)
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/vosk");
}

VoskSetupStatus VoiceInput::checkSetupStatus()
{
    return VoskDownloader::checkStatus(voskInstallDir());
}

void VoiceInput::initialize(const WhisperConfig& config)
{
    m_config = config;

    auto status = checkSetupStatus();

    if (!status.fullyReady()) {
        qDebug() << "[Voice] Vosk not installed, starting setup...";
        emit setupRequired();
        startSetup();
        return;
    }

    // Всё есть — загружаем модели
    loadModelsFromDisk();
}

void VoiceInput::startSetup()
{
    QString installDir = voskInstallDir();

    m_setupThread = new QThread(this);
    m_downloader  = new VoskDownloader();
    m_downloader->moveToThread(m_setupThread);

    connect(m_downloader, &VoskDownloader::downloadStarted, this,
            [this](const QString& c) {
        emit setupLogMessage(QStringLiteral("⬇ Downloading: %1").arg(c));
    });
    connect(m_downloader, &VoskDownloader::downloadProgress, this,
            [this](const QString& c, int pct, qint64 total) {
        emit setupProgress(c, pct, total);
    });
    connect(m_downloader, &VoskDownloader::extracting, this,
            [this](const QString& c) {
        emit setupLogMessage(QStringLiteral("📦 Extracting: %1...").arg(c));
    });
    connect(m_downloader, &VoskDownloader::componentReady, this,
            [this](const QString& c) {
        emit setupComponentReady(c);
    });
    connect(m_downloader, &VoskDownloader::logMessage, this,
            [this](const QString& msg) {
        emit setupLogMessage(msg);
    });
    connect(m_downloader, &VoskDownloader::setupFinished,
            this, &VoiceInput::onSetupFinished);

    // Запускаем установку при старте потока
    connect(m_setupThread, &QThread::started,
            m_downloader,  [this, installDir]() {
        m_downloader->setupVosk(installDir);
    }, Qt::QueuedConnection);

    m_setupThread->start(QThread::LowPriority);
}

void VoiceInput::onSetupFinished(bool success, const QString& error)
{
    m_setupThread->quit();

    if (!success) {
        emit setupFinished(false, error);
        emit initError(error);
        return;
    }

    emit setupFinished(true, QString());
    // Загружаем модели которые только что скачали
    loadModelsFromDisk();
}

void VoiceInput::loadModelsFromDisk()
{
    QString installDir = voskInstallDir();
    QString pathRu = installDir + QStringLiteral("/model-ru");
    QString pathEn = installDir + QStringLiteral("/model-en");

    m_config.modelPathRu = pathRu;
    m_config.modelPathEn = pathEn;

    qDebug() << "[Voice] Loading models. RU:" << pathRu << "EN:" << pathEn;
    emit requestLoadModels(pathRu, pathEn, m_config.threads);
}

void VoiceInput::onModelsLoaded(bool success, const QString& err)
{
    if (!success) { emit initError(err); return; }
    m_initialized = true;
    qDebug() << "[Voice] Vosk ready";
    emit ready();
}

void VoiceInput::startListening()
{
    if (!m_initialized) {
        emit errorOccurred(QStringLiteral("Voice input not ready yet"));
        return;
    }
    if (m_listening) return;
    if (!m_recorder->start(m_config)) return;
    m_listening = true;
    emit listeningStarted();
}

void VoiceInput::stopListening()
{
    if (!m_listening) return;
    m_recorder->stop();
    m_listening = false;
}

bool VoiceInput::isListening() const
{
    return m_listening && m_recorder->isRecording();
}

void VoiceInput::setConfig(const WhisperConfig& config) { m_config = config; }

void VoiceInput::onSpeechEnded(QByteArray pcmData)
{
    emit requestRecognize(pcmData, m_config.language);
}

void VoiceInput::onRecognized(const QString& text, const QString& lang, bool isWhisper)
{
    if (text.isEmpty()) return;
    emit whisperModeDetected(isWhisper);

    if (m_wakeWordMode) {
        QString lower = text.toLower();
        for (const QString& ww : m_config.wakeWords) {
            if (lower.contains(ww)) {
                emit wakeWordDetected(ww);
                QString cmd = lower;
                cmd.remove(ww);
                cmd = cmd.simplified();
                if (!cmd.isEmpty()) emit textRecognized(cmd, lang);
                return;
            }
        }
        return;
    }
    emit textRecognized(text, lang);
}

void VoiceInput::onRecorderError(const QString& message)
{
    emit errorOccurred(message);
}

QString VoiceInput::resolveModelPath(const QString& subdir)
{
    QString installDir = voskInstallDir();
    return installDir + QStringLiteral("/") + subdir;
}
