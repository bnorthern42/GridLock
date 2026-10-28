/**
 * @file GridLockAutomationRunner.hpp
 * @brief Automated debugging simulation runner for end-to-end integration workflows.
 *
 * Defines the GridLockAutomationRunner class which orchestrates sequential
 * automated test runs, compiling target binaries, dispatching GDB breakpoint
 * injections, and synchronizing MPI rank states to MainWindow views.
 */

#pragma once

#include "../core/hpc/GdbRankCoordinator.hpp"

#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>

namespace gridlock::ui {
class MainWindow;
}  // namespace gridlock::ui

namespace gridlock::backend {

/**
 * @class GridLockAutomationRunner
 * @brief Drives scripted integration simulations through MainWindow and GdbRankCoordinator.
 *
 * Provides a stepped execution timer that compiles sample MPI applications,
 * launches multi-rank debug sessions, verifies breakpoint hits, and tests UI reactivity.
 */
class GridLockAutomationRunner : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructs the automation runner.
     * @param mainWindow Active MainWindow instance whose views receive telemetry.
     * @param parent Optional parent QObject context.
     */
    explicit GridLockAutomationRunner(ui::MainWindow* mainWindow,
                                      QObject* parent = nullptr);

    /**
     * @brief Virtual destructor for clean resource teardown.
     */
    ~GridLockAutomationRunner() override = default;

public slots:
    /**
     * @brief Executes the next step in the multi-stage automated test script.
     */
    void runNextStep();

    /**
     * @brief Starts the automated testing timer sequence.
     */
    void startTestSequence();

private slots:
    /**
     * @brief Slot triggered when any rank transitions state in GdbRankCoordinator.
     * @param rankId Zero-based rank index.
     * @param state Updated RankState snapshot.
     */
    void onRankStateChanged(int rankId, const RankState& state);

private:
    /**
     * @brief Compiles the sample MPI matrix multiplication test binary.
     * @return True if compilation was successful, false otherwise.
     */
    bool generateAndCompileTestTarget();

    /// Target MainWindow interface.
    ui::MainWindow* m_mainWindow = nullptr;

    /// Owned GDB rank coordinator driving the back-end debug session.
    std::unique_ptr<GdbRankCoordinator> m_coordinator;

    /// Step execution cadence timer.
    QTimer m_timer;

    /// Current progression index in the automation sequence.
    int m_step = 0;

    /// Number of simulated MPI ranks to launch.
    int m_rankCount = 4;

    /// Cached source code content loaded from the target file.
    QString m_testSourceCode;
};

}  // namespace gridlock::backend
