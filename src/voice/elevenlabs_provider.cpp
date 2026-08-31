// ============================================================
// elevenlabs_provider.cpp — Networked TTS with controllable delivery
// ============================================================
#include "elevenlabs_provider.h"

#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

QSettings jarvisSettings()
{
    return QSettings(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
}

// Ключ лежит там же, где ключ Anthropic (см. claude_api.cpp), и тем же
// способом: отдельный файл в AppData, не в репозитории и не в реестре
// рядом с обычными настройками.
QString apiKeyFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/jarvis_elevenlabs.dat");
}

// Стоковый мужской голос ElevenLabs. Взят как рабочая отправная точка:
// для JARVIS правильнее свой клон, но начинать с пустого поля хуже, чем
// с голоса, который сразу звучит.
const char* kDefaultVoiceId = "pNInz6obpgDQGcFmaJgB";

// Многоязычная модель обязательна: одноязычные не читают кириллицу.
const char* kDefaultModelId = "eleven_multilingual_v2";

// Стиль → ручки ElevenLabs. Меньше stability — живее и разнообразнее
// интонация; для предупреждений это лишнее, там нужна ровность.
struct ElevenSettings {
    double stability;
    double similarityBoost;
    double style;
};

ElevenSettings settingsForStyle(SpeechStyle style)
{
    switch (style) {
    case SpeechStyle::Conversational: return {0.40, 0.75, 0.35};
    case SpeechStyle::Neutral:        return {0.50, 0.75, 0.20};
    case SpeechStyle::Informative:    return {0.55, 0.75, 0.15};
    case SpeechStyle::Warning:        return {0.70, 0.80, 0.10};
    case SpeechStyle::Critical:       return {0.80, 0.85, 0.05};
    case SpeechStyle::Whisper:        return {0.60, 0.75, 0.25};
    }
    return {0.50, 0.75, 0.20};
}

} // namespace

ElevenLabsProvider::ElevenLabsProvider() = default;

// ============================================================
//  Настройки
// ============================================================

QString ElevenLabsProvider::apiKey()
{
    // Переменная окружения важнее файла: так ключ можно подсунуть на
    // одну сессию, не оставляя его на диске.
    const QByteArray fromEnv = qgetenv("ELEVENLABS_API_KEY");
    if (!fromEnv.isEmpty())
        return QString::fromUtf8(fromEnv).trimmed();

    QFile file(apiKeyFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    const QString key = QString::fromUtf8(file.readAll()).trimmed();
    file.close();
    return key;
}

void ElevenLabsProvider::setApiKey(const QString& key)
{
    const QString trimmed = key.trimmed();

    if (trimmed.isEmpty()) {
        QFile::remove(apiKeyFilePath());
        return;
    }

    QFile file(apiKeyFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[ElevenLabs] cannot save key";
        return;
    }
    file.write(trimmed.toUtf8());
    file.close();
}

bool ElevenLabsProvider::hasApiKey()
{
    return !apiKey().isEmpty();
}

bool ElevenLabsProvider::enabled()
{
    // Ключ есть — значит включали осознанно; отдельный выключатель
    // нужен, чтобы уйти на офлайн-голос, не стирая ключ.
    return jarvisSettings().value(QStringLiteral("elevenlabs/enabled"), true).toBool();
}

void ElevenLabsProvider::setEnabled(bool on)
{
    jarvisSettings().setValue(QStringLiteral("elevenlabs/enabled"), on);
}

QString ElevenLabsProvider::configuredVoiceId()
{
    return jarvisSettings().value(QStringLiteral("elevenlabs/voice_id"),
                                   QString::fromLatin1(kDefaultVoiceId)).toString();
}

void ElevenLabsProvider::setConfiguredVoiceId(const QString& voice)
{
    jarvisSettings().setValue(QStringLiteral("elevenlabs/voice_id"), voice.trimmed());
}

QString ElevenLabsProvider::modelId()
{
    return jarvisSettings().value(QStringLiteral("elevenlabs/model_id"),
                                   QString::fromLatin1(kDefaultModelId)).toString();
}

void ElevenLabsProvider::setModelId(const QString& model)
{
    jarvisSettings().setValue(QStringLiteral("elevenlabs/model_id"), model.trimmed());
}

bool ElevenLabsProvider::isAvailable() const
{
    return enabled() && hasApiKey() && !configuredVoiceId().isEmpty();
}

QString ElevenLabsProvider::voiceId(const SpeechRequest&) const
{
    // Голос один на все языки — многоязычная модель сама переключается.
    // В ключ кэша идёт вместе с моделью: сменили любое — записи чужие.
    return configuredVoiceId() + QLatin1Char('@') + modelId();
}

// ============================================================
//  Синтез
// ============================================================

QByteArray ElevenLabsProvider::wrapPcmAsWav(const QByteArray& pcm, int sampleRate)
{
    const int channels      = 1;
    const int bitsPerSample = 16;
    const int byteRate      = sampleRate * channels * bitsPerSample / 8;
    const int blockAlign    = channels * bitsPerSample / 8;

    QByteArray wav;
    QDataStream out(&wav, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);

    out.writeRawData("RIFF", 4);
    out << quint32(36 + pcm.size());
    out.writeRawData("WAVE", 4);

    out.writeRawData("fmt ", 4);
    out << quint32(16);              // размер fmt-блока
    out << quint16(1);               // PCM
    out << quint16(channels);
    out << quint32(sampleRate);
    out << quint32(byteRate);
    out << quint16(blockAlign);
    out << quint16(bitsPerSample);

    out.writeRawData("data", 4);
    out << quint32(pcm.size());
    out.writeRawData(pcm.constData(), pcm.size());

    return wav;
}

// Кому сообщать об отказах, решает не голосовой слой: лента событий
// живёт выше (src/agent), и тянуть её сюда значило бы связать синтез
// речи с подсистемой уведомлений ради одной строки.
static ElevenLabsProvider::FailureReporter g_failureReporter;

void ElevenLabsProvider::setFailureReporter(FailureReporter reporter)
{
    g_failureReporter = std::move(reporter);
}

void ElevenLabsProvider::reportFailure(const QString& reason)
{
    m_lastError = reason;
    qWarning() << "[ElevenLabs]" << reason;

    if (g_failureReporter)
        g_failureReporter(reason);
}

bool ElevenLabsProvider::synthesize(const SpeechRequest& req, const QString& outPath)
{
    if (!isAvailable())
        return false;

    const StyleParams   sp = styleParams(req.style);
    const ElevenSettings es = settingsForStyle(req.style);

    QUrl url(QStringLiteral("https://api.elevenlabs.io/v1/text-to-speech/%1")
                 .arg(configuredVoiceId()));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("output_format"),
                        QStringLiteral("pcm_%1").arg(SAMPLE_RATE));
    url.setQuery(query);

    QJsonObject voiceSettings{
        {QStringLiteral("stability"),        es.stability},
        {QStringLiteral("similarity_boost"), es.similarityBoost},
        {QStringLiteral("style"),            es.style},
        {QStringLiteral("use_speaker_boost"), true},
        // Темп приходит из того же StyleParams, что крутит length_scale
        // у Piper, только там это множитель длительности, а здесь —
        // скорость: величины обратные.
        {QStringLiteral("speed"),            qBound(0.7, 1.0 / sp.lengthScale, 1.2)},
    };

    const QJsonObject body{
        {QStringLiteral("text"),           req.text},
        {QStringLiteral("model_id"),       modelId()},
        {QStringLiteral("voice_settings"), voiceSettings},
    };

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                       QStringLiteral("application/json"));
    request.setRawHeader("xi-api-key", apiKey().toUtf8());
    request.setRawHeader("accept", "audio/*");

    // Свой QNetworkAccessManager и свой цикл событий: синтез идёт на
    // фоновом потоке пула (см. VoiceSynthesisManager), тем же способом,
    // что и скачивание моделей Piper.
    QNetworkAccessManager nam;
    QNetworkReply* reply = nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(REQUEST_TIMEOUT_MS);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        reportFailure(QStringLiteral("превышено время ожидания ответа"));
        return false;
    }

    if (reply->error() != QNetworkReply::NoError) {
        // Тело ответа полезнее кода: там написано, что именно не так —
        // модель недоступна, кончились символы, голос не найден.
        const QString detail  = QString::fromUtf8(reply->readAll()).left(300);
        const QString network = reply->errorString();
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        reportFailure(QStringLiteral("HTTP %1: %2").arg(status)
                          .arg(detail.isEmpty() ? network : detail));
        return false;
    }

    const QByteArray pcm = reply->readAll();
    reply->deleteLater();

    if (pcm.size() < 1024) {
        reportFailure(QStringLiteral("ответ пустой или слишком короткий (%1 байт)")
                          .arg(pcm.size()));
        return false;
    }

    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly)) {
        reportFailure(QStringLiteral("не удалось записать файл: %1").arg(outPath));
        return false;
    }
    out.write(wrapPcmAsWav(pcm, SAMPLE_RATE));
    out.close();

    m_lastError.clear();
    return true;
}
