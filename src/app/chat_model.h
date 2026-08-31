#pragma once
// ============================================================
// chat_model.h — QAbstractListModel с лентой сообщений главного
// экрана.
//
// ПОЧЕМУ ОН ПОЯВИЛСЯ
// Лента жила в QTextEdit как один растущий HTML-документ:
// appendLog() собирал <div> со вписанными цветами и дописывал его
// в конец. Из этого следовало три вещи, каждая из которых мешает
// главному экрану выглядеть как приложение, а не как терминал:
//
//   1. Сообщение нельзя оформить — только разметить. Ни наведения,
//      ни выделения, ни действия «повторить», ни аватара роли:
//      всё это состояния элемента, а в HTML-строке состояний нет.
//   2. Цвета брались из ThemeManager мимо JarvisTheme — лента была
//      единственным местом приложения со своей палитрой.
//   3. Документ рос без границ: 142 вызова appendLog() за сессию
//      складывались в один QTextDocument, который перерисовывался
//      целиком на каждое добавление.
//
// Модель отвязывает «что сказано» от «как показано». QML-лента
// получает роли и рисует делегат; appendLog() остаётся
// единственной точкой входа, как и был, — все 142 вызова менять
// не пришлось.
// ============================================================

#include <QAbstractListModel>
#include <QColor>
#include <QDateTime>
#include <QList>
#include <QString>

// Без JARVIS_CORE_EXPORT: модель живёт в исполняемом файле
// (src/app), а не в общей библиотеке JarvisCore.
class ChatModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    // Роль отправителя. Цвет по роли выдаёт JarvisTheme, а не
    // вызывающая сторона: раньше цвет передавали строкой в каждый
    // из 142 вызовов, и «системный синий» существовал в трёх
    // немного разных оттенках.
    enum Kind {
        Jarvis = 0,
        User,
        System,
        Error,
    };
    Q_ENUM(Kind)

    enum Roles {
        KindRole = Qt::UserRole + 1,
        WhoRole,        // подпись роли: "JARVIS", "Вы", "Система"
        TextRole,       // тело сообщения, уже без HTML
        TimeRole,       // "HH:mm"
        AccentRole,     // цвет роли из темы
    };

    explicit ChatModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void append(Kind kind, const QString& who, const QString& text);

    // Лента живёт весь сеанс. Без потолка длинный рабочий день
    // упирается в память и в стоимость прокрутки, поэтому старые
    // сообщения вытесняются с головы — как в терминале scrollback.
    void setMaxMessages(int n);

    Q_INVOKABLE void clear();

    int count() const { return m_messages.size(); }

signals:
    void countChanged();

    // Лента должна доезжать до низа только когда пользователь и так
    // внизу. Решает это QML (он один знает положение прокрутки),
    // поэтому C++ просто сообщает факт добавления.
    void messageAppended();

private:
    struct Message {
        Kind      kind;
        QString   who;
        QString   text;
        QDateTime at;
    };

    QList<Message> m_messages;
    int m_maxMessages = 500;
};
