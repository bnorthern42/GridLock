#include <QApplication>
#include <QFile>
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
#include "core/hpc/DapCoordinator.hpp"

int main(int argc, char *argv[]) {
    // Wayland/Mesa environment overrides to prevent damage loops
    QString waylandDisplay = qEnvironmentVariable("WAYLAND_DISPLAY");
    QFile versionFile("/proc/version");
    bool isWsl = false;
    if (versionFile.open(QIODevice::ReadOnly)) {
        QString versionStr = QString::fromUtf8(versionFile.readAll()).toLower();
        if (versionStr.contains("microsoft") || versionStr.contains("wsl")) {
            isWsl = true;
        }
    }
    
    if (waylandDisplay.contains("wayland", Qt::CaseInsensitive) && !isWsl) {
        qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
    }
    qputenv("vblank_mode", "3");

    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(0); // temporarily disabled for rendering latency profiling
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);

    // Set org/app identity so QSettings keys are consistent everywhere.
    QApplication::setOrganizationName("GridLock");
    QApplication::setApplicationName("GridLock");
    QApplication::setApplicationVersion("0.5.0");
    
    // CRITICAL FOR WAYLAND:
    QGuiApplication::setDesktopFileName("gridlock"); 
    
    QApplication::setWindowIcon(QIcon(":/icon.png"));

    gridlock::core::managers::ThemeManager::instance().setTheme("Fusion", true);

    QPixmap splashPixmap(":/icon.png");
    // Scale nicely for the splash screen
    splashPixmap = splashPixmap.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QSplashScreen *splash = new QSplashScreen(splashPixmap);
    splash->show();
    splash->showMessage("Loading environment...", Qt::AlignBottom | Qt::AlignCenter, Qt::white);
    app.processEvents();

    QCommandLineParser parser;
    parser.setApplicationDescription("GridLock MPI Debugger - v0.5.0\nFeaturing the new Zero-Copy Multi-Rank Memory Diff Engine.");
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption testModeOption("test-mode", "Run interactive visual simulation tests");
    parser.addOption(testModeOption);
    
    parser.process(app);

    gridlock::ui::MainWindow window;

    auto* backend = new DapCoordinator(&window);
    window.setCoordinator(backend);

    window.show();

    if (parser.isSet(testModeOption)) {
        // Ownership is passed to 'window' via Qt parent chain; no local pointer needed.
        new gridlock::backend::GridLockAutomationRunner(&window, &app);
    }
    
    splash->finish(&window);
    splash->deleteLater();

    return app.exec();
}
