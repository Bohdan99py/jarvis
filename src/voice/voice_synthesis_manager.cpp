// ============================================================
// voice_synthesis_manager.cpp — Thread-Safe TTS Queue Engine
// ============================================================
#include "voice_synthesis_manager.h"
#include "jarvis_paths.h"

#include <QDir>
#include <QFile>
#include <QDebug>
#include <QUuid>
#include <QUrl>
#include <QTimer>
#include <QEventLoop>
#include <QDirIterator>
#include <QProcess>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QtConcurrent>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <sapi.h>
#include <mmsystem.h>

// ============================================================
//  Default Piper assets (offline neural TTS) — self-provisioned
//  into Documents/Jarvis Data on first run if not already bundled.
// ============================================================

namespace {

const QString& piperRuntimeUrl()
{
    static const QString url = QStringLiteral(
        "https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_windows_amd64.zip");
    return url;
}

// Blocking HTTP GET → file, meant to run on a background thread.
// Bounded by a hard timeout so a stalled connection can't hang the
// worker thread forever (falls back to SAPI if it never completes).
bool downloadToFile(const QString& url, const QString& destPath)
{
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                      QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = nam.get(req);

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeoutTimer.start(120000);
    loop.exec();

    if (!reply->isFinished()) {
        qWarning() << "[VoiceSynth] download timed out:" << url;
        reply->abort();
        reply->deleteLater();
        return false;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[VoiceSynth] download failed:" << url << reply->errorString();
        reply->deleteLater();
        return false;
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    QFile out(destPath);
    if (!out.open(QIODevice::WriteOnly)) {
        qWarning() << "[VoiceSynth] cannot write:" << destPath;
        return false;
    }
    out.write(data);
    out.close();
    return true;
}

} // namespace

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

    // Self-provisioned copy from a previous run (downloaded into user
    // data because no bundled redist/piper/ shipped with this build).
    const QString runtimeDir = JarvisPaths::subPath(QStringLiteral("piper_runtime"));
    QDirIterator it(runtimeDir, {QStringLiteral("piper.exe")}, QDir::Files,
                     QDirIterator::Subdirectories);
    if (it.hasNext())
        return it.next();

    return QString();
}

// Downloads and extracts the Piper Windows runtime (piper.exe + DLLs)
// into Documents/Jarvis Data/piper_runtime. Used when no bundled
// redist/piper/ was shipped with this build (e.g. a pre-built exe
// handed to someone who never ran CMake).
bool VoiceSynthesisManager::provisionPiperRuntime()
{
    const QString runtimeDir = JarvisPaths::subPath(QStringLiteral("piper_runtime"));
    QDir().mkpath(runtimeDir);

    const QString zipPath = runtimeDir + QStringLiteral("/piper_windows_amd64.zip");

    qDebug() << "[VoiceSynth] Downloading Piper runtime...";
    if (!downloadToFile(piperRuntimeUrl(), zipPath))
        return false;

    // Windows 10 1803+ ships tar.exe (bsdtar), which understands .zip —
    // avoids pulling in a zip-extraction library just for this.
    QProcess tar;
    tar.setProgram(QStringLiteral("tar"));
    tar.setArguments({QStringLiteral("-xf"), zipPath, QStringLiteral("-C"), runtimeDir});
    tar.start();
    const bool extracted = tar.waitForFinished(60000) && tar.exitCode() == 0;

    QFile::remove(zipPath);

    if (!extracted) {
        qWarning() << "[VoiceSynth] Failed to extract Piper runtime:" << tar.readAllStandardError();
        return false;
    }

    qDebug() << "[VoiceSynth] Piper runtime provisioned to" << runtimeDir;
    return true;
}

// Downloads one voice's .onnx + .onnx.json into modelsDir if either
// is missing. No-op (returns true) if both are already present.
bool VoiceSynthesisManager::provisionVoice(const QString& onnxUrl, const QString& jsonUrl,
                                            const QString& onnxName, const QString& jsonName,
                                            const QString& modelsDir)
{
    const QString onnxPath = modelsDir + QStringLiteral("/") + onnxName;
    const QString jsonPath = modelsDir + QStringLiteral("/") + jsonName;

    if (QFile::exists(onnxPath) && QFile::exists(jsonPath))
        return true;

    qDebug() << "[VoiceSynth] Downloading voice:" << onnxName;
    const bool ok = downloadToFile(onnxUrl, onnxPath) && downloadToFile(jsonUrl, jsonPath);
    if (!ok) {
        QFile::remove(onnxPath);
        QFile::remove(jsonPath);
    }
    return ok;
}

void VoiceSynthesisManager::tryLoadPiperModels()
{
    QString exePath = findPiperExe();
    if (exePath.isEmpty()) {
        qDebug() << "[VoiceSynth] piper.exe not found — provisioning...";
        if (provisionPiperRuntime())
            exePath = findPiperExe();
    }

    if (exePath.isEmpty()) {
        qDebug() << "[VoiceSynth] Piper runtime unavailable — using SAPI";
        return;
    }

    const QString modelsDir = JarvisPaths::subPath(QStringLiteral("models/tts"));
    QDir dir(modelsDir);

    // Default voices, downloaded on demand if not already present.
    provisionVoice(
        QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/ru/ru_RU/denis/medium/ru_RU-denis-medium.onnx"),
        QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/ru/ru_RU/denis/medium/ru_RU-denis-medium.onnx.json"),
        QStringLiteral("ru_RU-denis-medium.onnx"),
        QStringLiteral("ru_RU-denis-medium.onnx.json"),
        modelsDir);

    provisionVoice(
        QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/ryan/medium/en_US-ryan-medium.onnx"),
        QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/ryan/medium/en_US-ryan-medium.onnx.json"),
        QStringLiteral("en_US-ryan-medium.onnx"),
        QStringLiteral("en_US-ryan-medium.onnx.json"),
        modelsDir);

    const QStringList onnxFiles = dir.entryList(
        {QStringLiteral("*.onnx")}, QDir::Files);

    if (onnxFiles.isEmpty()) {
        qDebug() << "[VoiceSynth] No .onnx voice models available — using SAPI";
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
