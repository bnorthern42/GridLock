/**
 * @file test_hpc_memory.hpp
 * @brief Unit tests for Linux cross-process native memory inspection.
 *
 * Verifies NativeMemoryReader directly extracting floating-point arrays from
 * a child debuggee process using Linux process_vm_readv.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestHpcMemory
 * @brief Test suite validating direct Linux kernel cross-memory space reads.
 */
class TestHpcMemory : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies process_vm_readv memory extraction from a running child process.
     */
    void testDirectRead();
};
