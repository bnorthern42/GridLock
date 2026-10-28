#include <QtTest>
#include <QCoreApplication>
#include "../src/core/utils/CliParser.hpp"

class TestCliParser : public QObject {
    Q_OBJECT

private slots:
    void testEmptyArgs() {
        gridlock::core::utils::CliParser parser;
        QStringList args = {"app_name"};
        QVERIFY(parser.parse(args));

        QVERIFY(!parser.isTutorialMode());
        QVERIFY(!parser.isTestMode());
        QVERIFY(!parser.isVerbose());
        QVERIFY(parser.getTestFilePath().isEmpty());
    }

    void testTutorialMode() {
        gridlock::core::utils::CliParser parser;
        QStringList args = {"app_name", "--tutorial-mode"};
        QVERIFY(parser.parse(args));
        QVERIFY(parser.isTutorialMode());
    }

    void testTestMode() {
        gridlock::core::utils::CliParser parser;
        QStringList args = {"app_name", "--test-mode"};
        QVERIFY(parser.parse(args));
        QVERIFY(parser.isTestMode());
    }

    void testVerboseMode() {
        gridlock::core::utils::CliParser parser;
        QStringList args = {"app_name", "--verbose"};
        QVERIFY(parser.parse(args));
        QVERIFY(parser.isVerbose());
    }

    void testPositionalArgument() {
        gridlock::core::utils::CliParser parser;
        QStringList args = {"app_name", "some_file.cpp"};
        QVERIFY(parser.parse(args));
        QCOMPARE(parser.getTestFilePath(), QString("some_file.cpp"));
    }

    void testMultipleOptions() {
        gridlock::core::utils::CliParser parser;
        QStringList args = {"app_name", "--verbose", "--test-mode", "another_file.txt"};
        QVERIFY(parser.parse(args));
        QVERIFY(parser.isVerbose());
        QVERIFY(parser.isTestMode());
        QVERIFY(!parser.isTutorialMode());
        QCOMPARE(parser.getTestFilePath(), QString("another_file.txt"));
    }

    void testHeadlessFlagAndNormalLaunchDefault() {
        {
            gridlock::core::utils::CliParser parser;
            QStringList args = {"gridlock"};
            QVERIFY(parser.parse(args));
            QVERIFY(!parser.isHeadless());
        }
        {
            gridlock::core::utils::CliParser parser;
            QStringList args = {"gridlock", "--headless"};
            QVERIFY(parser.parse(args));
            QVERIFY(parser.isHeadless());
        }
        {
            gridlock::core::utils::CliParser parser;
            QStringList args = {"gridlock", "--snapshot", "/tmp/out.png"};
            QVERIFY(parser.parse(args));
            QVERIFY(parser.isHeadless());
        }
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
