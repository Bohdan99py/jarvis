// ============================================================
// voice_input.cpp — J.A.R.V.I.S. голосовой ввод (Vosk)
//
// Каталог моделей + диалог первого запуска + докачка из Settings
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
#include <QSettings>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <cmath>

// ============================================================
//  VoskModelInfo
// ============================================================

bool VoskModelInfo::isInstalled(const QString& installDir) const
{
    if (checkFile.isEmpty()) return false;
    return QFile::exists(fullPath(installDir) + QStringLiteral("/") + checkFile);
}

QString VoskModelInfo::fullPath(const QString& installDir) const
{
    return installDir + QStringLiteral("/") + subdir;
}

// ============================================================
//  VoskModels — глобальный каталог
// ============================================================

namespace VoskModels {

static QVector<VoskModelInfo> g_catalog;

static void initCatalog()
{
    if (!g_catalog.isEmpty()) return;

    // ---- English small (40 MB) — быстрый старт ----
    g_catalog.push_back({
        QStringLiteral("en-small"),
        QStringLiteral("en"),
        QStringLiteral("English — Fast (Recommended)"),
        QStringLiteral("40 MB · Commands & wake words · Instant load · Best for most users"),
        QStringLiteral("https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip"),
        QStringLiteral("vosk-model-small-en-us-0.15"),
        QStringLiteral("model-en"),
        QStringLiteral("am/final.mdl"),
        40LL * 1024 * 1024,
        true
    });

    // ---- English large (1.8 GB) — диктовка ----
    g_catalog.push_back({
        QStringLiteral("en-large"),
        QStringLiteral("en"),
        QStringLiteral("English — High Quality"),
        QStringLiteral("1.8 GB · Dictation & transcription · Slower load · Best accuracy"),
        QStringLiteral("https://alphacephei.com/vosk/models/vosk-model-en-us-0.42-gigaspeech.zip"),
        QStringLiteral("vosk-model-en-us-0.42-gigaspeech"),
        QStringLiteral("model-en-large"),
        QStringLiteral("am/final.mdl"),
        1800LL * 1024 * 1024,
        false
    });

    // ---- Russian (1.8 GB) ----
    g_catalog.push_back({
        QStringLiteral("ru"),
        QStringLiteral("ru"),
        QStringLiteral("Russian — Высокое качество"),
        QStringLiteral("1.8 GB · Диктовка и команды · Поддержка RU/EN переключения"),
        QStringLiteral("https://alphacephei.com/vosk/models/vosk-model-ru-0.42.zip"),
        QStringLiteral("vosk-model-ru-0.42"),
        QStringLiteral("model-ru"),
        QStringLiteral("am/final.mdl"),
        1800LL * 1024 * 1024,
        false
    });

    // ---- German (1.0 GB) ----
    g_catalog.push_back({
        QStringLiteral("de"),
        QStringLiteral("de"),
        QStringLiteral("Deutsch — Hohe Qualität"),
        QStringLiteral("1.0 GB · Diktat und Befehle"),
        QStringLiteral("https://alphacephei.com/vosk/models/vosk-model-de-0.21.zip"),
        QStringLiteral("vosk-model-de-0.21"),
        QStringLiteral("model-de"),
        QStringLiteral("am/final.mdl"),
        1000LL * 1024 * 1024,
        false
    });

    // ---- French (1.0 GB) ----
    g_catalog.push_back({
        QStringLiteral("fr"),
        QStringLiteral("fr"),
        QStringLiteral("Français — Haute qualité"),
        QStringLiteral("1.0 GB · Dictée et commandes"),
        QStringLiteral("https://alphacephei.com/vosk/models/vosk-model-fr-0.22.zip"),
        QStringLiteral("vosk-model-fr-0.22"),
        QStringLiteral("model-fr"),
        QStringLiteral("am/final.mdl"),
        1000LL * 1024 * 1024,
        false
    });

    // ---- Chinese small (500 MB) ----
    g_catalog.push_back({
        QStringLiteral("zh"),
        QStringLiteral("zh"),
        QStringLiteral("中文 — 快速识别"),
        QStringLiteral("500 MB · 命令和短语"),
        QStringLiteral("https://alphacephei.com/vosk/models/vosk-model-small-cn-0.22.zip"),
        QStringLiteral("vosk-model-small-cn-0.22"),
        QStringLiteral("model-zh"),
        QStringLiteral("am/final.mdl"),
        500LL * 1024 * 1024,
        false
    });
}

const QVector<VoskModelInfo>& catalog()
{
    initCatalog();
    return g_catalog;
}

VoskModelInfo findById(const QString& id)
{
    for (const auto& m : catalog()) {
        if (m.id == id) return m;
    }
    return {};
}

QString formatSize(qint64 bytes)
{
    if (bytes < 1024LL * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024 * 1024));
    return QStringLiteral("%.1f GB").arg(static_cast<double>(bytes) / (1024.0 * 1024 * 1024));
}

} // namespace VoskModels

// ============================================================
//  VoskSetupStatus — проверка установки
// ============================================================

VoskSetupStatus VoskDownloader::checkStatus(const QString& installDir)
{
    VoskSetupStatus s;
    s.dllReady = QFile::exists(installDir + QStringLiteral("/libvosk.dll"));

    for (const auto& m : VoskModels::catalog()) {
        if (m.isInstalled(installDir)) {
            s.installedModelIds.append(m.id);
            if (m.id == QStringLiteral("ru") || m.id == QStringLiteral("ru-small"))
                s.modelRuReady = true;
            if (m.id == QStringLiteral("en-small") || m.id == QStringLiteral("en-large"))
                s.modelEnReady = true;
        }
    }
    return s;
}

// ============================================================
//  VoskDownloader
// ============================================================

static const QString VOSK_DLL_URL =
    QStringLiteral("https://github.com/alphacep/vosk-api/releases/download/v0.3.45/vosk-win64-0.3.45.zip");
static const QString VOSK_DLL_PREFIX = QStringLiteral("vosk-win64-0.3.45");

VoskDownloader::VoskDownloader(QObject* parent) : QObject(parent) {}

void VoskDownloader::setupVosk(const QString& installDir, const QStringList& modelIds)
{
    m_installDir = installDir;
    QDir().mkpath(installDir);

    emit logMessage(QStringLiteral("🔍 Проверяем установку Vosk: %1").arg(installDir));

    ensureDll();

    auto status = checkStatus(installDir);
    if (!status.dllReady) {
        emit setupFinished(false,
            QStringLiteral("Не удалось установить libvosk.dll.\n"
                           "Проверьте интернет и попробуйте снова."));
        return;
    }

    QStringList toInstall = modelIds;
    // Если список пуст — ставим en-small по умолчанию
    if (toInstall.isEmpty()) {
        toInstall << QStringLiteral("en-small");
    }

    for (const QString& id : toInstall) {
        auto info = VoskModels::findById(id);
        if (info.id.isEmpty()) {
            emit logMessage(QStringLiteral("⚠ Неизвестная модель: %1").arg(id));
            continue;
        }
        if (info.isInstalled(installDir)) {
            emit logMessage(QStringLiteral("✅ %1 — уже установлена").arg(info.displayName));
            emit componentReady(id);
            continue;
        }
        emit logMessage(QStringLiteral("📥 Скачиваем %1 (%2)...")
                        .arg(info.displayName, VoskModels::formatSize(info.sizeBytes)));
        downloadAndExtract(id, info.url, info.fullPath(installDir), info.zipPrefix);
    }

    status = checkStatus(installDir);
    if (status.anyModelReady()) {
        emit logMessage(QStringLiteral("🎉 Vosk готов! Голосовой ввод активирован."));
        emit setupFinished(true, QString());
    } else {
        emit setupFinished(false,
            QStringLiteral("Ни одна модель не установлена.\n"
                           "Проверьте подключение к интернету и попробуйте снова."));
    }
}

void VoskDownloader::downloadModel(const QString& installDir, const QString& modelId)
{
    m_installDir = installDir;
    QDir().mkpath(installDir);

    auto info = VoskModels::findById(modelId);
    if (info.id.isEmpty()) {
        emit setupFinished(false, QStringLiteral("Unknown model: %1").arg(modelId));
        return;
    }

    ensureDll();

    auto status = checkStatus(installDir);
    if (!status.dllReady) {
        emit setupFinished(false,
            QStringLiteral("libvosk.dll не найдена. Переустановите JARVIS."));
        return;
    }

    if (info.isInstalled(installDir)) {
        emit componentReady(modelId);
        emit setupFinished(true, QString());
        return;
    }

    emit logMessage(QStringLiteral("📥 Скачиваем %1 (%2)...")
                    .arg(info.displayName, VoskModels::formatSize(info.sizeBytes)));
    downloadAndExtract(modelId, info.url, info.fullPath(installDir), info.zipPrefix);

    if (info.isInstalled(installDir)) {
        emit setupFinished(true, QString());
    } else {
        emit setupFinished(false,
            QStringLiteral("Не удалось скачать модель %1").arg(info.displayName));
    }
}

bool VoskDownloader::deleteModel(const QString& installDir, const QString& modelId)
{
    auto info = VoskModels::findById(modelId);
    if (info.id.isEmpty()) return false;
    QString path = info.fullPath(installDir);
    if (!QDir(path).exists()) return true;
    return QDir(path).removeRecursively();
}

void VoskDownloader::ensureDll()
{
    auto status = checkStatus(m_installDir);
    if (status.dllReady) {
        emit componentReady(QStringLiteral("dll"));
        return;
    }

    emit logMessage(QStringLiteral("📥 Скачиваем Vosk runtime (libvosk.dll)..."));
    downloadAndExtract(
        QStringLiteral("dll"),
        VOSK_DLL_URL,
        m_installDir,
        VOSK_DLL_PREFIX
    );
}

void VoskDownloader::downloadAndExtract(const QString& name,
                                         const QString& url,
                                         const QString& extractTo,
                                         const QString& stripPrefix)
{
    emit downloadStarted(name);

    QString tempPath = QDir::tempPath() + QStringLiteral("/jarvis_vosk_%1.zip").arg(name);

    QNetworkAccessManager nam;
    QUrl qurl(url);
    QNetworkRequest req(qurl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QVariant::fromValue(QNetworkRequest::NoLessSafeRedirectPolicy));
    // Таймаут 30 сек на соединение
    req.setTransferTimeout(30000);

    QNetworkReply* reply = nam.get(req);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::downloadProgress,
            this, [this, name](qint64 received, qint64 total) {
        int pct = (total > 0) ? static_cast<int>(received * 100 / total) : 0;
        emit downloadProgress(name, pct, total);
    });
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        emit logMessage(QStringLiteral("❌ Ошибка скачивания [%1]: %2")
                        .arg(name, reply->errorString()));
        reply->deleteLater();
        return;
    }

    QFile zipFile(tempPath);
    if (!zipFile.open(QIODevice::WriteOnly)) {
        emit logMessage(QStringLiteral("❌ Не удаётся записать файл: %1").arg(tempPath));
        reply->deleteLater();
        return;
    }
    zipFile.write(reply->readAll());
    zipFile.close();
    reply->deleteLater();

    emit logMessage(QStringLiteral("📦 Распаковываем %1...").arg(name));
    emit extracting(name);

    bool ok = extractZipPowerShell(tempPath, extractTo, stripPrefix);
    QFile::remove(tempPath);

    if (ok) {
        emit logMessage(QStringLiteral("✅ %1 установлена в: %2").arg(name, extractTo));
        emit componentReady(name);
    } else {
        emit logMessage(QStringLiteral("❌ Ошибка распаковки: %1").arg(name));
    }
}

bool VoskDownloader::extractZipPowerShell(const QString& zipPath,
                                           const QString& targetDir,
                                           const QString& stripPrefix)
{
    QDir().mkpath(targetDir);

    QString script;
    if (stripPrefix.isEmpty()) {
        script = QStringLiteral(
            "Expand-Archive -Path '%1' -DestinationPath '%2' -Force"
        ).arg(zipPath, targetDir);
    } else {
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
        qWarning() << "[Vosk] PowerShell недоступен";
        return false;
    }
    ps.waitForFinished(600000); // 10 минут максимум для больших моделей

    if (ps.exitCode() != 0) {
        qWarning() << "[Vosk] PowerShell ошибка:" << ps.readAllStandardError();
        return false;
    }
    return true;
}

// ============================================================
//  VoiceInput — статические методы
// ============================================================

bool VoiceInput::isFirstRun()
{
    QSettings s(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
    return !s.value(QStringLiteral("voice/setup_complete"), false).toBool();
}

void VoiceInput::markFirstRunComplete()
{
    QSettings s(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
    s.setValue(QStringLiteral("voice/setup_complete"), true);
}

QString VoiceInput::voskInstallDir()
{
    // 1. Рядом с exe (installer bundled DLL)
    QString exeDir = QCoreApplication::applicationDirPath();
    if (QFile::exists(exeDir + QStringLiteral("/libvosk.dll")))
        return exeDir;

    // 2. AppData/JARVIS/vosk
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/vosk");
}

VoskSetupStatus VoiceInput::checkSetupStatus()
{
    return VoskDownloader::checkStatus(voskInstallDir());
}

QStringList VoiceInput::installedModelIds() const
{
    return checkSetupStatus().installedModelIds;
}

// ============================================================
//  VoiceRecorder
// ============================================================

VoiceRecorder::VoiceRecorder(QObject* parent) : QObject(parent)
{
    m_silenceTimer = new QTimer(this);
    m_silenceTimer->setSingleShot(true);
    connect(m_silenceTimer, &QTimer::timeout, this, &VoiceRecorder::onSilenceTimeout);
}

VoiceRecorder::~VoiceRecorder() { stop(); }

bool VoiceRecorder::start(const WhisperConfig& config)
{
    if (m_recording.load()) return true;
    m_config = config;

    // Пробуем форматы в порядке предпочтения: 16k → 48k → 44.1k
    static const int kRates[] = { 16000, 48000, 44100 };

    QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        emit error(QStringLiteral("Микрофон не найден"));
        return false;
    }

    bool found = false;
    for (int rate : kRates) {
        m_format.setSampleRate(rate);
        m_format.setChannelCount(1);
        m_format.setSampleFormat(QAudioFormat::Int16);
        if (inputDevice.isFormatSupported(m_format)) { found = true; break; }
    }
    if (!found) {
        emit error(QStringLiteral("Аудиоформат не поддерживается устройством: %1")
                   .arg(inputDevice.description()));
        return false;
    }

    m_audioSource = new QAudioSource(inputDevice, m_format, this);
    m_audioSource->setBufferSize(4096);
    m_audioDevice = m_audioSource->start();

    if (!m_audioDevice) {
        emit error(QStringLiteral("Не удалось открыть микрофон"));
        return false;
    }

    connect(m_audioDevice, &QIODevice::readyRead, this, &VoiceRecorder::onAudioDataReady);

    m_recording.store(true);
    m_speaking = false;
    m_currentBuffer.clear();
    qDebug() << "[Voice] Запись начата:" << inputDevice.description()
             << "@ " << m_format.sampleRate() << "Hz";
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

    int srcRate = m_format.sampleRate();
    QByteArray pcm16 = (srcRate == 16000) ? raw : downsampleTo16k(raw, srcRate);

    emit audioChunkReady(pcm16);

    float db = computeRmsDb(pcm16);
    emit volumeLevel(db);

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
        m_currentBuffer.append(pcm16); // хвост для лучшего распознавания
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

QByteArray VoiceRecorder::downsampleTo16k(const QByteArray& src, int srcRate) const
{
    const int16_t* in = reinterpret_cast<const int16_t*>(src.constData());
    int inN  = src.size() / 2;
    int outN = static_cast<int>(static_cast<double>(inN) * 16000.0 / srcRate);
    if (outN <= 0) return {};
    QByteArray out(outN * 2, '\0');
    int16_t* o = reinterpret_cast<int16_t*>(out.data());
    for (int i = 0; i < outN; ++i) {
        float fi = static_cast<float>(i) * srcRate / 16000.0f;
        int i0 = static_cast<int>(fi);
        int i1 = qMin(i0 + 1, inN - 1);
        float f = fi - i0;
        o[i] = static_cast<int16_t>(in[i0] * (1.0f - f) + in[i1] * f);
    }
    return out;
}

// legacy alias
QByteArray VoiceRecorder::downsample44to16(const QByteArray& src) const
{
    return downsampleTo16k(src, 44100);
}

// ============================================================
//  VoskWorker
// ============================================================

VoskWorker::VoskWorker(QObject* parent) : QObject(parent) {}

VoskWorker::~VoskWorker()
{
    freeAll();
}

void VoskWorker::freeAll()
{
#ifdef JARVIS_VOSK_AVAILABLE
    for (auto& mp : m_models) {
        if (mp.recognizer) vosk_recognizer_free(static_cast<VoskRecognizer*>(mp.recognizer));
        if (mp.model)      vosk_model_free(static_cast<VoskModel*>(mp.model));
    }
#endif
    m_models.clear();
    m_loaded = false;
}

void VoskWorker::loadModels(const WhisperConfig& config)
{
    if (m_loaded) { emit modelsLoaded(true, QString()); return; }
    reloadModels(config);
}

void VoskWorker::reloadModels(const WhisperConfig& config)
{
    freeAll();

#ifdef JARVIS_VOSK_AVAILABLE
    vosk_set_log_level(-1);

    // Собираем пути: стандартные + extra
    QMap<QString, QString> paths;
    if (!config.modelPathRu.isEmpty()) paths[QStringLiteral("ru")] = config.modelPathRu;
    if (!config.modelPathEn.isEmpty()) paths[QStringLiteral("en")] = config.modelPathEn;
    for (auto it = config.extraModels.constBegin(); it != config.extraModels.constEnd(); ++it) {
        paths[it.key()] = it.value();
    }

    // Дополнительно: ищем по enabledModelIds
    QString installDir = VoiceInput::voskInstallDir();
    for (const QString& id : config.enabledModelIds) {
        auto info = VoskModels::findById(id);
        if (!info.id.isEmpty() && info.isInstalled(installDir)) {
            // Не дублируем
            if (!paths.contains(info.language)) {
                paths[info.language] = info.fullPath(installDir);
            }
        }
    }

    bool any = false;
    for (auto it = paths.constBegin(); it != paths.constEnd(); ++it) {
        const QString& lang = it.key();
        const QString& path = it.value();
        if (!QDir(path).exists()) {
            qWarning() << "[Vosk] Путь не существует:" << path;
            continue;
        }

        VoskModel* mdl = vosk_model_new(path.toUtf8().constData());
        if (!mdl) { qWarning() << "[Vosk] Не удалось загрузить модель:" << path; continue; }

        VoskRecognizer* reco = vosk_recognizer_new(mdl, 16000.0f);
        if (!reco) {
            vosk_model_free(mdl);
            qWarning() << "[Vosk] Не удалось создать распознаватель для:" << lang;
            continue;
        }

        vosk_recognizer_set_words(reco, 1);

        ModelPair mp;
        mp.model      = mdl;
        mp.recognizer = reco;
        mp.lang       = lang;
        m_models.push_back(mp);
        any = true;
        qDebug() << "[Vosk] Загружена модель:" << lang << "←" << path;
    }

    m_loaded = any;
    emit modelsLoaded(any, any ? QString()
        : QStringLiteral("Не удалось загрузить ни одной модели Vosk"));
#else
    Q_UNUSED(config)
    emit modelsLoaded(false, QStringLiteral("Vosk stub build — модели недоступны"));
#endif
}

void VoskWorker::recognize(QByteArray pcmData, QString preferredLang)
{
#ifdef JARVIS_VOSK_AVAILABLE
    if (!m_loaded || m_models.isEmpty()) {
        emit error(QStringLiteral("Модели не загружены"));
        return;
    }

    bool whisper = isWhisperLevel(pcmData);

    // Собираем результаты всех распознавателей
    struct Candidate { QString text; QString lang; };
    QVector<Candidate> candidates;
    candidates.reserve(m_models.size());

    for (auto& mp : m_models) {
        QString t = tryRecognize(mp.recognizer, pcmData);
        if (!t.isEmpty()) candidates.push_back({t, mp.lang});
    }

    if (candidates.isEmpty()) return;

    // Выбираем по предпочтению языка
    QString text, lang;
    for (const auto& c : candidates) {
        if (preferredLang == QStringLiteral("auto") || c.lang == preferredLang) {
            text = c.text; lang = c.lang; break;
        }
    }
    if (text.isEmpty()) { text = candidates.first().text; lang = candidates.first().lang; }

    text = text.simplified().trimmed();
    if (text.isEmpty()) return;

    qDebug() << "[Vosk] [" << lang << "]:" << text << (whisper ? "[WHISPER]" : "");
    emit recognized(text, lang, whisper);
#else
    Q_UNUSED(pcmData) Q_UNUSED(preferredLang)
    emit error(QStringLiteral("Vosk недоступен"));
#endif
}

QString VoskWorker::tryRecognize(void* recognizer, const QByteArray& pcmData) const
{
#ifdef JARVIS_VOSK_AVAILABLE
    auto* r = static_cast<VoskRecognizer*>(recognizer);
    vosk_recognizer_reset(r);
    vosk_recognizer_accept_waveform_s(r,
        reinterpret_cast<const int16_t*>(pcmData.constData()),
        pcmData.size() / 2);
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
    return (rms >= 1e-10) && ((20.0 * std::log10(rms)) < -35.0);
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
    connect(m_recorder, &VoiceRecorder::volumeLevel,   this, &VoiceInput::volumeLevel);
    connect(m_recorder, &VoiceRecorder::speechEnded,   this, &VoiceInput::onSpeechEnded);
    connect(m_recorder, &VoiceRecorder::error,         this, &VoiceInput::onRecorderError);

    connect(this, &VoiceInput::requestLoadModels,   m_worker, &VoskWorker::loadModels,   Qt::QueuedConnection);
    connect(this, &VoiceInput::requestRecognize,    m_worker, &VoskWorker::recognize,    Qt::QueuedConnection);
    connect(this, &VoiceInput::requestReloadModels, m_worker, &VoskWorker::reloadModels, Qt::QueuedConnection);

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

void VoiceInput::initialize(const WhisperConfig& config)
{
    m_config = config;

    auto status = checkSetupStatus();

    if (!status.dllReady) {
        // DLL нет совсем — нужна полная установка
        emit setupRequired();
        // Запускаем с моделями по умолчанию (en-small)
        startSetup({ QStringLiteral("en-small") });
        return;
    }

    if (!status.anyModelReady() || isFirstRun()) {
        // DLL есть, но нет моделей или первый запуск → показываем диалог
        emit setupRequired(); // UI покажет VoskSetupDialog
        return;
    }

    loadModelsFromDisk();
}

void VoiceInput::startSetup(const QStringList& modelIds)
{
    if (m_setupThread && m_setupThread->isRunning()) return;

    QString installDir = voskInstallDir();

    m_setupThread = new QThread(this);
    m_downloader  = new VoskDownloader();
    m_downloader->moveToThread(m_setupThread);

    connectDownloader(m_downloader);

    connect(m_downloader, &VoskDownloader::setupFinished,
            this, &VoiceInput::onSetupFinished);

    connect(m_setupThread, &QThread::started,
            m_downloader,  [this, installDir, modelIds]() {
        m_downloader->setupVosk(installDir, modelIds);
    }, Qt::QueuedConnection);

    m_setupThread->start(QThread::LowPriority);
}

// Вызывается из VoskSetupDialog после того как пользователь выбрал модели
void VoiceInput::downloadModel(const QString& modelId)
{
    m_pendingDownloadModelId = modelId;
    QString installDir = voskInstallDir();

    auto* thread = new QThread(this);
    auto* dl = new VoskDownloader();
    dl->moveToThread(thread);

    connect(dl, &VoskDownloader::downloadProgress, this,
            [this, modelId](const QString&, int pct, qint64 total) {
        emit modelDownloadProgress(modelId, pct, total);
    });
    connect(dl, &VoskDownloader::downloadStarted, this,
            [this, modelId](const QString&) {
        emit modelDownloadStarted(modelId);
    });
    connect(dl, &VoskDownloader::setupFinished,
            this, &VoiceInput::onModelDownloadFinished);
    connect(dl, &VoskDownloader::logMessage, this, &VoiceInput::setupLogMessage);

    connect(thread, &QThread::started, dl, [dl, installDir, modelId]() {
        dl->downloadModel(installDir, modelId);
    }, Qt::QueuedConnection);

    connect(dl, &VoskDownloader::setupFinished, thread, [thread, dl](...) {
        thread->quit();
        dl->deleteLater();
        thread->deleteLater();
    }, Qt::QueuedConnection);

    thread->start(QThread::LowPriority);
}

bool VoiceInput::deleteModel(const QString& modelId)
{
    bool ok = VoskDownloader::deleteModel(voskInstallDir(), modelId);
    if (ok) reloadModels();
    return ok;
}

void VoiceInput::reloadModels()
{
    loadModelsFromDisk();
}

void VoiceInput::connectDownloader(VoskDownloader* dl)
{
    connect(dl, &VoskDownloader::downloadStarted, this,
            [this](const QString& c) {
        emit setupLogMessage(QStringLiteral("⬇ Скачиваем: %1").arg(c));
    });
    connect(dl, &VoskDownloader::downloadProgress, this,
            [this](const QString& c, int pct, qint64 total) {
        emit setupProgress(c, pct, total);
    });
    connect(dl, &VoskDownloader::extracting, this,
            [this](const QString& c) {
        emit setupLogMessage(QStringLiteral("📦 Распаковываем: %1...").arg(c));
    });
    connect(dl, &VoskDownloader::componentReady, this,
            [this](const QString& c) {
        emit setupComponentReady(c);
    });
    connect(dl, &VoskDownloader::logMessage, this,
            [this](const QString& msg) {
        emit setupLogMessage(msg);
    });
}

void VoiceInput::onSetupFinished(bool success, const QString& error)
{
    if (m_setupThread) {
        m_setupThread->quit();
        m_setupThread->wait(3000);
    }

    markFirstRunComplete();
    emit setupFinished(success, error);

    if (success) {
        loadModelsFromDisk();
    } else {
        emit initError(error);
    }
}

void VoiceInput::onModelDownloadFinished(bool success, const QString& error)
{
    emit modelDownloadFinished(m_pendingDownloadModelId, success);
    if (success && !m_pendingDownloadModelId.isEmpty()) {
        // Перезагружаем модели чтобы подхватить новую
        reloadModels();
    }
    if (!success && !error.isEmpty()) {
        emit errorOccurred(error);
    }
    m_pendingDownloadModelId.clear();
}

void VoiceInput::loadModelsFromDisk()
{
    QString installDir = voskInstallDir();
    auto status = checkSetupStatus();

    // Маппинг ID установленных моделей → пути
    m_config.extraModels.clear();
    m_config.modelPathRu.clear();
    m_config.modelPathEn.clear();
    m_config.enabledModelIds.clear();

    for (const QString& id : status.installedModelIds) {
        auto info = VoskModels::findById(id);
        if (info.id.isEmpty()) continue;

        m_config.enabledModelIds.append(id);

        // Заполняем legacy поля для обратной совместимости
        if (info.language == QStringLiteral("ru"))
            m_config.modelPathRu = info.fullPath(installDir);
        else if (info.language == QStringLiteral("en") && m_config.modelPathEn.isEmpty())
            m_config.modelPathEn = info.fullPath(installDir);
        else
            m_config.extraModels[info.language] = info.fullPath(installDir);
    }

    qDebug() << "[Voice] Загружаем модели:" << status.installedModelIds;
    emit requestLoadModels(m_config);
}

void VoiceInput::onModelsLoaded(bool success, const QString& err)
{
    if (!success) { emit initError(err); return; }
    m_initialized = true;
    qDebug() << "[Voice] Vosk готов";
    emit ready();
}

void VoiceInput::startListening()
{
    if (!m_initialized) {
        emit errorOccurred(QStringLiteral("Голосовой ввод ещё не готов"));
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