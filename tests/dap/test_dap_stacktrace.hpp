/**
 * @file test_dap_stacktrace.hpp
 * @brief Unit tests for DAP stack trace querying and source location mapping.
 *
 * Verifies stackTrace request dispatch upon stop events and mapping of
 * stack frame responses to file and line coordinates via locationChanged signals.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestDapStacktrace
 * @brief Test suite exercising DAP stack frame requests and location synchronization.
 */
class TestDapStacktrace : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies that stop events automatically trigger stackTrace requests.
     */
    void testStackTraceRequest();

    /**
     * @brief Verifies parsing of stackTrace responses into locationChanged signals.
     */
    void testStackTraceResponseParsing();
};
