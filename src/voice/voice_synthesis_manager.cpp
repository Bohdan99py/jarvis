// ============================================================
// voice_synthesis_manager.cpp — Thread-Safe TTS Priority Queue
// ============================================================
#include "voice_synthesis_manager.h"
#include "speech_aggregator.h"
#include "elevenlabs_provider.h"
#include "piper_provider.h"
#include "speech_cache.h"
#include "speech_phrases.h"
#include "jarvis_paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QUuid>
#include <QUrl>
#include <QTimer>
#include <QEventLoop>
#include <QDirIterator>
#include <QProcess>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QtConcurrent>
#include <QtEndian>

#include <cstring>

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

// ------------------------------------------------------------
//  Минимальный разбор RIFF/WAVE.
//
//  Нужен, чтобы отдать PCM в waveOut вручную: PlaySound умеет только
//  «играть до конца» либо «оборвать всё разом», а нам нужен звук,
//  который можно прекратить в середине фразы и при этом знать, когда
//  он закончился сам.
// ------------------------------------------------------------

struct ParsedWav {
    WAVEFORMATEX fmt{};
    QByteArray   pcm;
};

bool parseWav(const QByteArray& raw, ParsedWav& out)
{
    if (raw.size() < 44 || !raw.startsWith("RIFF") || raw.mid(8, 4) != "WAVE")
        return false;

    bool haveFmt = false;
    int  pos     = 12;

    while (pos + 8 <= raw.size()) {
        const QByteArray id = raw.mid(pos, 4);
        const quint32 size  = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(raw.constData() + pos + 4));
        const int body = pos + 8;

        // Битый или обрезанный заголовок: дальше идти небезопасно.
        if (size == 0 || size > quint32(raw.size() - body))
            break;

        if (id == "fmt " && size >= 16) {
            std::memcpy(&out.fmt, raw.constData() + body, 16);
            out.fmt.cbSize = 0;
            haveFmt = true;
        } else if (id == "data") {
            out.pcm = raw.mid(body, int(size));
        }

        pos = body + int(size) + int(size & 1);
    }

    return haveFmt && !out.pcm.isEmpty()
        && out.fmt.wFormatTag == WAVE_FORMAT_PCM;
}

// Громкость применяется к сэмплам: у waveOut есть свой регулятор, но
// он общий на устройство — сдвинув его, JARVIS приглушил бы заодно и
// музыку пользователя.
void applyGain(QByteArray& pcm, int bitsPerSample, int volumePercent)
{
    if (volumePercent >= 100 || bitsPerSample != 16)
        return;

    const double gain = qBound(0, volumePercent, 100) / 100.0;
    qint16* samples   = reinterpret_cast<qint16*>(pcm.data());
    const int count   = pcm.size() / 2;

    for (int i = 0; i < count; ++i)
        samples[i] = static_cast<qint16>(samples[i] * gain);
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
    // say() зовут и из сетевых потоков: без регистрации типов сигнал
    // speechSuppressed не доедет до UI через очередь соединений.
    qRegisterMetaType<SpeechRequest>("SpeechRequest");
    qRegisterMetaType<VoiceDecision>("VoiceDecision");
    qRegisterMetaType<CancelReason>("CancelReason");

    m_clock.start();

    // Провайдеры создаются сразу, доступными становятся по-разному:
    // ElevenLabs — как только появится ключ, Piper — когда доедет
    // runtime (см. tryLoadPiperModels).
    m_eleven = std::make_unique<ElevenLabsProvider>();
    m_piper  = std::make_unique<PiperProvider>();

    // Живёт в потоке менеджера: его таблица всплесков и таймеры должны
    // крутиться там же, куда say() перекидывает работу.
    m_aggregator = new SpeechAggregator(this);
    connect(m_aggregator, &SpeechAggregator::ready, this,
            [this](const SpeechRequest& req, int absorbed) {
        if (absorbed > 1) {
            qDebug() << "[VoiceSynth] merged" << absorbed << "events into one line";
        }
        enqueue(req);
    });

    m_workerThread = new QThread(this);
    m_workerThread->setObjectName(QStringLiteral("VoiceSynthWorker"));
    m_workerThread->start();
}

VoiceSynthesisManager::~VoiceSynthesisManager()
{
    cancelCurrentSpeech(CancelReason::Shutdown);
    m_workerThread->quit();
    m_workerThread->wait(3000);
}

// ============================================================
//  Public API
// ============================================================

// Готовит текст к произношению: разметка, код, ссылки и пути читаются
// вслух как мусор, поэтому снимаются здесь — на единственном входе в
// очередь, а не в каждом вызывающем месте по отдельности.
QString VoiceSynthesisManager::sanitizeForSpeech(const QString& text)
{
    QString result = text;

    // Блоки кода ```...``` и инлайн-код `...` целиком.
    static const QRegularExpression fencedCode(
        QStringLiteral("```.*?```"), QRegularExpression::DotMatchesEverythingOption);
    result.remove(fencedCode);
    result.remove(QRegularExpression(QStringLiteral("`[^`]*`")));

    // Ссылки и windows-пути.
    result.remove(QRegularExpression(QStringLiteral("https?://\\S+")));
    result.remove(QRegularExpression(QStringLiteral("[A-Za-z]:\\\\\\S+")));

    // Markdown-оформление: сам текст оставляем, снимаем только маркеры.
    result.remove(QRegularExpression(QStringLiteral("^#{1,6}\\s+"),
                                      QRegularExpression::MultilineOption));
    result.remove(QRegularExpression(QStringLiteral("[*_]{1,3}(?=\\S)")));
    result.remove(QRegularExpression(QStringLiteral("(?<=\\S)[*_]{1,3}")));

    // HTML-хвосты и буллиты.
    result.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    result.remove(QRegularExpression(QStringLiteral("&[a-zA-Z]+;")));
    static const QString kBullets = QStringLiteral("•→►■●·—–");
    for (const QChar& c : kBullets)
        result.replace(c, QLatin1Char(' '));

    return result.simplified().trimmed();
}

void VoiceSynthesisManager::say(const QString& conversationalText)
{
    say(SpeechRequest::assistant(conversationalText));
}

void VoiceSynthesisManager::say(const SpeechRequest& request)
{
    if (!m_enabled.load()) return;

    SpeechRequest req = request;
    req.text = sanitizeForSpeech(req.text);
    if (req.text.length() < 3) return;

    // Единственная развилка «говорить или нет» — здесь, на входе в
    // очередь. Раскидывать её по вызывающим значит гарантировать, что
    // однажды кто-то заговорит посреди игры на весь экран.
    VoicePolicyManager& policy = VoicePolicyManager::instance();
    const VoiceDecision decision = policy.decide(req);
    if (decision != VoiceDecision::Speak) {
        qDebug() << "[VoiceSynth] policy" << (decision == VoiceDecision::Notify
                                                  ? "notify" : "silent")
                 << "—" << req.text.left(60);
        emit speechSuppressed(req, decision);
        return;
    }

    // Ночь делает реплику тише и медленнее, а не отменяет её.
    policy.applyContextStyle(req);

    // Всплеск событий об одном и том же превращается в одну фразу.
    // Склейка идёт ПОСЛЕ политики: придерживать полторы секунды то,
    // что всё равно не будет произнесено, бессмысленно.
    if (SpeechAggregator::shouldAggregate(req)) {
        m_aggregator->absorb(req);
        return;
    }

    enqueue(req);
}

void VoiceSynthesisManager::enqueue(const SpeechRequest& req)
{
    {
        QMutexLocker lock(&m_queueMutex);

        // Заикание выглядит так: следом за фразой приходит она же или её
        // начало (краткая сводка, а за ней первое предложение того же
        // ответа). Порог по длине обязателен: короткие подтверждения
        // («Готово», «Ок») законно повторяются и законно начинают собой
        // более длинную реплику — их глушить нельзя.
        if (!m_lastSpoken.isEmpty()
            && m_lastSpokenTimer.isValid()
            && m_lastSpokenTimer.elapsed() < DEDUP_WINDOW_MS)
        {
            const QString& prev = m_lastSpoken;
            const int shorter = qMin(prev.length(), req.text.length());
            if (shorter >= MIN_DEDUP_CHARS
                && (prev.startsWith(req.text, Qt::CaseInsensitive)
                    || req.text.startsWith(prev, Qt::CaseInsensitive)))
            {
                qDebug() << "[VoiceSynth] suppressed repeat:" << req.text.left(60);
                return;
            }
        }

        m_lastSpoken = req.text;
        m_lastSpokenTimer.start();

        // Вставка по важности: место — перед первой записью с меньшим
        // приоритетом. Внутри одного приоритета порядок остаётся
        // прежним, иначе очередь событий перемешивалась бы.
        int pos = m_queue.size();
        while (pos > 0 && m_queue[pos - 1].request.priority < req.priority)
            --pos;
        m_queue.insert(pos, QueuedSpeech{req, m_clock.elapsed()});
    }

    // Вытеснение: «Температура 98 градусов» не ждёт, пока договорит
    // напоминание про перерыв. Очередь при этом сохраняется — прерванную
    // реплику никто не отменял, она просто пропускает вперёд.
    if (m_speaking.load()
        && m_currentInterruptible.load()
        && static_cast<int>(req.priority) > m_currentPriority.load())
    {
        qDebug() << "[VoiceSynth] preempted by higher priority:" << req.text.left(60);
        cancelCurrentSpeech(CancelReason::Preempted);
        return;   // processQueue() поднимет очередь из finishUtterance()
    }

    if (!m_processing.load())
        processQueue();
}

void VoiceSynthesisManager::cancelCurrentSpeech(CancelReason reason)
{
    // Поколение растёт всегда: именно по нему синтезирующий поток
    // понимает, что его реплика больше не нужна, и обрывает звук.
    m_epoch.fetch_add(1);

    // Вытеснение — единственная причина, при которой очередь остаётся:
    // прервана одна фраза, а не поток речи целиком.
    if (reason != CancelReason::Preempted) {
        // Придержанный всплеск — это тоже речь, просто ещё не начатая.
        if (m_aggregator)
            m_aggregator->discardPending();

        QMutexLocker lock(&m_queueMutex);
        m_queue.clear();
        // Явная остановка снимает и защиту от повтора: пользователь,
        // прервавший фразу, вправе услышать её заново.
        m_lastSpoken.clear();
        m_lastSpokenTimer.invalidate();
    }

    emit speechCancelled(reason);
}

void VoiceSynthesisManager::setEnabled(bool on)
{
    m_enabled.store(on);
    if (!on)
        cancelCurrentSpeech(CancelReason::PolicyChanged);
}

qint64 VoiceSynthesisManager::currentSpeechElapsedMs() const
{
    if (!m_speaking.load())
        return 0;
    return m_clock.elapsed() - m_currentStartedAtMs.load();
}

// ============================================================
//  Queue processor
// ============================================================

bool VoiceSynthesisManager::dequeueNext(SpeechRequest& out)
{
    QMutexLocker lock(&m_queueMutex);

    while (!m_queue.isEmpty()) {
        const QueuedSpeech item = m_queue.takeFirst();

        // Фоновая реплика, дождавшаяся своей очереди слишком поздно, не
        // произносится: «пора сделать перерыв» через десять минут после
        // события — это уже не напоминание, а помеха.
        if (item.request.expiresAfterMs > 0
            && m_clock.elapsed() - item.enqueuedAtMs > item.request.expiresAfterMs)
        {
            qDebug() << "[VoiceSynth] dropped stale:" << item.request.text.left(60);
            continue;
        }

        out = item.request;
        return true;
    }

    return false;
}

void VoiceSynthesisManager::processQueue()
{
    // Все переходы состояния — в потоке менеджера: say() зовут и из
    // сетевых потоков (Telegram-шлюз), а speakingChanged слушает UI.
    QMetaObject::invokeMethod(this, [this]() {
        if (m_processing.load())
            return;   // воркер уже в работе, очередь поднимет finishUtterance()

        SpeechRequest req;
        if (!dequeueNext(req)) {
            m_processing.store(false);
            return;
        }

        m_processing.store(true);
        m_speaking.store(true);
        m_currentPriority.store(static_cast<int>(req.priority));
        m_currentInterruptible.store(req.interruptible);
        m_currentStartedAtMs.store(m_clock.elapsed());
        emit speakingChanged(true);

        const quint64 epoch = m_epoch.load();

        // Синтез и воспроизведение — на пуле, UI при этом не стоит.
        (void)QtConcurrent::run([this, req, epoch]() {
            if (!cancelled(epoch)) {
                // SAPI — последнее, что остаётся, когда не смог никто:
                // он всегда на месте, но не кэшируется и звучит хуже.
                if (!speakViaProviders(req, epoch))
                    speakViaSapi(req, epoch);
            }

            QMetaObject::invokeMethod(this, [this]() {
                finishUtterance();
            }, Qt::QueuedConnection);
        });
    }, Qt::QueuedConnection);
}

void VoiceSynthesisManager::finishUtterance()
{
    m_processing.store(false);

    bool more = false;
    {
        QMutexLocker lock(&m_queueMutex);
        more = !m_queue.isEmpty();
    }

    // Между репликами одной серии «замолчал» не объявляем: слушатели
    // speakingChanged (индикатор, ядро) моргали бы на каждой фразе.
    if (more) {
        processQueue();
        return;
    }

    m_speaking.store(false);
    emit speakingChanged(false);
}

// ============================================================
//  SAPI TTS backend
// ============================================================

void VoiceSynthesisManager::speakViaSapi(const SpeechRequest& req, quint64 epoch)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comOwned = SUCCEEDED(hr);

    ISpVoice* voice = nullptr;
    hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                          IID_ISpVoice, reinterpret_cast<void**>(&voice));

    if (SUCCEEDED(hr) && voice) {
        const StyleParams sp = styleParams(req.style);
        voice->SetRate(sapiRateForStyle(req.style));
        voice->SetVolume(static_cast<USHORT>(qBound(0, sp.volumePercent, TTS_VOLUME)));

        std::wstring wtext = req.text.toStdWString();

        // Асинхронно + опрос: синхронный Speak() дочитал бы фразу до
        // конца, что бы ни случилось, — именно из-за этого «стоп»
        // раньше срабатывал только на следующей реплике.
        // Отсчёт «сколько уже звучит» — от начала звука, не от постановки
        // в очередь: между ними помещается синтез, и окно защиты от эха
        // иначе истекало бы ещё до первого слова.
        m_currentStartedAtMs.store(m_clock.elapsed());

        if (SUCCEEDED(voice->Speak(wtext.c_str(), SPF_ASYNC, nullptr))) {
            while (voice->WaitUntilDone(CANCEL_POLL_MS) == S_FALSE) {
                if (cancelled(epoch)) {
                    voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
                    break;
                }
            }
        }

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

    // subPath("models/tts") создаёт только "models" — саму папку нужно
    // создать отдельно, иначе на чистой машине загрузка голосов молча
    // проваливается на первой же записи и JARVIS навсегда остаётся на SAPI.
    const QString modelsDir = JarvisPaths::subPath(QStringLiteral("models/tts"));
    QDir().mkpath(modelsDir);
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
    m_piper->setRuntime(exePath, ruModel, enModel);
    m_piperReady.store(true);

    qDebug() << "[VoiceSynth] Piper ready. RU model:" << ruModel << "EN model:" << enModel;

    QMetaObject::invokeMethod(this, [this, ruModel, enModel]() {
        emit modelLoaded(!ruModel.isEmpty() ? ruModel : enModel);
    }, Qt::QueuedConnection);

    // Голос известен — можно готовить дежурные фразы. Раньше было
    // нечем: ключ кэша считается в том числе по модели.
    warmupCacheAsync();
}

// ============================================================
//  Provider chain — сеть, затем локальный синтез, затем SAPI
// ============================================================

QString VoiceSynthesisManager::activeProviderName() const
{
    for (VoiceProvider* provider : orderedProviders()) {
        if (provider && provider->isAvailable())
            return provider->displayName();
    }
    return QStringLiteral("SAPI");
}

QVector<VoiceProvider*> VoiceSynthesisManager::orderedProviders() const
{
    // Сетевой первым: он звучит лучше, а повторяющиеся реплики берутся
    // из кэша и ничего не стоят. Локальный — то, что остаётся, когда
    // сети нет; SAPI ниже их обоих и живёт вне этого списка.
    return { static_cast<VoiceProvider*>(m_eleven.get()),
             static_cast<VoiceProvider*>(m_piper.get()) };
}

// Громкость в ключ не входит: она применяется при воспроизведении, к
// сэмплам уже готового файла (см. applyGain), и одна и та же запись
// годится и для обычного режима, и для тихого.
QString VoiceSynthesisManager::cacheKeyFor(const SpeechRequest& req,
                                           VoiceProvider* provider) const
{
    if (!req.cacheable || !provider)
        return QString();

    return SpeechCache::makeKey(provider->id(),
                                 provider->voiceId(req),
                                 req.language,
                                 styleParams(req.style),
                                 req.text);
}

bool VoiceSynthesisManager::speakViaProviders(const SpeechRequest& req, quint64 epoch)
{
    const StyleParams sp = styleParams(req.style);

    for (VoiceProvider* provider : orderedProviders()) {
        if (!provider || !provider->isAvailable())
            continue;

        const QString cacheKey = cacheKeyFor(req, provider);

        // Кэш спрашивается у КАЖДОГО провайдера до синтеза. Поэтому
        // заранее прогретая фраза звучит хорошим голосом и тогда, когда
        // сеть лежит: до сетевого запроса дело просто не доходит.
        QString wavPath = SpeechCache::instance().lookup(cacheKey);
        bool inCache    = !wavPath.isEmpty();

        if (!inCache) {
            if (cancelled(epoch))
                return true;   // отменили, пока ждали очереди — синтез не нужен

            const QString tempPath = QDir::tempPath() + QStringLiteral("/jarvis_tts_")
                + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".wav");

            if (!provider->synthesize(req, tempPath)) {
                QFile::remove(tempPath);
                continue;      // не смог — следующий провайдер
            }

            wavPath = tempPath;

            if (!cacheKey.isEmpty()) {
                const QString stored = SpeechCache::instance().store(cacheKey, tempPath);
                if (!stored.isEmpty()) {
                    wavPath = stored;
                    inCache = true;
                }
            }
        }

        if (cancelled(epoch)) {
            if (!inCache)
                QFile::remove(wavPath);
            return true;
        }

        const bool played = playWavInterruptible(wavPath, sp.volumePercent, epoch);

        // Кэшированный файл переживает реплику — в этом и смысл.
        if (!inCache)
            QFile::remove(wavPath);

        // Синтез удался: даже если не удалось воспроизвести, перебирать
        // остальных незачем — сломалось устройство, а не провайдер.
        return played;
    }

    return false;
}

// ============================================================
//  Cache warm-up — дежурные фразы синтезируются заранее
// ============================================================

// Прогревается ТОЛЬКО локальный провайдер. Прогрев сетевого — это
// расход символов на чужом счёте: двадцать восемь фраз стоят немного,
// но тратить чужие деньги без спроса нельзя. Для ElevenLabs прогрев
// уместен как явное действие в настройках голоса.
void VoiceSynthesisManager::warmupCacheAsync()
{
    if (!m_piperReady.load())
        return;

    (void)QtConcurrent::run([this]() {
        const QVector<SpeechRequest> phrases = SpeechPhrases::warmupSet();
        int made = 0;

        for (const SpeechRequest& phrase : phrases) {
            SpeechRequest req = phrase;
            // Через ту же очистку, что и обычная речь: иначе прогретый
            // ключ не совпадёт с тем, который посчитают при произнесении.
            req.text = sanitizeForSpeech(req.text);
            if (req.text.length() < 3)
                continue;

            const QString key = cacheKeyFor(req, m_piper.get());
            if (key.isEmpty() || SpeechCache::instance().contains(key))
                continue;

            const QString tempPath = QDir::tempPath() + QStringLiteral("/jarvis_warm_")
                + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".wav");

            if (!m_piper->synthesize(req, tempPath))
                continue;

            if (SpeechCache::instance().store(key, tempPath).isEmpty())
                QFile::remove(tempPath);
            else
                ++made;

            // Прогрев — работа фоновая и не срочная: живой реплике
            // незачем стоять в очереди за процессом piper.exe.
            QThread::msleep(100);
        }

        if (made > 0)
            qDebug() << "[VoiceSynth] warmed up" << made << "phrases";
    });
}

// Воспроизведение с опросом отмены. Возвращает false, только если
// звук не удалось воспроизвести вообще — тогда вызывающий откатится
// на SAPI.
bool VoiceSynthesisManager::playWavInterruptible(const QString& wavPath,
                                                  int volumePercent, quint64 epoch)
{
    QFile file(wavPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray raw = file.readAll();
    file.close();

    ParsedWav wav;
    if (!parseWav(raw, wav)) {
        // Формат неожиданный — играем как умеем. Прервать такую фразу
        // не выйдет, но молчать вместо неё хуже.
        qWarning() << "[VoiceSynth] unsupported WAV layout, falling back to PlaySound";
        const std::wstring wWavPath = wavPath.toStdWString();
        PlaySoundW(wWavPath.c_str(), nullptr, SND_FILENAME | SND_SYNC);
        return true;
    }

    applyGain(wav.pcm, wav.fmt.wBitsPerSample, volumePercent);

    HWAVEOUT hwo = nullptr;
    if (waveOutOpen(&hwo, WAVE_MAPPER, &wav.fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        qWarning() << "[VoiceSynth] waveOutOpen failed";
        return false;
    }

    WAVEHDR hdr{};
    hdr.lpData         = wav.pcm.data();
    hdr.dwBufferLength = static_cast<DWORD>(wav.pcm.size());

    bool ok = false;
    if (waveOutPrepareHeader(hwo, &hdr, sizeof(hdr)) == MMSYSERR_NOERROR) {
        // См. speakViaSapi: окно защиты от эха отсчитывается от звука,
        // а синтез Piper до этого места занимает заметное время.
        m_currentStartedAtMs.store(m_clock.elapsed());

        if (waveOutWrite(hwo, &hdr, sizeof(hdr)) == MMSYSERR_NOERROR) {
            ok = true;
            while (!(hdr.dwFlags & WHDR_DONE)) {
                if (cancelled(epoch)) {
                    // Звук обрывается здесь, в середине слова, — именно
                    // это отличает перебивание от «дослушай и замолчи».
                    waveOutReset(hwo);
                    break;
                }
                QThread::msleep(CANCEL_POLL_MS);
            }
        }
        waveOutReset(hwo);
        waveOutUnprepareHeader(hwo, &hdr, sizeof(hdr));
    }

    waveOutClose(hwo);
    return ok;
}
