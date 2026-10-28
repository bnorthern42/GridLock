/**
 * @file test_terminal_config.hpp
 * @brief Unit tests for TerminalDockWidget styling and environment configuration.
 *
 * Verifies custom terminal font family and size propagation, color scheme
 * selection, scrollback history limits, and child shell environment variables.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestTerminalConfig
 * @brief Test suite exercising TerminalDockWidget property setters and getters.
 */
class TestTerminalConfig : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies terminal font family and point size configuration.
     */
    void testFontConfiguration();

    /**
     * @brief Verifies color scheme name application.
     */
    void testColorSchemeConfiguration();

    /**
     * @brief Verifies scrollback line buffer capacity settings.
     */
    void testScrollbackLimit();

    /**
     * @brief Verifies environment variable propagation to terminal sessions.
     */
    void testEnvironmentVariables();
};
