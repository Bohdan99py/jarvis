#pragma once
// -------------------------------------------------------
// voice_input.h — Голосовой ввод (Vosk + Whisper.cpp)
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QAudioDecoder>
#include <QAudioBuffer>
#include <QByteArray>
#include <QMutex>
#include <atomic>
#include <memory>

#include "jarvis_core_export.h"
#include "memory_limits.h"

class JARVIS_CORE_EXPORT VoiceInput : public QObject
{
    Q_OBJECT

public:
    explicit VoiceInput(QObject* parent = nullptr);
    ~VoiceInput() override;

    // Запустить голосовой ввод
    void start();
    void stop();
    void cancel();

    bool isListening() const { return m_listening.load(); }
    bool isProcessing() const { return m_processing.load(); }

    // VAD (Voice Activity Detection) — где начинается/кончается речь
    void setVadThreshold(float threshold);
    float vadThreshold() const { return m_vadThreshold; }

    // Язык ввода
    void setLanguage(const QString& lang); // "ru", "en"
    QString language() const { return m_language; }

    // Выбрать устройство ввода (микрофон)
    QStringList availableDevices() const;
    void setDevice(const QString& deviceId);

    // Получить текущее уровень громкости
    float currentLevel() const { return m_currentLevel; }

    // Получить распознанный текст с уверенностью
    struct RecognitionResult {
        QString text;
        float confidence = 0.0f;
        QString language;
    };

signals:
    void recordingStarted();
    void recordingFinished(float duration);
    void recognitionStarted();
    void recognitionFinished(const RecognitionResult& result);
    void recognitionError(const QString& error);
    void levelChanged(float level);
    void partialResult(const QString& partial);

private slots:
    void onAudioReady();
    void onDecoderStateChanged(QAudioDecoder::State state);
    void onDecoderError(QAudioDecoder::Error error);
    void onBufferReady();

private:
    void initializeVosk();
    void processAudioChunk(const QByteArray& chunk);
    void detectVoiceActivity();
    void clearAudioBuffer();

    // OPTIMIZED: Phase 1 - Limited audio buffer
    // Max 5MB recording buffer (was unlimited) - saves ~100-200MB
    static constexpr size_t MAX_AUDIO_BUFFER_SIZE = MAX_AUDIO_BUFFER_MB * 1024 * 1024;  // 5MB
    static constexpr size_t AUDIO_CHUNK_SIZE = AUDIO_CHUNK_SIZE_LIMIT;                 // 4KB

    QByteArray m_audioBuffer;
    size_t m_bufferPosition = 0;

    QString m_language = "ru";
    float m_vadThreshold = 0.5f;
    float m_currentLevel = 0.0f;

    std::atomic<bool> m_listening{false};
    std::atomic<bool> m_processing{false};
    std::atomic<bool> m_voiceDetected{false};

    QMutex m_bufferMutex;
    QAudioDecoder* m_decoder = nullptr;
    QString m_deviceId;
};
