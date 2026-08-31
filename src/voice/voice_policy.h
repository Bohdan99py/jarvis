#pragma once
// ============================================================
// voice_policy.h — When JARVIS is allowed to speak
//
// Между «событие произошло» и «реплика ушла в синтез» стоит один
// вопрос: уместно ли сейчас вообще открывать рот. Ответов три:
//
//   Speak   — произнести
//   Notify  — показать в интерфейсе, промолчать
//   Silent  — не показывать и не произносить
//
// Решение складывается из двух вещей. Политика — что человек выбрал
// (её ставит активный режим, см. mode.json → system.voice). Контекст —
// что происходит вокруг: игра на весь экран, глубокая работа, ночь,
// человек сам говорит.
//
// Порядок именно такой: политика задаёт потолок, контекст может только
// понизить. Иначе «тихий режим» переставал бы что-то значить, стоило
// системе решить, что событие важное.
//
// Ровно одно исключение: Critical (перегрев, падение процесса) звучит
// при любом контексте. Замолчать его может только политика Silent — и
// тогда он всё равно доходит до человека как Notify.
// ============================================================

#include "speech_request.h"

#include <QDateTime>
#include <QMetaType>
#include <QMutex>
#include <QString>

enum class VoicePolicy {
    Silent,    // голос выключен; критическое уходит в уведомление
    Minimal,   // только важное и критическое
    Normal,    // обычная жизнь, контекст может приглушить
    Verbose    // говорить всё, что дали
};

enum class VoiceDecision {
    Silent,
    Notify,
    Speak
};

// ============================================================
//  VoiceContext — обстановка вокруг
//
//  Хранится как сырые наблюдения, а не как готовые выводы: «человек
//  сосредоточен» — это функция от категории и времени в ней, и считать
//  её надо в момент решения, а не в момент, когда пришёл сигнал.
// ============================================================

struct VoiceContext
{
    // Что человек делает прямо сейчас — категория из ActivityTracker
    // ("coding", "gaming", "art", "game_engine", "browsing", ...).
    QString   activityCategory;
    QDateTime activityStartedAt;

    // Последний раз, когда микрофон слышал речь пользователя.
    QDateTime lastUserSpeech;

    // Полноэкранное приложение / презентация и «не беспокоить» — оба
    // приходят от Windows (SHQueryUserNotificationState), не выдуманы.
    bool fullscreen    = false;
    bool doNotDisturb  = false;

    // Ручное «сейчас ночь». По часам ночь включается сама
    // (см. setNightHours), поэтому обычно флаг остаётся false.
    bool nightMode     = false;

    // --- Производные признаки ---

    bool isGaming() const { return activityCategory == QStringLiteral("gaming"); }

    // Глубокая работа: те же категории и тот же порог в 5 минут, что у
    // CuriosityEngine::evaluateAttention() — два разных ответа на вопрос
    // «человек занят?» в одной программе были бы хуже любого из них.
    bool isDeepFocus() const
    {
        if (!activityStartedAt.isValid())
            return false;
        const bool heavy = activityCategory == QStringLiteral("coding")
                        || activityCategory == QStringLiteral("art")
                        || activityCategory == QStringLiteral("game_engine");
        return heavy && activityStartedAt.secsTo(QDateTime::currentDateTime()) > 300;
    }

    // Пока человек говорит (и пару секунд после), перебивать его
    // фоновой болтовнёй нельзя — это ровно та грубость, которой не
    // прощают живому собеседнику.
    bool isUserSpeaking(int windowMs = 2000) const
    {
        return lastUserSpeech.isValid()
            && lastUserSpeech.msecsTo(QDateTime::currentDateTime()) < windowMs;
    }
};

// ============================================================
//  VoicePolicyManager
// ============================================================

class VoicePolicyManager
{
public:
    static VoicePolicyManager& instance();

    VoicePolicyManager(const VoicePolicyManager&)            = delete;
    VoicePolicyManager& operator=(const VoicePolicyManager&) = delete;

    // --- Политика (ставит активный режим) ---
    void        setPolicy(VoicePolicy p);
    VoicePolicy policy() const;

    static QString     policyName(VoicePolicy p);
    static VoicePolicy policyFromString(const QString& name, VoicePolicy fallback);

    // --- Контекст (заполняют наблюдатели) ---
    void setActivity(const QString& category);
    void noteUserSpeech();
    void setNightMode(bool on);
    void setDoNotDisturb(bool on);
    void setFullscreen(bool on);

    // Опрашивает Windows: полноэкранное приложение, презентация, режим
    // «не беспокоить». Зовётся с таймера из GUI-потока.
    void refreshSystemState();

    VoiceContext context() const;

    // --- Решение ---
    VoiceDecision decide(const SpeechRequest& req) const;

    // Ночью реплика не отменяется, а произносится тише и медленнее.
    // Критическую не трогает: перегрев в три часа ночи должен разбудить.
    void applyContextStyle(SpeechRequest& req) const;

    // Часы ночного режима. Выключается setNightHours(-1, -1).
    void setNightHours(int fromHour, int toHour);
    bool isNightNow() const;

private:
    VoicePolicyManager() = default;

    static VoiceDecision baseDecision(VoicePolicy policy, SpeechPriority priority);
    static VoiceDecision demote(VoiceDecision current, VoiceDecision floor);

    mutable QMutex m_mutex;

    VoicePolicy  m_policy = VoicePolicy::Normal;
    VoiceContext m_context;

    int m_nightFrom = 23;
    int m_nightTo   = 7;
};

// Решение уходит сигналом между потоками (см. speechSuppressed).
Q_DECLARE_METATYPE(VoiceDecision)
