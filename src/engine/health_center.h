#pragma once
// -------------------------------------------------------
// health_center.h — Проверка того, что JARVIS вообще жив
//
// У приложения из полутора сотен классов есть особенность: оно
// продолжает выглядеть работающим, когда половина подсистем молча
// отвалилась. База не открылась — история просто пустая. Ключ не
// задан — модель «не отвечает». Vosk не установлен — микрофон
// «не слышит». Каждый раз это выясняется через полчаса отладки.
//
// Центр здоровья отвечает на это одним экраном и одним вопросом
// голосом: что именно сломано ПРЯМО СЕЙЧАС.
//
//   Ядро          OK    JARVIS 1.x, аптайм 4 ч
//   LLM           OK    ключ задан
//   База          FAIL  database is locked
//   Инструменты   OK    31 в 9 категориях
//   Триггеры      OK    3 правила, 2 включены
//   Диск          WARN  свободно 1.4 ГБ
//
// Сам центр ничего не знает ни об одной подсистеме: пробы
// регистрируются снаружи лямбдами — так же, как источники в
// DeviceHub. Иначе engine пришлось бы связать с воском, сетью и
// всем остальным сразу.
//
// Проба обязана быть быстрой и НЕ ходить в сеть: диагностика
// запускается синхронно, в том числе из голосовой команды, и
// «проверка» с таймаутом в тридцать секунд бесполезна.
// -------------------------------------------------------

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

// ============================================================
//  Состояние одной подсистемы
// ============================================================
enum class HealthState {
    Ok      = 0,
    Warning = 1,   // работает, но скоро перестанет
    Failed  = 2,   // не работает
    Unknown = 3    // проверить не удалось
};

QString healthStateName(HealthState state);

struct HealthProbeResult
{
    HealthState state  = HealthState::Unknown;
    QString     detail;   // одна строка: цифры, путь, текст ошибки

    static HealthProbeResult ok(const QString& detail = QString());
    static HealthProbeResult warning(const QString& detail);
    static HealthProbeResult failed(const QString& detail);
};

struct HealthReport
{
    QString     id;       // "database"
    QString     name;     // "База данных"
    HealthState state = HealthState::Unknown;
    QString     detail;
    int         ms = 0;   // сколько заняла проверка

    QString toLine() const;
};

// ============================================================
//  HealthCenter
// ============================================================
class HealthCenter : public QObject
{
    Q_OBJECT

public:
    explicit HealthCenter(QObject* parent = nullptr);

    using Probe = std::function<HealthProbeResult()>;

    void addProbe(const QString& id, const QString& name, Probe probe);
    bool hasProbe(const QString& id) const;
    int  probeCount() const { return m_probes.size(); }

    // Синхронный прогон всех проб. Проба, бросившая исключение или
    // упавшая, становится Failed — диагностика не имеет права сама
    // ронять приложение.
    QVector<HealthReport> run();

    QVector<HealthReport> last() const { return m_last; }
    QDateTime             lastRunAt() const { return m_lastRunAt; }
    HealthState           worst() const;

    QString summaryForModel() const;

signals:
    void finished(const QVector<HealthReport>& reports);

private:
    struct Entry {
        QString id;
        QString name;
        Probe   probe;
    };

    QVector<Entry>        m_probes;
    QVector<HealthReport> m_last;
    QDateTime             m_lastRunAt;
};

class ToolRegistry;

namespace JarvisTools {

// run_diagnostics — «проверь себя» должно работать голосом, а не
// только кнопкой на экране, до которого ещё надо дойти.
void registerHealthTools(ToolRegistry& registry, HealthCenter* center);

} // namespace JarvisTools
