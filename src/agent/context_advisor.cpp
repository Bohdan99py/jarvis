// -------------------------------------------------------
// context_advisor.cpp — см. context_advisor.h
// -------------------------------------------------------

#include "context_advisor.h"

#include "context_tracker.h"
#include "database_manager.h"
#include "event_feed.h"
#include "jarvis_state.h"
#include "notification_manager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QDebug>

namespace {

// Окна, про которые предлагать нечего. Рабочий стол и панель задач —
// это не занятие, а промежуток между занятиями.
bool isBoringProcess(const QString& process)
{
    static const QStringList boring = {
        QStringLiteral("explorer.exe"),
        QStringLiteral("searchhost.exe"),
        QStringLiteral("shellexperiencehost.exe"),
        QStringLiteral("startmenuexperiencehost.exe"),
        QStringLiteral("applicationframehost.exe"),
        QStringLiteral("textinputhost.exe"),
        QStringLiteral("lockapp.exe"),
        QStringLiteral("jarvis.exe"),
    };
    return boring.contains(process.toLower());
}

// Заголовок вроде «Новая вкладка» или «—» ничего не говорит о занятии.
bool isBoringLabel(const QString& label)
{
    if (label.size() < 4)
        return true;

    static const QStringList boring = {
        QStringLiteral("новая вкладка"), QStringLiteral("new tab"),
        QStringLiteral("пустая страница"), QStringLiteral("blank page"),
        QStringLiteral("проводник"),      QStringLiteral("file explorer"),
    };
    const QString lower = label.toLower();
    for (const QString& b : boring) {
        if (lower.contains(b))
            return true;
    }
    return false;
}

} // namespace

ContextAdvisor::ContextAdvisor(ContextTracker* tracker, QObject* parent)
    : QObject(parent)
    , m_tracker(tracker)
{
    loadHistory();

    m_timer = new QTimer(this);
    m_timer->setInterval(kTickMs);
    connect(m_timer, &QTimer::timeout, this, &ContextAdvisor::tick);
    if (m_enabled)
        m_timer->start();
}

// «Раз в сутки на одну тему» должно переживать перезапуск. Пока история
// жила только в памяти, каждый рестарт JARVIS обнулял её — и человек
// получал то же самое предложение по той же странице заново. Для
// резидентной программы, которая перезапускается чаще, чем кажется,
// это превращало правило в его отсутствие.
void ContextAdvisor::loadHistory()
{
    const QString raw = DatabaseManager::instance()
                            .getConfig(QStringLiteral("context_advisor_seen"))
                            .toString();
    if (raw.isEmpty())
        return;

    const QJsonObject root = QJsonDocument::fromJson(raw.toUtf8()).object();
    const QDateTime now = QDateTime::currentDateTime();

    const QJsonObject subjects = root.value(QStringLiteral("subjects")).toObject();
    for (auto it = subjects.constBegin(); it != subjects.constEnd(); ++it) {
        const QDateTime when = QDateTime::fromString(it.value().toString(), Qt::ISODate);
        if (!when.isValid())
            continue;
        // Просроченное не тащим: иначе запись растёт вечно.
        if (when.secsTo(now) >= kPerSubjectHours * 3600)
            continue;
        m_lastOfferByKey.insert(it.key(), when);
    }

    // Пауза между предложениями и дневной потолок — такие же правила и
    // так же бессмысленны, если рестарт их обнуляет. Перезапуск не
    // должен быть способом получить право заговорить снова.
    m_lastOfferAt = QDateTime::fromString(root.value(QStringLiteral("lastOfferAt")).toString(),
                                          Qt::ISODate);
    m_countDate   = QDate::fromString(root.value(QStringLiteral("countDate")).toString(),
                                      Qt::ISODate);
    m_todayCount  = root.value(QStringLiteral("todayCount")).toInt();
}

void ContextAdvisor::saveHistory() const
{
    QJsonObject subjects;
    const QDateTime now = QDateTime::currentDateTime();

    for (auto it = m_lastOfferByKey.constBegin(); it != m_lastOfferByKey.constEnd(); ++it) {
        if (it.value().secsTo(now) >= kPerSubjectHours * 3600)
            continue;
        subjects.insert(it.key(), it.value().toString(Qt::ISODate));
    }

    QJsonObject root;
    root.insert(QStringLiteral("subjects"), subjects);
    if (m_lastOfferAt.isValid())
        root.insert(QStringLiteral("lastOfferAt"), m_lastOfferAt.toString(Qt::ISODate));
    if (m_countDate.isValid()) {
        root.insert(QStringLiteral("countDate"), m_countDate.toString(Qt::ISODate));
        root.insert(QStringLiteral("todayCount"), m_todayCount);
    }

    DatabaseManager::instance().setConfig(
        QStringLiteral("context_advisor_seen"),
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

void ContextAdvisor::setEnabled(bool on)
{
    if (m_enabled == on)
        return;

    m_enabled = on;
    if (on) {
        m_currentKey.clear();
        m_timer->start();
    } else {
        m_timer->stop();
    }
}

ContextAdvisor::Subject ContextAdvisor::currentSubject() const
{
    Subject s;
    if (!m_tracker)
        return s;

    const MachineContext ctx = m_tracker->snapshot();
    if (ctx.isEmpty() || isBoringProcess(ctx.processName))
        return s;

    if (!ctx.browserPage.isEmpty()) {
        s.kind  = QStringLiteral("browser");
        s.label = ctx.browserPage;
    } else if (!ctx.currentFile.isEmpty()) {
        s.kind  = QStringLiteral("editor");
        s.label = ctx.currentFile;
    } else {
        s.kind  = QStringLiteral("app");
        s.label = ctx.appName;
    }

    if (isBoringLabel(s.label))
        return Subject();

    s.key = s.kind + QLatin1Char(':') + s.label.toLower();
    return s;
}

// Пустая строка — можно говорить. Иначе причина молчания, и она нужна
// не для красоты: «советчик молчит» выглядит одинаково при выключённом
// режиме, зависшей фазе и исчерпанном лимите, а чинить это три разные
// вещи. Без причины в логе отличить их можно только отладчиком.
QString ContextAdvisor::silenceReason() const
{
    // Занят — значит человек уже чего-то ждёт от JARVIS. Влезать с
    // собственной инициативой в это ожидание нельзя.
    if (JarvisState::instance().isBusy())
        return QStringLiteral("занят: ") + JarvisState::instance().phaseName();

    // Режим сказал молчать (Focus, Gaming) — это и есть ответ.
    if (NotificationManager::instance().policy() == NotificationManager::Policy::None)
        return QStringLiteral("режим глушит уведомления");

    if (m_lastOfferAt.isValid()) {
        const int since = static_cast<int>(m_lastOfferAt.secsTo(QDateTime::currentDateTime()));
        if (since < kBetweenOffersMin * 60) {
            return QStringLiteral("пауза между предложениями: прошло %1 из %2 мин")
                .arg(since / 60).arg(kBetweenOffersMin);
        }
    }

    if (m_countDate == QDate::currentDate() && m_todayCount >= kMaxPerDay)
        return QStringLiteral("дневной потолок: %1").arg(kMaxPerDay);

    return QString();
}

void ContextAdvisor::tick()
{
    const Subject subject = currentSubject();

    // Сменилось занятие — начинаем отсчёт выдержки заново.
    if (subject.key != m_currentKey) {
        // Пишем только смену, а не каждый тик: иначе лог засоряется, а
        // диагностировать «почему он молчит» всё равно нужно именно по
        // тому, что он считает предметом и как часто тот меняется.
        qDebug() << "[Advisor] предмет:"
                 << (subject.key.isEmpty() ? QStringLiteral("(нет)") : subject.key);

        m_currentKey        = subject.key;
        m_currentSince      = QDateTime::currentDateTime();
        m_offeredForCurrent = false;
        m_silenceLogged     = false;
        return;
    }

    if (subject.isEmpty() || m_offeredForCurrent)
        return;

    if (m_currentSince.secsTo(QDateTime::currentDateTime()) < kDwellSec)
        return;

    // Про это уже предлагали — не сегодня.
    const QDateTime last = m_lastOfferByKey.value(subject.key);
    if (last.isValid() && last.secsTo(QDateTime::currentDateTime()) < kPerSubjectHours * 3600) {
        if (!m_silenceLogged) {
            m_silenceLogged = true;
            qDebug() << "[Advisor] молчу: про это уже предлагали сегодня";
        }
        return;
    }

    const QString reason = silenceReason();
    if (!reason.isEmpty()) {
        // Один раз на предмет: причина не меняется каждые 10 секунд, а
        // повторять её в лог до бесконечности — тот самый шум, от
        // которого сама эта подсистема и защищается.
        if (!m_silenceLogged) {
            m_silenceLogged = true;
            qDebug() << "[Advisor] молчу:" << reason;
        }
        return;
    }

    offer(subject);
}

void ContextAdvisor::offer(const Subject& subject)
{
    const QDateTime now = QDateTime::currentDateTime();

    m_offeredForCurrent = true;
    m_lastOfferByKey.insert(subject.key, now);
    m_lastOfferAt = now;
    if (m_countDate != QDate::currentDate()) {
        m_countDate  = QDate::currentDate();
        m_todayCount = 0;
    }
    ++m_todayCount;
    saveHistory();

    QString question;
    if (subject.kind == QStringLiteral("browser")) {
        question = QStringLiteral("Вижу, вы на «%1». Помочь?").arg(subject.label);
    } else if (subject.kind == QStringLiteral("editor")) {
        question = QStringLiteral("Вижу, вы над файлом %1. Помочь?").arg(subject.label);
    } else {
        question = QStringLiteral("Вижу, вы в %1. Помочь?").arg(subject.label);
    }

    // Поле свободного ответа тут важнее кнопок: чаще всего человек уже
    // знает, о чём хочет спросить, и печатает это сразу — вместо того
    // чтобы согласиться и потом объяснять с нуля.
    const QString label = subject.label;
    const QString kind  = subject.kind;

    NotificationManager::instance().askQuestion(
        QStringLiteral("J.A.R.V.I.S."), question,
        [this, label, kind](const QString& answer) {
            const QString reply = answer.trimmed();
            if (reply.isEmpty()
                || reply.compare(QStringLiteral("Не сейчас"), Qt::CaseInsensitive) == 0) {
                return;
            }
            if (!m_request)
                return;

            const QString where = (kind == QStringLiteral("browser"))
                ? QStringLiteral("на странице «%1»").arg(label)
                : QStringLiteral("в «%1»").arg(label);

            // «Помочь» — согласие без вопроса: пусть агент сам посмотрит,
            // что перед человеком, и предложит конкретное. Всё остальное —
            // уже готовый вопрос, и переспрашивать не нужно.
            const QString request =
                (reply.compare(QStringLiteral("Помочь"), Qt::CaseInsensitive) == 0)
                    ? QStringLiteral("Человек сейчас %1. Посмотри контекст экрана и "
                                     "предложи одну-две конкретные вещи, которыми ты "
                                     "можешь помочь именно здесь. Коротко.").arg(where)
                    : QStringLiteral("Человек сейчас %1 и спрашивает: %2").arg(where, reply);

            m_request(request);
        },
        { QStringLiteral("Помочь"), QStringLiteral("Не сейчас") });

    EventFeed::instance().post(
        QStringLiteral("context"), EventLevel::Info,
        QStringLiteral("Предложил помощь: %1").arg(subject.label),
        QString(), QStringLiteral("advisor/") + subject.key);
}
