/**
 * @file test_dap_commands.hpp
 * @brief Unit tests for Debug Adapter Protocol (DAP) command orchestration.
 *
 * Verifies adapter handshake sequencing (initialized -> configurationDone)
 * and execution control commands (step-over, next).
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestDapCommands
 * @brief Test suite exercising DAP protocol handshakes and control commands.
 */
class TestDapCommands : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies adapter initialization handshake and configurationDone emission.
     */
    void testHandshake();

    /**
     * @brief Verifies execution control commands like StepCommand issuing 'next'.
     */
    void testExecutionCommands();
};
