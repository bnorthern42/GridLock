/**
 * @file test_ui_aux.cpp
 * @brief Companion translation unit for TestMainWindowUI integration tests.
 *
 * Implements test slots for dock resizability, memory view toolbars, display screen
 * routing, cross-pane navigation history, CFG minimap overlays, dynamic rank sync,
 * and repository-wide Google C++ Style Guide and documentation compliance auditing.
 */

#include "test_ui.hpp"

#include <cmath>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFile>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolBar>
#include <QtTest/QtTest>

#include "../src/core/managers/ConfigManager.hpp"
#include "../src/core/managers/ThemeManager.hpp"
#include "../src/core/utils/CliParser.cpp"
#include "../src/core/utils/ElfBinaryInspector.hpp"
#include "../src/ui/MainWindow.hpp"
#include "../src/ui/dialogs/XrefInspectorDialog.hpp"
#include "../src/ui/docks/TerminalDockWidget.hpp"
#include "../src/ui/views/DisassemblyView.hpp"
#include "../src/ui/views/MemView.hpp"
#include "../src/ui/views/RegisterView.hpp"
#include "../src/ui/views/ServerRackView.hpp"
#if __has_include("../src/ui/widgets/HpcConsoleWidget.hpp")
#include "../src/ui/widgets/HpcConsoleWidget.hpp"
#else
#include "../src/core/managers/SpackManager.hpp"
#endif
#include "../src/ui/widgets/ProjectExplorerWidget.hpp"

/**
 * @brief Helper function verifying Google C++ Style and documentation coverage for all files.
 * @param targetFiles Sorted list of relative file paths to inspect.
 */
static void auditFilesGoogleStyleAndDocumentation(const QStringList& targetFiles) {
    for (const QString& relPath : targetFiles) {
        QFile f(relPath);
        // Verify file can be opened.
        QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(relPath));
        const QString content = QString::fromUtf8(f.readAll());
        const QStringList lines = content.split('\n');

        // Check line limit ceiling (< 950 lines).
        QVERIFY2(lines.size() < 950, qPrintable(QString("%1 has %2 lines (>= 950)").arg(relPath).arg(lines.size())));

        // Check required Doxygen file header banner.
        QVERIFY2(content.contains("@file") && content.contains("@brief"),
                 qPrintable(QString("%1 missing @file/@brief header banner").arg(relPath)));

        // Check Google C++ namespace closing comments.
        if (content.contains("\nnamespace ") || content.startsWith("namespace ")) {
            QVERIFY2(content.contains("// namespace "),
                     qPrintable(QString("%1 missing Google C++ closing // namespace comment").arg(relPath)));
        }

        // Compute comment density ratio (comments / non-empty lines >= 0.20).
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

/**
 * @brief Tests side dock resizability limits and xref / decompiler integration.
 */
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
            // Verify dock resize constraints permit flexible user layout.
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

    // Resize docks horizontally and verify dimensions adapt properly.
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

    // Verify cross-references to address 0x00401149.
    const QVector<uint64_t> xrefs = disassemblyView->xrefsToAddress(0x00401149ULL);
    QVERIFY2(xrefs.size() >= 2,
             qPrintable(QString("Expected >= 2 xrefs to 0x00401149, got %1").arg(xrefs.size())));
    QVERIFY(xrefs.contains(0x00401163ULL));
    QVERIFY(xrefs.contains(0x0040116eULL));

    // Verify generated decompiler pseudocode contents.
    const QString pseudocode = disassemblyView->decompiledPseudocode();
    QVERIFY(!pseudocode.isEmpty());
    QVERIFY(pseudocode.contains("int main(") || pseudocode.contains("sym.main"));
    QVERIFY(pseudocode.contains("MPI_Init"));
    QVERIFY(pseudocode.contains("401149"));

    // Verify decompiler mode toggling.
    disassemblyView->setDecompilerMode(true);
    QVERIFY(disassemblyView->isDecompilerMode());
    disassemblyView->setDecompilerMode(false);
    QVERIFY(!disassemblyView->isDecompilerMode());
}

/**
 * @brief Tests memory view toolbar geometry and source file line length limits.
 */
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

    // Ensure buttons are wide enough to display their full text labels.
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

    // Verify address input width allocation.
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

/**
 * @brief Tests preferred screen routing and offscreen startup configuration.
 */
void TestMainWindowUI::testOffscreenDefaultAndPreferredScreenRouting() {
    gridlock::ui::MainWindow mainWindow;
    const auto docks = mainWindow.findChildren<QDockWidget*>();
    QVERIFY(!docks.isEmpty());
    for (QDockWidget* dock : docks) {
        if (!dock) continue;
        if (QWidget* tb = dock->titleBarWidget()) {
            // Verify title bar widget ownership.
            QVERIFY2(tb->parentWidget() == dock,
                     qPrintable(QString("Dock '%1' has titleBarWidget with parent '%2' instead of dock")
                                    .arg(dock->objectName())
                                    .arg(tb->parentWidget() ? tb->parentWidget()->objectName() : "nullptr")));
            QVERIFY2(!tb->isWindow(),
                     qPrintable(QString("Dock '%1' titleBarWidget is a top-level window").arg(dock->objectName())));
        }
    }

    // Verify screen resolution logic.
    QScreen* primary = QGuiApplication::primaryScreen();
    QVERIFY(primary != nullptr);
    QCOMPARE(mainWindow.resolvePreferredScreen(""), primary);
    QCOMPARE(mainWindow.resolvePreferredScreen("nonexistent_monitor_xyz"), primary);

    // Verify monitor routing and focus attributes.
    mainWindow.applyPreferredScreenRouting("27", true);
    QVERIFY(mainWindow.testAttribute(Qt::WA_ShowWithoutActivating));
    QCOMPARE(mainWindow.preferredMonitorHint(), QStringLiteral("27"));

    mainWindow.applyPreferredScreenRouting("2560x1440", false);
    QVERIFY(!mainWindow.testAttribute(Qt::WA_ShowWithoutActivating));
    QCOMPARE(mainWindow.preferredMonitorHint(), QStringLiteral("2560x1440"));

    // Verify configuration manager round-trip persistence.
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

    // Restore previous configuration state.
    if (prevTestDir.isEmpty()) qunsetenv("GRIDLOCK_TEST_CONFIG_DIR");
    else qputenv("GRIDLOCK_TEST_CONFIG_DIR", prevTestDir);
    cfg.loadConfig();
    cfg.setPreferredMonitor(prevMonitor);
    cfg.setOpenUnfocused(prevUnfocused);
}

/**
 * @brief Tests address history back/forward navigation, string lists, and bookmarks.
 */
void TestMainWindowUI::testNavigationHistoryStringsAndBookmarks() {
    gridlock::ui::MainWindow mainWindow;
    auto* disassemblyView = mainWindow.disassemblyView();
    auto* projectExplorer = mainWindow.projectExplorerWidget();
    auto* memView = mainWindow.memView();
    QVERIFY(disassemblyView != nullptr);
    QVERIFY(projectExplorer != nullptr);
    QVERIFY(memView != nullptr);

    // Seek across sequence of virtual addresses.
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401120"));
    QCOMPARE(disassemblyView->currentAddress(), 0x00401120ULL);
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401149"));
    QCOMPARE(disassemblyView->currentAddress(), 0x00401149ULL);
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401178"));
    QCOMPARE(disassemblyView->currentAddress(), 0x00401178ULL);
    QVERIFY(mainWindow.canNavigateBack());
    QVERIFY(!mainWindow.canNavigateForward());

    // Navigate back in history stack.
    QVERIFY(mainWindow.navigateBack());
    QCOMPARE(disassemblyView->currentAddress(), 0x00401149ULL);
    QVERIFY(mainWindow.canNavigateBack());
    QVERIFY(mainWindow.canNavigateForward());

    // Navigate forward in history stack.
    QVERIFY(mainWindow.navigateForward());
    QCOMPARE(disassemblyView->currentAddress(), 0x00401178ULL);
    QVERIFY(!mainWindow.canNavigateForward());

    // Activate string list view in project explorer.
    projectExplorer->setStringsMode(true);
    QVERIFY(projectExplorer->isStringsMode());
    QVERIFY(projectExplorer->stringCount() >= 4);

    // Activate string entry to seek memory view to target address.
    QSignalSpy spyStr(projectExplorer, &gridlock::ui::ProjectExplorerWidget::symbolAddressSelected);
    QVERIFY(projectExplorer->activateString("MPI_COMM_WORLD"));
    QCOMPARE(spyStr.count(), 1);
    const uint64_t strAddr = spyStr.at(0).at(0).toULongLong();
    QCOMPARE(strAddr, 0x00402000ULL);
    QCOMPARE(memView->currentBaseAddress(), 0x00402000ULL);
    QVERIFY(mainWindow.canNavigateBack());

    // Set address comments and toggle bookmarks.
    disassemblyView->setAddressComment(0x00401149ULL, " Outer loop rank check ");
    QCOMPARE(disassemblyView->addressComment(0x00401149ULL), QStringLiteral("Outer loop rank check"));
    QVERIFY(!disassemblyView->isBookmarked(0x00401149ULL));
    QVERIFY(disassemblyView->toggleBookmark(0x00401149ULL));
    QVERIFY(disassemblyView->isBookmarked(0x00401149ULL));
    QVERIFY(disassemblyView->bookmarkedAddresses().contains(0x00401149ULL));
    QVERIFY(!disassemblyView->toggleBookmark(0x00401149ULL));
    QVERIFY(!disassemblyView->isBookmarked(0x00401149ULL));
}

/**
 * @brief Tests multi-language decompiler target generation and source fallback.
 */
void TestMainWindowUI::testCfgGraphDecompilerLanguagesAndSourceFallback() {
    gridlock::ui::MainWindow mainWindow;
    auto* disassemblyView = mainWindow.disassemblyView();
    QVERIFY(disassemblyView != nullptr);

    // Generate C target pseudocode.
    disassemblyView->setDecompilerTargetLanguage("C");
    QCOMPARE(disassemblyView->decompilerTargetLanguage(), QStringLiteral("C"));
    const QString cOut = disassemblyView->decompileToLanguage("C");
    QVERIFY(cOut.contains("int32_t") || cOut.contains("var_"));
    QVERIFY(cOut.contains("if (") || cOut.contains("while ("));
    QVERIFY(cOut.contains("MPI_Init"));

    // Generate C++ and high-level pseudocode variants.
    const QString cppOut = disassemblyView->decompileToLanguage("C++");
    const QString pseudoOut = disassemblyView->decompileToLanguage("Pseudocode");
    QVERIFY(!cppOut.isEmpty() && !pseudoOut.isEmpty());
    QVERIFY(cppOut != cOut && pseudoOut != cOut && cppOut != pseudoOut);

    // Function signature inspection.
    const QString sigHeader = disassemblyView->functionSignatureHeader();
    QVERIFY(!sigHeader.isEmpty() && sigHeader.contains("sym.main(int argc, char **argv)"));
    disassemblyView->setGraphMode(true);
    QVERIFY(disassemblyView->isGraphMode() && disassemblyView->basicBlockCount() >= 3);

    // Verify source code fallback on address miss.
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401120"));
    QVERIFY(!mainWindow.isShowingDecompilerFallback());
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401000"));
    QVERIFY(mainWindow.isShowingDecompilerFallback());
}

/**
 * @brief Tests unified dark slate chrome styling metrics and split view layout.
 */
void TestMainWindowUI::testUnifiedChromeMetricsAndSplitGraphDecompiler() {
    // Initialize ThemeManager to apply uniform dark slate theme.
    gridlock::core::managers::ThemeManager::instance().initialize();
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    auto* disassemblyView = mainWindow.disassemblyView();
    QVERIFY(disassemblyView != nullptr);

    // Enable dual split view displaying CFG graph alongside decompiled pseudocode.
    disassemblyView->setSplitGraphDecompilerMode(true);
    QVERIFY(disassemblyView->isSplitGraphDecompilerMode());
    QCoreApplication::processEvents();

    // Verify both graph and decompiler widgets are visible and have adequate width.
    auto* cfgGraphView = disassemblyView->findChild<QGraphicsView*>("cutterCfgGraphView");
    auto* decompView = disassemblyView->findChild<QPlainTextEdit*>("cutterDecompilerView");
    QVERIFY(cfgGraphView && decompView);
    // Ensure both panels are active and visible in the split container.
    QVERIFY(cfgGraphView->isVisibleTo(disassemblyView) && decompView->isVisibleTo(disassemblyView));
    QVERIFY(cfgGraphView->width() >= 180 && decompView->width() >= 180);
    // Export UI split snapshot for visual comparison.
    mainWindow.captureWorkspaceSnapshot("/tmp/gridlock_ui_v11_split.png");

    // Verify compact heights across common input widgets (buttons, edits, combos <= 28px).
    const QList<QWidget*> scopes = { &mainWindow, mainWindow.memView(), mainWindow.projectExplorerWidget(), disassemblyView };
    for (QWidget* s : scopes) {
        if (!s) continue;
        // Verify buttons, line edits, and combo boxes conform to compact ergonomics.
        for (auto* btn : s->findChildren<QPushButton*>()) if (btn->isVisible()) QVERIFY2(btn->height() <= 28, qPrintable(btn->objectName()));
        for (auto* edit : s->findChildren<QLineEdit*>()) if (edit->isVisible()) QVERIFY2(edit->height() <= 28, qPrintable(edit->objectName()));
        for (auto* combo : s->findChildren<QComboBox*>()) if (combo->isVisible()) QVERIFY2(combo->height() <= 28, qPrintable(combo->objectName()));
    }
    // Verify toolbars conform to compact height limit <= 36px.
    for (auto* tb : mainWindow.findChildren<QToolBar*>()) if (tb->isVisible()) QVERIFY2(tb->height() <= 36, qPrintable(tb->objectName()));
}

/**
 * @brief Tests synchronized selection choreography between decompiler lines and disassembly.
 */
void TestMainWindowUI::testCrossPaneChoreographyHistoryAndStrings() {
    gridlock::ui::MainWindow mainWindow;
    auto* disassemblyView = mainWindow.disassemblyView();
    auto* projectExplorer = mainWindow.projectExplorerWidget();
    auto* memView = mainWindow.memView();
    QVERIFY(disassemblyView && projectExplorer && memView);
    disassemblyView->setSplitGraphDecompilerMode(true);
    QVERIFY(disassemblyView->seekToAddress(0x0040113dULL));
    const int initLine = disassemblyView->currentDecompilerLine();
    QVERIFY(initLine > 0 && disassemblyView->decompilerLineToAddress(initLine) == 0x0040113dULL);
    QCOMPARE(disassemblyView->addressToDecompilerLine(0x0040113dULL), initLine);

    // Select line in decompiler and verify memory view seeks to corresponding address.
    const int loopLine = disassemblyView->addressToDecompilerLine(0x00401149ULL);
    QVERIFY(loopLine > 0 && disassemblyView->selectDecompilerLine(loopLine));
    QCOMPARE(disassemblyView->currentAddress(), 0x00401149ULL);
    QCOMPARE(memView->currentBaseAddress(), 0x00401149ULL);
    // Highlight token in pseudocode and verify navigation history traversal.
    disassemblyView->highlightToken("argc");
    QCOMPARE(disassemblyView->highlightedToken(), QStringLiteral("argc"));
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401120") && mainWindow.seekToAddressOrExpression("0x0040113d") && mainWindow.seekToAddressOrExpression("0x00401178"));
    QVERIFY(mainWindow.canNavigateBack() && mainWindow.navigateBack());
    QCOMPARE(disassemblyView->currentAddress(), 0x0040113dULL);
    QCOMPARE(memView->currentBaseAddress(), 0x0040113dULL);
    QVERIFY(mainWindow.navigateForward() && disassemblyView->currentAddress() == 0x00401178ULL);

    // Activate string and verify memory view navigates to string address and address comment.
    projectExplorer->setStringsMode(true);
    QVERIFY(projectExplorer->isStringsMode() && projectExplorer->stringCount() >= 4 && projectExplorer->activateString("MPI_COMM_WORLD"));
    QCOMPARE(memView->currentBaseAddress(), 0x00402000ULL);
    disassemblyView->setAddressComment(0x00401149ULL, "Rank loop header");
    QCOMPARE(disassemblyView->addressComment(0x00401149ULL), QStringLiteral("Rank loop header"));
    QVERIFY(disassemblyView->toggleBookmark(0x00401149ULL) && disassemblyView->isBookmarked(0x00401149ULL));
}

/**
 * @brief Tests CFG rank divergence badges, minimap overlay, and rack rank selection.
 */
void TestMainWindowUI::testCfgRankDivergenceMinimapAndRackSync() {
    // Instantiate primary MainWindow for rank telemetry sync tests.
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    auto* disassemblyView = mainWindow.disassemblyView();
    auto* serverRackView = mainWindow.serverRackView();
    auto* statusTelemetry = mainWindow.findChild<QLabel*>("cutterStatusTelemetry");
    QVERIFY(disassemblyView && serverRackView && statusTelemetry);

    // Set split graph decompiler and assign rank instruction pointers.
    disassemblyView->setSplitGraphDecompilerMode(true);
    disassemblyView->setRankAddresses({{0, 0x00401168ULL}, {1, 0x00401155ULL}, {2, 0x00401157ULL}, {3, 0x0040115fULL}});
    // Verify Rank 0 maps to block 0x00401168, Rank 1 to 0x0040114f, Ranks 2 and 3 to 0x00401157.
    QCOMPARE(disassemblyView->ranksAtBlock(0x00401168ULL), QVector<int>{0});
    QCOMPARE(disassemblyView->ranksAtBlock(0x0040114fULL), QVector<int>{1});
    QCOMPARE(disassemblyView->ranksAtBlock(0x00401157ULL), (QVector<int>{2, 3}));

    // Verify minimap visibility toggle and geometry constraints.
    QVERIFY(disassemblyView->isGraphMinimapVisible() && disassemblyView->minimapRect().width() >= 80 && disassemblyView->minimapRect().height() >= 60);
    disassemblyView->setGraphMinimapVisible(false);
    QVERIFY(!disassemblyView->isGraphMinimapVisible());
    disassemblyView->setGraphMinimapVisible(true);
    QVERIFY(disassemblyView->isGraphMinimapVisible());

    // Select rank 1 in ServerRackView and verify status telemetry and address update.
    QVERIFY(serverRackView->selectRank(1) && serverRackView->selectedRank() == 1);
    QVERIFY(statusTelemetry->text().contains("MPI RANK: 1") && disassemblyView->currentAddress() == 0x00401155ULL && disassemblyView->currentDecompilerLine() > 0);
}

/**
 * @brief Tests dynamic rank state updates without relying on constructor mocks.
 */
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

/**
 * @brief Audits entire repository for Google C++ Style Guide and documentation completeness.
 */
void TestMainWindowUI::testFullRepoGoogleStyleAndDocumentation() {
    QDir rootDir(QCoreApplication::applicationDirPath() + "/..");
    if (!rootDir.exists("src")) rootDir = QDir::current();
    QStringList targetFiles;
    for (const QString& subDir : {QStringLiteral("src"), QStringLiteral("tests")}) {
        QDirIterator it(rootDir.filePath(subDir), QStringList() << "*.hpp" << "*.cpp",
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) targetFiles.append(it.next());
    }
    targetFiles.sort();
    QVERIFY(targetFiles.size() >= 80);
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

/**
 * @brief Tests ELF64 binary parsing, section and symbol extraction, and static target loading.
 */
void TestMainWindowUI::testElfBinaryInspectorAndStaticTargetLoad() {
    // 1. Locate current test executable to serve as a valid 64-bit ELF binary target.
    const QString appPath = QCoreApplication::applicationFilePath();
    QVERIFY(!appPath.isEmpty() && QFile::exists(appPath));

    // 2. Parse binary using ElfBinaryInspector.
    gridlock::core::ElfBinaryInspector inspector;
    QVERIFY(inspector.loadFile(appPath) && inspector.isValid() && inspector.entryPoint() != 0ULL);

    // 3. Inspect ELF section headers (.text, .rodata, .data, etc.).
    const auto sections = inspector.sections();
    QVERIFY(!sections.isEmpty());
    bool hasTextSection = false;
    uint64_t textVaddr = 0;
    for (const auto& sec : sections) {
        if (sec.name == QStringLiteral(".text")) { hasTextSection = true; textVaddr = sec.address; break; }
    }
    QVERIFY(hasTextSection && textVaddr != 0ULL);

    // 4. Inspect extracted symbol table entries and printable ASCII strings.
    QVERIFY(inspector.symbols().size() > 20);
    QVERIFY(inspector.strings().size() > 10);

    // 6. Test virtual memory byte reading for the .text section.
    const QByteArray textBytes = inspector.readVirtualMemory(textVaddr, 32);
    QCOMPARE(textBytes.size(), 32);
    // Code section bytes must not be completely all zeroes.
    QVERIFY(textBytes != QByteArray(32, '\0'));

    // 7. Verify disassembleRange produces formatted assembly lines.
    QVERIFY(!inspector.disassembleRange(textVaddr, 32, QStringLiteral("entry")).isEmpty());

    // 8. Test MainWindow static target loading and widget population.
    gridlock::ui::MainWindow mainWindow;
    QVERIFY(mainWindow.loadBinaryTarget(appPath));

    // 9. Verify ProjectExplorerWidget contains populated symbols and strings.
    auto* explorer = mainWindow.findChild<gridlock::ui::ProjectExplorerWidget*>();
    QVERIFY(explorer != nullptr && explorer->symbolCount() > 20 && explorer->stringCount() > 10);

    // 10. Verify address seek synchronizes MemView and DisassemblyView.
    QVERIFY(mainWindow.seekToAddressOrExpression(QString("0x%1").arg(textVaddr, 0, 16)));
    auto* memView = mainWindow.findChild<gridlock::ui::MemView*>();
    QVERIFY(memView != nullptr);
    QCOMPARE(memView->currentBaseAddress(), textVaddr);
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(disasmView != nullptr && !disasmView->toPlainText().isEmpty());
}

/**
 * @brief Tests interactive Xref extraction, incoming/outgoing call graph analysis, and inspector dialog sync.
 */
void TestMainWindowUI::testInteractiveXrefInspectorAndCallGraphSync() {
    gridlock::ui::MainWindow mainWindow;
    // 1. Load preview fixture providing canonical multi-rank MPI loop disassembly
    mainWindow.loadPreviewFixture();
    auto* disasm = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(disasm != nullptr);

    // 2. Verify structured incoming cross-references targeting the barrier loop header 0x00401149
    const auto incoming = disasm->incomingXrefs(0x00401149ULL);
    QVERIFY(incoming.size() >= 2);
    // Ensure back-edge jumps from loop bottom (0x401163, 0x40116e) are detected
    bool foundBackEdge = false;
    for (const auto& xr : incoming) {
        QCOMPARE(xr.toAddress, 0x00401149ULL);
        if (xr.fromAddress == 0x00401163ULL || xr.fromAddress == 0x0040116eULL) foundBackEdge = true;
        QVERIFY(xr.type == QStringLiteral("JMP") || xr.type == QStringLiteral("CJMP"));
    }
    QVERIFY(foundBackEdge);

    // 3. Verify structured outgoing cross-references originating from function 0x00401120
    const auto outgoing = disasm->outgoingXrefs(0x00401120ULL);
    QVERIFY(outgoing.size() >= 2);
    bool foundMpiInitCall = false, foundMpiFinalizeCall = false;
    for (const auto& xr : outgoing) {
        if (xr.type == QStringLiteral("CALL")) {
            if (xr.toAddress == 0x00401050ULL || xr.instructionText.contains(QStringLiteral("MPI_Init"))) foundMpiInitCall = true;
            if (xr.toAddress == 0x00401060ULL || xr.instructionText.contains(QStringLiteral("MPI_Finalize"))) foundMpiFinalizeCall = true;
        }
    }
    QVERIFY(foundMpiInitCall && foundMpiFinalizeCall);

    // 4. Open XrefInspectorDialog at address 0x00401149 and test filtering + incoming row activation
    auto* dialog = disasm->openXrefInspector(0x00401149ULL);
    QVERIFY(dialog != nullptr && dialog->incomingRowCount() >= 2 && dialog->outgoingRowCount() >= 2 && !dialog->previewText().isEmpty());
    dialog->setFilterText(QStringLiteral("MPI_Init"));
    QVERIFY(dialog->outgoingRowCount() >= 1);
    dialog->setFilterText(QString());
    QVERIFY(dialog->outgoingRowCount() >= 2);
    auto* memView = mainWindow.findChild<gridlock::ui::MemView*>();
    QVERIFY(memView != nullptr && dialog->activateIncomingXref(0));
    // Verify DisassemblyView and MemView are synchronized to the caller address
    QCOMPARE(disasm->currentAddress(), incoming.first().fromAddress);
    QCOMPARE(memView->currentBaseAddress(), incoming.first().fromAddress);
}

/**
 * @brief Tests terminal monospace font enforcement, memory/disassembly live patching, and deadlock cycle badges.
 */
void TestMainWindowUI::testTerminalFontIsolationLivePatchingAndDeadlockCycle() {
    // 1. Initialize MainWindow and load standard preview fixture.
    gridlock::ui::MainWindow mainWindow;
    mainWindow.loadPreviewFixture();

    // 2. Verify TerminalDockWidget monospace font enforcement and geometry isolation.
    auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>();
    QVERIFY(terminalDock != nullptr);
    // Request a variable-pitch proportional font family and enforce fixed-pitch monospace resolution.
    terminalDock->setCustomFont("Cantarell", 11);
    QVERIFY(QFontInfo(terminalDock->activeTerminalFont()).fixedPitch() && terminalDock->width() >= 0);

    // 3. Verify MemView live byte patching, patch tracking count, signal emission, and revert.
    auto* memView = mainWindow.findChild<gridlock::ui::MemView*>();
    QVERIFY(memView != nullptr);
    QSignalSpy patchSpy(memView, &gridlock::ui::MemView::bytesPatched);
    // Apply live 2-byte patch at base address and verify counter + signal.
    QVERIFY(memView->patchBytes(0x00401120ULL, QByteArray::fromHex("9090")));
    QCOMPARE(memView->patchedByteCount(), 2);
    QCOMPARE(patchSpy.count(), 1);
    // Revert all applied memory patches and verify counter resets to 0.
    memView->revertAllPatches();
    QCOMPARE(memView->patchedByteCount(), 0);

    // 4. Verify DisassemblyView live instruction patching, CFG arc updates, and decompilation.
    auto* disasm = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(disasm != nullptr);
    const int initialArcs = disasm->branchArcCount();
    // 0x00401155 is a conditional jump instruction; patching it to 'nop' eliminates the branch arc.
    QVERIFY(disasm->patchInstruction(0x00401155ULL, QStringLiteral("nop")));
    QCOMPARE(disasm->patchedInstructionCount(), 1);
    QCOMPARE(disasm->branchArcCount(), initialArcs - 1);
    QVERIFY(!disasm->decompileToLanguage(QStringLiteral("C")).isEmpty());

    // 5. Verify MPI Deadlock cycle rank assignment, retrieval, and badge integration.
    const QVector<int> cycleRanks = {0, 1};
    disasm->setDeadlockCycleRanks(cycleRanks);
    QCOMPARE(disasm->deadlockCycleRanks(), cycleRanks);

    // 6. Verify ElfBinaryInspector virtual memory writing and round-trip verification.
    gridlock::core::ElfBinaryInspector inspector(QCoreApplication::applicationFilePath());
    if (inspector.isValid() && inspector.entryPoint() != 0) {
        const uint64_t entry = inspector.entryPoint();
        QVERIFY(!inspector.readVirtualMemory(entry, 2).isEmpty());
        const QByteArray patch = QByteArray::fromHex("9090");
        // Write virtual memory patch into ELF image in memory and verify upon readback.
        QVERIFY(inspector.writeVirtualMemory(entry, patch) && inspector.readVirtualMemory(entry, 2) == patch);
    }
}

/**
 * @brief Tests patched ELF export to disk, unified multi-domain workspace search, and deadlock propagation.
 */
void TestMainWindowUI::testPatchedElfExportWorkspaceSearchAndDeadlockPropagation() {
    // 1. Initialize MainWindow and load running binary target
    gridlock::ui::MainWindow mainWindow;
    const QString appPath = QCoreApplication::applicationFilePath();
    QVERIFY(mainWindow.loadBinaryTarget(appPath));
    mainWindow.loadPreviewFixture();
    auto* inspector = mainWindow.elfInspector();
    QVERIFY(inspector != nullptr && inspector->isValid());

    // Locate .text section address
    uint64_t textAddr = 0;
    for (const auto& sec : inspector->sections()) {
        if (sec.name == QStringLiteral(".text")) { textAddr = sec.address; break; }
    }
    if (textAddr == 0) textAddr = inspector->entryPoint();
    QVERIFY(textAddr != 0);

    // Apply live 2-byte patch via MemView and export patched binary to temporary file on disk
    auto* memView = mainWindow.findChild<gridlock::ui::MemView*>();
    QVERIFY(memView != nullptr);
    const QByteArray patchBytes = QByteArray::fromHex("9090");
    QVERIFY(memView->patchBytes(textAddr, patchBytes));
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString exportPath = tempDir.filePath(QStringLiteral("patched_gridlock_bin"));
    QVERIFY(mainWindow.exportPatchedBinary(exportPath) && QFile::exists(exportPath));

    // Verify written file permissions and payload via independent inspector
    gridlock::core::ElfBinaryInspector verifyInspector(exportPath);
    QVERIFY(verifyInspector.isValid() && verifyInspector.readVirtualMemory(textAddr, 2) == patchBytes);

    // 2. Test unified multi-domain workspace search
    const auto searchMatches = mainWindow.searchWorkspace(QStringLiteral("main"));
    QVERIFY(!searchMatches.isEmpty());
    bool foundValidAddress = false;
    for (const auto& match : searchMatches) {
        if (match.address != 0 && (match.label.contains(QStringLiteral("main"), Qt::CaseInsensitive) ||
                                   match.contextSnippet.contains(QStringLiteral("main"), Qt::CaseInsensitive))) {
            foundValidAddress = true; break;
        }
    }
    QVERIFY(foundValidAddress);

    // 3. Test deadlock wait-for cycle detection and propagation to DisassemblyView and status bar
    const QVector<int> detectedCycle = mainWindow.detectAndHighlightDeadlock({{0, 1}, {1, 0}});
    QVERIFY(detectedCycle.contains(0) && detectedCycle.contains(1));
    auto* disasm = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(disasm != nullptr && disasm->deadlockCycleRanks().contains(0) && disasm->deadlockCycleRanks().contains(1));
    auto* telemetryLabel = mainWindow.findChild<QLabel*>(QStringLiteral("cutterStatusTelemetry"));
    QVERIFY(telemetryLabel != nullptr && telemetryLabel->text().contains(QStringLiteral("DEADLOCK")));
}

/**
 * @brief Verifies system package manager fallback detection, multi-distro CLI output parsing
 *        (`pacman`, `dnf4`/`dnf5`, `apt`, `zypper`), switchable `Table` / `List` / `Raw` display modes,
 *        and live host package querying with synchronized visible row count badges.
 */
void TestMainWindowUI::testMultiDistroPackageManagerParsingAndViewModes() {
    // 1. System Package Manager Fallback Detection When Spack Is Missing.
    gridlock::ui::HpcConsoleWidget widget;
    QVERIFY(!widget.activePackageManagerBackend().isEmpty());

    // 2. Multi-Distro CLI Output Parsing (pacman, dnf4, dnf5, apt, zypper).
    const QString pacmanOut = QStringLiteral("extra/openmpi 5.0.5-1 [installed]\n    High performance message passing library (MPI)\nextra/hypre 2.31.0-1\n    Parallel multigrid solvers\n");
    const auto pacmanRecs = widget.parsePackageManagerOutput(QStringLiteral("pacman"), pacmanOut);
    QVERIFY(pacmanRecs.size() == 2 && pacmanRecs[0].name == QStringLiteral("openmpi") && pacmanRecs[0].version == QStringLiteral("5.0.5-1"));
    QVERIFY(pacmanRecs[0].repository == QStringLiteral("extra") && pacmanRecs[0].status == QStringLiteral("Installed") && pacmanRecs[0].description.contains(QStringLiteral("message passing")));
    QVERIFY(pacmanRecs[1].name == QStringLiteral("hypre") && pacmanRecs[1].status == QStringLiteral("Available"));

    // Verify Fedora/RHEL dnf4 ("pkg.arch : summary") and Fedora 40+ dnf5 (" pkg.arch\tsummary") formats.
    const auto dnfRecs = widget.parsePackageManagerOutput(QStringLiteral("dnf"), QStringLiteral("Last metadata expiration check...\n=== Name & Summary Matched: openmpi ===\nopenmpi.x86_64 : Open Message Passing Interface\nopenmpi-devel.x86_64 : Development headers for OpenMPI\n"));
    QVERIFY(dnfRecs.size() == 2 && dnfRecs[0].name == QStringLiteral("openmpi") && dnfRecs[0].arch == QStringLiteral("x86_64") && dnfRecs[1].name == QStringLiteral("openmpi-devel"));
    const auto dnf5Recs = widget.parsePackageManagerOutput(QStringLiteral("dnf"), QStringLiteral("Updating and loading repositories:\nRepositories loaded.\nMatched fields: name, summary\n fd-find.x86_64\tFast and user-friendly alternative to find\n fdupes.x86_64  Finds duplicate files in a given set of directories\n"));
    QVERIFY(dnf5Recs.size() == 2 && dnf5Recs[0].name == QStringLiteral("fd-find") && dnf5Recs[0].arch == QStringLiteral("x86_64") && dnf5Recs[0].description.contains(QStringLiteral("alternative to find")) && dnf5Recs[1].name == QStringLiteral("fdupes"));

    // Verify Debian/Ubuntu apt-cache search and openSUSE zypper se -s pipe-delimited table parsing.
    const auto aptRecs = widget.parsePackageManagerOutput(QStringLiteral("apt"), QStringLiteral("openmpi-bin - high performance message passing library -- binaries\nlibopenmpi-dev - high performance message passing library -- header files\n"));
    QVERIFY(aptRecs.size() == 2 && aptRecs[0].name == QStringLiteral("openmpi-bin") && aptRecs[1].name == QStringLiteral("libopenmpi-dev"));
    const auto zypperRecs = widget.parsePackageManagerOutput(QStringLiteral("zypper"), QStringLiteral("S  | Name        | Type    | Version   | Arch   | Repository\n---+-------------+---------+-----------+--------+----------------\ni+ | openmpi4    | package | 4.1.6-1.1 | x86_64 | Main Repository\n   | openmpi4-devel | package | 4.1.6-1.1 | x86_64 | Main Repository\n"));
    QVERIFY(zypperRecs.size() == 2 && zypperRecs[0].name == QStringLiteral("openmpi4") && zypperRecs[0].status == QStringLiteral("Installed") && zypperRecs[0].version == QStringLiteral("4.1.6-1.1") && zypperRecs[1].status == QStringLiteral("Available"));

    // 3. Three Switchable Views (Table, List, Raw) & Non-Empty Table Population.
    widget.displayPackageRecords(pacmanRecs, pacmanOut);
    QCOMPARE(widget.packageTableRowCount(), 2);
    for (const QString& mode : {QStringLiteral("Table"), QStringLiteral("List"), QStringLiteral("Raw")}) {
        widget.setViewMode(mode);
        QCOMPARE(widget.currentViewMode(), mode);
    }
    auto* listBrowser = widget.findChild<QTextBrowser*>();
    QVERIFY(listBrowser != nullptr && listBrowser->toPlainText().contains(QStringLiteral("openmpi")));

    // 4. Verify packageStatusFilterCombo generous width (>= 145px) and live host package query for "fd".
    auto* statusCombo = widget.findChild<QComboBox*>(QStringLiteral("packageStatusFilterCombo"));
    QVERIFY(statusCombo != nullptr && statusCombo->minimumWidth() >= 145 && statusCombo->count() == 3);
    QVERIFY(!statusCombo->itemIcon(0).isNull() && !statusCombo->itemIcon(1).isNull() && !statusCombo->itemIcon(2).isNull());
    // Execute live host query for "fd" and verify visiblePackageCount() matches actual visible table rows (> 0).
    const int fdMatches = widget.searchPackagesSynchronously(QStringLiteral("fd"), 400);
    QVERIFY2(fdMatches > 0, "Live host package query for 'fd' must return > 0 real system/repo packages");
    QCOMPARE(widget.visiblePackageCount(), fdMatches);
}

/**
 * @brief Generates and verifies all 1600x960 workstation, feature, and theme PNG screenshots in
 *        `docs/screenshots/` and confirms `docs/USER_GUIDE.md` embeds the new visual assets.
 */
void TestMainWindowUI::testGenerateAndVerifyUpdatedDocumentationScreenshots() {
    // Instantiate MainWindow and resolve the repository docs/screenshots output directory.
    gridlock::ui::MainWindow mainWindow;
    const QString shotDir = (!QDir(QStringLiteral("docs")).exists() && QDir(QStringLiteral("../docs")).exists())
                                ? QStringLiteral("../docs/screenshots") : QStringLiteral("docs/screenshots");
    // 1. Capture all workstation, feature, and theme screenshots via MainWindow::captureDocumentationScreenshots.
    QVERIFY2(mainWindow.captureDocumentationScreenshots(shotDir), "captureDocumentationScreenshots must succeed");

    // 2. Verify every required PNG file exists, exceeds 15 KB, and loads as a valid >= 800x500 QImage.
    for (const QString& fn : {QStringLiteral("workstation_overview.png"), QStringLiteral("workspace_overview.png"), QStringLiteral("cfg_decompiler_split.png"), QStringLiteral("mpi_diagnostics_docsets.png"), QStringLiteral("breakpoint_popover.png"), QStringLiteral("tool_rail_debug_tabs.png"), QStringLiteral("tool_rail_mpi_diagnostics.png"), QStringLiteral("package_manager_mpi_search.png"), QStringLiteral("package_manager_list_view.png"), QStringLiteral("preferences_slurm_autodetect.png"), QStringLiteral("theme_gridlock_slate.png"), QStringLiteral("theme_obsidian_teal.png"), QStringLiteral("theme_tokyo_night.png"), QStringLiteral("theme_catppuccin_mocha.png"), QStringLiteral("theme_gruvbox_dark.png"), QStringLiteral("theme_daylight_light.png")}) {
        const QString fullPath = shotDir + QLatin1Char('/') + fn;
        const QImage img(fullPath);
        QVERIFY2(QFileInfo(fullPath).size() > 15000 && !img.isNull() && img.width() >= 800 && img.height() >= 500, qPrintable(fullPath));
    }

    // 3. Verify docs/USER_GUIDE.md references breakpoint_popover.png and package_manager_mpi_search.png.
    QFile guideFile(QFileInfo::exists(QStringLiteral("docs/USER_GUIDE.md")) ? QStringLiteral("docs/USER_GUIDE.md") : QStringLiteral("../docs/USER_GUIDE.md"));
    QVERIFY(guideFile.open(QIODevice::ReadOnly));
    const QString guideText = QString::fromUtf8(guideFile.readAll());
    QVERIFY2(guideText.contains(QStringLiteral("breakpoint_popover.png")) && guideText.contains(QStringLiteral("package_manager_mpi_search.png")), "docs/USER_GUIDE.md must embed new assets");
}

/**
 * @brief Verifies v0.7.0 version alignment, `scripts/build_appimage.sh` external `libqtadvancedcss.so`
 *        bundling and `$ORIGIN/../lib` RPATH stamping, and direct `./AppDir/usr/bin/gridlock --version` execution.
 */
void TestMainWindowUI::testReleaseVersion070AndAppImageWorkflowDependencies() {
    const QString rootPrefix = QFileInfo::exists(QStringLiteral("meson.build")) ? QString() : QStringLiteral("../");
    auto readText = [&](const QString& relPath) -> QString {
        QFile f(rootPrefix + relPath);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };

    // 1. Verify meson.build declares version 0.7.0 + $ORIGIN/../lib RPATH and CHANGELOG.md documents [0.7.0].
    const QString mesonText = readText(QStringLiteral("meson.build"));
    QVERIFY2(mesonText.contains(QRegularExpression(QStringLiteral("version\\s*:\\s*'0\\.7\\.0'"))) &&
                 mesonText.contains(QStringLiteral("$ORIGIN/../lib")),
             "meson.build must declare version : '0.7.0' and install_rpath : '$ORIGIN/../lib'");
    QVERIFY2(readText(QStringLiteral("CHANGELOG.md")).contains(QStringLiteral("[0.7.0]")), "CHANGELOG.md must contain [0.7.0]");

    // 2. Verify scripts/build_appimage.sh bundles libqtadvancedcss.so, sets $ORIGIN/../lib RPATH, and populates AppDir.
    const QFileInfo scriptInfo(rootPrefix + QStringLiteral("scripts/build_appimage.sh"));
    QVERIFY2(scriptInfo.exists() && scriptInfo.isExecutable(), "scripts/build_appimage.sh must exist and be executable");
    const QString appImageScript = readText(QStringLiteral("scripts/build_appimage.sh"));
    for (const QString& req : {QStringLiteral("0.7.0"), QStringLiteral("sqldrivers"), QStringLiteral("qtermwidget6"), QStringLiteral("inspection_demo.cpp"), QStringLiteral("deadlock_ring.cpp"), QStringLiteral("libqtadvancedcss.so"), QStringLiteral("$ORIGIN/../lib"), QStringLiteral("patchelf")}) {
        QVERIFY2(appImageScript.contains(req), qPrintable(QStringLiteral("build_appimage.sh missing: ") + req));
    }
    QVERIFY2(QFileInfo::exists(rootPrefix + QStringLiteral("AppDir/usr/lib/libqtadvancedcss.so.1")) && QFileInfo::exists(rootPrefix + QStringLiteral("AppDir/AppRun")), "AppDir/usr/lib/libqtadvancedcss.so.1 and AppDir/AppRun must exist");

    // 3. Verify direct execution of ./AppDir/usr/bin/gridlock --version with empty LD_LIBRARY_PATH succeeds cleanly.
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("LD_LIBRARY_PATH"));
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    proc.setProcessEnvironment(env);
    proc.start(rootPrefix + QStringLiteral("AppDir/usr/bin/gridlock"), {QStringLiteral("--version")});
    QVERIFY2(proc.waitForFinished(5000) && proc.exitCode() == 0, "Direct ./AppDir/usr/bin/gridlock --version must exit 0");
    const QString errOut = QString::fromUtf8(proc.readAllStandardError());
    QVERIFY2(!errOut.contains(QStringLiteral("error while loading shared libraries")), qPrintable(errOut));

    // 4. Verify GitHub Actions workflows install full v0.7.0 dependencies and enforce AppImage-only packaging.
    QDir wfDir(rootPrefix + QStringLiteral(".github/workflows"));
    const QStringList ymlFiles = wfDir.entryList({QStringLiteral("*.yml"), QStringLiteral("*.yaml")}, QDir::Files);
    QVERIFY2(!ymlFiles.isEmpty(), ".github/workflows must contain at least one workflow YAML file");
    QString combinedYml;
    for (const QString& fn : ymlFiles) {
        const QString content = readText(QStringLiteral(".github/workflows/") + fn);
        combinedYml += content + QLatin1Char('\n');
        QVERIFY2(!content.contains(QStringLiteral("dpkg-deb")) && !content.contains(QStringLiteral("rpmbuild")) && !content.contains(QStringLiteral("flatpak-builder")) && !content.contains(QStringLiteral("snapcraft")), qPrintable(fn));
    }
    for (const QString& pkg : {QStringLiteral("clangd"), QStringLiteral("libqtermwidget6-dev"), QStringLiteral("libsqlite3-dev"), QStringLiteral("libqt6sql6-sqlite"), QStringLiteral("openmpi-bin"), QStringLiteral("libopenmpi-dev"), QStringLiteral("gdb"), QStringLiteral("binutils")}) {
        QVERIFY2(combinedYml.contains(pkg), qPrintable(QStringLiteral("CI workflow missing package: ") + pkg));
    }
}
