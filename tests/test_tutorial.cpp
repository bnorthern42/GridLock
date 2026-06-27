#include <QTest>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QSignalSpy>

// We will mock or include the models here
#include "../src/ui/tutorial/TutorialModel.hpp"
#include "../src/core/utils/CliParser.hpp"

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
        gridlock::ui::tutorial::TutorialModel model;
        
        QCOMPARE(model.rowCount(), 5);
        
        QModelIndex index0 = model.index(0, 0);
        QCOMPARE(model.data(index0, Qt::DisplayRole).toString(), QString("Deadlock Demo"));
        QCOMPARE(model.data(index0, Qt::UserRole).toString(), QString("tutorial/deadlock_demo.c"));
        
        QModelIndex index1 = model.index(1, 0);
        QCOMPARE(model.data(index1, Qt::DisplayRole).toString(), QString("Inspection Demo"));
        QCOMPARE(model.data(index1, Qt::UserRole).toString(), QString("tutorial/inspection_demo.cpp"));
    }
};

QTEST_MAIN(TestTutorial)
#include "test_tutorial.moc"
