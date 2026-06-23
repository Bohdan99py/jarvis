#pragma once
// ============================================================
// voice_input.h — J.A.R.V.I.S. голосовой ввод (Vosk)
//
// Vosk — офлайн ASR, работает без интернета и GPU.
// При первом запуске показывает диалог выбора моделей.
// Поддерживаемые языки и модели:
//   EN small  — ~40 MB   (быстрый старт, хватает для команд)
//   EN large  — ~1.8 GB  (высокое качество, диктовка)
//   RU        — ~1.8 GB  (русский, высокое качество)
//   DE        — ~1.0 GB  (немецкий)
//   FR        — ~1.0 GB  (французский)
//   ZH        — ~0.5 GB  (китайский, малая модель)
//
// Политика записи:
//   - Запись активируется ТОЛЬКО при обнаружении голосовой
//     активности (VAD, порог -45 dB). Тишина не пишется.
//   - Аудио не покидает устройство — всё обрабатывается локально.
//   - Данные не сохраняются на диск (только оперативная память).
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
#include <QVector>
#include <QMap>
#include <atomic>

// ============================================================
//  VoskModelInfo — описание одной модели
// ============================================================

struct VoskModelInfo {
    QString id;           // "en-small", "en-large", "ru", "de", "fr", "zh"
    QString language;     // "en", "ru", "de", "fr", "zh"
    QString displayName;  // "English (Fast)" и т.п.
    QString description;  // описание для UI
    QString url;          // URL для скачивания ZIP
    QString zipPrefix;    // имя верхней папки в ZIP
    QString subdir;       // куда распаковать (относительно installDir)
    QString checkFile;    // файл для проверки установки (relative to subdir)
    qint64  sizeBytes;    // приблизительный размер ZIP
    bool    recommended;  // выделить как рекомендованную

    bool isInstalled(const QString& installDir) const;
    QString fullPath(const QString& installDir) const;
};

// ============================================================
//  Глобальный каталог доступных моделей
// ============================================================

namespace VoskModels {
    const QVector<VoskModelInfo>& catalog();
    VoskModelInfo findById(const QString& id);
    QString formatSize(qint64 bytes);
}

// ============================================================
//  Конфиг голосового ввода
// ============================================================

struct WhisperConfig {
    QString modelPathRu  = QString();
    QString modelPathEn  = QString();
    // Список дополнительных моделей: язык → путь
    QMap<QString, QString> extraModels;

    float   silenceDbThreshold  = -45.0f;
    int     silenceAfterSpeechMs = 800;
    int     maxRecordingMs       = 15000;
    int     minSpeechMs          = 200;
    QString language    = QStringLiteral("auto");
    QStringList wakeWords = { "джарвис", "jarvis", "джарви" };
    int threads = 4;

    // Список ID установленных моделей для загрузки
    QStringList enabledModelIds;
};
using VoiceConfig = WhisperConfig;

// ============================================================
//  VoskSetupStatus
// ============================================================

struct VoskSetupStatus {
    bool dllReady        = false;
    bool modelRuReady    = false;
    bool modelEnReady    = false;
    QStringList installedModelIds;  // все установленные модели

    bool anyModelReady() const { return modelRuReady || modelEnReady || !installedModelIds.isEmpty(); }
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

    // Полная установка: DLL + список моделей по ID
    void setupVosk(const QString& installDir, const QStringList& modelIds = {});

    // Скачать одну модель (для Settings → докачать)
    void downloadModel(const QString& installDir, const QString& modelId);

    // Удалить модель (для Settings → удалить)
    static bool deleteModel(const QString& installDir, const QString& modelId);

    // Проверить статус установки
    static VoskSetupStatus checkStatus(const QString& installDir);

signals:
    void downloadStarted(const QString& component);
    void downloadProgress(const QString& component, int percent, qint64 bytesTotal);
    void extracting(const QString& component);
    void componentReady(const QString& component);
    void setupFinished(bool success, const QString& error);
    void logMessage(const QString& message);

private:
    void ensureDll();
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
    void volumeLevel(float db);       // -100..0 dB, для визуализации VAD
    void speechStarted();
    void speechEnded(QByteArray fullPcmData);
    void error(const QString& message);

private slots:
    void onAudioDataReady();
    void onSilenceTimeout();

private:
    float      computeRmsDb(const QByteArray& data) const;
    QByteArray downsample44to16(const QByteArray& src) const;
    QByteArray downsampleTo16k(const QByteArray& src, int srcRate) const;

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
//  VoskWorker — распознавание в отдельном потоке
// ============================================================

class VoskWorker : public QObject
{
    Q_OBJECT
public:
    explicit VoskWorker(QObject* parent = nullptr);
    ~VoskWorker() override;

public slots:
    // Загрузить модели по путям из конфига
    void loadModels(const WhisperConfig& config);
    // Распознать буфер
    void recognize(QByteArray pcmData, QString preferredLang);
    // Перезагрузить (после установки новой модели)
    void reloadModels(const WhisperConfig& config);

public:
    // Expose loaded models for sharing with PassiveTranscriber.
    // Возвращает VoskModel* (как void*) для уже загруженной модели языка,
    // или nullptr если модель данного языка сейчас не в памяти.
    void* modelForLang(const QString& lang) const;
    // Языки моделей, загруженных в память в данный момент.
    QStringList loadedLanguages() const;

signals:
    void modelsLoaded(bool success, const QString& error);
    void recognized(const QString& text, const QString& detectedLanguage, bool isWhisper);
    void error(const QString& message);

private:
    struct ModelPair {
        void* model     = nullptr;
        void* recognizer = nullptr;
        QString lang;
    };

    // Результат распознавания одной моделью + средняя уверенность по словам.
    // Vosk почти никогда не возвращает пустой text — модель всегда выдаёт
    // "наилучшее" совпадение из своего словаря, даже если реальная речь
    // была на другом языке (например, русское "стим" англ. модель
    // распознает как "steam" с низким, но ненулевым conf). Поэтому
    // выбор по одному наличию текста некорректен — нужно сравнивать
    // именно уверенность между моделями.
    struct RecognitionResult {
        QString text;
        float   avgConfidence = 0.0f; // 0..1, среднее по словам из Vosk "result"
        int     wordCount     = 0;
    };

    void freeAll();
    RecognitionResult tryRecognize(void* recognizer, const QByteArray& pcmData) const;
    bool isWhisperLevel(const QByteArray& pcmData) const;

    // --- Ленивая загрузка / выгрузка тяжёлых моделей ---
    // EN-small (~40 MB) держим в памяти постоянно (нужна для wake word).
    // Тяжёлые модели (en-large, ru, …) грузим по запросу и выгружаем
    // после MODEL_UNLOAD_TIMEOUT_MS простоя, экономя ~3.6 GB RAM.
    bool isSmallModel(const QString& modelId) const;
    bool isModelLoaded(const QString& lang) const;
    void ensureModelForLang(const QString& lang); // догрузить одну модель по языку
    void unloadHeavyModels();          // слот таймера: освободить всё кроме small
    void startUnloadTimer();           // создать/перезапустить таймер (в потоке worker'а)
    bool loadOne(const QString& lang, const QString& path); // загрузить одну модель

    QVector<ModelPair> m_models;
    bool  m_loaded  = false;

    QTimer*       m_unloadTimer    = nullptr;
    WhisperConfig m_lastConfig;        // сохранённый конфиг для ленивой дозагрузки
    bool          m_lazyLoadEnabled = true;
    static constexpr int MODEL_UNLOAD_TIMEOUT_MS = 2 * 60 * 1000; // 2 минуты
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

    // Доступ к worker'у — нужен PassiveTranscriber для шеринга загруженных
    // моделей Vosk (см. modelForLang / loadedLanguages), чтобы не грузить
    // дубликаты на ~3.6 GB. Worker живёт в отдельном потоке: вызывать его
    // методы извне можно только для чтения указателей моделей.
    VoskWorker* worker() const { return m_worker; }

    // --- Управление моделями ---
    // Скачать дополнительную модель (вызывать из Settings)
    void downloadModel(const QString& modelId);
    // Удалить модель
    bool deleteModel(const QString& modelId);
    // Список установленных моделей
    QStringList installedModelIds() const;
    // Перезагрузить Vosk после изменения набора моделей
    void reloadModels();

    // --- Статус установки ---
    static VoskSetupStatus checkSetupStatus();
    static QString voskInstallDir();

    // Был ли показан диалог выбора моделей
    static bool isFirstRun();
    static void markFirstRunComplete();

    // Запустить установку с заданным набором моделей
    // (вызывается из VoskSetupDialog после выбора пользователя)
    void startSetup(const QStringList& modelIds);

signals:
    void ready();
    void initError(const QString& message);
    void listeningStarted();
    void speechDetected();
    void volumeLevel(float db);
    void textRecognized(const QString& text, const QString& language);
    void wakeWordDetected(const QString& word);
    void whisperModeDetected(bool isWhisper);
    void errorOccurred(const QString& message);

    // Установка / обновление моделей
    void setupRequired();                      // нужно показать диалог выбора
    void setupProgress(const QString& component, int percent, qint64 bytesTotal);
    void setupComponentReady(const QString& component);
    void setupFinished(bool success, const QString& error);
    void setupLogMessage(const QString& message);

    // Докачка модели из Settings
    void modelDownloadStarted(const QString& modelId);
    void modelDownloadProgress(const QString& modelId, int percent, qint64 total);
    void modelDownloadFinished(const QString& modelId, bool success);

    // Внутренние сигналы → worker
    void requestLoadModels(WhisperConfig config);
    void requestRecognize(QByteArray pcmData, QString lang);
    void requestReloadModels(WhisperConfig config);

private slots:
    void onModelsLoaded(bool success, const QString& error);
    void onSpeechEnded(QByteArray pcmData);
    void onRecognized(const QString& text, const QString& lang, bool isWhisper);
    void onRecorderError(const QString& message);
    void onSetupFinished(bool success, const QString& error);
    void onModelDownloadFinished(bool success, const QString& error);

private:
    void loadModelsFromDisk();
    void connectDownloader(VoskDownloader* dl);

    WhisperConfig   m_config;
    QThread*        m_thread         = nullptr;
    VoskWorker*     m_worker         = nullptr;
    VoiceRecorder*  m_recorder       = nullptr;
    QThread*        m_setupThread    = nullptr;
    VoskDownloader* m_downloader     = nullptr;
    bool m_initialized  = false;
    bool m_listening    = false;
    bool m_wakeWordMode = true;
    QString m_pendingDownloadModelId; // ID модели в процессе скачивания
};