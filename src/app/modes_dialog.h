#pragma once
// ============================================================
// modes_dialog.h — Work modes dialog (QML).
//
// Показывает список режимов работы (ModeManager) сеткой карточек
// и позволяет одним кликом переключить активный режим. Режим
// задаёт набор включённых скиллов и добавляет ситуативный блок
// в system prompt.
// ============================================================

#include <QDialog>

class QQuickWidget;
class Jarvis;

class ModesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ModesDialog(Jarvis* jarvis, bool english, QWidget* parent = nullptr);

    Q_INVOKABLE void activate(const QString& modeId);
    Q_INVOKABLE void refresh();

private:
    void pushToQml();

    Jarvis*       m_jarvis  = nullptr;
    bool          m_english = false;
    QQuickWidget* m_view    = nullptr;
};
