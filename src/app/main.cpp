// -------------------------------------------------------
// main.cpp — Точка входа J.A.R.V.I.S.
// -------------------------------------------------------

#include "mainwindow.h"
#include "database_manager.h"
#include "background_learner.h"
#include "jarvis_paths.h"
#include "jarvis_theme.h"

#include <QApplication>
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

    // ── 4. Главное окно ──────────────────────────────────────
    MainWindow w;
    w.show();

    int result = app.exec();

    // ── 5. Чистое завершение ─────────────────────────────────
    learner.stop();
    db.close();

    return result;
}
