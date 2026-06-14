#pragma once
// ============================================================
// voice_input.h — J.A.R.V.I.S. голосовой ввод
//
// Whisper.cpp нативная интеграция:
//   - Модель medium (~1.5GB VRAM / CPU) — лучшее качество шёпота
//   - VAD (Voice Activity Detection) на основе RMS энергии
//   - Автоопределение языка RU/EN без переключения
//   - Порог шёпота: -45 dB (стандартный голос ~-20 dB)
//   - Wake word детектор (Джарвис / Jarvis)
//
// Зависимость: whisper.cpp (добавить в redist/whisper/)
//   https://github.com/ggerganov/whisper.cpp
//   Модель: redist/whisper/ggml-medium.bin
// ============================================================

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QAudioSource>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QByteArray>
#include <QString>
#include <atomic>
#include <vector>

// Forward-declare whisper context только если whisper доступен
#ifndef JARVIS_WHISPER_STUB
struct whisper_context;
#endif

// ============================================================
//  Настройки распознавания
// ============================================================

struct WhisperConfig {
    // Путь к модели (ggml-medium.bin для лучшего распознавания шёпота)
    QString modelPath     = QStringLiteral("redist/whisper/ggml-medium.bin");

    // VAD — порог энергии звука (RMS)
    // -50 dB ≈ тихий шёпот, -35 dB ≈ нормальный шёпот, -20 dB ≈ голос
    float   silenceDbThreshold = -45.0f;   // всё тише этого = тишина
    float   whisperDbThreshold = -38.0f;   // зона шёпота (доп. усиление)

    // Длительность тишины после которой считаем речь законченной (мс)
    int     silenceAfterSpeechMs = 800;

    // Максимальная длина записи (мс) — защита от бесконечной записи
    int     maxRecordingMs       = 15000;

    // Минимальная длина фразы для отправки на распознавание (мс)
    int     minSpeechMs          = 200;

    // Язык: "auto" = определяется автоматически, "ru", "en"
    QString language             = QStringLiteral("auto");

    // Wake words (любое из списка активирует JARVIS)
    QStringList wakeWords        = { "джарвис", "jarvis", "джарви" };

    // Использовать GPU если доступен
    bool    useGpu               = false;   // false = безопаснее для совместимости

    // Количество потоков CPU для Whisper
    int     threads              = 4;
};

// ============================================================
//  VoiceRecorder — захват аудио (живёт в потоке UI)
// ============================================================

class VoiceRecorder : public QObject
{
    Q_OBJECT
public:
    explicit VoiceRecorder(QObject* parent = nullptr);
    ~VoiceRecorder() override;

    bool    start(const WhisperConfig& config);
    void    stop();
    bool    isRecording() const { return m_recording.load(); }

signals:
    // Новая порция аудио-данных (PCM float32, 16kHz, mono)
    void audioChunkReady(QByteArray pcmData);
    // Пользователь начал говорить
    void speechStarted();
    // Пользователь замолчал — готово к распознаванию
    void speechEnded(QByteArray fullPcmData);
    void error(const QString& message);

private slots:
    void onAudioDataReady();
    void onSilenceTimeout();

private:
    float   computeRmsDb(const QByteArray& data) const;
    QByteArray convertToFloat32_16kHz(const QByteArray& int16_44khz) const;

    QAudioSource*    m_audioSource    = nullptr;
    QIODevice*       m_audioDevice    = nullptr;
    QTimer*          m_silenceTimer   = nullptr;

    QByteArray       m_currentBuffer;   // накопленный PCM текущей фразы
    bool             m_speaking        = false;
    std::atomic<bool> m_recording      {false};

    WhisperConfig    m_config;
    QAudioFormat     m_format;
};

// ============================================================
//  WhisperWorker — распознавание (отдельный поток)
// ============================================================

class WhisperWorker : public QObject
{
    Q_OBJECT
public:
    explicit WhisperWorker(QObject* parent = nullptr);
    ~WhisperWorker() override;

public slots:
    void loadModel(const QString& modelPath, int threads, bool useGpu);
    void transcribe(QByteArray pcmData, QString language);

signals:
    void modelLoaded(bool success, const QString& error);
    // Результат распознавания
    void transcribed(const QString& text, const QString& detectedLanguage,
                     float confidence, bool isWhisper);
    void error(const QString& message);

private:
    bool isWakeWord(const QString& text, const QStringList& wakeWords) const;

#ifndef JARVIS_WHISPER_STUB
    whisper_context* m_ctx     = nullptr;
#else
    void*            m_ctx     = nullptr;
#endif
    bool             m_loaded  = false;
};

// ============================================================
//  VoiceInput — главный контроллер (живёт в UI потоке)
// ============================================================

class VoiceInput : public QObject
{
    Q_OBJECT
public:
    explicit VoiceInput(QObject* parent = nullptr);
    ~VoiceInput() override;

    // Инициализация — вызвать один раз при старте
    void initialize(const WhisperConfig& config = WhisperConfig{});

    // Запуск / остановка прослушивания
    void startListening();
    void stopListening();
    bool isListening() const;

    // Установить конфигурацию (можно менять на лету)
    void setConfig(const WhisperConfig& config);
    const WhisperConfig& config() const { return m_config; }

    // Проверить наличие модели
    static bool isModelAvailable(const QString& modelPath
                                 = QStringLiteral("redist/whisper/ggml-medium.bin"));
    static QString defaultModelPath();

    // Автоматически скачать модель если её нет
    void downloadModelIfNeeded();

signals:
    // Модель загружена и готова
    void ready();
    // Ошибка инициализации
    void initError(const QString& message);

    // JARVIS начал слушать (сработал wake word или нажата кнопка)
    void listeningStarted();
    // Пользователь начал говорить
    void speechDetected();

    // Финальный результат — текст для обработки Brain'ом
    void textRecognized(const QString& text, const QString& language);

    // Wake word обнаружен
    void wakeWordDetected(const QString& word);

    // Режим шёпота (для UI — показать иконку 🤫)
    void whisperModeDetected(bool isWhisper);

    // Автоскачивание модели
    void modelDownloadProgress(int percent);
    void modelDownloadFinished(bool success, const QString& error);

    void errorOccurred(const QString& message);

    // внутренние сигналы для передачи в потоки
    void requestLoadModel(const QString& path, int threads, bool gpu);
    void requestTranscribe(QByteArray pcmData, QString language);

private slots:
    void onModelLoaded(bool success, const QString& error);
    void onSpeechEnded(QByteArray pcmData);
    void onTranscribed(const QString& text, const QString& lang,
                       float confidence, bool isWhisper);
    void onRecorderError(const QString& message);

private:
    WhisperConfig  m_config;

    QThread*       m_whisperThread = nullptr;
    WhisperWorker* m_worker        = nullptr;

    VoiceRecorder* m_recorder      = nullptr;

    bool           m_initialized   = false;
    bool           m_listening     = false;
    bool           m_wakeWordMode  = true;
    bool           m_downloading   = false;

    // Резолвит абсолютный путь к модели
    // Ищет рядом с exe, потом в AppData, потом в redist/whisper/
    static QString resolveModelPath(const QString& hint = QString());
};
