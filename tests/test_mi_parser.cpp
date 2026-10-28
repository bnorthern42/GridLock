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

#include "../src/ui/GdbConsoleWidget.hpp"

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
};

QTEST_MAIN(TestMiParser)
#include "test_mi_parser.moc"
