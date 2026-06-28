#include <QTest>
#include <QObject>
#include <QSignalSpy>
#include <QListWidget>
#include <QTimer>
#include "../../src/ui/MainWindow.hpp"
#include "../../src/ui/MainWindow.hpp"
#include "../../src/ui/tutorial/TutorialDialog.hpp"
#include "../../src/core/hpc/DapCoordinator.hpp"
#include "../../src/core/managers/ConfigManager.hpp"

class TestTutorialE2E : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        // Ensure ConfigManager has default debugger settings for test
        gridlock::core::DebuggerSettings ds;
        ds.gdbPath = "gdb";
        ds.mpiExecutable = "mpiexec";
        ds.mpiArgs = "--oversubscribe";
        ds.defaultRanks = 2;
        gridlock::core::ConfigManager::instance().saveDebuggerSettings(ds);
    }

    void testTutorialsE2E() {
        QStringList tutorials = {
            "Inspection Demo",
            "Register Demo",
            "MemView Demo",
            "MemView Diff Demo"
        };
        
        gridlock::ui::MainWindow mainWindow;
        DapCoordinator* coordinator = new DapCoordinator(&mainWindow);
        mainWindow.setCoordinator(coordinator);

        QString workspaceRoot = QDir::currentPath();
        gridlock::core::ConfigManager::instance().setWorkspace(workspaceRoot);

        for (const QString& tutorialName : tutorials) {
            gridlock::ui::TutorialDialog dialog;
            
            QListWidget* list = dialog.findChild<QListWidget*>();
            QVERIFY(list != nullptr);

            int row = -1;
            for (int i = 0; i < list->count(); i++) {
                if (list->item(i)->text() == tutorialName) {
                    row = i;
                    break;
                }
            }
            QVERIFY(row != -1);

            list->setCurrentRow(row);
            
            QSignalSpy spyDialog(&dialog, &gridlock::ui::TutorialDialog::launchTutorialRequested);
            
            // Emulate click on launch button
            QMetaObject::invokeMethod(&dialog, "onLaunchClicked");
            
            QCOMPARE(spyDialog.count(), 1);
            QString absPath = spyDialog.takeFirst().at(0).toString();
            
            // Trigger main window launch
            mainWindow.onTutorialLaunchRequested(absPath);
            
            // We already have coordinator pointer
            QVERIFY(coordinator != nullptr);
            
            QSignalSpy spyStop(coordinator, &DapCoordinator::executionStopped);
            
            // Wait for first stop (could be 'entry' or 'breakpoint')
            QVERIFY(spyStop.wait(60000));
            QString reason = spyStop.takeFirst().at(1).toString();
            
            if (reason == "entry") {
                // If stopped on entry, continue to hit the actual breakpoint
                coordinator->continueExecution(0);
                coordinator->continueExecution(1);
                QVERIFY(spyStop.wait(60000));
                reason = spyStop.takeFirst().at(1).toString();
            }
            
            QCOMPARE(reason, QString("breakpoint"));
            
            // Breakpoint successfully hit. We can terminate and move to next tutorial.
            QSignalSpy spyDisconnect(coordinator, &DapCoordinator::adapterExited);
            coordinator->terminateSession();
            if (spyDisconnect.isEmpty()) {
                QVERIFY(spyDisconnect.wait(10000)); // Give the slow CI runner up to 10 seconds to cleanly shut down
            }
            
            // Clean up temporary compiled binaries
            QString outBin = QDir::tempPath() + "/" + QFileInfo(tutorialName.toLower().replace(" ", "_") + ".cpp").baseName() + "_demo";
            if (QFile::exists(outBin)) {
                QFile::remove(outBin);
            }
        }
        
        qApp->removeEventFilter(&mainWindow);
    }
};

QTEST_MAIN(TestTutorialE2E)
#include "test_tutorial_e2e.moc"
