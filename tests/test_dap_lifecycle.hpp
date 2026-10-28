/**
 * @file test_dap_lifecycle.hpp
 * @brief Unit tests for DAP session lifecycle and output stream events.
 *
 * Verifies standard stdout/stderr target output event handling, session termination
 * via disconnect requests, and multi-rank thread stop state broadcasting.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestDapLifecycle
 * @brief Test suite validating DAP lifecycle commands and async rank notifications.
 */
class TestDapLifecycle : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies stdout/stderr output event deserialization and signal dispatch.
     */
    void testOutputEvent();

    /**
     * @brief Verifies disconnect request construction with terminateDebuggee flag.
     */
    void testDisconnectRequest();

    /**
     * @brief Verifies thread-to-rank index mapping when broadcasting stop events.
     */
    void testRankStateBroadcasting();
};
