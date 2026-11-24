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

#include <QStandardPaths>

#include "../../src/core/commands/DebugCommands.hpp"
#include "../../src/core/managers/ConfigManager.hpp"
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
 * @brief Verifies ROCm rocgdb MI3 pending-to-resolved kernel breakpoints, AMDGPU wavefront threads, SIMD lanes, and VGPR/PC registers.
 */
void TestGdbCoordinator::testRocgdbMockMiParsingAndPendingKernelBreakpoint() {
    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(1);

    QStringList sentCmds;
    QStringList progOut;
    QJsonArray lastRegs;
    QJsonArray lastLocals;
    QVector<ThreadEntry> lastThreads;
    QString createdFile;
    int createdLine = -1;
    int createdId = -1;

    connect(&coord, &gridlock::GdbRankCoordinator::commandSentToGdb, &coord,
            [&](int, const QString& cmd) { sentCmds.append(cmd); });
    connect(&coord, &gridlock::GdbRankCoordinator::programOutputReceived, &coord,
            [&](int, const QString& out, bool) { progOut.append(out); });
    connect(&coord, &gridlock::GdbRankCoordinator::registersUpdated, &coord,
            [&](int, const QJsonArray& regs) { lastRegs = regs; });
    connect(&coord, &gridlock::GdbRankCoordinator::localsUpdated, &coord,
            [&](int, const QJsonArray& locals) { lastLocals = locals; });
    connect(&coord, &gridlock::GdbRankCoordinator::threadsUpdated, &coord,
            [&](int, const QVector<ThreadEntry>& threads) { lastThreads = threads; });
    connect(&coord, &gridlock::GdbRankCoordinator::breakpointRecordCreated, &coord,
            [&](int, const QString& f, int ln, int id, bool) {
                createdFile = f;
                createdLine = ln;
                createdId = id;
            });

    // 1. Wrapper banner noise from /opt/rocm/bin/rocgdb must not pollute inferior stdout.
    coord.processGdbOutput(
        0,
        QStringLiteral("Not successful in running rocgdb with active Python Version: 3.14 in the system\n"
                       "Checking for working Python Version Installed\n"));
    QVERIFY(progOut.isEmpty());

    // 2. Pending device kernel breakpoint before HIP fat-binary code object loads on gfx1101.
    coord.processGdbOutput(
        0,
        QStringLiteral("^done,bkpt={number=\"1\",type=\"breakpoint\",disp=\"keep\",enabled=\"y\","
                       "addr=\"<PENDING>\",pending=\"/tmp/gridlock_rocm_probe.cpp:62\",times=\"0\","
                       "original-location=\"/tmp/gridlock_rocm_probe.cpp:62\"}\n"));
    QCOMPARE(createdId, 1);
    QCOMPARE(createdFile, QStringLiteral("/tmp/gridlock_rocm_probe.cpp"));
    QCOMPARE(createdLine, 62);
    QCOMPARE(coord.getRankState(0).breakpoints.value(1), QStringLiteral("/tmp/gridlock_rocm_probe.cpp:62"));

    // 3. HIP code-object load resolves the pending breakpoint via =breakpoint-modified.
    coord.processGdbOutput(
        0,
        QStringLiteral("=library-loaded,id=\"code_object_2\",target-name=\"code_object_2\","
                       "host-name=\"memory://3537494#offset=0x555555fae740&size=39720\",symbols-loaded=\"0\",thread-group=\"i1\"\n"
                       "=breakpoint-modified,bkpt={number=\"1\",type=\"breakpoint\",disp=\"keep\",enabled=\"y\","
                       "addr=\"0x00007ffff7db3428\",func=\"vectorScaleKernel(float const*, float*, float, int)\","
                       "file=\"/tmp/gridlock_rocm_probe.cpp\",fullname=\"/tmp/gridlock_rocm_probe.cpp\",line=\"62\","
                       "thread-groups=[\"i1\"],times=\"0\",original-location=\"/tmp/gridlock_rocm_probe.cpp:62\"}\n"));
    QCOMPARE(coord.getRankState(0).breakpoints.value(1), QStringLiteral("/tmp/gridlock_rocm_probe.cpp:62"));

    // 4. Hit breakpoint on AMD Radeon RX 7700 XT wavefront (thread-id="7", lane-id="0", arch="amdgcn:gfx1101").
    sentCmds.clear();
    coord.processGdbOutput(
        0,
        QStringLiteral("*stopped,reason=\"breakpoint-hit\",disp=\"keep\",bkptno=\"1\",hit-lanes=\"0-31\","
                       "frame={addr=\"0x00007ffff7db3428\",func=\"vectorScaleKernel\","
                       "args=[{name=\"in\",value=\"0x7ffee5c00000\"},{name=\"out\",value=\"0x7ffee5c01000\"},"
                       "{name=\"scale\",value=\"2.5\"},{name=\"n\",value=\"64\"}],"
                       "file=\"/tmp/gridlock_rocm_probe.cpp\",fullname=\"/tmp/gridlock_rocm_probe.cpp\","
                       "line=\"62\",arch=\"amdgcn:gfx1101\"},thread-id=\"7\",lane-id=\"0\",stopped-threads=\"all\"\n"));
    const gridlock::RankState stoppedSt = coord.getRankState(0);
    QCOMPARE(stoppedSt.currentState, QStringLiteral("stopped"));
    QCOMPARE(stoppedSt.currentFile, QStringLiteral("/tmp/gridlock_rocm_probe.cpp"));
    QCOMPARE(stoppedSt.currentLine, 62);
    QCOMPARE(stoppedSt.currentFunction, QStringLiteral("vectorScaleKernel"));
    QCOMPARE(coord.activeArchitectureForRank(0), QStringLiteral("amdgcn:gfx1101"));
    QCOMPARE(coord.activeLaneForRank(0), 0);
    QVERIFY(sentCmds.contains(QStringLiteral("214-data-list-register-names")));

    // 5. Parse AMDGPU register-names + vector/scalar register-values (v0, pc, exec, vcc).
    QStringList regNames(742, QString());
    regNames[256] = QStringLiteral("v0");
    regNames[734] = QStringLiteral("pc");
    regNames[739] = QStringLiteral("exec");
    regNames[741] = QStringLiteral("vcc");
    QStringList quotedNames;
    quotedNames.reserve(regNames.size());
    for (const QString& n : regNames) quotedNames.append(QStringLiteral("\"%1\"").arg(n));
    coord.processGdbOutput(0, QStringLiteral("214^done,register-names=[%1]\n").arg(quotedNames.join(QLatin1Char(','))));
    coord.processGdbOutput(
        0,
        QStringLiteral("^done,register-values=[{number=\"256\",value=\"{0x00000000, 0x00000001, 0x00000002, 0x00000003}\"},"
                       "{number=\"734\",value=\"0x00007ffff7db3428\"},{number=\"739\",value=\"0xffffffff\"},"
                       "{number=\"741\",value=\"0xffffffff\"}]\n"));
    QCOMPARE(lastRegs.size(), 4);
    QMap<QString, QString> regMap;
    for (const QJsonValue& v : lastRegs) {
        const QJsonObject o = v.toObject();
        regMap.insert(o.value(QStringLiteral("name")).toString(), o.value(QStringLiteral("value")).toString());
    }
    QVERIFY(regMap.value(QStringLiteral("v0")).startsWith(QStringLiteral("{0x00000000, 0x00000001")));
    QCOMPARE(regMap.value(QStringLiteral("pc")), QStringLiteral("0x00007ffff7db3428"));
    QCOMPARE(regMap.value(QStringLiteral("exec")), QStringLiteral("0xffffffff"));
    QCOMPARE(regMap.value(QStringLiteral("vcc")), QStringLiteral("0xffffffff"));
    QCOMPARE(coord.getRankState(0).registers.value(16), QStringLiteral("0x00007ffff7db3428"));

    // 6. Parse mixed CPU + AMDGPU Wavefront thread list (`AMDGPU Wave 1:1:1:1 (0,0,0)/0` and `(1,0,0)/0`).
    coord.processGdbOutput(
        0,
        QStringLiteral("203^done,threads=["
                       "{id=\"1\",target-id=\"Thread 0x7ffff7dcd8c0 (LWP 3537494)\",name=\"gridlock_rocm_p\","
                       "frame={level=\"0\",addr=\"0x00007ffff5a590cb\",func=\"BusyWaitSignal::WaitRelaxed\",args=[],"
                       "from=\"/lib64/libhsa-runtime64.so.1\",arch=\"i386:x86-64\"},state=\"stopped\",core=\"23\"},"
                       "{id=\"7\",target-id=\"AMDGPU Wave 1:1:1:1 (0,0,0)/0\",name=\"vectorScale-5c6e\","
                       "frame={level=\"0\",addr=\"0x00007ffff7db3428\",func=\"vectorScaleKernel\",args=[],"
                       "file=\"/tmp/gridlock_rocm_probe.cpp\",fullname=\"/tmp/gridlock_rocm_probe.cpp\",line=\"62\","
                       "arch=\"amdgcn:gfx1101\"},state=\"stopped\"},"
                       "{id=\"8\",target-id=\"AMDGPU Wave 1:1:1:2 (1,0,0)/0\",name=\"vectorScale-5c6e\","
                       "frame={level=\"0\",addr=\"0x00007ffff7db3428\",func=\"vectorScaleKernel\",args=[],"
                       "file=\"/tmp/gridlock_rocm_probe.cpp\",fullname=\"/tmp/gridlock_rocm_probe.cpp\",line=\"62\","
                       "arch=\"amdgcn:gfx1101\"},state=\"stopped\"}],current-thread-id=\"7\"\n"));
    QCOMPARE(lastThreads.size(), 3);
    QCOMPARE(lastThreads[0].name, QStringLiteral("gridlock_rocm_p"));
    QVERIFY(!lastThreads[0].isCurrent);
    QCOMPARE(lastThreads[1].name, QStringLiteral("AMDGPU Wave 1:1:1:1 (0,0,0)/0 [vectorScale-5c6e]"));
    QVERIFY(lastThreads[1].isCurrent);
    QCOMPARE(lastThreads[2].name, QStringLiteral("AMDGPU Wave 1:1:1:2 (1,0,0)/0 [vectorScale-5c6e]"));
    QVERIFY(!lastThreads[2].isCurrent);

    // 7. Parse kernel locals and async SIMD lane switch (`lane 5` -> `=thread-selected,id="7",lane-id="5"`).
    coord.processGdbOutput(
        0,
        QStringLiteral("201^done,variables=[{name=\"in\",arg=\"1\",type=\"const float *\",value=\"0x7ffee5c00000\"},"
                       "{name=\"out\",arg=\"1\",type=\"float *\",value=\"0x7ffee5c01000\"},"
                       "{name=\"tid\",type=\"int\",value=\"0\"},{name=\"gid\",type=\"int\",value=\"0\"}]\n"));
    QCOMPARE(lastLocals.size(), 4);

    sentCmds.clear();
    coord.processGdbOutput(
        0,
        QStringLiteral("=thread-selected,id=\"7\",lane-id=\"5\",frame={level=\"0\",addr=\"0x00007ffff7db3428\","
                       "func=\"vectorScaleKernel\",args=[],file=\"/tmp/gridlock_rocm_probe.cpp\","
                       "fullname=\"/tmp/gridlock_rocm_probe.cpp\",line=\"62\",arch=\"amdgcn:gfx1101\"}\n"));
    QCOMPARE(coord.activeLaneForRank(0), 5);
    QVERIFY(sentCmds.contains(QStringLiteral("201-stack-list-variables --simple-values")));
    QVERIFY(sentCmds.contains(QStringLiteral("-data-list-register-values r")));
}

/**
 * @brief Opt-in live HIP GPU kernel debugging test via GdbRankCoordinator + rocgdb (requires GRIDLOCK_ENABLE_GPU_TESTS=1).
 */
void TestGdbCoordinator::testLiveRocgdbHipKernelOnGpuIfAvailable() {
    if (qEnvironmentVariableIntValue("GRIDLOCK_ENABLE_GPU_TESTS") != 1) {
        QSKIP("Live GPU tests disabled by default; set GRIDLOCK_ENABLE_GPU_TESTS=1 to run.");
    }
    QFileInfo kfdInfo(QStringLiteral("/dev/kfd"));
    if (!kfdInfo.exists() || !kfdInfo.isReadable() || !kfdInfo.isWritable()) {
        QSKIP("Skipping live ROCm GPU test: /dev/kfd is not accessible on this host.");
    }
    const QString hipccBin = QStandardPaths::findExecutable(QStringLiteral("hipcc"));
    if (hipccBin.isEmpty()) {
        QSKIP("Skipping live ROCm GPU test: hipcc compiler is not installed.");
    }

    auto& cfg = gridlock::core::ConfigManager::instance();
    const QString origEngine = cfg.debuggerEngine();
    const auto origDs = cfg.getDebuggerSettings();
    auto origPs = cfg.loadProjectSettings();
    struct RestoreGuard {
        gridlock::core::ConfigManager* c;
        QString eng;
        gridlock::core::DebuggerSettings ds;
        gridlock::core::ProjectSettings ps;
        ~RestoreGuard() {
            c->setDebuggerEngine(eng);
            c->saveDebuggerSettings(ds);
            c->saveProjectSettings(ps);
        }
    } guard{&cfg, origEngine, origDs, origPs};

    cfg.setDebuggerEngine(QStringLiteral("rocgdb"));
    auto testDs = origDs;
    testDs.gdbPath = QStringLiteral("gdb");
    cfg.saveDebuggerSettings(testDs);
    auto testPs = origPs;
    testPs.customGdbPath = "gdb";
    cfg.saveProjectSettings(testPs);

    gridlock::GdbRankCoordinator coord;
    QString launchErr;
    if (!coord.validateLaunchEnvironment(1, &launchErr)) {
        QSKIP(qPrintable(QStringLiteral("Skipping live ROCm GPU test: %1").arg(launchErr)));
    }

    QString gpuArch;
    for (const QString& archTool : {QStandardPaths::findExecutable(QStringLiteral("amdgpu-arch")),
                                    QStringLiteral("/usr/lib64/rocm/llvm/bin/amdgpu-arch"),
                                    QStringLiteral("/opt/rocm/llvm/bin/amdgpu-arch")}) {
        if (archTool.isEmpty() || !QFileInfo(archTool).isExecutable()) continue;
        QProcess ap;
        ap.start(archTool, {});
        if (ap.waitForFinished(3000) && ap.exitCode() == 0) {
            const QString line = QString::fromUtf8(ap.readAllStandardOutput()).trimmed().section(QLatin1Char('\n'), 0, 0).trimmed();
            if (line.startsWith(QStringLiteral("gfx"))) { gpuArch = line; break; }
        }
    }
    if (gpuArch.isEmpty()) {
        QSKIP("Skipping live ROCm GPU test: no active AMDGPU gfx target reported by amdgpu-arch.");
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString srcPath = tempDir.filePath(QStringLiteral("rocm_kernel_probe.cpp"));
    const QString binPath = tempDir.filePath(QStringLiteral("rocm_kernel_probe"));

    static const char* const kHipSource = R"HIP(
#include <cstddef>
#include <cstdint>
#include <cstdio>

#if __has_include(<hip/hip_runtime.h>)
#include <hip/hip_runtime.h>
#else
extern "C" __device__ size_t __ockl_get_local_id(uint32_t);
extern "C" __device__ size_t __ockl_get_group_id(uint32_t);
extern "C" __device__ size_t __ockl_get_local_size(uint32_t);
struct dim3 {
  uint32_t x, y, z;
  __host__ __device__ constexpr dim3(uint32_t vx = 1, uint32_t vy = 1, uint32_t vz = 1) : x(vx), y(vy), z(vz) {}
};
struct HipThreadIdx { static inline __attribute__((always_inline)) __device__ uint32_t getX() { return static_cast<uint32_t>(__ockl_get_local_id(0)); } __declspec(property(get = getX)) uint32_t x; };
struct HipBlockIdx  { static inline __attribute__((always_inline)) __device__ uint32_t getX() { return static_cast<uint32_t>(__ockl_get_group_id(0)); } __declspec(property(get = getX)) uint32_t x; };
struct HipBlockDim  { static inline __attribute__((always_inline)) __device__ uint32_t getX() { return static_cast<uint32_t>(__ockl_get_local_size(0)); } __declspec(property(get = getX)) uint32_t x; };
extern const __attribute__((device)) __attribute__((weak)) HipThreadIdx threadIdx;
extern const __attribute__((device)) __attribute__((weak)) HipBlockIdx blockIdx;
extern const __attribute__((device)) __attribute__((weak)) HipBlockDim blockDim;
enum hipError_t : int { hipSuccess = 0 };
enum hipMemcpyKind : int { hipMemcpyHostToDevice = 1, hipMemcpyDeviceToHost = 2 };
typedef struct ihipStream_t* hipStream_t;
extern "C" {
hipError_t hipMalloc(void** ptr, size_t size);
hipError_t hipFree(void* ptr);
hipError_t hipMemcpy(void* dst, const void* src, size_t sizeBytes, hipMemcpyKind kind);
hipError_t hipDeviceSynchronize(void);
hipError_t hipLaunchKernel(const void* fn, dim3 grid, dim3 block, void** args, size_t shmem, hipStream_t stream);
int __hipPushCallConfiguration(dim3 grid, dim3 block, size_t shmem = 0, hipStream_t stream = nullptr);
hipError_t __hipPopCallConfiguration(dim3* grid, dim3* block, size_t* shmem, hipStream_t* stream);
}
#endif

__global__ void vectorScaleKernel(const float* in, float* out, float scale, int n) {
  int tid = static_cast<int>(threadIdx.x);
  int bid = static_cast<int>(blockIdx.x);
  int gid = bid * static_cast<int>(blockDim.x) + tid;
  if (gid < n) {
    float val = in[gid] * scale + static_cast<float>(tid); // KERNEL_BP_LINE
    out[gid] = val;
  }
}

int main() {
  constexpr int kCount = 64;
  float hostIn[kCount], hostOut[kCount];
  for (int i = 0; i < kCount; ++i) { hostIn[i] = static_cast<float>(i + 1); hostOut[i] = 0.0f; }
  float *devIn = nullptr, *devOut = nullptr;
  if (hipMalloc(reinterpret_cast<void**>(&devIn), sizeof(hostIn)) != hipSuccess) return 2;
  if (hipMalloc(reinterpret_cast<void**>(&devOut), sizeof(hostOut)) != hipSuccess) return 3;
  if (hipMemcpy(devIn, hostIn, sizeof(hostIn), hipMemcpyHostToDevice) != hipSuccess) return 4;
  vectorScaleKernel<<<dim3(2), dim3(32)>>>(devIn, devOut, 2.5f, kCount);
  if (hipDeviceSynchronize() != hipSuccess) return 5;
  if (hipMemcpy(hostOut, devOut, sizeof(hostOut), hipMemcpyDeviceToHost) != hipSuccess) return 6;
  std::printf("GPU_OK out[0]=%.1f out[5]=%.1f out[63]=%.1f\n", hostOut[0], hostOut[5], hostOut[63]);
  std::fflush(stdout);
  hipFree(devIn);
  hipFree(devOut);
  return (hostOut[0] == 2.5f && hostOut[5] == 20.0f) ? 0 : 1;
}
)HIP";

    {
        QFile sf(srcPath);
        QVERIFY(sf.open(QIODevice::WriteOnly));
        sf.write(kHipSource);
    }
    int kernelBpLine = 0;
    {
        QFile sf(srcPath);
        QVERIFY(sf.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(sf.readAll()).split(QLatin1Char('\n'));
        for (int i = 0; i < lines.size(); ++i) {
            if (lines[i].contains(QStringLiteral("KERNEL_BP_LINE"))) { kernelBpLine = i + 1; break; }
        }
    }
    QVERIFY(kernelBpLine > 0);

    QStringList compileArgs{QStringLiteral("-g"), QStringLiteral("-O0"), QStringLiteral("--offload-arch=") + gpuArch};
    if (!QFileInfo::exists(QStringLiteral("/usr/include/hip/hip_runtime.h")) &&
        !QFileInfo::exists(QStringLiteral("/opt/rocm/include/hip/hip_runtime.h"))) {
        QString hipSo;
        for (const QString& cand : {QStringLiteral("/usr/lib64/libamdhip64.so"), QStringLiteral("/usr/lib64/libamdhip64.so.7"),
                                    QStringLiteral("/opt/rocm/lib/libamdhip64.so")}) {
            if (QFileInfo::exists(cand)) { hipSo = cand; break; }
        }
        if (hipSo.isEmpty()) QSKIP("Skipping live ROCm GPU test: libamdhip64.so not found.");
        compileArgs << QStringLiteral("-no-hip-rt") << (QStringLiteral("-Wl,") + hipSo);
    }
    compileArgs << srcPath << QStringLiteral("-o") << binPath;

    QProcess cc;
    cc.start(hipccBin, compileArgs);
    if (!cc.waitForFinished(20000) || cc.exitCode() != 0 || !QFileInfo::exists(binPath)) {
        QSKIP(qPrintable(QStringLiteral("Skipping live ROCm GPU test: hipcc build skipped (%1)")
                             .arg(QString::fromUtf8(cc.readAllStandardError()).trimmed())));
    }

    QJsonArray capturedLocals;
    QJsonArray capturedRegs;
    QVector<ThreadEntry> capturedThreads;
    QString evalResult;
    QString memHex;
    QString combinedStdout;

    connect(&coord, &gridlock::GdbRankCoordinator::localsUpdated, &coord,
            [&](int, const QJsonArray& loc) { if (!loc.isEmpty()) capturedLocals = loc; });
    connect(&coord, &gridlock::GdbRankCoordinator::registersUpdated, &coord,
            [&](int, const QJsonArray& regs) { if (!regs.isEmpty()) capturedRegs = regs; });
    connect(&coord, &gridlock::GdbRankCoordinator::threadsUpdated, &coord,
            [&](int, const QVector<ThreadEntry>& th) { if (!th.isEmpty()) capturedThreads = th; });
    connect(&coord, &gridlock::GdbRankCoordinator::expressionEvaluated, &coord,
            [&](int, const QString&, const QString& val) { evalResult = val; });
    connect(&coord, &gridlock::GdbRankCoordinator::memoryDataReady, &coord,
            [&](int, qint64, const QString& hex) { memHex = hex; });
    connect(&coord, &gridlock::GdbRankCoordinator::programOutputReceived, &coord,
            [&](int, const QString& out, bool) { combinedStdout += out + QLatin1Char('\n'); });

    coord.setBreakpoint(srcPath, kernelBpLine);
    coord.launchParallelSession(binPath, 1, QString());

    QTRY_COMPARE_WITH_TIMEOUT(coord.getRankState(0).currentState, QStringLiteral("stopped"), 15000);
    QCOMPARE(coord.getRankState(0).currentLine, kernelBpLine);
    QCOMPARE(coord.getRankState(0).currentFunction, QStringLiteral("vectorScaleKernel"));
    QVERIFY(coord.activeArchitectureForRank(0).startsWith(QStringLiteral("amdgcn:")));
    QCOMPARE(coord.activeLaneForRank(0), 0);

    QTRY_VERIFY_WITH_TIMEOUT(!capturedLocals.isEmpty() && !capturedRegs.isEmpty() && !capturedThreads.isEmpty(), 5000);

    bool sawGpuWaveThread = false;
    for (const auto& th : capturedThreads) {
        if (th.name.startsWith(QStringLiteral("AMDGPU Wave "))) { sawGpuWaveThread = true; break; }
    }
    QVERIFY2(sawGpuWaveThread, "Expected AMDGPU Wave thread entry in threadsUpdated");

    QString devInPtr;
    for (const QJsonValue& v : capturedLocals) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("name")).toString() == QStringLiteral("in")) {
            devInPtr = o.value(QStringLiteral("value")).toString();
        }
    }
    QVERIFY2(devInPtr.startsWith(QStringLiteral("0x")), "Expected device pointer `in` in kernel locals");

    coord.readMemory(0, devInPtr, 8);
    QTRY_VERIFY_WITH_TIMEOUT(!memHex.isEmpty(), 4000);
    // hostIn[0] = 1.0f (0000803f LE), hostIn[1] = 2.0f (00000040 LE).
    QCOMPARE(memHex.left(16).toLower(), QStringLiteral("0000803f00000040"));

    coord.evaluateExpression(0, QStringLiteral("scale"));
    QTRY_COMPARE_WITH_TIMEOUT(evalResult, QStringLiteral("2.5"), 4000);

    // Switch to SIMD lane 5 via debugger console and verify tid updates to 5.
    evalResult.clear();
    coord.sendDebuggerCommand(0, QStringLiteral("lane 5"));
    QTRY_COMPARE_WITH_TIMEOUT(coord.activeLaneForRank(0), 5, 4000);
    coord.evaluateExpression(0, QStringLiteral("tid"));
    QTRY_COMPARE_WITH_TIMEOUT(evalResult, QStringLiteral("5"), 4000);

    coord.clearBreakpoints();
    coord.continueAll();
    QTRY_COMPARE_WITH_TIMEOUT(coord.getRankState(0).currentState, QStringLiteral("exited"), 10000);
    QVERIFY2(combinedStdout.contains(QStringLiteral("GPU_OK out[0]=2.5 out[5]=20.0 out[63]=191.0")),
             qPrintable(combinedStdout));
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
