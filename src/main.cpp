#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>
#include <QCommandLineParser>
#include "ui/MainWindow.hpp"
#include "ui/ThemeManager.hpp"
#include "backend/GridLockAutomationRunner.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Set org/app identity so QSettings keys are consistent everywhere.
    QApplication::setOrganizationName("GridLock");
    QApplication::setApplicationName("Debugger");

    // Always use Fusion + an explicit Catppuccin-Mocha dark palette.
    // Breeze on CachyOS/KDE defaults to its light theme unless the user has
    // configured a dark color scheme system-wide, which would make our dark
    // widget stylesheets produce white-on-white rendering.  Fusion with an
    // explicit palette is unconditionally correct and the DontUseNativeDialog
    // flag ensures file pickers inherit this palette rather than GTK/native.
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    QPalette dark;
    dark.setColor(QPalette::Window,          QColor(30,  30,  46));   // #1e1e2e
    dark.setColor(QPalette::WindowText,      QColor(205, 214, 244));  // #cdd6f4
    dark.setColor(QPalette::Base,            QColor(24,  24,  37));   // #181825
    dark.setColor(QPalette::AlternateBase,   QColor(49,  50,  68));   // #313244
    dark.setColor(QPalette::Text,            QColor(205, 214, 244));
    dark.setColor(QPalette::BrightText,      QColor(243, 139, 168));  // #f38ba8 (errors)
    dark.setColor(QPalette::Button,          QColor(69,  71,  90));   // #45475a
    dark.setColor(QPalette::ButtonText,      QColor(205, 214, 244));
    dark.setColor(QPalette::Highlight,       QColor(203, 166, 247));  // #cba6f7
    dark.setColor(QPalette::HighlightedText, QColor(30,  30,  46));
    dark.setColor(QPalette::Link,            QColor(137, 180, 250));  // #89b4fa
    dark.setColor(QPalette::LinkVisited,     QColor(203, 166, 247));
    dark.setColor(QPalette::Mid,             QColor(49,  50,  68));
    dark.setColor(QPalette::Midlight,        QColor(58,  60,  82));
    dark.setColor(QPalette::Dark,            QColor(17,  17,  27));   // #11111b
    dark.setColor(QPalette::Shadow,          QColor(0,   0,   0));
    dark.setColor(QPalette::ToolTipBase,     QColor(49,  50,  68));
    dark.setColor(QPalette::ToolTipText,     QColor(205, 214, 244));
    // Disabled-state roles: muted versions of active colours.
    dark.setColor(QPalette::Disabled, QPalette::WindowText, QColor(108, 112, 134)); // #6c7086
    dark.setColor(QPalette::Disabled, QPalette::Text,       QColor(108, 112, 134));
    dark.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(108, 112, 134));
    QApplication::setPalette(dark);

    // Apply the centralized ThemeManager QSS on top of the Fusion palette.
    gridlock::ui::ThemeManager::instance().applyGlobalTheme(app);

    QCommandLineParser parser;
    parser.setApplicationDescription("GridLock MPI Debugger");
    parser.addHelpOption();
    
    QCommandLineOption testModeOption("test-mode", "Run interactive visual simulation tests");
    parser.addOption(testModeOption);
    
    parser.process(app);

    gridlock::ui::MainWindow window;
    window.show();

    if (parser.isSet(testModeOption)) {
        // Ownership is passed to 'window' via Qt parent chain; no local pointer needed.
        new gridlock::backend::GridLockAutomationRunner(&window, &app);
    }
    
    return app.exec();
}
