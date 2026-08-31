#pragma once
// -------------------------------------------------------
// event_feed.h — Лента событий
//
// Смысл не в том, чтобы показывать всё, что происходит, —
// это и есть шум. Смысл в том, чтобы показывать РЕДКО:
//
//   плохо:  CPU 23%   (каждые 10 секунд)
//   хорошо: Загрузка CPU выше 90% уже 4 минуты — UnrealEditor
//
// Поэтому лента устроена вокруг двух вещей:
//   1. дедупликация — повторное событие с тем же ключом в
//      пределах окна не создаёт строку, а увеличивает счётчик;
//   2. уровень — только Warning и Error всплывают тостом,
//      остальное копится и ждёт, когда на него посмотрят.
//
// Тосты (NotificationManager) — про «сейчас», лента — про
// «что было». Всплывающее окно нельзя пролистать, а ленту
// нельзя не заметить только потому, что отвернулся.
// -------------------------------------------------------

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

enum class EventLevel {
    Info    = 0,   // просто факт
    Good    = 1,   // что-то получилось
    Warning = 2,   // стоит посмотреть
    Error   = 3    // сломалось
};

QString eventLevelName(EventLevel level);

struct FeedEvent
{
    qint64     id = 0;
    QDateTime  at;
    QString    source;     // SYSTEM | AGENT | WORKFLOW | PROFILE | DEVICE | BUILD
    EventLevel level = EventLevel::Info;
    QString    title;      // одна строка, без переносов
    QString    detail;     // необязательные подробности
    QString    dedupKey;   // совпал — значит это то же самое событие
    int        count = 1;  // сколько раз повторилось

    QString timeText() const { return at.toString(QStringLiteral("HH:mm")); }
};

// ============================================================
//  EventFeed
// ============================================================
class EventFeed : public QObject
{
    Q_OBJECT

public:
    // Синглтон намеренно: постить события должны все подсистемы —
    // от агента до наблюдателя за железом, — и протаскивать указатель
    // через полпроекта ради этого не стоит. Так же живут
    // NotificationManager и DatabaseManager.
    static EventFeed& instance();

    void post(const QString& source,
              EventLevel level,
              const QString& title,
              const QString& detail = QString(),
              const QString& dedupKey = QString());

    QVector<FeedEvent> events(int limit = 300) const;
    int  size() const  { return m_events.size(); }
    int  unread() const { return m_unread; }
    void markAllRead();
    void clear();

    // Сводка для системного промпта: чтобы на вопрос «что случилось,
    // пока меня не было» модель отвечала без вызова инструментов.
    QString summaryForModel(int maxEvents = 8) const;

signals:
    void eventPosted(const FeedEvent& event);
    void eventUpdated(const FeedEvent& event);   // вырос счётчик дедупликации
    void changed();

private:
    EventFeed() = default;

    QVector<FeedEvent> m_events;   // новые в конце
    qint64             m_nextId = 1;
    int                m_unread = 0;

    // Повтор в пределах окна — это тот же самый случай, а не новый.
    // Пять минут: достаточно, чтобы схлопнуть серию срабатываний
    // одного наблюдателя, и мало, чтобы склеить два разных эпизода.
    static constexpr int kDedupWindowSec = 300;
    static constexpr int kMaxEvents      = 500;
};
