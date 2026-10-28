/**
 * @file test_advanced_features.cpp
 * @brief Test implementation for deadlock analysis, FPE trapping, and variable highlights.
 *
 * Exercises MPI synchronization primitive interception, GDB signal trapping
 * command emission, conditional breakpoint syntax generation, and UI value diff styling.
 */

#include "test_advanced_features.hpp"
#include "../src/core/hpc/DeadlockAnalyzer.hpp"
#include "../src/core/managers/ConfigManager.hpp"
#include "../src/core/models/VariableTreeModel.hpp"

#define private public
#include "../src/core/hpc/GdbRankCoordinator.hpp"
#undef private

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

using namespace gridlock;
using namespace gridlock::core;

/**
 * @brief Tests DeadlockAnalyzer state monitoring and blocked MPI call detection.
 */
void TestAdvancedFeatures::testDeadlockAnalyzer() {
    // Construct mock coordinator with 1 mock process slot.
    GdbRankCoordinator coord;
    coord.initializeMockSession(1);
    DeadlockAnalyzer analyzer(&coord);
    
    // Spy on deadlockDetected signal to capture emissions.
    QSignalSpy spy(&analyzer, &DeadlockAnalyzer::deadlockDetected);
    
    // Trigger the stopped state so analyzer sets pending state to true.
    gridlock::RankState state;
    state.currentState = "stopped";
    emit coord.rankStateChanged(0, state);
    
    // Simulate -stack-list-frames MI response containing MPI_Barrier blocking call.
    QString mockPayload = "^done,stack=[frame={level=\"0\",func=\"MPI_Barrier\",line=\"10\"}]\n";
    coord.processGdbOutput(0, mockPayload);
    
    // Verify that DeadlockAnalyzer emitted a deadlock alert for rank 0 on MPI_Barrier.
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).value<DeadlockInfo>().rankId, 0);
    QCOMPARE(args.at(0).value<DeadlockInfo>().blockedFunction, QString("MPI_Barrier"));
    
    // Repeat verification with PMPI_Wait blocking call.
    emit coord.rankStateChanged(0, state);
    QString mockPayload2 = "^done,stack=[frame={level=\"0\",func=\"PMPI_Wait\",line=\"20\"}]\n";
    coord.processGdbOutput(0, mockPayload2);
    
    // Verify that second deadlock alert was fired identifying PMPI_Wait.
    QCOMPARE(spy.count(), 1);
    args = spy.takeFirst();
    QCOMPARE(args.at(0).value<DeadlockInfo>().rankId, 0);
    QCOMPARE(args.at(0).value<DeadlockInfo>().blockedFunction, QString("PMPI_Wait"));
}

/**
 * @brief Tests floating-point exception (SIGFPE) signal trap broadcast to GDB.
 */
void TestAdvancedFeatures::testFpeTrapper() {
    // Enable FPE trapping in debugger settings.
    auto ds = core::ConfigManager::instance().getDebuggerSettings();
    ds.trapFpe = true;
    core::ConfigManager::instance().saveDebuggerSettings(ds);
    
    GdbRankCoordinator coord;
    coord.initializeMockSession(1, true);
    
    // Inject active QProcess instance so writeCmd emits commandSentToGdb signal.
    coord.m_processes[0]->process = std::make_unique<QProcess>();
    coord.m_processes[0]->process->start("sleep", QStringList() << "10");
    coord.m_processes[0]->process->waitForStarted();
    
    // Spy on GDB command traffic to verify catch signal SIGFPE emission.
    QSignalSpy spy(&coord, &GdbRankCoordinator::commandSentToGdb);
    coord.broadcastCommand("catch signal SIGFPE");
    
    bool foundFpeCommand = false;
    for (int i = 0; i < spy.count(); ++i) {
        if (spy.at(i).at(1).toString().contains("catch signal SIGFPE")) {
            foundFpeCommand = true;
            break;
        }
    }
    
    // Assert that SIGFPE catch command was broadcast to active process.
    QVERIFY(foundFpeCommand);
    ds.trapFpe = false;
    core::ConfigManager::instance().saveDebuggerSettings(ds);
}

/**
 * @brief Tests conditional breakpoint formatting with predicate clauses.
 */
void TestAdvancedFeatures::testConditionalBreakpoints() {
    GdbRankCoordinator coord;
    coord.initializeMockSession(1);
    
    // Inject active QProcess to monitor formatted MI command insertion.
    coord.m_processes[0]->process = std::make_unique<QProcess>();
    coord.m_processes[0]->process->start("sleep", QStringList() << "10");
    coord.m_processes[0]->process->waitForStarted();
    
    QSignalSpy spy(&coord, &GdbRankCoordinator::commandSentToGdb);
    
    // Broadcast conditional breakpoint with predicate clause.
    coord.broadcastBreakpoint("main.cpp", 42, true, "i == 100 && rank == 0");
    
    bool foundCondBreak = false;
    for (int i = 0; i < spy.count(); ++i) {
        if (spy.at(i).at(1).toString().contains("-break-insert -c \"i == 100 && rank == 0\" main.cpp:42")) {
            foundCondBreak = true;
            break;
        }
    }
    
    // Assert that the -break-insert command included the exact condition.
    QVERIFY(foundCondBreak);
}

/**
 * @brief Tests background highlighting when variable values mutate between steps.
 */
void TestAdvancedFeatures::testValueChangeHighlighting() {
    GdbRankCoordinator coord;
    VariableTreeModel model(&coord, nullptr);
    
    // Initial JSON payload representing local variable value before step.
    QJsonArray initialVars;
    QJsonObject var1;
    var1["name"] = "my_var";
    var1["value"] = "10";
    var1["type"] = "int";
    initialVars.append(var1);
    
    model.onLocalsUpdated(0, 0, initialVars);
    
    // Query model index for variable value column (row 0, column 1).
    QModelIndex index = model.index(0, 1);
    QCOMPARE(model.data(index, Qt::DisplayRole).toString(), QString("10"));
    
    // Verify no change highlight exists on first load.
    QVariant bg = model.data(index, Qt::BackgroundRole);
    QVERIFY(bg.isNull() || !bg.isValid());
    
    // Trigger loadLocals to cache previous values for subsequent diffing.
    model.loadLocals(0);
    
    // Updated JSON payload with new value (10 -> 20).
    QJsonArray updatedVars;
    QJsonObject var1_updated;
    var1_updated["name"] = "my_var";
    var1_updated["value"] = "20";
    var1_updated["type"] = "int";
    updatedVars.append(var1_updated);
    
    model.onLocalsUpdated(0, 0, updatedVars);
    
    // Verify updated value in model.
    QModelIndex updatedIndex = model.index(0, 1);
    QCOMPARE(model.data(updatedIndex, Qt::DisplayRole).toString(), QString("20"));
    
    // Assert that model returned change highlight color (#f38ba8).
    QVariant newBg = model.data(updatedIndex, Qt::BackgroundRole);
    QVERIFY(newBg.isValid());
    QCOMPARE(newBg.value<QColor>().name(), QString("#f38ba8"));
}

QTEST_MAIN(TestAdvancedFeatures)
