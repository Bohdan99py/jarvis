#pragma once
// ============================================================
// voice_synthesis_manager.h — Thread-Safe TTS Queue Engine
//
// Manages the TTS pipeline with a sequential playback queue.
// Supports Windows SAPI out-of-the-box, with Piper ONNX model
// loading when models are present in /models/tts/.
//
// Usage:
//   VoiceSynthesisManager::instance().say("Готово, файл обновлён.");
//   VoiceSynthesisManager::instance().stopSpeaking();
// ============================================================

#include <QObject>
#include <QString>
#include <QQueue>
#include <QMutex>
#include <QThread>
#include <atomic>

class VoiceSynthesisManager : public QObject
{
    Q_OBJECT

public:
    static VoiceSynthesisManager& instance();

    VoiceSynthesisManager(const VoiceSynthesisManager&)            = delete;
    VoiceSynthesisManager& operator=(const VoiceSynthesisManager&) = delete;

    void say(const QString& conversationalText);
    void stopSpeaking();

    bool isSpeaking() const { return m_speaking.load(); }
    bool isEnabled()  const { return m_enabled.load(); }
    void setEnabled(bool on) { m_enabled.store(on); }

    void loadModelsAsync();
    bool piperAvailable() const { return m_piperReady.load(); }

signals:
    void speakingChanged(bool speaking);
    void modelLoaded(const QString& modelName);
    void modelLoadFailed(const QString& error);

private:
    explicit VoiceSynthesisManager(QObject* parent = nullptr);
    ~VoiceSynthesisManager() override;

    void processQueue();
    void speakViaSapi(const QString& text);
    void tryLoadPiperModels();

    QQueue<QString>   m_queue;
    QMutex            m_queueMutex;

    QThread*          m_workerThread = nullptr;
    std::atomic<bool> m_speaking{false};
    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_processing{false};
    std::atomic<bool> m_stopRequested{false};

    std::atomic<bool> m_piperReady{false};
    QString           m_piperModelPath;

    static constexpr int TTS_RATE   = 1;
    static constexpr int TTS_VOLUME = 100;
};
