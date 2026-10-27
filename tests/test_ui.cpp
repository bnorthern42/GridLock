#include "test_ui.hpp"
#include <QTemporaryFile>
#include "../src/ui/MainWindow.hpp"
#include "../src/ui/views/SourceCodeView.hpp"
#include "../src/ui/views/ServerRackView.hpp"
#include "../src/ui/views/DisassemblyView.hpp"
#include "../src/ui/widgets/DifferentialGrid.hpp"
#include "../src/RankState.hpp"
#include <QApplication>
#include <QSplitter>
#include <QTextDocument>
#include <QTextBlock>
#include <QHash>
#include <QComboBox>
#include <QLineEdit>
#include "../src/core/managers/ConfigManager.hpp"
#include "../src/core/managers/ThemeManager.hpp"
#include "../src/ui/GdbConsoleWidget.hpp"
#include <QFontDatabase>
#include "../src/ui/widgets/FileIconProvider.hpp"
#include <QIcon>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>
#include <QDockWidget>
#include <QImage>
#include <QLabel>
#include <QTabWidget>
#include "../src/ui/tutorial/TutorialDialog.hpp"
#include "../src/ui/views/MemView.hpp"
#include "../src/ui/views/RegisterView.hpp"
void TestMainWindowUI::testSourceFileLoading() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    tempFile.write("#include <iostream>\nint main(){}");
    tempFile.close();

    gridlock::ui::MainWindow mainWindow;
    mainWindow.loadSourceFile(tempFile.fileName());

    QVERIFY(mainWindow.getSourceCodeView() != nullptr);
    QCOMPARE(mainWindow.getSourceCodeView()->toPlainText().isEmpty(), false);
    QVERIFY(mainWindow.getSourceCodeView()->toPlainText().contains("int main"));
}

void TestMainWindowUI::testMainLayoutStructure() {
    gridlock::ui::MainWindow mainWindow;
    QList<QSplitter*> splitters = mainWindow.findChildren<QSplitter*>();
    
    QSplitter* mainVertical = nullptr;
    QSplitter* masterHorizontal = nullptr;
    
    for (QSplitter* s : splitters) {
        if (s->orientation() == Qt::Vertical) mainVertical = s;
        if (s->orientation() == Qt::Horizontal) masterHorizontal = s;
    }
    
    QVERIFY(mainVertical != nullptr);
    QVERIFY(masterHorizontal != nullptr);
    QCOMPARE(masterHorizontal->count(), 2);
}

void TestMainWindowUI::testSourceCodeViewportMargins() {
    gridlock::ui::SourceCodeView view;
    QMargins margins = view.getViewportMargins();
    QVERIFY(margins.left() > 0);
}

void TestMainWindowUI::testServerRackStateUpdate() {
    gridlock::ui::ServerRackView view;
    gridlock::RankState state;
    state.currentState = "stopped";
    
    view.updateRankState(0, state);
    QVERIFY(true);
}

void TestMainWindowUI::testAsmSyntaxHighlighter() {
    QTextDocument doc;
    doc.setPlainText("0x00555: movl $0x2, %eax");
    gridlock::ui::AsmSyntaxHighlighter highlighter(&doc);
    
    // Force rehighlight
    highlighter.rehighlight();
    
    // Test formatting of block 0
    QTextBlock block = doc.findBlockByLineNumber(0);
    QVector<QTextLayout::FormatRange> formats = block.layout()->formats();
    
    QString opcodeColor = gridlock::core::ConfigManager::instance().getAssemblyOpcode();
    QString registerColor = gridlock::core::ConfigManager::instance().getAssemblyRegister();
    QString addressColor = gridlock::core::ConfigManager::instance().getAssemblyAddress();

    bool foundOpcode = false;
    bool foundRegister = false;
    bool foundAddress = false;

    for (const auto& fmt : formats) {
        QString fmtColor = fmt.format.foreground().color().name(QColor::HexRgb);
        if (fmtColor == QColor(opcodeColor).name(QColor::HexRgb)) foundOpcode = true;
        if (fmtColor == QColor(registerColor).name(QColor::HexRgb)) foundRegister = true;
        if (fmtColor == QColor(addressColor).name(QColor::HexRgb)) foundAddress = true;
    }
    
    QVERIFY(foundOpcode);
    QVERIFY(foundRegister);
    QVERIFY(foundAddress);
}

void TestMainWindowUI::testSourceCodeHighlightBounds() {
    gridlock::ui::SourceCodeView view;
    QString code;
    for (int i = 1; i <= 100; ++i) {
        code += QString("int line_%1 = 0;\n").arg(i);
    }
    view.setSourceCode(code);
    
    // Highlight line 83 (GDB 1-based index) -> maps to block 82
    view.highlightCurrentLine(83);
    
    QTextBlock block = view.textCursor().block();
    QCOMPARE(block.blockNumber(), 82);
    
    // Highlight out of bounds (should not crash)
    view.highlightCurrentLine(200);
    view.highlightCurrentLine(0);
    view.highlightCurrentLine(-1);
    
    // Assert no exceptions
    QVERIFY(true);
}

void TestMainWindowUI::testDifferentialGridExpansion() {
    gridlock::ui::DifferentialGrid grid;
    
    // Programmatically inject 15 variables across 6 ranks via updateVariableDisplay
    for (int rankId = 0; rankId < 6; ++rankId) {
        for (int v = 0; v < 15; ++v) {
            QString varName = QString("var_%1").arg(v);
            QString value = (rankId == 5 && v == 0) ? "divergent_value" : "100";
            grid.updateVariableDisplay(rankId, varName, value);
        }
    }
    
    // Verify dynamic row/col expansion
    QCOMPARE(grid.rowCount(), 7); // 1 header + 6 ranks
    QCOMPARE(grid.columnCount(), 16); // 1 header + 15 variables
    
    // Find var_0 column
    int colVar0 = -1;
    for (int c = 1; c < grid.columnCount(); ++c) {
        if (grid.horizontalHeaderItem(c)->text() == "var_0") {
            colVar0 = c;
            break;
        }
    }
    QVERIFY(colVar0 != -1);

    // Assert majority-highlighting on Rank 0 (Normal)
    QTableWidgetItem* itemNormal = grid.item(1, colVar0);
    QVERIFY(itemNormal != nullptr);
    QCOMPARE(itemNormal->text(), QString("100"));
    QCOMPARE(itemNormal->background(), QBrush());
    
    // Assert majority-highlighting on Rank 5 (Divergent)
    QTableWidgetItem* itemDivergent = grid.item(6, colVar0);
    QVERIFY(itemDivergent != nullptr);
    QCOMPARE(itemDivergent->text(), QString("divergent_value"));
    QCOMPARE(itemDivergent->background().color(), QColor(255, 200, 200));
}

void TestMainWindowUI::testDifferentialGridEmits() {
    gridlock::ui::DifferentialGrid grid;
    int emitCount = 0;
    QObject::connect(&grid, &gridlock::ui::DifferentialGrid::watchVariableAdded, [&emitCount](const QString& /*name*/) {
        emitCount++;
    });

    QHash<QString, QString> vars;
    vars["offset"] = "123";
    grid.setVariableData(0, vars);
    
    // Should NOT emit watchVariableAdded for the placeholder "-" or anything else during setVariableData
    QCOMPARE(emitCount, 0);

    // Editing row 0 col 0 SHOULD emit
    grid.item(0, 0)->setText("mtype");
    QCOMPARE(emitCount, 1);
}

void TestMainWindowUI::testGdbConsoleFiltering() {
    gridlock::ui::GdbConsoleWidget console;
    console.resetRanks(4); // Creates Rank 0..3

    console.appendGdbOutput(0, "~\"Breakpoint hit at main\\n\"");
    console.appendGdbOutput(1, "~\"Segfault encountered\\n\"");
    console.appendGdbOutput(0, "~\"Variable x = 5\\n\"");

    QPlainTextEdit* consoleEdit = console.findChild<QPlainTextEdit*>();
    QComboBox* rankCombo = console.findChild<QComboBox*>();
    QLineEdit* filterEdit = console.findChild<QLineEdit*>();

    QVERIFY(consoleEdit != nullptr);
    QVERIFY(rankCombo != nullptr);
    QVERIFY(filterEdit != nullptr);

    // Verify Rank Filtering (Index 2 -> Rank 1, since Index 0 is "All Ranks", Index 1 is "Rank 0")
    rankCombo->setCurrentIndex(2);
    QCOMPARE(consoleEdit->toPlainText(), QString("Segfault encountered"));

    // Verify Text Filtering
    rankCombo->setCurrentIndex(0); // Reset to All Ranks
    filterEdit->setText("Variable");
    QCOMPARE(consoleEdit->toPlainText(), QString("Variable x = 5"));
}

void TestMainWindowUI::testGutterBreakpointPropagation() {
    gridlock::ui::SourceCodeView view;
    QString code;
    for (int i = 1; i <= 10; ++i) {
        code += QString("Line %1\n").arg(i);
    }
    view.setSourceCode(code);
    view.setCurrentFile("/test/dummy.cpp");
    
    // Headless layout initialization
    view.resize(800, 600);
    QApplication::processEvents();

    struct ViewSpy : public gridlock::ui::SourceCodeView {
        int getY(int line) {
            QTextBlock b = document()->findBlockByLineNumber(line - 1);
            return qRound(blockBoundingGeometry(b).translated(contentOffset()).top()) + 2;
        }
    };
    int yPos = static_cast<ViewSpy*>(&view)->getY(5);

    int signalFired = 0;
    int signaledLine = -1;
    QObject::connect(&view, &gridlock::ui::SourceCodeView::breakpointToggled, [&](const QString& /*file*/, int line, bool /*isSet*/, const QString& /*condition*/) {
        signalFired++;
        signaledLine = line;
    });

    // Use the modern QMouseEvent constructor (device-aware, replaces the deprecated 5-arg form)
    QMouseEvent event(QEvent::MouseButtonPress, QPointF(15, yPos), QPointF(15, yPos),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    view.lineNumberAreaMousePressEvent(&event);

    QCOMPARE(signalFired, 1);
    QCOMPARE(signaledLine, 5);
}


void TestMainWindowUI::testNerdFontLoading() {
    int fontId = QFontDatabase::addApplicationFont(":/fonts/SymbolsNerdFont-Regular.ttf");
    QVERIFY(fontId != -1);
    QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    QVERIFY(!families.isEmpty());
}

void TestMainWindowUI::testFileIconProvider() {
    gridlock::ui::FileIconProvider provider;
    
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    QFileInfo dirInfo(tempDir.path());
    QIcon dirIcon = provider.icon(dirInfo);
    QVERIFY(!dirIcon.isNull());
    
    QTemporaryFile tempCpp(tempDir.path() + "/test.cpp");
    QVERIFY(tempCpp.open());
    tempCpp.close();
    QFileInfo cppInfo(tempCpp.fileName());
    QIcon cppIcon = provider.icon(cppInfo);
    QVERIFY(!cppIcon.isNull());
    
    QTemporaryFile tempMeson(tempDir.path() + "/meson.build");
    QVERIFY(tempMeson.open());
    tempMeson.close();
    QFileInfo mesonInfo(tempMeson.fileName());
    QIcon mesonIcon = provider.icon(mesonInfo);
    QVERIFY(!mesonIcon.isNull());
}

void TestMainWindowUI::testCutterDockDensityAndSnapshot() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);

    // 1. Verify dock nesting is enabled
    QVERIFY(mainWindow.isDockNestingEnabled());

    // 2. Verify all primary QDockWidget children have movable/floatable/closable features and zero inner contentsMargins
    const QList<QDockWidget*> docks = mainWindow.findChildren<QDockWidget*>();
    QVERIFY(!docks.isEmpty());
    const auto requiredFeatures = QDockWidget::DockWidgetMovable |
                                  QDockWidget::DockWidgetFloatable |
                                  QDockWidget::DockWidgetClosable;
    for (QDockWidget* dock : docks) {
        QVERIFY((dock->features() & requiredFeatures) == requiredFeatures);
        if (QWidget* inner = dock->widget()) {
            QCOMPARE(inner->contentsMargins(), QMargins(0, 0, 0, 0));
        }
    }

    // 3. Verify captureWorkspaceSnapshot returns true, writes non-empty PNG, and matches MainWindow size
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString snapshotPath = tempDir.path() + "/workspace_snapshot.png";

    QVERIFY(mainWindow.captureWorkspaceSnapshot(snapshotPath));
    QFileInfo snapshotInfo(snapshotPath);
    QVERIFY(snapshotInfo.exists());
    QVERIFY(snapshotInfo.size() > 0);

    QImage capturedImage(snapshotPath);
    QVERIFY(!capturedImage.isNull());
    QCOMPARE(capturedImage.size() / capturedImage.devicePixelRatio(), mainWindow.size());
}

void TestMainWindowUI::testTutorialBreakpointInjection() {
    const auto originalBps = gridlock::core::ConfigManager::instance().getBreakpoints();

    QString relativePath = "tutorial/inspection_demo.cpp";
    QSet<int> expectedLines = gridlock::ui::TutorialDialog::getBreakpointsForFile(relativePath);
    QCOMPARE(expectedLines.size(), 1);
    QVERIFY(expectedLines.contains(76));

    QString absPath = QFileInfo(relativePath).absoluteFilePath();

    QMap<QString, QSet<int>> emptyBps;
    gridlock::core::ConfigManager::instance().saveBreakpoints(emptyBps);

    gridlock::ui::TutorialDialog::injectBreakpoints(relativePath, absPath);

    QMap<QString, QSet<int>> savedBps = gridlock::core::ConfigManager::instance().getBreakpoints();
    QVERIFY(savedBps.contains(absPath));
    QCOMPARE(savedBps[absPath], expectedLines);

    gridlock::core::ConfigManager::instance().saveBreakpoints(originalBps);
}

void TestMainWindowUI::testCutterZoneGroupingAndSeekBar() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);

    // 1. Verify Registers and Watch Expressions docks are in RightDockWidgetArea,
    //    and BottomDockWidgetArea / bottom tabs have at most 7 widgets.
    QDockWidget* registersDock = nullptr;
    QDockWidget* watchDock = nullptr;
    int bottomDockCount = 0;

    const QList<QDockWidget*> docks = mainWindow.findChildren<QDockWidget*>();
    for (QDockWidget* dock : docks) {
        if (dock->windowTitle().contains("Registers", Qt::CaseInsensitive) ||
            dock->objectName().contains("Register", Qt::CaseInsensitive)) {
            registersDock = dock;
        }
        if (dock->windowTitle().contains("Watch Expressions", Qt::CaseInsensitive) ||
            dock->objectName().contains("Watch", Qt::CaseInsensitive)) {
            watchDock = dock;
        }
        if (mainWindow.dockWidgetArea(dock) == Qt::BottomDockWidgetArea) {
            bottomDockCount++;
        }
    }

    QVERIFY(registersDock != nullptr);
    QVERIFY(watchDock != nullptr);
    QCOMPARE(mainWindow.dockWidgetArea(registersDock), Qt::RightDockWidgetArea);
    QCOMPARE(mainWindow.dockWidgetArea(watchDock), Qt::RightDockWidgetArea);
    QVERIFY(bottomDockCount <= 7);

    for (QTabWidget* tabWidget : mainWindow.findChildren<QTabWidget*>()) {
        if (tabWidget->objectName() == "BottomConsoleTabs") {
            QVERIFY(tabWidget->count() <= 7);
        }
    }

    // 2. Verify Cutter-style seek bar and status telemetry label exist
    auto* seekBar = mainWindow.findChild<QLineEdit*>("cutterSeekBar");
    auto* statusTelemetry = mainWindow.findChild<QLabel*>("cutterStatusTelemetry");
    QVERIFY(seekBar != nullptr);
    QVERIFY(statusTelemetry != nullptr);

    // 3. Verify seekToAddressOrExpression updates telemetry and syncs with Memory View safely
    QVERIFY(!mainWindow.seekToAddressOrExpression("   "));
    QVERIFY(mainWindow.seekToAddressOrExpression("0x401000"));
    QCOMPARE(seekBar->text(), QString("0x401000"));
    QVERIFY(statusTelemetry->text().contains("0x401000"));

    if (mainWindow.memView()) {
        if (auto* memAddrEdit = mainWindow.memView()->findChild<QLineEdit*>()) {
            QCOMPARE(memAddrEdit->text(), QString("0x401000"));
        }
    }
}

void TestMainWindowUI::testCutterDisassemblyCfgAndBranchFlow() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);

    gridlock::ui::DisassemblyView* disassemblyView = mainWindow.disassemblyView();
    QVERIFY(disassemblyView != nullptr);

    const QString asmListing =
        "0x401120: cmp eax, 0x0\n"
        "0x401123: je 0x401130\n"
        "0x401125: mov eax, 0x1\n"
        "0x40112a: jmp 0x401135\n"
        "0x401130: xor eax, eax\n"
        "0x401135: ret";

    disassemblyView->updateDisassembly(asmListing);

    // 1. Verify branch edges and basic block partitioning
    QVERIFY(disassemblyView->branchArcCount() >= 2);
    QVERIFY(disassemblyView->basicBlockCount() >= 3);

    // 2. Verify Linear vs CFG Graph view mode toggling
    QVERIFY(!disassemblyView->isGraphMode());
    disassemblyView->toggleViewMode();
    QVERIFY(disassemblyView->isGraphMode());
    disassemblyView->toggleViewMode();
    QVERIFY(!disassemblyView->isGraphMode());
    disassemblyView->setGraphMode(true);
    QVERIFY(disassemblyView->isGraphMode());
    disassemblyView->setGraphMode(false);
    QVERIFY(!disassemblyView->isGraphMode());

    // 3. Verify seeking to address updates currentAddress in both DisassemblyView and MainWindow
    QVERIFY(disassemblyView->seekToAddress(0x401130ULL));
    QCOMPARE(disassemblyView->currentAddress(), 0x401130ULL);

    QVERIFY(mainWindow.seekToAddressOrExpression("0x401120"));
    QCOMPARE(disassemblyView->currentAddress(), 0x401120ULL);
    QVERIFY(mainWindow.seekToAddressOrExpression("0x401130"));
    QCOMPARE(disassemblyView->currentAddress(), 0x401130ULL);

    // Capture Linear and CFG Graph mode snapshots for visual verification
    gridlock::core::managers::ThemeManager::instance().initialize();
    disassemblyView->updateDisassembly(QString());
    disassemblyView->seekToAddress(0x401120ULL);
    disassemblyView->setGraphMode(false);
    mainWindow.show();
    QVERIFY(mainWindow.captureWorkspaceSnapshot("/tmp/gridlock_cutter_v3_linear.png"));
    disassemblyView->setGraphMode(true);
    QVERIFY(mainWindow.captureWorkspaceSnapshot("/tmp/gridlock_cutter_v3_cfg.png"));
    disassemblyView->setGraphMode(false);
}

void TestMainWindowUI::testCutterHexdumpDiffAndRegisterSeekSync() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);

    gridlock::ui::RegisterView* registerView = mainWindow.registerView();
    gridlock::ui::MemView* memView = mainWindow.memView();
    gridlock::ui::DisassemblyView* disassemblyView = mainWindow.disassemblyView();
    QVERIFY(registerView != nullptr);
    QVERIFY(memView != nullptr);
    QVERIFY(disassemblyView != nullptr);

    // 1. Register Delta Tracking
    QMap<QString, QString> initialRegs = {
        {"rip", "0x00401120"},
        {"rax", "0x0"},
        {"rsp", "0x7fffffffe480"}
    };
    registerView->updateRegisters(initialRegs);
    QCOMPARE(registerView->changedRegisterCount(), 0);
    QVERIFY(!registerView->isRegisterModified("rip"));

    QMap<QString, QString> steppedRegs = {
        {"rip", "0x0040113d"},
        {"rax", "0x1"},
        {"rsp", "0x7fffffffe480"}
    };
    registerView->updateRegisters(steppedRegs);
    QCOMPARE(registerView->changedRegisterCount(), 2);
    QVERIFY(registerView->isRegisterModified("rip"));
    QVERIFY(registerView->isRegisterModified("rax"));
    QVERIFY(!registerView->isRegisterModified("rsp"));

    // 2. Hexdump Byte Diff Tracking & Seek
    QByteArray bufA(16, '\x00');
    bufA[0] = '\x55';
    bufA[1] = '\x48';
    bufA[2] = '\x89';
    bufA[3] = '\xe5';
    memView->setMemoryData("0x00401120", bufA);
    QCOMPARE(memView->changedByteCount(), 0);

    QByteArray bufB = bufA;
    bufB[4] = '\x90';
    bufB[8] = '\x01';
    bufB[15] = '\xff';
    memView->setMemoryData("0x00401120", bufB);
    QCOMPARE(memView->changedByteCount(), 3);

    QVERIFY(memView->seekToAddress(0x0040113dULL));
    QCOMPARE(memView->currentBaseAddress(), 0x0040113dULL);

    // 3. End-to-End Cross-View Pointer Sync in MainWindow
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401120"));
    QCOMPARE(disassemblyView->currentAddress(), 0x00401120ULL);
    QCOMPARE(memView->currentBaseAddress(), 0x00401120ULL);

    QVERIFY(registerView->activateRegister("rip"));
    auto* seekBar = mainWindow.findChild<QLineEdit*>("cutterSeekBar");
    auto* statusTelemetry = mainWindow.findChild<QLabel*>("cutterStatusTelemetry");
    QVERIFY(seekBar != nullptr);
    QVERIFY(statusTelemetry != nullptr);
    QVERIFY(seekBar->text().contains("40113d", Qt::CaseInsensitive));
    QVERIFY(statusTelemetry->text().contains("40113d", Qt::CaseInsensitive));
    QCOMPARE(disassemblyView->currentAddress(), 0x0040113dULL);
    QCOMPARE(memView->currentBaseAddress(), 0x0040113dULL);
}

QTEST_MAIN(TestMainWindowUI)
