/**
 * @file test_lsp.hpp
 * @brief Unit tests for Language Server Protocol (LSP) coordinator and JSON-RPC framing.
 *
 * Verifies message header formatting, server initialization responses,
 * fragmented packet assembly, and varied LSP hover tooltip payloads.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestLspCoordinator
 * @brief Test suite validating LSP message parsing and hover documentation extraction.
 */
class TestLspCoordinator : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies Content-Length header generation and JSON payload serialization.
     */
    void testMessageFraming();

    /**
     * @brief Data-driven test table for LSP initialize and hover messages.
     */
    void testLspParser_data();

    /**
     * @brief Executes LSP message extraction against parameterized test data.
     */
    void testLspParser();

    /**
     * @brief Verifies stream buffering when LSP packets are split across chunks.
     */
    void testFragmentedStream();

    /**
     * @brief Verifies hover contents parsing across string, object, and array variants.
     */
    void testLspHoverParsing();
};
