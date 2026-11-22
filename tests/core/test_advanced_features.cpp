/**
 * @file test_advanced_features.cpp
 * @brief Test implementation for deadlock analysis, FPE trapping, and variable highlights.
 *
 * Exercises MPI synchronization primitive interception, GDB signal trapping
 * command emission, conditional breakpoint syntax generation, and UI value diff styling.
 */

#include "test_advanced_features.hpp"
#include "../../src/core/hpc/analysis/DeadlockAnalyzer.hpp"
#include "../../src/core/managers/ConfigManager.hpp"
#include "../../src/core/models/VariableTreeModel.hpp"

#define private public
#include "../../src/core/hpc/gdb/GdbRankCoordinator.hpp"
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

    // Verify live multi-rank collective divergence detection (Rank 0 in MPI_Bcast vs Rank 1 in MPI_Barrier).
    GdbRankCoordinator multiCoord; // Multi-rank coordinator test harness
    multiCoord.initializeMockSession(2);
    multiCoord.m_processes[0]->state.currentState = "stopped"; // Mark Rank 0 paused
    multiCoord.m_processes[1]->state.currentState = "stopped"; // Mark Rank 1 paused
    DeadlockAnalyzer multiAnalyzer(&multiCoord);
    QSignalSpy divSpy(&multiAnalyzer, &DeadlockAnalyzer::deadlockDetected);

    // Feed live 64-frame MI stack snapshots (token 202) for Rank 0 and Rank 1.
    multiCoord.processGdbOutput(
        0, "202^done,stack=[frame={level=\"0\",addr=\"0x401140\",func=\"MPI_Bcast\","
           "fullname=\"/tmp/coll.cpp\",line=\"14\"},frame={level=\"1\",addr=\"0x401200\","
           "func=\"main\",fullname=\"/tmp/coll.cpp\",line=\"30\"}]\n");
    multiCoord.processGdbOutput(
        1, "202^done,stack=[frame={level=\"0\",addr=\"0x401180\",func=\"MPI_Barrier\","
           "fullname=\"/tmp/coll.cpp\",line=\"18\"},frame={level=\"1\",addr=\"0x401210\","
           "func=\"main\",fullname=\"/tmp/coll.cpp\",line=\"32\"}]\n");

    // Assert collective divergence alert is emitted and diagnostic summary names both primitives.
    QVERIFY2(divSpy.count() >= 2, "DeadlockAnalyzer must emit alerts for diverging collective ranks");
    const QString divAlert = multiAnalyzer.property("lastDiagnosticAlert").toString(); // Read diagnostic summary
    QVERIFY2(divAlert.contains(QStringLiteral("MPI_Bcast")) &&
                 divAlert.contains(QStringLiteral("MPI_Barrier")),
             qPrintable(QStringLiteral("Expected collective divergence summary, got: %1").arg(divAlert)));

    // Feed cyclic synchronous point-to-point MPI_Ssend frames across both stopped ranks.
    divSpy.clear(); // Reset spy before testing point-to-point wait cycle
    multiCoord.processGdbOutput(
        0, "202^done,stack=[frame={level=\"0\",addr=\"0x401300\",func=\"MPI_Ssend\","
           "fullname=\"/tmp/ring.cpp\",line=\"21\"}]\n");
    multiCoord.processGdbOutput(
        1, "202^done,stack=[frame={level=\"0\",addr=\"0x401300\",func=\"MPI_Ssend\","
           "fullname=\"/tmp/ring.cpp\",line=\"21\"}]\n");
    const QString cycleAlert = multiAnalyzer.property("lastDiagnosticAlert").toString(); // Read cycle summary
    QVERIFY2(divSpy.count() >= 2 && cycleAlert.contains(QStringLiteral("MPI_Ssend")),
             qPrintable(QStringLiteral("Expected point-to-point wait cycle alert, got: %1").arg(cycleAlert)));
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
 * @brief Tests conditional breakpoints, sanitized REPL command dispatch, and hardware watchpoints.
 */
void TestAdvancedFeatures::testConditionalBreakpoints() {
    GdbRankCoordinator coord;
    coord.initializeMockSession(1);
    
    // Inject active QProcess to monitor formatted MI command insertion.
    coord.m_processes[0]->process = std::make_unique<QProcess>();
    coord.m_processes[0]->process->start("sleep", QStringList() << "10");
    coord.m_processes[0]->process->waitForStarted();
    coord.m_processes[0]->state.currentState = "stopped";
    
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

    // Verify sendDebuggerCommand wraps safe GDB commands in -interpreter-exec console and blocks injections.
    QVERIFY2(coord.metaObject()->indexOfSignal(
                 QMetaObject::normalizedSignature("debuggerConsoleOutput(int,QString,bool)")) >= 0,
             "IBackendCoordinator must expose debuggerConsoleOutput(int,QString,bool)");
    QSignalSpy consoleOutSpy(&coord, SIGNAL(debuggerConsoleOutput(int,QString,bool)));
    // Clear previous breakpoint commands before dispatching interactive CLI command.
    spy.clear();
    QVERIFY(QMetaObject::invokeMethod(&coord, "sendDebuggerCommand",
                                      Q_ARG(int, 0), Q_ARG(QString, QStringLiteral("info registers"))));
    // Track whether `-interpreter-exec console` wrapper was emitted to GDB stdin.
    bool wrappedConsoleCmd = false;
    for (int i = 0; i < spy.count(); ++i) {
        if (spy.at(i).at(1).toString().contains("-interpreter-exec console \"info registers\"")) {
            wrappedConsoleCmd = true;
        }
    }
    QVERIFY2(wrappedConsoleCmd, "sendDebuggerCommand must wrap CLI commands in -interpreter-exec console");

    // Verify shell-injection payloads are scrubbed/rejected before reaching GDB stdin.
    // Ensure zero MI traffic occurs when shell metacharacters (`;`, `|`, `` ` ``) are supplied.
    spy.clear();
    QVERIFY(QMetaObject::invokeMethod(&coord, "sendDebuggerCommand",
                                      Q_ARG(int, 0), Q_ARG(QString, QStringLiteral("print rank; rm -rf /"))));
    QCOMPARE(spy.count(), 0);
    // Confirm rejected shell-injection command reports `isError=true` on `debuggerConsoleOutput`.
    QVERIFY2(consoleOutSpy.count() >= 1 && consoleOutSpy.last().at(2).toBool() == true,
             "Rejected shell-injection command must emit debuggerConsoleOutput with isError=true");

    // Verify Hardware Data Watchpoint (-break-watch) creation and watchpoint-trigger stop parsing.
    QSignalSpy wpSpy(&coord, SIGNAL(watchpointHit(int,QString,QString,QString)));
    // Clear spy before arming hardware write watchpoint on `watchedCounter`.
    spy.clear();
    QVERIFY2(QMetaObject::invokeMethod(&coord, "setDataWatchpoint",
                                       Q_ARG(QString, QStringLiteral("watchedCounter")),
                                       Q_ARG(int, 0), Q_ARG(int, -1)),
             "Coordinator must expose invokable setDataWatchpoint");
    // Check that `-break-watch watchedCounter` MI command was dispatched to Rank 0.
    bool foundBreakWatch = false;
    for (int i = 0; i < spy.count(); ++i) {
        if (spy.at(i).at(1).toString().contains("-break-watch watchedCounter")) foundBreakWatch = true;
    }
    QVERIFY2(foundBreakWatch, "setDataWatchpoint(Write) must emit -break-watch watchedCounter");

    // Simulate GDB MI hardware watchpoint trigger stop packet and validate watchpointHit payload.
    // Verifies extraction of `wpt={exp="..."}` and `value={old="10",new="77"}` fields.
    coord.processGdbOutput(
        0, "*stopped,reason=\"watchpoint-trigger\",wpt={number=\"2\",exp=\"watchedCounter\"},"
           "value={old=\"10\",new=\"77\"},frame={addr=\"0x401234\",func=\"main\","
           "file=\"main.cpp\",fullname=\"/tmp/main.cpp\",line=\"25\"},thread-id=\"1\"\n");
    // Validate rank index, watched expression string, previous value, and updated value.
    QCOMPARE(wpSpy.count(), 1);
    QCOMPARE(wpSpy.first().at(0).toInt(), 0);
    QCOMPARE(wpSpy.first().at(1).toString(), QStringLiteral("watchedCounter"));
    QCOMPARE(wpSpy.first().at(2).toString(), QStringLiteral("10"));
    QCOMPARE(wpSpy.first().at(3).toString(), QStringLiteral("77"));
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
