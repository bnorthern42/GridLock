/**
 * @file test_mi_parser.cpp
 * @brief Unit tests for GDB/MI output record formatting in GdbConsoleWidget.
 *
 * Verifies stream record filtering (~ console, @ target, & log), result/async
 * record suppression, escaped string unescaping, and raw stream tracing modes.
 */

#include <QObject>
#include <QString>
#include <QtTest/QtTest>

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
};

QTEST_MAIN(TestMiParser)
#include "test_mi_parser.moc"
