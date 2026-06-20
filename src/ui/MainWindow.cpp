#include "MainWindow.hpp"
#include "ServerRackView.hpp"
#include "SourceCodeView.hpp"
#include "DisassemblyView.hpp"
#include "ReferenceDock.hpp"
#include "DifferentialGrid.hpp"
#include "TerminalDock.hpp"
#include "../GdbRankCoordinator.hpp"
#include <QDockWidget>
#include <QToolBar>
#include <QAction>
#include <QCloseEvent>
#include <QMenuBar>
#include <QMenu>
#include <QSplitter>
#include <QFileDialog>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QProcess>
#include <QTabWidget>
#include <QSettings>
#include <QTimer>
#include <QFileInfo>

namespace gridlock::ui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setupUi();
    setupMenu();
    setupDocks();
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
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    QMenu* fileMenu = menuBar->addMenu("&File");
    QAction* newAction = fileMenu->addAction("New Session");
    connect(newAction, &QAction::triggered, this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle("New Session");
        QFormLayout* form = new QFormLayout(&dialog);
        QLineEdit* binaryEdit = new QLineEdit(&dialog);
        binaryEdit->setText("build/test_bin");
        QSpinBox* rankBox = new QSpinBox(&dialog);
        rankBox->setValue(4);
        rankBox->setMinimum(1);
        form->addRow("Target Binary:", binaryEdit);
        form->addRow("Rank Count:", rankBox);
        QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
        form->addRow(box);
        connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted && m_coordinator) {
            startDebuggingSession(binaryEdit->text(), rankBox->value());
        }
    });

    QAction* openAction = fileMenu->addAction("Open Source File");
    connect(openAction, &QAction::triggered, this, &MainWindow::openFile);
    fileMenu->addAction("Exit", this, &MainWindow::close);

    QMenu* editMenu = menuBar->addMenu("&Edit");
    editMenu->addAction("Undo");
    editMenu->addAction("Redo");
    QAction* prefAction = editMenu->addAction("Preferences");
    connect(prefAction, &QAction::triggered, this, &MainWindow::openPreferences);

    QMenu* viewMenu = menuBar->addMenu("&View");
    viewMenu->addAction("Toggle Bottom Tabs");
    viewMenu->addAction("Zoom In");
    viewMenu->addAction("Zoom Out");

    QMenu* historyMenu = menuBar->addMenu("&History");
    historyMenu->addAction("Recent Executions");
    historyMenu->addAction("Session Logs");

    QMenu* bookMenu = menuBar->addMenu("&Bookmarks");
    bookMenu->addAction("Saved Code Pointers");

    QMenu* profMenu = menuBar->addMenu("&Profiles");
    profMenu->addAction("MPI Cluster/Node Configurations");

    QMenu* toolsMenu = menuBar->addMenu("&Tools");
    QAction* buildAction = toolsMenu->addAction("Build Target");
    connect(buildAction, &QAction::triggered, this, [this]() {
        if (!m_terminalDock) return;
        m_terminalDock->appendText("Building target...\n");
        QProcess* proc = new QProcess(this);
        connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
            m_terminalDock->appendText(proc->readAllStandardOutput());
        });
        connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
            m_terminalDock->appendError(proc->readAllStandardError());
        });
        proc->start("mpicxx", QStringList() << "tests/matrix_multiply.cpp" << "-g" << "-O0" << "-o" << "build/test_bin");
    });
    QAction* runTestsAction = toolsMenu->addAction("Run Tests");
    connect(runTestsAction, &QAction::triggered, this, &MainWindow::runTargetRequested);

    QMenu* helpMenu = menuBar->addMenu("&Help");
    helpMenu->addAction("Documentation Index");
    helpMenu->addAction("About GridLock");
}

void MainWindow::setupToolbar() {
    QToolBar* toolbar = addToolBar("Main Toolbar");
    toolbar->setMovable(false);
    toolbar->setStyleSheet("QToolButton { color: #ff5555; font-weight: bold; }");

    QAction* pauseAction = new QAction("Pause Rank", this);
    connect(pauseAction, &QAction::triggered, this, [this]() {
        if (m_coordinator) m_coordinator->pauseFocusedRank(m_focusedRank);
    });
    toolbar->addAction(pauseAction);

    QAction* exitAction = new QAction("Terminate Session", this);
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);
    toolbar->addAction(exitAction);
}

void MainWindow::setupDocks() {
    QSplitter* mainVerticalSplitter = new QSplitter(Qt::Vertical, this);

    QSplitter* masterHorizontalSplitter = new QSplitter(Qt::Horizontal, mainVerticalSplitter);

    m_sourceCodeView = new SourceCodeView(masterHorizontalSplitter);
    m_sourceCodeView->setMinimumWidth(350);
    connect(m_sourceCodeView, &SourceCodeView::runTargetRequested, this, [this]() {
        startDebuggingSession("build/test_bin", 4);
    });
    connect(m_sourceCodeView, &SourceCodeView::continueRequested, this, [this]() {
        if (m_coordinator) m_coordinator->continueAll();
    });
    connect(m_sourceCodeView, &SourceCodeView::stepInstRequested, this, [this]() {
        if (m_coordinator) m_coordinator->stepAll();
    });
    connect(m_sourceCodeView, &SourceCodeView::toggleBreakpointRequested, this, [this](const QString& loc) {
        if (m_coordinator) m_coordinator->insertBreakpoint(loc);
    });
    connect(m_sourceCodeView, &SourceCodeView::breakpointToggled, this, [this](const QString& file, int line) {
        if (m_coordinator) m_coordinator->broadcastBreakpoint(file, line);
    });

    m_disassemblyView = new DisassemblyView(masterHorizontalSplitter);
    m_serverRackView = new ServerRackView(masterHorizontalSplitter);
    connect(m_serverRackView, &ServerRackView::rankSelected, this, &MainWindow::onRankSelected);

    masterHorizontalSplitter->addWidget(m_sourceCodeView);
    masterHorizontalSplitter->addWidget(m_disassemblyView);
    masterHorizontalSplitter->addWidget(m_serverRackView);

    masterHorizontalSplitter->setStretchFactor(0, 40);
    masterHorizontalSplitter->setStretchFactor(1, 45);
    masterHorizontalSplitter->setStretchFactor(2, 15);

    mainVerticalSplitter->addWidget(masterHorizontalSplitter);

    QTabWidget* bottomTabs = new QTabWidget(mainVerticalSplitter);
    
    m_terminalDock = new TerminalDock("Compiler Terminal", bottomTabs);
    m_differentialGrid = new DifferentialGrid(bottomTabs);
    m_referenceDock = new ReferenceDock(bottomTabs);

    bottomTabs->addTab(m_terminalDock, "Compiler Terminal");
    bottomTabs->addTab(m_differentialGrid, "Watch Expressions");
    bottomTabs->addTab(m_referenceDock, "Reference Manual");

    mainVerticalSplitter->addWidget(bottomTabs);
    mainVerticalSplitter->setStretchFactor(0, 75);
    mainVerticalSplitter->setStretchFactor(1, 25);

    setCentralWidget(mainVerticalSplitter);
}

void MainWindow::onRankStateChanged(int rankId, const RankState& state) {
    m_latestStates[rankId] = state;
    m_serverRackView->updateRankState(rankId, state);
    
    if (rankId == m_focusedRank) {
        if (!state.disassemblyText.isEmpty()) {
            m_disassemblyView->updateDisassembly(state.disassemblyText);
        }
    }
    
    for (const auto& kv : state.variableWatches) {
        m_differentialGrid->setVariableData(rankId, kv.first, kv.second);
    }
}

void MainWindow::onRankSelected(int rankId) {
    m_focusedRank = rankId;
    if (m_latestStates.count(rankId)) {
        const auto& state = m_latestStates[rankId];
        m_disassemblyView->updateDisassembly(state.disassemblyText);
        
        if (state.disassemblyText.isEmpty() && m_coordinator) {
            m_coordinator->requestDisassemblyFallback(rankId);
        }
        
        // Force update DifferentialGrid table matrix rows
        for (const auto& kv : state.variableWatches) {
            m_differentialGrid->setVariableData(rankId, kv.first, kv.second);
        }
    } else {
        if (m_coordinator) m_coordinator->requestDisassemblyFallback(rankId);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_coordinator) {
        m_coordinator->terminateAllSessions();
    }
    event->accept();
}

void MainWindow::openFile() {
    QString fileName = QFileDialog::getOpenFileName(this, "Open Source File", "", "C++ Files (*.cpp *.hpp *.h *.c)");
    if (!fileName.isEmpty()) {
        loadSourceFile(fileName);
    }
}

void MainWindow::loadSourceFile(const QString& filePath) {
    QStringList searchPaths = { filePath, "../" + filePath, "../../" + filePath };
    QString targetPath = "";
    
    for (const QString& path : searchPaths) {
        if (QFile::exists(path)) {
            targetPath = path;
            break;
        }
    }
    
    if (targetPath.isEmpty()) {
        if (m_terminalDock) m_terminalDock->appendError("CRITICAL: File not found: " + filePath + "\n");
        return;
    }
    
    QFile file(targetPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_currentFile = QFileInfo(targetPath).absoluteFilePath();
        m_sourceCodeView->setPlainText(file.readAll());
        if (m_terminalDock) m_terminalDock->appendText("Loaded: " + m_currentFile + "\n");
        file.close();
    }
}

void MainWindow::openPreferences() {
    QDialog dialog(this);
    dialog.setWindowTitle("Preferences");
    QFormLayout* form = new QFormLayout(&dialog);
    
    QSettings settings("GridLock", "Debugger");

    QLineEdit* mpiExecEdit = new QLineEdit(&dialog);
    mpiExecEdit->setText(settings.value("mpi_executable", "mpiexec").toString());
    form->addRow("MPI Executable:", mpiExecEdit);

    QSpinBox* rankBox = new QSpinBox(&dialog);
    rankBox->setMinimum(1);
    rankBox->setValue(settings.value("rank_count", 4).toInt());
    form->addRow("Rank Count:", rankBox);

    QLineEdit* extraArgsEdit = new QLineEdit(&dialog);
    extraArgsEdit->setText(settings.value("extra_args", "--oversubscribe").toString());
    form->addRow("Extra Args:", extraArgsEdit);

    QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        settings.setValue("mpi_executable", mpiExecEdit->text());
        settings.setValue("rank_count", rankBox->value());
        settings.setValue("extra_args", extraArgsEdit->text());
    }
}

void MainWindow::startDebuggingSession(const QString& binaryPath, int ranks) {
    if (!m_coordinator) return;
    
    if (m_currentFile.isEmpty()) {
        loadSourceFile("tests/matrix_multiply.cpp");
    }

    // 1. Launch the processes
    m_coordinator->launchParallelSession(binaryPath, ranks);
    
    // 2. Delay to allow GDB to spin up and load symbols
    QTimer::singleShot(500, this, [this]() {
        // 3. Fallback Breakpoint to prevent runaway execution
        m_coordinator->broadcastCommand("-break-insert main\n");
        
        // 4. Inject visual breakpoints (if you have an active map)
        // ...
        
        // 5. Fire execution
        m_coordinator->runAll();
    });
}

} // namespace gridlock::ui
