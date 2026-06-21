#include "test_config.hpp"
#include "../src/core/ConfigManager.hpp"
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <unistd.h>

void TestConfig::testTomlSerialization() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    QString oldCwd = QDir::currentPath();
    QDir::setCurrent(tempDir.path());

    QFile file("gridlock_config.toml");
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QString content = "[debugger]\ndefault_ranks = 12\ngdb_path = '/custom/gdb'\n[breakpoints]\n\"/test/file.c\" = [1, 2, 3]\n";
    file.write(content.toUtf8());
    file.close();

    auto& manager = gridlock::core::ConfigManager::instance();
    manager.loadConfig();

    QCOMPARE(manager.getDefaultRanks(), 12);
    QCOMPARE(manager.getGdbPath(), QString("/custom/gdb"));
    
    auto bps = manager.getBreakpoints();
    QVERIFY(bps.contains("/test/file.c"));
    QCOMPARE(bps["/test/file.c"].size(), 3);
    QVERIFY(bps["/test/file.c"].contains(1));
    QVERIFY(bps["/test/file.c"].contains(2));
    QVERIFY(bps["/test/file.c"].contains(3));

    // Test saving back
    bps["/test/file.c"].insert(4);
    manager.saveBreakpoints(bps);
    manager.loadConfig(); // reload
    
    auto updatedBps = manager.getBreakpoints();
    QVERIFY(updatedBps["/test/file.c"].contains(4));

    QDir::setCurrent(oldCwd);
}

void TestConfig::testSingletonReload() {
    auto& manager = gridlock::core::ConfigManager::instance();
    QVERIFY(!manager.getSourceBackground().isEmpty());
    QVERIFY(!manager.getAssemblyOpcode().isEmpty());
}

QTEST_MAIN(TestConfig)
