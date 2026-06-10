// -------------------------------------------------------
// main.cpp — Точка входа J.A.R.V.I.S.
// -------------------------------------------------------

#include "mainwindow.h"

#include <QApplication>
#include <QFont>
#include <QIcon>

// Версия из CMakeLists.txt (через -DJARVIS_VERSION="2.0.0")
#ifndef JARVIS_VERSION
#define JARVIS_VERSION "2.0.0"
#endif

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
    app.setApplicationName(QStringLiteral("Jarvis"));
    app.setApplicationVersion(QStringLiteral(JARVIS_VERSION));
    app.setOrganizationName(QStringLiteral("JARVIS Project"));

    // Без этого Qt завершит процесс когда главное окно скроется в трей
    app.setQuitOnLastWindowClosed(false);

    // Иконка приложения (окно + таскбар + Alt+Tab)
    // Файл assets/jarvis.ico добавлен в resources.qrc
    app.setWindowIcon(QIcon(":/jarvis.ico"));

    MainWindow w;
    w.show();

    return app.exec();
}
