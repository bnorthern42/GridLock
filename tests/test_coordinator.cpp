#include "test_coordinator.hpp"

void TestGdbCoordinator::initTestCase() {
}

void TestGdbCoordinator::testInitialRankStates() {
    gridlock::GdbRankCoordinator coordinator;
    
    QCOMPARE(coordinator.getProcessCount(), 0);
    
    gridlock::RankState state = coordinator.getRankState(0);
    QCOMPARE(state.currentState, QString("offline"));
}

void TestGdbCoordinator::testDataDrivenParser_data() {
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expectedState");
    QTest::addColumn<int>("expectedLine");
    QTest::addColumn<QString>("expectedFile");

    QTest::newRow("Stopped at breakpoint") 
        << "*stopped,reason=\"breakpoint-hit\",disp=\"keep\",bkptno=\"1\",frame={addr=\"0x401000\",func=\"main\",args=[],file=\"mpi_mm.c\",fullname=\"/tmp/mpi_mm.c\",line=\"26\"}\n"
        << "stopped" << 26 << "/tmp/mpi_mm.c";

    QTest::newRow("Stopped signal") 
        << "*stopped,reason=\"signal-received\",signal-name=\"SIGSEGV\",frame={addr=\"0x123\",func=\"bad_func\",file=\"test.c\",fullname=\"/tmp/test.c\",line=\"42\"}\n"
        << "stopped" << 42 << "/tmp/test.c";

    QTest::newRow("Running") 
        << "*running,thread-id=\"all\"\n"
        << "running" << -1 << "";

    QTest::newRow("Exited normally") 
        << "*stopped,reason=\"exited-normally\"\n"
        << "exited" << -1 << "";
}

void TestGdbCoordinator::testDataDrivenParser() {
    QFETCH(QString, input);
    QFETCH(QString, expectedState);
    QFETCH(int, expectedLine);
    QFETCH(QString, expectedFile);

    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(1);
    
    coord.processGdbOutput(0, input);

    auto state = coord.getRankState(0);
    QCOMPARE(state.currentState, expectedState);
    
    // GDB parser falls back to -1 when line is missing or 0
    if (expectedLine > 0) {
        QCOMPARE(state.currentLine, expectedLine);
    }
    if (!expectedFile.isEmpty()) {
        QCOMPARE(state.currentFile, expectedFile);
    }
}

void TestGdbCoordinator::testMultiRankSync() {
    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(4);

    // Simulate interleaved messages
    coord.processGdbOutput(0, "*stopped,reason=\"breakpoint-hit\",frame={fullname=\"rank0.c\",line=\"10\"}\n");
    coord.processGdbOutput(1, "*stopped,reason=\"breakpoint-hit\",frame={fullname=\"rank1.c\",line=\"20\"}\n");
    coord.processGdbOutput(2, "*running,thread-id=\"all\"\n");
    coord.processGdbOutput(3, "*stopped,reason=\"exited\"\n");

    QCOMPARE(coord.getRankState(0).currentState, QString("stopped"));
    QCOMPARE(coord.getRankState(0).currentLine, 10);
    
    QCOMPARE(coord.getRankState(1).currentState, QString("stopped"));
    QCOMPARE(coord.getRankState(1).currentLine, 20);

    QCOMPARE(coord.getRankState(2).currentState, QString("running"));
    
    QCOMPARE(coord.getRankState(3).currentState, QString("exited"));
}

void TestGdbCoordinator::testBreakpointCacheFlush() {
    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(1, true);

    // Fake an internal sync
    coord.processGdbOutput(0, "*stopped,reason=\"signal-received\"\n");

    // The coordinator flushes breakpoints because lastFiredTimestamp is empty.
    QCOMPARE(coord.getRankState(0).lastFiredTimestamp, QString("synced"));
}

void TestGdbCoordinator::cleanupTestCase() {
}

QTEST_MAIN(TestGdbCoordinator)
