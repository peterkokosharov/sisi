// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - application entry point
// Licensed under MIT License.
#include <QApplication>
#include <QIcon>

#include "config.h"
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Sisi Valkodav"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/sisi.svg")));

    loadConfig();          // needs the QApplication for the exe directory

    MainWindow window;
    window.show();
    return app.exec();
}
