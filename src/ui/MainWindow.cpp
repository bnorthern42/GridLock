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
#include "../core/hpc/DeadlockAnalyzer.hpp"
#include "../core/hpc/MemoryDiffer.hpp"
#include "../core/hpc/MockHpcBackend.hpp"
#include "../core/managers/SessionManager.hpp"
#include "../core/managers/ShortcutManager.hpp"
#include "../core/managers/SpackManager.hpp"
#include "dialogs/ProjectSettingsDialog.hpp"
#include "dialogs/ProjectWizard.hpp"
#include "tutorial/TutorialDialog.hpp"
#include "docks/TerminalDock.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "widgets/ExpressionEvaluatorWidget.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
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
#include <algorithm>

namespace gridlock {
class PlainJaneTooltip : public QLabel {
public:
    PlainJaneTooltip(QWidget* parent = nullptr) : QLabel(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus) {
        setStyleSheet("background-color: #1e1e2e; color: #cdd6f4; border: 1px solid #45475a; padding: 4px; border-radius: 4px;");
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

  gridlock::core::ShortcutManager::instance().initialize(this);
  qApp->installEventFilter(this);

  // Eagerly load default source file so UI is populated before running
  m_lspCoordinator = new gridlock::core::LspCoordinator(this);
  m_plainJaneTooltip = new gridlock::PlainJaneTooltip(this);



  // Load offline persistence TOML breakpoints
  m_persistentBreakpoints =
      gridlock::core::ConfigManager::instance().getBreakpoints();
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
                } else {
                  m_runAction->setEnabled(false);
                  if (state == SessionState::Launching)
                    m_runAction->setText("Launching...");
                  else if (state == SessionState::Queued)
                    m_runAction->setText("Queued in SLURM...");
                  else if (state == SessionState::Running) {
                    m_runAction->setText("Running");
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
                  if (m_terminalDock) {
                    m_terminalDock->appendError(
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

    if (m_terminalDock) {
      connect(m_coordinator, &IBackendCoordinator::targetOutputReceived,
              m_terminalDock,
              [this](const QString &category, const QString &output) {
                m_terminalDock->appendText(category, output);
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
  setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AnimatedDocks);
  setDockNestingEnabled(true);
  setupToolbar();
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
    if (!m_terminalDock)
      return;
    m_terminalDock->appendText("Building target...\n");
    QProcess *proc = new QProcess(this);
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
      m_terminalDock->appendText(proc->readAllStandardOutput());
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
      m_terminalDock->appendError(proc->readAllStandardError());
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
    QString latestTag = "v0.5.3"; // default fallback
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
}

void MainWindow::setupDocks() {
  QSplitter *mainVerticalSplitter = new QSplitter(Qt::Vertical, this);
  mainVerticalSplitter->setContentsMargins(0, 0, 0, 0);

  m_projectExplorerWidget = new ProjectExplorerWidget(this);
  addDockWidget(Qt::LeftDockWidgetArea, m_projectExplorerWidget);

  connect(&gridlock::core::managers::SessionManager::instance(),
          &gridlock::core::managers::SessionManager::sessionLoaded,
          m_projectExplorerWidget, &ProjectExplorerWidget::setRootPath);

  m_variablesDockWidget = new VariablesDockWidget(this);
  // It's no longer a dock widget, it will be added to the splitter

  connect(m_projectExplorerWidget, &ProjectExplorerWidget::fileDoubleClicked,
          this, [this](const QString &filePath) {
            if (m_editorTabManager) {
              loadSourceFile(
                  filePath); // Actually loadSourceFile does everything needed,
                             // or we can just call
                             // m_editorTabManager->openFile(filePath) and
                             // m_lspCoordinator
            }
          });

  QSplitter *masterHorizontalSplitter =
      new QSplitter(Qt::Horizontal, mainVerticalSplitter);

  m_editorTabManager = new EditorTabManager(masterHorizontalSplitter);
  m_editorTabManager->setMinimumWidth(350);
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
                // DapCoordinator doesn't track conditions currently, so just
                // toggle Note: to strictly toggle based on isSet, we would need
                // dapCoord->setBreakpoint(..., isSet), but toggleBreakpoint
                // toggles it. Since we are just toggling it, it's fine.
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



  connect(m_editorTabManager, &EditorTabManager::pinVariableRequested, this,
          [this](const QString &varName) {
            if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(
                    m_coordinator)) {
              gdbCoord->registerWatchVariable(varName);
            }
            if (m_differentialGrid) {
              m_differentialGrid->addVariableColumn(varName);
            }
            if (m_bottomTabs) {
              m_bottomTabs->setCurrentWidget(m_differentialGrid);
            }
          });

  QSplitter *rightPaneSplitter =
      new QSplitter(Qt::Vertical, masterHorizontalSplitter);

  m_disassemblyView = new DisassemblyView(rightPaneSplitter);
  m_serverRackView = new ServerRackView(rightPaneSplitter);
  connect(m_serverRackView, &ServerRackView::rankSelected, this,
          &MainWindow::onRankSelected);

  // m_variablesDockWidget already constructed above
  rightPaneSplitter->addWidget(m_disassemblyView);
  rightPaneSplitter->addWidget(m_serverRackView);
  rightPaneSplitter->addWidget(m_variablesDockWidget);
  rightPaneSplitter->setSizes({400, 200, 400});

  masterHorizontalSplitter->addWidget(m_editorTabManager);
  masterHorizontalSplitter->addWidget(rightPaneSplitter);

  masterHorizontalSplitter->setStretchFactor(0, 60);
  masterHorizontalSplitter->setStretchFactor(1, 40);

  mainVerticalSplitter->addWidget(masterHorizontalSplitter);

  m_bottomTabs = new QTabWidget(mainVerticalSplitter);

  m_terminalDock = new TerminalDock("Compiler Terminal", m_bottomTabs);
  m_differentialGrid = new DifferentialGrid(m_bottomTabs);
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

  m_expressionEvaluatorWidget = new ExpressionEvaluatorWidget(m_bottomTabs);
  connect(m_expressionEvaluatorWidget,
          &ExpressionEvaluatorWidget::evaluateRequested, this,
          [this](const QString &expr) {
            if (m_coordinator) {
              m_coordinator->evaluateExpression(m_focusedRank, expr);
            }
          });

  m_referenceManualWidget = new ReferenceManualWidget(m_bottomTabs);
  m_gdbConsoleWidget = new GdbConsoleWidget(m_bottomTabs);
  m_memView = new MemView(m_bottomTabs);
  m_registerView = new RegisterView(m_bottomTabs);
  m_spackManager = new SpackManager(m_hpcBackend, m_bottomTabs);

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

  m_bottomTabs->addTab(m_terminalDock, "Compiler Terminal");
  m_bottomTabs->addTab(m_differentialGrid, "Watch Expressions");
  m_bottomTabs->addTab(m_expressionEvaluatorWidget, "Evaluator");
  m_bottomTabs->addTab(m_referenceManualWidget, "Reference Manual");
  m_bottomTabs->addTab(m_gdbConsoleWidget, "GDB Console");
  m_bottomTabs->addTab(m_memView, "Memory View");
  m_mpiDiagnosticsWidget = new MpiDiagnosticsWidget(m_bottomTabs);
  connect(m_mpiDiagnosticsWidget, &MpiDiagnosticsWidget::jumpToFrameRequested,
          this, &MainWindow::onRankSelected);

  m_bottomTabs->addTab(m_registerView, "Registers");
  m_bottomTabs->addTab(m_spackManager, "HPC Console");
  m_bottomTabs->addTab(m_mpiDiagnosticsWidget, "MPI Diagnostics");

  mainVerticalSplitter->addWidget(m_bottomTabs);
  m_bottomTabs->setMinimumHeight(250);

  mainVerticalSplitter->setStretchFactor(0, 70);
  mainVerticalSplitter->setStretchFactor(1, 30);
  mainVerticalSplitter->setSizes({700, 300});

  setCentralWidget(mainVerticalSplitter);
}

void MainWindow::onRankStateChanged(int rankId, const RankState &state) {
  m_latestStates[rankId] = state;
  m_serverRackView->updateRankState(rankId, state);

  if (rankId == m_focusedRank) {
    if (!state.disassemblyText.isEmpty()) {
      m_disassemblyView->updateDisassembly(state.disassemblyText);
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
      if (m_terminalDock) {
        m_terminalDock->appendText("Successfully loaded: " + m_currentFile +
                                   "\n");
      }
    } else {
      if (m_terminalDock) {
        m_terminalDock->appendError("CRITICAL: Failed to load " + filePath +
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
  });

  connect(dlg, &QDialog::accepted, this, [this]() {
    // SourceCodeView and RegisterView may want to re-read palette / font prefs.
    if (m_editorTabManager) {
      for (int i = 0; i < m_editorTabManager->count(); ++i) {
        if (auto view =
                qobject_cast<SourceCodeView *>(m_editorTabManager->widget(i))) {
          view->update();
        }
      }
    }
    if (m_registerView)
      m_registerView->update();
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
    for (int line : it.value()) {
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
        if (m_terminalDock) {
            m_terminalDock->appendError("Tutorial compilation failed:\n" + proc.readAllStandardError());
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