/**
 * @file test_coordinator.cpp
 * @brief Implementation of GdbRankCoordinator unit tests.
 *
 * Validates GDB/MI output processing, breakpoint tracking across MPI ranks,
 * process crash signal reporting, and debugger register value extraction.
 */

#include "test_coordinator.hpp"

/**
 * @brief Global test setup hook.
 */
void TestGdbCoordinator::initTestCase() {
}

/**
 * @brief Verifies rank state initialization when zero processes are configured.
 */
void TestGdbCoordinator::testInitialRankStates() {
    gridlock::GdbRankCoordinator coordinator;

    // A fresh coordinator has no managed debuggee processes.
    QCOMPARE(coordinator.getProcessCount(), 0);

    // Any queried rank should report offline status by default.
    gridlock::RankState state = coordinator.getRankState(0);
    QCOMPARE(state.currentState, QString("offline"));
}

/**
 * @brief Generates test data matrix covering varied GDB/MI async records and stops.
 */
void TestGdbCoordinator::testGdbMiParser_data() {
    QTest::addColumn<QString>("rawOutput");
    QTest::addColumn<QString>("expectedState");
    QTest::addColumn<int>("expectedLine");
    QTest::addColumn<QString>("expectedFile");

    // Test standard breakpoint hits with source file location info.
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

    // Test process exit notifications (normal and non-zero exit codes).
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

    // Test fatal signals and exceptions (SIGSEGV, SIGINT, SIGFPE, SIGABRT).
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

    // Test running state updates.
    QTest::newRow("Running: all threads") 
        << "*running,thread-id=\"all\"\n"
        << "running" << 0 << "";
    QTest::newRow("Running: specific thread") 
        << "*running,thread-id=\"1\"\n"
        << "running" << 0 << "";

    // Thread creations and library loads (mock initializes to "running").
    QTest::newRow("Thread: created") 
        << "=thread-created,id=\"1\",group-id=\"i1\"\n"
        << "running" << 0 << "";
    QTest::newRow("Library: loaded") 
        << "=library-loaded,id=\"/lib/libc.so.6\",target-name=\"/lib/libc.so.6\",host-name=\"target:/lib/libc.so.6\",symbols-loaded=\"0\"\n"
        << "running" << 0 << "";
    QTest::newRow("Library: unloaded") 
        << "=library-unloaded,id=\"/lib/libm.so.6\",target-name=\"/lib/libm.so.6\",host-name=\"target:/lib/libm.so.6\"\n"
        << "running" << 0 << "";

    // Out-of-bounds variable evaluations and error results.
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

/**
 * @brief Executes GDB/MI response parsing against all configured test cases.
 */
void TestGdbCoordinator::testGdbMiParser() {
    QFETCH(QString, rawOutput);
    QFETCH(QString, expectedState);
    QFETCH(int, expectedLine);
    QFETCH(QString, expectedFile);

    gridlock::GdbRankCoordinator coord;
    // Initialize mock session for single rank without auto breakpoint flush.
    coord.initializeMockSession(1, false);

    // Feed raw GDB/MI output into coordinator rank 0.
    coord.processGdbOutput(0, rawOutput);

    // Verify parsed state matches expected outputs.
    gridlock::RankState state = coord.getRankState(0);
    QCOMPARE(state.currentState, expectedState);
    QCOMPARE(state.currentLine, expectedLine);
    QCOMPARE(state.currentFile, expectedFile);
}

/**
 * @brief Tests concurrent rank lifecycle state transitions and isolation.
 */
void TestGdbCoordinator::testMultiRankSync() {
    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(2, false);

    // Initial state check - both ranks should start running.
    QCOMPARE(coord.getRankState(0).currentState, QString("running"));
    QCOMPARE(coord.getRankState(1).currentState, QString("running"));

    // Rank 0 hits a breakpoint at line 10.
    coord.processGdbOutput(0, "*stopped,reason=\"breakpoint-hit\",frame={fullname=\"/app/rank0.c\",line=\"10\"}\n");
    // Rank 1 hits a breakpoint at line 20.
    coord.processGdbOutput(1, "*stopped,reason=\"breakpoint-hit\",frame={fullname=\"/app/rank1.c\",line=\"20\"}\n");

    // Verify Rank 0 stopped status and location.
    QCOMPARE(coord.getRankState(0).currentState, QString("stopped"));
    QCOMPARE(coord.getRankState(0).currentLine, 10);
    QCOMPARE(coord.getRankState(0).currentFile, QString("/app/rank0.c"));

    // Verify Rank 1 stopped status and location.
    QCOMPARE(coord.getRankState(1).currentState, QString("stopped"));
    QCOMPARE(coord.getRankState(1).currentLine, 20);
    QCOMPARE(coord.getRankState(1).currentFile, QString("/app/rank1.c"));

    // Resume Rank 0; Rank 1 must remain stopped.
    coord.processGdbOutput(0, "*running,thread-id=\"all\"\n");
    QCOMPARE(coord.getRankState(0).currentState, QString("running"));
    QCOMPARE(coord.getRankState(1).currentState, QString("stopped"));

    // Rank 1 hits a segmentation fault.
    coord.processGdbOutput(1, "*stopped,reason=\"signal-received\",signal-name=\"SIGSEGV\",frame={fullname=\"/app/rank1_crash.c\",line=\"42\"}\n");
    QCOMPARE(coord.getRankState(0).currentState, QString("running"));
    QCOMPARE(coord.getRankState(1).currentState, QString("stopped"));
    QCOMPARE(coord.getRankState(1).currentLine, 42);
    QCOMPARE(coord.getRankState(1).currentFile, QString("/app/rank1_crash.c"));

    // Rank 0 exits cleanly.
    coord.processGdbOutput(0, "*stopped,reason=\"exited-normally\"\n");
    QCOMPARE(coord.getRankState(0).currentState, QString("exited"));
    QCOMPARE(coord.getRankState(1).currentState, QString("stopped"));
}

/**
 * @brief Tests breakpoint cache flushing behavior during initial target synchronization.
 */
void TestGdbCoordinator::testBreakpointCacheFlush() {
    gridlock::GdbRankCoordinator coord;
    // Pass simulateInitialSync=true to trigger automatic cache synchronization.
    coord.initializeMockSession(1, true);

    // Simulate an internal stop notification to trigger synchronization.
    coord.processGdbOutput(0, "*stopped,reason=\"signal-received\"\n");

    // Coordinator marks state synced once initial breakpoints are flushed.
    QCOMPARE(coord.getRankState(0).lastFiredTimestamp, QString("synced"));
}

/**
 * @brief Tests watch variable creation, out-of-scope errors, and value updates.
 */
void TestGdbCoordinator::testRecreateWatchVariable() {
    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(1);

    // Register a variable name for inspection.
    coord.registerWatchVariable("offset");

    // Simulate GDB failing to find the variable in current scope.
    coord.processGdbOutput(0, "300^error,msg=\"-var-create: unable to create variable object\"\n");
    QCOMPARE(coord.getRankState(0).variableWatches["offset"], QString("<Out of Scope>"));

    // Simulate target hitting a breakpoint where variable is in scope.
    coord.processGdbOutput(0, "*stopped,reason=\"breakpoint-hit\",frame={fullname=\"mpi_mm.c\",line=\"79\"}\n");
    coord.processGdbOutput(0, "300^done,name=\"var1\",numchild=\"0\",value=\"123\",type=\"int\"\n");

    // Value should now be updated to the parsed result.
    QCOMPARE(coord.getRankState(0).variableWatches["offset"], QString("123"));
}

/**
 * @brief Tests parsing of GDB/MI register dump lists into internal rank state.
 */
void TestGdbCoordinator::testRegisterValuesParsing() {
    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(1, false);

    // Simulate register dump response from GDB.
    QString rawOutput = "^done,register-values=[{number=\"0\",value=\"0x0000000000000000\"},{number=\"1\",value=\"0x12345678\"}]\n";
    coord.processGdbOutput(0, rawOutput);

    // Verify registers map contains parsed register values.
    gridlock::RankState state = coord.getRankState(0);
    QCOMPARE(state.registers.size(), 2);
    QCOMPARE(state.registers[0], QString("0x0000000000000000"));
    QCOMPARE(state.registers[1], QString("0x12345678"));
}

#include "../../src/core/commands/DebugCommands.hpp"
#include "../../src/ui/MainWindow.hpp"

namespace {
class MockCommandCoordinator : public gridlock::GdbRankCoordinator {
public:
    MockCommandCoordinator() : gridlock::GdbRankCoordinator(nullptr) {}
    bool stepCalled = false;
    bool pauseCalled = false;
    int pauseRankId = -1;
    void stepAll() override { stepCalled = true; }
    void pauseFocusedRank(int rankId) override {
        pauseCalled = true;
        pauseRankId = rankId;
    }
};
}  // namespace

/**
 * @brief Tests StepCommand execution routing to coordinator stepAll().
 */
void TestGdbCoordinator::testStepCommand() {
    MockCommandCoordinator mock;
    gridlock::core::commands::StepCommand cmd(&mock, 1);
    cmd.execute();
    QVERIFY(mock.stepCalled);
}

/**
 * @brief Tests PauseCommand execution converting 1-based rank (43) to 0-based rank (42).
 */
void TestGdbCoordinator::testPauseCommand() {
    MockCommandCoordinator mock;
    gridlock::core::commands::PauseCommand cmd(&mock, 43);
    cmd.execute();
    QVERIFY(mock.pauseCalled);
    QCOMPARE(mock.pauseRankId, 42);
}

/**
 * @brief Verifies ProcessScanner synthetic /proc parsing, MPI job grouping, ptrace_scope, and live child attach.
 */
void TestGdbCoordinator::testProcessScannerAndCoordinatorAttach() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString procRoot = tempDir.filePath(QStringLiteral("proc"));
    const uint uid = static_cast<uint>(::geteuid());

    auto writeProcEntry = [&](int pid, int ppid, const QString& comm, const QByteArray& cmdline,
                              const QString& exePath, const QByteArray& environBytes) {
        const QString dirPath = QStringLiteral("%1/%2").arg(procRoot).arg(pid);
        QVERIFY(QDir().mkpath(dirPath));
        { QFile f(dirPath + "/comm"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(comm.toUtf8() + "\n"); }
        { QFile f(dirPath + "/status"); QVERIFY(f.open(QIODevice::WriteOnly));
          f.write(QStringLiteral("Name:\t%1\nPPid:\t%2\nUid:\t%3\t%3\t%3\t%3\n").arg(comm).arg(ppid).arg(uid).toUtf8()); }
        { QFile f(dirPath + "/cmdline"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(cmdline); }
        { QFile f(dirPath + "/exe"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(exePath.toUtf8()); }
        { QFile f(dirPath + "/environ"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(environBytes); }
    };

    // 1. Two OpenMPI ranks (inserted out-of-order: rank 1 on PID 4102, rank 0 on PID 4101).
    const QByteArray ringCmd = QByteArrayList{"/opt/gridlock/deadlock_ring", "--steps", "100"}.join('\0');
    writeProcEntry(4102, 4000, QStringLiteral("deadlock_ring"), ringCmd,
                   QStringLiteral("/opt/gridlock/deadlock_ring"),
                   QByteArrayList{"OMPI_COMM_WORLD_RANK=1", "OMPI_COMM_WORLD_SIZE=2", "OMPI_MCA_ess_base_jobid=job99"}.join('\0'));
    writeProcEntry(4101, 4000, QStringLiteral("deadlock_ring"), ringCmd,
                   QStringLiteral("/opt/gridlock/deadlock_ring"),
                   QByteArrayList{"OMPI_COMM_WORLD_RANK=0", "OMPI_COMM_WORLD_SIZE=2", "OMPI_MCA_ess_base_jobid=job99"}.join('\0'));

    // 2. One MPICH rank using PMI_RANK / PMI_SIZE.
    writeProcEntry(5200, 5100, QStringLiteral("inspection_demo"),
                   QByteArray("/opt/gridlock/inspection_demo"),
                   QStringLiteral("/opt/gridlock/inspection_demo"),
                   QByteArrayList{"PMI_RANK=2", "PMI_SIZE=4", "PMI_JOBID=mpich7"}.join('\0'));

    // 3. One standalone non-MPI process.
    writeProcEntry(6300, 1, QStringLiteral("solver_serial"),
                   QByteArrayList{"/usr/local/bin/solver_serial", "--mesh", "grid.msh"}.join('\0'),
                   QStringLiteral("/usr/local/bin/solver_serial"),
                   QByteArray("PATH=/usr/bin"));

    const QList<gridlock::ProcessEntry> entries =
        gridlock::ProcessScanner::scanRunningProcesses(procRoot, true);
    QCOMPARE(entries.size(), 4);

    // OpenMPI ranks 0 and 1 must be grouped under the same non-empty mpiJobTag and ordered by rank.
    QCOMPARE(entries[0].pid, static_cast<pid_t>(4101));
    QCOMPARE(entries[0].mpiRank, 0);
    QCOMPARE(entries[0].mpiWorldSize, 2);
    QCOMPARE(entries[1].pid, static_cast<pid_t>(4102));
    QCOMPARE(entries[1].mpiRank, 1);
    QCOMPARE(entries[1].mpiWorldSize, 2);
    QVERIFY(!entries[0].mpiJobTag.isEmpty());
    QCOMPARE(entries[0].mpiJobTag, entries[1].mpiJobTag);
    QCOMPARE(entries[0].cmdline, QStringLiteral("/opt/gridlock/deadlock_ring --steps 100"));

    // MPICH rank and standalone process verification.
    QCOMPARE(entries[2].pid, static_cast<pid_t>(5200));
    QCOMPARE(entries[2].mpiRank, 2);
    QCOMPARE(entries[2].mpiWorldSize, 4);
    QVERIFY(entries[2].isMpiProcess());
    QCOMPARE(entries[3].pid, static_cast<pid_t>(6300));
    QVERIFY(!entries[3].isMpiProcess());

    // 4. Verify checkPtraceScope against synthetic sysctl files.
    const QString sysctlFile = tempDir.filePath(QStringLiteral("ptrace_scope"));
    QCOMPARE(gridlock::ProcessScanner::checkPtraceScope(sysctlFile), 0);
    { QFile f(sysctlFile); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("1\n"); }
    QCOMPARE(gridlock::ProcessScanner::checkPtraceScope(sysctlFile), 1);
    { QFile f(sysctlFile); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("2\n"); }
    QCOMPARE(gridlock::ProcessScanner::checkPtraceScope(sysctlFile), 2);

    // 5. Attach GdbRankCoordinator to a real direct-child `sleep` process.
    QProcess childProc;
    childProc.start(QStringLiteral("sleep"), {QStringLiteral("30")});
    QVERIFY(childProc.waitForStarted(3000));
    const qint64 childPid = childProc.processId();
    QVERIFY(childPid > 0);

    gridlock::ProcessEntry liveTarget;
    liveTarget.pid = static_cast<pid_t>(childPid);
    liveTarget.mpiRank = 0;
    liveTarget.mpiWorldSize = 1;

    gridlock::GdbRankCoordinator coord;
    QVERIFY(coord.attachToProcesses({liveTarget}));
    QCOMPARE(coord.getPidForRank(0), childPid);
    coord.terminateSession();

    childProc.kill();
    childProc.waitForFinished(1000);

    // 6. Verify ProcessScanner::expandRemoteEndpoints() for single, :port, range, comma list, and invalid ports.
    const auto singleSpec = gridlock::ProcessScanner::expandRemoteEndpoints(
        QStringLiteral("127.0.0.1:2345"), QStringLiteral("/tmp/guest.elf"),
        QStringLiteral("gdb-multiarch"), true);
    QCOMPARE(singleSpec.size(), 1);
    QCOMPARE(singleSpec[0].endpoint, QStringLiteral("127.0.0.1:2345"));
    QCOMPARE(singleSpec[0].localSymbolBinary, QStringLiteral("/tmp/guest.elf"));
    QCOMPARE(singleSpec[0].gdbBinary, QStringLiteral("gdb-multiarch"));
    QVERIFY(singleSpec[0].extendedRemote);
    QCOMPARE(singleSpec[0].mpiRank, 0);

    const auto shortSpec = gridlock::ProcessScanner::expandRemoteEndpoints(QStringLiteral(":1234"));
    QCOMPARE(shortSpec.size(), 1);
    QCOMPARE(shortSpec[0].endpoint, QStringLiteral("localhost:1234"));

    const auto rangeSpec = gridlock::ProcessScanner::expandRemoteEndpoints(QStringLiteral("localhost:1234..1237"));
    QCOMPARE(rangeSpec.size(), 4);
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(rangeSpec[i].endpoint, QStringLiteral("localhost:%1").arg(1234 + i));
        QCOMPARE(rangeSpec[i].mpiRank, i);
    }

    const auto dashRangeSpec = gridlock::ProcessScanner::expandRemoteEndpoints(QStringLiteral("10.0.0.5:4000-4001"));
    QCOMPARE(dashRangeSpec.size(), 2);
    QCOMPARE(dashRangeSpec[0].endpoint, QStringLiteral("10.0.0.5:4000"));
    QCOMPARE(dashRangeSpec[1].endpoint, QStringLiteral("10.0.0.5:4001"));

    const auto commaSpec = gridlock::ProcessScanner::expandRemoteEndpoints(QStringLiteral("node0:1234, node1:1235"));
    QCOMPARE(commaSpec.size(), 2);
    QCOMPARE(commaSpec[0].endpoint, QStringLiteral("node0:1234"));
    QCOMPARE(commaSpec[0].mpiRank, 0);
    QCOMPARE(commaSpec[1].endpoint, QStringLiteral("node1:1235"));
    QCOMPARE(commaSpec[1].mpiRank, 1);

    QVERIFY(gridlock::ProcessScanner::expandRemoteEndpoints(QString()).isEmpty());
    QVERIFY(gridlock::ProcessScanner::expandRemoteEndpoints(QStringLiteral("localhost")).isEmpty());
    QVERIFY(gridlock::ProcessScanner::expandRemoteEndpoints(QStringLiteral("localhost:0")).isEmpty());
    QVERIFY(gridlock::ProcessScanner::expandRemoteEndpoints(QStringLiteral("localhost:70000")).isEmpty());
    QVERIFY(gridlock::ProcessScanner::expandRemoteEndpoints(QStringLiteral("localhost:1237..1234")).isEmpty());
    QVERIFY(gridlock::ProcessScanner::expandRemoteEndpoints(QStringLiteral("node0:1234,")).isEmpty());

    // 7. Verify GdbRankCoordinator::attachToRemoteTargets() issues -file-exec-and-symbols and -target-select remote/extended-remote.
    QStringList sentCmds;
    connect(&coord, &gridlock::GdbRankCoordinator::commandSentToGdb,
            &coord, [&](int, const QString& cmd) { sentCmds.append(cmd); });
    QVERIFY(coord.attachToRemoteTargets(singleSpec));
    QCOMPARE(coord.getCurrentBinaryPath(), QStringLiteral("/tmp/guest.elf"));
    QVERIFY(sentCmds.contains(QStringLiteral("-file-exec-and-symbols \"/tmp/guest.elf\"")));
    QVERIFY(sentCmds.contains(QStringLiteral("-target-select extended-remote 127.0.0.1:2345")));
    coord.processGdbOutput(
        0, QStringLiteral("^connected,frame={level=\"0\",addr=\"0x00401120\",func=\"main\",fullname=\"/tmp/guest.c\",line=\"15\"}\n"));
    const gridlock::RankState remState = coord.getRankState(0);
    QCOMPARE(remState.currentState, QStringLiteral("stopped"));
    QCOMPARE(remState.currentFunction, QStringLiteral("main"));
    QCOMPARE(remState.currentLine, 15);
    coord.terminateSession();
}

/**
 * @brief Global test teardown hook.
 */
void TestGdbCoordinator::cleanupTestCase() {
}

QTEST_MAIN(TestGdbCoordinator)

namespace gridlock::ui {
void MainWindow::startDebuggingSession(const QString& binaryPath, int ranks) {
    Q_UNUSED(binaryPath);
    Q_UNUSED(ranks);
}
}  // namespace gridlock::ui
