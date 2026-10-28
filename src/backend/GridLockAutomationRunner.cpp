/**
 * @file GridLockAutomationRunner.cpp
 * @brief Implementation of the automated debugging simulation runner.
 *
 * Implements compilation orchestration via ninja, multi-rank debug session
 * initialization, breakpoint management, and real-time UI synchronization
 * with SourceCodeView and GdbRankCoordinator.
 */

#include "GridLockAutomationRunner.hpp"
#include "../ui/MainWindow.hpp"
#include "../ui/views/DisassemblyView.hpp"
#include "../ui/views/ServerRackView.hpp"
#include "../ui/views/SourceCodeView.hpp"
#include "../ui/widgets/DifferentialGrid.hpp"
#include "../ui/widgets/MpiNetworkLogWidget.hpp"
#include "TestGenerator.hpp"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <iostream>

namespace gridlock::backend {

/**
 * @brief Constructs the automation runner and binds coordinator signals to MainWindow.
 * @param mainWindow Host main window receiving simulation telemetry.
 * @param parent Optional parent QObject context.
 */
GridLockAutomationRunner::GridLockAutomationRunner(ui::MainWindow* mainWindow,
                                                   QObject* parent)
    : QObject(parent),
      m_mainWindow(mainWindow),
      m_coordinator(std::make_unique<GdbRankCoordinator>()) {
    m_mainWindow->setCoordinator(m_coordinator.get());

    // Connect coordinator signals to internal handler and main window view router.
    connect(m_coordinator.get(), &GdbRankCoordinator::rankStateChanged, this,
            &GridLockAutomationRunner::onRankStateChanged);
    connect(m_coordinator.get(), &GdbRankCoordinator::rankStateChanged,
            m_mainWindow, &ui::MainWindow::onRankStateChanged);

    // Trigger test sequence on user run target action.
    connect(m_mainWindow, &ui::MainWindow::runTargetRequested, this,
            &GridLockAutomationRunner::startTestSequence);

    // Configure 1.5-second step timer for automated ticks.
    connect(&m_timer, &QTimer::timeout, this,
            &GridLockAutomationRunner::runNextStep);
    m_timer.setInterval(1500);
    m_timer.setSingleShot(false);
}

/**
 * @brief Initiates timer-driven test sequence.
 */
void GridLockAutomationRunner::startTestSequence() {
    m_timer.start();
}

/**
 * @brief Compiles the sample test application target using Ninja.
 * @return True if compilation process exits with 0, false on missing file or build error.
 */
bool GridLockAutomationRunner::generateAndCompileTestTarget() {
    const QString sourceFile = "tests/mpi_mm.c";
    if (!QFile::exists(sourceFile)) {
        qDebug() << "Test file missing: tests/mpi_mm.c";
        if (m_mainWindow && m_mainWindow->networkLog()) {
            m_mainWindow->networkLog()->appendError(
                "Error: tests/mpi_mm.c not found!\n");
        }
        return false;
    }

    // Capture compilation stdout/stderr into the UI network log.
    QProcess compiler;
    if (m_mainWindow && m_mainWindow->networkLog()) {
        connect(&compiler, &QProcess::readyReadStandardOutput, [&]() {
            if (m_mainWindow->networkLog()) {
                m_mainWindow->networkLog()->appendText(
                    QString::fromUtf8(compiler.readAllStandardOutput()));
            }
        });
        connect(&compiler, &QProcess::readyReadStandardError, [&]() {
            if (m_mainWindow->networkLog()) {
                m_mainWindow->networkLog()->appendError(
                    QString::fromUtf8(compiler.readAllStandardError()));
            }
        });
        m_mainWindow->networkLog()->appendText("Compiling tests/mpi_mm.c...\n");
    }

    // Ensure build output directory exists.
    QDir dir;
    if (!dir.exists("build")) {
        dir.mkpath("build");
    }

    // Clean and rebuild test target binary.
    compiler.start("ninja", QStringList() << "-C" << "build" << "clean");
    compiler.waitForFinished();
    compiler.start("ninja", QStringList() << "-C" << "build" << "mpi_mm_bin");
    compiler.waitForFinished();

    if (compiler.exitCode() != 0) {
        qDebug() << "Compilation failed!";
        if (m_mainWindow && m_mainWindow->networkLog()) {
            m_mainWindow->networkLog()->appendError("Compilation failed!\n");
        }
        return false;
    }

    if (m_mainWindow && m_mainWindow->networkLog()) {
        m_mainWindow->networkLog()->appendText("Compilation succeeded.\n");
    }

    return true;
}

/**
 * @brief Progresses the automation sequence state machine.
 */
void GridLockAutomationRunner::runNextStep() {
    m_step++;
    if (m_step == 1) {
        // Step 1: Compile binary fixture and display initial source code.
        std::cout << "[TEST] Tick 1: Initialize (Compile and Load)\n";
        generateAndCompileTestTarget();
        m_mainWindow->loadSourceFile("tests/mpi_mm.c");
        QFile file("tests/mpi_mm.c");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_testSourceCode = file.readAll();
        }
    } else if (m_step == 2) {
        // Step 2: Launch multi-rank parallel debugging session.
        std::cout << "[TEST] Tick 2: Launch\n";
        m_mainWindow->startDebuggingSession("build/mpi_mm_bin", 2);
    } else if (m_step == 3) {
        // Step 3: Insert synchronization breakpoint at line 18.
        std::cout << "[TEST] Tick 3: Breakpoint\n";
        m_coordinator->insertBreakpoint("tests/mpi_mm.c:18");
    } else if (m_step == 4) {
        // Step 4: Dispatch -exec-run command to resume all ranks.
        std::cout << "[TEST] Tick 4: Execution\n";
        m_coordinator->runAll();
    } else if (m_step == 5) {
        // Step 5: Complete simulation run and deactivate timer.
        std::cout << "[TEST] Tick 5: Simulation End. (Views auto-populated by GdbRankCoordinator events)\n";
        m_timer.stop();
    }
}

/**
 * @brief Updates source view highlight when a rank halts on a breakpoint.
 * @param state RankState snapshot containing stopped line number.
 */
void GridLockAutomationRunner::onRankStateChanged(int /*rankId*/,
                                                 const RankState& state) {
    if (state.currentState == "stopped" && state.currentLine > 0) {
        if (auto* scv = m_mainWindow->sourceCodeView()) {
            scv->setSourceCode(m_testSourceCode, state.currentLine);
        }
    }
}

}  // namespace gridlock::backend
