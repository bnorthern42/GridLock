#include <QtTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include "test_advanced_features.hpp"
#include "../src/core/hpc/DeadlockAnalyzer.hpp"
#define private public
#include "../src/core/hpc/GdbRankCoordinator.hpp"
#undef private
#include "../src/core/models/VariableTreeModel.hpp"
#include "../src/core/managers/ConfigManager.hpp"


using namespace gridlock;
using namespace gridlock::core;

void TestAdvancedFeatures::testDeadlockAnalyzer() {
    GdbRankCoordinator coord;
    coord.initializeMockSession(1); // Set up internal states for 1 process
    DeadlockAnalyzer analyzer(&coord);
    
    QSignalSpy spy(&analyzer, &DeadlockAnalyzer::deadlockDetected);
    
    // Trigger the stopped state so analyzer sets pending to true
    gridlock::RankState state;
    state.currentState = "stopped";
    emit coord.rankStateChanged(0, state);
    
    // Simulate -stack-list-frames output with MPI_Barrier
    QString mockPayload = "^done,stack=[frame={level=\"0\",func=\"MPI_Barrier\",line=\"10\"}]\n";
    coord.processGdbOutput(0, mockPayload);
    
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).value<DeadlockInfo>().rankId, 0);
    QCOMPARE(args.at(0).value<DeadlockInfo>().blockedFunction, QString("MPI_Barrier"));
    
    // Test MPI_Wait
    emit coord.rankStateChanged(0, state);
    QString mockPayload2 = "^done,stack=[frame={level=\"0\",func=\"PMPI_Wait\",line=\"20\"}]\n";
    coord.processGdbOutput(0, mockPayload2);
    
    QCOMPARE(spy.count(), 1);
    args = spy.takeFirst();
    QCOMPARE(args.at(0).value<DeadlockInfo>().rankId, 0);
    QCOMPARE(args.at(0).value<DeadlockInfo>().blockedFunction, QString("PMPI_Wait"));
}

void TestAdvancedFeatures::testFpeTrapper() {
    auto ds = core::ConfigManager::instance().getDebuggerSettings();
    ds.trapFpe = true;
    core::ConfigManager::instance().saveDebuggerSettings(ds);
    
    GdbRankCoordinator coord;
    coord.initializeMockSession(1, true);
    
    // Inject a real QProcess so writeCmd emits commandSentToGdb
    coord.m_processes[0]->process = std::make_unique<QProcess>();
    coord.m_processes[0]->process->start("sleep", QStringList() << "10");
    coord.m_processes[0]->process->waitForStarted();
    
    QSignalSpy spy(&coord, &GdbRankCoordinator::commandSentToGdb);
    coord.broadcastCommand("catch signal SIGFPE");
    
    bool foundFpeCommand = false;
    for (int i = 0; i < spy.count(); ++i) {
        if (spy.at(i).at(1).toString().contains("catch signal SIGFPE")) {
            foundFpeCommand = true;
            break;
        }
    }
    
    QVERIFY(foundFpeCommand);
    ds.trapFpe = false;
    core::ConfigManager::instance().saveDebuggerSettings(ds);
}

void TestAdvancedFeatures::testConditionalBreakpoints() {
    GdbRankCoordinator coord;
    coord.initializeMockSession(1);
    
    coord.m_processes[0]->process = std::make_unique<QProcess>();
    coord.m_processes[0]->process->start("sleep", QStringList() << "10");
    coord.m_processes[0]->process->waitForStarted();
    
    QSignalSpy spy(&coord, &GdbRankCoordinator::commandSentToGdb);
    
    coord.broadcastBreakpoint("main.cpp", 42, true, "i == 100 && rank == 0");
    
    bool foundCondBreak = false;
    for (int i = 0; i < spy.count(); ++i) {
        if (spy.at(i).at(1).toString().contains("-break-insert -c \"i == 100 && rank == 0\" main.cpp:42")) {
            foundCondBreak = true;
            break;
        }
    }
    
    QVERIFY(foundCondBreak);
}

void TestAdvancedFeatures::testValueChangeHighlighting() {
    GdbRankCoordinator coord;
    VariableTreeModel model(&coord, nullptr);
    
    // Initial payload
    QJsonArray initialVars;
    QJsonObject var1;
    var1["name"] = "my_var";
    var1["value"] = "10";
    var1["type"] = "int";
    initialVars.append(var1);
    
    model.onLocalsUpdated(0, 0, initialVars);
    
    // Assume my_var is at row 0, column 1
    QModelIndex index = model.index(0, 1);
    QCOMPARE(model.data(index, Qt::DisplayRole).toString(), QString("10"));
    
    // No highlight initially since there's no previous value
    QVariant bg = model.data(index, Qt::BackgroundRole);
    QVERIFY(bg.isNull() || !bg.isValid());
    
    // Update value
    model.loadLocals(0); // This should store previous values
    
    QJsonArray updatedVars;
    QJsonObject var1_updated;
    var1_updated["name"] = "my_var";
    var1_updated["value"] = "20";
    var1_updated["type"] = "int";
    updatedVars.append(var1_updated);
    
    model.onLocalsUpdated(0, 0, updatedVars);
    
    QModelIndex updatedIndex = model.index(0, 1);
    QCOMPARE(model.data(updatedIndex, Qt::DisplayRole).toString(), QString("20"));
    
    // It should now have a highlight since the value changed
    QVariant newBg = model.data(updatedIndex, Qt::BackgroundRole);
    QVERIFY(newBg.isValid());
    QCOMPARE(newBg.value<QColor>().name(), QString("#f38ba8"));
}

QTEST_MAIN(TestAdvancedFeatures)
