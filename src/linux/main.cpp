#include <QApplication>

#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("CossacksLogViewer"));
    QApplication::setOrganizationName(QStringLiteral("CossacksModLauncher"));
    QApplication::setApplicationVersion(QStringLiteral("0.1"));

    MainWindow window;
    window.show();

    return application.exec();
}
