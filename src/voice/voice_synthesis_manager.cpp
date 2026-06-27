// ============================================================
// voice_synthesis_manager.cpp — Thread-Safe TTS Queue Engine
// ============================================================
#include "voice_synthesis_manager.h"
#include "jarvis_paths.h"

#include <QDir>
#include <QDebug>
#include <QtConcurrent>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <sapi.h>

// ============================================================
//  Singleton
// ============================================================

VoiceSynthesisManager& VoiceSynthesisManager::instance()
{
    static VoiceSynthesisManager inst;
    return inst;
}

VoiceSynthesisManager::VoiceSynthesisManager(QObject* parent)
    : QObject(parent)
{
    m_workerThread = new QThread(this);
    m_workerThread->setObjectName(QStringLiteral("VoiceSynthWorker"));
    m_workerThread->start();
}

VoiceSynthesisManager::~VoiceSynthesisManager()
{
    stopSpeaking();
    m_workerThread->quit();
    m_workerThread->wait(3000);
}

// ============================================================
//  Public API
// ============================================================

void VoiceSynthesisManager::say(const QString& conversationalText)
{
    if (!m_enabled.load()) return;
    if (conversationalText.trimmed().isEmpty()) return;

    {
        QMutexLocker lock(&m_queueMutex);
        m_queue.enqueue(conversationalText.trimmed());
    }

    if (!m_processing.load())
        processQueue();
}

void VoiceSynthesisManager::stopSpeaking()
{
    m_stopRequested.store(true);

    {
        QMutexLocker lock(&m_queueMutex);
        m_queue.clear();
    }

    m_speaking.store(false);
    m_processing.store(false);
    m_stopRequested.store(false);

    emit speakingChanged(false);
}

// ============================================================
//  Queue processor
// ============================================================

void VoiceSynthesisManager::processQueue()
{
    QString text;
    {
        QMutexLocker lock(&m_queueMutex);
        if (m_queue.isEmpty()) {
            m_processing.store(false);
            return;
        }
        text = m_queue.dequeue();
    }

    m_processing.store(true);
    m_speaking.store(true);
    emit speakingChanged(true);

    // Run TTS on the dedicated worker thread to avoid blocking the UI
    QMetaObject::invokeMethod(this, [this, text]() {
        (void)QtConcurrent::run([this, text]() {
            if (m_stopRequested.load()) {
                m_processing.store(false);
                m_speaking.store(false);
                QMetaObject::invokeMethod(this, [this]() {
                    emit speakingChanged(false);
                }, Qt::QueuedConnection);
                return;
            }

            speakViaSapi(text);

            // After this utterance finishes, process the next item
            QMetaObject::invokeMethod(this, [this]() {
                QMutexLocker lock(&m_queueMutex);
                if (m_queue.isEmpty()) {
                    m_processing.store(false);
                    m_speaking.store(false);
                    emit speakingChanged(false);
                } else {
                    lock.unlock();
                    processQueue();
                }
            }, Qt::QueuedConnection);
        });
    }, Qt::QueuedConnection);
}

// ============================================================
//  SAPI TTS backend
// ============================================================

void VoiceSynthesisManager::speakViaSapi(const QString& text)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comOwned = SUCCEEDED(hr);

    ISpVoice* voice = nullptr;
    hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                          IID_ISpVoice, reinterpret_cast<void**>(&voice));

    if (SUCCEEDED(hr) && voice) {
        voice->SetRate(TTS_RATE);
        voice->SetVolume(TTS_VOLUME);

        std::wstring wtext = text.toStdWString();
        voice->Speak(wtext.c_str(), SPF_DEFAULT, nullptr);
        voice->Release();
    } else {
        qWarning() << "[VoiceSynth] SAPI initialization failed";
    }

    if (comOwned)
        CoUninitialize();
}

// ============================================================
//  Piper ONNX model loader (async, non-blocking)
// ============================================================

void VoiceSynthesisManager::loadModelsAsync()
{
    (void)QtConcurrent::run([this]() {
        tryLoadPiperModels();
    });
}

void VoiceSynthesisManager::tryLoadPiperModels()
{
    const QString modelsDir = JarvisPaths::subPath(QStringLiteral("models/tts"));
    QDir dir(modelsDir);

    if (!dir.exists()) {
        qDebug() << "[VoiceSynth] No models/tts directory — using SAPI";
        return;
    }

    const QStringList onnxFiles = dir.entryList(
        {QStringLiteral("*.onnx")}, QDir::Files);

    if (onnxFiles.isEmpty()) {
        qDebug() << "[VoiceSynth] No .onnx voice models found — using SAPI";
        return;
    }

    // Store path for future Piper integration
    m_piperModelPath = dir.absoluteFilePath(onnxFiles.first());
    m_piperReady.store(true);

    qDebug() << "[VoiceSynth] Piper model discovered:" << m_piperModelPath;

    QMetaObject::invokeMethod(this, [this, name = onnxFiles.first()]() {
        emit modelLoaded(name);
    }, Qt::QueuedConnection);
}
