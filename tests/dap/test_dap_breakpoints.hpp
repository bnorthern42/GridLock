/**
 * @file test_dap_breakpoints.hpp
 * @brief Unit tests for Debug Adapter Protocol (DAP) breakpoint management.
 *
 * Verifies setBreakpoints request generation, source line accumulation,
 * breakpoint toggling, and asynchronous stopped event parsing.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestDapBreakpoints
 * @brief Test suite exercising DAP breakpoint tracking and stopped event handling.
 */
class TestDapBreakpoints : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies setBreakpoints command payload and line accumulation.
     */
    void testBreakpointAccumulation();

    /**
     * @brief Verifies parsing of DAP stopped events into executionStopped signals.
     */
    void testStopEventParsing();
};
