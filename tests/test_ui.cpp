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
#include <QDirIterator>
#include <QTemporaryDir>
#include <QDockWidget>
#include <QImage>
#include <QLabel>
#include <QTabWidget>
#include <QToolBar>
#include "../src/ui/tutorial/TutorialDialog.hpp"
#include "../src/ui/views/MemView.hpp"
#include "../src/ui/views/RegisterView.hpp"
#include "../src/ui/widgets/ProjectExplorerWidget.hpp"
#include "../src/core/utils/CliParser.cpp"

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
    highlighter.rehighlight();
    QTextBlock block = doc.findBlockByLineNumber(0);
    QVector<QTextLayout::FormatRange> formats = block.layout()->formats();
    QString opcodeColor = gridlock::core::ConfigManager::instance().getAssemblyOpcode();
    QString registerColor = gridlock::core::ConfigManager::instance().getAssemblyRegister();
    QString addressColor = gridlock::core::ConfigManager::instance().getAssemblyAddress();
    bool foundOpcode = false, foundRegister = false, foundAddress = false;
    for (const auto& fmt : formats) {
        QString fmtColor = fmt.format.foreground().color().name(QColor::HexRgb);
        if (fmtColor == QColor(opcodeColor).name(QColor::HexRgb)) foundOpcode = true;
        if (fmtColor == QColor(registerColor).name(QColor::HexRgb)) foundRegister = true;
        if (fmtColor == QColor(addressColor).name(QColor::HexRgb)) foundAddress = true;
    }
    QVERIFY(foundOpcode && foundRegister && foundAddress);
}

void TestMainWindowUI::testSourceCodeHighlightBounds() {
    gridlock::ui::SourceCodeView view;
    QString code;
    for (int i = 1; i <= 100; ++i) code += QString("int line_%1 = 0;\n").arg(i);
    view.setSourceCode(code);
    view.highlightCurrentLine(83);
    QTextBlock block = view.textCursor().block();
    QCOMPARE(block.blockNumber(), 82);
    view.highlightCurrentLine(200);
    view.highlightCurrentLine(0);
    view.highlightCurrentLine(-1);
    QVERIFY(true);
}

void TestMainWindowUI::testDifferentialGridExpansion() {
    gridlock::ui::DifferentialGrid grid;
    for (int rankId = 0; rankId < 6; ++rankId) {
        for (int v = 0; v < 15; ++v) {
            QString varName = QString("var_%1").arg(v);
            QString value = (rankId == 5 && v == 0) ? "divergent_value" : "100";
            grid.updateVariableDisplay(rankId, varName, value);
        }
    }
    QCOMPARE(grid.rowCount(), 7);
    QCOMPARE(grid.columnCount(), 16);
    int colVar0 = -1;
    for (int c = 1; c < grid.columnCount(); ++c) {
        if (grid.horizontalHeaderItem(c)->text() == "var_0") { colVar0 = c; break; }
    }
    QVERIFY(colVar0 != -1);
    QTableWidgetItem* itemNormal = grid.item(1, colVar0);
    QVERIFY(itemNormal != nullptr);
    QCOMPARE(itemNormal->text(), QString("100"));
    QCOMPARE(itemNormal->background(), QBrush());
    QTableWidgetItem* itemDivergent = grid.item(6, colVar0);
    QVERIFY(itemDivergent != nullptr);
    QCOMPARE(itemDivergent->text(), QString("divergent_value"));
    QCOMPARE(itemDivergent->background().color(), QColor(255, 200, 200));
}

void TestMainWindowUI::testDifferentialGridEmits() {
    gridlock::ui::DifferentialGrid grid;
    int emitCount = 0;
    QObject::connect(&grid, &gridlock::ui::DifferentialGrid::watchVariableAdded, [&emitCount](const QString&) { emitCount++; });
    QHash<QString, QString> vars;
    vars["offset"] = "123";
    grid.setVariableData(0, vars);
    QCOMPARE(emitCount, 0);
    grid.item(0, 0)->setText("mtype");
    QCOMPARE(emitCount, 1);
}

void TestMainWindowUI::testGdbConsoleFiltering() {
    gridlock::ui::GdbConsoleWidget console;
    console.resetRanks(4);
    console.appendGdbOutput(0, "~\"Breakpoint hit at main\\n\"");
    console.appendGdbOutput(1, "~\"Segfault encountered\\n\"");
    console.appendGdbOutput(0, "~\"Variable x = 5\\n\"");
    QPlainTextEdit* consoleEdit = console.findChild<QPlainTextEdit*>();
    QComboBox* rankCombo = console.findChild<QComboBox*>();
    QLineEdit* filterEdit = console.findChild<QLineEdit*>();
    QVERIFY(consoleEdit && rankCombo && filterEdit);
    rankCombo->setCurrentIndex(2);
    QCOMPARE(consoleEdit->toPlainText(), QString("Segfault encountered"));
    rankCombo->setCurrentIndex(0);
    filterEdit->setText("Variable");
    QCOMPARE(consoleEdit->toPlainText(), QString("Variable x = 5"));
}

void TestMainWindowUI::testGutterBreakpointPropagation() {
    gridlock::ui::SourceCodeView view;
    QString code;
    for (int i = 1; i <= 10; ++i) code += QString("Line %1\n").arg(i);
    view.setSourceCode(code);
    view.setCurrentFile("/test/dummy.cpp");
    view.resize(800, 600);
    QApplication::processEvents();
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
    QMouseEvent event(QEvent::MouseButtonPress, QPointF(15, yPos), QPointF(15, yPos), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
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
    QVERIFY(!provider.icon(QFileInfo(tempDir.path())).isNull());
    QTemporaryFile tempCpp(tempDir.path() + "/test.cpp");
    QVERIFY(tempCpp.open()); tempCpp.close();
    QVERIFY(!provider.icon(QFileInfo(tempCpp.fileName())).isNull());
    QTemporaryFile tempMeson(tempDir.path() + "/meson.build");
    QVERIFY(tempMeson.open()); tempMeson.close();
    QVERIFY(!provider.icon(QFileInfo(tempMeson.fileName())).isNull());
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
    QVERIFY(registerView != nullptr && memView != nullptr && disassemblyView != nullptr);

    QMap<QString, QString> initialRegs = {{"rip", "0x00401120"}, {"rax", "0x0"}, {"rsp", "0x7fffffffe480"}};
    registerView->updateRegisters(initialRegs);
    QCOMPARE(registerView->changedRegisterCount(), 0);
    QVERIFY(!registerView->isRegisterModified("rip"));

    QMap<QString, QString> steppedRegs = {{"rip", "0x0040113d"}, {"rax", "0x1"}, {"rsp", "0x7fffffffe480"}};
    registerView->updateRegisters(steppedRegs);
    QCOMPARE(registerView->changedRegisterCount(), 2);
    QVERIFY(registerView->isRegisterModified("rip") && registerView->isRegisterModified("rax") && !registerView->isRegisterModified("rsp"));

    QByteArray bufA(16, '\x00');
    bufA[0] = '\x55'; bufA[1] = '\x48'; bufA[2] = '\x89'; bufA[3] = '\xe5';
    memView->setMemoryData("0x00401120", bufA);
    QCOMPARE(memView->changedByteCount(), 0);

    QByteArray bufB = bufA;
    bufB[4] = '\x90'; bufB[8] = '\x01'; bufB[15] = '\xff';
    memView->setMemoryData("0x00401120", bufB);
    QCOMPARE(memView->changedByteCount(), 3);
    QVERIFY(memView->seekToAddress(0x0040113dULL));
    QCOMPARE(memView->currentBaseAddress(), 0x0040113dULL);

    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401120"));
    QCOMPARE(disassemblyView->currentAddress(), 0x00401120ULL);
    QCOMPARE(memView->currentBaseAddress(), 0x00401120ULL);

    QVERIFY(registerView->activateRegister("rip"));
    auto* seekBar = mainWindow.findChild<QLineEdit*>("cutterSeekBar");
    auto* statusTelemetry = mainWindow.findChild<QLabel*>("cutterStatusTelemetry");
    QVERIFY(seekBar != nullptr && statusTelemetry != nullptr);
    QVERIFY(seekBar->text().contains("40113d", Qt::CaseInsensitive));
    QVERIFY(statusTelemetry->text().contains("40113d", Qt::CaseInsensitive));
    QCOMPARE(disassemblyView->currentAddress(), 0x0040113dULL);
    QCOMPARE(memView->currentBaseAddress(), 0x0040113dULL);
}

void TestMainWindowUI::testSymbolSidebarAndNavRibbon() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    QWidget* navBar = mainWindow.findChild<QWidget*>("addressNavBar");
    QVERIFY(navBar != nullptr);

    auto* projectExplorer = mainWindow.findChild<gridlock::ui::ProjectExplorerWidget*>();
    QVERIFY(projectExplorer != nullptr);
    projectExplorer->setSymbolsMode(true);
    QVERIFY(projectExplorer->isSymbolsMode() && projectExplorer->symbolCount() >= 4);

    QVERIFY(projectExplorer->activateSymbol("sym.main"));
    QCOMPARE(mainWindow.disassemblyView()->currentAddress(), 0x00401120ULL);
    QCOMPARE(mainWindow.memView()->currentBaseAddress(), 0x00401120ULL);

    QVERIFY(projectExplorer->activateSymbol("MPI_Finalize@plt"));
    QCOMPARE(mainWindow.disassemblyView()->currentAddress(), 0x00401060ULL);
    QCOMPARE(mainWindow.memView()->currentBaseAddress(), 0x00401060ULL);

    auto* serverRackView = mainWindow.findChild<gridlock::ui::ServerRackView*>();
    QVERIFY(serverRackView != nullptr && serverRackView->rankNodeCount() >= 4);
}

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
        QVERIFY2(lines < 1000, qPrintable(QString("%1 exceeds 1000-line limit (%2 lines)").arg(filePath).arg(lines)));
    }
}

void TestMainWindowUI::testDockResizabilityAndXrefDecompiler() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    // 1. Left & Right Dock Resizability
    QDockWidget* leftDock = nullptr;
    QDockWidget* rightDock = nullptr;
    const auto docks = mainWindow.findChildren<QDockWidget*>();
    for (QDockWidget* dock : docks) {
        const auto area = mainWindow.dockWidgetArea(dock);
        if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) {
            QVERIFY2(dock->minimumWidth() <= 200, qPrintable(dock->windowTitle()));
            QVERIFY2(dock->maximumWidth() >= 1000, qPrintable(dock->windowTitle()));
            if (dock->widget()) {
                QVERIFY2(dock->widget()->minimumWidth() <= 200, qPrintable(dock->windowTitle()));
                QVERIFY2(dock->widget()->maximumWidth() >= 1000, qPrintable(dock->windowTitle()));
            }
            if (area == Qt::LeftDockWidgetArea && !leftDock) leftDock = dock;
            if (area == Qt::RightDockWidgetArea && dock->objectName() == QStringLiteral("RegisterViewDock")) rightDock = dock;
        }
    }
    QVERIFY(leftDock != nullptr && rightDock != nullptr);

    mainWindow.resizeDocks({leftDock}, {340}, Qt::Horizontal);
    mainWindow.resizeDocks({rightDock}, {420}, Qt::Horizontal);
    QTest::qWait(30);
    QApplication::processEvents();

    QVERIFY2(std::abs(leftDock->width() - 340) <= 30,
             qPrintable(QString("Left dock width %1 did not resize near 340").arg(leftDock->width())));
    QVERIFY2(std::abs(rightDock->width() - 420) <= 30,
             qPrintable(QString("Right dock width %1 did not resize near 420").arg(rightDock->width())));

    // 2. Xrefs & Decompiler Mode in DisassemblyView
    auto* disassemblyView = mainWindow.disassemblyView();
    QVERIFY(disassemblyView != nullptr);

    const QVector<uint64_t> xrefs = disassemblyView->xrefsToAddress(0x00401149ULL);
    QVERIFY2(xrefs.size() >= 2,
             qPrintable(QString("Expected >= 2 xrefs to 0x00401149, got %1").arg(xrefs.size())));
    QVERIFY(xrefs.contains(0x00401163ULL));
    QVERIFY(xrefs.contains(0x0040116eULL));

    const QString pseudocode = disassemblyView->decompiledPseudocode();
    QVERIFY(!pseudocode.isEmpty());
    QVERIFY(pseudocode.contains("int main(") || pseudocode.contains("sym.main"));
    QVERIFY(pseudocode.contains("MPI_Init"));
    QVERIFY(pseudocode.contains("401149"));

    disassemblyView->setDecompilerMode(true);
    QVERIFY(disassemblyView->isDecompilerMode());
    disassemblyView->setDecompilerMode(false);
    QVERIFY(!disassemblyView->isDecompilerMode());
}

void TestMainWindowUI::testMemViewToolbarNoTruncationAndDisassemblyLayout() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QTest::qWait(30);
    QApplication::processEvents();

    // 1. MemView toolbar button & input non-truncation check
    auto* memView = mainWindow.memView();
    QVERIFY(memView != nullptr);
    if (auto* bottomTabs = mainWindow.findChild<QTabWidget*>("BottomConsoleTabs")) {
        bottomTabs->setCurrentWidget(memView);
        QApplication::processEvents();
    }

    const auto buttons = memView->findChildren<QPushButton*>();
    QVERIFY(!buttons.isEmpty());
    for (QPushButton* btn : buttons) {
        if (!btn->isVisible()) {
            continue;
        }
        const int minRequiredW = btn->fontMetrics().horizontalAdvance(btn->text()) + 12;
        QVERIFY2(btn->width() >= minRequiredW,
                 qPrintable(QString("MemView button '%1' width (%2) is narrower than required (%3)")
                                .arg(btn->text())
                                .arg(btn->width())
                                .arg(minRequiredW)));
    }

    auto* addrInput = memView->findChild<QLineEdit*>();
    QVERIFY(addrInput != nullptr);
    QVERIFY2(addrInput->width() >= 90,
             qPrintable(QString("MemView address input width (%1) < 90").arg(addrInput->width())));

    // 2. Central horizontal splitter gives DisassemblyView >= 420px on 1440x900
    auto* disassemblyView = mainWindow.disassemblyView();
    QVERIFY(disassemblyView != nullptr);
    QVERIFY2(disassemblyView->width() >= 420,
             qPrintable(QString("DisassemblyView width (%1) < 420 on 1440x900 window")
                            .arg(disassemblyView->width())));

    // 3. Every .cpp and .hpp file in src/ and tests/ has strictly < 950 lines
    QDir rootDir(QCoreApplication::applicationDirPath() + "/..");
    if (!rootDir.exists("src")) {
        rootDir = QDir::current();
    }
    const QStringList dirsToScan = {rootDir.filePath("src"), rootDir.filePath("tests")};
    QStringList allFiles;
    for (const QString& dirPath : dirsToScan) {
        QDirIterator it(dirPath, {"*.cpp", "*.hpp"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            allFiles.append(it.next());
        }
    }
    allFiles.sort();
    for (const QString& filePath : allFiles) {
        QFile file(filePath);
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(filePath));
        int lines = 0;
        while (!file.atEnd()) {
            file.readLine();
            ++lines;
        }
        QVERIFY2(lines < 950,
                 qPrintable(QString("%1 exceeds 950-line headroom limit (%2 lines)")
                                .arg(filePath)
                                .arg(lines)));
    }
}

void TestMainWindowUI::testOffscreenDefaultAndPreferredScreenRouting() {
    gridlock::ui::MainWindow mainWindow;
    const auto docks = mainWindow.findChildren<QDockWidget*>();
    QVERIFY(!docks.isEmpty());
    for (QDockWidget* dock : docks) {
        if (!dock) continue;
        if (QWidget* tb = dock->titleBarWidget()) {
            QVERIFY2(tb->parentWidget() == dock,
                     qPrintable(QString("Dock '%1' has titleBarWidget with parent '%2' instead of dock")
                                    .arg(dock->objectName())
                                    .arg(tb->parentWidget() ? tb->parentWidget()->objectName() : "nullptr")));
            QVERIFY2(!tb->isWindow(),
                     qPrintable(QString("Dock '%1' titleBarWidget is a top-level window").arg(dock->objectName())));
        }
    }

    QScreen* primary = QGuiApplication::primaryScreen();
    QVERIFY(primary != nullptr);
    QCOMPARE(mainWindow.resolvePreferredScreen(""), primary);
    QCOMPARE(mainWindow.resolvePreferredScreen("nonexistent_monitor_xyz"), primary);

    mainWindow.applyPreferredScreenRouting("27", true);
    QVERIFY(mainWindow.testAttribute(Qt::WA_ShowWithoutActivating));
    QCOMPARE(mainWindow.preferredMonitorHint(), QStringLiteral("27"));

    mainWindow.applyPreferredScreenRouting("2560x1440", false);
    QVERIFY(!mainWindow.testAttribute(Qt::WA_ShowWithoutActivating));
    QCOMPARE(mainWindow.preferredMonitorHint(), QStringLiteral("2560x1440"));

    auto& cfg = gridlock::core::ConfigManager::instance();
    const QString prevMonitor = cfg.preferredMonitor();
    const bool prevUnfocused = cfg.openUnfocused();

    QTemporaryDir tmpConfigDir;
    QVERIFY(tmpConfigDir.isValid());
    const QByteArray prevTestDir = qgetenv("GRIDLOCK_TEST_CONFIG_DIR");
    qputenv("GRIDLOCK_TEST_CONFIG_DIR", tmpConfigDir.path().toUtf8());
    cfg.loadConfig();

    cfg.setPreferredMonitor("2560x1440");
    cfg.setOpenUnfocused(true);
    QCOMPARE(cfg.preferredMonitor(), QStringLiteral("2560x1440"));
    QVERIFY(cfg.openUnfocused());
    cfg.loadConfig();
    QCOMPARE(cfg.preferredMonitor(), QStringLiteral("2560x1440"));
    QVERIFY(cfg.openUnfocused());

    cfg.setPreferredMonitor("1920x1080");
    cfg.setOpenUnfocused(false);
    QCOMPARE(cfg.preferredMonitor(), QStringLiteral("1920x1080"));
    QVERIFY(!cfg.openUnfocused());
    cfg.loadConfig();
    QCOMPARE(cfg.preferredMonitor(), QStringLiteral("1920x1080"));
    QVERIFY(!cfg.openUnfocused());

    if (prevTestDir.isEmpty()) qunsetenv("GRIDLOCK_TEST_CONFIG_DIR");
    else qputenv("GRIDLOCK_TEST_CONFIG_DIR", prevTestDir);
    cfg.loadConfig();
    cfg.setPreferredMonitor(prevMonitor);
    cfg.setOpenUnfocused(prevUnfocused);
}

void TestMainWindowUI::testNavigationHistoryStringsAndBookmarks() {
    gridlock::ui::MainWindow mainWindow;
    auto* disassemblyView = mainWindow.disassemblyView();
    auto* projectExplorer = mainWindow.projectExplorerWidget();
    auto* memView = mainWindow.memView();
    QVERIFY(disassemblyView != nullptr);
    QVERIFY(projectExplorer != nullptr);
    QVERIFY(memView != nullptr);

    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401120"));
    QCOMPARE(disassemblyView->currentAddress(), 0x00401120ULL);
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401149"));
    QCOMPARE(disassemblyView->currentAddress(), 0x00401149ULL);
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401178"));
    QCOMPARE(disassemblyView->currentAddress(), 0x00401178ULL);
    QVERIFY(mainWindow.canNavigateBack());
    QVERIFY(!mainWindow.canNavigateForward());

    QVERIFY(mainWindow.navigateBack());
    QCOMPARE(disassemblyView->currentAddress(), 0x00401149ULL);
    QVERIFY(mainWindow.canNavigateBack());
    QVERIFY(mainWindow.canNavigateForward());
    QVERIFY(mainWindow.navigateForward());
    QCOMPARE(disassemblyView->currentAddress(), 0x00401178ULL);
    QVERIFY(!mainWindow.canNavigateForward());

    projectExplorer->setStringsMode(true);
    QVERIFY(projectExplorer->isStringsMode());
    QVERIFY(projectExplorer->stringCount() >= 4);

    QSignalSpy spyStr(projectExplorer, &gridlock::ui::ProjectExplorerWidget::symbolAddressSelected);
    QVERIFY(projectExplorer->activateString("MPI_COMM_WORLD"));
    QCOMPARE(spyStr.count(), 1);
    const uint64_t strAddr = spyStr.at(0).at(0).toULongLong();
    QCOMPARE(strAddr, 0x00402000ULL);
    QCOMPARE(memView->currentBaseAddress(), 0x00402000ULL);
    QVERIFY(mainWindow.canNavigateBack());

    disassemblyView->setAddressComment(0x00401149ULL, " Outer loop rank check ");
    QCOMPARE(disassemblyView->addressComment(0x00401149ULL), QStringLiteral("Outer loop rank check"));
    QVERIFY(!disassemblyView->isBookmarked(0x00401149ULL));
    QVERIFY(disassemblyView->toggleBookmark(0x00401149ULL));
    QVERIFY(disassemblyView->isBookmarked(0x00401149ULL));
    QVERIFY(disassemblyView->bookmarkedAddresses().contains(0x00401149ULL));
    QVERIFY(!disassemblyView->toggleBookmark(0x00401149ULL));
    QVERIFY(!disassemblyView->isBookmarked(0x00401149ULL));
}

void TestMainWindowUI::testCfgGraphDecompilerLanguagesAndSourceFallback() {
    gridlock::ui::MainWindow mainWindow;
    auto* disassemblyView = mainWindow.disassemblyView();
    QVERIFY(disassemblyView != nullptr);

    disassemblyView->setDecompilerTargetLanguage("C");
    QCOMPARE(disassemblyView->decompilerTargetLanguage(), QStringLiteral("C"));
    const QString cOut = disassemblyView->decompileToLanguage("C");
    QVERIFY(cOut.contains("int32_t") || cOut.contains("var_"));
    QVERIFY(cOut.contains("if (") || cOut.contains("while ("));
    QVERIFY(cOut.contains("MPI_Init"));

    const QString cppOut = disassemblyView->decompileToLanguage("C++");
    const QString pseudoOut = disassemblyView->decompileToLanguage("Pseudocode");
    QVERIFY(!cppOut.isEmpty() && !pseudoOut.isEmpty());
    QVERIFY(cppOut != cOut && pseudoOut != cOut && cppOut != pseudoOut);

    const QString sigHeader = disassemblyView->functionSignatureHeader();
    QVERIFY(!sigHeader.isEmpty() && sigHeader.contains("sym.main(int argc, char **argv)"));
    disassemblyView->setGraphMode(true);
    QVERIFY(disassemblyView->isGraphMode() && disassemblyView->basicBlockCount() >= 3);

    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401120"));
    QVERIFY(!mainWindow.isShowingDecompilerFallback());
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401000"));
    QVERIFY(mainWindow.isShowingDecompilerFallback());
}

void TestMainWindowUI::testUnifiedChromeMetricsAndSplitGraphDecompiler() {
    gridlock::core::managers::ThemeManager::instance().initialize();
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));

    auto* disassemblyView = mainWindow.disassemblyView();
    QVERIFY(disassemblyView != nullptr);
    disassemblyView->setSplitGraphDecompilerMode(true);
    QVERIFY(disassemblyView->isSplitGraphDecompilerMode());
    QCoreApplication::processEvents();

    auto* cfgGraphView = disassemblyView->findChild<QGraphicsView*>("cutterCfgGraphView");
    auto* decompView = disassemblyView->findChild<QPlainTextEdit*>("cutterDecompilerView");
    QVERIFY(cfgGraphView && decompView);
    QVERIFY(cfgGraphView->isVisibleTo(disassemblyView) && decompView->isVisibleTo(disassemblyView));
    QVERIFY(cfgGraphView->width() >= 180 && decompView->width() >= 180);
    mainWindow.captureWorkspaceSnapshot("/tmp/gridlock_ui_v11_split.png");

    const QList<QWidget*> scopes = { &mainWindow, mainWindow.memView(), mainWindow.projectExplorerWidget(), disassemblyView };
    for (QWidget* s : scopes) {
        if (!s) continue;
        for (auto* btn : s->findChildren<QPushButton*>()) if (btn->isVisible()) QVERIFY2(btn->height() <= 28, qPrintable(btn->objectName()));
        for (auto* edit : s->findChildren<QLineEdit*>()) if (edit->isVisible()) QVERIFY2(edit->height() <= 28, qPrintable(edit->objectName()));
        for (auto* combo : s->findChildren<QComboBox*>()) if (combo->isVisible()) QVERIFY2(combo->height() <= 28, qPrintable(combo->objectName()));
    }
    for (auto* tb : mainWindow.findChildren<QToolBar*>()) if (tb->isVisible()) QVERIFY2(tb->height() <= 36, qPrintable(tb->objectName()));
}

void TestMainWindowUI::testCrossPaneChoreographyHistoryAndStrings() {
    gridlock::ui::MainWindow mainWindow;
    auto* disassemblyView = mainWindow.disassemblyView();
    auto* projectExplorer = mainWindow.projectExplorerWidget();
    auto* memView = mainWindow.memView();
    QVERIFY(disassemblyView && projectExplorer && memView);

    disassemblyView->setSplitGraphDecompilerMode(true);
    QVERIFY(disassemblyView->seekToAddress(0x0040113dULL));
    const int initLine = disassemblyView->currentDecompilerLine();
    QVERIFY(initLine > 0);
    QCOMPARE(disassemblyView->decompilerLineToAddress(initLine), 0x0040113dULL);
    QCOMPARE(disassemblyView->addressToDecompilerLine(0x0040113dULL), initLine);

    const int loopLine = disassemblyView->addressToDecompilerLine(0x00401149ULL);
    QVERIFY(loopLine > 0);
    QVERIFY(disassemblyView->selectDecompilerLine(loopLine));
    QCOMPARE(disassemblyView->currentAddress(), 0x00401149ULL);
    QCOMPARE(memView->currentBaseAddress(), 0x00401149ULL);

    disassemblyView->highlightToken("argc");
    QCOMPARE(disassemblyView->highlightedToken(), QStringLiteral("argc"));

    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401120"));
    QVERIFY(mainWindow.seekToAddressOrExpression("0x0040113d"));
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401178"));
    QVERIFY(mainWindow.canNavigateBack());
    QVERIFY(mainWindow.navigateBack());
    QCOMPARE(disassemblyView->currentAddress(), 0x0040113dULL);
    QCOMPARE(memView->currentBaseAddress(), 0x0040113dULL);
    QVERIFY(mainWindow.navigateForward());
    QCOMPARE(disassemblyView->currentAddress(), 0x00401178ULL);

    projectExplorer->setStringsMode(true);
    QVERIFY(projectExplorer->isStringsMode() && projectExplorer->stringCount() >= 4);
    QVERIFY(projectExplorer->activateString("MPI_COMM_WORLD"));
    QCOMPARE(memView->currentBaseAddress(), 0x00402000ULL);

    disassemblyView->setAddressComment(0x00401149ULL, "Rank loop header");
    QCOMPARE(disassemblyView->addressComment(0x00401149ULL), QStringLiteral("Rank loop header"));
    QVERIFY(disassemblyView->toggleBookmark(0x00401149ULL));
    QVERIFY(disassemblyView->isBookmarked(0x00401149ULL));
}

void TestMainWindowUI::testCfgRankDivergenceMinimapAndRackSync() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));

    auto* disassemblyView = mainWindow.disassemblyView();
    auto* serverRackView = mainWindow.serverRackView();
    auto* statusTelemetry = mainWindow.findChild<QLabel*>("cutterStatusTelemetry");
    QVERIFY(disassemblyView && serverRackView && statusTelemetry);

    disassemblyView->setSplitGraphDecompilerMode(true);
    disassemblyView->setRankAddresses({{0, 0x00401168ULL}, {1, 0x00401155ULL}, {2, 0x00401157ULL}, {3, 0x0040115fULL}});
    QCOMPARE(disassemblyView->ranksAtBlock(0x00401168ULL), QVector<int>{0});
    QCOMPARE(disassemblyView->ranksAtBlock(0x0040114fULL), QVector<int>{1});
    QCOMPARE(disassemblyView->ranksAtBlock(0x00401157ULL), (QVector<int>{2, 3}));

    QVERIFY(disassemblyView->isGraphMinimapVisible());
    const QRect mmRect = disassemblyView->minimapRect();
    QVERIFY(mmRect.width() >= 80 && mmRect.height() >= 60);
    disassemblyView->setGraphMinimapVisible(false);
    QVERIFY(!disassemblyView->isGraphMinimapVisible());
    disassemblyView->setGraphMinimapVisible(true);
    QVERIFY(disassemblyView->isGraphMinimapVisible());

    QVERIFY(serverRackView->selectRank(1));
    QCOMPARE(serverRackView->selectedRank(), 1);
    QVERIFY(statusTelemetry->text().contains("MPI RANK: 1"));
    QCOMPARE(disassemblyView->currentAddress(), 0x00401155ULL);
    QVERIFY(disassemblyView->currentDecompilerLine() > 0);
}

void TestMainWindowUI::testDynamicRankSyncWithoutHardcodedConstructorMocks() {
    // 1. Standalone widgets start clean with zero hardcoded mock ranks
    gridlock::ui::ServerRackView standaloneRack;
    QCOMPARE(standaloneRack.rankNodeCount(), 0);

    gridlock::ui::DisassemblyView standaloneDisasm;
    QVERIFY(standaloneDisasm.ranksAtBlock(0x00401155ULL).isEmpty());
    QVERIFY(standaloneDisasm.ranksAtBlock(0x00401168ULL).isEmpty());

    // 2. Dynamic updateRanks(QList<RankState>) populates rank cards and PC addresses
    QList<gridlock::RankState> states;
    for (int r = 0; r < 4; ++r) {
        gridlock::RankState st;
        st.rankId = r;
        st.currentState = "stopped";
        st.registers[16] = (r == 0) ? QStringLiteral("0x00401168") : QStringLiteral("0x00401155");
        states.append(st);
    }
    standaloneRack.updateRanks(states);
    QCOMPARE(standaloneRack.rankNodeCount(), 4);
    QCOMPARE(standaloneRack.rankPcAddress(0), 0x00401168ULL);
    QCOMPARE(standaloneRack.rankPcAddress(1), 0x00401155ULL);

    // 3. MainWindow dynamic rank synchronization from ServerRackView -> DisassemblyView
    gridlock::ui::MainWindow mainWindow;
    auto* rack = mainWindow.serverRackView();
    auto* disasm = mainWindow.disassemblyView();
    QVERIFY(rack && disasm);

    rack->updateRanks(states);
    QCOMPARE(rack->rankNodeCount(), 4);
    QCOMPARE(disasm->ranksAtBlock(0x00401168ULL), QVector<int>{0});
    QCOMPARE(disasm->ranksAtBlock(0x0040114fULL), (QVector<int>{1, 2, 3}));

    // 4. loadPreviewFixture() populates 8 preview ranks via the dynamic pipeline
    mainWindow.loadPreviewFixture();
    QCOMPARE(rack->rankNodeCount(), 8);
    QCOMPARE(disasm->ranksAtBlock(0x00401120ULL), QVector<int>{0});
    QCOMPARE(disasm->ranksAtBlock(0x00401157ULL), (QVector<int>{2, 3, 4, 5, 6, 7}));
}

static void auditFilesGoogleStyleAndDocumentation(const QStringList& targetFiles) {
    for (const QString& relPath : targetFiles) {
        QFile f(relPath);
        QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(relPath));
        const QString content = QString::fromUtf8(f.readAll());
        const QStringList lines = content.split('\n');
        QVERIFY2(lines.size() < 950, qPrintable(QString("%1 has %2 lines (>= 950)").arg(relPath).arg(lines.size())));
        QVERIFY2(content.contains("@file") && content.contains("@brief"),
                 qPrintable(QString("%1 missing @file/@brief header banner").arg(relPath)));
        if (content.contains("\nnamespace ") || content.startsWith("namespace ")) {
            QVERIFY2(content.contains("// namespace "),
                     qPrintable(QString("%1 missing Google C++ closing // namespace comment").arg(relPath)));
        }
        int nonEmptyLines = 0, commentLines = 0;
        for (const QString& line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;
            ++nonEmptyLines;
            if (trimmed.startsWith("//") || trimmed.startsWith("/*") || trimmed.startsWith("*")) ++commentLines;
        }
        QVERIFY2(nonEmptyLines > 0, qPrintable(relPath));
        const double ratio = static_cast<double>(commentLines) / static_cast<double>(nonEmptyLines);
        QVERIFY2(ratio >= 0.20,
                 qPrintable(QString("%1 comment ratio %2 (%3/%4) < 0.20")
                                .arg(relPath).arg(ratio, 0, 'f', 3).arg(commentLines).arg(nonEmptyLines)));
    }
}

void TestMainWindowUI::testCoreGoogleStyleAndDocumentation() {
    QDir rootDir(QCoreApplication::applicationDirPath() + "/..");
    if (!rootDir.exists("src")) rootDir = QDir::current();
    QStringList targetFiles = { rootDir.filePath(QStringLiteral("src/main.cpp")) };
    QDirIterator it(rootDir.filePath(QStringLiteral("src/core")), QStringList() << "*.hpp" << "*.cpp",
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) targetFiles.append(it.next());
    targetFiles.sort();
    QVERIFY(targetFiles.size() >= 45);
    auditFilesGoogleStyleAndDocumentation(targetFiles);
}

void TestMainWindowUI::testUiViewsAndWidgetsGoogleStyleAndDocumentation() {
    QDir rootDir(QCoreApplication::applicationDirPath() + "/..");
    if (!rootDir.exists("src")) rootDir = QDir::current();
    QStringList targetFiles;
    for (const QString& subDir : {QStringLiteral("src/ui/views"), QStringLiteral("src/ui/widgets")}) {
        QDirIterator it(rootDir.filePath(subDir), QStringList() << "*.hpp" << "*.cpp",
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) targetFiles.append(it.next());
    }
    targetFiles.sort();
    QVERIFY(targetFiles.size() >= 15);
    auditFilesGoogleStyleAndDocumentation(targetFiles);

    // Interactive launch assertions: normal CLI launch must not be invisible/offscreen
    char appName[] = "gridlock";
    char* normalArgv[] = { appName, nullptr };
    QVERIFY(!gridlock::core::utils::CliParser::shouldUseOffscreenPlatform(1, normalArgv));

    gridlock::ui::MainWindow mainWindow;
    QVERIFY(!mainWindow.testAttribute(Qt::WA_ShowWithoutActivating));
    QVERIFY(!mainWindow.testAttribute(Qt::WA_DontShowOnScreen));
    mainWindow.show();
    QVERIFY(!mainWindow.isMinimized());
    QVERIFY(!mainWindow.isHidden());
}

QTEST_MAIN(TestMainWindowUI)
