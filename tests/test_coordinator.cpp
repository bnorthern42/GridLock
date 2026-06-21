#include "test_coordinator.hpp"

void TestGdbCoordinator::initTestCase() {
}

void TestGdbCoordinator::testInitialRankStates() {
    gridlock::GdbRankCoordinator coordinator;
    
    QCOMPARE(coordinator.getProcessCount(), 0);
    
    gridlock::RankState state = coordinator.getRankState(0);
    QCOMPARE(state.currentState, QString("offline"));
}

void TestGdbCoordinator::testGdbMiParser_data() {
    QTest::addColumn<QString>("rawOutput");
    QTest::addColumn<QString>("expectedState");
    QTest::addColumn<int>("expectedLine");
    QTest::addColumn<QString>("expectedFile");

    // Normal breakpoints
    QTest::newRow("Breakpoint: simple") 
        << "*stopped,reason=\"breakpoint-hit\",disp=\"keep\",bkptno=\"1\",frame={addr=\"0x401000\",func=\"main\",args=[],file=\"mpi_mm.c\",fullname=\"/tmp/mpi_mm.c\",line=\"26\"}\n"
        << "stopped" << 26 << "/tmp/mpi_mm.c";
    QTest::newRow("Breakpoint: with thread-id") 
        << "*stopped,reason=\"breakpoint-hit\",disp=\"keep\",bkptno=\"2\",frame={addr=\"0x123\",func=\"calc\",args=[],file=\"solver.cpp\",fullname=\"/usr/src/solver.cpp\",line=\"100\"},thread-id=\"1\",stopped-threads=\"all\"\n"
        << "stopped" << 100 << "/usr/src/solver.cpp";
    QTest::newRow("Breakpoint: missing fullname fallback to empty if missing") 
        << "*stopped,reason=\"breakpoint-hit\",disp=\"keep\",bkptno=\"3\",frame={addr=\"0xabc\",func=\"loop\",args=[],file=\"test.c\",line=\"42\"}\n"
        << "running" << 42 << ""; // Note: GdbRankCoordinator issues exec-continue if currentFile is empty
    QTest::newRow("Breakpoint: missing line defaults to -1") 
        << "*stopped,reason=\"breakpoint-hit\",disp=\"keep\",bkptno=\"4\",frame={addr=\"0xdef\",func=\"init\",args=[],file=\"main.c\",fullname=\"/tmp/main.c\"}\n"
        << "stopped" << -1 << "/tmp/main.c";
    QTest::newRow("Breakpoint: different file path") 
        << "*stopped,reason=\"breakpoint-hit\",disp=\"keep\",bkptno=\"5\",frame={addr=\"0x456\",func=\"setup\",args=[],file=\"app.c\",fullname=\"/opt/app/src/app.c\",line=\"15\"}\n"
        << "stopped" << 15 << "/opt/app/src/app.c";

    // Normal exits
    QTest::newRow("Exit: normally") 
        << "*stopped,reason=\"exited-normally\"\n"
        << "exited" << 0 << "";
    QTest::newRow("Exit: just exited") 
        << "*stopped,reason=\"exited\"\n"
        << "exited" << 0 << "";
    QTest::newRow("Exit: exited normally with thread group") 
        << "*stopped,reason=\"exited-normally\",thread-id=\"1\"\n"
        << "exited" << 0 << "";
    QTest::newRow("Exit: exited with code") 
        << "*stopped,reason=\"exited\",exit-code=\"0\"\n"
        << "exited" << 0 << "";

    // Syntax errors/Segfaults
    QTest::newRow("Signal: SIGSEGV") 
        << "*stopped,reason=\"signal-received\",signal-name=\"SIGSEGV\",frame={addr=\"0x123\",func=\"bad_func\",file=\"test.c\",fullname=\"/tmp/test.c\",line=\"42\"}\n"
        << "stopped" << 42 << "/tmp/test.c";
    QTest::newRow("Signal: SIGINT") 
        << "*stopped,reason=\"signal-received\",signal-name=\"SIGINT\",frame={addr=\"0x456\",func=\"loop\",file=\"loop.c\",fullname=\"/tmp/loop.c\",line=\"88\"}\n"
        << "stopped" << 88 << "/tmp/loop.c";
    QTest::newRow("Signal: SIGFPE") 
        << "*stopped,reason=\"signal-received\",signal-name=\"SIGFPE\",frame={addr=\"0x789\",func=\"div\",file=\"math.c\",fullname=\"/tmp/math.c\",line=\"12\"}\n"
        << "stopped" << 12 << "/tmp/math.c";
    QTest::newRow("Signal: SIGABRT") 
        << "*stopped,reason=\"signal-received\",signal-name=\"SIGABRT\",frame={addr=\"0xabc\",func=\"abort\",file=\"libc.c\",fullname=\"/lib/libc.c\",line=\"99\"}\n"
        << "stopped" << 99 << "/lib/libc.c";
    QTest::newRow("Signal: SIGTERM missing line") 
        << "*stopped,reason=\"signal-received\",signal-name=\"SIGTERM\",frame={addr=\"0xdef\",func=\"term\",file=\"sys.c\",fullname=\"/sys/sys.c\"}\n"
        << "stopped" << -1 << "/sys/sys.c";

    // Running states
    QTest::newRow("Running: all threads") 
        << "*running,thread-id=\"all\"\n"
        << "running" << 0 << "";
    QTest::newRow("Running: specific thread") 
        << "*running,thread-id=\"1\"\n"
        << "running" << 0 << "";

    // Thread creations and library loads (do not change stopped/running state directly, mock initializes to "running")
    QTest::newRow("Thread: created") 
        << "=thread-created,id=\"1\",group-id=\"i1\"\n"
        << "running" << 0 << "";
    QTest::newRow("Library: loaded") 
        << "=library-loaded,id=\"/lib/libc.so.6\",target-name=\"/lib/libc.so.6\",host-name=\"target:/lib/libc.so.6\",symbols-loaded=\"0\"\n"
        << "running" << 0 << "";
    QTest::newRow("Library: unloaded") 
        << "=library-unloaded,id=\"/lib/libm.so.6\",target-name=\"/lib/libm.so.6\",host-name=\"target:/lib/libm.so.6\"\n"
        << "running" << 0 << "";

    // Out-of-bounds variable evaluations
    QTest::newRow("Error: out of bounds") 
        << "300^error,msg=\"-var-create: unable to create variable object\"\n"
        << "running" << 0 << "";
    QTest::newRow("Error: syntax error") 
        << "^error,msg=\"Syntax error in expression, near `123'.\"\n"
        << "running" << 0 << "";
    QTest::newRow("Error: no symbol") 
        << "^error,msg=\"No symbol \\\"foo\\\" in current context.\"\n"
        << "running" << 0 << "";
}

void TestGdbCoordinator::testGdbMiParser() {
    QFETCH(QString, rawOutput);
    QFETCH(QString, expectedState);
    QFETCH(int, expectedLine);
    QFETCH(QString, expectedFile);

    gridlock::GdbRankCoordinator coord;
    // Use simulateInitialSync=false so mock sets lastFiredTimestamp="mocked" to avoid flushing breakpoints
    coord.initializeMockSession(1, false);
    
    coord.processGdbOutput(0, rawOutput);

    gridlock::RankState state = coord.getRankState(0);
    QCOMPARE(state.currentState, expectedState);
    QCOMPARE(state.currentLine, expectedLine);
    QCOMPARE(state.currentFile, expectedFile);
}

void TestGdbCoordinator::testMultiRankSync() {
    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(2, false);

    // Initial state check
    QCOMPARE(coord.getRankState(0).currentState, QString("running"));
    QCOMPARE(coord.getRankState(1).currentState, QString("running"));

    // Rank 0 hits a breakpoint
    coord.processGdbOutput(0, "*stopped,reason=\"breakpoint-hit\",frame={fullname=\"/app/rank0.c\",line=\"10\"}\n");
    // Rank 1 hits a breakpoint
    coord.processGdbOutput(1, "*stopped,reason=\"breakpoint-hit\",frame={fullname=\"/app/rank1.c\",line=\"20\"}\n");

    QCOMPARE(coord.getRankState(0).currentState, QString("stopped"));
    QCOMPARE(coord.getRankState(0).currentLine, 10);
    QCOMPARE(coord.getRankState(0).currentFile, QString("/app/rank0.c"));
    
    QCOMPARE(coord.getRankState(1).currentState, QString("stopped"));
    QCOMPARE(coord.getRankState(1).currentLine, 20);
    QCOMPARE(coord.getRankState(1).currentFile, QString("/app/rank1.c"));

    // Rank 0 starts running
    coord.processGdbOutput(0, "*running,thread-id=\"all\"\n");
    QCOMPARE(coord.getRankState(0).currentState, QString("running"));
    QCOMPARE(coord.getRankState(1).currentState, QString("stopped")); // Ensure Rank 1 remains unaffected
    
    // Rank 1 hits a segfault
    coord.processGdbOutput(1, "*stopped,reason=\"signal-received\",signal-name=\"SIGSEGV\",frame={fullname=\"/app/rank1_crash.c\",line=\"42\"}\n");
    QCOMPARE(coord.getRankState(0).currentState, QString("running"));
    QCOMPARE(coord.getRankState(1).currentState, QString("stopped"));
    QCOMPARE(coord.getRankState(1).currentLine, 42);
    QCOMPARE(coord.getRankState(1).currentFile, QString("/app/rank1_crash.c"));

    // Rank 0 exits
    coord.processGdbOutput(0, "*stopped,reason=\"exited-normally\"\n");
    QCOMPARE(coord.getRankState(0).currentState, QString("exited"));
    QCOMPARE(coord.getRankState(1).currentState, QString("stopped"));
}

void TestGdbCoordinator::testBreakpointCacheFlush() {
    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(1, true);

    // Fake an internal sync
    coord.processGdbOutput(0, "*stopped,reason=\"signal-received\"\n");

    // The coordinator flushes breakpoints because lastFiredTimestamp is empty.
    QCOMPARE(coord.getRankState(0).lastFiredTimestamp, QString("synced"));
}

void TestGdbCoordinator::testRecreateWatchVariable() {
    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(1);
    
    coord.registerWatchVariable("offset");
    
    coord.processGdbOutput(0, "300^error,msg=\"-var-create: unable to create variable object\"\n");
    QCOMPARE(coord.getRankState(0).variableWatches["offset"], QString("<Out of Scope>"));
    
    coord.processGdbOutput(0, "*stopped,reason=\"breakpoint-hit\",frame={fullname=\"mpi_mm.c\",line=\"79\"}\n");
    coord.processGdbOutput(0, "300^done,name=\"var1\",numchild=\"0\",value=\"123\",type=\"int\"\n");
    
    QCOMPARE(coord.getRankState(0).variableWatches["offset"], QString("123"));
}

void TestGdbCoordinator::cleanupTestCase() {
}

QTEST_MAIN(TestGdbCoordinator)
