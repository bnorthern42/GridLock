/**
 * @file test_dap_native_bridge.hpp
 * @brief Unit tests for DAP process ID extraction and native memory read routing.
 *
 * Verifies extraction of OS process identifiers from DAP process events and
 * routing of high-throughput memory queries directly to Linux process_vm_readv.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestDapNativeBridge
 * @brief Test suite validating hybrid DAP / NativeMemoryReader IPC optimization.
 */
class TestDapNativeBridge : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies systemProcessId extraction from DAP process events.
     */
    void testPidExtraction();

    /**
     * @brief Verifies dynamic routing between fast native memory reads and DAP JSON.
     */
    void testRoutingLogic();
};
