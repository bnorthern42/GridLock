/**
 * @file test_coordinator.hpp
 * @brief Unit tests for GdbRankCoordinator and GDB/MI output parsing.
 *
 * Verifies multi-rank MPI coordination, GDB/MI asynchronous notification
 * handling, breakpoint hit routing, register state extraction, and variable
 * inspection state machines.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

#include "../src/core/hpc/GdbRankCoordinator.hpp"

/**
 * @class TestGdbCoordinator
 * @brief Test suite exercising GDB/MI response parsing and rank state sync.
 */
class TestGdbCoordinator : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Pre-test environment setup.
     */
    void initTestCase();

    /**
     * @brief Verifies default rank state when coordinator has no active targets.
     */
    void testInitialRankStates();

    /**
     * @brief Data-driven test table for GDB/MI string parser permutations.
     */
    void testGdbMiParser_data();

    /**
     * @brief Executes GDB/MI response parsing across parameterized test vectors.
     */
    void testGdbMiParser();

    /**
     * @brief Verifies independent state transitions across multiple MPI ranks.
     */
    void testMultiRankSync();

    /**
     * @brief Verifies breakpoint cache synchronization and timestamp tracking.
     */
    void testBreakpointCacheFlush();

    /**
     * @brief Verifies watch variable recreation and out-of-scope handling.
     */
    void testRecreateWatchVariable();

    /**
     * @brief Verifies parsing of GDB/MI register dump lists.
     */
    void testRegisterValuesParsing();

    /**
     * @brief Post-test environment cleanup.
     */
    void cleanupTestCase();
};
