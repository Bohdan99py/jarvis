// ============================================================
// voice_synthesis_manager.cpp — Thread-Safe TTS Queue Engine
// ============================================================
#include "voice_synthesis_manager.h"
#include "jarvis_paths.h"

#include <QDir>
#include <QFile>
#include <QDebug>
#include <QUuid>
#include <QProcess>
#include <QCoreApplication>
#include <QtConcurrent>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <sapi.h>
#include <mmsystem.h>

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

            if (!m_piperReady.load() || !speakViaPiper(text)) {
                speakViaSapi(text);
            }

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

// Locate piper.exe next to the redistributed runtime DLLs. Tries the
// installed layout (next to the app exe) and the dev-tree layout
// (running from the build dir inside the source checkout), mirroring
// how ocr_extractor.cpp finds tesseract.exe/pdftoppm.exe.
QString VoiceSynthesisManager::findPiperExe() const
{
    const QString appDir = QCoreApplication::applicationDirPath();

    const QStringList candidates = {
        appDir + QStringLiteral("/redist/piper/piper.exe"),
        appDir + QStringLiteral("/../redist/piper/piper.exe"),
        appDir + QStringLiteral("/piper/piper.exe"),
    };

    for (const QString& path : candidates) {
        if (QFile::exists(path))
            return path;
    }
    return QString();
}

void VoiceSynthesisManager::tryLoadPiperModels()
{
    const QString exePath = findPiperExe();
    if (exePath.isEmpty()) {
        qDebug() << "[VoiceSynth] piper.exe not found — using SAPI";
        return;
    }

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

    QString ruModel, enModel;
    for (const QString& name : onnxFiles) {
        if (name.startsWith(QStringLiteral("ru_RU"), Qt::CaseInsensitive) && ruModel.isEmpty())
            ruModel = dir.absoluteFilePath(name);
        else if (name.startsWith(QStringLiteral("en_"), Qt::CaseInsensitive) && enModel.isEmpty())
            enModel = dir.absoluteFilePath(name);
    }

    if (ruModel.isEmpty() && enModel.isEmpty()) {
        qDebug() << "[VoiceSynth] No ru_RU-*/en_*-prefixed voice models found — using SAPI";
        return;
    }

    m_piperExePath     = exePath;
    m_piperModelPathRu = ruModel;
    m_piperModelPathEn = enModel;
    m_piperReady.store(true);

    qDebug() << "[VoiceSynth] Piper ready. RU model:" << ruModel << "EN model:" << enModel;

    QMetaObject::invokeMethod(this, [this, ruModel, enModel]() {
        emit modelLoaded(!ruModel.isEmpty() ? ruModel : enModel);
    }, Qt::QueuedConnection);
}

// ============================================================
//  Piper TTS backend — spawns piper.exe, synthesizes to a temp
//  WAV file, plays it back synchronously via winmm.
// ============================================================

bool VoiceSynthesisManager::speakViaPiper(const QString& text)
{
    // Pick a voice by script: any Cyrillic character routes to the
    // Russian model, otherwise the English model. Falls back to
    // whichever single model is available if only one was found.
    bool hasCyrillic = false;
    for (const QChar& ch : text) {
        if (ch.unicode() >= 0x0400 && ch.unicode() <= 0x04FF) {
            hasCyrillic = true;
            break;
        }
    }

    QString modelPath = hasCyrillic ? m_piperModelPathRu : m_piperModelPathEn;
    if (modelPath.isEmpty())
        modelPath = hasCyrillic ? m_piperModelPathEn : m_piperModelPathRu;

    if (modelPath.isEmpty() || m_piperExePath.isEmpty())
        return false;

    const QString wavPath = QDir::tempPath() + QStringLiteral("/jarvis_tts_")
        + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".wav");

    QProcess proc;
    proc.setProgram(m_piperExePath);
    proc.setArguments({
        QStringLiteral("--model"), modelPath,
        QStringLiteral("--output_file"), wavPath,
    });
    proc.start();
    if (!proc.waitForStarted(3000)) {
        qWarning() << "[VoiceSynth] piper.exe failed to start";
        return false;
    }

    proc.write(text.toUtf8());
    proc.closeWriteChannel();

    if (!proc.waitForFinished(15000) || proc.exitCode() != 0 || !QFile::exists(wavPath)) {
        qWarning() << "[VoiceSynth] piper.exe synthesis failed:" << proc.readAllStandardError();
        QFile::remove(wavPath);
        return false;
    }

    if (m_stopRequested.load()) {
        QFile::remove(wavPath);
        return true;
    }

    const std::wstring wWavPath = wavPath.toStdWString();
    PlaySoundW(wWavPath.c_str(), nullptr, SND_FILENAME | SND_SYNC);

    QFile::remove(wavPath);
    return true;
}
