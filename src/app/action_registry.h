#pragma once
// -------------------------------------------------------
// action_registry.h — Команды приложения как данные
//
// Меню собиралось императивно: одна функция на 2700 строк,
// 82 действия, каждое — лямбда внутри неё. Из-за этого
// команды существуют ровно в одном месте и ровно в одном
// виде: их нельзя ни найти через Ctrl+K, ни отрисовать в
// QML, ни перечислить модели.
//
// Здесь команда — запись: id, заголовок, значок, горячая
// клавиша, группа и обработчик. Из одного списка строятся
// (1) пункты меню, (2) результаты поиска, (3) ActionModel
// для QML. Добавить команду — добавить запись.
//
// Это первый шаг миграции окна на QML: отрисовать 82 лямбды
// декларативно невозможно, а модель — можно.
// -------------------------------------------------------

#include <QAbstractListModel>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

class QMenu;
class QWidget;

// ============================================================
//  Команда
// ============================================================
struct AppAction
{
    QString id;        // "view.dashboard" — стабильный, не переводится
    QString title;     // то, что видит человек
    QString icon;      // эмодзи
    QString shortcut;  // "Ctrl+Shift+D", пусто — без горячей клавиши
    QString group;     // группа меню: "view", "tools", ...
    QString hint;      // подсказка и текст для поиска

    std::function<void()> run;

    // Необязательный предикат доступности. Пусто = всегда доступна.
    std::function<bool()> enabled;

    // Переключатели (пассивный режим, автоскриншот, охрана и т.п.) — это
    // тоже команды, но у них есть состояние. Держим их здесь, а не
    // отдельным списком: иначе половина меню осталась бы вне реестра,
    // а значит и вне поиска, и вне модели для QML.
    bool                  checkable = false;
    std::function<bool()> checked;

    bool isEnabled() const { return enabled ? enabled() : true; }
    bool isChecked() const { return checked ? checked() : false; }
};

// ============================================================
//  ActionRegistry
// ============================================================
class ActionRegistry : public QObject
{
    Q_OBJECT

public:
    explicit ActionRegistry(QObject* parent = nullptr);

    // owner — объект, чьё состояние захватывает обработчик команды.
    // Реестр теперь живёт в main() и переживает главное окно, а команды
    // окна пережить его не могут: их лямбды открывают его диалоги. Когда
    // owner умирает, его записи уходят из реестра сами — иначе Ctrl+K
    // предлагал бы пункты, ведущие в освобождённую память.
    void add(AppAction action, QObject* owner = nullptr);
    void removeOwnedBy(QObject* owner);

    const AppAction*   find(const QString& id) const;
    QVector<AppAction> all() const { return m_actions; }
    QVector<AppAction> inGroup(const QString& group) const;
    QStringList        groups() const;
    int                count() const { return m_actions.size(); }

    // Выполнить по id. false — команды нет или она недоступна.
    bool run(const QString& id);

    // Наполнить QMenu действиями группы. Горячая клавиша только
    // РИСУЕТСЯ (текстом после табуляции) — назначает её installShortcuts.
    // Иначе при пересборке меню на каждый показ на окне копились бы
    // дубли QAction с одной комбинацией, и Qt переставал её обрабатывать
    // как неоднозначную.
    void populateMenu(QMenu* menu, const QString& group);

    // Повесить настоящие горячие клавиши на окно. Вызывать ОДИН раз,
    // после регистрации всех команд.
    void installShortcuts(QWidget* host);

signals:
    void changed();

private:
    QVector<AppAction>        m_actions;
    QHash<QString, QObject*>  m_owners;    // id команды -> кто её принёс
    QSet<QObject*>            m_watched;   // на чей destroyed уже подписаны
};

// ============================================================
//  ActionModel — тот же список для QML
// ============================================================
class ActionModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        IconRole,
        ShortcutRole,
        GroupRole,
        HintRole,
        EnabledRole,
        CheckableRole,
        CheckedRole
    };

    explicit ActionModel(ActionRegistry* registry, QObject* parent = nullptr);

    // Пустая группа — все команды
    Q_INVOKABLE void setGroupFilter(const QString& group);
    Q_INVOKABLE bool run(const QString& id);

    int      rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    void reload();

    ActionRegistry*    m_registry = nullptr;
    QString            m_group;
    QVector<AppAction> m_rows;
};
