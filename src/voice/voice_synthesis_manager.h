#pragma once
// ============================================================
// voice_synthesis_manager.h — Thread-Safe TTS Priority Queue
//
// Manages the TTS pipeline with a priority playback queue.
// Starts on Windows SAPI immediately, then self-provisions Piper
// (offline neural TTS) in the background: downloads the piper.exe
// runtime and the RU/EN voice models on first run if missing, and
// switches over automatically once ready.
//
// Очередь не FIFO: критическая реплика обгоняет и вытесняет ту, что
// сейчас звучит, фоновая — выбрасывается, если протухла за время
// ожидания. Воспроизведение прерывается на самом звуке (waveOut /
// SAPI purge), а не только «на бумаге» очисткой очереди.
//
// Usage:
//   VoiceSynthesisManager::instance().say("Готово, файл обновлён.");
//   VoiceSynthesisManager::instance().say(SpeechRequest::critical(
//       "Температура процессора 98 градусов."));
//   VoiceSynthesisManager::instance().cancelCurrentSpeech(
//       CancelReason::UserBargeIn);
// ============================================================

#include "speech_request.h"
#include "voice_policy.h"

#include <memory>

class SpeechAggregator;
class VoiceProvider;
class PiperProvider;
class ElevenLabsProvider;

#include <QObject>
#include <QString>
#include <QVector>
#include <QMutex>
#include <QThread>
#include <QElapsedTimer>
#include <atomic>

class VoiceSynthesisManager : public QObject
{
    Q_OBJECT

public:
    static VoiceSynthesisManager& instance();

    VoiceSynthesisManager(const VoiceSynthesisManager&)            = delete;
    VoiceSynthesisManager& operator=(const VoiceSynthesisManager&) = delete;

    // Короткая форма для вызывающих, которым нечего сказать о подаче:
    // обычный ответ ассистента, Normal/Conversational.
    void say(const QString& conversationalText);
    void say(const SpeechRequest& request);

    // Обрывает текущую реплику. UserBargeIn и UserStop чистят очередь —
    // пользователь остановил не одну фразу, а поток речи; Preempted
    // оставляет её, иначе вытесняющая реплика убила бы всё остальное.
    void cancelCurrentSpeech(CancelReason reason);

    // Прежнее имя, оставлено для существующих вызывающих.
    void stopSpeaking() { cancelCurrentSpeech(CancelReason::UserStop); }

    bool isSpeaking() const { return m_speaking.load(); }
    bool isEnabled()  const { return m_enabled.load(); }
    void setEnabled(bool on);

    // Приоритет реплики, которая звучит прямо сейчас. Нужен тем, кто
    // решает, уместно ли перебивать (см. barge-in в MainWindow).
    SpeechPriority currentPriority() const
    {
        return static_cast<SpeechPriority>(m_currentPriority.load());
    }
    bool currentIsInterruptible() const { return m_currentInterruptible.load(); }

    // Сколько миллисекунд звучит текущая реплика. Первые сотни мс
    // перебивание игнорируется: это почти всегда эхо собственного
    // голоса из колонок, а не пользователь.
    qint64 currentSpeechElapsedMs() const;

    void loadModelsAsync();
    bool piperAvailable() const { return m_piperReady.load(); }

    // Кто озвучит следующую реплику. Для панели здоровья: «голос стал
    // хуже» должно иметь видимое объяснение.
    QString activeProviderName() const;

    // Прогревает кэш дежурными фразами (см. speech_phrases.h). Зовётся
    // сама, когда Piper готов; открыта для настроек — после смены голоса
    // прежние записи не подойдут, их ключ изменился.
    void warmupCacheAsync();

    // Снимает markdown/код/URL — то, что незачем произносить вслух.
    // say() применяет её сам; открыта для вызывающих, которым нужно
    // оценить уже очищенный текст (см. Jarvis::filterTextForSpeech).
    static QString sanitizeForSpeech(const QString& text);

signals:
    void speakingChanged(bool speaking);
    void speechCancelled(CancelReason reason);

    // Политика голоса запретила реплику. Notify означает «покажи её
    // человеку глазами» — иначе событие пропадёт совсем, а это уже не
    // тактичность, а потеря информации.
    void speechSuppressed(const SpeechRequest& request, VoiceDecision decision);
    void modelLoaded(const QString& modelName);
    void modelLoadFailed(const QString& error);

private:
    explicit VoiceSynthesisManager(QObject* parent = nullptr);
    ~VoiceSynthesisManager() override;

    // Одна запись очереди: сам запрос плюс момент постановки — по нему
    // отбраковываются протухшие фоновые реплики.
    struct QueuedSpeech {
        SpeechRequest request;
        qint64        enqueuedAtMs = 0;
    };

    // Постановка в очередь без политики и без склейки: сюда приходит
    // то, что уже решено произнести. Повторно прогонять через say()
    // нельзя — склеенная реплика ушла бы в склейку заново.
    void enqueue(const SpeechRequest& req);

    void processQueue();
    void finishUtterance();
    bool dequeueNext(SpeechRequest& out);
    bool cancelled(quint64 epoch) const { return m_epoch.load() != epoch; }

    void speakViaSapi(const SpeechRequest& req, quint64 epoch);

    // Проходит по провайдерам сверху вниз: сетевой, затем локальный.
    // Каждый сначала спрашивает кэш и только потом синтезирует, поэтому
    // при мёртвой сети заранее прогретая фраза всё равно звучит хорошим
    // голосом. false — не смог никто, остаётся SAPI.
    bool speakViaProviders(const SpeechRequest& req, quint64 epoch);
    QVector<VoiceProvider*> orderedProviders() const;

    bool playWavInterruptible(const QString& wavPath, int volumePercent, quint64 epoch);

    // Пустая строка — реплика не кэшируется (текст уникален).
    QString cacheKeyFor(const SpeechRequest& req, VoiceProvider* provider) const;

    void tryLoadPiperModels();
    QString findPiperExe() const;
    bool provisionPiperRuntime();
    bool provisionVoice(const QString& onnxUrl, const QString& jsonUrl,
                         const QString& onnxName, const QString& jsonName,
                         const QString& modelsDir);

    QVector<QueuedSpeech> m_queue;
    QMutex                m_queueMutex;
    QElapsedTimer         m_clock;   // монотонные метки для expiresAfterMs

    // Защита от заикания: та же фраза (или её начало) не произносится
    // дважды подряд. Структурно дубль уже устранён — реплику озвучивает
    // только MainWindow, — но подстраховка не даёт вернуть его случайно
    // из нового места вызова.
    QString           m_lastSpoken;
    QElapsedTimer     m_lastSpokenTimer;
    static constexpr qint64 DEDUP_WINDOW_MS  = 10000;
    // Ниже этой длины совпадение начала ни о чём не говорит.
    static constexpr int    MIN_DEDUP_CHARS  = 24;

    SpeechAggregator* m_aggregator   = nullptr;
    QThread*          m_workerThread = nullptr;
    std::atomic<bool> m_speaking{false};
    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_processing{false};

    // Отмена через счётчик поколений, а не через флаг «остановись»:
    // флаг приходилось сбрасывать сразу же, и синтезирующий поток мог
    // его вообще не увидеть. Поколение растёт при каждой отмене, и
    // поток, начавший работу со старым номером, сам себя прекращает —
    // гонки «кто успел сбросить флаг» здесь нет.
    std::atomic<quint64> m_epoch{0};

    std::atomic<int>  m_currentPriority{static_cast<int>(SpeechPriority::Normal)};
    std::atomic<bool> m_currentInterruptible{true};
    std::atomic<qint64> m_currentStartedAtMs{0};

    // Провайдеры синтеза. Порядок опроса задаёт orderedProviders();
    // SAPI сюда не входит — он не отдаёт файл (см. voice_provider.h).
    std::unique_ptr<ElevenLabsProvider> m_eleven;
    std::unique_ptr<PiperProvider>      m_piper;

    std::atomic<bool> m_piperReady{false};
    QString           m_piperExePath;
    QString           m_piperModelPathRu;
    QString           m_piperModelPathEn;

    static constexpr int TTS_VOLUME = 100;
    // Шаг опроса при воспроизведении: столько в худшем случае звучит
    // речь после команды «замолчи».
    static constexpr int CANCEL_POLL_MS = 15;
};
