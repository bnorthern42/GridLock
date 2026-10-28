/**
 * @file test_cliparser.cpp
 * @brief Unit test fixture for command-line argument parser (CliParser).
 *
 * Verifies parsing of interactive, verbose, test-mode, tutorial-mode, and
 * headless CLI arguments, including early offscreen platform detection.
 */

#include <QCoreApplication>
#include <QtTest>
#include "../src/core/utils/CliParser.hpp"

/**
 * @class TestCliParser
 * @brief QtTest suite verifying CliParser command-line flag handling.
 */
class TestCliParser : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies default flag values when no extra CLI options are supplied.
     */
    void testEmptyArgs() {
        gridlock::core::utils::CliParser parser;
        QStringList args = {"app_name"};
        // Assert parse succeeds on basic executable name.
        QVERIFY(parser.parse(args));

        // Verify that all special operational modes default to false/empty.
        QVERIFY(!parser.isTutorialMode());
        QVERIFY(!parser.isTestMode());
        QVERIFY(!parser.isVerbose());
        QVERIFY(parser.getTestFilePath().isEmpty());
    }

    /**
     * @brief Verifies that `--tutorial-mode` enables interactive tutorial workflow.
     */
    void testTutorialMode() {
        gridlock::core::utils::CliParser parser;
        QStringList args = {"app_name", "--tutorial-mode"};
        QVERIFY(parser.parse(args));
        QVERIFY(parser.isTutorialMode());
    }

    /**
     * @brief Verifies that `--test-mode` activates automated test execution routing.
     */
    void testTestMode() {
        gridlock::core::utils::CliParser parser;
        QStringList args = {"app_name", "--test-mode"};
        QVERIFY(parser.parse(args));
        QVERIFY(parser.isTestMode());
    }

    /**
     * @brief Verifies that `--verbose` enables diagnostic debug console logging.
     */
    void testVerboseMode() {
        gridlock::core::utils::CliParser parser;
        QStringList args = {"app_name", "--verbose"};
        QVERIFY(parser.parse(args));
        QVERIFY(parser.isVerbose());
    }

    /**
     * @brief Verifies that positional file arguments are correctly extracted as target path.
     */
    void testPositionalArgument() {
        gridlock::core::utils::CliParser parser;
        QStringList args = {"app_name", "some_file.cpp"};
        QVERIFY(parser.parse(args));
        QCOMPARE(parser.getTestFilePath(), QString("some_file.cpp"));
    }

    /**
     * @brief Verifies concurrent combinations of multiple flags and positional arguments.
     */
    void testMultipleOptions() {
        gridlock::core::utils::CliParser parser;
        QStringList args = {"app_name", "--verbose", "--test-mode", "another_file.txt"};
        QVERIFY(parser.parse(args));
        QVERIFY(parser.isVerbose());
        QVERIFY(parser.isTestMode());
        QVERIFY(!parser.isTutorialMode());
        QCOMPARE(parser.getTestFilePath(), QString("another_file.txt"));
    }

    /**
     * @brief Verifies `--headless` and `--snapshot` flags and offscreen platform detection.
     */
    void testHeadlessFlagAndNormalLaunchDefault() {
        // Normal interactive launch must not be headless by default.
        {
            gridlock::core::utils::CliParser parser;
            QStringList args = {"gridlock"};
            QVERIFY(parser.parse(args));
            QVERIFY(!parser.isHeadless());
        }
        // Explicit --headless flag must report isHeadless() == true.
        {
            gridlock::core::utils::CliParser parser;
            QStringList args = {"gridlock", "--headless"};
            QVERIFY(parser.parse(args));
            QVERIFY(parser.isHeadless());
        }
        // Snapshot generation mode implies headless execution.
        {
            gridlock::core::utils::CliParser parser;
            QStringList args = {"gridlock", "--snapshot", "/tmp/out.png"};
            QVERIFY(parser.parse(args));
            QVERIFY(parser.isHeadless());
        }
        // Early offscreen platform check before QApplication construction.
        {
            char arg0[] = "gridlock";
            char* argvNormal[] = {arg0, nullptr};
            QVERIFY(!gridlock::core::utils::CliParser::shouldUseOffscreenPlatform(1, argvNormal));

            char argHeadless[] = "--headless";
            char* argvHeadless[] = {arg0, argHeadless, nullptr};
            QVERIFY(gridlock::core::utils::CliParser::shouldUseOffscreenPlatform(2, argvHeadless));

            char argSnapshot[] = "--snapshot";
            char argSnapPath[] = "/tmp/out.png";
            char* argvSnapshot[] = {arg0, argSnapshot, argSnapPath, nullptr};
            QVERIFY(gridlock::core::utils::CliParser::shouldUseOffscreenPlatform(3, argvSnapshot));
        }
    }
};

QTEST_MAIN(TestCliParser)
#include "test_cliparser.moc"
