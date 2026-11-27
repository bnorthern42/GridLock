/**
 * @file test_mi_parser.cpp
 * @brief Unit tests for GDB/MI output record formatting in GdbConsoleWidget.
 *
 * Verifies stream record filtering (~ console, @ target, & log), result/async
 * record suppression, escaped string unescaping, and raw stream tracing modes.
 */

#include <QFile>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "../../src/core/hpc/gdb/GdbMiParser.hpp"
#include "../../src/core/hpc/gdb/GdbMiTranscriptRecorder.hpp"
#include "../../src/core/hpc/gdb/LanguageExpressionHelper.hpp"
#include "../../src/ui/GdbConsoleWidget.hpp"

using namespace gridlock::ui;

/**
 * @class TestMiParser
 * @brief Test suite validating GdbConsoleWidget MI log formatting and filtering rules.
 */
class TestMiParser : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies formatting of valid stream records (~ console, @ target, & log).
     */
    void testValidStreamRecords() {
        // ~ console record
        QString result1 = GdbConsoleWidget::formatLogEntry(0, "~\"hello\\n\"", false, false);
        QCOMPARE(result1, QString("hello"));

        // @ target record
        QString result2 = GdbConsoleWidget::formatLogEntry(0, "@\"target info\\t\"", false, false);
        QCOMPARE(result2, QString("target info"));

        // & log record
        QString result3 = GdbConsoleWidget::formatLogEntry(0, "&\"log data\"", false, false);
        QCOMPARE(result3, QString("log data"));
    }

    /**
     * @brief Verifies suppression of raw result and async notifications in clean log mode.
     */
    void testInvalidRecordsSuppressed() {
        // ^ result record
        QString result1 = GdbConsoleWidget::formatLogEntry(0, "^done,value=\"0\"", false, false);
        QVERIFY(result1.isEmpty());

        // * async record
        QString result2 = GdbConsoleWidget::formatLogEntry(0, "*stopped,reason=\"exited\"", false, false);
        QVERIFY(result2.isEmpty());

        // = async record
        QString result3 = GdbConsoleWidget::formatLogEntry(0, "=thread-exited", false, false);
        QVERIFY(result3.isEmpty());

        // + async record
        QString result4 = GdbConsoleWidget::formatLogEntry(0, "+download", false, false);
        QVERIFY(result4.isEmpty());

        // Input strings should be suppressed if not showing raw
        QString result5 = GdbConsoleWidget::formatLogEntry(0, "-exec-continue", true, false);
        QVERIFY(result5.isEmpty());
    }

    /**
     * @brief Verifies unescaping of nested quotes, commas, and edge-case log strings.
     */
    void testMaxValueSizeEdgeCase() {
        // Embedded quotes and commas in an error log
        QString errStr = "&\"warning: value requires 140000 bytes, which is more than max-value-size\\n\"";
        QString result = GdbConsoleWidget::formatLogEntry(0, errStr, false, false);
        QCOMPARE(result, QString("warning: value requires 140000 bytes, which is more than max-value-size"));

        // Another variant of error parsing with escaped quotes
        QString complexStr = "~\"value=\\\"embedded\\\", more data\\n\"";
        QString result2 = GdbConsoleWidget::formatLogEntry(0, complexStr, false, false);
        QCOMPARE(result2, QString("value=\"embedded\", more data"));
    }

    /**
     * @brief Verifies prefix tagging when raw input and output logging is enabled.
     */
    void testShowRawToggle() {
        QString inputRaw = GdbConsoleWidget::formatLogEntry(1, "-exec-continue", true, true);
        QCOMPARE(inputRaw, QString("[GDB IN Rank 1]: -exec-continue"));

        QString outputRaw = GdbConsoleWidget::formatLogEntry(2, "^done", false, true);
        QCOMPARE(outputRaw, QString("[GDB OUT Rank 2]: ^done"));
    }

    /**
     * @brief Verifies Fortran vs C/C++ language detection, 1-based slicing, GDB type/bounds parsing,
     *        and column-major vs row-major linear indexing.
     */
    void testFortranLanguageSlicingAndColumnMajorHelpers() {
        using namespace gridlock;

        // 1. Language detection from -gdb-show language and filename fallback.
        QCOMPARE(detectSourceLanguageFromGdbShow(QStringLiteral("fortran")), SourceLanguage::Fortran);
        QCOMPARE(detectSourceLanguageFromGdbShow(QStringLiteral("c++")), SourceLanguage::Cpp);
        QCOMPARE(detectSourceLanguageFromGdbShow(QStringLiteral("c")), SourceLanguage::C);
        QCOMPARE(detectSourceLanguageFromGdbShow(QStringLiteral("auto"), QStringLiteral("/work/heat_solver.f90")),
                 SourceLanguage::Fortran);
        QCOMPARE(detectSourceLanguageFromGdbShow(QStringLiteral("auto"), QStringLiteral("/work/kernel.F90")),
                 SourceLanguage::Fortran);
        QCOMPARE(detectSourceLanguageFromGdbShow(QStringLiteral("auto"), QStringLiteral("/work/mesh.cpp")),
                 SourceLanguage::Cpp);
        QCOMPARE(detectSourceLanguageFromGdbShow(QStringLiteral("The current source language is \"auto; currently fortran\".")),
                 SourceLanguage::Fortran);

        // 2. Fortran 1-based slice expression generation vs C/C++ 0-based slicing.
        QCOMPARE(formatArraySliceExpression(QStringLiteral("vec"), 0, 16, SourceLanguage::Fortran),
                 QStringLiteral("vec(1:16)"));
        QCOMPARE(formatArraySliceExpression(QStringLiteral("vec"), 0, 8, SourceLanguage::Fortran, 2),
                 QStringLiteral("vec(1:15:2)"));
        QCOMPARE(format2DArraySliceExpression(QStringLiteral("mat"), 1, 4, 0, 4, SourceLanguage::Fortran),
                 QStringLiteral("mat(2:5, 1:4)"));
        QCOMPARE(formatArraySliceExpression(QStringLiteral("vec"), 0, 16, SourceLanguage::Cpp),
                 QStringLiteral("vec[0]@16"));
        QCOMPARE(formatArraySliceExpression(QStringLiteral("vec"), 0, 8, SourceLanguage::C, 2),
                 QStringLiteral("vec[0:16:2]"));
        QCOMPARE(formatArrayBaseAddressExpression(QStringLiteral("field"), SourceLanguage::Fortran),
                 QStringLiteral("&(field)"));
        QCOMPARE(formatArrayBaseAddressExpression(QStringLiteral("field"), SourceLanguage::Cpp),
                 QStringLiteral("&(field)[0]"));

        // 3. Fortran GDB type & bounds parsing (default 1-based and explicit lower bounds).
        const auto t1 = parseFortranArrayType(QStringLiteral("real(kind=8) (3,4)"));
        QVERIFY(t1.isFortranArray);
        QCOMPARE(t1.elementType, QStringLiteral("real(kind=8)"));
        QCOMPARE(t1.dims.size(), 2);
        QCOMPARE(t1.dims[0].lowerBound, 1);
        QCOMPARE(t1.dims[0].upperBound, 3);
        QCOMPARE(t1.dims[1].lowerBound, 1);
        QCOMPARE(t1.dims[1].upperBound, 4);
        QCOMPARE(t1.totalElements(), uint64_t{12});

        const auto t2 = parseFortranArrayType(QStringLiteral("integer(kind=4) (0:2, 1:4)"));
        QVERIFY(t2.isFortranArray);
        QCOMPARE(t2.elementType, QStringLiteral("integer(kind=4)"));
        QCOMPARE(t2.dims.size(), 2);
        QCOMPARE(t2.dims[0].lowerBound, 0);
        QCOMPARE(t2.dims[0].upperBound, 2);
        QCOMPARE(t2.dims[0].extent(), 3);
        QCOMPARE(t2.dims[1].lowerBound, 1);
        QCOMPARE(t2.dims[1].upperBound, 4);
        QCOMPARE(t2.dims[1].extent(), 4);
        QCOMPARE(t2.totalElements(), uint64_t{12});

        // 4. Column-major vs row-major N-D element lookup.
        // 3x4 matrix at (row=2, col=1): Fortran col-major = 2 + 1*3 = 5; C row-major = 2*4 + 1 = 9.
        QCOMPARE(computeColumnMajorLinearIndex({2, 1}, {3, 4}), uint64_t{5});
        QCOMPARE(computeRowMajorLinearIndex({2, 1}, {3, 4}), uint64_t{9});
        // 3D (2, 3, 4) at (1, 2, 1): Fortran = 1 + 2*2 + 1*(2*3) = 11; C = 1*(3*4) + 2*4 + 1 = 21.
        QCOMPARE(computeColumnMajorLinearIndex({1, 2, 1}, {2, 3, 4}), uint64_t{11});
        QCOMPARE(computeRowMajorLinearIndex({1, 2, 1}, {2, 3, 4}), uint64_t{21});

        // 5. Fortran 1D and 2D slice suffix parsing.
        QString base;
        int off = 0, cnt = 0, str = 1, rows = 0, cols = 0;
        QVERIFY(parseFortranSliceSuffix(QStringLiteral("arr(2:9:2)"), &base, &off, &cnt, &str, &rows, &cols));
        QCOMPARE(base, QStringLiteral("arr"));
        QCOMPARE(off, 1);
        QCOMPARE(cnt, 4);
        QCOMPARE(str, 2);

        QVERIFY(parseFortranSliceSuffix(QStringLiteral("mat(2:5, 1:4)"), &base, &off, &cnt, &str, &rows, &cols));
        QCOMPARE(base, QStringLiteral("mat"));
        QCOMPARE(rows, 4);
        QCOMPARE(cols, 4);
    }

    /**
     * @brief Verifies balanced-brace / quote-aware GDB/MI3 tuple scanning across nested pretty-printed
     *        locals, multi-arg *stopped frames, and malformed/truncated inputs.
     */
    void testBalancedMiTupleAndFrameScanner() {
        using namespace gridlock::mi;

        // 1. Nested pretty-printed std::vector, std::map, brace-quoted strings, and Fortran derived types.
        const QString varsLine = QStringLiteral(
            "201^done,variables=["
            "{name=\"vec\",type=\"std::vector<int>\",value=\"std::vector of length 3, capacity 4 = {10, 20, 30}\"},"
            "{name=\"m\",type=\"std::map<int, Point>\",value=\"std::map with 2 elements = {[1] = {x = 2}, [3] = {x = 4}}\"},"
            "{name=\"msg\",type=\"const char *\",value=\"\\\"hello {world}\\\"\"},"
            "{name=\"grid\",type=\"Type grid_t\",value=\"( nx = 4, ny = 6, origin = ( 0.0, 0.0 ) )\"}"
            "]");
        bool hasList = false;
        const QString body = extractMiListBody(varsLine, QStringLiteral("variables=["), &hasList);
        QVERIFY(hasList);
        const QVector<QString> tuples = extractTopLevelMiTuples(body);
        QCOMPARE(tuples.size(), 4);

        const auto f0 = parseMiTupleFields(tuples[0]);
        QCOMPARE(f0.value(QStringLiteral("name")), QStringLiteral("vec"));
        QCOMPARE(f0.value(QStringLiteral("type")), QStringLiteral("std::vector<int>"));
        QCOMPARE(f0.value(QStringLiteral("value")), QStringLiteral("std::vector of length 3, capacity 4 = {10, 20, 30}"));

        const auto f1 = parseMiTupleFields(tuples[1]);
        QCOMPARE(f1.value(QStringLiteral("name")), QStringLiteral("m"));
        QCOMPARE(f1.value(QStringLiteral("value")), QStringLiteral("std::map with 2 elements = {[1] = {x = 2}, [3] = {x = 4}}"));

        const auto f2 = parseMiTupleFields(tuples[2]);
        QCOMPARE(f2.value(QStringLiteral("name")), QStringLiteral("msg"));
        QCOMPARE(f2.value(QStringLiteral("value")), QStringLiteral("\"hello {world}\""));

        const auto f3 = parseMiTupleFields(tuples[3]);
        QCOMPARE(f3.value(QStringLiteral("name")), QStringLiteral("grid"));
        QCOMPARE(f3.value(QStringLiteral("value")), QStringLiteral("( nx = 4, ny = 6, origin = ( 0.0, 0.0 ) )"));

        // 2. *stopped frame with nested args=[{...}] BEFORE func, file, fullname, line, addr.
        const QString stopLine = QStringLiteral(
            "*stopped,reason=\"breakpoint-hit\",frame={"
            "args=[{name=\"v\",value=\"{1, 2, 3}\"},{name=\"tag\",value=\"\\\"a}b\\\"\"}],"
            "addr=\"0x401234\",func=\"jacobi_step\",file=\"solver.f90\",fullname=\"/work/solver.f90\",line=\"42\"},thread-id=\"1\"");
        bool hasFrame = false;
        const QString frameBody = extractMiTupleBody(stopLine, QStringLiteral("frame={"), &hasFrame);
        QVERIFY(hasFrame);
        const auto ff = parseMiTupleFields(frameBody);
        QCOMPARE(ff.value(QStringLiteral("addr")), QStringLiteral("0x401234"));
        QCOMPARE(ff.value(QStringLiteral("func")), QStringLiteral("jacobi_step"));
        QCOMPARE(ff.value(QStringLiteral("fullname")), QStringLiteral("/work/solver.f90"));
        QCOMPARE(ff.value(QStringLiteral("line")), QStringLiteral("42"));
        QVERIFY(!ff.contains(QStringLiteral("name")));  // Inner args fields must never leak to top-level frame fields.

        // 3. Malformed/truncated brace and quote inputs terminate safely in linear time.
        bool okTrunc = true;
        QVERIFY(extractMiListBody(QStringLiteral("201^done,variables=[{name=\"a\",value=\"{1, 2"), QStringLiteral("variables=["), &okTrunc).isEmpty());
        QVERIFY(!okTrunc);
        const QVector<QString> partialTuples = extractTopLevelMiTuples(QStringLiteral("{name=\"ok\",value=\"7\"},{name=\"bad\",value=\"unclosed {brace"));
        QCOMPARE(partialTuples.size(), 1);
        QCOMPARE(parseMiTupleFields(partialTuples[0]).value(QStringLiteral("name")), QStringLiteral("ok"));
        const auto unterminatedFields = parseMiTupleFields(QStringLiteral("name=\"x\",value=\"dangling\\"));
        QCOMPARE(unterminatedFields.value(QStringLiteral("name")), QStringLiteral("x"));
        QVERIFY(!unterminatedFields.contains(QStringLiteral("value")));
    }

    void testGdbMiTranscriptRecorderAndSanitizer() {
        using gridlock::core::GdbMiTranscriptRecorder;
        GdbMiTranscriptRecorder::SanitizeContext ctx;
        ctx.homeDir = QStringLiteral("/home/alice");
        ctx.userName = QStringLiteral("alice");
        ctx.hostName = QStringLiteral("hpc-node04");

        // 1. Deterministic redaction preserves valid GDB/MI syntax, types, addresses, and error codes.
        const QString rawStop = QStringLiteral(
            "*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401234\",func=\"solve\","
            "fullname=\"/home/alice/mpi_app/solver.f90\",line=\"42\"},host=\"alice@hpc-node04\"");
        const QString sanStop = GdbMiTranscriptRecorder::sanitizeLine(rawStop, ctx);
        QVERIFY(!sanStop.contains(QStringLiteral("alice")));
        QVERIFY(!sanStop.contains(QStringLiteral("hpc-node04")));
        QVERIFY(sanStop.contains(QStringLiteral("/home/<USER>/mpi_app/solver.f90")));
        QVERIFY(sanStop.contains(QStringLiteral("<USER>@<HOST>")));
        bool hasFrame = false;
        const auto frameFields = gridlock::mi::parseMiTupleFields(
            gridlock::mi::extractMiTupleBody(sanStop, QStringLiteral("frame={"), &hasFrame));
        QVERIFY(hasFrame);
        QCOMPARE(frameFields.value(QStringLiteral("addr")), QStringLiteral("0x401234"));
        QCOMPARE(frameFields.value(QStringLiteral("fullname")), QStringLiteral("/home/<USER>/mpi_app/solver.f90"));
        QCOMPARE(frameFields.value(QStringLiteral("line")), QStringLiteral("42"));

        // Environment variable values in -gdb-set environment and console set environment are redacted.
        QCOMPARE(GdbMiTranscriptRecorder::sanitizeLine(
                     QStringLiteral("42-gdb-set environment SECRET_TOKEN=ghp_supersecret123"), ctx),
                 QStringLiteral("42-gdb-set environment SECRET_TOKEN=<REDACTED>"));
        QCOMPARE(GdbMiTranscriptRecorder::sanitizeLine(
                     QStringLiteral("-gdb-set environment API_KEY my_private_key"), ctx),
                 QStringLiteral("-gdb-set environment API_KEY <REDACTED>"));
        QVERIFY(GdbMiTranscriptRecorder::sanitizeLine(
                    QStringLiteral("-interpreter-exec console \"set environment DB_PASS=hunter2\""), ctx)
                    .contains(QStringLiteral("set environment DB_PASS=<REDACTED>")));
        QVERIFY(GdbMiTranscriptRecorder::sanitizeLine(QStringLiteral("/work/bob/proj/a.cpp"), ctx)
                    .contains(QStringLiteral("/work/<USER>/proj/a.cpp")));

        // 2. Ring-buffer line-count and byte-size eviction keeps only the newest entries.
        GdbMiTranscriptRecorder rec(3u, 4096u);
        rec.setMpiLauncherMode(QStringLiteral("mpi:mpirun (ranks=2)"));
        rec.record(0, false, QStringLiteral("~\"GNU gdb (GDB) 15.2\\n\""));
        rec.record(0, true, QStringLiteral("-gdb-set environment TOKEN=abc123"));
        rec.record(0, false, rawStop);
        rec.record(1, true, QStringLiteral("-stack-list-variables --all-values"));
        QCOMPARE(rec.entryCount(), size_t{3u});

        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        const QString outPath = tmpDir.filePath(QStringLiteral("trace.mi.log"));
        QString err;
        QVERIFY2(rec.exportToFile(outPath, &err, ctx), qPrintable(err));
        QFile f(outPath);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString exported = QString::fromUtf8(f.readAll());
        QVERIFY(exported.contains(QStringLiteral("# GridLock Sanitized GDB/MI Session Trace")));
        QVERIFY(exported.contains(QStringLiteral("# GDB Version: GNU gdb (GDB) 15.2")));
        QVERIFY(exported.contains(QStringLiteral("# MPI Launcher Mode: mpi:mpirun (ranks=2)")));
        QVERIFY(exported.contains(QStringLiteral("[rank 0] -> -gdb-set environment TOKEN=<REDACTED>")));
        QVERIFY(exported.contains(QStringLiteral("[rank 1] -> -stack-list-variables --all-values")));
        QVERIFY(!exported.contains(QStringLiteral("alice")));
        QVERIFY(!exported.contains(QStringLiteral("hpc-node04")));
        QVERIFY(!exported.contains(QStringLiteral("abc123")));

        // Byte-size cap eviction works independently of line count.
        rec.setMaxLimits(100u, 180u);
        for (int i = 0; i < 10; ++i) {
            rec.record(0, false, QStringLiteral("^done,seq=\"%1\",pad=\"0123456789abcdef\"").arg(i));
        }
        QVERIFY(rec.totalBytes() <= 180u && rec.entryCount() >= 1u && rec.entryCount() < 10u);
    }
};

QTEST_MAIN(TestMiParser)
#include "test_mi_parser.moc"
