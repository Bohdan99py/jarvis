// -------------------------------------------------------
// main.cpp — Точка входа J.A.R.V.I.S.
// -------------------------------------------------------

#include "mainwindow.h"

#include <QApplication>
#include <QFont>

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

    MainWindow w;
    w.show();

    return app.exec();
}
