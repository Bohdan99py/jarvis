#pragma once
// ============================================================
// welcome_controller.h — состояние приветственного экрана.
//
// Экран собирался как одна HTML-строка на ~200 строк кода: цвета
// вписаны в разметку, %1…%17 подставляются двумя .arg() подряд
// (девять и восемь аргументов — потому что .arg() принимает не
// больше девяти), а перерисовка означала пересборку всего
// документа целиком.
//
// Здесь то же самое — данные. Разметку строит QML, цвета берёт из
// темы, и обновление одной строки статуса не трогает остальные.
// ============================================================

#include <QObject>
#include <QString>
#include <QVariantList>

class QTimer;

class Jarvis;

class WelcomeController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString greeting READ greeting NOTIFY changed)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString today READ today NOTIFY changed)

    // Строки статуса: [{ text, tone }], tone — "ok" | "warn" | "error".
    // Список, а не набор свойств: строк переменное число (проект и
    // самостоятельность появляются не всегда), и QML должен просто
    // их перечислить.
    Q_PROPERTY(QVariantList statusLines READ statusLines NOTIFY changed)

    Q_PROPERTY(QString thought READ thought NOTIFY changed)
    Q_PROPERTY(int unverifiedCount READ unverifiedCount NOTIFY changed)

    // Экран на виду (лента пуста). Опрос состояния идёт ТОЛЬКО пока
    // это так: как только пришло первое сообщение, панель скрыта, и
    // дёргать диск, базу и индексатор каждые несколько секунд не за
    // чем. Флаг ставит QML — он один знает, видно панель или нет.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    explicit WelcomeController(Jarvis* jarvis, QObject* parent = nullptr);

    QString      greeting()        const { return m_greeting; }
    QString      version()         const;
    QString      today()           const { return m_today; }
    QVariantList statusLines()     const { return m_statusLines; }
    QString      thought()         const { return m_thought; }
    int          unverifiedCount() const { return m_unverified; }
    bool         active()          const { return m_active; }

    void setActive(bool on);

    // Пересобирает состояние из живых источников. Зовётся на показ и
    // по сигналам смены профиля / статуса диска.
    Q_INVOKABLE void refresh();

signals:
    void changed();
    void activeChanged();

private:

    QTimer*      m_poll   = nullptr;
    Jarvis*      m_jarvis = nullptr;
    bool         m_active = false;
    QString      m_greeting;
    QString      m_today;
    QVariantList m_statusLines;
    QString      m_thought;
    int          m_unverified = 0;
};
