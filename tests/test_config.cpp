#include "test_config.hpp"
#include "../src/core/ConfigManager.hpp"
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QTextStream>

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

    QCOMPARE(manager.getDefaultRanks(), 6);
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

QTEST_MAIN(TestConfig)
