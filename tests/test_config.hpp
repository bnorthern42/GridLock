/**
 * @file test_config.hpp
 * @brief Unit test fixture for configuration serialization, deserialization, and fallbacks.
 *
 * Verifies TOML workspace breakpoint round-tripping, corrupt configuration fallback
 * values, validation clamping, SLURM batch settings, SSH profiles, and terminal preferences.
 */

#pragma once

#include <QObject>
#include <QtTest>

/**
 * @class TestConfig
 * @brief QtTest suite verifying ConfigManager file I/O and schema fallbacks.
 */
class TestConfig : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Tests serializing active breakpoint map into `.gridlock/workspace.toml`.
     */
    void testSerialization();

    /**
     * @brief Tests deserializing breakpoints from valid `.gridlock/workspace.toml` format.
     */
    void testDeserialization();

    /**
     * @brief Tests graceful fallbacks to defaults when config.toml is corrupt or unparseable.
     */
    void testFallbacks();

    /**
     * @brief Tests persistence and retrieval of MPI launch settings and rank counts.
     */
    void testMpiSettingsPersistence();

    /**
     * @brief Tests validation clamping ensuring rank counts never fall below 1.
     */
    void testSettingValidation();

    /**
     * @brief Tests SLURM cluster partition, GPU, and node allocation setting persistence.
     */
    void testSlurmSettingsPersistence();

    /**
     * @brief Tests remote SSH connection credentials and key path persistence.
     */
    void testSshSettingsPersistence();

    /**
     * @brief Tests terminal emulator shell binary, font family, and font size persistence.
     */
    void testTerminalSettingsPersistence();
};
