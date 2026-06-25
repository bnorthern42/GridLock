#include "test_config_engine.hpp"
#include "../src/core/managers/ConfigManager.hpp"
#include "../src/core/managers/SessionManager.hpp"
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QStandardPaths>

void TestConfigEngine::testConfigIO() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    qputenv("GRIDLOCK_TEST_CONFIG_DIR", tempDir.path().toUtf8());

    auto& manager = gridlock::core::ConfigManager::instance();
    manager.loadConfig(); // Should create the file with defaults

    QFile file(QDir(tempDir.path()).filePath("config.toml"));
    QVERIFY(file.exists());
    
    QCOMPARE(manager.getGlobalDapAdapterPath(), QString("lldb-dap"));
    QCOMPARE(manager.getGlobalGdbPath(), QString("gdb"));
    QCOMPARE(manager.getGlobalMpiArgs(), QString("--oversubscribe"));
    
    // Modify and verify persistence
    manager.setGlobalDapAdapterPath("custom-dap");
    manager.loadConfig();
    QCOMPARE(manager.getGlobalDapAdapterPath(), QString("custom-dap"));
}

void TestConfigEngine::testMruLogic() {
    auto& manager = gridlock::core::managers::SessionManager::instance();
    
    // Push 6 unique paths
    for (int i = 1; i <= 6; ++i) {
        manager.addMruSession(QString("path%1").arg(i));
    }
    
    QStringList mru = manager.getMruSessions();
    QCOMPARE(mru.size(), 5);
    QCOMPARE(mru.first(), QString("path6"));
    QVERIFY(!mru.contains("path1")); // The oldest should drop off
    
    // Push an existing path (path4)
    manager.addMruSession("path4");
    mru = manager.getMruSessions();
    QCOMPARE(mru.size(), 5);
    QCOMPARE(mru.first(), QString("path4"));
    QCOMPARE(mru.at(1), QString("path6"));
}

QTEST_MAIN(TestConfigEngine)
