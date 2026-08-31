#pragma once
// -------------------------------------------------------
// context_advisor.h — «Вижу, вы на такой-то странице. Помочь?»
//
// ContextTracker знает, что перед человеком на экране, но знает
// это молча: контекст уходил в системный промпт и ждал, пока
// спросят. Здесь появляется вторая половина — право заговорить
// первым.
//
// Главный риск такой функции очевиден: ассистент, который
// комментирует каждое переключение окна, выключается в первый
// же день. Поэтому предложение обставлено четырьмя условиями,
// и снимать их по одному нельзя — они работают только вместе:
//
//   1. ВЫДЕРЖКА. Окно должно быть открыто какое-то время. Пока
//      человек листает вкладки, он не работает ни с одной из
//      них — он ищет. Помощь нужна тому, кто остановился.
//   2. ПАМЯТЬ. На одну и ту же страницу предлагаем раз в сутки.
//      Второе предложение по тому же поводу — это уже не помощь,
//      а навязчивость.
//   3. ПАУЗА между любыми двумя предложениями и потолок на день:
//      даже полезное, сказанное десять раз за час, — это шум.
//   4. МОЛЧАНИЕ, когда JARVIS занят, когда режим глушит
//      уведомления и когда человек в игре или на видеозвонке.
//
// Модель здесь не участвует: решение «стоит ли заговорить»
// принимается локальными правилами, и только когда человек
// согласился — вопрос уходит агенту. Спрашивать модель на
// каждое переключение окна значило бы платить за право
// промолчать.
// -------------------------------------------------------

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>

#include <functional>

class ContextTracker;
class QTimer;

class ContextAdvisor : public QObject
{
    Q_OBJECT

public:
    explicit ContextAdvisor(ContextTracker* tracker, QObject* parent = nullptr);

    // Что делать, когда человек согласился. Ставит Jarvis (runAgentTask):
    // сам советчик выполнять ничего не умеет и не должен.
    using RequestHandler = std::function<void(const QString& request)>;
    void setRequestHandler(RequestHandler handler) { m_request = std::move(handler); }

    void setEnabled(bool on);
    bool isEnabled() const { return m_enabled; }

private:
    void tick();

    void loadHistory();
    void saveHistory() const;

    // Ключ контекста: то, повторение чего считается «тем же самым».
    // Для браузера это страница, для редактора — файл, иначе само
    // приложение.
    struct Subject {
        QString key;      // "browser:qt documentation"
        QString label;    // "Qt Documentation"
        QString kind;     // browser | editor | app
        bool isEmpty() const { return key.isEmpty(); }
    };

    Subject currentSubject() const;
    QString silenceReason() const;
    void    offer(const Subject& subject);

    ContextTracker* m_tracker = nullptr;
    QTimer*         m_timer   = nullptr;
    RequestHandler  m_request;

    bool      m_enabled = true;
    QString   m_currentKey;
    QDateTime m_currentSince;
    bool      m_offeredForCurrent = false;
    bool      m_silenceLogged     = false;   // причину молчания пишем раз на предмет

    QHash<QString, QDateTime> m_lastOfferByKey;
    QDateTime                 m_lastOfferAt;
    QDate                     m_countDate;
    int                       m_todayCount = 0;

    // Проверяем раз в 10 секунд: выдержка измеряется минутами, чаще
    // смотреть незачем.
    static constexpr int kTickMs = 10 * 1000;

    // Сколько нужно просидеть на одном месте, прежде чем предложение
    // перестаёт быть репликой под руку.
    static constexpr int kDwellSec = 90;

    static constexpr int kPerSubjectHours = 24;
    static constexpr int kBetweenOffersMin = 45;
    static constexpr int kMaxPerDay = 4;
};
