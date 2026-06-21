#include "GridLockAutomationRunner.hpp"
#include "TestGenerator.hpp"
#include "../ui/MainWindow.hpp"
#include "../ui/ServerRackView.hpp"
#include "../ui/SourceCodeView.hpp"
#include "../ui/DisassemblyView.hpp"
#include "../ui/DifferentialGrid.hpp"
#include "../ui/TerminalDock.hpp"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <iostream>

namespace gridlock::backend {

GridLockAutomationRunner::GridLockAutomationRunner(ui::MainWindow* mainWindow, QObject* parent)
    : QObject(parent), m_mainWindow(mainWindow), m_coordinator(std::make_unique<GdbRankCoordinator>()) {
    
    m_mainWindow->setCoordinator(m_coordinator.get());
    
    // Also dispatch to main window natively
    connect(m_coordinator.get(), &GdbRankCoordinator::rankStateChanged,
            this, &GridLockAutomationRunner::onRankStateChanged);
    connect(m_coordinator.get(), &GdbRankCoordinator::rankStateChanged,
            m_mainWindow, &ui::MainWindow::onRankStateChanged);
            
    connect(m_mainWindow, &ui::MainWindow::runTargetRequested, this, &GridLockAutomationRunner::startTestSequence);
            
    connect(&m_timer, &QTimer::timeout, this, &GridLockAutomationRunner::runNextStep);
    m_timer.setInterval(1500);
    m_timer.setSingleShot(false);
}

void GridLockAutomationRunner::startTestSequence() {
    m_timer.start();
}

bool GridLockAutomationRunner::generateAndCompileTestTarget() {
    QString sourceFile = "tests/mpi_mm.c";
    if (!QFile::exists(sourceFile)) {
        qDebug() << "Test file missing: tests/mpi_mm.c";
        if (m_mainWindow && m_mainWindow->terminalDock()) {
            m_mainWindow->terminalDock()->appendError("Error: tests/mpi_mm.c not found!\n");
        }
        return false;
    }

    QProcess compiler;
    if (m_mainWindow && m_mainWindow->terminalDock()) {
        connect(&compiler, &QProcess::readyReadStandardOutput, [&]() {
            m_mainWindow->terminalDock()->appendText(QString::fromUtf8(compiler.readAllStandardOutput()));
        });
        connect(&compiler, &QProcess::readyReadStandardError, [&]() {
            m_mainWindow->terminalDock()->appendError(QString::fromUtf8(compiler.readAllStandardError()));
        });
        m_mainWindow->terminalDock()->appendText("Compiling tests/mpi_mm.c...\n");
    }

    QDir dir;
    if (!dir.exists("build")) {
        dir.mkpath("build");
    }

    compiler.start("ninja", QStringList() << "-C" << "build" << "clean");
    compiler.waitForFinished();
    compiler.start("ninja", QStringList() << "-C" << "build" << "mpi_mm_bin");
    compiler.waitForFinished();
    
    if (compiler.exitCode() != 0) {
        qDebug() << "Compilation failed!";
        if (m_mainWindow && m_mainWindow->terminalDock()) {
            m_mainWindow->terminalDock()->appendError("Compilation failed!\n");
        }
        return false;
    }
    
    if (m_mainWindow && m_mainWindow->terminalDock()) {
        m_mainWindow->terminalDock()->appendText("Compilation succeeded.\n");
    }

    return true;
}

void GridLockAutomationRunner::runNextStep() {
    m_step++;
    if (m_step == 1) {
        std::cout << "[TEST] Tick 1: Initialize (Compile and Load)\n";
        generateAndCompileTestTarget();
        m_mainWindow->loadSourceFile("tests/mpi_mm.c");
        QFile file("tests/mpi_mm.c");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_testSourceCode = file.readAll();
        }
    } else if (m_step == 2) {
        std::cout << "[TEST] Tick 2: Launch\n";
        m_mainWindow->startDebuggingSession("build/mpi_mm_bin", 2);
    } else if (m_step == 3) {
        std::cout << "[TEST] Tick 3: Breakpoint\n";
        m_coordinator->insertBreakpoint("tests/mpi_mm.c:18");
    } else if (m_step == 4) {
        std::cout << "[TEST] Tick 4: Execution\n";
        m_coordinator->runAll(); // This already sends -exec-run
    } else if (m_step == 5) {
        std::cout << "[TEST] Tick 5: Simulation End. (Views auto-populated by GdbRankCoordinator events)\n";
        m_timer.stop();
    }
}

void GridLockAutomationRunner::onRankStateChanged(int /*rankId*/, const RankState& state) {
    if (state.currentState == "stopped" && state.currentLine > 0) {
        m_mainWindow->sourceCodeView()->setSourceCode(m_testSourceCode, state.currentLine);
    }
}

} // namespace gridlock::backend
