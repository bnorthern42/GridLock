/**
 * @file test_memory_export.hpp
 * @brief Unit tests for HPC memory buffer exporting into raw binary and CSV formats.
 *
 * Verifies byte-exact raw dump serialization, matrix dimension reshaping across
 * Int32, Float, and Double data types, and buffer underflow safety checks.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestMemoryExport
 * @brief Test suite validating MemoryExporter binary and CSV export pipelines.
 */
class TestMemoryExport : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies byte-exact writing and reading of raw memory buffers.
     */
    void testExportToBinary();

    /**
     * @brief Verifies 2D matrix export of 32-bit signed integer memory buffers.
     */
    void testExportToCsvInt32();

    /**
     * @brief Verifies 2D matrix export of single-precision IEEE 754 float buffers.
     */
    void testExportToCsvFloat();

    /**
     * @brief Verifies precision formatting during CSV export of double buffers.
     */
    void testExportToCsvDouble();

    /**
     * @brief Verifies bounds checking when buffer size is smaller than matrix dimensions.
     */
    void testExportBoundsCheck();
};
