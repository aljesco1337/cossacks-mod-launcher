#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QString>
#include <QStyle>
#include <QStyleFactory>

#include "MainWindow.h"

namespace {

// Qt's native Windows styles (windowsvista/windows11) paint the widgets Qt draws
// itself - menu bar, menus, table headers, scroll bars, check boxes - with the
// system theme and ignore the application palette. That is invisible while the
// light theme is on, but the dark theme then shows pale text on pale backgrounds.
// Fusion paints everything from the palette, which is exactly what this app's
// theme switching relies on, so it is used instead. Another style is only kept
// when it was asked for on the command line (-style <name>).
void UsePaletteAwareStyle()
{
    const QString current = QApplication::style()->objectName().toLower();

    if (current != QLatin1String("windowsvista") && current != QLatin1String("windows11"))
    {
        return;
    }

    for (const QString& argument : QCoreApplication::arguments())
    {
        if (argument == QLatin1String("-style") || argument.startsWith(QLatin1String("-style=")))
        {
            return;
        }
    }

    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
    {
        QApplication::setStyle(fusion);
    }
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("CossacksLogViewer"));
    QApplication::setOrganizationName(QStringLiteral("CossacksModLauncher"));
    // Compiled in by CMake (see CLV_APP_VERSION), so the About box and the
    // update check both read the same version as the release tag.
    QApplication::setApplicationVersion(QStringLiteral(CLV_APP_VERSION));

    // Before any widget exists: the style cannot be swapped afterwards.
    UsePaletteAwareStyle();

    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/mod-launcher.png")));

    MainWindow window;
    window.show();

    return application.exec();
}
