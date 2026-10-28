/**
 * @file test_config_engine.cpp
 * @brief Implementation of configuration file I/O and MRU history test cases.
 *
 * Exercises TOML serialization, deserialization, path resolution, and MRU list
 * truncation logic across ConfigManager and SessionManager singletons.
 */

#include "test_config_engine.hpp"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "../src/core/managers/ConfigManager.hpp"
#include "../src/core/managers/SessionManager.hpp"

/**
 * @brief Tests basic TOML configuration file creation, defaults, and persistence.
 */
void TestConfigEngine::testConfigIO() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Isolate config file operations to the temporary directory.
    qputenv("GRIDLOCK_TEST_CONFIG_DIR", tempDir.path().toUtf8());

    auto& manager = gridlock::core::ConfigManager::instance();
    // loadConfig creates the default config.toml if it does not yet exist.
    manager.loadConfig();

    QFile file(QDir(tempDir.path()).filePath("config.toml"));
    QVERIFY(file.exists());

    // Validate factory default settings for debugger and MPI tools.
    QCOMPARE(manager.getGlobalDapAdapterPath(), QString("lldb-dap"));
    QCOMPARE(manager.getGlobalGdbPath(), QString("gdb"));
    QCOMPARE(manager.getGlobalMpiArgs(), QString("--oversubscribe"));

    // Modify a configuration field, reload, and verify persistence.
    manager.setGlobalDapAdapterPath("custom-dap");
    manager.loadConfig();
    QCOMPARE(manager.getGlobalDapAdapterPath(), QString("custom-dap"));
}

/**
 * @brief Tests MRU session queue capacity limiting, ordering, and re-insertion.
 */
void TestConfigEngine::testMruLogic() {
    auto& manager = gridlock::core::managers::SessionManager::instance();

    // Push 6 unique paths into the MRU list (max capacity is 5).
    for (int i = 1; i <= 6; ++i) {
        manager.addMruSession(QString("path%1").arg(i));
    }

    // Ensure list is capped at 5 elements and most recent is at the front.
    QStringList mru = manager.getMruSessions();
    QCOMPARE(mru.size(), 5);
    QCOMPARE(mru.first(), QString("path6"));
    // The oldest entry (path1) must have been evicted.
    QVERIFY(!mru.contains("path1"));

    // Push an existing path (path4) to verify it moves to the top without duplicates.
    manager.addMruSession("path4");
    mru = manager.getMruSessions();
    QCOMPARE(mru.size(), 5);
    QCOMPARE(mru.first(), QString("path4"));
    QCOMPARE(mru.at(1), QString("path6"));
}

QTEST_MAIN(TestConfigEngine)
