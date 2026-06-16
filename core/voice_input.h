#pragma once
// ============================================================
// voice_input.h — J.A.R.V.I.S. голосовой ввод (Vosk)
//
// Vosk — офлайн ASR, работает без интернета и GPU.
// При первом запуске автоматически скачивает всё необходимое:
//   1. libvosk.dll  — runtime библиотека
//   2. model-en/    — английская модель (~40MB, быстрый старт)
//   3. model-ru/    — русская модель (~1.8GB, в фоне)
// ============================================================

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QAudioSource>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <atomic>

struct WhisperConfig {
    QString modelPathRu  = QString();
    QString modelPathEn  = QString();
    float   silenceDbThreshold  = -45.0f;
    int     silenceAfterSpeechMs = 800;
    int     maxRecordingMs       = 15000;
    int     minSpeechMs          = 200;
    QString language    = QStringLiteral("auto");
    QStringList wakeWords = { "джарвис", "jarvis", "джарви" };
    int threads = 4;
};
using VoiceConfig = WhisperConfig;

struct VoskSetupStatus {
    bool dllReady     = false;
    bool modelRuReady = false;
    bool modelEnReady = false;
    bool anyModelReady() const { return modelRuReady || modelEnReady; }
    bool fullyReady()    const { return dllReady && anyModelReady(); }
};

// ============================================================
//  VoskDownloader — скачивание и распаковка
// ============================================================

class VoskDownloader : public QObject
{
    Q_OBJECT
public:
    explicit VoskDownloader(QObject* parent = nullptr);
    void setupVosk(const QString& installDir);
    static VoskSetupStatus checkStatus(const QString& installDir);

signals:
    void downloadStarted(const QString& component);
    void downloadProgress(const QString& component, int percent, qint64 bytesTotal);
    void extracting(const QString& component);
    void componentReady(const QString& component);
    void setupFinished(bool success, const QString& error);
    void logMessage(const QString& message);

private:
    void downloadAndExtract(const QString& name, const QString& url,
                            const QString& extractTo, const QString& stripPrefix);
    bool extractZipPowerShell(const QString& zipPath, const QString& targetDir,
                              const QString& stripPrefix);
    QString m_installDir;
};

// ============================================================
//  VoiceRecorder
// ============================================================

class VoiceRecorder : public QObject
{
    Q_OBJECT
public:
    explicit VoiceRecorder(QObject* parent = nullptr);
    ~VoiceRecorder() override;
    bool start(const WhisperConfig& config);
    void stop();
    bool isRecording() const { return m_recording.load(); }

signals:
    void audioChunkReady(QByteArray pcmData);
    void speechStarted();
    void speechEnded(QByteArray fullPcmData);
    void error(const QString& message);

private slots:
    void onAudioDataReady();
    void onSilenceTimeout();

private:
    float      computeRmsDb(const QByteArray& data) const;
    QByteArray downsample44to16(const QByteArray& src) const;
    QAudioSource*     m_audioSource  = nullptr;
    QIODevice*        m_audioDevice  = nullptr;
    QTimer*           m_silenceTimer = nullptr;
    QByteArray        m_currentBuffer;
    bool              m_speaking     = false;
    std::atomic<bool> m_recording    {false};
    WhisperConfig     m_config;
    QAudioFormat      m_format;
};

// ============================================================
//  VoskWorker
// ============================================================

class VoskWorker : public QObject
{
    Q_OBJECT
public:
    explicit VoskWorker(QObject* parent = nullptr);
    ~VoskWorker() override;

public slots:
    void loadModels(const QString& modelPathRu, const QString& modelPathEn, int threads);
    void recognize(QByteArray pcmData, QString preferredLang);

signals:
    void modelsLoaded(bool success, const QString& error);
    void recognized(const QString& text, const QString& detectedLanguage, bool isWhisper);
    void error(const QString& message);

private:
    QString tryRecognize(void* recognizer, const QByteArray& pcmData) const;
    bool    isWhisperLevel(const QByteArray& pcmData) const;
    void* m_modelRu = nullptr;
    void* m_modelEn = nullptr;
    void* m_recoRu  = nullptr;
    void* m_recoEn  = nullptr;
    bool  m_loaded  = false;
};

// ============================================================
//  VoiceInput — главный контроллер
// ============================================================

class VoiceInput : public QObject
{
    Q_OBJECT
public:
    explicit VoiceInput(QObject* parent = nullptr);
    ~VoiceInput() override;

    void initialize(const WhisperConfig& config = WhisperConfig{});
    void startListening();
    void stopListening();
    bool isListening() const;
    void setConfig(const WhisperConfig& config);
    const WhisperConfig& config() const { return m_config; }

    static VoskSetupStatus checkSetupStatus();
    static QString voskInstallDir();

signals:
    void ready();
    void initError(const QString& message);
    void listeningStarted();
    void speechDetected();
    void textRecognized(const QString& text, const QString& language);
    void wakeWordDetected(const QString& word);
    void whisperModeDetected(bool isWhisper);
    void errorOccurred(const QString& message);

    // Прогресс установки Vosk
    void setupRequired();
    void setupProgress(const QString& component, int percent, qint64 bytesTotal);
    void setupComponentReady(const QString& component);
    void setupFinished(bool success, const QString& error);
    void setupLogMessage(const QString& message);

    void requestLoadModels(const QString& pathRu, const QString& pathEn, int threads);
    void requestRecognize(QByteArray pcmData, QString lang);

private slots:
    void onModelsLoaded(bool success, const QString& error);
    void onSpeechEnded(QByteArray pcmData);
    void onRecognized(const QString& text, const QString& lang, bool isWhisper);
    void onRecorderError(const QString& message);
    void onSetupFinished(bool success, const QString& error);

private:
    void startSetup();
    void loadModelsFromDisk();
    static QString resolveModelPath(const QString& subdir);

    WhisperConfig   m_config;
    QThread*        m_thread         = nullptr;
    VoskWorker*     m_worker         = nullptr;
    VoiceRecorder*  m_recorder       = nullptr;
    QThread*        m_setupThread    = nullptr;
    VoskDownloader* m_downloader     = nullptr;
    bool m_initialized  = false;
    bool m_listening    = false;
    bool m_wakeWordMode = true;
};