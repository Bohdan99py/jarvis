// -------------------------------------------------------
// main.cpp — Точка входа J.A.R.V.I.S.
// -------------------------------------------------------

#include "mainwindow.h"
#include "database_manager.h"
#include "background_learner.h"
#include "jarvis_paths.h"

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
    app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
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
