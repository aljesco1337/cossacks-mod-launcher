#include <QApplication>

#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("CossacksLogViewer"));
    QApplication::setOrganizationName(QStringLiteral("CossacksModLauncher"));
    // Compiled in by CMake (see CLV_APP_VERSION), so the About box and the
    // update check both read the same version as the release tag.
    QApplication::setApplicationVersion(QStringLiteral(CLV_APP_VERSION));

    MainWindow window;
    window.show();

    return application.exec();
}
