#pragma once
// ============================================================
// passive_listener.h — J.A.R.V.I.S. пассивная запись голоса
//
// Работает в фоне (трей), записывает голос пользователя через VAD,
// транскрибирует через Whisper.cpp, сохраняет в voice_journal.
// Brain периодически обрабатывает журнал и создаёт training pairs.
//
// Поток данных:
//   Микрофон → VAD (RMS) → PCM буфер → Whisper → voice_journal
//   → Brain → training_logs → export .jsonl
//
// Еженедельная очистка: сырые записи удаляются после обработки.
// ============================================================

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QAudioSource>
#include <QAudioFormat>
#include <QString>
#include <QDateTime>
#include <atomic>
#include <vector>



// ============================================================
//  Настройки пассивного слушателя
// ============================================================

struct PassiveListenerConfig {
    // Папка для временных WAV файлов (настраивается пользователем)
    QString datasetPath;

    // VAD порог — тише этого считается тишиной
    float   silenceDbThreshold     = -42.0f;

    // Тишина после речи (мс) — когда считаем фразу законченной
    int     silenceAfterSpeechMs   = 1200;

    // Минимальная длина фразы для записи (мс)
    int     minSpeechMs            = 500;

    // Максимальная длина одной записи (мс) — защита от бесконечной записи
    int     maxRecordingMs         = 15000;

    // Пути к моделям Vosk (те же что у VoiceInput)
    QString modelPathRu = QStringLiteral("redist/vosk/model-ru");
    QString modelPathEn = QStringLiteral("redist/vosk/model-en");

    // Язык транскрипции
    QString language               = QStringLiteral("auto");

    // Еженедельная очистка — удалять сырые записи старше N дней
    int     cleanupAfterDays       = 7;

    // Максимальный размер датасета (МБ) — защита от переполнения диска
    int     maxDatasetMb           = 200;
};

// ============================================================
//  VoiceJournalEntry — одна транскрибированная фраза
// ============================================================

struct VoiceJournalEntry {
    qint64    id           = 0;
    qint64    userId       = 1;
    QString   transcript;          // что сказал пользователь
    QString   language;            // "ru" | "en"
    float     confidence   = 0.0f; // уверенность Whisper
    QString   audioFile;           // путь к WAV файлу (опционально)
    bool      processed    = false; // обработано Brain → training_logs
    QDateTime createdAt;
};

// ============================================================
//  PassiveRecorder — захват аудио (главный поток)
// ============================================================

class PassiveRecorder : public QObject
{
    Q_OBJECT
public:
    explicit PassiveRecorder(QObject* parent = nullptr);
    ~PassiveRecorder() override;

    bool start(const PassiveListenerConfig& config);
    void stop();
    bool isRecording() const { return m_recording.load(); }

signals:
    void speechCaptured(QByteArray pcmData, QDateTime capturedAt);
    void error(const QString& message);

private slots:
    void onAudioDataReady();
    void onSilenceTimeout();

private:
    float    computeRmsDb(const QByteArray& data) const;

    // Жёсткий лимит буфера: 15 сек @ 16kHz mono 16-bit = 480000 байт
    static constexpr int MAX_BUFFER_BYTES = 15 * 16000 * 2;

    QAudioSource*     m_audioSource  = nullptr;
    QIODevice*        m_audioDevice  = nullptr;
    QTimer*           m_silenceTimer = nullptr;
    QByteArray        m_buffer;
    bool              m_speaking     = false;
    std::atomic<bool> m_recording    {false};
    PassiveListenerConfig m_config;
    QAudioFormat      m_format;
};

// ============================================================
//  PassiveTranscriber — Whisper транскрипция (отдельный поток)
// ============================================================

class PassiveTranscriber : public QObject
{
    Q_OBJECT
public:
    explicit PassiveTranscriber(QObject* parent = nullptr);
    ~PassiveTranscriber() override;

public slots:
    void loadModel(const QString& modelPath);   // legacy compat — no-op
    void loadModels(const QString& modelPathRu, const QString& modelPathEn); // legacy compat — no-op
    // NEW: use shared VoskModel* from VoskWorker (no duplicate loading)
    void setSharedModels(void* modelRu, void* modelEn);
    void transcribe(QByteArray pcmData, QDateTime capturedAt, QString language);

signals:
    void modelLoaded(bool ok);
    void transcribed(VoiceJournalEntry entry);
    void error(const QString& message);

private:
    // Shared VoskModel pointers — NOT owned, do NOT free
    void* m_modelRu = nullptr;
    void* m_modelEn = nullptr;
    // Own recognizers (cheap, ~few KB each)
    void* m_recoRu  = nullptr;
    void* m_recoEn  = nullptr;
    bool  m_loaded  = false;
    bool  m_ownsModels = false;  // false when using shared models
};

// ============================================================
//  PassiveListener — главный контроллер (живёт в UI потоке)
// ============================================================

class PassiveListener : public QObject
{
    Q_OBJECT
public:
    explicit PassiveListener(QObject* parent = nullptr);
    ~PassiveListener() override;

    void initialize(const PassiveListenerConfig& config);
    void initializeWithSharedModels(void* modelRu, void* modelEn,
                                    const PassiveListenerConfig& config);
    void setConfig(const PassiveListenerConfig& config);
    const PassiveListenerConfig& config() const { return m_config; }

    void startListening();
    void stopListening();
    bool isListening() const;

    // Запустить обработку накопленных записей через Brain → training_logs
    void processJournal();

    // Сохранить голосовую команду + ответ как training pair
    void addVoiceCommandPair(const QString& command, const QString& response,
                             const QString& language = QString());

    // Статистика
    int journalEntriesCount() const;
    int processedEntriesCount() const;

    // Еженедельная очистка
    void scheduleWeeklyCleanup();

signals:
    void ready();
    void listeningStarted();
    void listeningStopped();
    void entrySaved(const VoiceJournalEntry& entry);  // новая фраза сохранена
    void journalProcessed(int pairsCreated);          // пары созданы
    void weeklyCleanupDone(int entriesDeleted);
    void errorOccurred(const QString& message);
    void statusChanged(const QString& status);        // для иконки трея

    // внутренние
    void requestLoadModel(const QString& path);   // совместимость (no-op)
    void requestSetSharedModels(void* modelRu, void* modelEn);
    void requestTranscribe(QByteArray pcmData, QDateTime capturedAt, QString lang);

private slots:
    void onModelLoaded(bool ok);
    void onTranscribed(const VoiceJournalEntry& entry);
    void onSpeechCaptured(QByteArray pcmData, QDateTime capturedAt);
    void onWeeklyCleanupTimer();

private:
    void saveToJournal(const VoiceJournalEntry& entry);
    void ensureDatasetDir();

    PassiveListenerConfig m_config;

    QThread*              m_thread      = nullptr;
    PassiveTranscriber*   m_transcriber = nullptr;
    PassiveRecorder*      m_recorder    = nullptr;
    QTimer*               m_cleanupTimer = nullptr;

    bool                  m_initialized = false;
    bool                  m_listening   = false;
    bool                  m_autoStart   = false;  // автостарт после загрузки моделей
};
