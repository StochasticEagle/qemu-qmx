/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mainwindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QEMU QMX Builder"));
    QCoreApplication::setOrganizationName(QStringLiteral("StochasticEagle"));
    QGuiApplication::setDesktopFileName(
        QStringLiteral("io.github.StochasticEagle.QmxBuilder"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("qemu")));

    MainWindow window;
    if (application.arguments().size() > 1) {
        window.openFile(application.arguments().at(1));
    }
    window.show();
    return application.exec();
}
