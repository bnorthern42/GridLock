/**
 * @file test_config_engine.hpp
 * @brief Unit tests for configuration I/O and MRU session history logic.
 *
 * Verifies default TOML configuration generation, parameter persistence, and
 * most-recently-used (MRU) session queue eviction policies.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestConfigEngine
 * @brief Test suite validating ConfigManager and SessionManager behaviors.
 */
class TestConfigEngine : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies default TOML creation and configuration value persistence.
     */
    void testConfigIO();

    /**
     * @brief Verifies MRU session list eviction rules and duplicate handling.
     */
    void testMruLogic();
};
