#pragma once
// -------------------------------------------------------
// context_tracker.h — «Здесь» и «это»
//
// Чтобы вопрос «почему здесь ошибка?» имел смысл, кто-то
// должен знать, что такое «здесь». ActivityTracker снимает
// активность раз в 15 секунд ради статистики и категорий;
// для разрешения указательных местоимений это слишком грубо
// и, главное, он не отличает окно JARVIS от окна, в котором
// человек реально работает.
//
// ContextTracker опрашивает передний план часто (1.5 с) и
// запоминает ПОСЛЕДНЕЕ ЧУЖОЕ окно — то, что было под курсором
// до того, как фокус перешёл к нам. Плюс разбирает заголовок
// на файл / проект / страницу:
//
//     "jarvis – mainwindow.cpp"          -> проект jarvis, файл mainwindow.cpp
//     "main.py - myapp - Visual Studio Code"
//     "Qt Documentation - Google Chrome" -> страница в браузере
//
// Результат уходит двумя путями: блоком в системный промпт
// (SessionMemory::setMachineContext) и инструментом get_context.
// -------------------------------------------------------

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QTimer;

// ============================================================
//  MachineContext — то, что видит пользователь прямо сейчас
// ============================================================
struct MachineContext
{
    QDateTime   capturedAt;

    QString     appName;        // "Rider"
    QString     processName;    // "rider64.exe"
    QString     windowTitle;    // сырой заголовок

    QString     currentFile;    // "mainwindow.cpp" — если окно редактора
    QString     projectName;    // "jarvis"        — если окно редактора
    QString     browserPage;    // заголовок вкладки — если окно браузера

    QString     projectRoot;    // из ProjectIndexer
    QStringList recentFiles;    // недавно изменённые файлы проекта
    QStringList recentApps;     // последние приложения, в которых работали
    QStringList runningApps;    // что запущено прямо сейчас

    QString     clipboardPreview;

    bool isEmpty() const { return appName.isEmpty() && windowTitle.isEmpty(); }

    // Компактный блок для системного промпта
    QString toPromptBlock() const;

    // Человеческий вид — для инструмента get_context и отладки
    QString toHumanText() const;
};

// ============================================================
//  ContextTracker
// ============================================================
class ContextTracker : public QObject
{
    Q_OBJECT

public:
    explicit ContextTracker(QObject* parent = nullptr);

    void start(int pollMs = 1500);
    void stop();
    bool isRunning() const;

    // Корень проекта и недавние файлы знает только Jarvis (ProjectIndexer),
    // поэтому он отдаёт их сюда колбэком, а не через прямую зависимость.
    using ProjectInfoProvider = std::function<void(QString& root, QStringList& recentFiles)>;
    void setProjectInfoProvider(ProjectInfoProvider provider);

    // Список запущенных приложений — тоже колбэком: процессы знает
    // SystemMonitor, и он их и так опрашивает. Без этого списка модель
    // не знает, что уже открыто, и предлагает открыть браузер человеку,
    // который на него в этот момент смотрит.
    using RunningAppsProvider = std::function<QStringList()>;
    void setRunningAppsProvider(RunningAppsProvider provider);

    MachineContext snapshot() const;
    QString        promptBlock() const;

    QString lastForeignTitle() const   { return m_title; }
    QString lastForeignProcess() const { return m_process; }
    QString lastForeignApp() const     { return m_app; }

    // Разбор заголовка окна — вынесен наружу, чтобы можно было
    // проверять правила отдельно от Win32.
    static void parseWindowTitle(const QString& processName,
                                 const QString& title,
                                 QString* fileOut,
                                 QString* projectOut,
                                 QString* pageOut);

    // "rider64.exe" -> "Rider"
    static QString friendlyAppName(const QString& processName);

signals:
    // Человек переключился в другое окно (не в JARVIS)
    void focusChanged(const QString& appName, const QString& windowTitle);

private:
    void poll();

    QTimer*     m_timer = nullptr;

    // Последнее ЧУЖОЕ окно — своё намеренно не запоминаем
    QString     m_title;
    QString     m_process;
    QString     m_app;
    QStringList m_recentApps;      // MRU, до 6 записей

    ProjectInfoProvider m_projectInfo;
    RunningAppsProvider m_runningApps;

    static constexpr int kMaxRecentApps = 6;
};
