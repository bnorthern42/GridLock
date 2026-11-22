/**
 * @file test_dap_memory.hpp
 * @brief Unit tests for DAP memory read operations and register scope inspection.
 *
 * Verifies requestScopes parsing for register inspection, readMemory command
 * formatting, and base64 memory buffer decoding into memoryRead signals.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestDapMemory
 * @brief Test suite exercising DAP memory read and register query workflows.
 */
class TestDapMemory : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies that scopes responses containing Registers are parsed and signaled.
     */
    void testRegistersFetch();

    /**
     * @brief Verifies readMemory JSON-RPC request serialization and argument checks.
     */
    void testMemoryRequest();

    /**
     * @brief Verifies readMemory base64 response decoding and memoryRead signal emission.
     */
    void testMemoryResponse();
};
