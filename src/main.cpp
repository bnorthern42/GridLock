#include <QApplication>
#include <QSurfaceFormat>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>
#include <QCommandLineParser>
#include <QSplashScreen>
#include <QIcon>
#include <QPixmap>
#include "ui/MainWindow.hpp"
#include "core/managers/ThemeManager.hpp"
#include "backend/GridLockAutomationRunner.hpp"

int main(int argc, char *argv[]) {
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(1); // CRITICAL: Forces VSync
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);

    // Set org/app identity so QSettings keys are consistent everywhere.
    QApplication::setOrganizationName("GridLock");
    QApplication::setApplicationName("GridLock");
    
    // CRITICAL FOR WAYLAND:
    QGuiApplication::setDesktopFileName("gridlock.desktop"); 
    
    QApplication::setWindowIcon(QIcon(":/icon.png"));

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

    QPixmap splashPixmap(":/icon.png");
    // Scale nicely for the splash screen
    splashPixmap = splashPixmap.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QSplashScreen *splash = new QSplashScreen(splashPixmap);
    splash->show();
    splash->showMessage("Loading environment...", Qt::AlignBottom | Qt::AlignCenter, Qt::white);
    app.processEvents();

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
    
    splash->finish(&window);
    splash->deleteLater();

    return app.exec();
}
