#pragma once
// -------------------------------------------------------
// tray_presence.h — Трей как самостоятельный интерфейс к ядру
//
// Раньше трей был частью MainWindow и умел ровно две вещи:
// показать окно и выйти. То есть присутствие JARVIS в системе
// сводилось к ярлыку, за которым всё равно нужно открывать окно.
//
// Здесь он живёт рядом с окном, а не внутри него, и берёт всё
// из ядра напрямую:
//
//     JarvisState   -> чем занят прямо сейчас
//     SystemMonitor -> CPU / RAM
//     EventFeed     -> что случилось, пока не смотрели
//     ModeManager   -> профили
//     WorkflowManager -> сценарии
//
// Своей логики у трея нет: он ничего не вычисляет и ничего не
// выполняет сам. Режим переключает ModeManager, сценарий запускает
// WorkflowManager — то есть через ToolRegistry и PermissionGate,
// той же дорогой, что голос, палитра и триггеры. Иначе в проекте
// появился бы второй способ сделать то же самое, отличающийся
// только тем, что его вызвали из меню.
//
// Окно трею неизвестно намеренно: он просит его открыть сигналом,
// а кто и как это сделает — забота main().
// -------------------------------------------------------

#include <QObject>
#include <QString>

class Jarvis;
class QMenu;
class QSystemTrayIcon;

class TrayPresence : public QObject
{
    Q_OBJECT

public:
    explicit TrayPresence(Jarvis* core, QObject* parent = nullptr);
    ~TrayPresence() override;

    // false — в системе нет области уведомлений. Тогда окно обязано
    // закрываться по-настоящему: прятать его будет некуда.
    bool isAvailable() const;

signals:
    void openWindowRequested();
    void askRequested();     // «Спросить» — палитра команд
    void quitRequested();

private:
    // Меню пересобирается на каждый показ. Держать его в актуальном
    // состоянии подписками дороже и хрупче: пунктов десяток, а
    // источников у них пять, и меняются они постоянно.
    void rebuildMenu();
    void refreshTooltip();

    QString statusLine() const;

    Jarvis*          m_core = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu*           m_menu = nullptr;

    // Сколько последних событий показывать в подменю. Лента хранит
    // сотни; в меню имеет смысл только «что было только что».
    static constexpr int kRecentEvents = 6;
};
