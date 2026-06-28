#include "test_lsp.hpp"
#include "../src/core/hpc/LspCoordinator.hpp"
#include <QJsonObject>
#include <QJsonDocument>
#include <QSignalSpy>

using namespace gridlock::core;

void TestLspCoordinator::testMessageFraming() {
    QJsonObject dummyPayload;
    dummyPayload["jsonrpc"] = "2.0";
    dummyPayload["id"] = 1;
    dummyPayload["method"] = "initialize";
    
    QByteArray result = LspCoordinator::formatMessage(dummyPayload);
    
    QByteArray expectedJson = QJsonDocument(dummyPayload).toJson(QJsonDocument::Compact);
    QByteArray expectedHeader = QString("Content-Length: %1\r\n\r\n").arg(expectedJson.size()).toUtf8();
    QByteArray expected = expectedHeader + expectedJson;
    
    QCOMPARE(result, expected);
}

void TestLspCoordinator::testLspParser_data() {
    QTest::addColumn<QString>("rawStandardOutput");
    QTest::addColumn<QString>("expectedExtraction");

    // Row 1 (Initialize)
    QJsonObject initResult;
    initResult["jsonrpc"] = "2.0";
    initResult["id"] = 1;
    QJsonObject resultObj;
    resultObj["capabilities"] = QJsonObject();
    initResult["result"] = resultObj;
    QByteArray initBytes = LspCoordinator::formatMessage(initResult);

    QTest::newRow("Initialize") << QString::fromUtf8(initBytes) << QString("INITIALIZED");

    // Row 2 (Hover)
    QJsonObject hoverResult;
    hoverResult["jsonrpc"] = "2.0";
    hoverResult["id"] = 2;
    QJsonObject hResult;
    QJsonObject hContents;
    hContents["value"] = "int main()";
    hResult["contents"] = hContents;
    hoverResult["result"] = hResult;
    QByteArray hoverBytes = LspCoordinator::formatMessage(hoverResult);

    QTest::newRow("Hover") << QString::fromUtf8(hoverBytes) << QString("int main()");
}

void TestLspCoordinator::testLspParser() {
    QFETCH(QString, rawStandardOutput);
    QFETCH(QString, expectedExtraction);

    LspCoordinator coordinator;
    QSignalSpy spy(&coordinator, &LspCoordinator::hoverResultReceived);

    if (expectedExtraction == "INITIALIZED") {
        coordinator.processRawOutput(rawStandardOutput.toUtf8());
        QVERIFY(coordinator.isInitialized());
    } else {
        // Initialize first so requestHover is not ignored
        QJsonObject initResult;
        initResult["jsonrpc"] = "2.0";
        initResult["id"] = 1;
        QJsonObject resultObj;
        resultObj["capabilities"] = QJsonObject();
        initResult["result"] = resultObj;
        coordinator.processRawOutput(LspCoordinator::formatMessage(initResult));

        // Our test data uses ID=2 for the hover.
        // We call requestHover twice so m_nextRequestId hits 2 and registers it.
        coordinator.requestHover("dummy.cpp", 1, 1, QPoint(10, 10)); // Request ID 1
        coordinator.requestHover("dummy.cpp", 1, 1, QPoint(10, 10)); // Request ID 2
        
        coordinator.processRawOutput(rawStandardOutput.toUtf8());
        
        QCOMPARE(spy.count(), 1);
        QList<QVariant> arguments = spy.takeFirst();
        QCOMPARE(arguments.at(0).toString(), expectedExtraction);
    }
}

void TestLspCoordinator::testFragmentedStream() {
    LspCoordinator coordinator;
    QSignalSpy spy(&coordinator, &LspCoordinator::hoverResultReceived);
    
    // Initialize first
    QJsonObject initResult;
    initResult["jsonrpc"] = "2.0";
    initResult["id"] = 1;
    QJsonObject resultObj;
    resultObj["capabilities"] = QJsonObject();
    initResult["result"] = resultObj;
    coordinator.processRawOutput(LspCoordinator::formatMessage(initResult));

    // Send one request to get ID=2
    coordinator.requestHover("dummy.cpp", 1, 1, QPoint(10, 10));

    QJsonObject hoverResult;
    hoverResult["jsonrpc"] = "2.0";
    hoverResult["id"] = 1;
    QJsonObject hResult;
    QJsonObject hContents;
    hContents["value"] = "Fragmented Markdown";
    hResult["contents"] = hContents;
    hoverResult["result"] = hResult;
    
    QByteArray fullBytes = LspCoordinator::formatMessage(hoverResult);
    
    // Split the stream in half
    int splitPoint = fullBytes.size() / 2;
    QByteArray frag1 = fullBytes.left(splitPoint);
    QByteArray frag2 = fullBytes.mid(splitPoint);

    coordinator.processRawOutput(frag1);
    QCOMPARE(spy.count(), 0); // Shouldn't parse yet, payload incomplete
    
    coordinator.processRawOutput(frag2);
    QCOMPARE(spy.count(), 1); // Now it should parse successfully
    
    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toString(), QString("Fragmented Markdown"));
}

void TestLspCoordinator::testLspHoverParsing() {
    LspCoordinator coordinator;
    QSignalSpy spy(&coordinator, &LspCoordinator::hoverResultReceived);
    
    // Initialize first
    QJsonObject initResult;
    initResult["jsonrpc"] = "2.0";
    initResult["id"] = 1;
    QJsonObject resultObj;
    resultObj["capabilities"] = QJsonObject();
    initResult["result"] = resultObj;
    coordinator.processRawOutput(LspCoordinator::formatMessage(initResult));

    // 1. Object Test
    coordinator.requestHover("dummy.cpp", 1, 1, QPoint(10, 10));
    QJsonObject hoverObj;
    hoverObj["jsonrpc"] = "2.0";
    hoverObj["id"] = 1; 
    QJsonObject hResultObj;
    QJsonObject hContentsObj;
    hContentsObj["kind"] = "markdown";
    hContentsObj["value"] = "Object Markdown";
    hResultObj["contents"] = hContentsObj;
    hoverObj["result"] = hResultObj;
    coordinator.processRawOutput(LspCoordinator::formatMessage(hoverObj));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QString("Object Markdown"));

    // 2. String Test
    coordinator.requestHover("dummy.cpp", 2, 2, QPoint(20, 20));
    QJsonObject hoverStr;
    hoverStr["jsonrpc"] = "2.0";
    hoverStr["id"] = 2;
    QJsonObject hResultStr;
    hResultStr["contents"] = "String Markdown";
    hoverStr["result"] = hResultStr;
    coordinator.processRawOutput(LspCoordinator::formatMessage(hoverStr));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QString("String Markdown"));

    // 3. Array Test
    coordinator.requestHover("dummy.cpp", 3, 3, QPoint(30, 30));
    QJsonObject hoverArr;
    hoverArr["jsonrpc"] = "2.0";
    hoverArr["id"] = 3;
    QJsonObject hResultArr;
    QJsonArray hContentsArr;
    hContentsArr.append("Array String");
    QJsonObject objVal;
    objVal["kind"] = "markdown";
    objVal["value"] = "Array Object";
    hContentsArr.append(objVal);
    hResultArr["contents"] = hContentsArr;
    hoverArr["result"] = hResultArr;
    coordinator.processRawOutput(LspCoordinator::formatMessage(hoverArr));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString().trimmed(), QString("Array String\nArray Object"));
}

QTEST_MAIN(TestLspCoordinator)
