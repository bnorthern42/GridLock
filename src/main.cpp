/**
 * @file main.cpp
 * @brief Application entry point and platform initialization for the GridLock MPI Debugger.
 *
 * Configures Wayland/Mesa surface parameters, evaluates pre-GUI command-line flags
 * (--headless and --snapshot) to select the Qt offscreen platform plugin when
 * running non-interactively, initializes the global ThemeManager and font database,
 * synchronizes the configured live debugger coordinator (`gdb` or `lldb-dap`) on MainWindow,
 * and dispatches either snapshot capture, interactive tutorial mode, automated test mode,
 * or the standard GUI event loop.
 */

#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

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
#include <QSet>
#include <QSocketNotifier>
#include <QSplashScreen>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <QThreadPool>
#include <QTimer>

#include "QtAdvancedStylesheet.h"
#include "backend/GridLockAutomationRunner.hpp"
#include "core/Version.hpp"
#include "core/hpc/gdb/GdbRankCoordinator.hpp"
#include "core/managers/ConfigManager.hpp"
#include "core/managers/ThemeManager.hpp"
#include "core/utils/ClangdLspClient.hpp"
#include "core/utils/CliParser.hpp"
#include "ui/MainWindow.hpp"
#include "ui/tutorial/TutorialDialog.hpp"

namespace {
int g_signalPipeFds[2] = {-1, -1};

void handlePosixTerminationSignal(int sig) {
  if (g_signalPipeFds[1] >= 0) {
    const char byte = static_cast<char>(sig);
    [[maybe_unused]] const ssize_t n = ::write(g_signalPipeFds[1], &byte, 1);
  }
}

bool hasElfMagicHeader(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return false;
  const QByteArray hdr = f.read(4);
  return hdr.size() == 4 && hdr[0] == '\x7f' && hdr[1] == 'E' && hdr[2] == 'L' && hdr[3] == 'F';
}
}  // namespace

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
  // Suppress benign Wayland QPA subwindow grab warnings emitted during dock/splitter drags.
  if (type == QtWarningMsg && msg.contains(QLatin1String("This plugin supports grabbing the mouse only for popup windows"))) {
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
  // Fast-path CLI version query before initializing any GUI/display platform plugin.
  // This ensures `./gridlock --version` succeeds in headless containers without X11/Wayland.
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromUtf8(argv[i]);
    if (arg == QLatin1String("--")) break;
    if (arg == QLatin1String("--version") || arg == QLatin1String("-v")) {
      // Print semantic version banner directly to stdout and exit with code 0.
      fprintf(stdout, "gridlock %s\n", gridlock::core::Version::getString().toUtf8().constData());
      fflush(stdout);
      return 0;
    }
  }

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

  // Preserve Wayland decorations by default for GNOME/KDE; only disable when frameless_window = true.
  if (waylandDisplay.contains("wayland", Qt::CaseInsensitive) && !isWsl &&
      qEnvironmentVariableIsEmpty("QT_WAYLAND_DISABLE_WINDOWDECORATION")) {
    bool wantFrameless = false;
    try {
      const QString cfgPath = gridlock::core::ConfigManager::configDirectory() + QStringLiteral("/config.toml");
      if (QFile::exists(cfgPath)) {
        const auto tbl = toml::parse_file(cfgPath.toStdString());
        wantFrameless = tbl["ui"]["frameless_window"].value_or(false);
      }
    } catch (...) {}
    if (wantFrameless) qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
  }
  qputenv("vblank_mode", "3");

  // Route to the offscreen QPA platform when --headless/--snapshot is passed or when no display server exists.
  QSurfaceFormat format;
  format.setVersion(4, 1);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
  format.setSwapInterval(0);
  QSurfaceFormat::setDefaultFormat(format);

  if (gridlock::core::utils::CliParser::shouldUseOffscreenPlatform(argc, argv)) {
    if (qEnvironmentVariableIsEmpty("GRIDLOCK_LIVE_WAYLAND") &&
        qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
      qputenv("QT_QPA_PLATFORM", "offscreen");
    }
  } else if (qgetenv("QT_QPA_PLATFORM") == QByteArrayLiteral("offscreen") &&
             (!qEnvironmentVariableIsEmpty("DISPLAY") ||
              !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))) {
    // Restore interactive X11/Wayland platform plugin when a desktop display server is active.
    qunsetenv("QT_QPA_PLATFORM");
  } else if (qEnvironmentVariableIsEmpty("DISPLAY") &&
             qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") &&
             qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    // Fallback to offscreen rendering when running inside a headless CI container.
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }

  QApplication app(argc, argv);

  // Register relative ../plugins directory so AppDir/usr/bin/gridlock locates bundled Qt plugins.
  const QString relPlugins = QCoreApplication::applicationDirPath() + QStringLiteral("/../plugins");
  if (QDir(relPlugins).exists()) {
    QCoreApplication::addLibraryPath(relPlugins);
  }

  // Register the bundled Nerd Font symbol glyphs for file tree and status badges.
  QFontDatabase::addApplicationFont(":/fonts/SymbolsNerdFont-Regular.ttf");

  // Configure organization and application metadata so QSettings paths remain deterministic.
  QApplication::setOrganizationName("gridlock");
  QApplication::setApplicationName("gridlock");
  QApplication::setApplicationVersion(gridlock::core::Version::getString());

  // Isolate --test-mode runs from the user's real ~/.config/gridlock/ only when no explicit config sandbox env is set.
  if (qEnvironmentVariableIsEmpty("XDG_CONFIG_HOME") && qEnvironmentVariableIsEmpty("GRIDLOCK_TEST_CONFIG_DIR")) {
    for (int i = 1; i < argc; ++i) {
      if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--test-mode")) {
        QStandardPaths::setTestModeEnabled(true);
        break;
      }
    }
  }

  // Bind the Wayland xdg_toplevel app_id to the desktop entry name.
  QGuiApplication::setDesktopFileName("gridlock");
  QApplication::setWindowIcon(QIcon(":/icon.png"));

  // Parse structured CLI flags (--headless, --snapshot, --tutorial-mode, --test-mode, --verbose) early.
  gridlock::core::utils::CliParser parser;
  parser.process(app);

  // Fail fast with non-zero exit code when an explicit positional target does not exist or is an invalid/non-exec binary.
  const QString rawCliTarget = parser.targetPath().trimmed();
  if (!rawCliTarget.isEmpty()) {
    const QFileInfo tInfo(rawCliTarget);
    if (!tInfo.exists()) {
      fprintf(stderr, "gridlock: target does not exist: %s\n", rawCliTarget.toUtf8().constData());
      return 1;
    }
    if (tInfo.isFile() && !tInfo.isReadable()) {
      fprintf(stderr, "gridlock: target file is not readable: %s\n", rawCliTarget.toUtf8().constData());
      return 1;
    }
    if (tInfo.isFile() && hasElfMagicHeader(tInfo.absoluteFilePath()) && !tInfo.isExecutable()) {
      fprintf(stderr, "gridlock: ELF binary lacks execute permissions: %s\n", rawCliTarget.toUtf8().constData());
      return 1;
    }
  }

  // Initialize the global workstation stylesheet and dark slate palette via ThemeManager.
  gridlock::core::managers::ThemeManager::instance().initialize();

  // Construct and display the startup splash screen only during interactive desktop launches.
  QSplashScreen* splash = nullptr;
  if (!gridlock::core::utils::CliParser::shouldUseOffscreenPlatform(argc, argv) &&
      QGuiApplication::platformName() != QStringLiteral("offscreen")) {
    QPixmap splashPixmap(":/icon.png");
    splashPixmap = splashPixmap.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    splash = new QSplashScreen(splashPixmap);
    splash->show();
    splash->showMessage("Loading environment...", Qt::AlignBottom | Qt::AlignCenter, Qt::white);
    app.processEvents();
  }

  g_isVerbose = parser.isVerbose();
  qInstallMessageHandler(gridlockMessageHandler);

  // Instantiate the primary workstation window (which synchronizes its live debugger coordinator internally).
  gridlock::ui::MainWindow window;

  // Install async-signal-safe SIGTERM/SIGINT self-pipe so external CLI shutdown triggers full closeEvent() cleanup.
  QSocketNotifier* sigNotifier = nullptr;
  if (::pipe2(g_signalPipeFds, O_NONBLOCK | O_CLOEXEC) == 0) {
    struct sigaction sa {};
    sa.sa_handler = handlePosixTerminationSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGINT, &sa, nullptr);
    sigNotifier = new QSocketNotifier(g_signalPipeFds[0], QSocketNotifier::Read, &app);
    QObject::connect(sigNotifier, &QSocketNotifier::activated, &app, [&window, &app, sigNotifier]() {
      sigNotifier->setEnabled(false);
      char buf[16];
      while (::read(g_signalPipeFds[0], buf, sizeof(buf)) > 0) {}
      window.close();
      app.quit();
    });
  }

  // Ensure initial geometry has suitable workstation dimensions before showing.
  if (window.width() < 1440 || window.height() < 900) {
    window.resize(1440, 900);
  }

  // Apply standard CLI target / workspace / `-- <program_args...>` before optional snapshot capture.
  if (!parser.isTestMode() && (!parser.targetPath().isEmpty() || !parser.programArgs().isEmpty())) {
    window.applyCliLaunchOptions(parser.targetPath(), parser.programArgs());
  }

  // If --snapshot <file> was specified, render and save the workspace PNG immediately and exit cleanly.
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
      window.close();
      return 0;
    }
  }

  // Ensure normal interactive launches always map visibly on the desktop compositor.
  window.show();
  if (!parser.isHeadless() && QGuiApplication::platformName() != QStringLiteral("offscreen")) {
    window.raise();
    window.activateWindow();
  }

  if (splash) {
    splash->finish(&window);
    splash->deleteLater();
    splash = nullptr;
  }

  // In automated test mode, launch either a compiled ELF binary or compile a C/C++ source target.
  if (parser.isTestMode()) {
    if (!parser.programArgs().isEmpty()) {
      window.applyCliLaunchOptions(QString(), parser.programArgs());
    }
    const QString testFile = parser.getTestFilePath();
    if (!testFile.isEmpty()) {
      const QFileInfo fi(testFile);
      QString launchBin;
      if (fi.isFile() && fi.isExecutable() && hasElfMagicHeader(fi.absoluteFilePath())) {
        launchBin = fi.absoluteFilePath();
      } else {
        const QString ext = fi.suffix().toLower();
        if (ext != QLatin1String("c") && ext != QLatin1String("cc") &&
            ext != QLatin1String("cpp") && ext != QLatin1String("cxx")) {
          fprintf(stderr, "gridlock: --test-mode target must be an executable ELF binary or C/C++ source: %s\n",
                  testFile.toUtf8().constData());
          window.close();
          return 1;
        }
        bool usesMpi = true;
        QFile srcReader(fi.absoluteFilePath());
        if (srcReader.open(QIODevice::ReadOnly | QIODevice::Text)) {
          const QByteArray srcBytes = srcReader.read(64 * 1024);
          usesMpi = srcBytes.contains("mpi.h");
        }
        const QString compiler = usesMpi ? (ext == QLatin1String("c") ? QStringLiteral("mpicc") : QStringLiteral("mpic++"))
                                         : (ext == QLatin1String("c") ? QStringLiteral("gcc") : QStringLiteral("g++"));
        launchBin = QDir::tempPath() + QLatin1Char('/') + fi.baseName() + QStringLiteral("_test_demo");
        QProcess proc;
        proc.start(compiler, {fi.absoluteFilePath(), QStringLiteral("-g"), QStringLiteral("-O0"), QStringLiteral("-o"), launchBin});
        proc.waitForFinished(15000);
        if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
          fprintf(stderr, "gridlock: failed to compile %s\n", testFile.toUtf8().constData());
          window.close();
          return 1;
        }
      }

      window.applyCliLaunchOptions(launchBin, parser.programArgs());
      const int configuredRanks = std::max(1, gridlock::core::ConfigManager::instance().getDefaultRanks());
      if (parser.isHeadless() || QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        auto* coord = window.coordinator();
        using CoordBase = std::remove_pointer_t<decltype(coord)>;
        auto resumedEntryStop = std::make_shared<bool>(false);
        auto finishedSession = std::make_shared<bool>(false);
        auto exitedRanks = std::make_shared<QSet<int>>();
        const auto triggerCleanExit = [&window, &app, finishedSession]() {
          if (*finishedSession) return;
          *finishedSession = true;
          QTimer::singleShot(150, &app, [&window, &app]() {
            window.close();
            app.quit();
          });
        };
        if (coord) {
          QObject::connect(coord, &CoordBase::programOutputReceived, &app,
                           [](int /*rank*/, const QString& text, bool /*isErr*/) {
                             fprintf(stdout, "%s", text.toUtf8().constData());
                             fflush(stdout);
                           });
          QObject::connect(coord, &CoordBase::targetOutputReceived, &app,
                           [](const QString& cat, const QString& text) {
                             if (cat == QLatin1String("stdout") || cat == QLatin1String("stderr")) {
                               fprintf(stdout, "%s", text.toUtf8().constData());
                               fflush(stdout);
                             }
                           });
          QObject::connect(coord, &CoordBase::temporaryBreakpointHit, &app,
                           [&window, &app, resumedEntryStop](int /*rank*/, const QString& /*f*/, int /*ln*/) {
                             if (*resumedEntryStop) return;
                             *resumedEntryStop = true;
                             QTimer::singleShot(200, &app, [&window]() { window.continueExecutionWorkflow(); });
                           });
          if (auto* gdbCoord = qobject_cast<gridlock::GdbRankCoordinator*>(coord)) {
            QObject::connect(gdbCoord, &gridlock::GdbRankCoordinator::gdbOutputReceived, &app,
                             [exitedRanks, configuredRanks, triggerCleanExit, &window, &app, resumedEntryStop](int rankId, const QString& line) {
                               if (line.contains(QLatin1String("*stopped")) &&
                                   (line.contains(QLatin1String("reason=\"exited")) || line.contains(QLatin1String("reason=\"signal-received\"")))) {
                                 if (line.contains(QLatin1String("signal-name=\"SIGSEGV\"")) ||
                                     line.contains(QLatin1String("signal-name=\"SIGABRT\"")) ||
                                     line.contains(QLatin1String("signal-name=\"SIGFPE\"")) ||
                                     line.contains(QLatin1String("signal-name=\"SIGILL\"")) ||
                                     line.contains(QLatin1String("signal-name=\"SIGBUS\""))) {
                                   triggerCleanExit();
                                   return;
                                 }
                                 exitedRanks->insert(rankId);
                                 if (exitedRanks->size() >= configuredRanks) triggerCleanExit();
                               } else if (!*resumedEntryStop && line.contains(QLatin1String("*stopped,reason=\"breakpoint-hit\""))) {
                                 *resumedEntryStop = true;
                                 QTimer::singleShot(200, &app, [&window]() { window.continueExecutionWorkflow(); });
                               }
                             });
          }
        }
        const int dwellMs = qEnvironmentVariableIntValue("GRIDLOCK_TEST_DWELL_MS") > 0
                                ? qEnvironmentVariableIntValue("GRIDLOCK_TEST_DWELL_MS")
                                : 3500;
        QTimer::singleShot(dwellMs, &app, triggerCleanExit);
      }
      window.startDebuggingSession(launchBin, configuredRanks);
    } else {
      new gridlock::backend::GridLockAutomationRunner(&window, &app);
    }
  }

  // Guarantee clean background worker, pipe, and clangd teardown while QApplication is still alive.
  QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
    gridlock::core::ClangdLspClient::instance().shutdown();
    QThreadPool::globalInstance()->waitForDone(500);
    if (g_signalPipeFds[0] >= 0) { ::close(g_signalPipeFds[0]); g_signalPipeFds[0] = -1; }
    if (g_signalPipeFds[1] >= 0) { ::close(g_signalPipeFds[1]); g_signalPipeFds[1] = -1; }
  });

  return app.exec();
}
