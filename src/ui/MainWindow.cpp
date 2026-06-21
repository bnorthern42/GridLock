#include "MainWindow.hpp"
#include "../GdbRankCoordinator.hpp"
#include "../core/ConfigManager.hpp"
#include "../core/LspCoordinator.hpp"
#include "DifferentialGrid.hpp"
#include "DisassemblyView.hpp"
#include "GdbConsoleWidget.hpp"
#include "GdbConsoleWidget.hpp"
#include "MemView.hpp"
#include "RegisterView.hpp"
#include "ReferenceDock.hpp"
#include "ServerRackView.hpp"
#include "SourceCodeView.hpp"
#include "TerminalDock.hpp"
#include <QAction>
#include <QCloseEvent>
#include <QDialog>
#include <QDialog>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
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

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setupUi();
  setupMenu();
  setupDocks();

  // Eagerly load default source file so UI is populated before running
  m_lspCoordinator = new gridlock::core::LspCoordinator(this);
  
  connect(m_lspCoordinator, &gridlock::core::LspCoordinator::hoverResultReceived, this, [this](const QString &resultMarkdown, const QPoint &globalPos) {
      if (m_sourceCodeView) {
          QToolTip::showText(globalPos, resultMarkdown, m_sourceCodeView);
      }
  });

  loadSourceFile("tests/mpi_mm.c");

  // Load offline persistence TOML breakpoints
  m_persistentBreakpoints =
      gridlock::core::ConfigManager::instance().getBreakpoints();
  QString absolutePath = QFileInfo("tests/mpi_mm.c").absoluteFilePath();
  if (m_persistentBreakpoints.contains(absolutePath) && m_sourceCodeView) {
    m_sourceCodeView->setBreakpoints(m_persistentBreakpoints[absolutePath]);
  }
}

void MainWindow::setCoordinator(gridlock::GdbRankCoordinator *coord) {
  m_coordinator = coord;
  if (m_coordinator) {
    connect(m_coordinator, &GdbRankCoordinator::hoverEvaluationComplete, this, [this](QString varName, QString result, QPoint globalPos) {
        if (m_sourceCodeView) {
            QToolTip::showText(globalPos, QString("<b>%1</b>: %2").arg(varName).arg(result), m_sourceCodeView);
        }
    });
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
    binaryEdit->setText("build/mpi_mm_bin");
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

  QMenu *profMenu = menuBar->addMenu("&Profiles");
  profMenu->addAction("MPI Cluster/Node Configurations");

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
  helpMenu->addAction("About GridLock");
}

void MainWindow::setupToolbar() {
  QToolBar *toolbar = addToolBar("Main Toolbar");
  toolbar->setMovable(false);

  QAction *runAction = new QAction("▶ Run Target", this);
  connect(runAction, &QAction::triggered, this, [this]() {
    startDebuggingSession(
        "build/mpi_mm_bin",
        gridlock::core::ConfigManager::instance().getDefaultRanks());
  });
  toolbar->addAction(runAction);

  QAction *continueAction = new QAction("⏩ Continue", this);
  connect(continueAction, &QAction::triggered, this, [this]() {
    if (m_coordinator)
      m_coordinator->continueAll();
  });
  toolbar->addAction(continueAction);

  QAction *stepAction = new QAction("↷ Step Inst", this);
  connect(stepAction, &QAction::triggered, this, [this]() {
    if (m_coordinator)
      m_coordinator->stepAll();
  });
  toolbar->addAction(stepAction);

  QAction *pauseAction = new QAction("Pause Rank", this);
  connect(pauseAction, &QAction::triggered, this, [this]() {
    if (m_coordinator)
      m_coordinator->pauseFocusedRank(m_focusedRank);
  });
  toolbar->addAction(pauseAction);

  QAction *exitAction = new QAction("Terminate Session", this);
  connect(exitAction, &QAction::triggered, this, &MainWindow::close);
  toolbar->addAction(exitAction);
}

void MainWindow::setupDocks() {
  QSplitter *mainVerticalSplitter = new QSplitter(Qt::Vertical, this);
  mainVerticalSplitter->setContentsMargins(0, 0, 0, 0);

  QSplitter *masterHorizontalSplitter =
      new QSplitter(Qt::Horizontal, mainVerticalSplitter);

  m_sourceCodeView = new SourceCodeView(masterHorizontalSplitter);
  m_sourceCodeView->setMinimumWidth(350);
  connect(m_sourceCodeView, &SourceCodeView::toggleBreakpointRequested, this,
          [this](const QString &loc) {
            if (m_coordinator)
              m_coordinator->insertBreakpoint(loc);
          });
  connect(m_sourceCodeView, &SourceCodeView::breakpointToggled, this,
          [this](const QString &file, int line, bool ctrlClicked) {
            QString absoluteFilePath = QFileInfo(file).absoluteFilePath();
            bool isAdded = true;
            QString condition;

            if (ctrlClicked) {
                bool ok;
                condition = QInputDialog::getText(this, "Conditional Breakpoint", "Enter condition (e.g. i == 5):", QLineEdit::Normal, "", &ok);
                if (!ok) return; // Cancelled
            }

            // Update persistent cache
            if (m_persistentBreakpoints[absoluteFilePath].contains(line) && condition.isEmpty() && !ctrlClicked) {
              m_persistentBreakpoints[absoluteFilePath].remove(line);
              isAdded = false;
            } else {
              m_persistentBreakpoints[absoluteFilePath].insert(line);
            }
            gridlock::core::ConfigManager::instance().saveBreakpoints(
                m_persistentBreakpoints);

            if (m_coordinator)
              m_coordinator->broadcastBreakpoint(absoluteFilePath, line, condition);
          });

  connect(m_sourceCodeView, &SourceCodeView::hoverVariableRequested, this, [this](const QString &varName, const QPoint &globalPos) {
      if (m_coordinator) {
          m_coordinator->evaluateHoverVariable(m_focusedRank, varName, globalPos);
      }
  });

  connect(m_sourceCodeView, &SourceCodeView::semanticHoverRequested, this, [this](const QString &file, int line, int character, const QPoint &globalPos) {
      if (m_lspCoordinator) {
          m_lspCoordinator->requestHover(file, line, character, globalPos);
      }
  });

  connect(m_sourceCodeView, &SourceCodeView::pinVariableRequested, this, [this](const QString &varName) {
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

  m_disassemblyView = new DisassemblyView(masterHorizontalSplitter);
  m_serverRackView = new ServerRackView(masterHorizontalSplitter);
  connect(m_serverRackView, &ServerRackView::rankSelected, this,
          &MainWindow::onRankSelected);

  masterHorizontalSplitter->addWidget(m_sourceCodeView);
  masterHorizontalSplitter->addWidget(m_disassemblyView);
  masterHorizontalSplitter->addWidget(m_serverRackView);

  masterHorizontalSplitter->setStretchFactor(0, 40);
  masterHorizontalSplitter->setStretchFactor(1, 45);
  masterHorizontalSplitter->setStretchFactor(2, 15);

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
  m_referenceDock = new ReferenceDock(m_bottomTabs);
  m_gdbConsoleWidget = new GdbConsoleWidget(m_bottomTabs);
  m_memView = new MemView(m_bottomTabs);
  m_registerView = new RegisterView(m_bottomTabs);

  connect(m_memView, &MemView::requestMemory, this, [this](const QString &address, int length) {
    if (m_coordinator) {
      m_coordinator->readMemory(m_focusedRank, address, length);
    }
  });

  m_bottomTabs->addTab(m_terminalDock, "Compiler Terminal");
  m_bottomTabs->addTab(m_differentialGrid, "Watch Expressions");
  m_bottomTabs->addTab(m_referenceDock, "Reference Manual");
  m_bottomTabs->addTab(m_gdbConsoleWidget, "GDB Console");
  m_bottomTabs->addTab(m_memView, "Memory View");
  m_bottomTabs->addTab(m_registerView, "Registers");

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
        m_sourceCodeView->highlightCurrentLine(state.currentLine);
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
        m_sourceCodeView->highlightCurrentLine(state.currentLine);
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
  event->accept();
}

void MainWindow::openFile() {
  QString fileName = QFileDialog::getOpenFileName(
      this, "Open Source File", "", "C++ Files (*.cpp *.hpp *.h *.c)");
  if (!fileName.isEmpty()) {
    loadSourceFile(fileName);
  }
}

void MainWindow::loadSourceFile(const QString &filePath) {
  QFile file(filePath);
  // Check direct path, then check fallback for meson build dirs
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    file.setFileName("../" + filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      if (m_terminalDock)
        m_terminalDock->appendError("CRITICAL: Failed to load " + filePath +
                                    "\n");
      return;
    }
  }

  QString code = file.readAll();
  code.replace("\r\n", "\n");
  m_currentFile = file.fileName();

  if (m_sourceCodeView) {
    m_sourceCodeView->setSourceCode(code);
    m_sourceCodeView->setCurrentFile(m_currentFile);
    QString absolutePath = QFileInfo(m_currentFile).absoluteFilePath();
    if (m_persistentBreakpoints.contains(absolutePath)) {
      m_sourceCodeView->setBreakpoints(m_persistentBreakpoints[absolutePath]);
    } else {
      m_sourceCodeView->setBreakpoints(QSet<int>());
    }
    if (m_lspCoordinator) {
      m_lspCoordinator->stop();
      m_lspCoordinator->start(absolutePath);
      m_lspCoordinator->didOpen(m_currentFile, code);
    }
  }

  if (m_terminalDock) {
    m_terminalDock->appendText("Successfully loaded: " + m_currentFile + "\n");
  }
  file.close();
}

void MainWindow::openPreferences() {
  QDialog dialog(this);
  dialog.setWindowTitle("Preferences");
  QFormLayout *form = new QFormLayout(&dialog);

  QSettings settings("GridLock", "Debugger");

  QLineEdit *mpiExecEdit = new QLineEdit(&dialog);
  mpiExecEdit->setText(settings.value("mpi_executable", "mpiexec").toString());
  form->addRow("MPI Executable:", mpiExecEdit);

  QSpinBox *rankBox = new QSpinBox(&dialog);
  rankBox->setMinimum(1);
  rankBox->setValue(
      settings
          .value("rank_count",
                 gridlock::core::ConfigManager::instance().getDefaultRanks())
          .toInt());
  form->addRow("Rank Count:", rankBox);

  QLineEdit *extraArgsEdit = new QLineEdit(&dialog);
  extraArgsEdit->setText(
      settings.value("extra_args", "--oversubscribe").toString());
  form->addRow("Extra Args:", extraArgsEdit);

  QDialogButtonBox *box = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
  form->addRow(box);
  connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() == QDialog::Accepted) {
    settings.setValue("mpi_executable", mpiExecEdit->text());
    settings.setValue("rank_count", rankBox->value());
    settings.setValue("extra_args", extraArgsEdit->text());
  }
}

void MainWindow::startDebuggingSession(const QString &binaryPath, int ranks) {
  if (!m_coordinator)
    return;

  if (m_currentFile.isEmpty() ||
      m_sourceCodeView->toPlainText().trimmed().isEmpty()) {
    loadSourceFile("tests/mpi_mm.c");
  }

  if (m_gdbConsoleWidget) {
    m_gdbConsoleWidget->setRankCount(ranks);
  }

  // 1. Launch the unified OpenMPI backplane + GDB servers, then attach remote
  // QProcess instances natively. Notice we REMOVED the QTimer and "-exec-run"
  // command here. The Coordinator handles it asynchronously.
  m_coordinator->launchParallelSession(binaryPath, ranks);
}

} // namespace gridlock::ui