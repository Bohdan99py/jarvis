// -------------------------------------------------------
// dashboard_dialog.cpp — см. dashboard_dialog.h
// -------------------------------------------------------

#include "dashboard_dialog.h"

#include "device_hub.h"
#include "event_feed.h"
#include "jarvis.h"
#include "mode_manager.h"
#include "permission_gate.h"
#include "sparkline.h"
#include "system_monitor.h"
#include "workflow_manager.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFrame>
#include <QStyle>
#include <QDir>
#include <QDrag>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QDateTime>

namespace {

const char* kMime = "application/x-jarvis-dashboard-item";

QString humanBytes(quint64 bytes)
{
    static const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = double(bytes);
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    return QString::number(v, 'f', v < 10.0 ? 1 : 0)
           + QLatin1Char(' ') + QLatin1String(units[i]);
}

// Ширина в колонках и минимальная высота. Высоту меняем вместо
// растягивания по строкам: rowSpan в QGridLayout пришлось бы
// упаковывать вручную, а карточки при этом наезжают друг на друга.
int spanFor(const QString& size)
{
    if (size == QLatin1String("wide"))   return 4;
    if (size == QLatin1String("medium")) return 2;
    if (size == QLatin1String("large"))  return 2;
    return 1;
}

int heightFor(const QString& size)
{
    if (size == QLatin1String("large")) return 260;
    if (size == QLatin1String("wide"))  return 180;
    return 150;
}

QLabel* bigValue(QWidget* parent, const QString& color)
{
    auto* l = new QLabel(QStringLiteral("—"), parent);
    l->setObjectName(QStringLiteral("dashValue"));
    l->setStyleSheet(QStringLiteral("color: %1;").arg(color));
    return l;
}

QLabel* note(QWidget* parent)
{
    auto* l = new QLabel(QString(), parent);
    l->setObjectName(QStringLiteral("dashNote"));
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    l->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    return l;
}

} // namespace

// ============================================================
//  DashboardCard
// ============================================================

class DashboardCard : public QFrame
{
public:
    DashboardCard(const DashboardWidgetDef& def, const QString& size,
                  QWidget* content, bool english, QWidget* parent = nullptr)
        : QFrame(parent)
        , m_id(def.id)
        , m_size(size)
        , m_english(english)
    {
        setObjectName(QStringLiteral("dashCard"));
        setAcceptDrops(true);
        setMinimumHeight(heightFor(size));

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(14, 10, 14, 12);
        layout->setSpacing(6);

        auto* header = new QHBoxLayout();
        auto* title = new QLabel(
            (def.icon.isEmpty() ? QString() : def.icon + QChar(' ')) + def.title, this);
        title->setObjectName(QStringLiteral("dashTitle"));
        header->addWidget(title);
        header->addStretch(1);

        auto* grip = new QLabel(QStringLiteral("⠿"), this);
        grip->setObjectName(QStringLiteral("dashGrip"));
        grip->setToolTip(english ? QStringLiteral("Drag to reorder, right-click for size")
                                 : QStringLiteral("Тяни, чтобы переставить; правая кнопка — размер"));
        header->addWidget(grip);
        layout->addLayout(header);

        if (content) {
            content->setParent(this);
            layout->addWidget(content, 1);
        }
    }

    QString id() const   { return m_id; }
    QString size() const { return m_size; }

    // Колбэки вместо сигналов: класс живёт в .cpp, а Q_OBJECT здесь
    // потребовал бы отдельного #include "*.moc" ради трёх уведомлений.
    std::function<void(const QString& dragged, const QString& target)> onDropped;
    std::function<void(const QString& id, const QString& size)>        onResize;
    std::function<void(const QString& id)>                             onRemove;

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton)
            m_pressPos = e->pos();
        QFrame::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (!(e->buttons() & Qt::LeftButton))
            return;
        if ((e->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance())
            return;

        auto* mime = new QMimeData();
        mime->setData(QLatin1String(kMime), m_id.toUtf8());

        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(grab().scaledToWidth(220, Qt::SmoothTransformation));
        drag->exec(Qt::MoveAction);
    }

    void dragEnterEvent(QDragEnterEvent* e) override
    {
        if (e->mimeData()->hasFormat(QLatin1String(kMime))) {
            e->acceptProposedAction();
            setProperty("dropTarget", true);
            style()->unpolish(this);
            style()->polish(this);
        }
    }

    void dragLeaveEvent(QDragLeaveEvent*) override
    {
        setProperty("dropTarget", false);
        style()->unpolish(this);
        style()->polish(this);
    }

    void dropEvent(QDropEvent* e) override
    {
        setProperty("dropTarget", false);
        style()->unpolish(this);
        style()->polish(this);

        const QString dragged =
            QString::fromUtf8(e->mimeData()->data(QLatin1String(kMime)));
        if (!dragged.isEmpty() && dragged != m_id && onDropped)
            onDropped(dragged, m_id);
        e->acceptProposedAction();
    }

    void contextMenuEvent(QContextMenuEvent* e) override
    {
        QMenu menu(this);

        struct Entry { const char* key; const char* ru; const char* en; };
        static const Entry kSizes[] = {
            { "small",  "Маленькая",  "Small"  },
            { "medium", "Средняя",    "Medium" },
            { "large",  "Большая",    "Large"  },
            { "wide",   "Во всю ширину", "Wide" },
        };

        for (const Entry& s : kSizes) {
            QAction* act = menu.addAction(
                m_english ? QString::fromUtf8(s.en) : QString::fromUtf8(s.ru));
            act->setCheckable(true);
            act->setChecked(m_size == QLatin1String(s.key));
            const QString key = QString::fromLatin1(s.key);
            QObject::connect(act, &QAction::triggered, this, [this, key]() {
                if (onResize) onResize(m_id, key);
            });
        }

        menu.addSeparator();
        QAction* rm = menu.addAction(m_english ? QStringLiteral("Remove")
                                               : QStringLiteral("Убрать"));
        QObject::connect(rm, &QAction::triggered, this, [this]() {
            if (onRemove) onRemove(m_id);
        });

        menu.exec(e->globalPos());
    }

private:
    QString m_id;
    QString m_size;
    bool    m_english = false;
    QPoint  m_pressPos;
};

// ============================================================
//  DashboardDialog
// ============================================================

DashboardDialog::DashboardDialog(Jarvis* jarvis, bool english, QWidget* parent)
    : QDialog(parent)
    , m_jarvis(jarvis)
    , m_english(english)
{
    setWindowTitle(english ? QStringLiteral("Dashboard") : QStringLiteral("Дашборд"));
    resize(1040, 720);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &DashboardDialog::refresh);

    buildRegistry();
    buildUi();
    load();
    relayout();
}

// ============================================================
//  Реестр карточек
// ============================================================

void DashboardDialog::buildRegistry()
{
    Jarvis* j = m_jarvis;
    const bool en = m_english;

    // --- CPU ---
    m_registry.append({
        QStringLiteral("cpu"), en ? QStringLiteral("CPU") : QStringLiteral("ПРОЦЕССОР"),
        QStringLiteral("⚡"), QStringLiteral("medium"),
        [j]() -> BuiltWidget {
            auto* box = new QWidget();
            auto* lay = new QVBoxLayout(box);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->setSpacing(2);
            auto* value = bigValue(box, QStringLiteral("#00d4ff"));
            auto* chart = new Sparkline(QColor(QStringLiteral("#00d4ff")), box);
            lay->addWidget(value);
            lay->addWidget(chart, 1);

            return { box, [j, value, chart]() {
                SystemMonitor* m = j->systemMonitor();
                if (!m) return;
                QVector<double> data;
                for (int v : m->cpuHistory()) data.append(double(v));
                chart->setData(data, 100.0);
                value->setText(QStringLiteral("%1%").arg(m->cpuPercent()));
            } };
        }
    });

    // --- Память ---
    m_registry.append({
        QStringLiteral("ram"), en ? QStringLiteral("MEMORY") : QStringLiteral("ПАМЯТЬ"),
        QStringLiteral("▦"), QStringLiteral("medium"),
        [j]() -> BuiltWidget {
            auto* box = new QWidget();
            auto* lay = new QVBoxLayout(box);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->setSpacing(2);
            auto* value = bigValue(box, QStringLiteral("#7c4dff"));
            auto* chart = new Sparkline(QColor(QStringLiteral("#7c4dff")), box);
            auto* sub   = note(box);
            lay->addWidget(value);
            lay->addWidget(chart, 1);
            lay->addWidget(sub);

            return { box, [j, value, chart, sub]() {
                SystemMonitor* m = j->systemMonitor();
                if (!m) return;
                QVector<double> data;
                for (int v : m->ramHistory()) data.append(double(v));
                chart->setData(data, 100.0);
                value->setText(QStringLiteral("%1%").arg(m->ramPercent()));
                sub->setText(QStringLiteral("%1 / %2")
                                 .arg(humanBytes(m->ramUsedBytes()),
                                      humanBytes(m->ramTotalBytes())));
            } };
        }
    });

    // --- Сеть ---
    m_registry.append({
        QStringLiteral("net"), en ? QStringLiteral("NETWORK") : QStringLiteral("СЕТЬ"),
        QStringLiteral("↕"), QStringLiteral("medium"),
        [j]() -> BuiltWidget {
            auto* box = new QWidget();
            auto* lay = new QVBoxLayout(box);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->setSpacing(2);
            auto* value = bigValue(box, QStringLiteral("#00e676"));
            auto* chart = new Sparkline(QColor(QStringLiteral("#00e676")), box);
            auto* sub   = note(box);
            lay->addWidget(value);
            lay->addWidget(chart, 1);
            lay->addWidget(sub);

            return { box, [j, value, chart, sub]() {
                SystemMonitor* m = j->systemMonitor();
                if (!m) return;
                chart->setData(m->netHistory(), -1.0);
                value->setText(QStringLiteral("%1 KB/s")
                                   .arg(m->netDownKbps() + m->netUpKbps(), 0, 'f', 0));
                sub->setText(QStringLiteral("↓ %1&nbsp;&nbsp;↑ %2")
                                 .arg(m->netDownKbps(), 0, 'f', 0)
                                 .arg(m->netUpKbps(), 0, 'f', 0));
            } };
        }
    });

    // --- Диски ---
    m_registry.append({
        QStringLiteral("disks"), en ? QStringLiteral("DISKS") : QStringLiteral("ДИСКИ"),
        QStringLiteral("▤"), QStringLiteral("medium"),
        [j, en]() -> BuiltWidget {
            auto* label = note(nullptr);
            return { label, [j, label, en]() {
                SystemMonitor* m = j->systemMonitor();
                if (!m) return;
                QStringList lines;
                for (const DiskSample& d : m->disks()) {
                    const int used = d.usedPercent();
                    const QString color = used >= 90 ? QStringLiteral("#ff5252")
                                        : used >= 75 ? QStringLiteral("#ffb300")
                                                     : QStringLiteral("#00e676");
                    lines << QStringLiteral("%1 <b style='color:%2'>%3%</b> — %4 %5")
                                 .arg(d.root, color).arg(used)
                                 .arg(humanBytes(d.freeBytes),
                                      en ? QStringLiteral("free") : QStringLiteral("свободно"));
                }
                label->setText(lines.join(QStringLiteral("<br>")));
            } };
        }
    });

    // --- Процессы ---
    m_registry.append({
        QStringLiteral("processes"), en ? QStringLiteral("TOP PROCESSES")
                                        : QStringLiteral("ПРОЦЕССЫ"),
        QStringLiteral("▶"), QStringLiteral("large"),
        [j]() -> BuiltWidget {
            auto* label = note(nullptr);
            return { label, [j, label]() {
                SystemMonitor* m = j->systemMonitor();
                if (!m) return;
                QStringList lines;
                for (const ProcessSample& p : m->topProcesses(8)) {
                    lines << QStringLiteral("%1 &nbsp;<b>%2%</b> &nbsp;%3")
                                 .arg(p.name.left(24))
                                 .arg(p.cpuPercent, 0, 'f', 1)
                                 .arg(humanBytes(p.memBytes));
                }
                label->setText(lines.join(QStringLiteral("<br>")));
            } };
        }
    });

    // --- Устройства ---
    m_registry.append({
        QStringLiteral("devices"), en ? QStringLiteral("DEVICES") : QStringLiteral("УСТРОЙСТВА"),
        QStringLiteral("🛰"), QStringLiteral("medium"),
        [j]() -> BuiltWidget {
            auto* label = note(nullptr);
            return { label, [j, label]() {
                if (!j->devices()) return;
                QStringList lines;
                for (const DeviceInfo& d : j->devices()->devices()) {
                    const QString color =
                        d.status == DeviceInfo::Status::Offline ? QStringLiteral("#ff5252")
                                                                : QStringLiteral("#00e676");
                    lines << QStringLiteral("<span style='color:%1'>●</span> %2 %3")
                                 .arg(color, d.icon, d.name);
                }
                label->setText(lines.join(QStringLiteral("<br>")));
            } };
        }
    });

    // --- События ---
    m_registry.append({
        QStringLiteral("events"), en ? QStringLiteral("EVENTS") : QStringLiteral("СОБЫТИЯ"),
        QStringLiteral("🔔"), QStringLiteral("wide"),
        [](){
            auto* label = note(nullptr);
            return BuiltWidget{ label, [label]() {
                const QVector<FeedEvent> all = EventFeed::instance().events();
                QStringList lines;
                for (int i = all.size() - 1; i >= 0 && lines.size() < 6; --i) {
                    const FeedEvent& e = all[i];
                    const QString color =
                        e.level == EventLevel::Error   ? QStringLiteral("#ff5252")
                      : e.level == EventLevel::Warning ? QStringLiteral("#ffb300")
                      : e.level == EventLevel::Good    ? QStringLiteral("#00e676")
                                                       : QStringLiteral("#8fa7bf");
                    QString line = QStringLiteral("<span style='color:#3a4a5e'>%1</span> "
                                                  "<span style='color:%2'>%3</span> %4")
                                       .arg(e.timeText(), color, e.source, e.title);
                    if (e.count > 1)
                        line += QStringLiteral(" ×%1").arg(e.count);
                    lines << line;
                }
                label->setText(lines.isEmpty() ? QStringLiteral("—")
                                               : lines.join(QStringLiteral("<br>")));
            } };
        }
    });

    // --- Сценарии ---
    m_registry.append({
        QStringLiteral("workflows"), en ? QStringLiteral("WORKFLOWS")
                                        : QStringLiteral("СЦЕНАРИИ"),
        QStringLiteral("⚙"), QStringLiteral("medium"),
        [j]() -> BuiltWidget {
            auto* label = note(nullptr);
            return { label, [j, label]() {
                if (!j->workflows()) return;
                QStringList lines;
                for (const Workflow& wf : j->workflows()->all())
                    lines << QStringLiteral("%1 %2")
                                 .arg(wf.icon.isEmpty() ? QStringLiteral("▶") : wf.icon,
                                      wf.name);
                label->setText(lines.isEmpty() ? QStringLiteral("—")
                                               : lines.join(QStringLiteral("<br>")));
            } };
        }
    });

    // --- Профиль ---
    m_registry.append({
        QStringLiteral("profile"), en ? QStringLiteral("PROFILE") : QStringLiteral("ПРОФИЛЬ"),
        QStringLiteral("👤"), QStringLiteral("medium"),
        [j, en]() -> BuiltWidget {
            auto* box = new QWidget();
            auto* lay = new QVBoxLayout(box);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->setSpacing(2);
            auto* value = bigValue(box, QStringLiteral("#00d4ff"));
            auto* sub   = note(box);
            lay->addWidget(value);
            lay->addWidget(sub, 1);

            return { box, [j, value, sub, en]() {
                if (!j->modeManager()) return;
                const ModeInfo mode = j->modeManager()->activeMode();
                value->setText(mode.displayName(en).isEmpty() ? QStringLiteral("—")
                                                              : mode.displayName(en));
                QString text = mode.system.summary(en);
                if (j->permissions()) {
                    text += (text.isEmpty() ? QString() : QStringLiteral("<br>"))
                            + permissionModeDescription(j->permissions()->mode(), en);
                }
                sub->setText(text);
            } };
        }
    });

    // --- Часы и аптайм ---
    m_registry.append({
        QStringLiteral("clock"), en ? QStringLiteral("TIME") : QStringLiteral("ВРЕМЯ"),
        QStringLiteral("◷"), QStringLiteral("small"),
        [j, en]() -> BuiltWidget {
            auto* box = new QWidget();
            auto* lay = new QVBoxLayout(box);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->setSpacing(2);
            auto* value = bigValue(box, QStringLiteral("#e8f0fe"));
            auto* sub   = note(box);
            lay->addWidget(value);
            lay->addWidget(sub, 1);

            return { box, [j, value, sub, en]() {
                value->setText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")));
                SystemMonitor* m = j->systemMonitor();
                if (!m) return;
                const qint64 up = m->uptimeSeconds();
                sub->setText((en ? QStringLiteral("uptime %1h %2m")
                                 : QStringLiteral("аптайм %1 ч %2 мин"))
                                 .arg(up / 3600).arg((up % 3600) / 60));
            } };
        }
    });
}

const DashboardWidgetDef* DashboardDialog::findDef(const QString& id) const
{
    for (const DashboardWidgetDef& d : m_registry) {
        if (d.id == id)
            return &d;
    }
    return nullptr;
}

// ============================================================
//  Интерфейс
// ============================================================

void DashboardDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* topRow = new QHBoxLayout();
    auto* addBtn = new QPushButton(
        m_english ? QStringLiteral("＋ Add widget") : QStringLiteral("＋ Добавить виджет"), this);
    connect(addBtn, &QPushButton::clicked, this, &DashboardDialog::showAddMenu);
    topRow->addWidget(addBtn);

    auto* resetBtn = new QPushButton(
        m_english ? QStringLiteral("Reset layout") : QStringLiteral("Сбросить раскладку"), this);
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        m_items.clear();
        seedDefaults();
        save();
        relayout();
    });
    topRow->addWidget(resetBtn);
    topRow->addStretch(1);

    auto* hint = new QLabel(
        m_english ? QStringLiteral("Drag a card onto another to reorder · right-click for size")
                  : QStringLiteral("Перетащи карточку на другую, чтобы переставить · "
                                   "правая кнопка — размер"), this);
    hint->setObjectName(QStringLiteral("dashHint"));
    topRow->addWidget(hint);
    root->addLayout(topRow);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_canvas = new QWidget(scroll);
    m_grid = new QGridLayout(m_canvas);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(12);
    for (int c = 0; c < kColumns; ++c)
        m_grid->setColumnStretch(c, 1);

    scroll->setWidget(m_canvas);
    root->addWidget(scroll, 1);

    setStyleSheet(QStringLiteral(R"(
        QDialog { background-color: #080a12; }
        QLabel  { color: #c0d8ee; font-family: "Segoe UI", sans-serif; }
        #dashCard {
            background-color: rgba(255, 255, 255, 8);
            border: 1px solid rgba(0, 212, 255, 45);
            border-radius: 12px;
        }
        #dashCard[dropTarget="true"] {
            border: 1px solid #00d4ff;
            background-color: rgba(0, 212, 255, 22);
        }
        #dashTitle { color: #3a4a5e; font-size: 11px; letter-spacing: 2px; }
        #dashGrip  { color: #2a3a4e; font-size: 13px; }
        #dashValue { font-size: 24px; font-weight: bold; }
        #dashNote  { color: #8fa7bf; font-size: 12px; }
        #dashHint  { color: #3a4a5e; font-size: 11px; }
        QPushButton {
            background-color: rgba(255, 255, 255, 10);
            border: 1px solid rgba(0, 212, 255, 60);
            border-radius: 7px;
            color: #c0d8ee;
            padding: 6px 14px;
        }
        QPushButton:hover { border: 1px solid #00d4ff; }
    )"));
}

void DashboardDialog::relayout()
{
    // Сносим старые карточки целиком: пересобрать сетку дешевле, чем
    // выяснять, какие ячейки освободились после смены размера.
    for (DashboardCard* card : m_cards)
        card->deleteLater();
    m_cards.clear();
    m_updaters.clear();

    while (QLayoutItem* item = m_grid->takeAt(0))
        delete item;

    int row = 0;
    int col = 0;

    for (const DashboardItem& item : m_items) {
        const DashboardWidgetDef* def = findDef(item.id);
        if (!def || !def->factory)
            continue;   // раскладка из файла может ссылаться на снятый виджет

        const BuiltWidget built = def->factory();
        auto* card = new DashboardCard(*def, item.size, built.widget, m_english, m_canvas);

        card->onDropped = [this](const QString& dragged, const QString& target) {
            moveItem(dragged, target);
        };
        card->onResize = [this](const QString& id, const QString& size) {
            setItemSize(id, size);
        };
        card->onRemove = [this](const QString& id) {
            removeItem(id);
        };

        const int span = spanFor(item.size);
        if (col + span > kColumns) {
            col = 0;
            ++row;
        }
        m_grid->addWidget(card, row, col, 1, span);
        col += span;
        if (col >= kColumns) {
            col = 0;
            ++row;
        }

        m_cards.insert(item.id, card);
        if (built.update)
            m_updaters.insert(item.id, built.update);
    }

    m_grid->setRowStretch(row + 1, 1);
    refresh();
}

void DashboardDialog::refresh()
{
    for (auto it = m_updaters.begin(); it != m_updaters.end(); ++it)
        it.value()();
}

void DashboardDialog::showAddMenu()
{
    QMenu menu(this);
    bool any = false;

    for (const DashboardWidgetDef& def : m_registry) {
        bool present = false;
        for (const DashboardItem& item : m_items) {
            if (item.id == def.id) { present = true; break; }
        }
        if (present)
            continue;

        any = true;
        QAction* act = menu.addAction(
            (def.icon.isEmpty() ? QString() : def.icon + QChar(' ')) + def.title);
        const QString id   = def.id;
        const QString size = def.defaultSize;
        connect(act, &QAction::triggered, this, [this, id, size]() {
            m_items.append({ id, size });
            save();
            relayout();
        });
    }

    if (!any) {
        QAction* empty = menu.addAction(
            m_english ? QStringLiteral("All widgets are already on the board")
                      : QStringLiteral("Все виджеты уже на доске"));
        empty->setEnabled(false);
    }

    menu.exec(QCursor::pos());
}

// ============================================================
//  Изменения раскладки
// ============================================================

void DashboardDialog::moveItem(const QString& draggedId, const QString& targetId)
{
    int from = -1, to = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == draggedId) from = i;
        if (m_items[i].id == targetId)  to   = i;
    }
    if (from < 0 || to < 0 || from == to)
        return;

    const DashboardItem moved = m_items.takeAt(from);
    m_items.insert(to, moved);

    save();
    relayout();
}

void DashboardDialog::removeItem(const QString& id)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == id) {
            m_items.remove(i);
            save();
            relayout();
            return;
        }
    }
}

void DashboardDialog::setItemSize(const QString& id, const QString& size)
{
    for (DashboardItem& item : m_items) {
        if (item.id == id) {
            if (item.size == size)
                return;
            item.size = size;
            save();
            relayout();
            return;
        }
    }
}

// ============================================================
//  Хранение
// ============================================================

QString DashboardDialog::storagePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/dashboard.json");
}

void DashboardDialog::seedDefaults()
{
    m_items = {
        { QStringLiteral("cpu"),       QStringLiteral("medium") },
        { QStringLiteral("ram"),       QStringLiteral("medium") },
        { QStringLiteral("net"),       QStringLiteral("medium") },
        { QStringLiteral("clock"),     QStringLiteral("small")  },
        { QStringLiteral("disks"),     QStringLiteral("medium") },
        { QStringLiteral("processes"), QStringLiteral("large")  },
        { QStringLiteral("devices"),   QStringLiteral("medium") },
        { QStringLiteral("events"),    QStringLiteral("wide")   },
    };
}

void DashboardDialog::load()
{
    QFile f(storagePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        seedDefaults();
        return;
    }

    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();

    m_items.clear();
    for (const QJsonValue& v : arr) {
        const QJsonObject obj = v.toObject();
        DashboardItem item;
        item.id   = obj.value(QStringLiteral("id")).toString();
        item.size = obj.value(QStringLiteral("size")).toString(QStringLiteral("medium"));
        if (!item.id.isEmpty())
            m_items.append(item);
    }

    // Пустой или битый файл не должен приводить к пустой доске —
    // человек решит, что дашборд сломался, а не что раскладка пуста.
    if (m_items.isEmpty())
        seedDefaults();
}

void DashboardDialog::save() const
{
    QJsonArray arr;
    for (const DashboardItem& item : m_items) {
        arr.append(QJsonObject{
            { QStringLiteral("id"),   item.id },
            { QStringLiteral("size"), item.size }
        });
    }

    QFile f(storagePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.close();
}

// ============================================================
//  Показ
// ============================================================

void DashboardDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    // Пока доска открыта, монитору нужен секундный шаг — иначе
    // графики двигаются рывками раз в пять секунд.
    if (SystemMonitor* m = m_jarvis->systemMonitor())
        m->setSampleIntervalMs(1000);

    refresh();
    m_timer->start();
}

void DashboardDialog::hideEvent(QHideEvent* event)
{
    m_timer->stop();
    if (SystemMonitor* m = m_jarvis->systemMonitor())
        m->setSampleIntervalMs(5000);
    QDialog::hideEvent(event);
}
