#include <QTest>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QSignalSpy>

// We will mock or include the models here
#include "../src/ui/tutorial/TutorialDialog.hpp"
#include "../src/core/utils/CliParser.hpp"
#include "../src/core/managers/ConfigManager.hpp"
#include <QFileInfo>
#include <QDir>
#include <QSet>

class TestTutorial : public QObject {
    Q_OBJECT

private slots:
    void testCliParserTutorialMode() {
        QStringList args = {"app", "--tutorial-mode"};
        gridlock::core::utils::CliParser parser;
        QVERIFY(parser.parse(args));
        QVERIFY(parser.isTutorialMode());
        QVERIFY(!parser.isTestMode());
        QVERIFY(parser.getTestFilePath().isEmpty());
    }

    void testCliParserTestModeWithFile() {
        QStringList args = {"app", "--test-mode", "some_file.cpp"};
        gridlock::core::utils::CliParser parser;
        QVERIFY(parser.parse(args));
        QVERIFY(!parser.isTutorialMode());
        QVERIFY(parser.isTestMode());
        QCOMPARE(parser.getTestFilePath(), QString("some_file.cpp"));
    }

    void testCliParserTestModeWithoutFile() {
        QStringList args = {"app", "--test-mode"};
        gridlock::core::utils::CliParser parser;
        QVERIFY(parser.parse(args));
        QVERIFY(!parser.isTutorialMode());
        QVERIFY(parser.isTestMode());
        QVERIFY(parser.getTestFilePath().isEmpty());
    }

    void testTutorialModelInitialization() {
        // Obsolete as TutorialModel is removed
    }

    void testBreakpointInjection() {
        // Find inspection_demo.cpp
        QString relativePath = "tutorial/inspection_demo.cpp";
        QSet<int> expectedLines = gridlock::ui::TutorialDialog::getBreakpointsForFile(relativePath);
        QCOMPARE(expectedLines.size(), 1);
        QVERIFY(expectedLines.contains(76));
        
        QString absPath = QFileInfo(relativePath).absoluteFilePath();
        
        // Clear existing breakpoints
        QMap<QString, QSet<int>> emptyBps;
        gridlock::core::ConfigManager::instance().saveBreakpoints(emptyBps);
        
        // Simulate injection
        QMap<QString, QSet<int>> currentBps = gridlock::core::ConfigManager::instance().getBreakpoints();
        currentBps[absPath].unite(expectedLines);
        gridlock::core::ConfigManager::instance().saveBreakpoints(currentBps);
        
        // Verify
        QMap<QString, QSet<int>> savedBps = gridlock::core::ConfigManager::instance().getBreakpoints();
        QVERIFY(savedBps.contains(absPath));
        QCOMPARE(savedBps[absPath], expectedLines);
    }
};

QTEST_MAIN(TestTutorial)
#include "test_tutorial.moc"
