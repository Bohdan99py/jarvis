#pragma once
// ============================================================
// modes_dialog.h — Work modes dialog (QML).
//
// Показывает список режимов работы (ModeManager) сеткой карточек
// и позволяет одним кликом переключить активный режим. Режим
// задаёт набор включённых скиллов и добавляет ситуативный блок
// в system prompt.
//
// Данные отдаются в QML через Q_PROPERTY этого объекта, а НЕ через
// setContextProperty на каждое поле. Причина не в стиле: у контекстных
// свойств нет сигнала изменения, привязанного к объекту, поэтому
// `onActiveIdChanged` в QML не находил сигнала — а несуществующий
// обработчик в QML это не предупреждение, а ошибка создания
// компонента. Экран просто не открывался (пустой прямоугольник).
// С Q_PROPERTY + NOTIFY такие обработчики законны и проверяются
// компилятором QML.
// ============================================================

#include <QDialog>
#include <QString>
#include <QVariantList>

class QQuickWidget;
class QLabel;
class Jarvis;

class ModesDialog : public QDialog
{
    Q_OBJECT

    // modes — [{ id, name, description, icon, accent, enableSkills,
    //            disableSkills, exclusive, active }]
    Q_PROPERTY(QVariantList modes    READ modes    NOTIFY modesChanged)
    Q_PROPERTY(QString      activeId READ activeId NOTIFY modesChanged)
    Q_PROPERTY(bool         english  READ english  CONSTANT)
    Q_PROPERTY(QString      modesDir READ modesDir CONSTANT)

public:
    explicit ModesDialog(Jarvis* jarvis, bool english, QWidget* parent = nullptr);

    QVariantList modes()    const { return m_modes; }
    QString      activeId() const { return m_activeId; }
    bool         english()  const { return m_english; }
    QString      modesDir() const;

    Q_INVOKABLE void activate(const QString& modeId);
    Q_INVOKABLE void refresh();
    // Пустой список режимов выглядит так же, как сломанный экран.
    // Даём из UI попасть в папку, куда их кладут.
    Q_INVOKABLE void openModesFolder();

signals:
    void modesChanged();

private:
    void reload();

    Jarvis*       m_jarvis  = nullptr;
    bool          m_english = false;
    QQuickWidget* m_view    = nullptr;
    QLabel*       m_error   = nullptr;
    QVariantList  m_modes;
    QString       m_activeId;
};
