// -------------------------------------------------------
// main.cpp — Точка входа J.A.R.V.I.S.
// -------------------------------------------------------

#include "mainwindow.h"
#include "tray_presence.h"
#include "push_to_talk.h"
#include "command_palette.h"
#include "action_registry.h"
#include "global_search.h"
#include "lang.h"
#include "jarvis.h"
#include "database_manager.h"
#include "background_learner.h"
#include "jarvis_paths.h"
#include "jarvis_theme.h"

#include <QApplication>

#ifdef JARVIS_HAS_WEBENGINE
#include <QtWebEngineQuick>
#endif
#include <QFont>
#include <QIcon>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>

#ifndef JARVIS_VERSION
#define JARVIS_VERSION "2.5.0"
#endif

namespace {
// jarvis.exe is a WIN32-subsystem app — it has no console, so qDebug/
// qWarning/qCritical (including Qt's own QML engine diagnostics) go
// nowhere by default and are impossible to inspect after the fact. This
// mirrors them to a plain text log file so behavior can actually be
// diagnosed post-hoc instead of only via live debugger attachment.
void fileMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);

    static QFile logFile(JarvisPaths::subPath(QStringLiteral("logs/jarvis.log")));
    if (!logFile.isOpen()) {
        QDir().mkpath(QFileInfo(logFile).absolutePath());
        logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }

    const char* level = "DEBUG";
    switch (type) {
    case QtWarningMsg:  level = "WARN"; break;
    case QtCriticalMsg: level = "CRIT"; break;
    case QtFatalMsg:    level = "FATAL"; break;
    default: break;
    }

    QTextStream out(&logFile);
    out << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))
        << " [" << level << "] " << msg << Qt::endl;
}
}

int main(int argc, char* argv[])
{
    qInstallMessageHandler(fileMessageHandler);

#ifdef JARVIS_HAS_WEBENGINE
    // ОБЯЗАТЕЛЬНО до QApplication: WebEngine Quick поднимает свой
    // процесс Chromium и выставляет формат поверхности, и сделать это
    // после создания приложения уже нельзя. Панель диаграмм без
    // этого вызова просто не отрисуется.
    QtWebEngineQuick::initialize();
#endif

    QApplication app(argc, argv);

    // Меню и тултипы появляются с затуханием, а не мгновенным щелчком.
    // Это встроенные эффекты Qt, а не собственная анимация: они применяются
    // ко ВСЕМ меню приложения сразу, включая те, что будут созданы позже,
    // тогда как ручная анимация пришлось бы вешать на каждое меню отдельно
    // и она бы разъезжалась с новыми.
    QApplication::setEffectEnabled(Qt::UI_AnimateMenu,    true);
    QApplication::setEffectEnabled(Qt::UI_FadeMenu,       true);
    QApplication::setEffectEnabled(Qt::UI_AnimateCombo,   true);
    QApplication::setEffectEnabled(Qt::UI_AnimateTooltip, true);
    QApplication::setEffectEnabled(Qt::UI_FadeTooltip,    true);

    // Дизайн-токены регистрируем до создания любого QQuickWidget —
    // иначе QML-экраны стартуют с неразрешённым import Jarvis.Theme.
    // Базовый шрифт берём из той же шкалы, что и QML: один источник
    // правды и для виджетов, и для QML (см. jarvis_theme.h).
    JarvisTheme::registerQmlTypes();
    app.setFont(QFont(QStringLiteral("Segoe UI Variable Text"),
                      JarvisType::instance().body() * 3 / 4)); // px -> pt @96dpi

    app.setApplicationName(QStringLiteral("Jarvis"));
    app.setApplicationVersion(QStringLiteral(JARVIS_VERSION));
    app.setOrganizationName(QStringLiteral("JARVIS Project"));

    // Без этого Qt завершит процесс когда главное окно скроется в трей
    app.setQuitOnLastWindowClosed(false);

    // Иконка приложения (окно + таскбар + Alt+Tab)
    app.setWindowIcon(QIcon(":/jarvis.ico"));

    // ── 1. Открываем БД ─────────────────────────────────────
    auto& db = DatabaseManager::instance();
    if (!db.open()) {
        qCritical() << "[JARVIS] Failed to open database:" << db.lastError();
        // Не крашимся — JARVIS может работать без БД в ограниченном режиме
    } else {
        qDebug() << "[JARVIS] Database ready";
    }

    // ── 2. Восстанавливаем настройки из БД ──────────────────
    // (тема, язык, сценарий пользователя — было в JSON, теперь в SQLite)
    // Пример: QString theme = db.getConfig("theme", "dark").toString();

    // ── 3. Фоновое обучение ──────────────────────────────────
    // Пути к проекту игры берём из БД (пользователь настраивает в UI)
    // Если не установлен — пропускаем, не падаем
    static BackgroundLearner learner;

    QString gamePath = db.getConfig("game_project_path").toString();
    if (!gamePath.isEmpty() && QDir(gamePath).exists()) {
        learner.setWatchPaths({ gamePath });
        learner.start();
        qDebug() << "[JARVIS] Background learner started for:" << gamePath;
    } else {
        qDebug() << "[JARVIS] No game project path configured — learner not started";
        qDebug() << "[JARVIS] Set 'game_project_path' in settings to enable RAG indexing";
    }

    // Логируем когда обучение заканчивает цикл
    QObject::connect(&learner, &BackgroundLearner::learningFinished,
                     [](int files, int patterns) {
        qDebug() << "[JARVIS] Learning cycle done:"
                 << files << "files indexed,"
                 << patterns << "patterns updated";
    });

    // ── 4. Ядро ──────────────────────────────────────────────
    // Ядро создаётся здесь, а не внутри окна. Разница не косметическая:
    // пока Jarvis был ребёнком MainWindow, всё, что работает в фоне —
    // лента событий, триггеры, наблюдение за системой — существовало
    // ровно столько, сколько существовал объект окна. Теперь окно можно
    // уничтожить и создать заново, а ядро продолжит работать: оно
    // объявлено раньше и разрушается позже (обратный порядок).
    Jarvis core;

    // ── 5. Команды и палитра ─────────────────────────────────
    // Реестр команд объявлен раньше окна и разрушается позже: окно его
    // наполняет, но не владеет им. Палитра берёт оттуда список и запускает
    // выбранное — Ctrl+Space перестал зависеть от того, открыто ли окно.
    ActionRegistry actions;

    CommandPalette palette(&core);   // top-level, без родителя
    palette.setCommandRunner([&actions](const QString& actionId) {
        actions.run(actionId);
    });
    installPaletteHotkeys(&palette, &palette);

    // Команды в Ctrl+K. Раньше провайдер жил в окне — вместе с ним
    // исчезал бы и поиск по командам.
    if (GlobalSearch* search = core.search()) {
        search->addProvider(QStringLiteral("commands"),
                            [&actions](const QString& q, int limit) {
            const QString ql = q.toLower();
            QVector<SearchHit> hits;
            for (const AppAction& a : actions.all()) {
                const int score = qMax(GlobalSearch::matchScore(a.title, ql),
                                       GlobalSearch::matchScore(a.hint, ql) / 3);
                if (score <= 0)
                    continue;

                SearchHit h;
                h.category = IS_EN ? QStringLiteral("Commands") : QStringLiteral("Команды");
                h.icon     = a.icon.isEmpty() ? QStringLiteral("▸") : a.icon;
                h.title    = a.title;
                h.subtitle = a.shortcut.isEmpty() ? a.hint
                                                  : a.shortcut + QStringLiteral("   ") + a.hint;
                h.action   = SearchHit::Action::RunCommand;
                h.payload  = a.id;
                h.score    = score;
                hits.append(h);
                if (hits.size() >= limit)
                    break;
            }
            return hits;
        });
    }

    // ── 6. Главное окно — один из интерфейсов к ядру ──────────
    MainWindow w(&core, &actions);
    w.show();

    // ── 7. Трей — ещё один интерфейс к тому же ядру ───────────
    // Не часть окна: он переживает его закрытие и берёт состояние,
    // события, режимы и сценарии прямо из Core.
    TrayPresence tray(&core);
    w.setHideOnClose(tray.isAvailable());

    QObject::connect(&tray, &TrayPresence::openWindowRequested,
                     &w, &MainWindow::showAndRaise);
    QObject::connect(&tray, &TrayPresence::askRequested,
                     &palette, [&palette]() {
        palette.togglePalette(CommandPalette::Mode::Act);
    });
    QObject::connect(&tray, &TrayPresence::quitRequested,
                     &app, &QApplication::quit);

    // ── 8. Push-to-talk ──────────────────────────────────────
    // Держишь Win+J — говоришь, отпустил — выполняется. Главное окно при
    // этом не открывается и фокус не забирается.
    PushToTalk ptt(&core);

    // Распознавание запускаем последним: Vosk при первом запуске просит
    // доустановить модели, и просьба уходит сигналом — к этому моменту
    // окно уже подписано и может показать диалог. Без окна ядро просто
    // молча поднимет микрофон.
    core.startVoice();

    int result = app.exec();

    // ── 9. Чистое завершение ─────────────────────────────────
    learner.stop();
    db.close();

    return result;
}
