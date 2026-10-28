/**
 * @file test_memory_export.cpp
 * @brief Implementation of MemoryExporter format conversion unit tests.
 *
 * Exercises direct binary disk serialization, CSV matrix reshaping with typed
 * endianness decoding (Int32, Float, Double), and underflow boundary validation.
 */

#include "test_memory_export.hpp"

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <QTemporaryFile>
#include <QtTest/QtTest>

#include "../src/core/hpc/MemoryExporter.hpp"

using namespace gridlock::core::hpc;

/**
 * @brief Tests raw binary buffer persistence and round-trip verification.
 */
void TestMemoryExport::testExportToBinary() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    QString path = tempFile.fileName();
    tempFile.close();

    // Construct raw byte pattern.
    std::vector<uint8_t> buffer = {0x01, 0x02, 0x03, 0x04, 0xFF, 0xAA};
    QVERIFY(MemoryExporter::exportToBinary(path.toStdString(), buffer));

    // Read file contents back from disk.
    std::ifstream in(path.toStdString(), std::ios::binary);
    std::vector<uint8_t> readBack((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // Verify written size and bytes match exactly.
    QCOMPARE(readBack.size(), buffer.size());
    for (size_t i = 0; i < buffer.size(); ++i) {
        QCOMPARE(readBack[i], buffer[i]);
    }
}

/**
 * @brief Tests exporting a 2x3 matrix of 32-bit signed integers into CSV format.
 */
void TestMemoryExport::testExportToCsvInt32() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    QString path = tempFile.fileName();
    tempFile.close();

    // Prepare 6 integers (2 rows, 3 cols).
    std::vector<int32_t> data = {1, 2, 3, 4, -5, 6};
    std::vector<uint8_t> buffer(data.size() * sizeof(int32_t));
    std::memcpy(buffer.data(), data.data(), buffer.size());

    // Export with 2 rows and 3 columns.
    QVERIFY(MemoryExporter::exportToCsv(path.toStdString(), buffer, 2, 3, DataType::Int32));

    // Verify row contents in output CSV.
    std::ifstream in(path.toStdString());
    std::string line1, line2;
    std::getline(in, line1);
    std::getline(in, line2);

    QCOMPARE(QString::fromStdString(line1), QString("1,2,3"));
    QCOMPARE(QString::fromStdString(line2), QString("4,-5,6"));
}

/**
 * @brief Tests exporting a 2x2 matrix of single-precision floating point values into CSV.
 */
void TestMemoryExport::testExportToCsvFloat() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    QString path = tempFile.fileName();
    tempFile.close();

    // Prepare 4 floats (2 rows, 2 cols).
    std::vector<float> data = {1.5f, 2.25f, -3.125f, 4.0f};
    std::vector<uint8_t> buffer(data.size() * sizeof(float));
    std::memcpy(buffer.data(), data.data(), buffer.size());

    // Export with 2 rows and 2 columns.
    QVERIFY(MemoryExporter::exportToCsv(path.toStdString(), buffer, 2, 2, DataType::Float));

    // Verify CSV output text representation.
    std::ifstream in(path.toStdString());
    std::string line1, line2;
    std::getline(in, line1);
    std::getline(in, line2);

    QCOMPARE(QString::fromStdString(line1), QString("1.5,2.25"));
    QCOMPARE(QString::fromStdString(line2), QString("-3.125,4"));
}

/**
 * @brief Tests exporting double-precision floating point values with high precision.
 */
void TestMemoryExport::testExportToCsvDouble() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    QString path = tempFile.fileName();
    tempFile.close();

    // Prepare 2 doubles (1 row, 2 cols).
    std::vector<double> data = {1.123456789, 2.987654321};
    std::vector<uint8_t> buffer(data.size() * sizeof(double));
    std::memcpy(buffer.data(), data.data(), buffer.size());

    // Export with 1 row and 2 columns.
    QVERIFY(MemoryExporter::exportToCsv(path.toStdString(), buffer, 1, 2, DataType::Double));

    // Verify precision formatting in first line.
    std::ifstream in(path.toStdString());
    std::string line1;
    std::getline(in, line1);

    // std::setprecision(9) formatting check.
    QVERIFY(QString::fromStdString(line1).startsWith("1.12345679"));
}

/**
 * @brief Tests memory buffer bounds verification to prevent out-of-bounds reads.
 */
void TestMemoryExport::testExportBoundsCheck() {
    std::vector<uint8_t> buffer = {0x00, 0x01}; // 2 bytes
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    QString path = tempFile.fileName();
    tempFile.close();

    // 1 Int32 requires 4 bytes, this should safely return false.
    QVERIFY(!MemoryExporter::exportToCsv(path.toStdString(), buffer, 1, 1, DataType::Int32));
    QVERIFY(!MemoryExporter::exportToCsv(path.toStdString(), buffer, 1, 1, DataType::Float));
    QVERIFY(!MemoryExporter::exportToCsv(path.toStdString(), buffer, 1, 1, DataType::Double));
}

QTEST_MAIN(TestMemoryExport)
