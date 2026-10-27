#include "MainWindow.hpp"
#include "../core/hpc/DapCoordinator.hpp"
#include "../core/hpc/GdbRankCoordinator.hpp"
#include "../core/hpc/LspCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "EditorTabManager.hpp"
#include "GdbConsoleWidget.hpp"
#include "dialogs/PreferencesDialog.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/ReferenceManualWidget.hpp"

#include "../core/commands/DebugCommands.hpp"
#include "../core/Version.hpp"
#include "../core/hpc/DeadlockAnalyzer.hpp"
#include "../core/hpc/MemoryDiffer.hpp"
#include "../core/hpc/MockHpcBackend.hpp"
#include "../core/managers/SessionManager.hpp"
#include "../core/managers/ShortcutManager.hpp"
#include "../core/managers/SpackManager.hpp"
#include "../core/managers/ThemeManager.hpp"
#include "dialogs/ProjectSettingsDialog.hpp"
#include "dialogs/ProjectWizard.hpp"
#include "tutorial/TutorialDialog.hpp"
#include "widgets/MpiNetworkLogWidget.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "docks/TerminalDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "widgets/ExpressionEvaluatorWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"
#include <QFileDialog>
#include <QPointer>
#include <QtConcurrent/QtConcurrent>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>
#include <QLabel>
#include <QScreen>
#include <QGuiApplication>
#include <QShortcut>
#include <QStatusBar>
#include <QTabBar>
#include <QTextCursor>
#include <algorithm>

namespace gridlock {
class PlainJaneTooltip : public QLabel {
public:
    PlainJaneTooltip(QWidget* parent = nullptr) : QLabel(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus) {
        setStyleSheet("padding: 4px;");
        setTextFormat(Qt::RichText);
        setAttribute(Qt::WA_ShowWithoutActivating);
    }
    
    void showTooltip(const QPoint& globalPos, const QString& text) {
        setText(text);
        adjustSize();
        QPoint pos = globalPos + QPoint(10, 10);
        if (QScreen* screen = QGuiApplication::screenAt(globalPos)) {
            QRect avail = screen->availableGeometry();
            if (pos.x() + width() > avail.right()) pos.setX(globalPos.x() - width() - 10);
            if (pos.y() + height() > avail.bottom()) pos.setY(globalPos.y() - height() - 10);
            pos.setX(qMax(pos.x(), avail.left()));
            pos.setY(qMax(pos.y(), avail.top()));
        }
        move(pos);
        show();
    }
    
    void hideTooltip() {
        hide();
    }
};
}

namespace gridlock::ui {

MpiNetworkLogWidget* MainWindow::networkLog() const {
    if (m_mpiDiagnosticsWidget) {
        return m_mpiDiagnosticsWidget->networkLogWidget();
    }
    return nullptr;
}

SourceCodeView *MainWindow::sourceCodeView() const {
  return m_editorTabManager ? m_editorTabManager->currentSourceCodeView()
                            : nullptr;
}

SourceCodeView *MainWindow::getSourceCodeView() const {
  return sourceCodeView();
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  m_hpcBackend = new gridlock::core::MockHpcBackend(this);
  setupUi();
  setupMenu();
  setupDocks();
  setupStatusBar();

  gridlock::core::ShortcutManager::instance().initialize(this);
  qApp->installEventFilter(this);

  // Eagerly load default source file so UI is populated before running
  m_lspCoordinator = new gridlock::core::LspCoordinator(this);
  m_plainJaneTooltip = new gridlock::PlainJaneTooltip(this);
  if (QFileInfo::exists("tutorial/inspection_demo.cpp")) {
      loadSourceFile("tutorial/inspection_demo.cpp");
  }

  // Load offline persistence TOML breakpoints
  m_persistentBreakpoints =
      gridlock::core::ConfigManager::instance().getBreakpoints();

  // Restore layout state (versioned for Cutter 4-zone workspace)
  QSettings settings("gridlock", "debugger");
  if (settings.contains("MainWindow/Geometry_v6")) {
      restoreGeometry(settings.value("MainWindow/Geometry_v6").toByteArray());
  }
  if (settings.contains("MainWindow/State_v6")) {
      restoreState(settings.value("MainWindow/State_v6").toByteArray());
  }
  
  // Restore splitter states
  for (auto* splitter : findChildren<QSplitter*>()) {
      if (!splitter->objectName().isEmpty()) {
          auto state = settings.value("MainWindow/Splitter_v6_" + splitter->objectName()).toByteArray();
          if (!state.isEmpty()) {
              splitter->restoreState(state);
          }
      }
  }

  for (auto* tabBar : findChildren<QTabBar*>()) {
      if (tabBar->count() == 0) {
          tabBar->hide();
      }
      tabBar->setUsesScrollButtons(true);
      tabBar->setElideMode(Qt::ElideNone);
  }
}

void MainWindow::executeCommand(
    std::unique_ptr<gridlock::core::commands::IDebugCommand> cmd) {
  if (cmd) {
    cmd->execute();
  }
}

void MainWindow::setCoordinator(IBackendCoordinator *coord) {
  m_coordinator = coord;

  if (auto *dapCoord = dynamic_cast<DapCoordinator *>(m_coordinator)) {
    connect(dapCoord, &DapCoordinator::stateChanged, this,
            [this, dapCoord](SessionState state) {
              if (m_runAction) {
                if (state == SessionState::Disconnected) {
                  m_runAction->setEnabled(true);
                  m_runAction->setText("▶ Run Target");
                  updateStatusTelemetry(QString(), QStringLiteral("IDLE"));
                } else {
                  m_runAction->setEnabled(false);
                  if (state == SessionState::Launching) {
                    m_runAction->setText("Launching...");
                    updateStatusTelemetry(QString(), QStringLiteral("LAUNCHING"));
                  } else if (state == SessionState::Queued) {
                    m_runAction->setText("Queued in SLURM...");
                    updateStatusTelemetry(QString(), QStringLiteral("QUEUED"));
                  } else if (state == SessionState::Running) {
                    m_runAction->setText("Running");
                    updateStatusTelemetry(QString(), QStringLiteral("RUNNING"));
                    for (int i = 0; i < dapCoord->getProcessCount(); ++i) {
                        RankState rs;
                        if (m_latestStates.contains(i)) {
                            rs = m_latestStates[i];
                        }
                        rs.currentState = "running";
                        rs.executionTimer.start();
                        onRankStateChanged(i, rs);
                    }
                  } else if (state == SessionState::Paused) {
                    m_runAction->setText("Paused");
                    updateStatusTelemetry(QString(), QStringLiteral("PAUSED"));
                  }
                }
              }
            });

    connect(dapCoord, &DapCoordinator::executionStopped, this,
            [this](int rankId, const QString& /*reason*/) {
                RankState rs;
                if (m_latestStates.contains(rankId)) {
                    rs = m_latestStates[rankId];
                }
                rs.currentState = "stopped";
                rs.totalRuntimeMs += rs.executionTimer.isValid() ? rs.executionTimer.elapsed() : 0;
                onRankStateChanged(rankId, rs);
            });
  }

  if (m_variablesDockWidget) {
    m_variablesDockWidget->setCoordinator(m_coordinator);
    if (auto *gdbCoord =
            dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {

      if (!m_deadlockAnalyzer) {
        m_deadlockAnalyzer =
            new gridlock::core::DeadlockAnalyzer(gdbCoord, this);
        connect(gdbCoord, &gridlock::GdbRankCoordinator::rankStateChanged,
                this, &MainWindow::onRankStateChanged);
        if (m_mpiDiagnosticsWidget) {
          connect(m_deadlockAnalyzer,
                  &gridlock::core::DeadlockAnalyzer::deadlockDetected,
                  m_mpiDiagnosticsWidget,
                  &MpiDiagnosticsWidget::onDeadlockDetected);
          connect(m_deadlockAnalyzer,
                  &gridlock::core::DeadlockAnalyzer::rankCleared,
                  m_mpiDiagnosticsWidget, &MpiDiagnosticsWidget::onRankCleared);
        }
      }
    }
  }

  if (m_coordinator) {
    connect(m_coordinator, &IBackendCoordinator::locationChanged, this,
            [this](int rankId, const QString &file, int line) {
              if (rankId == m_focusedRank) {
                if (!file.isEmpty() && m_editorTabManager) {
                  if (auto view = m_editorTabManager->openFile(file)) {
                    view->highlightCurrentLine(line);
                  }
                } else if (getSourceCodeView()) {
                  getSourceCodeView()->highlightCurrentLine(line);
                }
              }
            });

    if (auto *gdbCoord =
            dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
      connect(gdbCoord, &gridlock::GdbRankCoordinator::hoverEvaluationComplete,
              this, [this](QString varName, QString result, QPoint globalPos) {
                if (getSourceCodeView() && m_plainJaneTooltip) {
                  m_plainJaneTooltip->showTooltip(
                      globalPos,
                      QString("<b>%1</b>: %2").arg(varName).arg(result));
                }
              });
      if (m_expressionEvaluatorWidget) {
        connect(gdbCoord, &gridlock::GdbRankCoordinator::expressionEvaluated,
                m_expressionEvaluatorWidget,
                &ExpressionEvaluatorWidget::appendResult);
      }
      if (m_gdbConsoleWidget) {
        connect(gdbCoord, &gridlock::GdbRankCoordinator::gdbOutputReceived,
                m_gdbConsoleWidget, &GdbConsoleWidget::appendGdbOutput);
        connect(gdbCoord, &gridlock::GdbRankCoordinator::commandSentToGdb,
                m_gdbConsoleWidget, &GdbConsoleWidget::appendGdbInput);
        connect(m_gdbConsoleWidget, &GdbConsoleWidget::commandEntered, gdbCoord,
                &gridlock::GdbRankCoordinator::sendCommand);
      }
      connect(gdbCoord, &gridlock::GdbRankCoordinator::gdbOutputReceived, this,
              [this](int rankId, const QString &output) {
                if (output.contains("reason=\"signal-received\"") &&
                    output.contains("signal-name=\"SIGFPE\"")) {
                  onRankSelected(rankId);
                  if (networkLog()) {
                    networkLog()->appendError(
                        QString("SIGFPE (Floating-Point Exception) detected on "
                                "Rank %1!\n")
                            .arg(rankId));
                  }
                }
              });
      connect(
          gdbCoord, &gridlock::GdbRankCoordinator::memoryDataReady, this,
          [this](int rankId, qint64 beginAddress, const QString &hexContents) {
            if (rankId == m_focusedRank && m_memView) {
              m_memView->setMemoryData(beginAddress, hexContents);
            }
          });
    }

    if (networkLog()) {
      connect(m_coordinator, &IBackendCoordinator::targetOutputReceived,
              this,
              [this](const QString &category, const QString &output) {
                if (networkLog()) networkLog()->appendText(category, output);
              });
    }
    connect(m_coordinator, &IBackendCoordinator::memoryRead, this,
            [this](int rankId, const QString &address, const QByteArray &data) {
              if (rankId == m_focusedRank && m_memView) {
                m_memView->setMemoryData(address, data);
              }
            });
    connect(m_coordinator, &IBackendCoordinator::registersUpdated, this,
            [this](int rankId, const QJsonArray &registers) {
              if (rankId == m_focusedRank && m_registerView) {
                m_registerView->updateRegisters(registers);
              }
            });
  }
}

void MainWindow::setupUi() {
  setWindowTitle("GridLock - MPI Graphical Debugger");
  setWindowIcon(QIcon());
  resize(1440, 900);
  applyCutterWorkspaceLayout();
  setupToolbar();
}

void MainWindow::applyCutterWorkspaceLayout() {
  setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AnimatedDocks);
  setDockNestingEnabled(true);
  setDocumentMode(true);
  setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
  setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
}

bool MainWindow::captureWorkspaceSnapshot(const QString& outputPath) {
  QToolTip::hideText();
  if (m_plainJaneTooltip) {
    m_plainJaneTooltip->hideTooltip();
  }
  for (QWidget *topLevel : QApplication::topLevelWidgets()) {
    if (topLevel != this && (topLevel->windowFlags() & Qt::ToolTip)) {
      topLevel->hide();
    }
  }
  for (QLabel *lbl : findChildren<QLabel *>()) {
    if (lbl->text().startsWith("Size:")) {
      lbl->hide();
    }
  }
  this->ensurePolished();
  if (this->layout()) {
    this->layout()->activate();
  }
  QCoreApplication::processEvents();
  QPixmap pixmap = this->grab();
  if (pixmap.isNull()) {
    return false;
  }
  return pixmap.save(outputPath, "PNG");
}

void MainWindow::setupStatusBar() {
  QStatusBar *bar = statusBar();
  bar->setObjectName("cutterStatusBar");
  bar->setSizeGripEnabled(false);
  bar->setContentsMargins(6, 0, 6, 0);

  if (m_currentSeekOrPc == QStringLiteral("0x00000000") || m_currentSeekOrPc.isEmpty()) {
    m_currentSeekOrPc = QStringLiteral("0x00401120");
  }

  m_statusTelemetryLabel = new QLabel(bar);
  m_statusTelemetryLabel->setObjectName("cutterStatusTelemetry");
  m_statusTelemetryLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
  bar->addPermanentWidget(m_statusTelemetryLabel, 1);
  updateStatusTelemetry(m_currentSeekOrPc, m_currentTargetState);
}

void MainWindow::updateStatusTelemetry(const QString& pcOrSeek, const QString& targetState) {
  if (!pcOrSeek.isEmpty()) {
    m_currentSeekOrPc = pcOrSeek;
  }
  if (!targetState.isEmpty()) {
    m_currentTargetState = targetState;
  }
  if (m_statusTelemetryLabel) {
    m_statusTelemetryLabel->setText(
        QStringLiteral("ARCH: x86_64 | MPI RANK: %1 | PC/SEEK: %2 | STATUS: %3")
            .arg(m_focusedRank)
            .arg(m_currentSeekOrPc)
            .arg(m_currentTargetState));
  }
}

bool MainWindow::seekToAddressOrExpression(const QString& expr) {
  const QString cleaned = expr.trimmed();
  if (cleaned.isEmpty()) {
    return false;
  }

  QString normalized = cleaned;
  bool isHexWithoutPrefix = false;
  if (!normalized.startsWith("0x", Qt::CaseInsensitive)) {
    (void)normalized.toULongLong(&isHexWithoutPrefix, 16);
    if (isHexWithoutPrefix && normalized.length() >= 4) {
      normalized = QStringLiteral("0x") + normalized;
    }
  }

  if (m_seekBar && m_seekBar->text() != normalized) {
    m_seekBar->setText(normalized);
  }

  updateStatusTelemetry(normalized);

  // Parse hex address if applicable
  bool hexOk = false;
  QString hexPart = normalized;
  if (hexPart.startsWith("0x", Qt::CaseInsensitive)) {
    hexPart = hexPart.mid(2);
  }
  const uint64_t parsedAddr = hexPart.toULongLong(&hexOk, 16);

  // Synchronize with Memory View address bar and request memory if coordinator is active
  if (m_memView) {
    if (hexOk && parsedAddr != 0ULL) {
      m_memView->seekToAddress(parsedAddr);
    }
    if (auto *addrEdit = m_memView->findChild<QLineEdit *>()) {
      addrEdit->setText(normalized);
    }
    if (m_coordinator && m_currentTargetState != QStringLiteral("IDLE")) {
      m_coordinator->readMemory(m_focusedRank, normalized, 256);
    }
  }

  // Synchronize with DisassemblyView by forwarding hex addresses and highlighting symbols
  if (m_disassemblyView) {
    if (hexOk && parsedAddr != 0ULL) {
      m_disassemblyView->seekToAddress(parsedAddr);
    } else if (!m_disassemblyView->toPlainText().isEmpty()) {
      QTextDocument *doc = m_disassemblyView->document();
      if (doc) {
        QTextCursor found = doc->find(normalized);
        if (!found.isNull()) {
          m_disassemblyView->setTextCursor(found);
          m_disassemblyView->centerCursor();
        }
      }
    }
  }

  return true;
}

void MainWindow::setupMenu() {
  QMenuBar *menuBar = new QMenuBar(this);
  setMenuBar(menuBar);

  QMenu *fileMenu = menuBar->addMenu("&File");
  QAction *newAction = fileMenu->addAction("New Project...");
  connect(newAction, &QAction::triggered, this, [this]() {
    gridlock::ui::dialogs::ProjectWizard dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
      QString projectRoot = dialog.getProjectRoot();
      if (projectRoot.isEmpty())
        return;

      if (m_differentialGrid)
        m_differentialGrid->clearWatches();
      if (m_editorTabManager)
        m_editorTabManager->clearAllTabs();

      gridlock::core::ConfigManager::instance().setWorkspace(projectRoot);

      auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
      ps.targetBinary = dialog.getTargetBinary().toStdString();
      ps.workingDirectory = dialog.getProjectRoot().toStdString();
      gridlock::core::ConfigManager::instance().saveProjectSettings(ps);

      auto dbgSettings =
          gridlock::core::ConfigManager::instance().getDebuggerSettings();
      dbgSettings.mpiExecutable = "mpirun";
      dbgSettings.defaultRanks = dialog.getRanks();
      // Using the generated mpirun string for demonstration
      dbgSettings.mpiArgs =
          dialog.getMpiLaunchCommand()
              .remove("mpirun ")
              .remove("-np " + QString::number(dialog.getRanks()) + " ")
              .remove(dialog.getTargetBinary())
              .trimmed();
      gridlock::core::ConfigManager::instance().saveDebuggerSettings(
          dbgSettings);

      if (m_projectExplorerWidget) {
        m_projectExplorerWidget->setRootPath(projectRoot);
      }

      if (m_coordinator) {
        startDebuggingSession(dialog.getTargetBinary(), dialog.getRanks());
      }
    }
  });

  QAction *openProjAction = fileMenu->addAction("Open Workspace...");
  connect(openProjAction, &QAction::triggered, this, [this]() {
    QString dir = QFileDialog::getExistingDirectory(this, "Open Workspace",
                                                    QDir::currentPath());
    if (!dir.isEmpty()) {
      if (m_differentialGrid)
        m_differentialGrid->clearWatches();
      if (m_editorTabManager)
        m_editorTabManager->clearAllTabs();

      gridlock::core::ConfigManager::instance().setWorkspace(dir);
      m_persistentBreakpoints =
          gridlock::core::ConfigManager::instance().getBreakpoints();

      auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
      for (const auto &w : ps.watchExpressions) {
        if (m_differentialGrid)
          m_differentialGrid->addVariableColumn(QString::fromStdString(w));
      }
    }
  });

  QAction *openAction = fileMenu->addAction("Open Source File");
  connect(openAction, &QAction::triggered, this, &MainWindow::openFile);

  fileMenu->addSeparator();

  QAction *saveSessionAction = fileMenu->addAction("Save Session As...");
  connect(saveSessionAction, &QAction::triggered, this,
          &MainWindow::saveSessionAs);

  QAction *loadSessionAction = fileMenu->addAction("Load Session...");
  connect(loadSessionAction, &QAction::triggered, this,
          &MainWindow::loadSession);

  QMenu *recentMenu = fileMenu->addMenu("Recent Sessions");
  connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu]() {
    recentMenu->clear();
    QStringList mru =
        gridlock::core::managers::SessionManager::instance().getMruSessions();
    for (const QString &session : mru) {
      QAction *act = recentMenu->addAction(session);
      connect(act, &QAction::triggered, this, [this, session]() {
        gridlock::core::managers::SessionManager::instance().loadSession(
            session);
      });
    }
    if (mru.isEmpty()) {
      QAction *empty = recentMenu->addAction("No Recent Sessions");
      empty->setEnabled(false);
    }
  });

  fileMenu->addSeparator();

  QAction *projSetAction = fileMenu->addAction("Project Settings");
  connect(projSetAction, &QAction::triggered, this, [this]() {
    ProjectSettingsDialog dlg(this);
    dlg.exec();
  });

  QAction *slurmAction = fileMenu->addAction("Submit SLURM Job");
  connect(slurmAction, &QAction::triggered, this, [this]() {
    const auto slurm =
        gridlock::core::ConfigManager::instance().getSlurmSettings();
    QString script = slurm.scriptTemplate;
    script.replace("{FILE}", m_currentFile);
    if (m_spackManager) {
      m_spackManager->appendMessage("Submitting SLURM batch job...");
      m_bottomTabs->setCurrentWidget(m_spackManager);
    }
    m_hpcBackend->submitSlurmJob(script);
  });

  fileMenu->addSeparator();
  fileMenu->addAction("Exit", this, &MainWindow::close);

  QMenu *editMenu = menuBar->addMenu("&Edit");
  QAction *prefAction = editMenu->addAction("Preferences");
  connect(prefAction, &QAction::triggered, this, &MainWindow::openPreferences);
  menuBar->addMenu("&View");

  QMenu *toolsMenu = menuBar->addMenu("&Tools");
  QAction *buildAction = toolsMenu->addAction("Build Target");
  connect(buildAction, &QAction::triggered, this, [this]() {
    if (!networkLog())
      return;
    networkLog()->appendText("Building target...\n");
    QProcess *proc = new QProcess(this);
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
      if (networkLog()) networkLog()->appendText(proc->readAllStandardOutput());
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
      if (networkLog()) networkLog()->appendError(proc->readAllStandardError());
    });
    proc->start("ninja", QStringList() << "-C" << "build" << "mpi_mm_bin");
  });
  QAction *runTestsAction = toolsMenu->addAction("Run Tests");
  connect(runTestsAction, &QAction::triggered, this,
          &MainWindow::runTargetRequested);

  QMenu *helpMenu = menuBar->addMenu("&Help");
  QAction *aboutAction = helpMenu->addAction("About GridLock");
  connect(aboutAction, &QAction::triggered, this, [this]() {
    QProcess gitProc;
    gitProc.start("git", QStringList()
                             << "ls-remote" << "--tags" << "--sort=v:refname"
                             << "https://github.com/bnorthern42/GridLock.git");
    gitProc.waitForFinished(3000);
    QString latestTag = "v" + gridlock::core::Version::getString(); // default fallback
    if (gitProc.exitStatus() == QProcess::NormalExit &&
        gitProc.exitCode() == 0) {
      QString output =
          QString::fromUtf8(gitProc.readAllStandardOutput()).trimmed();
      if (!output.isEmpty()) {
        QStringList lines = output.split('\n');
        QString lastLine = lines.last();
        QString tag = lastLine.split('\t').last().replace("refs/tags/", "");
        if (!tag.isEmpty()) {
          latestTag = tag;
        }
      }
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("About GridLock");
    msgBox.setIconPixmap(
        QPixmap(":/icon.png")
            .scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(
        QString("<b>GridLock IDE %1</b><br><br>"
                "A high-performance graphical debugger for MPI "
                "applications.<br><br>"
                "<a href=\"https://github.com/bnorthern42/GridLock\">GridLock "
                "on GitHub</a><br><br>"
                "<b>Acknowledgments & Attributions:</b><ul>"
                "<li><a href=\"https://www.gnu.org/software/ddd/\">GNU "
                "DDD</a>: For pioneering visual data display in debugging.</li>"
                "<li><a href=\"https://www.kdbg.org/\">KDbg</a>: For early "
                "inspiration on KDE/Qt-based GDB frontend wrappers.</li>"
                "<li><a href=\"https://www.gdbgui.com/\">gdbgui</a>: For "
                "demonstrating the power of browser-based debug visualization, "
                "which inspired our native DifferentialGrid.</li>"
                "<li><a href=\"https://zealdocs.org/\">Zeal</a>: For the "
                "open-source .docset standard and offline documentation "
                "workflows that power our Reference Manual.</li>"
                "</ul><br>"
                "Special thanks to the <b>Spack</b>, <b>SLURM</b>, and "
                "<b>OpenMPI</b> communities for the tools that power our HPC "
                "environments.")
            .arg(latestTag));
    msgBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
    msgBox.exec();
  });
}

void MainWindow::setupToolbar() {
  QToolBar *toolbar = addToolBar("Main Toolbar");
  toolbar->setObjectName("MainToolbar");

  m_runAction = new QAction("▶ Run Target", this);
  connect(m_runAction, &QAction::triggered, this, [this]() {
    auto settings = gridlock::core::ConfigManager::instance().loadProjectSettings();
    QString binary = m_coordinator && !m_coordinator->getCurrentBinaryPath().isEmpty() ? 
                     m_coordinator->getCurrentBinaryPath() : 
                     QString::fromStdString(settings.targetBinary);
    auto cmd = std::make_unique<gridlock::core::commands::LaunchCommand>(
        this, binary,
        gridlock::core::ConfigManager::instance().getDefaultRanks());
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(m_runAction);

  QAction *continueAction = new QAction("⏩ Continue", this);
  connect(continueAction, &QAction::triggered, this, [this]() {
    auto cmd = std::make_unique<gridlock::core::commands::ContinueCommand>(
        m_coordinator, m_focusedRank + 1);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(continueAction);

  QAction *stepAction = new QAction("↷ Step Inst", this);
  connect(stepAction, &QAction::triggered, this, [this]() {
    auto cmd = std::make_unique<gridlock::core::commands::StepCommand>(
        m_coordinator, m_focusedRank + 1, false);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(stepAction);

  QAction *pauseAction = new QAction("Pause Rank", this);
  connect(pauseAction, &QAction::triggered, this, [this]() {
    auto cmd = std::make_unique<gridlock::core::commands::PauseCommand>(
        m_coordinator, m_focusedRank + 1);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(pauseAction);

  QAction *stopAction = new QAction("⏹ Stop", this);
  connect(stopAction, &QAction::triggered, this, [this]() {
    if (m_coordinator) {
      m_coordinator->terminateSession();
    }
  });
  toolbar->addAction(stopAction);

  QAction *exitAction = new QAction("Terminate Session", this);
  connect(exitAction, &QAction::triggered, this, [this]() {
    auto cmd =
        std::make_unique<gridlock::core::commands::TerminateCommand>(this);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(exitAction);

  for (QAction *action : toolbar->actions()) {
      action->setToolTip(QString());
  }

  QWidget *spacer = new QWidget(toolbar);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  toolbar->addWidget(spacer);

  m_seekBar = new QLineEdit(toolbar);
  m_seekBar->setObjectName("cutterSeekBar");
  m_seekBar->setPlaceholderText("Go to address / symbol (Ctrl+G)...");
  m_seekBar->setMinimumWidth(300);
  m_seekBar->setFixedWidth(320);
  m_seekBar->setMaximumWidth(320);
  QFont seekFont("JetBrains Mono");
  seekFont.setStyleHint(QFont::Monospace);
  seekFont.setPointSize(9);
  m_seekBar->setFont(seekFont);
  toolbar->addWidget(m_seekBar);

  connect(m_seekBar, &QLineEdit::returnPressed, this, [this]() {
    seekToAddressOrExpression(m_seekBar->text());
  });

  QShortcut *seekShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), this);
  connect(seekShortcut, &QShortcut::activated, this, [this]() {
    if (m_seekBar) {
      m_seekBar->setFocus(Qt::ShortcutFocusReason);
      m_seekBar->selectAll();
    }
  });
}

void MainWindow::setupDocks() {
  // Left Zone: Project Explorer / Symbols & Functions list
  m_projectExplorerWidget = new ProjectExplorerWidget(this);
  m_projectExplorerWidget->setObjectName("ProjectExplorerDock");
  addDockWidget(Qt::LeftDockWidgetArea, m_projectExplorerWidget);

  connect(&gridlock::core::managers::SessionManager::instance(),
          &gridlock::core::managers::SessionManager::sessionLoaded,
          m_projectExplorerWidget, &ProjectExplorerWidget::setRootPath);

  connect(m_projectExplorerWidget, &ProjectExplorerWidget::fileDoubleClicked,
          this, [this](const QString &filePath) {
            if (m_editorTabManager) {
              loadSourceFile(filePath);
            }
          });

  // Center & Bottom Zone Splitter
  QSplitter *mainVerticalSplitter = new QSplitter(Qt::Vertical, this);
  mainVerticalSplitter->setObjectName("MainVerticalSplitter");
  mainVerticalSplitter->setContentsMargins(0, 0, 0, 0);
  mainVerticalSplitter->setHandleWidth(1);
  mainVerticalSplitter->setChildrenCollapsible(false);

  // Center Zone: Source Code (Left) & Disassembly View (Right) side-by-side
  QSplitter *masterHorizontalSplitter =
      new QSplitter(Qt::Horizontal, mainVerticalSplitter);
  masterHorizontalSplitter->setObjectName("MasterHorizontalSplitter");
  masterHorizontalSplitter->setContentsMargins(0, 0, 0, 0);
  masterHorizontalSplitter->setHandleWidth(1);
  masterHorizontalSplitter->setChildrenCollapsible(false);

  m_editorTabManager = new EditorTabManager(masterHorizontalSplitter);
  m_editorTabManager->setMinimumWidth(350);
  m_editorTabManager->setDocumentMode(true);
  m_editorTabManager->setContentsMargins(0, 0, 0, 0);
  if (m_editorTabManager->tabBar()) {
    m_editorTabManager->tabBar()->setUsesScrollButtons(true);
    m_editorTabManager->tabBar()->setElideMode(Qt::ElideNone);
  }
  connect(m_editorTabManager, &EditorTabManager::toggleBreakpointRequested,
          this, [this](const QString &loc) {
            if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(
                    m_coordinator)) {
              gdbCoord->insertBreakpoint(loc);
            }
          });
  connect(m_editorTabManager, &EditorTabManager::breakpointToggled, this,
          [this](const QString &file, int line, bool isSet,
                 const QString &condition) {
            QString absoluteFilePath = QFileInfo(file).absoluteFilePath();

            // Update persistent cache (only tracks line numbers currently)
            if (!isSet) {
              m_persistentBreakpoints[absoluteFilePath].remove(line);
            } else {
              m_persistentBreakpoints[absoluteFilePath].insert(line);
            }
            gridlock::core::ConfigManager::instance().saveBreakpoints(
                m_persistentBreakpoints);

            if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(
                    m_coordinator)) {
              gdbCoord->broadcastBreakpoint(absoluteFilePath, line, isSet,
                                            condition);
            } else if (auto *dapCoord =
                           dynamic_cast<DapCoordinator *>(m_coordinator)) {
              if (isSet) {
                dapCoord->toggleBreakpoint(absoluteFilePath, line);
              } else {
                dapCoord->toggleBreakpoint(absoluteFilePath, line);
              }
            }
          });

  connect(
      m_editorTabManager, &EditorTabManager::hoverVariableRequested, this,
      [this](const QString &varName, const QPoint &globalPos) {
        if (auto *gdbCoord =
                dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
          gdbCoord->evaluateHoverVariable(m_focusedRank, varName, globalPos);
        }
      });

  connect(m_editorTabManager, &EditorTabManager::hideHoverTooltip, this,
          [this]() {
            if (m_plainJaneTooltip) {
              m_plainJaneTooltip->hideTooltip();
            }
          });

  // Right Inspection Zone: Variables, Registers, Watch Expressions, and Server Rack
  m_variablesDockWidget = new VariablesDockWidget(this);
  m_variablesDockWidget->setObjectName("VariablesWidget");
  QDockWidget *variablesDock = new QDockWidget("Variables", this);
  variablesDock->setObjectName("VariablesDock");
  variablesDock->setWidget(m_variablesDockWidget);
  QWidget *emptyVarTitle = new QWidget(variablesDock);
  emptyVarTitle->setFixedHeight(0);
  variablesDock->setTitleBarWidget(emptyVarTitle);

  m_registerView = new RegisterView(this);
  m_registerView->setObjectName("RegisterViewWidget");
  connect(m_registerView, &RegisterView::addressActivated, this, [this](uint64_t addr) {
    seekToAddressOrExpression(QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')));
  });
  QDockWidget *registersDock = new QDockWidget("Registers", this);
  registersDock->setObjectName("RegisterViewDock");
  registersDock->setWidget(m_registerView);
  QWidget *emptyRegTitle = new QWidget(registersDock);
  emptyRegTitle->setFixedHeight(0);
  registersDock->setTitleBarWidget(emptyRegTitle);

  m_differentialGrid = new DifferentialGrid(this);
  m_differentialGrid->setObjectName("DifferentialGrid");
  connect(m_differentialGrid, &DifferentialGrid::watchVariableAdded, this,
          [this](const QString &name) {
            if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(
                    m_coordinator)) {
              gdbCoord->registerWatchVariable(name);
            }
          });

  auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
  for (const auto &w : ps.watchExpressions) {
    if (!w.empty()) {
      m_differentialGrid->addVariableColumn(QString::fromStdString(w));
    }
  }
  QDockWidget *watchDock = new QDockWidget("Watch Expressions", this);
  watchDock->setObjectName("WatchExpressionsDock");
  watchDock->setWidget(m_differentialGrid);

  m_serverRackView = new ServerRackView(this);
  m_serverRackView->setObjectName("ServerRackViewWidget");
  connect(m_serverRackView, &ServerRackView::rankSelected, this,
          &MainWindow::onRankSelected);
  QDockWidget *serverRackDock = new QDockWidget("Server Rack", this);
  serverRackDock->setObjectName("ServerRackDock");
  serverRackDock->setWidget(m_serverRackView);

  addDockWidget(Qt::RightDockWidgetArea, variablesDock);
  addDockWidget(Qt::RightDockWidgetArea, registersDock);
  tabifyDockWidget(variablesDock, registersDock);
  addDockWidget(Qt::RightDockWidgetArea, watchDock);
  addDockWidget(Qt::RightDockWidgetArea, serverRackDock);
  registersDock->raise();

  connect(m_editorTabManager, &EditorTabManager::pinVariableRequested, this,
          [this, watchDock](const QString &varName) {
            if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(
                    m_coordinator)) {
              gdbCoord->registerWatchVariable(varName);
            }
            if (m_differentialGrid) {
              m_differentialGrid->addVariableColumn(varName);
            }
            if (watchDock) {
              watchDock->show();
              watchDock->raise();
            }
          });

  m_disassemblyView = new DisassemblyView(masterHorizontalSplitter);
  m_disassemblyView->setContentsMargins(0, 0, 0, 0);

  masterHorizontalSplitter->addWidget(m_editorTabManager);
  masterHorizontalSplitter->addWidget(m_disassemblyView);

  masterHorizontalSplitter->setStretchFactor(0, 60);
  masterHorizontalSplitter->setStretchFactor(1, 40);
  masterHorizontalSplitter->setSizes({520, 360});

  mainVerticalSplitter->addWidget(masterHorizontalSplitter);

  // Bottom Zone: Reduced to 7 core I/O & memory inspection tabs
  m_bottomTabs = new QTabWidget(mainVerticalSplitter);
  m_bottomTabs->setObjectName("BottomConsoleTabs");
  m_bottomTabs->setDocumentMode(true);
  m_bottomTabs->setContentsMargins(0, 0, 0, 0);
  if (m_bottomTabs->tabBar()) {
    m_bottomTabs->tabBar()->setUsesScrollButtons(true);
    m_bottomTabs->tabBar()->setElideMode(Qt::ElideNone);
  }

  m_terminalDock = new TerminalDockWidget("Terminal", m_bottomTabs);
  m_terminalDock->setObjectName("TerminalDock");

  m_expressionEvaluatorWidget = new ExpressionEvaluatorWidget(m_bottomTabs);
  m_expressionEvaluatorWidget->setObjectName("ExpressionEvaluatorDock");
  QWidget *emptyEvalTitle = new QWidget(m_expressionEvaluatorWidget);
  emptyEvalTitle->setFixedHeight(0);
  m_expressionEvaluatorWidget->setTitleBarWidget(emptyEvalTitle);
  connect(m_expressionEvaluatorWidget,
          &ExpressionEvaluatorWidget::evaluateRequested, this,
          [this](const QString &expr) {
            if (m_coordinator) {
              m_coordinator->evaluateExpression(m_focusedRank, expr);
            }
          });

  m_referenceManualWidget = new ReferenceManualWidget(m_bottomTabs);
  m_referenceManualWidget->setObjectName("ReferenceManualWidget");
  
  m_gdbConsoleWidget = new GdbConsoleWidget(m_bottomTabs);
  m_gdbConsoleWidget->setObjectName("GdbConsoleWidget");
  
  m_memView = new MemView(m_bottomTabs);
  m_memView->setObjectName("MemViewDock");
  
  m_spackManager = new SpackManager(m_hpcBackend, m_bottomTabs);
  m_spackManager->setObjectName("HpcConsoleDock");

  connect(m_memView, &MemView::requestMemory, this,
          [this](const QString &address, int length) {
            if (m_coordinator) {
              m_coordinator->readMemory(m_focusedRank, address, length);
            }
          });

  connect(
      m_memView, &MemView::requestMemoryDiff, this,
      [this](int baseRank, int targetRank, const QString &addrStr, int length) {
        if (!m_coordinator)
          return;
        pid_t basePid = m_coordinator->getPidForRank(baseRank);
        pid_t targetPid = m_coordinator->getPidForRank(targetRank);

        bool ok;
        quint64 addrInt = addrStr.toULongLong(&ok, 16);
        if (!ok)
          return;
        void *remoteAddr = reinterpret_cast<void *>(addrInt);

        QPointer<MainWindow> weakThis(this);
        (void)QtConcurrent::run([weakThis, basePid, targetPid, remoteAddr,
                                 length]() {
          auto result = gridlock::core::hpc::MemoryDiffer::compareMemory(
              basePid, targetPid, remoteAddr, length);
          QMetaObject::invokeMethod(weakThis, [weakThis, result, remoteAddr]() {
            if (weakThis && weakThis->m_memView) {
              weakThis->m_memView->displayMemoryDiff(result, remoteAddr);
            }
          });
        });
      });

  m_mpiDiagnosticsWidget = new MpiDiagnosticsWidget(m_bottomTabs);
  connect(m_mpiDiagnosticsWidget, &MpiDiagnosticsWidget::jumpToFrameRequested,
          this, &MainWindow::onRankSelected);

  m_bottomTabs->addTab(m_terminalDock, "Terminal");
  m_bottomTabs->addTab(m_gdbConsoleWidget, "GDB Console");
  m_bottomTabs->addTab(m_memView, "Memory View");
  m_bottomTabs->addTab(m_mpiDiagnosticsWidget, "MPI Diagnostics");
  m_bottomTabs->addTab(m_spackManager, "HPC Console");
  m_bottomTabs->addTab(m_expressionEvaluatorWidget, "Evaluator");
  m_bottomTabs->addTab(m_referenceManualWidget, "Reference Manual");
  m_bottomTabs->setCurrentWidget(m_memView);

  mainVerticalSplitter->addWidget(m_bottomTabs);
  m_bottomTabs->setMinimumHeight(220);

  mainVerticalSplitter->setStretchFactor(0, 68);
  mainVerticalSplitter->setStretchFactor(1, 32);
  mainVerticalSplitter->setSizes({600, 260});

  setCentralWidget(mainVerticalSplitter);
  variablesDock->setMinimumWidth(240);
  registersDock->setMinimumWidth(240);
  watchDock->setMinimumWidth(240);
  serverRackDock->setMinimumWidth(240);
  resizeDocks({m_projectExplorerWidget, variablesDock, watchDock, serverRackDock}, {200, 260, 260, 260}, Qt::Horizontal);
  resizeDocks({variablesDock, watchDock, serverRackDock}, {340, 260, 240}, Qt::Vertical);

  QMenu* viewMenu = nullptr;
  for (QAction* action : menuBar()->actions()) {
      if (action->menu() && action->text() == "&View") {
          viewMenu = action->menu();
          break;
      }
  }

  for (auto* dock : findChildren<QDockWidget*>()) {
      if (viewMenu) {
          viewMenu->addAction(dock->toggleViewAction());
      }
      dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
      dock->setContentsMargins(0, 0, 0, 0);
      if (QWidget* inner = dock->widget()) {
          inner->setContentsMargins(0, 0, 0, 0);
          if (inner->layout()) {
              inner->layout()->setContentsMargins(0, 0, 0, 0);
              inner->layout()->setSpacing(0);
          }
      }
      
      connect(dock, &QDockWidget::topLevelChanged, dock, [dock](bool topLevel) {
          if (topLevel) {
              dock->setWindowFlags(dock->windowFlags() & ~Qt::FramelessWindowHint);
              dock->show();
          }
      });
  }
}

void MainWindow::onRankStateChanged(int rankId, const RankState &state) {
  m_latestStates[rankId] = state;
  m_serverRackView->updateRankState(rankId, state);

  if (rankId == m_focusedRank) {
    if (!state.disassemblyText.isEmpty()) {
      m_disassemblyView->updateDisassembly(state.disassemblyText);
    }
    const QString pcAddr = state.registers.value(16, m_currentSeekOrPc);
    updateStatusTelemetry(pcAddr, state.currentState.isEmpty() ? m_currentTargetState : state.currentState.toUpper());
    if (state.currentState == "stopped") {
      if (state.currentLine > 0) {
        if (!state.currentFile.isEmpty() && m_editorTabManager) {
          if (auto view = m_editorTabManager->openFile(state.currentFile)) {
            view->highlightCurrentLine(state.currentLine);
          }
        } else if (getSourceCodeView()) {
          getSourceCodeView()->highlightCurrentLine(state.currentLine);
        }
      }
    }
  }

  m_differentialGrid->setVariableData(rankId, state.variableWatches);
  if (rankId == m_focusedRank && m_registerView) {
    m_registerView->updateRegisters(state);
  }
}

void MainWindow::onRankSelected(int rankId) {
  m_focusedRank = rankId;
  if (m_latestStates.count(rankId)) {
    const auto &state = m_latestStates[rankId];
    const QString pcAddr = state.registers.value(16, m_currentSeekOrPc);
    updateStatusTelemetry(pcAddr, state.currentState.isEmpty() ? m_currentTargetState : state.currentState.toUpper());
    m_disassemblyView->updateDisassembly(state.disassemblyText);

    if (state.disassemblyText.isEmpty()) {
      if (auto *gdbCoord =
              dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
        gdbCoord->requestDisassemblyFallback(rankId);
      }
    }

    if (state.currentState == "stopped") {
      if (state.currentLine > 0) {
        if (!state.currentFile.isEmpty() && m_editorTabManager) {
          if (auto view = m_editorTabManager->openFile(state.currentFile)) {
            view->highlightCurrentLine(state.currentLine);
          }
        } else if (getSourceCodeView()) {
          getSourceCodeView()->highlightCurrentLine(state.currentLine);
        }
      }
    }

    // Force update DifferentialGrid table matrix rows
    m_differentialGrid->setVariableData(rankId, state.variableWatches);
    if (m_registerView) {
      m_registerView->updateRegisters(state);
    }
  } else {
    updateStatusTelemetry();
    if (auto *gdbCoord =
            dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
      gdbCoord->requestDisassemblyFallback(rankId);
    }
  }
}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (m_coordinator) {
    m_coordinator->terminateSession();
  }
  if (m_lspCoordinator) {
    m_lspCoordinator->stop();
  }

  if (m_differentialGrid) {
    auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
    ps.watchExpressions = m_differentialGrid->getWatchExpressions();
    gridlock::core::ConfigManager::instance().saveProjectSettings(ps);
  }

  QSettings settings("gridlock", "debugger");
  settings.setValue("MainWindow/Geometry_v6", saveGeometry());
  settings.setValue("MainWindow/State_v6", saveState());

  for (auto* splitter : findChildren<QSplitter*>()) {
      if (!splitter->objectName().isEmpty()) {
          settings.setValue("MainWindow/Splitter_v6_" + splitter->objectName(), splitter->saveState());
      }
  }

  event->accept();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::KeyPress) {
    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
    if (gridlock::core::ShortcutManager::instance().handleKeyPress(watched,
                                                                   keyEvent)) {
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::openFile() {
  auto &cfg = gridlock::core::ConfigManager::instance();
  const QString startDir = cfg.getLastOpenDir();

  QString fileName =
      QFileDialog::getOpenFileName(this, tr("Open Source File"), startDir,
                                   tr("C/C++ Files (*.cpp *.hpp *.h *.c)"),
                                   nullptr, QFileDialog::DontUseNativeDialog);

  if (!fileName.isEmpty()) {
    // Persist the directory so the next open picks up where we left off.
    cfg.setLastOpenDir(QFileInfo(fileName).absolutePath());
    loadSourceFile(fileName);
  }
}

void MainWindow::loadSourceFile(const QString &filePath) {
  if (m_editorTabManager) {
    if (auto view = m_editorTabManager->openFile(filePath)) {
      m_currentFile = filePath; // Update active file tracked by MainWindow
      QString absolutePath = QFileInfo(m_currentFile).absoluteFilePath();
      if (m_lspCoordinator) {
        m_lspCoordinator->stop();
        m_lspCoordinator->start(absolutePath);
        m_lspCoordinator->didOpen(m_currentFile, view->getPlainText());
      }
      if (networkLog()) {
        networkLog()->appendText("Successfully loaded: " + m_currentFile +
                                   "\n");
      }
    } else {
      if (networkLog()) {
        networkLog()->appendError("CRITICAL: Failed to load " + filePath +
                                    "\n");
      }
    }
  }
}

void MainWindow::openPreferences() {
  // Removed the 'dialogs::' qualifier
  auto *dlg = new PreferencesDialog(this);
  dlg->setAttribute(Qt::WA_DeleteOnClose);

  connect(dlg, &PreferencesDialog::preferencesChanged, this, [this]() {
    if (m_projectExplorerWidget) {
        m_projectExplorerWidget->reloadStyle();
    }

    // Apply UI theme and font size globally without restart
    gridlock::core::managers::ThemeManager::instance().applyThemeAndFonts();

    // Update Terminal font
    if (m_terminalDock) {
      const auto ts = gridlock::core::ConfigManager::instance().getTerminalSettings();
      m_terminalDock->setTerminalFont(ts.fontFamily, ts.fontSize);
    }

    // SourceCodeView and RegisterView may want to re-read palette / font prefs.
    if (m_editorTabManager) {
      for (int i = 0; i < m_editorTabManager->count(); ++i) {
        if (auto view =
                qobject_cast<SourceCodeView *>(m_editorTabManager->widget(i))) {
          view->reloadStyle();
        }
      }
    }
    if (m_registerView)
      m_registerView->update();
    if (m_disassemblyView)
      m_disassemblyView->reloadStyle();
  });

  connect(dlg, &QDialog::accepted, this, [this]() {
      // Unused, as preferencesChanged handles all dynamic styling
  });

  dlg->exec();
}

void MainWindow::startDebuggingSession(const QString &binaryPath, int ranks) {
  if (!m_coordinator)
    return;

  if (m_currentFile.isEmpty() ||
      (getSourceCodeView() &&
       getSourceCodeView()->toPlainText().trimmed().isEmpty())) {
    // Graceful fallback removed
  }

  // QProcess instances natively. Notice we REMOVED the QTimer and "-exec-run"
  // command here. The Coordinator handles it asynchronously.
  m_coordinator->launchParallelSession(binaryPath, ranks);

  if (m_gdbConsoleWidget) {
    m_gdbConsoleWidget->resetRanks(ranks);
  }
  if (m_serverRackView) {
    m_serverRackView->resetRanks(ranks);
  }

  if (m_differentialGrid) {
    auto watches = m_differentialGrid->getWatchExpressions();
    if (auto *gdbCoord =
            dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
      for (const auto &w : watches) {
        gdbCoord->registerWatchVariable(QString::fromStdString(w));
      }
    }
  }
}

void MainWindow::setVulkanInstance([[maybe_unused]] QVulkanInstance *inst) {}

void MainWindow::saveSessionAs() {
  QString filePath = QFileDialog::getSaveFileName(
      this, "Save Session", QDir::currentPath(), "TOML Files (*.toml)");
  if (filePath.isEmpty())
    return;

  gridlock::core::managers::SessionState state;

  if (m_differentialGrid) {
    for (const auto &w : m_differentialGrid->getWatchExpressions()) {
      state.watchedVariables.push_back(QString::fromStdString(w));
    }
  }

  for (auto it = m_persistentBreakpoints.constBegin();
       it != m_persistentBreakpoints.constEnd(); ++it) {
    for (int line : *it) {
      state.activeBreakpoints.push_back(
          QString("%1:%2").arg(it.key()).arg(line));
    }
  }

  state.selectedRanks.push_back(m_focusedRank);

  gridlock::core::managers::SessionManager::instance().saveSession(filePath,
                                                                   state);
}

void MainWindow::loadSession() {
  QString filePath = QFileDialog::getOpenFileName(
      this, "Load Session", QDir::currentPath(), "TOML Files (*.toml)");
  if (filePath.isEmpty())
    return;

  auto optState =
      gridlock::core::managers::SessionManager::instance().loadSession(
          filePath);
  if (!optState)
    return;

  const auto &state = *optState;

  if (m_differentialGrid)
    m_differentialGrid->clearWatches();
  for (const auto &w : state.watchedVariables) {
    if (m_differentialGrid)
      m_differentialGrid->addVariableColumn(w);
  }

  m_persistentBreakpoints.clear();
  for (const auto &bp : state.activeBreakpoints) {
    auto parts = bp.split(':');
    if (parts.size() == 2) {
      QString file = parts[0];
      int line = parts[1].toInt();
      m_persistentBreakpoints[file].insert(line);

      if (m_editorTabManager) {
        if (auto *view = m_editorTabManager->getSourceCodeViewForFile(file)) {
          view->setBreakpoints(m_persistentBreakpoints[file]);
        }
      }

      if (auto *gdbCoord =
              dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
        gdbCoord->broadcastBreakpoint(file, line, true, QString());
      } else if (auto *dapCoord =
                     dynamic_cast<DapCoordinator *>(m_coordinator)) {
        // If it's already set this will unset it, but session loading assumes
        // fresh state
        dapCoord->toggleBreakpoint(file, line);
      }
    }
  }

  if (!state.selectedRanks.empty()) {
    m_focusedRank = state.selectedRanks.front();
    onRankSelected(m_focusedRank);
  }
}

void MainWindow::onTutorialLaunchRequested(const QString& absoluteFilePath) {
    loadSourceFile(absoluteFilePath);
    
    QFileInfo fi(absoluteFilePath);
    if (m_projectExplorerWidget) {
        m_projectExplorerWidget->setRootPath(fi.absolutePath());
    }

    QString compiler = fi.suffix() == "c" ? "mpicc" : "mpic++";
    QString outDir = QDir::tempPath();
    QString outBin = outDir + "/" + fi.baseName() + "_demo";

    QProcess proc;
    proc.start(compiler, QStringList() << absoluteFilePath << "-g" << "-O0" << "-o" << outBin);
    proc.waitForFinished();
    
    if (proc.exitCode() == 0) {
        // Load the tutorial breakpoints injected into workspace.toml
        m_persistentBreakpoints = gridlock::core::ConfigManager::instance().getBreakpoints();
        
        // Push breakpoints to editor and coordinator
        if (m_editorTabManager) {
            if (auto* view = m_editorTabManager->getSourceCodeViewForFile(absoluteFilePath)) {
                view->setBreakpoints(m_persistentBreakpoints[absoluteFilePath]);
            }
        }
        
        if (m_coordinator) {
            if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
                for (int line : m_persistentBreakpoints[absoluteFilePath]) {
                    gdbCoord->broadcastBreakpoint(absoluteFilePath, line, true, QString());
                }
            } else if (auto* dapCoord = dynamic_cast<DapCoordinator*>(m_coordinator)) {
                for (int line : m_persistentBreakpoints[absoluteFilePath]) {
                    dapCoord->toggleBreakpoint(absoluteFilePath, line);
                }
            }
        }
        
        int ranks = (fi.baseName() == "deadlock_demo") ? 2 : 3;
        startDebuggingSession(outBin, ranks);
    } else {
        if (networkLog()) {
            networkLog()->appendError("Tutorial compilation failed:\n" + proc.readAllStandardError());
        }
    }
}

bool MainWindow::execTutorialDialog() {
    gridlock::ui::TutorialDialog dialog(this);
    QObject::connect(&dialog, &gridlock::ui::TutorialDialog::launchTutorialRequested,
                     this, &gridlock::ui::MainWindow::onTutorialLaunchRequested);
    if (dialog.exec() == QDialog::Accepted) {
        show();
        return true;
    }
    return false;
}

} // namespace gridlock::ui