/**
 * @file test_ui.cpp
 * @brief Primary UI integration test suite for GridLock Qt6 desktop environment.
 *
 * Implements test slots validating main window layout, source editor syntax
 * highlighting and gutter markers, differential memory grid views, GDB console
 * filtering, font loading, file icon providers, and memory zone seeking.
 */

#include "test_ui.hpp"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMargins>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QString>
#include <QTabWidget>
#include <QTableWidgetItem>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QToolBar>
#include <QtTest/QtTest>

#include "../src/RankState.hpp"
#include "../src/core/managers/ConfigManager.hpp"
#include "../src/core/managers/ThemeManager.hpp"
#include "../src/ui/GdbConsoleWidget.hpp"
#include "../src/ui/MainWindow.hpp"
#include "../src/ui/tutorial/TutorialDialog.hpp"
#include "../src/ui/views/DisassemblyView.hpp"
#include "../src/ui/views/MemView.hpp"
#include "../src/ui/views/RegisterView.hpp"
#include "../src/ui/views/ServerRackView.hpp"
#include "../src/ui/views/SourceCodeView.hpp"
#include "../src/ui/widgets/DifferentialGrid.hpp"
#include "../src/ui/widgets/FileIconProvider.hpp"
#include "../src/ui/widgets/ProjectExplorerWidget.hpp"

/**
 * @brief Tests loading C++ source code files into MainWindow's SourceCodeView.
 */
void TestMainWindowUI::testSourceFileLoading() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    tempFile.write("#include <iostream>\nint main(){}");
    tempFile.close();

    // Instantiate main window and load temporary file.
    gridlock::ui::MainWindow mainWindow;
    mainWindow.loadSourceFile(tempFile.fileName());

    // Verify source code view exists and displays loaded file content.
    QVERIFY(mainWindow.getSourceCodeView() != nullptr);
    QCOMPARE(mainWindow.getSourceCodeView()->toPlainText().isEmpty(), false);
    QVERIFY(mainWindow.getSourceCodeView()->toPlainText().contains("int main"));
}

/**
 * @brief Tests the presence and hierarchy of primary splitters in MainWindow.
 */
void TestMainWindowUI::testMainLayoutStructure() {
    gridlock::ui::MainWindow mainWindow;
    QList<QSplitter*> splitters = mainWindow.findChildren<QSplitter*>();
    QSplitter* mainVertical = nullptr;
    QSplitter* masterHorizontal = nullptr;

    // Locate vertical and horizontal splitters.
    for (QSplitter* s : splitters) {
        if (s->orientation() == Qt::Vertical) mainVertical = s;
        if (s->orientation() == Qt::Horizontal) masterHorizontal = s;
    }
    // Verify both splitters are instantiated.
    QVERIFY(mainVertical != nullptr);
    QVERIFY(masterHorizontal != nullptr);
    QCOMPARE(masterHorizontal->count(), 2);
}

/**
 * @brief Tests SourceCodeView left gutter margin dimensions for line numbering.
 */
void TestMainWindowUI::testSourceCodeViewportMargins() {
    gridlock::ui::SourceCodeView view;
    // Query left gutter margin.
    QMargins margins = view.getViewportMargins();
    // Left margin must accommodate line numbers and breakpoint icons.
    QVERIFY(margins.left() > 0);
}

/**
 * @brief Tests updating rank state telemetry in ServerRackView.
 */
void TestMainWindowUI::testServerRackStateUpdate() {
    gridlock::ui::ServerRackView view;
    gridlock::RankState state;
    state.currentState = "stopped";
    // Update rank state for rank 0.
    view.updateRankState(0, state);
    QVERIFY(true);
}

/**
 * @brief Tests assembly syntax highlighting token classification and color formatting.
 */
void TestMainWindowUI::testAsmSyntaxHighlighter() {
    QTextDocument doc;
    doc.setPlainText("0x00555: movl $0x2, %eax");
    gridlock::ui::AsmSyntaxHighlighter highlighter(&doc);
    highlighter.rehighlight();

    // Inspect layout format ranges on first block.
    QTextBlock block = doc.findBlockByLineNumber(0);
    QVector<QTextLayout::FormatRange> formats = block.layout()->formats();
    QString opcodeColor = gridlock::core::ConfigManager::instance().getAssemblyOpcode();
    QString registerColor = gridlock::core::ConfigManager::instance().getAssemblyRegister();
    QString addressColor = gridlock::core::ConfigManager::instance().getAssemblyAddress();
    bool foundOpcode = false, foundRegister = false, foundAddress = false;

    // Verify opcode, register, and memory address colors match theme definitions.
    for (const auto& fmt : formats) {
        QString fmtColor = fmt.format.foreground().color().name(QColor::HexRgb);
        if (fmtColor == QColor(opcodeColor).name(QColor::HexRgb)) foundOpcode = true;
        if (fmtColor == QColor(registerColor).name(QColor::HexRgb)) foundRegister = true;
        if (fmtColor == QColor(addressColor).name(QColor::HexRgb)) foundAddress = true;
    }
    QVERIFY(foundOpcode && foundRegister && foundAddress);
}

/**
 * @brief Tests line highlighting bounds checking in SourceCodeView.
 */
void TestMainWindowUI::testSourceCodeHighlightBounds() {
    gridlock::ui::SourceCodeView view;
    QString code;
    for (int i = 1; i <= 100; ++i) code += QString("int line_%1 = 0;\n").arg(i);
    view.setSourceCode(code);

    // Highlight valid line 83.
    view.highlightCurrentLine(83);
    QTextBlock block = view.textCursor().block();
    QCOMPARE(block.blockNumber(), 82);

    // Out-of-bounds line numbers must not crash.
    view.highlightCurrentLine(200);
    view.highlightCurrentLine(0);
    view.highlightCurrentLine(-1);
    QVERIFY(true);
}

/**
 * @brief Tests dynamic table expansion and divergent rank cell highlighting in DifferentialGrid.
 */
void TestMainWindowUI::testDifferentialGridExpansion() {
    gridlock::ui::DifferentialGrid grid;
    // Populate variables across ranks.
    for (int rankId = 0; rankId < 6; ++rankId) {
        for (int v = 0; v < 15; ++v) {
            QString varName = QString("var_%1").arg(v);
            QString value = (rankId == 5 && v == 0) ? "divergent_value" : "100";
            grid.updateVariableDisplay(rankId, varName, value);
        }
    }
    // Verify grid dimension growth.
    QCOMPARE(grid.rowCount(), 7);
    QCOMPARE(grid.columnCount(), 16);

    // Locate column corresponding to var_0.
    int colVar0 = -1;
    for (int c = 1; c < grid.columnCount(); ++c) {
        if (grid.horizontalHeaderItem(c)->text() == "var_0") { colVar0 = c; break; }
    }
    QVERIFY(colVar0 != -1);

    // Verify standard item background.
    QTableWidgetItem* itemNormal = grid.item(1, colVar0);
    QVERIFY(itemNormal != nullptr);
    QCOMPARE(itemNormal->text(), QString("100"));
    QCOMPARE(itemNormal->background(), QBrush());

    // Verify divergent rank item is highlighted with soft reddish tint.
    QTableWidgetItem* itemDivergent = grid.item(6, colVar0);
    QVERIFY(itemDivergent != nullptr);
    QCOMPARE(itemDivergent->text(), QString("divergent_value"));
    QCOMPARE(itemDivergent->background().color(), QColor(255, 200, 200));
}

/**
 * @brief Tests watchVariableAdded signal emission from user interactions on DifferentialGrid.
 */
void TestMainWindowUI::testDifferentialGridEmits() {
    gridlock::ui::DifferentialGrid grid;
    int emitCount = 0;
    QObject::connect(&grid, &gridlock::ui::DifferentialGrid::watchVariableAdded, [&emitCount](const QString&) { emitCount++; });

    QHash<QString, QString> vars;
    vars["offset"] = "123";
    grid.setVariableData(0, vars);
    QCOMPARE(emitCount, 0);

    // Editing variable cell should emit watchVariableAdded signal.
    grid.item(0, 0)->setText("mtype");
    QCOMPARE(emitCount, 1);
}

/**
 * @brief Tests filtering GDB/MI output logs by MPI rank and search keywords.
 */
void TestMainWindowUI::testGdbConsoleFiltering() {
    gridlock::ui::GdbConsoleWidget console;
    console.resetRanks(4);

    // Append sample GDB/MI output across ranks 0 and 1.
    console.appendGdbOutput(0, "~\"Breakpoint hit at main\\n\"");
    console.appendGdbOutput(1, "~\"Segfault encountered\\n\"");
    console.appendGdbOutput(0, "~\"Variable x = 5\\n\"");

    QPlainTextEdit* consoleEdit = console.findChild<QPlainTextEdit*>();
    QComboBox* rankCombo = console.findChild<QComboBox*>();
    QLineEdit* filterEdit = console.findChild<QLineEdit*>();
    QVERIFY(consoleEdit && rankCombo && filterEdit);

    // Filter to rank 1.
    rankCombo->setCurrentIndex(2);
    QCOMPARE(consoleEdit->toPlainText(), QString("Segfault encountered"));

    // Filter back to all ranks with text filter.
    rankCombo->setCurrentIndex(0);
    filterEdit->setText("Variable");
    QCOMPARE(consoleEdit->toPlainText(), QString("Variable x = 5"));
}

/**
 * @brief Tests clicking editor gutter margin to toggle line breakpoints.
 */
void TestMainWindowUI::testGutterBreakpointPropagation() {
    gridlock::ui::SourceCodeView view;
    QString code;
    for (int i = 1; i <= 10; ++i) code += QString("Line %1\n").arg(i);
    view.setSourceCode(code);
    view.setCurrentFile("/test/dummy.cpp");
    view.resize(800, 600);
    QApplication::processEvents();

    // Helper subclass to access internal line bounding box.
    struct ViewSpy : public gridlock::ui::SourceCodeView {
        int getY(int line) {
            QTextBlock b = document()->findBlockByLineNumber(line - 1);
            return qRound(blockBoundingGeometry(b).translated(contentOffset()).top()) + 2;
        }
    };
    int yPos = static_cast<ViewSpy*>(&view)->getY(5);
    int signalFired = 0, signaledLine = -1;
    QObject::connect(&view, &gridlock::ui::SourceCodeView::breakpointToggled, [&](const QString&, int line, bool, const QString&) {
        signalFired++; signaledLine = line;
    });

    // Simulate mouse press in line number area.
    QMouseEvent event(QEvent::MouseButtonPress, QPointF(15, yPos), QPointF(15, yPos), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    view.lineNumberAreaMousePressEvent(&event);

    // Verify breakpointToggled emitted for line 5.
    QCOMPARE(signalFired, 1);
    QCOMPARE(signaledLine, 5);
}

/**
 * @brief Tests embedded Symbols Nerd Font resource registration.
 */
void TestMainWindowUI::testNerdFontLoading() {
    int fontId = QFontDatabase::addApplicationFont(":/fonts/SymbolsNerdFont-Regular.ttf");
    QVERIFY(fontId != -1);
    QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    QVERIFY(!families.isEmpty());
}

/**
 * @brief Tests icon lookup for directory, C++ source, and Meson build files.
 */
void TestMainWindowUI::testFileIconProvider() {
    gridlock::ui::FileIconProvider provider;
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    // Directory icon.
    QVERIFY(!provider.icon(QFileInfo(tempDir.path())).isNull());

    // C++ source icon.
    QTemporaryFile tempCpp(tempDir.path() + "/test.cpp");
    QVERIFY(tempCpp.open()); tempCpp.close();
    QVERIFY(!provider.icon(QFileInfo(tempCpp.fileName())).isNull());

    // Meson build icon.
    QTemporaryFile tempMeson(tempDir.path() + "/meson.build");
    QVERIFY(tempMeson.open()); tempMeson.close();
    QVERIFY(!provider.icon(QFileInfo(tempMeson.fileName())).isNull());
}

/**
 * @brief Tests cutter dock styling density, dock features, and workspace snapshot export.
 */
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

/**
 * @brief Tests injecting interactive tutorial breakpoints into ConfigManager.
 */
void TestMainWindowUI::testTutorialBreakpointInjection() {
    const auto originalBps = gridlock::core::ConfigManager::instance().getBreakpoints();

    QString relativePath = "tutorial/inspection_demo.cpp";
    QSet<int> expectedLines = gridlock::ui::TutorialDialog::getBreakpointsForFile(relativePath);
    QCOMPARE(expectedLines.size(), 1);
    QVERIFY(expectedLines.contains(76));

    QString absPath = QFileInfo(relativePath).absoluteFilePath();

    // Clear breakpoints and inject tutorial breakpoint.
    QMap<QString, QSet<int>> emptyBps;
    gridlock::core::ConfigManager::instance().saveBreakpoints(emptyBps);

    gridlock::ui::TutorialDialog::injectBreakpoints(relativePath, absPath);

    // Verify injected breakpoint persisted.
    QMap<QString, QSet<int>> savedBps = gridlock::core::ConfigManager::instance().getBreakpoints();
    QVERIFY(savedBps.contains(absPath));
    QCOMPARE(savedBps[absPath], expectedLines);

    // Restore original breakpoints.
    gridlock::core::ConfigManager::instance().saveBreakpoints(originalBps);
}

/**
 * @brief Tests cutter zone grouping and address seek bar synchronization.
 */
void TestMainWindowUI::testCutterZoneGroupingAndSeekBar() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);

    // 1. Verify Registers and Watch Expressions docks are in RightDockWidgetArea
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

/**
 * @brief Tests linear and CFG graph disassembly views, branch arcs, and basic block counts.
 */
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

/**
 * @brief Tests register stepping diffs, hexdump byte diffs, and register seek synchronization.
 */
void TestMainWindowUI::testCutterHexdumpDiffAndRegisterSeekSync() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);

    gridlock::ui::RegisterView* registerView = mainWindow.registerView();
    gridlock::ui::MemView* memView = mainWindow.memView();
    gridlock::ui::DisassemblyView* disassemblyView = mainWindow.disassemblyView();
    QVERIFY(registerView != nullptr && memView != nullptr && disassemblyView != nullptr);

    // Initial register state.
    QMap<QString, QString> initialRegs = {{"rip", "0x00401120"}, {"rax", "0x0"}, {"rsp", "0x7fffffffe480"}};
    registerView->updateRegisters(initialRegs);
    QCOMPARE(registerView->changedRegisterCount(), 0);
    QVERIFY(!registerView->isRegisterModified("rip"));

    // Stepped register state with rip and rax changed.
    QMap<QString, QString> steppedRegs = {{"rip", "0x0040113d"}, {"rax", "0x1"}, {"rsp", "0x7fffffffe480"}};
    registerView->updateRegisters(steppedRegs);
    QCOMPARE(registerView->changedRegisterCount(), 2);
    QVERIFY(registerView->isRegisterModified("rip") && registerView->isRegisterModified("rax") && !registerView->isRegisterModified("rsp"));

    // Initial memory buffer.
    QByteArray bufA(16, '\x00');
    bufA[0] = '\x55'; bufA[1] = '\x48'; bufA[2] = '\x89'; bufA[3] = '\xe5';
    memView->setMemoryData("0x00401120", bufA);
    QCOMPARE(memView->changedByteCount(), 0);

    // Modified memory buffer with 3 changed bytes.
    QByteArray bufB = bufA;
    bufB[4] = '\x90'; bufB[8] = '\x01'; bufB[15] = '\xff';
    memView->setMemoryData("0x00401120", bufB);
    QCOMPARE(memView->changedByteCount(), 3);
    QVERIFY(memView->seekToAddress(0x0040113dULL));
    QCOMPARE(memView->currentBaseAddress(), 0x0040113dULL);

    // Seek to address.
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401120"));
    QCOMPARE(disassemblyView->currentAddress(), 0x00401120ULL);
    QCOMPARE(memView->currentBaseAddress(), 0x00401120ULL);

    // Double-click or activate register in RegisterView to seek.
    QVERIFY(registerView->activateRegister("rip"));
    auto* seekBar = mainWindow.findChild<QLineEdit*>("cutterSeekBar");
    auto* statusTelemetry = mainWindow.findChild<QLabel*>("cutterStatusTelemetry");
    QVERIFY(seekBar != nullptr && statusTelemetry != nullptr);
    QVERIFY(seekBar->text().contains("40113d", Qt::CaseInsensitive));
    QVERIFY(statusTelemetry->text().contains("40113d", Qt::CaseInsensitive));
    QCOMPARE(disassemblyView->currentAddress(), 0x0040113dULL);
    QCOMPARE(memView->currentBaseAddress(), 0x0040113dULL);
}

/**
 * @brief Tests symbol sidebar selection and address space navigation ribbon.
 */
void TestMainWindowUI::testSymbolSidebarAndNavRibbon() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    QWidget* navBar = mainWindow.findChild<QWidget*>("addressNavBar");
    QVERIFY(navBar != nullptr);

    auto* projectExplorer = mainWindow.findChild<gridlock::ui::ProjectExplorerWidget*>();
    QVERIFY(projectExplorer != nullptr);
    projectExplorer->setSymbolsMode(true);
    QVERIFY(projectExplorer->isSymbolsMode() && projectExplorer->symbolCount() >= 4);

    // Activate sym.main.
    QVERIFY(projectExplorer->activateSymbol("sym.main"));
    QCOMPARE(mainWindow.disassemblyView()->currentAddress(), 0x00401120ULL);
    QCOMPARE(mainWindow.memView()->currentBaseAddress(), 0x00401120ULL);

    // Activate PLT stub.
    QVERIFY(projectExplorer->activateSymbol("MPI_Finalize@plt"));
    QCOMPARE(mainWindow.disassemblyView()->currentAddress(), 0x00401060ULL);
    QCOMPARE(mainWindow.memView()->currentBaseAddress(), 0x00401060ULL);

    // Server rack rank nodes check.
    auto* serverRackView = mainWindow.findChild<gridlock::ui::ServerRackView*>();
    QVERIFY(serverRackView != nullptr && serverRackView->rankNodeCount() >= 4);
}

/**
 * @brief Tests repository file size boundaries (all source files < 1000 lines).
 */
void TestMainWindowUI::testSourceFileLineLimits() {
    QDir rootDir(QCoreApplication::applicationDirPath() + "/..");
    if (!rootDir.exists("src")) rootDir = QDir::current();
    const QStringList dirsToScan = {rootDir.filePath("src"), rootDir.filePath("tests")};
    QStringList allFiles;
    for (const QString& dirPath : dirsToScan) {
        QDirIterator it(dirPath, {"*.cpp", "*.hpp"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) allFiles.append(it.next());
    }
    allFiles.sort();
    for (const QString& filePath : allFiles) {
        QFile file(filePath);
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(filePath));
        int lines = 0;
        while (!file.atEnd()) { file.readLine(); ++lines; }
        // Verify line ceiling.
        QVERIFY2(lines < 1000, qPrintable(QString("%1 exceeds 1000-line limit (%2 lines)").arg(filePath).arg(lines)));
    }
}

QTEST_MAIN(TestMainWindowUI)
