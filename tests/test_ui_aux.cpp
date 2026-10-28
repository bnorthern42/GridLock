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
#include <QToolBar>
#include <QtTest/QtTest>

#include "../src/core/managers/ConfigManager.hpp"
#include "../src/core/managers/ThemeManager.hpp"
#include "../src/core/utils/CliParser.cpp"
#include "../src/ui/MainWindow.hpp"
#include "../src/ui/views/DisassemblyView.hpp"
#include "../src/ui/views/MemView.hpp"
#include "../src/ui/views/RegisterView.hpp"
#include "../src/ui/views/ServerRackView.hpp"
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
        // Verify buttons conform to compact ergonomics.
        for (auto* btn : s->findChildren<QPushButton*>()) if (btn->isVisible()) QVERIFY2(btn->height() <= 28, qPrintable(btn->objectName()));
        // Verify line edits conform to compact ergonomics.
        for (auto* edit : s->findChildren<QLineEdit*>()) if (edit->isVisible()) QVERIFY2(edit->height() <= 28, qPrintable(edit->objectName()));
        // Verify combo boxes conform to compact ergonomics.
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
    QVERIFY(initLine > 0);
    QCOMPARE(disassemblyView->decompilerLineToAddress(initLine), 0x0040113dULL);
    QCOMPARE(disassemblyView->addressToDecompilerLine(0x0040113dULL), initLine);

    // Select line in decompiler and verify memory view seeks to corresponding address.
    const int loopLine = disassemblyView->addressToDecompilerLine(0x00401149ULL);
    QVERIFY(loopLine > 0);
    QVERIFY(disassemblyView->selectDecompilerLine(loopLine));
    QCOMPARE(disassemblyView->currentAddress(), 0x00401149ULL);
    QCOMPARE(memView->currentBaseAddress(), 0x00401149ULL);

    // Highlight token in pseudocode.
    disassemblyView->highlightToken("argc");
    QCOMPARE(disassemblyView->highlightedToken(), QStringLiteral("argc"));

    // Verify navigation history traversal.
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401120"));
    QVERIFY(mainWindow.seekToAddressOrExpression("0x0040113d"));
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401178"));
    QVERIFY(mainWindow.canNavigateBack());
    QVERIFY(mainWindow.navigateBack());
    QCOMPARE(disassemblyView->currentAddress(), 0x0040113dULL);
    QCOMPARE(memView->currentBaseAddress(), 0x0040113dULL);
    QVERIFY(mainWindow.navigateForward());
    QCOMPARE(disassemblyView->currentAddress(), 0x00401178ULL);

    // Activate string and verify memory view navigates to string address.
    projectExplorer->setStringsMode(true);
    QVERIFY(projectExplorer->isStringsMode() && projectExplorer->stringCount() >= 4);
    QVERIFY(projectExplorer->activateString("MPI_COMM_WORLD"));
    QCOMPARE(memView->currentBaseAddress(), 0x00402000ULL);

    // Address comment verification.
    disassemblyView->setAddressComment(0x00401149ULL, "Rank loop header");
    QCOMPARE(disassemblyView->addressComment(0x00401149ULL), QStringLiteral("Rank loop header"));
    QVERIFY(disassemblyView->toggleBookmark(0x00401149ULL));
    QVERIFY(disassemblyView->isBookmarked(0x00401149ULL));
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

    // Verify Rank 0 maps to block 0x00401168.
    QCOMPARE(disassemblyView->ranksAtBlock(0x00401168ULL), QVector<int>{0});
    // Verify Rank 1 maps to block 0x0040114f.
    QCOMPARE(disassemblyView->ranksAtBlock(0x0040114fULL), QVector<int>{1});
    // Verify Ranks 2 and 3 map to block 0x00401157.
    QCOMPARE(disassemblyView->ranksAtBlock(0x00401157ULL), (QVector<int>{2, 3}));

    // Verify minimap visibility toggle and geometry constraints.
    QVERIFY(disassemblyView->isGraphMinimapVisible());
    const QRect mmRect = disassemblyView->minimapRect();
    // Minimap rectangle must have positive dimensions.
    QVERIFY(mmRect.width() >= 80 && mmRect.height() >= 60);

    // Toggle minimap visibility off.
    disassemblyView->setGraphMinimapVisible(false);
    QVERIFY(!disassemblyView->isGraphMinimapVisible());

    // Toggle minimap visibility back on.
    disassemblyView->setGraphMinimapVisible(true);
    QVERIFY(disassemblyView->isGraphMinimapVisible());

    // Select rank 1 in ServerRackView and verify status telemetry and address update.
    QVERIFY(serverRackView->selectRank(1));
    QCOMPARE(serverRackView->selectedRank(), 1);
    // Status telemetry banner must indicate active rank.
    QVERIFY(statusTelemetry->text().contains("MPI RANK: 1"));
    // Disassembly view must seek to rank 1's instruction pointer.
    QCOMPARE(disassemblyView->currentAddress(), 0x00401155ULL);
    // Active decompiler line must be non-zero.
    QVERIFY(disassemblyView->currentDecompilerLine() > 0);
}

/**
 * @brief Tests dynamic rank state updates without relying on constructor mocks.
 */
void TestMainWindowUI::testDynamicRankSyncWithoutHardcodedConstructorMocks() {
    // 1. Standalone widgets start clean with zero hardcoded mock ranks
    gridlock::ui::ServerRackView standaloneRack;
    // Rack node count should be zero initially.
    QCOMPARE(standaloneRack.rankNodeCount(), 0);

    gridlock::ui::DisassemblyView standaloneDisasm;
    // Disassembly view should have no rank occupancy badges initially.
    QVERIFY(standaloneDisasm.ranksAtBlock(0x00401155ULL).isEmpty());
    QVERIFY(standaloneDisasm.ranksAtBlock(0x00401168ULL).isEmpty());

    // 2. Dynamic updateRanks(QList<RankState>) populates rank cards and PC addresses
    QList<gridlock::RankState> states;
    for (int r = 0; r < 4; ++r) {
        gridlock::RankState st;
        st.rankId = r;
        st.currentState = "stopped";
        // RIP register at index 16.
        st.registers[16] = (r == 0) ? QStringLiteral("0x00401168") : QStringLiteral("0x00401155");
        states.append(st);
    }
    // Feed telemetry into standalone rack.
    standaloneRack.updateRanks(states);
    QCOMPARE(standaloneRack.rankNodeCount(), 4);
    QCOMPARE(standaloneRack.rankPcAddress(0), 0x00401168ULL);
    QCOMPARE(standaloneRack.rankPcAddress(1), 0x00401155ULL);

    // 3. MainWindow dynamic rank synchronization from ServerRackView -> DisassemblyView
    gridlock::ui::MainWindow mainWindow;
    auto* rack = mainWindow.serverRackView();
    auto* disasm = mainWindow.disassemblyView();
    QVERIFY(rack && disasm);

    // Synchronize ranks into main window rack.
    rack->updateRanks(states);
    QCOMPARE(rack->rankNodeCount(), 4);
    // Disassembly view should reflect updated rank locations.
    QCOMPARE(disasm->ranksAtBlock(0x00401168ULL), QVector<int>{0});
    QCOMPARE(disasm->ranksAtBlock(0x0040114fULL), (QVector<int>{1, 2, 3}));

    // 4. loadPreviewFixture() populates 8 preview ranks via the dynamic pipeline
    mainWindow.loadPreviewFixture();
    QCOMPARE(rack->rankNodeCount(), 8);
    // Rank 0 should reside at entrypoint 0x00401120.
    QCOMPARE(disasm->ranksAtBlock(0x00401120ULL), QVector<int>{0});
    // Worker ranks 2..7 reside at barrier loop 0x00401157.
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
