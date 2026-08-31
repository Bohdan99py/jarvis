#pragma once
// ============================================================
// elevenlabs_provider.h — Networked TTS with controllable delivery
//
// Главный голос JARVIS, когда есть ключ и сеть. Всё остальное в
// голосовом слое от этого не меняется: провайдер отдаёт такой же WAV,
// он так же ложится в кэш и так же прерывается на полуслове.
//
// Три вещи, которые здесь важны и неочевидны:
//
// 1. Просим PCM, а не MP3. Сырой PCM 22050/16/моно — ровно то, что
//    умеет играть waveOut, и ровно то, что отдаёт Piper. MP3 пришлось
//    бы декодировать, а прерываемое воспроизведение строить заново.
//
// 2. Стиль переводится в stability/style/speed ЗДЕСЬ. Ни ядро, ни тем
//    более модель не пишут теги подачи в текст: сменив провайдера,
//    пришлось бы переписывать промпты.
//
// 3. Провал не роняет речь. Нет ключа, кончились символы, отвалилась
//    сеть — менеджер молча уходит на Piper, а причина попадает в
//    ленту событий, чтобы это не выглядело «просто голос стал хуже».
//
// Ключ берётся из ELEVENLABS_API_KEY либо из файла рядом с ключом
// Anthropic (AppData/jarvis_elevenlabs.dat) — в репозитории его нет.
// ============================================================

#include "voice_provider.h"

#include <QString>

#include <functional>

class ElevenLabsProvider : public VoiceProvider
{
public:
    ElevenLabsProvider();

    QString id() const override { return QStringLiteral("elevenlabs"); }
    QString displayName() const override { return QStringLiteral("ElevenLabs"); }

    // Доступен = включён в настройках и есть ключ. Наличие сети здесь
    // не проверяется: это выяснится на первом же запросе, а гадать
    // заранее — лишний источник неправды.
    bool isAvailable() const override;

    QString voiceId(const SpeechRequest& req) const override;
    bool synthesize(const SpeechRequest& req, const QString& outPath) override;

    // --- Настройка ---
    static QString apiKey();
    static void    setApiKey(const QString& key);
    static bool    hasApiKey();

    static bool enabled();
    static void setEnabled(bool on);

    // Голос и модель хранятся в общих настройках JARVIS. Голос по
    // умолчанию — стоковый мужской; для «того самого» JARVIS сюда
    // стоит подставить свой клонированный.
    static QString configuredVoiceId();
    static void    setConfiguredVoiceId(const QString& voice);
    static QString modelId();
    static void    setModelId(const QString& model);

    // Последняя ошибка API — для панели здоровья и диалога настроек.
    QString lastError() const { return m_lastError; }

    // Куда сообщать об отказах. Ставит ядро (Jarvis) — оно знает про
    // ленту событий, а голосовой слой про неё знать не обязан.
    using FailureReporter = std::function<void(const QString& reason)>;
    static void setFailureReporter(FailureReporter reporter);

    // Оборачивает сырой PCM в RIFF/WAVE: ElevenLabs отдаёт голые
    // сэмплы, а всё остальное в голосовом слое ждёт файл с заголовком.
    // Открыта наружу намеренно: заголовок обязан совпадать с тем, что
    // пишет Piper, иначе воспроизведение молча уедет на непрерываемый
    // PlaySound — это стоит проверять тестом, а не глазами.
    static QByteArray wrapPcmAsWav(const QByteArray& pcm, int sampleRate);

    // Частота PCM, которую просим у API. 22050 совпадает с Piper —
    // одинаковый звук на слух при переключении провайдера.
    static constexpr int SAMPLE_RATE = 22050;

private:
    void reportFailure(const QString& reason);

    QString m_lastError;

    static constexpr int REQUEST_TIMEOUT_MS = 20000;
};
