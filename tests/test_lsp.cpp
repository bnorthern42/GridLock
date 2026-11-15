/**
 * @file test_lsp.cpp
 * @brief Implementation of Language Server Protocol (LSP) coordinator unit tests.
 *
 * Validates JSON-RPC 2.0 Content-Length wire framing, server initialization handshakes,
 * TCP packet fragmentation buffering, and hover tooltip markdown parsing.
 */

#include "test_lsp.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "../src/core/hpc/LspCoordinator.hpp"
#include "../src/core/managers/ConfigManager.hpp"
#include "../src/core/utils/ClangdLspClient.cpp"
#include "../src/ui/views/SourceCodeView.hpp"

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
    //    Validates extraction of `contents.value` from structured MarkupContent payloads.
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
    //    Validates legacy LSP plain-string `contents` payloads.
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
    //    Validates MarkedString[] concatenation across heterogeneous array entries.
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

    // 5. Verify F12 Go-to-Definition, Shift+F12 Find References, & Shift+F6 AST Symbol Rename.
    // Define an in-memory C++ source buffer with struct DeeplyNested, local variable `data`, and comment.
    const QString navSource = QStringLiteral(
        "struct DeeplyNested { int id; double val; };\n"
        "int main() {\n"
        "    // Comment mentioning data should not be corrupted by AST symbol rename.\n"
        "    DeeplyNested data{1, 2.5};\n"
        "    data.id += 10;\n"
        "    return data.id;\n"
        "}\n");
    auto checkDefinitionRefsAndRename = [&](auto& client) -> bool {
        if constexpr (requires {
                          client.findDefinition(navSource, 5, 5, 2000);
                          client.findReferences(navSource, 4, 18, 2000);
                          client.renameSymbolInSource(navSource, 4, 18, QStringLiteral("rank_payload"), 2000);
                      }) {
            // 1. Resolve declaration of `data` used on line 5 (1-based) back to line 4.
            const auto defLoc = client.findDefinition(navSource, 5, 5, 2000);
            if (defLoc.line != 4 || defLoc.column < 4) return false;
            // Also verify resolving declaration of `DeeplyNested` on line 4 back to line 1.
            const auto typeDefLoc = client.findDefinition(navSource, 4, 6, 2000);
            if (typeDefLoc.line != 1) return false;
            // 2. Find all references to `data` across main() (lines 4, 5, and 6).
            const auto refs = client.findReferences(navSource, 4, 18, 2000);
            if (refs.size() < 3) return false;
            // 3. Rename `data` to `rank_payload` and ensure code references update while comments stay intact.
            const QString renamed = client.renameSymbolInSource(navSource, 4, 18, QStringLiteral("rank_payload"), 2000);
            return renamed.contains(QStringLiteral("DeeplyNested rank_payload{1, 2.5};")) &&
                   renamed.contains(QStringLiteral("rank_payload.id += 10;")) &&
                   renamed.contains(QStringLiteral("return rank_payload.id;")) &&
                   renamed.contains(QStringLiteral("// Comment mentioning data should not be corrupted"));
        } else {
            return false;
        }
    };
    QVERIFY2(checkDefinitionRefsAndRename(ClangdLspClient::instance()),
             "ClangdLspClient must support findDefinition, findReferences, and renameSymbolInSource");

    // 6. Verify real cross-file clangd JSON-RPC definition, references, WorkspaceEdit AST rename,
    //    compile_commands.json auto-discovery, and live textDocument/publishDiagnostics emission.
    //    Construct a temporary multi-file C++23 workspace containing:
    //      - `math_ops.hpp` declaring `Vec2::lengthSq()` on 1-based line 5
    //      - `caller.cpp` invoking `v.lengthSq()` alongside string literals and comments
    //      - `builddir/compile_commands.json` specifying the header include directory
    //      - `broken_diag.cpp` triggering an asynchronous `textDocument/publishDiagnostics` error
    QTemporaryDir crossDir;
    QVERIFY(crossDir.isValid());
    const QString headerPath = crossDir.filePath(QStringLiteral("math_ops.hpp"));
    const QString callerPath = crossDir.filePath(QStringLiteral("caller.cpp"));
    const QString brokenPath = crossDir.filePath(QStringLiteral("broken_diag.cpp"));
    const QString buildSubdir = crossDir.filePath(QStringLiteral("builddir"));
    QVERIFY(QDir().mkpath(buildSubdir));

    // Write `math_ops.hpp` declaring `Vec2::lengthSq()` on 1-based line 5 (0-based line 4).
    // This header exercises cross-translation-unit `textDocument/definition` and `WorkspaceEdit` rename.
    const QString headerCode = QStringLiteral(
        "#pragma once\n"
        "struct Vec2 {\n"
        "    double x;\n"
        "    double y;\n"
        "    double lengthSq() const { return x * x + y * y; }\n"
        "};\n");
    {
        QFile hf(headerPath);
        QVERIFY(hf.open(QIODevice::WriteOnly | QIODevice::Text));
        hf.write(headerCode.toUtf8());
    }

    // Write `caller.cpp` calling `v.lengthSq()` with a comment and string literal containing `lengthSq`.
    // Regex-based token replacement would corrupt `"lengthSq"`, whereas clangd AST rename preserves it.
    const QString callerCode = QStringLiteral(
        "#include \"math_ops.hpp\"\n"
        "// lengthSq comment must stay intact\n"
        "int main() {\n"
        "    const char* label = \"lengthSq\";\n"
        "    Vec2 v{3.0, 4.0};\n"
        "    return static_cast<int>(v.lengthSq()) + (label[0] == 'l' ? 0 : 1);\n"
        "}\n");
    {
        QFile cf(callerPath);
        QVERIFY(cf.open(QIODevice::WriteOnly | QIODevice::Text));
        cf.write(callerCode.toUtf8());
    }

    // Write `<root>/builddir/compile_commands.json` to verify `--compile-commands-dir` discovery.
    // ClangdLspClient::discoverCompileCommandsDir() must locate `builddir/compile_commands.json`
    // when given any translation unit inside `<root>`.
    {
        QFile ccf(buildSubdir + QStringLiteral("/compile_commands.json"));
        QVERIFY(ccf.open(QIODevice::WriteOnly | QIODevice::Text));
        const QJsonArray db{QJsonObject{
            {QStringLiteral("directory"), crossDir.path()},
            {QStringLiteral("command"), QStringLiteral("clang++ -std=c++23 -I\"%1\" -c \"%2\"").arg(crossDir.path(), callerPath)},
            {QStringLiteral("file"), callerPath},
        }};
        ccf.write(QJsonDocument(db).toJson());
    }

    // Assert `renameSymbolInSource` never corrupts string literals (`"lengthSq"`) or block comments (`/* lengthSq */`).
    // Single-file regex stubs replace every word-boundary match of `lengthSq` outside `//` comments,
    // corrupting both C-style block comments and C string literals; clangd's AST `textDocument/rename`
    // restricts edits strictly to semantic symbol references (`Box::lengthSq` and `b.lengthSq()`).
    const QString stringLiteralProbe = QStringLiteral(
        "struct Box { int lengthSq() const { return 1; } };\n"
        "int main() {\n"
        "    /* lengthSq block comment */\n"
        "    const char* lit = \"lengthSq\";\n"
        "    Box b;\n"
        "    return b.lengthSq() + (lit[0] == 'l' ? 0 : 1);\n"
        "}\n");
    const QString astRenamedSingle = ClangdLspClient::instance().renameSymbolInSource(
        stringLiteralProbe, 6, 15, QStringLiteral("normSquared"), 3000);
    QVERIFY2(astRenamedSingle.contains(QStringLiteral("b.normSquared()")) &&
                 astRenamedSingle.contains(QStringLiteral("\"lengthSq\"")) &&
                 astRenamedSingle.contains(QStringLiteral("/* lengthSq block comment */")),
             "renameSymbolInSource must use clangd AST WorkspaceEdit so string literals and block comments remain intact");

    // Verify cross-file findDefinition, findReferences, WorkspaceEdit renameSymbol, and publishDiagnostics.
    // Uses C++20 requires-clause so Gate 1 Red Phase compiles cleanly before Gate 2 signatures land,
    // returning false immediately when any cross-file method or signal is missing.
    auto checkCrossFileClangd = [&](auto& client) -> bool {
        using ClientT = std::decay_t<decltype(client)>;
        if constexpr (requires {
                          ClientT::discoverCompileCommandsDir(callerPath);
                          client.findDefinition(callerPath, callerCode, 5, 31, 4000);
                          client.findReferences(callerPath, callerCode, 5, 31, 4000);
                          client.renameSymbol(callerPath, callerCode, 5, 31, QStringLiteral("normSquared"), 4000);
                          client.syncDocument(brokenPath, QString());
                      }) {
            // 1. Auto-discover `<root>/builddir` containing `compile_commands.json`.
            //    Compare canonical paths so `/tmp` symlink differences do not cause false mismatches.
            const QString ccDir = ClientT::discoverCompileCommandsDir(callerPath);
            if (QFileInfo(ccDir).canonicalFilePath() != QFileInfo(buildSubdir).canonicalFilePath()) {
                return false;
            }
            // 2. Resolve `lengthSq` on 0-based line 5, col 31 of `caller.cpp` across files into `math_ops.hpp` line 5.
            //    Verifies `textDocument/definition` parses cross-file target URI and 1-based line number.
            const auto crossDef = client.findDefinition(callerPath, callerCode, 5, 31, 4000);
            if (QFileInfo(crossDef.filePath).fileName() != QStringLiteral("math_ops.hpp") || crossDef.line != 5) {
                return false;
            }
            // 3. Find references across opened header + caller translation units.
            //    Syncs `math_ops.hpp` first so both declaration and call-site references are indexed.
            client.syncDocument(headerPath, headerCode);
            const auto crossRefs = client.findReferences(callerPath, callerCode, 5, 31, 4000);
            if (crossRefs.size() < 2) {
                return false;
            }
            // 4. Cross-file AST rename (`lengthSq` -> `normSquared`) updates both files without touching comments/strings.
            //    Validates bottom-up application of `WorkspaceEdit` across `math_ops.hpp` and `caller.cpp`.
            const auto edits = client.renameSymbol(callerPath, callerCode, 5, 31, QStringLiteral("normSquared"), 4000);
            bool headerUpdated = false;
            bool callerUpdated = false;
            for (auto it = edits.constBegin(); it != edits.constEnd(); ++it) {
                // Check header declaration rename (`double normSquared() const`).
                if (QFileInfo(it.key()).fileName() == QStringLiteral("math_ops.hpp") &&
                    it.value().contains(QStringLiteral("double normSquared() const"))) {
                    headerUpdated = true;
                }
                // Check caller invocation rename while preserving comment and string literal tokens.
                if (QFileInfo(it.key()).fileName() == QStringLiteral("caller.cpp") &&
                    it.value().contains(QStringLiteral("v.normSquared()")) &&
                    it.value().contains(QStringLiteral("// lengthSq comment must stay intact")) &&
                    it.value().contains(QStringLiteral("\"lengthSq\""))) {
                    callerUpdated = true;
                }
            }
            if (!headerUpdated || !callerUpdated) {
                return false;
            }
            // 5. Verify live `textDocument/publishDiagnostics` signal emission on broken C++ buffer.
            //    Confirms `diagnosticsPublished` fires asynchronously with parsed diagnostic entries.
            QSignalSpy diagSpy(&client, &ClientT::diagnosticsPublished);
            client.syncDocument(brokenPath, QStringLiteral("int main() { int bad = \"not_an_int\"; return bad; }\n"));
            if (!diagSpy.wait(4000)) {
                return false;
            }
            return diagSpy.count() >= 1;
        } else {
            return false;
        }
    };
    QVERIFY2(checkCrossFileClangd(ClangdLspClient::instance()),
             "ClangdLspClient must implement cross-file findDefinition, findReferences, renameSymbol, compile_commands discovery, and diagnosticsPublished");

    // 7. Verify zero "QIODevice::write (QProcess): device not open" warnings when invoking
    // LspCoordinator or ClangdLspClient before start or after process exit / closed write channel.
    static QStringList s_capturedWriteWarnings;
    s_capturedWriteWarnings.clear();
    const QtMessageHandler prevHandler = qInstallMessageHandler(
        [](QtMsgType type, const QMessageLogContext&, const QString& msg) {
            if (type == QtWarningMsg &&
                (msg.contains(QStringLiteral("QIODevice::write"), Qt::CaseInsensitive) ||
                 msg.contains(QStringLiteral("device not open"), Qt::CaseInsensitive) ||
                 msg.contains(QStringLiteral("Destroyed while process"), Qt::CaseInsensitive))) {
                s_capturedWriteWarnings.append(msg);
            }
        });

    {
        // Exercise LspCoordinator when pointing to a running process whose QIODevice is closed.
        gridlock::core::ConfigManager::instance().setGlobalClangdPath(QStringLiteral("/bin/cat"));
        LspCoordinator deadLsp;
        deadLsp.start(QStringLiteral("/tmp/nonexistent_test.cpp"));
        QTest::qWait(30);
        if (auto* proc = deadLsp.findChild<QProcess*>()) {
            proc->QIODevice::close();
            deadLsp.didOpen(QStringLiteral("/tmp/nonexistent_test.cpp"), QStringLiteral("int main() {}"));
            deadLsp.requestHover(QStringLiteral("/tmp/nonexistent_test.cpp"), 1, 1, QPoint(0, 0));
        }
        deadLsp.stop();

        // Exercise LspCoordinator with a non-existent binary path so QProcess transitions cleanly.
        gridlock::core::ConfigManager::instance().setGlobalClangdPath(QStringLiteral("/nonexistent/clangd_missing_bin"));
        LspCoordinator missingLsp;
        missingLsp.start(QStringLiteral("/tmp/nonexistent_test.cpp"));
        missingLsp.didOpen(QStringLiteral("/tmp/nonexistent_test.cpp"), QStringLiteral("int main() {}"));
        missingLsp.stop();
        gridlock::core::ConfigManager::instance().setGlobalClangdPath(QStringLiteral("clangd"));

        // Exercise ClangdLspClient JSON-RPC dispatch and fallback paths under warning capture.
        ClangdLspClient::instance().requestHoverInfo(navSource, 4, 18, 50);
    }

    // Restore the default Qt message handler and assert zero QIODevice::write warnings occurred.
    qInstallMessageHandler(prevHandler);
    QVERIFY2(s_capturedWriteWarnings.isEmpty(),
             qPrintable(QStringLiteral("Unexpected QIODevice::write warnings: ") + s_capturedWriteWarnings.join(QStringLiteral(" | "))));

    // 8. Verify ancestor `build_meson/compile_commands.json` discovery, workspace-isolated async
    //    struct member completion (`double ds = params.` -> cx/cy/zCenterStart with `double` detail
    //    and zero `toml::` leakage), and < 35ms non-blocking GUI-thread keystroke + Git diff latency.
    //    Constructs an external C++ workspace layout mirroring `mfem_solver`:
    //      - `<root>/build_meson/compile_commands.json`
    //      - `<root>/include/Params.hpp` (`struct ElectrodeParams { double cx; double cy; double zCenterStart; };`)
    //      - `<root>/ProblemSetup/ElectrodeGenerator.cpp` (`double ds = params.`)
    QTemporaryDir extWsDir;
    QVERIFY(extWsDir.isValid());
    const QString projectRoot = extWsDir.path();
    const QString buildMesonDir = projectRoot + QStringLiteral("/build_meson");
    const QString includeDir = projectRoot + QStringLiteral("/include");
    const QString problemSetupDir = projectRoot + QStringLiteral("/ProblemSetup");
    QVERIFY(QDir().mkpath(buildMesonDir) && QDir().mkpath(includeDir) && QDir().mkpath(problemSetupDir));

    // Write `<root>/include/Params.hpp` defining `ElectrodeParams` with `double` member fields.
    const QString paramsHdrPath = includeDir + QStringLiteral("/Params.hpp");
    const QString electrodeCppPath = problemSetupDir + QStringLiteral("/ElectrodeGenerator.cpp");
    {
        QFile hf(paramsHdrPath);
        QVERIFY(hf.open(QIODevice::WriteOnly | QIODevice::Text));
        hf.write("#pragma once\nstruct ElectrodeParams {\n    double cx;\n    double cy;\n    double zCenterStart;\n};\n");
    }
    // Write `<root>/ProblemSetup/ElectrodeGenerator.cpp` with an incomplete `params.` member access on line 3.
    const QString electrodeCode = QStringLiteral(
        "#include \"Params.hpp\"\n"
        "void run(const ElectrodeParams& params) {\n"
        "    double ds = params.\n"
        "}\n");
    {
        QFile cf(electrodeCppPath);
        QVERIFY(cf.open(QIODevice::WriteOnly | QIODevice::Text));
        cf.write(electrodeCode.toUtf8());
    }
    // Write `<root>/build_meson/compile_commands.json` supplying `-I<root>/include` for `ElectrodeGenerator.cpp`.
    {
        QFile ccf(buildMesonDir + QStringLiteral("/compile_commands.json"));
        QVERIFY(ccf.open(QIODevice::WriteOnly | QIODevice::Text));
        const QJsonArray db{QJsonObject{
            {QStringLiteral("directory"), projectRoot},
            {QStringLiteral("command"),
             QStringLiteral("clang++ -std=c++23 -I\"%1\" -c \"%2\"").arg(includeDir, electrodeCppPath)},
            {QStringLiteral("file"), electrodeCppPath},
        }};
        ccf.write(QJsonDocument(db).toJson());
    }

    // Use C++20 requires-expression so Gate 1 Red Phase compiles before async completion APIs exist.
    auto checkAsyncMemberCompletionAndLatency = [&]<typename ViewT = gridlock::ui::SourceCodeView>(auto& client) -> bool {
        using ClientT = std::decay_t<decltype(client)>;
        if constexpr (requires {
                          client.setWorkspaceRoot(projectRoot);
                          client.requestDocumentCompletionAsync(electrodeCppPath, electrodeCode, 2, 23, QStringLiteral("."));
                          &ClientT::completionReady;
                      }) {
            // 1. Prime ClangdLspClient on GridLock's own repository root first (which indexes tomlplusplus),
            //    then switch workspace root to `projectRoot` and verify `build_meson` ancestor discovery.
            const QString repoRoot = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath() + QStringLiteral("/..")).canonicalPath();
            client.setWorkspaceRoot(repoRoot);
            // Confirm `discoverCompileCommandsDir` walks up from `ProblemSetup/ElectrodeGenerator.cpp` to `build_meson`.
            const QString discoveredMeson = ClientT::discoverCompileCommandsDir(electrodeCppPath);
            if (QFileInfo(discoveredMeson).canonicalFilePath() != QFileInfo(buildMesonDir).canonicalFilePath()) {
                return false;
            }
            // Switch workspace root to `projectRoot` so clangd restarts cleanly without GridLock's `toml::` index.
            client.setWorkspaceRoot(projectRoot);

            // 2. Request async real-document completion right after `double ds = params.` (0-based line 2, col 23).
            //    Verifies `requestDocumentCompletionAsync` operates on the real document URI without splicing
            //    `auto __gl_cond = (...);` and emits `completionReady` with accurate `double` field types.
            QVector<ClangdLspClient::LspCompletionItem> capturedItems;
            quint64 capturedSeq = 0;
            QObject::connect(&client, &ClientT::completionReady, &client,
                             [&](quint64 seq, const QString&, const QVector<ClangdLspClient::LspCompletionItem>& items) {
                                 capturedSeq = seq;
                                 capturedItems = items;
                             });
            QSignalSpy compSpy(&client, &ClientT::completionReady);
            const quint64 reqSeq = client.requestDocumentCompletionAsync(
                electrodeCppPath, electrodeCode, 2, 23, QStringLiteral("."));
            if (reqSeq == 0 || !compSpy.wait(4000) || capturedSeq != reqSeq) {
                return false;
            }
            // Validate that `cx`, `cy`, and `zCenterStart` report `double` detailType (not fabricated `int`)
            // and that zero `toml::` symbols from GridLock's own subprojects leak into the completion list.
            bool hasCx = false, hasCy = false, hasZCenter = false, hasTomlLeak = false;
            for (const auto& item : capturedItems) {
                if (item.label.contains(QStringLiteral("toml"), Qt::CaseInsensitive) ||
                    item.insertText.contains(QStringLiteral("toml"), Qt::CaseInsensitive)) {
                    hasTomlLeak = true;
                }
                if (item.label == QStringLiteral("cx") && item.detailType.contains(QStringLiteral("double")) &&
                    item.detailType != QStringLiteral("int")) {
                    hasCx = true;
                }
                if (item.label == QStringLiteral("cy") && item.detailType.contains(QStringLiteral("double")) &&
                    item.detailType != QStringLiteral("int")) {
                    hasCy = true;
                }
                if (item.label == QStringLiteral("zCenterStart") && item.detailType.contains(QStringLiteral("double")) &&
                    item.detailType != QStringLiteral("int")) {
                    hasZCenter = true;
                }
            }
            if (!hasCx || !hasCy || !hasZCenter || hasTomlLeak) {
                return false;
            }

            // 3. Initialize a Git repo with a 2,000-line C++ file and verify GUI-thread keystroke + Git diff < 35ms.
            //    Ensures neither `SourceCodeView::keyPressEvent` nor `refreshGitDiffGutter()` blocks the GUI thread.
            //    Previously, typing `.` blocked on `readResponseForId(450)` and `refreshGitDiffGutter()` spawned
            //    synchronous `git rev-parse`, `git show`, and `git diff` subprocesses on the Qt GUI thread.
            auto runGitSetup = [&](const QStringList& args) {
                QProcess p;
                p.setWorkingDirectory(projectRoot);
                p.start(QStringLiteral("git"), args);
                p.waitForFinished(3000);
            };
            // Initialize the temporary repository and configure commit identity for deterministic `git diff HEAD`.
            runGitSetup({QStringLiteral("init")});
            runGitSetup({QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("test@gridlock.dev")});
            runGitSetup({QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("GridLock Test")});
            // Populate a 2,000-line translation unit and commit it to HEAD so `git diff` / `git show` are active.
            // This 2,000-line buffer also ensures structural regex completion does not stall the GUI thread.
            QString largeCode = QStringLiteral("#include \"Params.hpp\"\nvoid largeFn(const ElectrodeParams& params) {\n");
            for (int i = 0; i < 2000; ++i) {
                largeCode += QStringLiteral("    double v_%1 = params.cx + %1.0;\n").arg(i);
            }
            largeCode += QStringLiteral("    double ds = params\n}\n");
            {
                QFile lf(electrodeCppPath);
                if (!lf.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
                lf.write(largeCode.toUtf8());
            }
            // Stage and commit the 2,000-line file so subsequent in-memory buffer edits diff against HEAD.
            runGitSetup({QStringLiteral("add"), QStringLiteral(".")});
            runGitSetup({QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("init")});

            // Open the 2,000-line file in SourceCodeView and position the cursor at the end of `double ds = params`.
            // Move the cursor to the penultimate block's end column right before typing the `.` member operator.
            ViewT editor;
            editor.setPlainText(largeCode);
            editor.setCurrentFile(electrodeCppPath);
            QTextCursor cur = editor.textCursor();
            cur.movePosition(QTextCursor::End);
            cur.movePosition(QTextCursor::PreviousBlock);
            cur.movePosition(QTextCursor::EndOfBlock);
            editor.setTextCursor(cur);

            // Measure synchronous GUI-thread elapsed time for typing `.` and invoking `refreshGitDiffGutter()`.
            // Both operations must dispatch asynchronously and return to the GUI event loop in under 35ms.
            QElapsedTimer guiTimer;
            guiTimer.start();
            QKeyEvent dotKey(QEvent::KeyPress, Qt::Key_Period, Qt::NoModifier, QStringLiteral("."));
            QCoreApplication::sendEvent(&editor, &dotKey);
            editor.refreshGitDiffGutter();
            const qint64 elapsedMs = guiTimer.elapsed();
            return elapsedMs < 35;
        } else {
            return false;
        }
    };
    QVERIFY2(checkAsyncMemberCompletionAndLatency(ClangdLspClient::instance()),
             "ClangdLspClient + SourceCodeView must discover build_meson, isolate workspace completions without toml/int leaks, and run keystrokes + Git diff in < 35ms on the GUI thread");
}

QTEST_MAIN(TestLspCoordinator)
