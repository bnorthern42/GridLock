#include "MainWindow.hpp"
#include "../core/hpc/GdbRankCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/hpc/LspCoordinator.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "views/DisassemblyView.hpp"
#include "GdbConsoleWidget.hpp"
#include "GdbConsoleWidget.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
#include "widgets/ReferenceManualWidget.hpp"
#include "views/ServerRackView.hpp"
#include "dialogs/PreferencesDialog.hpp"
#include "views/SourceCodeView.hpp"
#include "EditorTabManager.hpp"
#include "widgets/ProjectExplorerWidget.hpp"
#include "dialogs/ProjectSettingsDialog.hpp"
#include "docks/TerminalDock.hpp"
#include "widgets/ExpressionEvaluatorWidget.hpp"
#include "../core/hpc/MockHpcBackend.hpp"
#include "../core/commands/DebugCommands.hpp"
#include "../core/managers/ShortcutManager.hpp"
#include "../core/managers/SpackManager.hpp"
#include "docks/VariablesDockWidget.hpp"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QDialog>
#include <QDialog>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QProcess>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>

namespace gridlock::ui {

SourceCodeView* MainWindow::sourceCodeView() const {
  return m_editorTabManager ? m_editorTabManager->currentSourceCodeView() : nullptr;
}

SourceCodeView* MainWindow::getSourceCodeView() const {
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
  
  connect(m_lspCoordinator, &gridlock::core::LspCoordinator::hoverResultReceived, this, [this](const QString &resultMarkdown, const QPoint &globalPos) {
      if (getSourceCodeView()) {
          QToolTip::showText(globalPos, resultMarkdown, getSourceCodeView());
      }
  });

  loadSourceFile("tests/mpi_mm.c");

  // Load offline persistence TOML breakpoints
  m_persistentBreakpoints =
      gridlock::core::ConfigManager::instance().getBreakpoints();
  QString absolutePath = QFileInfo("tests/mpi_mm.c").absoluteFilePath();
  if (m_persistentBreakpoints.contains(absolutePath) && getSourceCodeView()) {
    getSourceCodeView()->setBreakpoints(m_persistentBreakpoints[absolutePath]);
  }
}

void MainWindow::executeCommand(std::unique_ptr<gridlock::core::commands::IDebugCommand> cmd) {
  if (cmd) {
    cmd->execute();
  }
}

void MainWindow::setCoordinator(gridlock::GdbRankCoordinator *coord) {
  m_coordinator = coord;
  if (m_variablesDockWidget) {
      m_variablesDockWidget->setCoordinator(m_coordinator);
  }
  if (m_coordinator) {
    connect(m_coordinator, &GdbRankCoordinator::hoverEvaluationComplete, this, [this](QString varName, QString result, QPoint globalPos) {
        if (getSourceCodeView()) {
            QToolTip::showText(globalPos, QString("<b>%1</b>: %2").arg(varName).arg(result), getSourceCodeView());
        }
    });
    if (m_expressionEvaluatorWidget) {
        connect(m_coordinator, &GdbRankCoordinator::expressionEvaluated, m_expressionEvaluatorWidget, &ExpressionEvaluatorWidget::appendResult);
    }
  }
  if (m_coordinator && m_gdbConsoleWidget) {
    connect(m_coordinator, &GdbRankCoordinator::gdbOutputReceived,
            m_gdbConsoleWidget, &GdbConsoleWidget::appendGdbOutput);
    connect(m_coordinator, &GdbRankCoordinator::commandSentToGdb,
            m_gdbConsoleWidget, &GdbConsoleWidget::appendGdbInput);
    connect(m_gdbConsoleWidget, &GdbConsoleWidget::commandEntered,
            m_coordinator, &GdbRankCoordinator::sendCommand);
    if (m_terminalDock) {
      connect(m_coordinator, &GdbRankCoordinator::targetOutputReceived,
              m_terminalDock, &TerminalDock::appendText);
    }
    connect(m_coordinator, &GdbRankCoordinator::memoryDataReady, this, [this](int rankId, qint64 beginAddress, const QString &hexContents) {
      if (rankId == m_focusedRank && m_memView) {
        m_memView->setMemoryData(beginAddress, hexContents);
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
  QAction *newAction = fileMenu->addAction("New Session");
  connect(newAction, &QAction::triggered, this, [this]() {
    QDialog dialog(this);
    dialog.setWindowTitle("New Session");
    QFormLayout *form = new QFormLayout(&dialog);
    QLineEdit *binaryEdit = new QLineEdit(&dialog);
    auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
    binaryEdit->setText(ps.targetBinary.empty() ? "build/mpi_mm_bin" : QString::fromStdString(ps.targetBinary));
    QSpinBox *rankBox = new QSpinBox(&dialog);
    rankBox->setValue(
        gridlock::core::ConfigManager::instance().getDefaultRanks());
    rankBox->setMinimum(1);
    form->addRow("Target Binary:", binaryEdit);
    form->addRow("Rank Count:", rankBox);
    QDialogButtonBox *box =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             Qt::Horizontal, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted && m_coordinator) {
      startDebuggingSession(binaryEdit->text(), rankBox->value());
    }
  });

  QAction *openAction = fileMenu->addAction("Open Source File");
  connect(openAction, &QAction::triggered, this, &MainWindow::openFile);

  QAction *projSetAction = fileMenu->addAction("Project Settings");
  connect(projSetAction, &QAction::triggered, this, [this]() {
    ProjectSettingsDialog dlg(this);
    dlg.exec();
  });

  QAction *slurmAction = fileMenu->addAction("Submit SLURM Job");
  connect(slurmAction, &QAction::triggered, this, [this]() {
    const auto slurm = gridlock::core::ConfigManager::instance().getSlurmSettings();
    QString script = slurm.scriptTemplate;
    script.replace("{FILE}", m_currentFile);
    if (m_spackManager) {
      m_spackManager->appendMessage("Submitting SLURM batch job...");
      m_bottomTabs->setCurrentWidget(m_spackManager);
    }
    m_hpcBackend->submitSlurmJob(script);
  });

  fileMenu->addAction("Exit", this, &MainWindow::close);

  QMenu *editMenu = menuBar->addMenu("&Edit");
  editMenu->addAction("Undo");
  editMenu->addAction("Redo");
  QAction *prefAction = editMenu->addAction("Preferences");
  connect(prefAction, &QAction::triggered, this, &MainWindow::openPreferences);

  QMenu *viewMenu = menuBar->addMenu("&View");
  viewMenu->addAction("Toggle Bottom Tabs");
  viewMenu->addAction("Zoom In");
  viewMenu->addAction("Zoom Out");

  QMenu *historyMenu = menuBar->addMenu("&History");
  historyMenu->addAction("Recent Executions");
  historyMenu->addAction("Session Logs");

  QMenu *bookMenu = menuBar->addMenu("&Bookmarks");
  bookMenu->addAction("Saved Code Pointers");

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
  helpMenu->addAction("Documentation Index");
  QAction *aboutAction = helpMenu->addAction("About GridLock");
  connect(aboutAction, &QAction::triggered, this, [this]() {
    QMessageBox::about(this, "About GridLock",
                       "<b>GridLock IDE</b><br><br>"
                       "A high-performance graphical debugger for MPI applications.<br><br>"
                       "<b>Acknowledgments & Attributions:</b><ul>"
                       "<li><a href=\"https://www.gnu.org/software/ddd/\">GNU DDD</a>: For pioneering visual data display in debugging.</li>"
                       "<li><a href=\"https://www.kdbg.org/\">KDbg</a>: For early inspiration on KDE/Qt-based GDB frontend wrappers.</li>"
                       "<li><a href=\"https://www.gdbgui.com/\">gdbgui</a>: For demonstrating the power of browser-based debug visualization, which inspired our native DifferentialGrid.</li>"
                       "<li><a href=\"https://zealdocs.org/\">Zeal</a>: For the open-source .docset standard and offline documentation workflows that power our Reference Manual.</li>"
                       "</ul><br>"
                       "Special thanks to the <b>Spack</b>, <b>SLURM</b>, and <b>OpenMPI</b> communities for the tools that power our HPC environments."
                       );
  });
}

void MainWindow::setupToolbar() {
  QToolBar *toolbar = addToolBar("Main Toolbar");
  toolbar->setMovable(false);

  QAction *runAction = new QAction("▶ Run Target", this);
  connect(runAction, &QAction::triggered, this, [this]() {
    auto cmd = std::make_unique<gridlock::core::commands::LaunchCommand>(
        this, "build/mpi_mm_bin", gridlock::core::ConfigManager::instance().getDefaultRanks());
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(runAction);

  QAction *continueAction = new QAction("⏩ Continue", this);
  connect(continueAction, &QAction::triggered, this, [this]() {
    auto cmd = std::make_unique<gridlock::core::commands::ContinueCommand>(m_coordinator, m_focusedRank + 1);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(continueAction);

  QAction *stepAction = new QAction("↷ Step Inst", this);
  connect(stepAction, &QAction::triggered, this, [this]() {
    auto cmd = std::make_unique<gridlock::core::commands::StepCommand>(m_coordinator, m_focusedRank + 1, false);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(stepAction);

  QAction *pauseAction = new QAction("Pause Rank", this);
  connect(pauseAction, &QAction::triggered, this, [this]() {
    auto cmd = std::make_unique<gridlock::core::commands::PauseCommand>(m_coordinator, m_focusedRank + 1);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(pauseAction);

  QAction *exitAction = new QAction("Terminate Session", this);
  connect(exitAction, &QAction::triggered, this, [this]() {
    auto cmd = std::make_unique<gridlock::core::commands::TerminateCommand>(this);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(exitAction);
}

void MainWindow::setupDocks() {
  QSplitter *mainVerticalSplitter = new QSplitter(Qt::Vertical, this);
  mainVerticalSplitter->setContentsMargins(0, 0, 0, 0);

  m_projectExplorerWidget = new ProjectExplorerWidget(this);
  addDockWidget(Qt::LeftDockWidgetArea, m_projectExplorerWidget);

  m_variablesDockWidget = new VariablesDockWidget(this);
  // It's no longer a dock widget, it will be added to the splitter

  connect(m_projectExplorerWidget, &ProjectExplorerWidget::fileDoubleClicked, this, [this](const QString& filePath) {
      if (m_editorTabManager) {
          loadSourceFile(filePath); // Actually loadSourceFile does everything needed, or we can just call m_editorTabManager->openFile(filePath) and m_lspCoordinator
      }
  });

  QSplitter *masterHorizontalSplitter =
      new QSplitter(Qt::Horizontal, mainVerticalSplitter);

  m_editorTabManager = new EditorTabManager(masterHorizontalSplitter);
  m_editorTabManager->setMinimumWidth(350);
  connect(m_editorTabManager, &EditorTabManager::toggleBreakpointRequested, this,
          [this](const QString &loc) {
            if (m_coordinator)
              m_coordinator->insertBreakpoint(loc);
          });
  connect(m_editorTabManager, &EditorTabManager::breakpointToggled, this,
          [this](const QString &file, int line, bool ctrlClicked) {
            QString absoluteFilePath = QFileInfo(file).absoluteFilePath();
            QString condition;

            if (ctrlClicked) {
                bool ok;
                condition = QInputDialog::getText(this, "Conditional Breakpoint", "Enter condition (e.g. i == 5):", QLineEdit::Normal, "", &ok);
                if (!ok) return; // Cancelled
            }

            // Update persistent cache
            if (m_persistentBreakpoints[absoluteFilePath].contains(line) && condition.isEmpty() && !ctrlClicked) {
              m_persistentBreakpoints[absoluteFilePath].remove(line);
            } else {
              m_persistentBreakpoints[absoluteFilePath].insert(line);
            }
            gridlock::core::ConfigManager::instance().saveBreakpoints(
                m_persistentBreakpoints);

            if (m_coordinator)
              m_coordinator->broadcastBreakpoint(absoluteFilePath, line, condition);
          });

  connect(m_editorTabManager, &EditorTabManager::hoverVariableRequested, this, [this](const QString &varName, const QPoint &globalPos) {
      if (m_coordinator) {
          m_coordinator->evaluateHoverVariable(m_focusedRank, varName, globalPos);
      }
  });

  connect(m_editorTabManager, &EditorTabManager::semanticHoverRequested, this, [this](const QString &file, int line, int character, const QPoint &globalPos) {
      if (m_lspCoordinator) {
          m_lspCoordinator->requestHover(file, line, character, globalPos);
      }
  });

  connect(m_editorTabManager, &EditorTabManager::pinVariableRequested, this, [this](const QString &varName) {
      if (m_coordinator) {
          m_coordinator->registerWatchVariable(varName);
      }
      if (m_differentialGrid) {
          m_differentialGrid->addVariableColumn(varName);
      }
      if (m_bottomTabs) {
          m_bottomTabs->setCurrentWidget(m_differentialGrid);
      }
  });

  QSplitter* rightPaneSplitter = new QSplitter(Qt::Vertical, masterHorizontalSplitter);

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
            if (m_coordinator) {
              m_coordinator->registerWatchVariable(name);
            }
          });
          
  auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
  for (const auto& w : ps.watchExpressions) {
      if (!w.empty()) {
          m_differentialGrid->addVariableColumn(QString::fromStdString(w));
      }
  }
          
  m_expressionEvaluatorWidget = new ExpressionEvaluatorWidget(m_bottomTabs);
  connect(m_expressionEvaluatorWidget, &ExpressionEvaluatorWidget::evaluateRequested, this, [this](const QString& expr) {
      if (m_coordinator) {
          m_coordinator->evaluateExpression(m_focusedRank, expr);
      }
  });
          
  m_referenceManualWidget = new ReferenceManualWidget(m_bottomTabs);
  m_gdbConsoleWidget = new GdbConsoleWidget(m_bottomTabs);
  m_memView = new MemView(m_bottomTabs);
  m_registerView = new RegisterView(m_bottomTabs);
  m_spackManager = new SpackManager(m_hpcBackend, m_bottomTabs);

  connect(m_memView, &MemView::requestMemory, this, [this](const QString &address, int length) {
    if (m_coordinator) {
      m_coordinator->readMemory(m_focusedRank, address, length);
    }
  });

  m_bottomTabs->addTab(m_terminalDock, "Compiler Terminal");
  m_bottomTabs->addTab(m_differentialGrid, "Watch Expressions");
  m_bottomTabs->addTab(m_expressionEvaluatorWidget, "Evaluator");
  m_bottomTabs->addTab(m_referenceManualWidget, "Reference Manual");
  m_bottomTabs->addTab(m_gdbConsoleWidget, "GDB Console");
  m_bottomTabs->addTab(m_memView, "Memory View");
  m_bottomTabs->addTab(m_registerView, "Registers");
  m_bottomTabs->addTab(m_spackManager, "HPC Console");

  mainVerticalSplitter->addWidget(m_bottomTabs);
  mainVerticalSplitter->setStretchFactor(0, 75);
  mainVerticalSplitter->setStretchFactor(1, 25);

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

    if (state.disassemblyText.isEmpty() && m_coordinator) {
      m_coordinator->requestDisassemblyFallback(rankId);
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
    if (m_coordinator)
      m_coordinator->requestDisassemblyFallback(rankId);
  }
}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (m_coordinator) {
    m_coordinator->terminateAllSessions();
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
    if (gridlock::core::ShortcutManager::instance().handleKeyPress(watched, keyEvent)) {
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::openFile() {
  auto &cfg = gridlock::core::ConfigManager::instance();
  const QString startDir = cfg.getLastOpenDir();

  QString fileName = QFileDialog::getOpenFileName(
      this,
      tr("Open Source File"),
      startDir,
      tr("C/C++ Files (*.cpp *.hpp *.h *.c)"),
      nullptr,
      QFileDialog::DontUseNativeDialog);

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
        m_terminalDock->appendText("Successfully loaded: " + m_currentFile + "\n");
      }
    } else {
      if (m_terminalDock) {
        m_terminalDock->appendError("CRITICAL: Failed to load " + filePath + "\n");
      }
    }
  }
}

void MainWindow::openPreferences() {
  auto *dlg = new PreferencesDialog(this);
  dlg->setAttribute(Qt::WA_DeleteOnClose);

  // React to live Apply/OK so views refresh without a restart
  connect(dlg, &PreferencesDialog::preferencesChanged, this, [this]() {
    // SourceCodeView and RegisterView may want to re-read palette / font prefs.
    if (m_editorTabManager) {
        for (int i = 0; i < m_editorTabManager->count(); ++i) {
            if (auto view = qobject_cast<SourceCodeView*>(m_editorTabManager->widget(i))) {
                view->update();
            }
        }
    }
    if (m_registerView)   m_registerView->update();
  });

  dlg->exec();
}

void MainWindow::startDebuggingSession(const QString &binaryPath, int ranks) {
  if (!m_coordinator)
    return;

  if (m_currentFile.isEmpty() ||
      (getSourceCodeView() && getSourceCodeView()->toPlainText().trimmed().isEmpty())) {
    loadSourceFile("tests/mpi_mm.c");
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
      for (const auto& w : watches) {
          m_coordinator->registerWatchVariable(QString::fromStdString(w));
      }
  }
}

} // namespace gridlock::ui