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

#include "../../src/core/hpc/lsp/LspCoordinator.hpp"
#include "../../src/core/managers/ConfigManager.hpp"
#include "../../src/core/utils/ClangdLspClient.cpp"
#include "../../src/ui/views/SourceCodeView.hpp"

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

    // 4. Verify zero "QIODevice::write (QProcess): device not open" warnings on closed/missing LSP pipes.
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
        gridlock::core::ConfigManager::instance().setGlobalClangdPath(QStringLiteral("/nonexistent/clangd_missing_bin"));
        LspCoordinator missingLsp;
        missingLsp.start(QStringLiteral("/tmp/nonexistent_test.cpp"));
        missingLsp.didOpen(QStringLiteral("/tmp/nonexistent_test.cpp"), QStringLiteral("int main() {}"));
        missingLsp.stop();
        gridlock::core::ConfigManager::instance().setGlobalClangdPath(QStringLiteral("clangd"));
    }
    qInstallMessageHandler(prevHandler);
    QVERIFY2(s_capturedWriteWarnings.isEmpty(),
             qPrintable(QStringLiteral("Unexpected QIODevice::write warnings: ") + s_capturedWriteWarnings.join(QStringLiteral(" | "))));
}

void TestLspCoordinator::testClangdDiagnosticsAndCompletion() {
    auto& client = ClangdLspClient::instance();
    const QByteArray publishDiagJson = R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{"uri":"file:///tmp/test.cpp","diagnostics":[{"range":{"start":{"line":4,"character":8},"end":{"line":4,"character":18}},"severity":1,"message":"Cannot initialize a variable of type 'int' with an lvalue of type 'const char[9]'"},{"range":{"start":{"line":6,"character":4},"end":{"line":6,"character":9}},"severity":2,"message":"Unused variable 'unusedVar'"}]}})";
    const auto parsed = ClangdLspClient::parsePublishDiagnostics(publishDiagJson);
    QVERIFY(parsed.size() == 2 && parsed[0].line == 5 && parsed[0].column == 8 && parsed[0].endColumn == 18 &&
            parsed[0].severity == 1 && parsed[0].message.contains(QStringLiteral("Cannot initialize")) &&
            parsed[1].line == 7 && parsed[1].severity == 2);

    const QString badSource = QStringLiteral(
        "struct DeeplyNested { int rank_id; double payload; };\n"
        "int main() {\n"
        "    DeeplyNested node{0, 3.14};\n"
        "    int x = \"bad_type\";\n"
        "    return node.rank_id;\n"
        "}\n");
    const auto liveDiags = client.analyzeSourceDiagnostics(badSource, 2500);
    bool foundLine4Error = false;
    for (const auto& d : liveDiags) {
        if (d.line == 4 && d.severity == 1 && !d.message.isEmpty()) { foundLine4Error = true; break; }
    }
    QVERIFY(foundLine4Error);

    const QString hoverMarkdown = client.requestHoverInfo(badSource, 3, 18, 2000);
    QVERIFY(!hoverMarkdown.trimmed().isEmpty() &&
            (hoverMarkdown.contains(QStringLiteral("DeeplyNested")) || hoverMarkdown.contains(QStringLiteral("node"))));

    // Verify ancestor build_meson/compile_commands.json discovery, isolated async completion, and <35ms GUI latency.
    QTemporaryDir extWsDir; QVERIFY(extWsDir.isValid());
    const QString projectRoot = extWsDir.path();
    const QString buildMesonDir = projectRoot + QStringLiteral("/build_meson");
    const QString includeDir = projectRoot + QStringLiteral("/include");
    const QString problemSetupDir = projectRoot + QStringLiteral("/ProblemSetup");
    QVERIFY(QDir().mkpath(buildMesonDir) && QDir().mkpath(includeDir) && QDir().mkpath(problemSetupDir));

    const QString paramsHdrPath = includeDir + QStringLiteral("/Params.hpp");
    const QString electrodeCppPath = problemSetupDir + QStringLiteral("/ElectrodeGenerator.cpp");
    { QFile hf(paramsHdrPath); QVERIFY(hf.open(QIODevice::WriteOnly | QIODevice::Text)); hf.write("#pragma once\nstruct ElectrodeParams {\n    double cx;\n    double cy;\n    double zCenterStart;\n};\n"); }
    const QString electrodeCode = QStringLiteral("#include \"Params.hpp\"\nvoid run(const ElectrodeParams& params) {\n    double ds = params.\n}\n");
    { QFile cf(electrodeCppPath); QVERIFY(cf.open(QIODevice::WriteOnly | QIODevice::Text)); cf.write(electrodeCode.toUtf8()); }
    {
        QFile ccf(buildMesonDir + QStringLiteral("/compile_commands.json"));
        QVERIFY(ccf.open(QIODevice::WriteOnly | QIODevice::Text));
        const QJsonArray db{QJsonObject{{QStringLiteral("directory"), projectRoot}, {QStringLiteral("command"), QStringLiteral("clang++ -std=c++23 -I\"%1\" -c \"%2\"").arg(includeDir, electrodeCppPath)}, {QStringLiteral("file"), electrodeCppPath}}};
        ccf.write(QJsonDocument(db).toJson());
    }

    const QString repoRoot = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath() + QStringLiteral("/..")).canonicalPath();
    client.setWorkspaceRoot(repoRoot);
    QCOMPARE(QFileInfo(ClangdLspClient::discoverCompileCommandsDir(electrodeCppPath)).canonicalFilePath(),
             QFileInfo(buildMesonDir).canonicalFilePath());
    client.setWorkspaceRoot(projectRoot);

    QVector<ClangdLspClient::LspCompletionItem> capturedItems;
    quint64 capturedSeq = 0;
    QObject::connect(&client, &ClangdLspClient::completionReady, &client,
                     [&](quint64 seq, const QString&, const QVector<ClangdLspClient::LspCompletionItem>& items) {
                         capturedSeq = seq;
                         capturedItems = items;
                     });
    QSignalSpy compSpy(&client, &ClangdLspClient::completionReady);
    const quint64 reqSeq = client.requestDocumentCompletionAsync(electrodeCppPath, electrodeCode, 2, 23, QStringLiteral("."));
    QVERIFY(reqSeq != 0 && compSpy.wait(4000) && capturedSeq == reqSeq);

    bool hasCx = false, hasCy = false, hasZCenter = false, hasTomlLeak = false;
    for (const auto& item : capturedItems) {
        if (item.label.contains(QStringLiteral("toml"), Qt::CaseInsensitive) || item.insertText.contains(QStringLiteral("toml"), Qt::CaseInsensitive)) hasTomlLeak = true;
        if (item.label == QStringLiteral("cx") && item.detailType.contains(QStringLiteral("double"))) hasCx = true;
        if (item.label == QStringLiteral("cy") && item.detailType.contains(QStringLiteral("double"))) hasCy = true;
        if (item.label == QStringLiteral("zCenterStart") && item.detailType.contains(QStringLiteral("double"))) hasZCenter = true;
    }
    QVERIFY(hasCx && hasCy && hasZCenter && !hasTomlLeak);

    auto runGitSetup = [&](const QStringList& args) { QProcess p; p.setWorkingDirectory(projectRoot); p.start(QStringLiteral("git"), args); p.waitForFinished(3000); };
    runGitSetup({QStringLiteral("init")});
    runGitSetup({QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("test@gridlock.dev")});
    runGitSetup({QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("GridLock Test")});
    QString largeCode = QStringLiteral("#include \"Params.hpp\"\nvoid largeFn(const ElectrodeParams& params) {\n");
    for (int i = 0; i < 2000; ++i) largeCode += QStringLiteral("    double v_%1 = params.cx + %1.0;\n").arg(i);
    largeCode += QStringLiteral("    double ds = params\n}\n");
    { QFile lf(electrodeCppPath); QVERIFY(lf.open(QIODevice::WriteOnly | QIODevice::Text)); lf.write(largeCode.toUtf8()); }
    runGitSetup({QStringLiteral("add"), QStringLiteral(".")});
    runGitSetup({QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("init")});

    gridlock::ui::SourceCodeView editor;
    editor.setPlainText(largeCode);
    editor.setCurrentFile(electrodeCppPath);
    QTextCursor cur = editor.textCursor();
    cur.movePosition(QTextCursor::End);
    cur.movePosition(QTextCursor::PreviousBlock);
    cur.movePosition(QTextCursor::EndOfBlock);
    editor.setTextCursor(cur);

    QElapsedTimer guiTimer;
    guiTimer.start();
    QKeyEvent dotKey(QEvent::KeyPress, Qt::Key_Period, Qt::NoModifier, QStringLiteral("."));
    QCoreApplication::sendEvent(&editor, &dotKey);
    editor.refreshGitDiffGutter();
    QVERIFY2(guiTimer.elapsed() < 35, "Keystroke + Git diff dispatch must stay < 35ms on GUI thread");
}

void TestLspCoordinator::testClangdNavigationAndWorkspaceRename() {
    auto& client = ClangdLspClient::instance();
    const QString navSource = QStringLiteral(
        "struct DeeplyNested { int id; double val; };\n"
        "int main() {\n"
        "    // Comment mentioning data should not be corrupted by AST symbol rename.\n"
        "    DeeplyNested data{1, 2.5};\n"
        "    data.id += 10;\n"
        "    return data.id;\n"
        "}\n");
    const auto defLoc = client.findDefinition(navSource, 5, 5, 2000);
    QVERIFY(defLoc.line == 4 && defLoc.column >= 4);
    QCOMPARE(client.findDefinition(navSource, 4, 6, 2000).line, 1);
    QVERIFY(client.findReferences(navSource, 4, 18, 2000).size() >= 3);
    const QString renamed = client.renameSymbolInSource(navSource, 4, 18, QStringLiteral("rank_payload"), 2000);
    QVERIFY(renamed.contains(QStringLiteral("DeeplyNested rank_payload{1, 2.5};")) &&
            renamed.contains(QStringLiteral("rank_payload.id += 10;")) &&
            renamed.contains(QStringLiteral("return rank_payload.id;")) &&
            renamed.contains(QStringLiteral("// Comment mentioning data should not be corrupted")));

    const QString stringLiteralProbe = QStringLiteral(
        "struct Box { int lengthSq() const { return 1; } };\n"
        "int main() {\n"
        "    /* lengthSq block comment */\n"
        "    const char* lit = \"lengthSq\";\n"
        "    Box b;\n"
        "    return b.lengthSq() + (lit[0] == 'l' ? 0 : 1);\n"
        "}\n");
    const QString astRenamedSingle = client.renameSymbolInSource(stringLiteralProbe, 6, 15, QStringLiteral("normSquared"), 3000);
    QVERIFY2(astRenamedSingle.contains(QStringLiteral("b.normSquared()")) &&
                 astRenamedSingle.contains(QStringLiteral("\"lengthSq\"")) &&
                 astRenamedSingle.contains(QStringLiteral("/* lengthSq block comment */")),
             "renameSymbolInSource must preserve string literals and block comments");

    // Cross-file definition, references, WorkspaceEdit rename, and publishDiagnostics.
    QTemporaryDir crossDir; QVERIFY(crossDir.isValid());
    const QString headerPath = crossDir.filePath(QStringLiteral("math_ops.hpp"));
    const QString callerPath = crossDir.filePath(QStringLiteral("caller.cpp"));
    const QString brokenPath = crossDir.filePath(QStringLiteral("broken_diag.cpp"));
    const QString buildSubdir = crossDir.filePath(QStringLiteral("builddir"));
    QVERIFY(QDir().mkpath(buildSubdir));

    const QString headerCode = QStringLiteral("#pragma once\nstruct Vec2 {\n    double x;\n    double y;\n    double lengthSq() const { return x * x + y * y; }\n};\n");
    const QString callerCode = QStringLiteral("#include \"math_ops.hpp\"\n// lengthSq comment must stay intact\nint main() {\n    const char* label = \"lengthSq\";\n    Vec2 v{3.0, 4.0};\n    return static_cast<int>(v.lengthSq()) + (label[0] == 'l' ? 0 : 1);\n}\n");
    { QFile hf(headerPath); QVERIFY(hf.open(QIODevice::WriteOnly | QIODevice::Text)); hf.write(headerCode.toUtf8()); }
    { QFile cf(callerPath); QVERIFY(cf.open(QIODevice::WriteOnly | QIODevice::Text)); cf.write(callerCode.toUtf8()); }
    {
        QFile ccf(buildSubdir + QStringLiteral("/compile_commands.json"));
        QVERIFY(ccf.open(QIODevice::WriteOnly | QIODevice::Text));
        const QJsonArray db{QJsonObject{{QStringLiteral("directory"), crossDir.path()}, {QStringLiteral("command"), QStringLiteral("clang++ -std=c++23 -I\"%1\" -c \"%2\"").arg(crossDir.path(), callerPath)}, {QStringLiteral("file"), callerPath}}};
        ccf.write(QJsonDocument(db).toJson());
    }

    QCOMPARE(QFileInfo(ClangdLspClient::discoverCompileCommandsDir(callerPath)).canonicalFilePath(),
             QFileInfo(buildSubdir).canonicalFilePath());
    const auto crossDef = client.findDefinition(callerPath, callerCode, 5, 31, 4000);
    QVERIFY(QFileInfo(crossDef.filePath).fileName() == QStringLiteral("math_ops.hpp") && crossDef.line == 5);
    client.syncDocument(headerPath, headerCode);
    QVERIFY(client.findReferences(callerPath, callerCode, 5, 31, 4000).size() >= 2);

    const auto edits = client.renameSymbol(callerPath, callerCode, 5, 31, QStringLiteral("normSquared"), 4000);
    bool headerUpdated = false, callerUpdated = false;
    for (auto it = edits.constBegin(); it != edits.constEnd(); ++it) {
        if (QFileInfo(it.key()).fileName() == QStringLiteral("math_ops.hpp") && it.value().contains(QStringLiteral("double normSquared() const"))) headerUpdated = true;
        if (QFileInfo(it.key()).fileName() == QStringLiteral("caller.cpp") && it.value().contains(QStringLiteral("v.normSquared()")) &&
            it.value().contains(QStringLiteral("// lengthSq comment must stay intact")) && it.value().contains(QStringLiteral("\"lengthSq\""))) callerUpdated = true;
    }
    QVERIFY(headerUpdated && callerUpdated);

    QSignalSpy diagSpy(&client, &ClangdLspClient::diagnosticsPublished);
    client.syncDocument(brokenPath, QStringLiteral("int main() { int bad = \"not_an_int\"; return bad; }\n"));
    QVERIFY(diagSpy.wait(4000) && diagSpy.count() >= 1);
}

void TestLspCoordinator::testBoundedJsonRpcFramerAndCompileCommands() {
    // 1. Direct JsonRpcFramer edge cases: Content-Length: 0, multi-chunk split, back-to-back, oversized rejection, buffer cap.
    JsonRpcFramer framer(1024, 2048);
    const QByteArray frameA = JsonRpcFramer::encode(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 1}});
    const QByteArray frameB = JsonRpcFramer::encode(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 2}});

    // Content-Length: 0 must advance cleanly without deadlocking subsequent frames.
    const QByteArray zeroAndTwo = QByteArray("Content-Length: 0\r\n\r\n") + frameA + frameB;
    const auto resZero = framer.feed(zeroAndTwo.left(12));
    QVERIFY(resZero.payloads.isEmpty() && resZero.errors.isEmpty());
    const auto resRest = framer.feed(zeroAndTwo.mid(12));
    QCOMPARE(resRest.payloads.size(), 2);
    QVERIFY(resRest.errors.isEmpty());
    QCOMPARE(QJsonDocument::fromJson(resRest.payloads[0]).object().value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(QJsonDocument::fromJson(resRest.payloads[1]).object().value(QStringLiteral("id")).toInt(), 2);
    QCOMPARE(framer.bufferedBytes(), 0);

    // Oversized Content-Length must reject cleanly, reset buffer to 0, and allow subsequent valid frames.
    const auto resOversized = framer.feed("Content-Length: 99999999\r\n\r\n{\"partial\":");
    QVERIFY(resOversized.payloads.isEmpty());
    QCOMPARE(resOversized.errors.size(), 1);
    QCOMPARE(framer.bufferedBytes(), 0);
    const auto resRecover = framer.feed(frameA);
    QCOMPARE(resRecover.payloads.size(), 1);
    QVERIFY(resRecover.errors.isEmpty());

    // Negative/malformed Content-Length discards only the bad header block and continues.
    const auto resBadHdr = framer.feed(QByteArray("Content-Length: -5\r\n\r\n") + frameB);
    QCOMPARE(resBadHdr.errors.size(), 1);
    QCOMPARE(resBadHdr.payloads.size(), 1);
    QCOMPARE(QJsonDocument::fromJson(resBadHdr.payloads[0]).object().value(QStringLiteral("id")).toInt(), 2);

    // Raw chunk exceeding rolling buffer limit is rejected without accumulating unbounded memory.
    const auto resBufCap = framer.feed(QByteArray(3000, 'X'));
    QVERIFY(resBufCap.payloads.isEmpty());
    QCOMPARE(resBufCap.errors.size(), 1);
    QCOMPARE(framer.bufferedBytes(), 0);

    // 2. LspCoordinator end-to-end Content-Length: 0 + oversized rejection with protocolError signal.
    LspCoordinator coord;
    QSignalSpy errSpy(&coord, &LspCoordinator::protocolError);
    QJsonObject initResult;
    initResult["jsonrpc"] = "2.0";
    initResult["id"] = 1;
    initResult["result"] = QJsonObject{{"capabilities", QJsonObject()}};
    coord.processRawOutput(QByteArray("Content-Length: 0\r\n\r\n") + LspCoordinator::formatMessage(initResult));
    QVERIFY(coord.isInitialized());
    QCOMPARE(errSpy.count(), 0);
    coord.processRawOutput("Content-Length: 33554432\r\n\r\n");
    QCOMPARE(errSpy.count(), 1);
    QCOMPARE(coord.bufferedBytes(), 0);

    // 3. LspCoordinator::resolveCompileCommandsDir precedence: explicit -> builddir/ -> build/ -> root -> missing.
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());
    const QString root = tmpRoot.path();
    const QString srcFile = root + QStringLiteral("/src/main.cpp");
    const QString builddirPath = root + QStringLiteral("/builddir");
    const QString buildPath = root + QStringLiteral("/build");
    const QString customPath = root + QStringLiteral("/custom_cc");
    QVERIFY(QDir().mkpath(root + QStringLiteral("/src")) && QDir().mkpath(builddirPath) &&
            QDir().mkpath(buildPath) && QDir().mkpath(customPath));
    auto touch = [](const QString& path) {
        QFile f(path);
        const bool ok = f.open(QIODevice::WriteOnly | QIODevice::Text) && f.write("[]") == 2;
        f.close();
        return ok;
    };
    QVERIFY(touch(srcFile));

    // Missing compile_commands.json anywhere in tmpRoot returns empty string (unless an ancestor on host has one, check isolated dir only when none).
    const QString missingResult = LspCoordinator::resolveCompileCommandsDir(srcFile);
    QVERIFY(missingResult.isEmpty() || !missingResult.startsWith(root));

    // Root fallback.
    QVERIFY(touch(root + QStringLiteral("/compile_commands.json")));
    QCOMPARE(QFileInfo(LspCoordinator::resolveCompileCommandsDir(srcFile)).canonicalFilePath(),
             QFileInfo(root).canonicalFilePath());

    // CMake build/ takes precedence over root.
    QVERIFY(touch(buildPath + QStringLiteral("/compile_commands.json")));
    QCOMPARE(QFileInfo(LspCoordinator::resolveCompileCommandsDir(srcFile)).canonicalFilePath(),
             QFileInfo(buildPath).canonicalFilePath());

    // Meson builddir/ takes precedence over build/.
    QVERIFY(touch(builddirPath + QStringLiteral("/compile_commands.json")));
    QCOMPARE(QFileInfo(LspCoordinator::resolveCompileCommandsDir(srcFile)).canonicalFilePath(),
             QFileInfo(builddirPath).canonicalFilePath());

    // Explicit override takes precedence over builddir/.
    QVERIFY(touch(customPath + QStringLiteral("/compile_commands.json")));
    coord.setCompileCommandsDir(customPath);
    QCOMPARE(QFileInfo(LspCoordinator::resolveCompileCommandsDir(srcFile, coord.compileCommandsDir())).canonicalFilePath(),
             QFileInfo(customPath).canonicalFilePath());
}

QTEST_MAIN(TestLspCoordinator)
