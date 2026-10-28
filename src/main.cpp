/**
 * @file main.cpp
 * @brief Application entry point and platform initialization for the GridLock MPI Debugger.
 *
 * Configures Wayland/Mesa surface parameters, evaluates pre-GUI command-line flags
 * (--headless and --snapshot) to select the Qt offscreen platform plugin when
 * running non-interactively, initializes the global ThemeManager and font database,
 * wires the DapCoordinator backend to MainWindow, and dispatches either snapshot
 * capture, interactive tutorial mode, automated test mode, or the standard GUI event loop.
 */

#include <cstdio>

#include <QApplication>
#include <QColor>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QProcess>
#include <QSplashScreen>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QSurfaceFormat>

#include "QtAdvancedStylesheet.h"
#include "backend/GridLockAutomationRunner.hpp"
#include "core/Version.hpp"
#include "core/hpc/DapCoordinator.hpp"
#include "core/managers/ThemeManager.hpp"
#include "core/utils/CliParser.hpp"
#include "ui/MainWindow.hpp"
#include "ui/tutorial/TutorialDialog.hpp"

// Global verbosity flag toggled via the --verbose command-line option.
bool g_isVerbose = false;

/**
 * @brief Custom Qt message handler that filters debug/info logs unless verbose mode is active.
 * @param type Severity category of the emitted Qt diagnostic message.
 * @param context Source file, line, and function metadata (unused in production output).
 * @param msg Formatted diagnostic message payload.
 */
void gridlockMessageHandler(QtMsgType type, const QMessageLogContext& /*context*/, const QString& msg) {
  // Suppress noisy debug and info messages during standard non-verbose execution.
  if (!g_isVerbose && (type == QtDebugMsg || type == QtInfoMsg)) {
    return;
  }

  // Convert the UTF-16 QString into local 8-bit encoding for stderr stream output.
  const QByteArray localMsg = msg.toLocal8Bit();
  switch (type) {
    case QtDebugMsg:
      fprintf(stderr, "[DEBUG] %s\n", localMsg.constData());
      break;
    case QtInfoMsg:
      fprintf(stderr, "[INFO] %s\n", localMsg.constData());
      break;
    case QtWarningMsg:
      fprintf(stderr, "[WARNING] %s\n", localMsg.constData());
      break;
    case QtCriticalMsg:
      fprintf(stderr, "[CRITICAL] %s\n", localMsg.constData());
      break;
    case QtFatalMsg:
      fprintf(stderr, "[FATAL] %s\n", localMsg.constData());
      abort();
  }
}

/**
 * @brief Main entry point for the GridLock MPI Debugger workstation.
 * @param argc Number of command-line arguments passed by the operating system.
 * @param argv Array of null-terminated command-line argument strings.
 * @return Process exit code (0 on clean shutdown or successful snapshot capture).
 */
int main(int argc, char* argv[]) {
  // Inspect WAYLAND_DISPLAY and /proc/version to detect native Wayland vs. WSLg environments.
  const QString waylandDisplay = qEnvironmentVariable("WAYLAND_DISPLAY");
  QFile versionFile("/proc/version");
  bool isWsl = false;
  if (versionFile.open(QIODevice::ReadOnly)) {
    const QString versionStr = QString::fromUtf8(versionFile.readAll()).toLower();
    if (versionStr.contains("microsoft") || versionStr.contains("wsl")) {
      isWsl = true;
    }
  }

  // Disable client-side Wayland window decorations on native compositors to prevent buffer damage loops.
  if (waylandDisplay.contains("wayland", Qt::CaseInsensitive) && !isWsl) {
    qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
  }
  qputenv("vblank_mode", "3");

  // Request an OpenGL 4.1 Core Profile double-buffered surface with zero swap interval for low-latency rendering.
  QSurfaceFormat format;
  format.setVersion(4, 1);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
  format.setSwapInterval(0);
  QSurfaceFormat::setDefaultFormat(format);

  // Route to the offscreen QPA platform ONLY when --headless or --snapshot is explicitly requested on the CLI.
  if (gridlock::core::utils::CliParser::shouldUseOffscreenPlatform(argc, argv)) {
    if (qEnvironmentVariableIsEmpty("GRIDLOCK_LIVE_WAYLAND") &&
        qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
      qputenv("QT_QPA_PLATFORM", "offscreen");
    }
  } else if (qgetenv("QT_QPA_PLATFORM") == QByteArrayLiteral("offscreen")) {
    qunsetenv("QT_QPA_PLATFORM");
  }

  QApplication app(argc, argv);

  // Register the bundled Nerd Font symbol glyphs for file tree and status badges.
  QFontDatabase::addApplicationFont(":/fonts/SymbolsNerdFont-Regular.ttf");

  // Configure organization and application metadata so QSettings paths remain deterministic.
  QApplication::setOrganizationName("gridlock");
  QApplication::setApplicationName("gridlock");
  QApplication::setApplicationVersion(gridlock::core::Version::getString());

  // Bind the Wayland xdg_toplevel app_id to the desktop entry name.
  QGuiApplication::setDesktopFileName("gridlock");
  QApplication::setWindowIcon(QIcon(":/icon.png"));

  // Initialize the global workstation stylesheet and dark slate palette via ThemeManager.
  gridlock::core::managers::ThemeManager::instance().initialize();

  // Construct and display the startup splash screen only during interactive desktop launches.
  QSplashScreen* splash = nullptr;
  if (!gridlock::core::utils::CliParser::shouldUseOffscreenPlatform(argc, argv)) {
    QPixmap splashPixmap(":/icon.png");
    splashPixmap = splashPixmap.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    splash = new QSplashScreen(splashPixmap);
    splash->show();
    splash->showMessage("Loading environment...", Qt::AlignBottom | Qt::AlignCenter, Qt::white);
    app.processEvents();
  }

  // Parse structured CLI flags (--headless, --snapshot, --tutorial-mode, --test-mode, --verbose).
  gridlock::core::utils::CliParser parser;
  parser.process(app);

  g_isVerbose = parser.isVerbose();
  qInstallMessageHandler(gridlockMessageHandler);

  // Instantiate the primary workstation window and attach the Debug Adapter Protocol coordinator.
  gridlock::ui::MainWindow window;
  auto* backend = new DapCoordinator(&window);
  window.setCoordinator(backend);

  // Ensure initial geometry has suitable workstation dimensions before showing.
  if (window.width() < 1440 || window.height() < 900) {
    window.resize(1440, 900);
  }

  // If --snapshot <file> was specified, render and save the workspace PNG immediately and exit.
  const QString snapshotPath = parser.snapshotFilePath();
  if (!snapshotPath.isEmpty()) {
    if (splash) {
      splash->finish(&window);
      splash->deleteLater();
      splash = nullptr;
    }
    window.show();
    app.processEvents();
    const bool ok = window.captureWorkspaceSnapshot(snapshotPath);
    window.close();
    return ok ? 0 : 1;
  }

  // Launch either the interactive tutorial modal or map the main workstation window visibly.
  if (parser.isTutorialMode()) {
    if (splash) {
      splash->finish(&window);
      splash->deleteLater();
      splash = nullptr;
    }
    if (!window.execTutorialDialog()) {
      return 0;
    }
  }

  // Ensure normal interactive launches always map visibly on the desktop compositor.
  window.show();
  if (!parser.isHeadless()) {
    window.raise();
    window.activateWindow();
  }

  if (splash) {
    splash->finish(&window);
    splash->deleteLater();
    splash = nullptr;
  }

  // In automated test mode, optionally compile a positional MPI source file and start a 2-rank session.
  if (parser.isTestMode()) {
    const QString testFile = parser.getTestFilePath();
    if (!testFile.isEmpty()) {
      const QFileInfo fi(testFile);
      const QString compiler = fi.suffix() == "c" ? "mpicc" : "mpic++";
      const QString outDir = QDir::tempPath();
      const QString outBin = outDir + "/" + fi.baseName() + "_test_demo";

      QProcess proc;
      proc.start(compiler, QStringList() << testFile << "-g" << "-O0" << "-o" << outBin);
      proc.waitForFinished();

      if (proc.exitCode() == 0) {
        window.startDebuggingSession(outBin, 2);
      }
    }
    new gridlock::backend::GridLockAutomationRunner(&window, &app);
  }

  return app.exec();
}
