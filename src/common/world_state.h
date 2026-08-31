#pragma once
// -------------------------------------------------------
// world_state.h — Одно состояние мира вместо двадцати рассказчиков
//
// До этого файла состояние JARVIS существовало только как текст.
// SessionMemory::buildSystemPrompt() склеивал два десятка
// независимых блоков — ON SCREEN RIGHT NOW, RECENT EVENTS,
// CURRENT ACTIVITY, KNOWLEDGE BASE, MEMORY STREAM, — и почти
// каждый пересобирался заново на каждом проходе processCommand.
// Свести их в цельную картину было работой модели: код отдавал
// ей двадцать рассказов и надеялся, что она разберётся.
//
// WorldState — то место, где картина собирается один раз.
// Не база данных: ничего не пишется на диск, ничего не
// опрашивается по запросу. Живой снимок в памяти, который
// поддерживают наблюдатели, уже работающие в проекте:
//
//     ContextTracker  (1.5 с)  -> observeForeground()
//     ActivityTracker (15 с)   -> observeActivity()
//     SystemMonitor   (1 с)    -> observeSystem()
//     DeviceHub                -> observeDevices()
//     Jarvis                   -> observeCognition()
//
// Направление важно. EventFeed НЕ является входом сюда: это
// курируемая человеческая лента с дедупликацией по окну в пять
// минут, и смена фокуса раз в полторы секунды либо превратит её
// в шум, либо будет ею проглочена. Наоборот — лента подписана на
// significantChange() и получает уже отобранные переходы.
// WorldState про «что есть», EventFeed про «что было».
//
// Единственное место, где состояние снова становится текстом, —
// toPromptContext(). Оно ЗАМЕНЯЕТ прежние блоки, а не добавляется
// к ним двадцать первым: два описания одного состояния неизбежно
// разойдутся, и разойдутся молча.
// -------------------------------------------------------

#include <QDateTime>
#include <QObject>
#include <QPair>
#include <QReadWriteLock>
#include <QString>
#include <QStringList>
#include <QVector>

// ============================================================
//  Observed<T> — значение вместе с тем, откуда оно взялось
// ============================================================
//
// Разница между «CPU 92%» и «CPU 92%, замерено восемнадцать минут
// назад» — это разница между уверенным враньём и честным ответом.
// Поэтому в состоянии не лежит ни одного голого значения.
//
// Два числа, и они означают разное:
//
//   certainty  — насколько был уверен САМ наблюдатель в момент
//                замера. У счётчика загрузки это 1.0: он не
//                угадывает. У классификатора активности — 0.84:
//                «похоже, человек пишет код».
//
//   confidence() — сколько этому стоит верить СЕЙЧАС: certainty,
//                ослабленная возрастом относительно ttl.
//
// Протухшее значение не стирается. Последнее известное состояние
// с честно упавшим доверием полезнее пустоты — на нём и стоит
// ответ «подтверждения не было уже восемнадцать минут».
template <typename T>
struct Observed
{
    T         value{};
    QString   source;                 // кто это увидел: "ContextTracker", "SystemMonitor"
    QDateTime observedAt;
    float     certainty = 1.0f;       // уверенность наблюдателя в момент замера
    int       ttlSeconds = 60;        // после этого возраста значение считается протухшим

    bool isKnown() const { return observedAt.isValid(); }

    int ageSeconds() const
    {
        return observedAt.isValid()
                   ? int(observedAt.secsTo(QDateTime::currentDateTime()))
                   : -1;
    }

    bool isStale() const
    {
        return !observedAt.isValid() || ageSeconds() > ttlSeconds;
    }

    // certainty, ослабленная возрастом. Внутри ttl — не ослабляется
    // вовсе (значение свежее, наблюдателю верим как есть). Дальше
    // спадает как 1/(1+x), где x — во сколько раз возраст превысил
    // ttl: через один ttl сверху остаётся половина, через три —
    // четверть. Ноль не достигается никогда: «очень старое» и
    // «неизвестное» — разные вещи, и путать их нельзя.
    float confidence() const
    {
        if (!isKnown())   return 0.0f;
        if (!isStale())   return certainty;
        const float over = float(ageSeconds() - ttlSeconds) / float(qMax(1, ttlSeconds));
        return certainty / (1.0f + over);
    }

    void set(const T& v, const QString& src, float cert = 1.0f, int ttl = 60)
    {
        value      = v;
        source     = src;
        certainty  = cert;
        ttlSeconds = ttl;
        observedAt = QDateTime::currentDateTime();
    }

    // "17% (SystemMonitor, 2 с назад)" — для toPromptContext и отладки.
    // Возраст приписывается только когда значение уже протухло: у
    // свежего он ничего не добавляет, кроме длины промпта.
    QString describe(const QString& rendered) const;
};

// ============================================================
//  Домены состояния
// ============================================================
//
// Разделены не по источникам, а по тому, о чём вопрос. Загрузку
// процессора наполняет SystemMonitor, а список процессов — он же,
// но лежат они в разных доменах, потому что «машине тяжело» и
// «Rider запущен» — ответы на разные вопросы.

struct SystemState
{
    Observed<int>     cpuPercent;
    Observed<int>     ramPercent;
    Observed<double>  netKbps;
    Observed<qint64>  uptimeSeconds;
    Observed<bool>    online;
    // Корень -> процент свободного места. Отдельным Observed на
    // весь список: диски опрашиваются одним проходом.
    Observed<QVector<QPair<QString, int>>> disksFreePercent;
};

struct ActivityState
{
    Observed<QString> category;        // "coding" | "art" | "browsing" | ...
    Observed<QString> role;            // ActivityTracker::detectUserRole()
    Observed<int>     durationSeconds; // сколько длится текущая активность
    Observed<float>   focus;           // 0..1, из BehavioralMetrics
};

struct ApplicationState
{
    Observed<QString>     foregroundApp;     // "Rider"
    Observed<QString>     foregroundProcess; // "rider64.exe"
    Observed<QString>     windowTitle;
    Observed<QString>     currentFile;       // если окно редактора
    Observed<QString>     projectName;
    Observed<QString>     projectRoot;
    Observed<QStringList> runningApps;

    // Чужое окно, бывшее под курсором до перехода фокуса к JARVIS —
    // то самое «здесь» из вопроса «почему здесь ошибка?».
    // ContextTracker уже отличает его от нашего собственного окна.
    Observed<QString>     lastForeignApp;
};

struct UserState
{
    Observed<QString>   name;
    Observed<qint64>    userId;
    Observed<bool>      present;        // не отошёл ли от машины
    Observed<int>       idleSeconds;
    Observed<QString>   emotion;        // из ReflectionEngine::EmotionTrend
};

struct DeviceState
{
    struct Entry {
        QString id;
        QString name;
        QString kind;        // pc | esp32 | phone | peer | bluetooth
        QString status;      // DeviceInfo::statusName()
        QString detail;      // "COM5", "192.168.1.14"
    };
    Observed<QVector<Entry>> devices;

    bool    isConnected(const QString& idOrName) const;
    QString detailOf(const QString& idOrName) const;
};

// Что JARVIS знает о собственном внутреннем состоянии. Не о
// способностях — те живут в SystemManifest и пробуются там же, —
// а о том, что прямо сейчас происходит у него в голове.
struct CognitiveState
{
    Observed<int>     unresolvedDoubts;    // SelfJournal
    Observed<int>     synapseNodes;
    Observed<int>     synapseEdges;
    Observed<int>     cachedCases;
    Observed<bool>    externalDriveOnline; // MemoryConsolidation Tier 1
    Observed<QString> activeMode;          // ModeManager
};

// ============================================================
//  WorldSnapshot — состояние, вырванное из времени
// ============================================================
//
// Возвращается по значению намеренно. Ссылка на живое состояние
// в проекте с девятью десятками мест QThread/QtConcurrent — это
// гонка, до которой остаётся дожить, а не поймать.
struct WorldSnapshot
{
    SystemState       system;
    ActivityState     activity;
    ApplicationState  applications;
    UserState         user;
    DeviceState       devices;
    CognitiveState    cognition;
    QDateTime         takenAt;

    // Единственное место, где состояние превращается в текст.
    // maxAgeSeconds: всё, что старше, в промпт не попадает вовсе —
    // упавшее доверие честнее отсутствия, но не бесконечно.
    QString toPromptContext(int maxAgeSeconds = 900) const;

    // Человеческий вид — для панели состояния и инструмента get_world.
    QString toHumanText() const;
};

// ============================================================
//  WorldState
// ============================================================
class WorldState : public QObject
{
    Q_OBJECT

public:
    // Синглтон по той же причине, что EventFeed и DatabaseManager:
    // писать сюда должны наблюдатели из четырёх разных библиотек,
    // и протаскивать указатель через полпроекта ради этого не стоит.
    static WorldState& instance();

    WorldState(const WorldState&)            = delete;
    WorldState& operator=(const WorldState&) = delete;

    // ── Чтение ──────────────────────────────────────────────
    // Всё копиями. Держать const& на поле, которое перепишет
    // ContextTracker через 200 мс из своего таймера, нельзя.
    WorldSnapshot     snapshot() const;

    SystemState       system() const;
    ActivityState     activity() const;
    ApplicationState  applications() const;
    UserState         user() const;
    DeviceState       devices() const;
    CognitiveState    cognition() const;

    // Прямые ответы на частые вопросы — чтобы «запущен ли Rider»
    // не требовало снимка целиком.
    bool    isRunning(const QString& appNameOrProcess) const;
    QString currentActivity() const;
    QString currentProject() const;

    // ── Наблюдение ──────────────────────────────────────────
    // Типизированные методы вместо общего apply(const Event&):
    // обобщённое событие — это enum плюс QVariant плюс switch,
    // то есть потеря проверки типов ровно там, где мы её наводим.
    // Реплея событий нигде не требуется, значит платить за
    // обобщение нечем.
    //
    // Каждый метод сам решает, значимо ли изменение, и только
    // тогда испускает significantChange(). Наблюдатель, тикающий
    // раз в полторы секунды, обязан иметь право звать это на
    // каждом тике, ничего не разбудив.
    void observeForeground(const QString& app, const QString& process,
                           const QString& windowTitle,
                           const QString& file, const QString& project);
    void observeRunningApps(const QStringList& apps);
    void observeActivity(const QString& category, int durationSeconds,
                         float certainty = 1.0f);
    void observeRole(const QString& role, float certainty);
    void observeFocus(float focus, float certainty);
    void observeSystem(int cpuPercent, int ramPercent,
                       double netKbps, qint64 uptimeSeconds);
    void observeDisks(const QVector<QPair<QString, int>>& freePercentByRoot);
    void observeNetwork(bool online);
    void observeUser(qint64 userId, const QString& name);
    void observePresence(bool present, int idleSeconds);
    void observeEmotion(const QString& emotion, float certainty);
    void observeDevices(const QVector<DeviceState::Entry>& devices);
    void observeCognition(int doubts, int synapseNodes, int synapseEdges,
                          int cachedCases, bool externalDrive,
                          const QString& activeMode);

signals:
    // Что-то заметно изменилось. domain — "applications" | "system" |
    // "activity" | "devices" | "user" | "cognition".
    //
    // На это подписан EventFeed: значимые переходы становятся
    // строками ленты здесь, а не в двадцати местах, каждое из
    // которых решало это по-своему.
    void significantChange(const QString& domain, const QString& what);

    // Любое обновление, включая незначимое — для панели состояния,
    // которой нужно живое число, а не событие.
    void updated(const QString& domain);

private:
    explicit WorldState(QObject* parent = nullptr);

    void note(const QString& domain, const QString& what);   // significantChange + updated
    void touch(const QString& domain);                        // только updated

    mutable QReadWriteLock m_lock;

    SystemState      m_system;
    ActivityState    m_activity;
    ApplicationState m_applications;
    UserState        m_user;
    DeviceState      m_devices;
    CognitiveState   m_cognition;

    // ── Пороги значимости ───────────────────────────────────
    // Загрузка процессора дрожит постоянно; значимо не любое
    // изменение, а заметное. Пороги здесь, а не у наблюдателей:
    // иначе каждый заведёт свой и они разойдутся.
    static constexpr int kCpuSignificantDelta = 15;   // проценты
    static constexpr int kRamSignificantDelta = 10;

    // TTL по доменам: как быстро устаревает знание.
    static constexpr int kTtlForeground = 30;    // окно меняется часто
    static constexpr int kTtlSystem     = 10;    // счётчики живут секунды
    static constexpr int kTtlActivity   = 120;   // категория держится долго
    static constexpr int kTtlDevices    = 120;
    static constexpr int kTtlUser       = 3600;  // имя не протухает
    static constexpr int kTtlCognition  = 300;
};
