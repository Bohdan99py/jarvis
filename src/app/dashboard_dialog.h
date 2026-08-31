#pragma once
// -------------------------------------------------------
// dashboard_dialog.h — Настраиваемый дашборд
//
// Все данные уже есть в системе: SystemMonitor, DeviceHub,
// EventFeed, WorkflowManager, ModeManager. Дашборд ничего
// не считает сам — он только раскладывает их по карточкам
// так, как захотел человек, и запоминает раскладку.
//
// Карточка = виджет из реестра + размер. Реестр строится
// в конструкторе лямбдами: каждая запись умеет создать
// содержимое и обновить его по тику. Из-за этого добавить
// новую карточку — это одна запись, а не новый класс.
//
// Раскладка живёт в AppData/dashboard.json.
// -------------------------------------------------------

#include <QDialog>
#include <QHash>
#include <QString>
#include <QVector>

#include <functional>

class Jarvis;
class QGridLayout;
class QScrollArea;
class QTimer;

// Что карточка показывает и как обновляется
struct BuiltWidget
{
    QWidget*              widget = nullptr;
    std::function<void()> update;
};

// Описание одного вида карточек
struct DashboardWidgetDef
{
    QString                     id;
    QString                     title;
    QString                     icon;
    QString                     defaultSize;   // small | medium | large | wide
    std::function<BuiltWidget()> factory;
};

// Карточка на дашборде: что показываем и какого размера
struct DashboardItem
{
    QString id;
    QString size;
};

class DashboardCard;

class DashboardDialog : public QDialog
{
    Q_OBJECT

public:
    DashboardDialog(Jarvis* jarvis, bool english, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void buildRegistry();
    void buildUi();

    void relayout();          // перестроить сетку по m_items
    void refresh();           // обновить содержимое карточек
    void showAddMenu();

    void moveItem(const QString& draggedId, const QString& targetId);
    void removeItem(const QString& id);
    void setItemSize(const QString& id, const QString& size);

    void load();
    void save() const;
    QString storagePath() const;
    void seedDefaults();

    const DashboardWidgetDef* findDef(const QString& id) const;

    Jarvis*      m_jarvis  = nullptr;
    bool         m_english = false;

    QVector<DashboardWidgetDef> m_registry;
    QVector<DashboardItem>      m_items;

    QGridLayout* m_grid   = nullptr;
    QWidget*     m_canvas = nullptr;
    QTimer*      m_timer  = nullptr;

    // id карточки -> живая карточка и её обновлятор
    QHash<QString, DashboardCard*>        m_cards;
    QHash<QString, std::function<void()>> m_updaters;

    static constexpr int kColumns = 4;
};
