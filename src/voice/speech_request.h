#pragma once
// ============================================================
// speech_request.h — Speech contract between JARVIS and the voice layer
//
// Контракт «что сказать» отделён от «как это произнести»: ядро
// описывает намерение (стиль, важность, источник), а конкретный
// провайдер — SAPI, Piper, позже ElevenLabs — сам решает, чем это
// намерение отобразить в параметры своего API.
//
// Обратное разделение (LLM пишет теги вроде [concerned] прямо в
// текст) не годится: тогда промпт становится пультом звукорежиссёра,
// а сменить движок нельзя без переписывания промптов.
// ============================================================

#include <QMetaType>
#include <QString>

// Манера подачи. Влияет на темп, паузы и громкость, но не на выбор
// голоса — голос у JARVIS один.
enum class SpeechStyle {
    Neutral,          // ровно, по умолчанию
    Conversational,   // ответ в диалоге, чуть свободнее
    Informative,      // короткий факт: «ESP32 подключён к COM5»
    Warning,          // медленнее и весомее
    Critical,         // коротко, чётко, громче
    Whisper           // ночной/тихий режим
};

// Место в очереди. Critical обгоняет всё и вытесняет текущую реплику,
// Background может быть выброшен, если протух за время ожидания.
enum class SpeechPriority {
    Background = 0,
    Normal     = 1,
    Important  = 2,
    Critical   = 3
};

// Кто инициировал речь. Нужен политике голоса (Silent/Minimal/Normal/
// Verbose) и телеметрии: «почему он вообще заговорил».
enum class SpeechSource {
    Assistant,      // ответ модели пользователю
    System,         // состояние самого JARVIS
    Event,          // внешнее событие (устройство, сборка, датчик)
    Notification,   // напоминание, проактивная реплика
    Workflow,       // шаг задачи/агента
    UserCommand     // подтверждение команды пользователя
};

// Почему речь оборвалась. Без причины система не может отличить
// «пользователь перебил» от «вытеснено критическим событием», а это
// разные последствия: в первом случае реплику не возобновляют, во
// втором — можно вернуться.
enum class CancelReason {
    UserBargeIn,    // пользователь заговорил поверх
    UserStop,       // явная команда «стоп» / кнопка
    Preempted,      // вытеснено более важной репликой
    PolicyChanged,  // выключили звук, включился тихий режим
    Shutdown
};

// ============================================================
//  SpeechRequest
// ============================================================

struct SpeechRequest
{
    QString text;
    QString language;   // "ru" | "en"; пусто — определить по тексту

    SpeechStyle    style    = SpeechStyle::Neutral;
    SpeechPriority priority = SpeechPriority::Normal;
    SpeechSource   source   = SpeechSource::Assistant;

    // Можно ли оборвать эту реплику (перебиванием или вытеснением).
    bool interruptible = true;

    // Годится ли результат синтеза для дискового кэша. Ответ модели —
    // нет (текст каждый раз новый), системная реплика — да, она
    // повторяется сотни раз за месяц.
    bool cacheable = false;

    // Через сколько реплика теряет смысл, если не успела прозвучать.
    // 0 — не протухает.
    int expiresAfterMs = 0;

    // Склейка всплесков: реплики одной группы, пришедшие подряд,
    // произносятся одной (см. speech_aggregator.h). Пусто — говорить
    // сразу; ответ ассистента задерживать нельзя, поэтому по умолчанию
    // группы нет.
    QString aggregateGroup;
    int     aggregateWindowMs = 1500;

    bool isValid() const { return !text.trimmed().isEmpty(); }

    // Удобная форма: SpeechRequest::systemEvent(…).inGroup("device")
    SpeechRequest& inGroup(const QString& group, int windowMs = 1500)
    {
        aggregateGroup    = group;
        aggregateWindowMs = windowMs;
        return *this;
    }

    // ---- Готовые формы под типовые случаи ----

    // Ответ модели пользователю.
    static SpeechRequest assistant(const QString& text,
                                    const QString& language = QString())
    {
        SpeechRequest r;
        r.text     = text;
        r.language = language;
        r.style    = SpeechStyle::Conversational;
        r.priority = SpeechPriority::Normal;
        r.source   = SpeechSource::Assistant;
        return r;
    }

    // Короткий системный факт. Повторяется — значит кэшируется.
    static SpeechRequest systemEvent(const QString& text,
                                      SpeechSource source = SpeechSource::Event)
    {
        SpeechRequest r;
        r.text      = text;
        r.style     = SpeechStyle::Informative;
        r.priority  = SpeechPriority::Normal;
        r.source    = source;
        r.cacheable = true;
        return r;
    }

    // Предупреждение: сборка с ошибками, место на диске, отвал сети.
    static SpeechRequest warning(const QString& text,
                                  SpeechSource source = SpeechSource::Event)
    {
        SpeechRequest r;
        r.text      = text;
        r.style     = SpeechStyle::Warning;
        r.priority  = SpeechPriority::Important;
        r.source    = source;
        r.cacheable = true;
        return r;
    }

    // Критическое: перегрев, падение процесса. Не прерывается ничем,
    // включая покашливание пользователя.
    static SpeechRequest critical(const QString& text,
                                   SpeechSource source = SpeechSource::Event)
    {
        SpeechRequest r;
        r.text          = text;
        r.style         = SpeechStyle::Critical;
        r.priority      = SpeechPriority::Critical;
        r.source        = source;
        r.interruptible = false;
        r.cacheable     = true;
        return r;
    }

    // Фоновая болтовня: напоминания, «пора сделать перерыв».
    // Протухает — через минуту напоминание про перерыв уже не новость.
    static SpeechRequest background(const QString& text,
                                     SpeechSource source = SpeechSource::Notification)
    {
        SpeechRequest r;
        r.text           = text;
        r.style          = SpeechStyle::Conversational;
        r.priority       = SpeechPriority::Background;
        r.source         = source;
        r.expiresAfterMs = 60000;
        return r;
    }
};

// ============================================================
//  StyleParams — единственное место, где стиль превращается в цифры
//
//  Значения нейтральны к движку: темп как множитель длительности
//  (Piper --length_scale), громкость в процентах, пауза между
//  предложениями в секундах. Провайдер переводит их в своё.
// ============================================================

struct StyleParams
{
    double lengthScale;      // 1.0 — обычный темп, >1 — медленнее
    double sentenceSilence;  // пауза между предложениями, сек
    int    volumePercent;    // 0..100
};

inline StyleParams styleParams(SpeechStyle style)
{
    switch (style) {
    case SpeechStyle::Neutral:        return {1.00, 0.20, 100};
    // Разговор чуть быстрее и с более короткими паузами — так живая
    // речь и отличается от диктора.
    case SpeechStyle::Conversational: return {0.95, 0.15, 100};
    case SpeechStyle::Informative:    return {0.92, 0.18, 100};
    // Предупреждение и критическое — медленнее и с паузой: у человека
    // должно быть время понять, что случилось.
    case SpeechStyle::Warning:        return {1.10, 0.35, 100};
    case SpeechStyle::Critical:       return {1.15, 0.40, 100};
    case SpeechStyle::Whisper:        return {1.05, 0.25,  40};
    }
    return {1.00, 0.20, 100};
}

// SAPI умеет только целочисленный rate -10..10. Отображаем множитель
// длительности обратно в него: медленнее → отрицательный rate.
inline int sapiRateForStyle(SpeechStyle style)
{
    const double scale = styleParams(style).lengthScale;
    if (scale >= 1.14) return -2;
    if (scale >= 1.05) return -1;
    if (scale <= 0.93) return  2;
    if (scale <= 0.98) return  1;
    return 1;   // прежнее поведение по умолчанию (TTS_RATE)
}

// Сигналы с этими типами уходят между потоками (say() зовут из
// сетевых), поэтому они должны быть известны системе метатипов.
Q_DECLARE_METATYPE(SpeechRequest)
Q_DECLARE_METATYPE(CancelReason)
