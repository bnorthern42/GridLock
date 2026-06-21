#include "test_config.hpp"
#include "../src/core/ConfigManager.hpp"
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QSettings>

void TestConfig::testSerialization() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString oldCwd = QDir::currentPath();
    QDir::setCurrent(tempDir.path());

    auto& manager = gridlock::core::ConfigManager::instance();
    manager.loadConfig(); // Loads defaults into the new dir

    QMap<QString, QSet<int>> breakpoints;
    breakpoints["/app/src/main.cpp"] = {10, 15, 20};
    breakpoints["/app/src/utils.cpp"] = {5, 10};
    breakpoints["/app/src/solver.cpp"] = {100};

    manager.saveBreakpoints(breakpoints);

    QFile file("gridlock_config.toml");
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QString content = file.readAll();
    file.close();

    QVERIFY(content.contains("/app/src/main.cpp"));
    QVERIFY(content.contains("/app/src/utils.cpp"));
    QVERIFY(content.contains("/app/src/solver.cpp"));
    
    QDir::setCurrent(oldCwd);
}

void TestConfig::testDeserialization() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString oldCwd = QDir::currentPath();
    QDir::setCurrent(tempDir.path());

    QFile file("gridlock_config.toml");
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    out << "[breakpoints]\n";
    out << "\"/test/parser.c\" = [42, 84]\n";
    out << "\"/test/network.c\" = [12, 13, 14]\n";
    out << "\"/test/math.c\" = [99]\n";
    file.close();

    auto& manager = gridlock::core::ConfigManager::instance();
    manager.loadConfig();

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

void TestConfig::testFallbacks() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString oldCwd = QDir::currentPath();
    QDir::setCurrent(tempDir.path());

    QFile file("gridlock_config.toml");
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("invalid = toml = syntax []\n[missing keys");
    file.close();

    auto& manager = gridlock::core::ConfigManager::instance();
    manager.loadConfig();

    QCOMPARE(manager.getDefaultRanks(), 2);  // Updated: default changed from 6 → 2
    QCOMPARE(manager.getGdbPath(), QString("gdb"));
    QCOMPARE(manager.getSourceBackground(), QString("#1e1e1e"));
    QCOMPARE(manager.getSourceText(), QString("#f0f0f0"));
    QCOMPARE(manager.getSourceActiveLine(), QString("#505000"));
    QCOMPARE(manager.getAssemblyOpcode(), QString("#00ff7f"));
    QCOMPARE(manager.getAssemblyRegister(), QString("#ff7f50"));
    QCOMPARE(manager.getAssemblyAddress(), QString("#555555"));
    QCOMPARE(manager.getBreakpoints().size(), 0);

    QDir::setCurrent(oldCwd);
}

void TestConfig::testMpiSettingsPersistence() {
    // Use a dedicated QSettings scope so this test doesn't clobber real user
    // settings and can be inspected in isolation.
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

    // Verify ConfigManager::saveDebuggerSettings / getDebuggerSettings round-trip
    // using the production org/app keys against a temp TOML directory.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString oldCwd = QDir::currentPath();
    QDir::setCurrent(tempDir.path());

    auto &mgr = gridlock::core::ConfigManager::instance();
    mgr.loadConfig(); // writes default TOML in temp dir

    gridlock::core::DebuggerSettings ds;
    ds.gdbPath       = "/custom/gdb";
    ds.mpiExecutable = "srun";
    ds.mpiArgs       = "--nodes=4";
    ds.defaultRanks  = 8;
    mgr.saveDebuggerSettings(ds);

    // Re-read through the accessor — must reflect what was saved.
    const auto loaded = mgr.getDebuggerSettings();
    QCOMPARE(loaded.gdbPath,       QString("/custom/gdb"));
    QCOMPARE(loaded.mpiExecutable, QString("srun"));
    QCOMPARE(loaded.mpiArgs,       QString("--nodes=4"));
    QCOMPARE(loaded.defaultRanks,  8);

    // Clean up production QSettings written above.
    QSettings prod("GridLock", "Debugger");
    prod.remove("debugger/gdb_path");
    prod.remove("debugger/mpi_executable");
    prod.remove("debugger/mpi_args");
    prod.remove("debugger/default_ranks");
    prod.sync();

    QDir::setCurrent(oldCwd);
    s.clear();
    s.sync();
}

void TestConfig::testSettingValidation() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString oldCwd = QDir::currentPath();
    QDir::setCurrent(tempDir.path());

    auto &mgr = gridlock::core::ConfigManager::instance();
    mgr.loadConfig();

    // Attempt to persist a non-positive rank count (simulates a buggy writer).
    gridlock::core::DebuggerSettings bad;
    bad.defaultRanks = -99;  // invalid
    mgr.saveDebuggerSettings(bad);

    // ConfigManager must clamp to at least 1 so the backend never gets 0 ranks.
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

    // Clean up production QSettings written above.
    QSettings prod("GridLock", "Debugger");
    prod.remove("debugger/default_ranks");
    prod.sync();

    QDir::setCurrent(oldCwd);
}

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

    const auto loaded = mgr.getSshSettings();
    QCOMPARE(loaded.host, QString("192.168.1.100"));
    QCOMPARE(loaded.user, QString("hpcuser"));
    QCOMPARE(loaded.keyPath, QString("~/.ssh/id_rsa_hpc"));

    QDir::setCurrent(oldCwd);
}

QTEST_MAIN(TestConfig)
