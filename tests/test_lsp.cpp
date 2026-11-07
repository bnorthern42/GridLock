/**
 * @file test_lsp.cpp
 * @brief Implementation of Language Server Protocol (LSP) coordinator unit tests.
 *
 * Validates JSON-RPC 2.0 Content-Length wire framing, server initialization handshakes,
 * TCP packet fragmentation buffering, and hover tooltip markdown parsing.
 */

#include "test_lsp.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include "../src/core/hpc/LspCoordinator.hpp"
#include "../src/core/utils/ClangdLspClient.cpp"

using namespace gridlock::core;

/**
 * @brief Verifies that formatMessage encodes valid Content-Length headers and compact JSON.
 */
void TestLspCoordinator::testMessageFraming() {
    QJsonObject dummyPayload;
    dummyPayload["jsonrpc"] = "2.0";
    dummyPayload["id"] = 1;
    dummyPayload["method"] = "initialize";

    // Format message with HTTP-like Content-Length header.
    QByteArray result = LspCoordinator::formatMessage(dummyPayload);

    QByteArray expectedJson = QJsonDocument(dummyPayload).toJson(QJsonDocument::Compact);
    QByteArray expectedHeader = QString("Content-Length: %1\r\n\r\n").arg(expectedJson.size()).toUtf8();
    QByteArray expected = expectedHeader + expectedJson;

    QCOMPARE(result, expected);
}

/**
 * @brief Prepares data matrix for initialization and hover response parsing.
 */
void TestLspCoordinator::testLspParser_data() {
    QTest::addColumn<QString>("rawStandardOutput");
    QTest::addColumn<QString>("expectedExtraction");

    // Row 1 (Initialize response with server capabilities)
    QJsonObject initResult;
    initResult["jsonrpc"] = "2.0";
    initResult["id"] = 1;
    QJsonObject resultObj;
    resultObj["capabilities"] = QJsonObject();
    initResult["result"] = resultObj;
    QByteArray initBytes = LspCoordinator::formatMessage(initResult);

    QTest::newRow("Initialize") << QString::fromUtf8(initBytes) << QString("INITIALIZED");

    // Row 2 (Hover response containing function signature)
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

/**
 * @brief Tests coordinator state changes and hover signal dispatches from raw output.
 */
void TestLspCoordinator::testLspParser() {
    QFETCH(QString, rawStandardOutput);
    QFETCH(QString, expectedExtraction);

    LspCoordinator coordinator;
    QSignalSpy spy(&coordinator, &LspCoordinator::hoverResultReceived);

    if (expectedExtraction == "INITIALIZED") {
        coordinator.processRawOutput(rawStandardOutput.toUtf8());
        QVERIFY(coordinator.isInitialized());
    } else {
        // Initialize first so subsequent requestHover call is not ignored.
        QJsonObject initResult;
        initResult["jsonrpc"] = "2.0";
        initResult["id"] = 1;
        QJsonObject resultObj;
        resultObj["capabilities"] = QJsonObject();
        initResult["result"] = resultObj;
        coordinator.processRawOutput(LspCoordinator::formatMessage(initResult));

        // Advance sequence IDs to match test vector.
        coordinator.requestHover("dummy.cpp", 1, 1, QPoint(10, 10)); // Request ID 1
        coordinator.requestHover("dummy.cpp", 1, 1, QPoint(10, 10)); // Request ID 2

        coordinator.processRawOutput(rawStandardOutput.toUtf8());

        // Verify hover tooltip content extracted.
        QCOMPARE(spy.count(), 1);
        QList<QVariant> arguments = spy.takeFirst();
        QCOMPARE(arguments.at(0).toString(), expectedExtraction);
    }
}

/**
 * @brief Tests reassembly when an LSP response packet is split across chunk boundaries.
 */
void TestLspCoordinator::testFragmentedStream() {
    LspCoordinator coordinator;
    QSignalSpy spy(&coordinator, &LspCoordinator::hoverResultReceived);

    // Initialize coordinator session.
    QJsonObject initResult;
    initResult["jsonrpc"] = "2.0";
    initResult["id"] = 1;
    QJsonObject resultObj;
    resultObj["capabilities"] = QJsonObject();
    initResult["result"] = resultObj;
    coordinator.processRawOutput(LspCoordinator::formatMessage(initResult));

    // Send hover request to expect ID=1.
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

    // Split the stream in half across two writes.
    int splitPoint = fullBytes.size() / 2;
    QByteArray frag1 = fullBytes.left(splitPoint);
    QByteArray frag2 = fullBytes.mid(splitPoint);

    // Fragment 1 should not trigger hover result.
    coordinator.processRawOutput(frag1);
    QCOMPARE(spy.count(), 0);

    // Fragment 2 completes the message and triggers delivery.
    coordinator.processRawOutput(frag2);
    QCOMPARE(spy.count(), 1);

    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toString(), QString("Fragmented Markdown"));
}

/**
 * @brief Verifies hover parsing across object, string, and array content representations.
 */
void TestLspCoordinator::testLspHoverParsing() {
    LspCoordinator coordinator;
    QSignalSpy spy(&coordinator, &LspCoordinator::hoverResultReceived);

    // Initialize coordinator.
    QJsonObject initResult;
    initResult["jsonrpc"] = "2.0";
    initResult["id"] = 1;
    QJsonObject resultObj;
    resultObj["capabilities"] = QJsonObject();
    initResult["result"] = resultObj;
    coordinator.processRawOutput(LspCoordinator::formatMessage(initResult));

    // 1. Object Test (MarkedString / MarkupContent with kind="markdown")
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

    // 2. String Test (raw string contents)
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

    // 3. Array Test (array of strings and objects joined by newlines)
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

    // 4. Verify ClangdLspClient::parsePublishDiagnostics(), analyzeSourceDiagnostics(), & requestHoverInfo().
    // Build a synthetic LSP textDocument/publishDiagnostics JSON notification with an error and warning.
    // Severity 1 represents an LSP Error; Severity 2 represents an LSP Warning.
    const QByteArray publishDiagJson = R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{"uri":"file:///tmp/test.cpp","diagnostics":[{"range":{"start":{"line":4,"character":8},"end":{"line":4,"character":18}},"severity":1,"message":"Cannot initialize a variable of type 'int' with an lvalue of type 'const char[9]'"},{"range":{"start":{"line":6,"character":4},"end":{"line":6,"character":9}},"severity":2,"message":"Unused variable 'unusedVar'"}]}})";
    auto checkDiagnosticsAndHover = [&](auto& client) -> bool {
        using ClientT = std::decay_t<decltype(client)>;
        // Use C++20 requires expression so Gate 1 Red Phase compiles cleanly prior to implementation.
        if constexpr (requires {
                          ClientT::parsePublishDiagnostics(publishDiagJson);
                          client.analyzeSourceDiagnostics(QString(), 2500);
                          client.requestHoverInfo(QString(), 1, 0, 2000);
                      }) {
            // Verify JSON notification parsing into structured 1-based line and column LspDiagnostic records.
            // Confirm both error (line 5, cols 8..18) and warning (line 7) entries are extracted accurately.
            const auto parsed = ClientT::parsePublishDiagnostics(publishDiagJson);
            if (parsed.size() != 2 || parsed[0].line != 5 || parsed[0].column != 8 || parsed[0].endColumn != 18 ||
                parsed[0].severity != 1 || !parsed[0].message.contains(QStringLiteral("Cannot initialize")) ||
                parsed[1].line != 7 || parsed[1].severity != 2) {
                return false;
            }
            // Verify live / AST source diagnostics on an intentional C++ type mismatch error.
            // Line 4 assigns a string literal ("bad_type") to a scalar int variable.
            const QString badSource = QStringLiteral(
                "struct DeeplyNested { int rank_id; double payload; };\n"
                "int main() {\n"
                "    DeeplyNested node{0, 3.14};\n"
                "    int x = \"bad_type\";\n"
                "    return node.rank_id;\n"
                "}\n");
            // Execute analyzeSourceDiagnostics and verify a severity-1 error is reported on line 4.
            const auto liveDiags = client.analyzeSourceDiagnostics(badSource, 2500);
            bool foundLine4Error = false;
            for (const auto& d : liveDiags) {
                if (d.line == 4 && d.severity == 1 && !d.message.isEmpty()) {
                    foundLine4Error = true;
                    break;
                }
            }
            if (!foundLine4Error) return false;
            // Verify rich hover documentation / signature card on struct variable or type token.
            // Query line 3, column 18 ('node' of type 'DeeplyNested') and validate non-empty markdown.
            const QString hoverMarkdown = client.requestHoverInfo(badSource, 3, 18, 2000);
            return !hoverMarkdown.trimmed().isEmpty() &&
                   (hoverMarkdown.contains(QStringLiteral("DeeplyNested")) || hoverMarkdown.contains(QStringLiteral("node")));
        } else {
            return false;
        }
    };
    QVERIFY2(checkDiagnosticsAndHover(ClangdLspClient::instance()),
             "ClangdLspClient must support parsePublishDiagnostics, analyzeSourceDiagnostics, and requestHoverInfo");
}

QTEST_MAIN(TestLspCoordinator)
