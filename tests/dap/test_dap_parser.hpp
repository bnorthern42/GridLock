/**
 * @file test_dap_parser.hpp
 * @brief Unit tests for DAP wire framing and JSON message boundary parser.
 *
 * Verifies Content-Length header parsing, TCP fragmentation reassembly,
 * and back-to-back multiple message unpacking.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestDapParser
 * @brief Test suite exercising low-level DAP stream deserialization.
 */
class TestDapParser : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies complete Content-Length framed message parsing.
     */
    void testFullMessage();

    /**
     * @brief Verifies buffering and assembly of TCP fragmented message chunks.
     */
    void testFragmentedMessage();

    /**
     * @brief Verifies extraction of multiple back-to-back DAP messages in a single packet.
     */
    void testMultipleMessages();
};
