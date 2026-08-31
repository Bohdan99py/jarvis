#pragma once
// ============================================================
// speech_cache.h — On-disk cache of synthesized speech
//
// Системные реплики повторяются сотни раз: «Сборка завершилась с
// ошибкой» звучит столько же раз, сколько ломается сборка. Синтезировать
// их заново каждый раз — значит платить временем (запуск piper.exe), а
// у сетевого провайдера ещё и деньгами за каждый символ.
//
// Ключ — хеш от всего, что влияет на звук: провайдер, голос, язык,
// параметры подачи, текст. Меняется голос или темп — меняется ключ,
// старый файл просто перестаёт находиться и уходит при чистке.
//
// Кэшируются только реплики с cacheable = true (см. SpeechRequest):
// ответ модели каждый раз новый, складывать его на диск бессмысленно.
// ============================================================

#include "speech_request.h"

#include <QString>
#include <QMutex>
#include <atomic>

class SpeechCache
{
public:
    static SpeechCache& instance();

    SpeechCache(const SpeechCache&)            = delete;
    SpeechCache& operator=(const SpeechCache&) = delete;

    // Ключ считается по уже очищенному тексту — тому самому, который
    // уйдёт в синтез (см. VoiceSynthesisManager::sanitizeForSpeech).
    static QString makeKey(const QString& provider,
                           const QString& voiceId,
                           const QString& language,
                           const StyleParams& params,
                           const QString& text);

    // Путь к готовому файлу или пустая строка. При попадании отметка
    // времени обновляется — по ней чистка понимает, что запись живая.
    QString lookup(const QString& key);

    // Переносит готовый WAV в кэш и возвращает его новый путь. Пустая
    // строка — перенести не удалось; вызывающий играет исходный файл.
    QString store(const QString& key, const QString& wavPath);

    bool contains(const QString& key) const;

    // Диагностика для панели здоровья: сколько занято и сколько записей.
    qint64 totalBytes();
    int    entryCount() const;

    void clear();

private:
    SpeechCache() = default;

    QString cacheDir() const;
    QString pathForKey(const QString& key) const;

    // Держит папку в пределах лимита, удаляя самые давние записи.
    // Вызывается после записи, под тем же мьютексом.
    void pruneLocked();

    mutable QMutex m_mutex;

    // Суммарный размер считается один раз при первом обращении, дальше
    // ведётся приращениями: сканировать папку на каждой реплике — ровно
    // та работа, ради экономии которой кэш и заводился.
    qint64 m_totalBytes = -1;

    // Лимит и цель после чистки. 150 МБ — это тысячи реплик Piper;
    // до него в обычной жизни не доходит, он от накопления мусора при
    // смене голоса, а не от нормальной работы.
    static constexpr qint64 MAX_BYTES    = 150LL * 1024 * 1024;
    static constexpr double PRUNE_TARGET = 0.8;
};
