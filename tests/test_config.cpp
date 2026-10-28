/**
 * @file test_config.cpp
 * @brief Implementation of configuration management and persistence test cases.
 *
 * Exercises TOML serialization, deserialization, invalid syntax recovery,
 * validation boundary checks, SLURM templates, and terminal preferences.
 */

#include "test_config.hpp"
#include "../src/core/managers/ConfigManager.hpp"

#include <QDir>
#include <QFile>
#include <QSettings>
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
    QString oldCwd = QDir::currentPath();
    QDir::setCurrent(tempDir.path());

    auto &mgr = gridlock::core::ConfigManager::instance();
    gridlock::core::SlurmSettings slurm;
    slurm.scriptTemplate = "sbatch --custom";
    slurm.partition = "gpu";
    slurm.nodes = 2;
    slurm.tasksPerNode = 8;
    slurm.requestGpus = true;
    slurm.gpusPerNode = 4;
    slurm.spackRoot = "/opt/spack_custom";
    mgr.saveSlurmSettings(slurm);

    // Verify all SLURM properties round-trip correctly.
    const auto loaded = mgr.getSlurmSettings();
    QCOMPARE(loaded.scriptTemplate, QString("sbatch --custom"));
    QCOMPARE(loaded.partition, QString("gpu"));
    QCOMPARE(loaded.nodes, 2);
    QCOMPARE(loaded.tasksPerNode, 8);
    QCOMPARE(loaded.requestGpus, true);
    QCOMPARE(loaded.gpusPerNode, 4);
    QCOMPARE(loaded.spackRoot, QString("/opt/spack_custom"));

    QDir::setCurrent(oldCwd);
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

QTEST_MAIN(TestConfig)
