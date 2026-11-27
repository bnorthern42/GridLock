/**
 * @file test_config.cpp
 * @brief Implementation of configuration management and persistence test cases.
 *
 * Exercises TOML serialization, deserialization, invalid syntax recovery,
 * validation boundary checks, SLURM templates, and terminal preferences.
 */

#include "test_config.hpp"
#include "../../src/core/hpc/system/LocalSlurmBackend.hpp"
#include "../../src/core/managers/ConfigManager.hpp"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>
#include <fstream>

/**
 * @brief Verifies serializing breakpoint sets into `.gridlock/workspace.toml`.
 */
void TestConfig::testSerialization() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString oldCwd = QDir::currentPath();
    QDir::setCurrent(tempDir.path());

    auto& manager = gridlock::core::ConfigManager::instance();
    manager.setWorkspace(QDir::currentPath());
    manager.loadConfig(); // Loads defaults into the new dir

    // Prepare test breakpoint mappings across multiple source files.
    QMap<QString, QSet<int>> breakpoints;
    breakpoints["/app/src/main.cpp"] = {10, 15, 20};
    breakpoints["/app/src/utils.cpp"] = {5, 10};
    breakpoints["/app/src/solver.cpp"] = {100};

    // Serialize breakpoints to disk.
    manager.saveBreakpoints(breakpoints);

    // Verify output TOML file contents.
    QFile file(".gridlock/workspace.toml");
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QString content = file.readAll();
    file.close();

    // Assert that all serialized source file keys are present in file.
    QVERIFY(content.contains("/app/src/main.cpp"));
    QVERIFY(content.contains("/app/src/utils.cpp"));
    QVERIFY(content.contains("/app/src/solver.cpp"));

    // Verify programArgs are strictly scoped per target binary and workspace (never leaking globally via QSettings).
    const QString appA = tempDir.filePath(QStringLiteral("app_a"));
    const QString appB = tempDir.filePath(QStringLiteral("app_b"));
    manager.setProgramArgs(appA, QStringLiteral("--mesh coarse.msh --steps 10"));
    QCOMPARE(manager.programArgs(appA), QStringLiteral("--mesh coarse.msh --steps 10"));
    QVERIFY2(manager.programArgs(appB).isEmpty(), "programArgs configured for app_a must never leak to app_b");

    // Verify global QSettings does not store project/targetArguments and switching to a clean workspace starts empty.
    QSettings globalSettings(QStringLiteral("gridlock"), QStringLiteral("debugger"));
    QVERIFY2(!globalSettings.contains(QStringLiteral("project/targetArguments")),
             "programArgs must not be persisted globally in QSettings");
    QTemporaryDir otherWs;
    QVERIFY(otherWs.isValid());
    manager.setWorkspace(otherWs.path());
    QVERIFY2(manager.loadProjectSettings().targetArguments.isEmpty(),
             "Switching to a fresh workspace must not inherit previous workspace programArgs");
    QVERIFY2(manager.programArgs(otherWs.filePath(QStringLiteral("other_bin"))).isEmpty(),
             "Unconfigured target in another workspace must default to empty programArgs");

    // Switching back to original workspace preserves app_a's args from .gridlock/settings.toml while keeping app_b empty.
    manager.setWorkspace(tempDir.path());
    QCOMPARE(manager.programArgs(appA), QStringLiteral("--mesh coarse.msh --steps 10"));
    QVERIFY(manager.programArgs(appB).isEmpty());

    // Standalone (no workspace) in-memory map also isolates targets cleanly.
    manager.setWorkspace(QString());
    const QString standaloneA = QStringLiteral("/tmp/gridlock_standalone_a");
    const QString standaloneB = QStringLiteral("/tmp/gridlock_standalone_b");
    manager.setProgramArgs(standaloneA, QStringLiteral("--tol 1e-8"));
    QCOMPARE(manager.programArgs(standaloneA), QStringLiteral("--tol 1e-8"));
    QVERIFY(manager.programArgs(standaloneB).isEmpty());
    manager.setProgramArgs(standaloneA, QString());

    QDir::setCurrent(oldCwd);
}

/**
 * @brief Verifies deserializing `.gridlock/workspace.toml` breakpoint files.
 */
void TestConfig::testDeserialization() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString oldCwd = QDir::currentPath();
    QDir::setCurrent(tempDir.path());

    // Write mock workspace.toml with nested breakpoint tables.
    QDir dir(tempDir.path());
    dir.mkdir(".gridlock");
    QFile file(".gridlock/workspace.toml");
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    out << "[breakpoints]\n";
    out << "[breakpoints.\"\"]\n"; // Empty target executable since it's the fallback
    out << "\"/test/parser.c\" = [42, 84]\n";
    out << "\"/test/network.c\" = [12, 13, 14]\n";
    out << "\"/test/math.c\" = [99]\n";
    file.close();

    auto& manager = gridlock::core::ConfigManager::instance();
    manager.setWorkspace(QDir::currentPath());
    manager.loadConfig();

    // Verify deserialized breakpoint sets match written values.
    auto bps = manager.getBreakpoints();
    QCOMPARE(bps.size(), 3);
    
    QVERIFY(bps.contains("/test/parser.c"));
    QCOMPARE(bps["/test/parser.c"].size(), 2);
    QVERIFY(bps["/test/parser.c"].contains(42));
    QVERIFY(bps["/test/parser.c"].contains(84));

    QVERIFY(bps.contains("/test/network.c"));
    QCOMPARE(bps["/test/network.c"].size(), 3);
    QVERIFY(bps["/test/network.c"].contains(12));
    QVERIFY(bps["/test/network.c"].contains(13));
    QVERIFY(bps["/test/network.c"].contains(14));

    QVERIFY(bps.contains("/test/math.c"));
    QCOMPARE(bps["/test/math.c"].size(), 1);
    QVERIFY(bps["/test/math.c"].contains(99));

    QDir::setCurrent(oldCwd);
}

/**
 * @brief Verifies fallback values when configuration TOML is corrupted.
 */
void TestConfig::testFallbacks() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString oldCwd = QDir::currentPath();
    QDir::setCurrent(tempDir.path());

    // Sandbox the global TOML config directory.
    qputenv("GRIDLOCK_TEST_CONFIG_DIR", tempDir.path().toUtf8());

    QSettings prod("gridlock", "debugger");
    prod.remove("debugger/default_ranks");
    prod.remove("debugger/gdb_path");
    prod.sync();

    // Write malformed TOML to test error resilience.
    QFile file(QDir(tempDir.path()).filePath("config.toml"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("invalid = toml = syntax []\n[missing keys");
    file.close();

    auto& manager = gridlock::core::ConfigManager::instance();
    manager.loadConfig();

    // Verify fallback values are restored cleanly.
    QCOMPARE(manager.getDefaultRanks(), 2);
    QCOMPARE(manager.getGdbPath(), QString("gdb"));
    QCOMPARE(manager.getSourceBackground(), QString("#1e1e1e"));
    QCOMPARE(manager.getSourceText(), QString("#f0f0f0"));
    QCOMPARE(manager.getSourceActiveLine(), QString("#505000"));
    QCOMPARE(manager.getAssemblyOpcode(), QString("#00ff7f"));
    QCOMPARE(manager.getAssemblyRegister(), QString("#ff7f50"));
    QCOMPARE(manager.getAssemblyAddress(), QString("#555555"));
    QCOMPARE(manager.getBreakpoints().size(), 0);

    qunsetenv("GRIDLOCK_TEST_CONFIG_DIR");
    QDir::setCurrent(oldCwd);
}

/**
 * @brief Verifies round-trip persistence of MPI launcher settings.
 */
void TestConfig::testMpiSettingsPersistence() {
    // Dedicated QSettings scope for test isolation.
    QSettings s("GridLock_Test", "Debugger_Test");
    s.clear();
    s.setValue("debugger/mpi_executable", QString("mpirun"));
    s.setValue("debugger/mpi_args",       QString("--hostfile hosts.txt"));
    s.setValue("debugger/default_ranks",  7);
    s.setValue("debugger/gdb_path",       QString("/usr/local/bin/gdb"));
    s.sync();

    // Reload via the same key space.
    QSettings r("GridLock_Test", "Debugger_Test");
    QCOMPARE(r.value("debugger/mpi_executable").toString(), QString("mpirun"));
    QCOMPARE(r.value("debugger/mpi_args").toString(),       QString("--hostfile hosts.txt"));
    QCOMPARE(r.value("debugger/default_ranks").toInt(),     7);
    QCOMPARE(r.value("debugger/gdb_path").toString(),       QString("/usr/local/bin/gdb"));

    // Verify ConfigManager::saveDebuggerSettings round-trip in temp environment.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString oldCwd = QDir::currentPath();
    QDir::setCurrent(tempDir.path());

    auto &mgr = gridlock::core::ConfigManager::instance();
    mgr.loadConfig();

    gridlock::core::DebuggerSettings ds;
    ds.gdbPath       = "/custom/gdb";
    ds.mpiExecutable = "srun";
    ds.mpiArgs       = "--nodes=4";
    ds.defaultRanks  = 8;
    mgr.saveDebuggerSettings(ds);

    // Re-read through the accessor — must reflect what was saved.
    // Assert all debugger configurations match what was serialized into settings.
    const auto loaded = mgr.getDebuggerSettings();
    QCOMPARE(loaded.gdbPath,       QString("/custom/gdb"));
    QCOMPARE(loaded.mpiExecutable, QString("srun"));
    QCOMPARE(loaded.mpiArgs,       QString("--nodes=4"));
    QCOMPARE(loaded.defaultRanks,  8);

    // Clean up production QSettings written above.
    // Clear key paths to avoid leaking test state to user configuration files.
    QSettings prod("GridLock", "Debugger");
    prod.remove("debugger/gdb_path");
    prod.remove("debugger/mpi_executable");
    prod.remove("debugger/mpi_args");
    prod.remove("debugger/default_ranks");
    prod.sync();

    // Restore original current working directory.
    QDir::setCurrent(oldCwd);
    s.clear();
    s.sync();
}

/**
 * @brief Verifies rank count clamping validation (preventing <= 0 ranks).
 */
void TestConfig::testSettingValidation() {
    // Sandbox test execution in an isolated temporary directory.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString oldCwd = QDir::currentPath();
    QDir::setCurrent(tempDir.path());

    // Load initial default configuration settings into manager.
    auto &mgr = gridlock::core::ConfigManager::instance();
    mgr.loadConfig();

    // Attempt to persist a negative rank count (simulates malformed input).
    gridlock::core::DebuggerSettings bad;
    bad.defaultRanks = -99;
    mgr.saveDebuggerSettings(bad);

    // ConfigManager must clamp to at least 1 so the backend never receives invalid rank count.
    // Assert recovered rank count is at least 1.
    const auto recovered = mgr.getDebuggerSettings();
    QVERIFY2(recovered.defaultRanks >= 1,
             "defaultRanks must be >= 1 after clamping an invalid value");

    // Try zero rank count — same expectation.
    bad.defaultRanks = 0;
    mgr.saveDebuggerSettings(bad);
    const auto recovered2 = mgr.getDebuggerSettings();
    QVERIFY2(recovered2.defaultRanks >= 1,
             "defaultRanks must be >= 1 even when zero is written");

    // A valid rank count must survive unchanged.
    gridlock::core::DebuggerSettings good;
    good.defaultRanks = 4;
    mgr.saveDebuggerSettings(good);
    QCOMPARE(mgr.getDebuggerSettings().defaultRanks, 4);

    // Clean up production QSettings.
    QSettings prod("GridLock", "Debugger");
    prod.remove("debugger/default_ranks");
    prod.sync();

    QDir::setCurrent(oldCwd);
}

/**
 * @brief Verifies persistence of SLURM batch job parameters and Spack paths.
 */
void TestConfig::testSlurmSettingsPersistence() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString prevCfgPath = QString::fromUtf8(qgetenv("GRIDLOCK_TEST_CONFIG_DIR"));
    struct SettingsGuard {
        QString prev;
        ~SettingsGuard() {
            if (!prev.isEmpty()) {
                QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, prev);
                QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, prev);
            }
        }
    } guard{prevCfgPath};
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, tempDir.path());

    auto &mgr = gridlock::core::ConfigManager::instance();
    gridlock::core::SlurmSettings slurm;
    slurm.scriptTemplate = "sbatch --custom";
    slurm.partition = "gpu";
    slurm.nodes = 2;
    slurm.tasksPerNode = 8;
    slurm.requestGpus = true;
    slurm.gpusPerNode = 4;
    slurm.spackRoot = "/opt/spack_custom";
    slurm.jobName = "solver_bench";
    slurm.account = "ttu_hpcc";
    slurm.qos = "high";
    slurm.wallTime = "04:00:00";
    slurm.cpusPerTask = 4;
    slurm.memory = "64G";
    slurm.gpuType = "a100";
    slurm.constraint = "nvlink";
    slurm.mpiLauncher = "srun --mpi=pmix";
    slurm.modules = "gcc/13.2 openmpi/5.0";
    slurm.outputLog = "bench_%j.out";
    slurm.errorLog = "bench_%j.err";
    slurm.mailType = "END,FAIL";
    slurm.mailUser = "dev@ttu.edu";
    slurm.scriptPath = "/scratch/job.sbatch";
    slurm.sbatchPath = "/opt/slurm/bin/sbatch";
    slurm.squeuePath = "/opt/slurm/bin/squeue";
    slurm.scontrolPath = "/opt/slurm/bin/scontrol";
    mgr.saveSlurmSettings(slurm);

    const auto loaded = mgr.getSlurmSettings();
    QCOMPARE(loaded.scriptTemplate, QString("sbatch --custom"));
    QCOMPARE(loaded.partition, QString("gpu"));
    QCOMPARE(loaded.nodes, 2);
    QCOMPARE(loaded.tasksPerNode, 8);
    QCOMPARE(loaded.requestGpus, true);
    QCOMPARE(loaded.gpusPerNode, 4);
    QCOMPARE(loaded.spackRoot, QString("/opt/spack_custom"));
    QCOMPARE(loaded.jobName, QString("solver_bench"));
    QCOMPARE(loaded.account, QString("ttu_hpcc"));
    QCOMPARE(loaded.qos, QString("high"));
    QCOMPARE(loaded.wallTime, QString("04:00:00"));
    QCOMPARE(loaded.cpusPerTask, 4);
    QCOMPARE(loaded.memory, QString("64G"));
    QCOMPARE(loaded.gpuType, QString("a100"));
    QCOMPARE(loaded.constraint, QString("nvlink"));
    QCOMPARE(loaded.mpiLauncher, QString("srun --mpi=pmix"));
    QCOMPARE(loaded.modules, QString("gcc/13.2 openmpi/5.0"));
    QCOMPARE(loaded.outputLog, QString("bench_%j.out"));
    QCOMPARE(loaded.errorLog, QString("bench_%j.err"));
    QCOMPARE(loaded.mailType, QString("END,FAIL"));
    QCOMPARE(loaded.mailUser, QString("dev@ttu.edu"));
    QCOMPARE(loaded.scriptPath, QString("/scratch/job.sbatch"));
    QCOMPARE(loaded.sbatchPath, QString("/opt/slurm/bin/sbatch"));
    QCOMPARE(loaded.squeuePath, QString("/opt/slurm/bin/squeue"));
    QCOMPARE(loaded.scontrolPath, QString("/opt/slurm/bin/scontrol"));
}

/**
 * @brief Verifies persistence of remote SSH target credentials.
 */
void TestConfig::testSshSettingsPersistence() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString oldCwd = QDir::currentPath();
    QDir::setCurrent(tempDir.path());

    auto &mgr = gridlock::core::ConfigManager::instance();
    gridlock::core::SshSettings ssh;
    ssh.host = "192.168.1.100";
    ssh.user = "hpcuser";
    ssh.keyPath = "~/.ssh/id_rsa_hpc";
    mgr.saveSshSettings(ssh);

    // Verify host, user, and keyPath settings.
    const auto loaded = mgr.getSshSettings();
    QCOMPARE(loaded.host, QString("192.168.1.100"));
    QCOMPARE(loaded.user, QString("hpcuser"));
    QCOMPARE(loaded.keyPath, QString("~/.ssh/id_rsa_hpc"));

    QDir::setCurrent(oldCwd);
}

/**
 * @brief Verifies persistence of terminal font and shell binary configuration.
 */
void TestConfig::testTerminalSettingsPersistence() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString oldCwd = QDir::currentPath();
    QDir::setCurrent(tempDir.path());
    
    // Override test config directory to isolate file I/O within temporary directory.
    QByteArray oldTestDir = qgetenv("GRIDLOCK_TEST_CONFIG_DIR");
    qputenv("GRIDLOCK_TEST_CONFIG_DIR", tempDir.path().toLocal8Bit());

    auto &mgr = gridlock::core::ConfigManager::instance();
    mgr.loadConfig();
    gridlock::core::TerminalSettings term;
    term.shellPath = "/bin/zsh";
    term.fontFamily = "Hack";
    term.fontSize = 14;
    mgr.saveTerminalSettings(term);

    // Verify configured terminal settings.
    const auto loaded = mgr.getTerminalSettings();
    QCOMPARE(loaded.shellPath, QString("/bin/zsh"));
    QCOMPARE(loaded.fontFamily, QString("Hack"));
    QCOMPARE(loaded.fontSize, 14);

    // Test fallback behavior when terminal keys are omitted.
    QString globalConfigPath = QDir(tempDir.path()).filePath("config.toml");
    toml::table tbl;
    try {
        tbl = toml::parse_file(globalConfigPath.toStdString());
    } catch (...) {}
    if (auto* termTable = tbl["terminal"].as_table()) {
        termTable->erase("shell_path");
        termTable->erase("font_family");
        termTable->erase("font_size");
    }
    // Write back TOML configuration without terminal keys.
    std::ofstream out(globalConfigPath.toStdString());
    if (out.is_open()) out << tbl;
    out.close();

    // Reload config with missing keys to verify system defaults.
    mgr.loadConfig();

    // Validate fallback to default shell and standard font configurations.
    const auto defaults = mgr.getTerminalSettings();
    QString expectedShell = qEnvironmentVariable("SHELL");
    if (expectedShell.isEmpty()) expectedShell = "/bin/bash";
    QCOMPARE(defaults.shellPath, expectedShell);
    QCOMPARE(defaults.fontFamily, QString("FiraCode Nerd Font"));
    QCOMPARE(defaults.fontSize, 12);

    // Restore environment variable and current working directory.
    if (oldTestDir.isEmpty()) {
        qunsetenv("GRIDLOCK_TEST_CONFIG_DIR");
    } else {
        qputenv("GRIDLOCK_TEST_CONFIG_DIR", oldTestDir);
    }
    QDir::setCurrent(oldCwd);
}

#include "../../src/core/managers/SessionManager.hpp"

/**
 * @brief Tests MRU session queue capacity limiting, ordering, and re-insertion.
 */
void TestConfig::testMruLogic() {
    auto& manager = gridlock::core::managers::SessionManager::instance();
    for (int i = 1; i <= 6; ++i) {
        manager.addMruSession(QString("path%1").arg(i));
    }
    QStringList mru = manager.getMruSessions();
    QCOMPARE(mru.size(), 5);
    QCOMPARE(mru.first(), QString("path6"));
    QVERIFY(!mru.contains("path1"));
    manager.addMruSession("path4");
    mru = manager.getMruSessions();
    QCOMPARE(mru.size(), 5);
    QCOMPARE(mru.first(), QString("path4"));
    QCOMPARE(mru.at(1), QString("path6"));
}

void TestConfig::testLocalSlurmBackendPipeline() {
    qRegisterMetaType<gridlock::core::HpcResult>("gridlock::core::HpcResult");
    qRegisterMetaType<gridlock::core::HpcResult>("HpcResult");
    qRegisterMetaType<QList<gridlock::RemoteTargetSpec>>("QList<gridlock::RemoteTargetSpec>");

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString prevCfgPath = QString::fromUtf8(qgetenv("GRIDLOCK_TEST_CONFIG_DIR"));
    struct SettingsGuard {
        QString prev;
        ~SettingsGuard() {
            if (!prev.isEmpty()) {
                QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, prev);
                QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, prev);
            }
        }
    } guard{prevCfgPath};
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, tempDir.path());

    const QString binDir = tempDir.filePath(QStringLiteral("shims"));
    const QString workDir = tempDir.filePath(QStringLiteral("shared work"));
    QVERIFY(QDir().mkpath(binDir) && QDir().mkpath(workDir));

    auto writeExec = [](const QString& path, const QByteArray& body) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        f.write(body);
        f.close();
        return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                               QFileDevice::ReadGroup | QFileDevice::ExeGroup);
    };

    const QString fakeTarget = QDir(workDir).filePath(QStringLiteral("mpi solver"));
    QVERIFY(writeExec(fakeTarget, "#!/bin/sh\nexit 0\n"));

    const QString sbatchShim = QDir(binDir).filePath(QStringLiteral("sbatch"));
    const QString squeueShim = QDir(binDir).filePath(QStringLiteral("squeue"));
    const QString scontrolShim = QDir(binDir).filePath(QStringLiteral("scontrol"));
    const QString scancelShim = QDir(binDir).filePath(QStringLiteral("scancel"));
    const QString failFlag = QDir(binDir).filePath(QStringLiteral("fail_sbatch"));
    const QString pollCounter = QDir(binDir).filePath(QStringLiteral("poll_count"));
    const QString scancelLog = QDir(binDir).filePath(QStringLiteral("scancel.log"));
    const QString capturedSbatch = QDir(binDir).filePath(QStringLiteral("captured.sbatch"));

    QVERIFY(writeExec(sbatchShim, QStringLiteral(
        "#!/bin/sh\n"
        "if [ -f \"%1\" ]; then\n"
        "  echo \"sbatch: error: Invalid partition specified\" >&2\n"
        "  exit 1\n"
        "fi\n"
        "cp \"$2\" \"%2\"\n"
        "echo \"48291;testcluster\"\n").arg(failFlag, capturedSbatch).toUtf8()));

    QVERIFY(writeExec(squeueShim, QStringLiteral(
        "#!/bin/sh\n"
        "for arg in \"$@\"; do\n"
        "  if [ \"$arg\" = \"%N\" ]; then echo \"node[01-02]\"; exit 0; fi\n"
        "done\n"
        "cnt=0\n"
        "if [ -f \"%1\" ]; then cnt=$(cat \"%1\"); fi\n"
        "cnt=$((cnt + 1))\n"
        "echo \"$cnt\" > \"%1\"\n"
        "if [ \"$cnt\" -eq 1 ]; then echo \"PENDING\"; elif [ \"$cnt\" -eq 2 ]; then echo \"CONFIGURING\"; else echo \"RUNNING\"; fi\n")
        .arg(pollCounter).toUtf8()));

    QVERIFY(writeExec(scontrolShim,
        "#!/bin/sh\n"
        "if [ \"$1\" = \"show\" ] && [ \"$2\" = \"hostnames\" ]; then\n"
        "  printf 'node01\\nnode02\\n'\n"
        "  exit 0\n"
        "fi\n"
        "exit 1\n"));

    QVERIFY(writeExec(scancelShim, QStringLiteral(
        "#!/bin/sh\n"
        "echo \"$1\" >> \"%1\"\n").arg(scancelLog).toUtf8()));

    auto& cfg = gridlock::core::ConfigManager::instance();
    gridlock::core::SshSettings emptySsh;
    cfg.saveSshSettings(emptySsh);

    // 1. Missing sbatch path emits clean actionable failure.
    gridlock::core::SlurmSettings slCfg = cfg.getSlurmSettings();
    slCfg.sbatchPath = QDir(binDir).filePath(QStringLiteral("nonexistent_sbatch"));
    cfg.saveSlurmSettings(slCfg);

    gridlock::core::LocalSlurmBackend backend;
    backend.setPollIntervalMs(20);
    QSignalSpy failSpy(&backend, &gridlock::core::LocalSlurmBackend::jobFailed);
    backend.submitDebugJob(QStringLiteral("#!/bin/bash\n#SBATCH --nodes=2\n#SBATCH --ntasks-per-node=2\nsrun \"{FILE}\"\n"),
                           fakeTarget, QString(), workDir);
    QCOMPARE(failSpy.count(), 1);
    QVERIFY(failSpy.takeFirst().at(1).toString().contains(QStringLiteral("not found"), Qt::CaseInsensitive));

    // 2. Non-zero sbatch exit surfaces stderr immediately.
    slCfg.sbatchPath = sbatchShim;
    slCfg.squeuePath = squeueShim;
    slCfg.scontrolPath = scontrolShim;
    cfg.saveSlurmSettings(slCfg);
    QVERIFY(writeExec(failFlag, "1\n"));

    QSignalSpy submitSpy(&backend, &gridlock::core::HpcBackend::slurmJobSubmitted);
    backend.submitDebugJob(QStringLiteral("#!/bin/bash\n#SBATCH --nodes=2\n#SBATCH --ntasks-per-node=2\nsrun \"{FILE}\"\n"),
                           fakeTarget, QStringLiteral("--tol 1e-6"), workDir);
    QTRY_COMPARE_WITH_TIMEOUT(failSpy.count(), 1, 3000);
    QCOMPARE(submitSpy.count(), 1);
    QVERIFY(failSpy.takeFirst().at(1).toString().contains(QStringLiteral("Invalid partition specified")));
    QFile::remove(failFlag);

    // 3. Full PENDING -> CONFIGURING -> RUNNING polling, scontrol node expansion, wrapper generation, and scancel cleanup.
    QStringList observedStates;
    QObject::connect(&backend, &gridlock::core::LocalSlurmBackend::jobStateChanged,
                     [&](int, const QString& st) { observedStates.append(st); });
    QSignalSpy readySpy(&backend, &gridlock::core::LocalSlurmBackend::attachTargetsReady);

    backend.submitDebugJob(QStringLiteral("#!/bin/bash\n#SBATCH --nodes=2\n#SBATCH --ntasks-per-node=2\nsrun \"{FILE}\"\n"),
                           fakeTarget, QStringLiteral("--mesh fine"), workDir);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.count(), 1, 5000);
    QVERIFY(observedStates.contains(QStringLiteral("PENDING")));
    QVERIFY(observedStates.contains(QStringLiteral("CONFIGURING")));
    QVERIFY(observedStates.contains(QStringLiteral("RUNNING")));
    QCOMPARE(backend.activeJobId(), 48291);

    const QStringList expectedEndpoints{
        QStringLiteral("node01:2345"), QStringLiteral("node01:2346"),
        QStringLiteral("node02:2347"), QStringLiteral("node02:2348")};
    QCOMPARE(backend.resolvedComputeEndpoints(), expectedEndpoints);

    const auto readyArgs = readySpy.takeFirst();
    QCOMPARE(readyArgs.at(0).toInt(), 48291);
    const auto specs = readyArgs.at(1).value<QList<gridlock::RemoteTargetSpec>>();
    QCOMPARE(specs.size(), 4);
    for (int r = 0; r < 4; ++r) {
        QCOMPARE(specs[r].endpoint, expectedEndpoints[r]);
        QCOMPARE(specs[r].localSymbolBinary, fakeTarget);
        QVERIFY(specs[r].extendedRemote);
        QCOMPARE(specs[r].mpiRank, r);
    }

    QFile capFile(capturedSbatch);
    QVERIFY(capFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString capText = QString::fromUtf8(capFile.readAll());
    QVERIFY(capText.contains(QStringLiteral("\"%1\"").arg(fakeTarget)));
    QVERIFY(capText.contains(QStringLiteral("gridlock_gdbserver_wrap.sh")));

    QFile wrapFile(backend.lastWrapperScriptPath());
    QVERIFY(wrapFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString wrapText = QString::fromUtf8(wrapFile.readAll());
    QVERIFY(wrapText.contains(QStringLiteral("SLURM_PROCID")) && wrapText.contains(QStringLiteral("PMI_RANK")) &&
            wrapText.contains(QStringLiteral("OMPI_COMM_WORLD_RANK")) && wrapText.contains(QStringLiteral("gdbserver --multi")));

    QSignalSpy cancelSpy(&backend, &gridlock::core::HpcBackend::commandFinished);
    backend.cancelActiveJob();
    QTRY_COMPARE_WITH_TIMEOUT(cancelSpy.count(), 1, 3000);
    QVERIFY(!backend.isJobActive());
    QFile scLog(scancelLog);
    QVERIFY(scLog.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QString::fromUtf8(scLog.readAll()).trimmed(), QStringLiteral("48291"));
}

QTEST_MAIN(TestConfig)
