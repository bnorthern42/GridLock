#include "test_memory_export.hpp"
#include "../src/core/hpc/MemoryExporter.hpp"
#include <QtTest>
#include <QTemporaryFile>
#include <vector>
#include <cstring>
#include <fstream>
#include <string>

using namespace gridlock::core::hpc;

void TestMemoryExport::testExportToBinary() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    QString path = tempFile.fileName();
    tempFile.close();

    std::vector<uint8_t> buffer = {0x01, 0x02, 0x03, 0x04, 0xFF, 0xAA};
    QVERIFY(MemoryExporter::exportToBinary(path.toStdString(), buffer));

    std::ifstream in(path.toStdString(), std::ios::binary);
    std::vector<uint8_t> readBack((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    
    QCOMPARE(readBack.size(), buffer.size());
    for (size_t i = 0; i < buffer.size(); ++i) {
        QCOMPARE(readBack[i], buffer[i]);
    }
}

void TestMemoryExport::testExportToCsvInt32() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    QString path = tempFile.fileName();
    tempFile.close();

    std::vector<int32_t> data = {1, 2, 3, 4, -5, 6};
    std::vector<uint8_t> buffer(data.size() * sizeof(int32_t));
    std::memcpy(buffer.data(), data.data(), buffer.size());

    QVERIFY(MemoryExporter::exportToCsv(path.toStdString(), buffer, 2, 3, DataType::Int32));

    std::ifstream in(path.toStdString());
    std::string line1, line2;
    std::getline(in, line1);
    std::getline(in, line2);

    QCOMPARE(QString::fromStdString(line1), QString("1,2,3"));
    QCOMPARE(QString::fromStdString(line2), QString("4,-5,6"));
}

void TestMemoryExport::testExportToCsvFloat() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    QString path = tempFile.fileName();
    tempFile.close();

    std::vector<float> data = {1.5f, 2.25f, -3.125f, 4.0f};
    std::vector<uint8_t> buffer(data.size() * sizeof(float));
    std::memcpy(buffer.data(), data.data(), buffer.size());

    QVERIFY(MemoryExporter::exportToCsv(path.toStdString(), buffer, 2, 2, DataType::Float));

    std::ifstream in(path.toStdString());
    std::string line1, line2;
    std::getline(in, line1);
    std::getline(in, line2);

    QCOMPARE(QString::fromStdString(line1), QString("1.5,2.25"));
    QCOMPARE(QString::fromStdString(line2), QString("-3.125,4"));
}

void TestMemoryExport::testExportToCsvDouble() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    QString path = tempFile.fileName();
    tempFile.close();

    std::vector<double> data = {1.123456789, 2.987654321};
    std::vector<uint8_t> buffer(data.size() * sizeof(double));
    std::memcpy(buffer.data(), data.data(), buffer.size());

    QVERIFY(MemoryExporter::exportToCsv(path.toStdString(), buffer, 1, 2, DataType::Double));

    std::ifstream in(path.toStdString());
    std::string line1;
    std::getline(in, line1);

    QVERIFY(QString::fromStdString(line1).startsWith("1.12345679")); // std::setprecision(9)
}

void TestMemoryExport::testExportBoundsCheck() {
    std::vector<uint8_t> buffer = {0x00, 0x01}; // 2 bytes
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    QString path = tempFile.fileName();
    tempFile.close();

    // 1 Int32 requires 4 bytes, this should safely return false.
    QVERIFY(!MemoryExporter::exportToCsv(path.toStdString(), buffer, 1, 1, DataType::Int32));
}

QTEST_MAIN(TestMemoryExport)
