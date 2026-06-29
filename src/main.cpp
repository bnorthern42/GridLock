#include "backend/GridLockAutomationRunner.hpp"
#include "core/hpc/DapCoordinator.hpp"
#include "core/managers/ThemeManager.hpp"
#include "ui/MainWindow.hpp"
#include <QApplication>
#include <QColor>
#include "core/utils/CliParser.hpp"
#include "ui/tutorial/TutorialDialog.hpp"
#include <QCommandLineParser>
#include <QFile>
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QSplashScreen>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <cstdio>
#include <QFontDatabase>

bool g_isVerbose = false;

void gridlockMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    if (!g_isVerbose && (type == QtDebugMsg || type == QtInfoMsg)) {
        return;
    }

    QByteArray localMsg = msg.toLocal8Bit();
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
  format.setSwapInterval(
      0); // temporarily disabled for rendering latency profiling
  QSurfaceFormat::setDefaultFormat(format);

  QApplication app(argc, argv);

  QFontDatabase::addApplicationFont(":/fonts/SymbolsNerdFont-Regular.ttf");

  // Set org/app identity so QSettings keys are consistent everywhere.
  QApplication::setOrganizationName("gridlock");
  QApplication::setApplicationName("gridlock");
  QApplication::setApplicationVersion("0.5.3");

  // CRITICAL FOR WAYLAND:
  QGuiApplication::setDesktopFileName("gridlock");

  QApplication::setWindowIcon(QIcon(":/icon.png"));

  gridlock::core::managers::ThemeManager::instance().setTheme("Fusion", true);

  QPixmap splashPixmap(":/icon.png");
  // Scale nicely for the splash screen
  splashPixmap = splashPixmap.scaled(256, 256, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
  QSplashScreen *splash = new QSplashScreen(splashPixmap);
  splash->show();
  splash->showMessage("Loading environment...",
                      Qt::AlignBottom | Qt::AlignCenter, Qt::white);
  app.processEvents();

  gridlock::core::utils::CliParser parser;
  parser.process(app);

  g_isVerbose = parser.isVerbose();
  qInstallMessageHandler(gridlockMessageHandler);

  gridlock::ui::MainWindow window;

  auto *backend = new DapCoordinator(&window);
  window.setCoordinator(backend);

  if (parser.isTutorialMode()) {
    if (!window.execTutorialDialog()) {
        return 0; // Exit if user cancels
    }
  } else {
    window.show();
  }

  if (parser.isTestMode()) {
    QString testFile = parser.getTestFilePath();
    if (!testFile.isEmpty()) {
        QFileInfo fi(testFile);
        QString compiler = fi.suffix() == "c" ? "mpicc" : "mpic++";
        QString outDir = QDir::tempPath();
        QString outBin = outDir + "/" + fi.baseName() + "_test_demo";
        
        QProcess proc;
        proc.start(compiler, QStringList() << testFile << "-g" << "-O0" << "-o" << outBin);
        proc.waitForFinished();
        
        if (proc.exitCode() == 0) {
            window.startDebuggingSession(outBin, 2);
        }
    }
    // Ownership is passed to 'window' via Qt parent chain; no local pointer needed.
    new gridlock::backend::GridLockAutomationRunner(&window, &app);
  }

  splash->finish(&window);
  splash->deleteLater();

  return app.exec();
}
