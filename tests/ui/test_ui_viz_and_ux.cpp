/** @file test_ui_viz_and_ux.cpp @brief Scientific visualization, editor UX, LSP, Command Palette, Calculator, and keybind UI tests. */
#include "test_ui.hpp"
#include <QDir>
#include <QtWidgets>
#include <QtTest/QtTest>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>
#include "../../src/core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../../src/core/hpc/IBackendCoordinator.hpp"
#include "../../src/core/managers/ConfigManager.hpp"
#include "../../src/core/managers/SessionManager.hpp"
#include "../../src/core/managers/ShortcutManager.hpp"
#include "../../src/core/managers/ThemeManager.hpp"
#include "../../src/core/utils/ClangdLspClient.hpp"
#include "../../src/ui/EditorTabManager.hpp"
#include "../../src/ui/GdbConsoleWidget.hpp"
#include "../../src/ui/MainWindow.hpp"
#include "../../src/ui/dialogs/plotter/ArrayPlotDialog.hpp"
#include "../../src/ui/dialogs/project/ProjectSettingsDialog.hpp"
#include "../../src/ui/dialogs/debugger/AttachProcessDialog.hpp"
#include "../../src/ui/dialogs/search/CommandPaletteDialog.hpp"
#include "../../src/ui/dialogs/debugger/ConditionalBreakpointDialog.hpp"
#include "../../src/ui/dialogs/graph/PointerGraphDialog.hpp"
#include "../../src/ui/dialogs/preferences/PreferencesDialog.hpp"
#include "../../src/ui/dialogs/calculator/ProgrammerCalculatorDialog.hpp"
#include "../../src/ui/docks/TerminalDockWidget.hpp"
#include "../../src/ui/docks/VariablesDockWidget.hpp"
#include "../../src/ui/views/CsvTablePreviewWidget.hpp"
#include "../../src/ui/views/DisassemblyView.hpp"
#include "../../src/ui/views/MemView.hpp"
#include "../../src/ui/views/RegisterView.hpp"
#include "../../src/ui/views/SourceCodeView.hpp"
#include "../../src/ui/widgets/HpcConsoleWidget.hpp"
#include "../../src/ui/widgets/ProjectExplorerWidget.hpp"
#include "../../src/ui/widgets/ReferenceManualWidget.hpp"

void TestMainWindowUI::testSeekInMemoryViewAndMnemonicDocLookup() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture(); mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>(); auto* memView = mainWindow.findChild<gridlock::ui::MemView*>(); auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs")); auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY(disasmView && memView && bottomTabs && refManual); bottomTabs->setCurrentIndex(0); QApplication::processEvents(); QVERIFY(bottomTabs->currentWidget() != memView);
    QVERIFY(disasmView->triggerContextAction(0x00401149ULL, QStringLiteral("Seek in Memory View"))); QApplication::processEvents(); QVERIFY(bottomTabs->currentWidget() == memView && memView->currentBaseAddress() == 0x00401149ULL);
    auto* dumpEdit = memView->findChild<QTextEdit*>(QStringLiteral("cutterHexDumpEdit")); QVERIFY(dumpEdit && dumpEdit->toPlainText().contains(QStringLiteral("83 7d fc 02"), Qt::CaseInsensitive));
    mainWindow.seekToAddressOrExpression(QStringLiteral("0x00402010")); QApplication::processEvents(); QCOMPARE(memView->currentBaseAddress(), 0x00402010ULL);
    QVERIFY(disasmView->triggerContextAction(0x00401125ULL, QStringLiteral("Lookup in Reference Manual (F1)"))); QApplication::processEvents(); QCOMPARE(bottomTabs->currentWidget(), refManual);
    auto* docBrowser = refManual->findChild<QTextBrowser*>(QStringLiteral("docsetTextBrowser")); QVERIFY(docBrowser != nullptr); const QString docContent = docBrowser->toHtml();
    QVERIFY2(docContent.contains(QStringLiteral("MOV"), Qt::CaseInsensitive) && !docContent.contains(QStringLiteral("MPI_Init")) && !docContent.contains(QStringLiteral("MPI-3.1 Standard")), "Must display MOV instruction documentation");
}
void TestMainWindowUI::testSmartBuildSystemDetectionAndTerminalBuildCleanWorkflow() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture(); mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    const auto writeText = [](const QString& path, const QByteArray& bytes) { QFile f(path); if (f.open(QIODevice::WriteOnly)) { f.write(bytes); f.close(); } };
    { QTemporaryDir d; QVERIFY(d.isValid()); writeText(d.filePath(QStringLiteral("meson.build")), "project('demo', 'cpp')\n"); QDir(d.path()).mkpath(QStringLiteral("builddir")); writeText(d.filePath(QStringLiteral("builddir/build.ninja")), "# ninja\n"); QVERIFY(mainWindow.detectBuildSystem(d.path()) == QStringLiteral("Meson") && mainWindow.buildCommandForPath(d.path()).contains(QStringLiteral("ninja -C")) && mainWindow.cleanCommandForPath(d.path()).contains(QStringLiteral("clean"))); }
    { QTemporaryDir d; QVERIFY(d.isValid()); writeText(d.filePath(QStringLiteral("CMakeLists.txt")), "cmake_minimum_required(VERSION 3.20)\nproject(demo)\n"); QVERIFY(mainWindow.detectBuildSystem(d.path()) == QStringLiteral("CMake") && mainWindow.buildCommandForPath(d.path()).contains(QStringLiteral("cmake --build")) && mainWindow.cleanCommandForPath(d.path()).contains(QStringLiteral("clean"))); }
    { QTemporaryDir d; QVERIFY(d.isValid()); writeText(d.filePath(QStringLiteral("Makefile")), "all:\n\t@echo building\nclean:\n\t@echo cleaning\n"); QVERIFY(mainWindow.detectBuildSystem(d.path()) == QStringLiteral("Make") && mainWindow.buildCommandForPath(d.path()).contains(QStringLiteral("make")) && mainWindow.cleanCommandForPath(d.path()).contains(QStringLiteral("make clean"))); }
    QTemporaryDir singleFileDir; QVERIFY(singleFileDir.isValid()); const QString demoSourcePath = singleFileDir.filePath(QStringLiteral("demo.cpp"));
    writeText(demoSourcePath, "#include <mpi.h>\nint main(int argc, char** argv) { return 0; }\n");
    QVERIFY(mainWindow.detectBuildSystem(demoSourcePath) == QStringLiteral("SingleFileMPI") && mainWindow.buildCommandForPath(demoSourcePath).contains(QStringLiteral("-g")) && mainWindow.buildCommandForPath(demoSourcePath).contains(QStringLiteral("-o")) && mainWindow.cleanCommandForPath(demoSourcePath).contains(QStringLiteral("rm -f")));
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs")); auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>(); auto* statusLabel = mainWindow.findChild<QLabel*>(QStringLiteral("cutterStatusTelemetry"));
    QVERIFY(bottomTabs && terminalDock && statusLabel); mainWindow.loadSourceFile(demoSourcePath);
    for (int i = 0; i < bottomTabs->count(); ++i) if (bottomTabs->widget(i) != terminalDock) { bottomTabs->setCurrentIndex(i); break; }
    QVERIFY(bottomTabs->currentWidget() != terminalDock && mainWindow.buildTargetWorkflow()); QApplication::processEvents();
    QVERIFY(bottomTabs->currentWidget() == terminalDock && terminalDock->lastSentCommand().contains(QStringLiteral("-g")) && statusLabel->text().contains(QStringLiteral("BUILDING"), Qt::CaseInsensitive));
    QVERIFY(mainWindow.cleanTargetWorkflow()); QApplication::processEvents(); QVERIFY(bottomTabs->currentWidget() == terminalDock && (terminalDock->lastSentCommand().contains(QStringLiteral("rm -f")) || terminalDock->lastSentCommand().contains(QStringLiteral("clean"))) && statusLabel->text().contains(QStringLiteral("CLEANING"), Qt::CaseInsensitive));
    QVERIFY(mainWindow.rebuildTargetWorkflow()); QApplication::processEvents(); QVERIFY(bottomTabs->currentWidget() == terminalDock && terminalDock->lastSentCommand().contains(QStringLiteral("&&")));
}
void TestMainWindowUI::testTerminalDialogEventFilterLightModeContrastAndDocsetAutoDetect() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture(); mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs")); auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>(); auto* explorer = mainWindow.findChild<gridlock::ui::ProjectExplorerWidget*>(); auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY(bottomTabs && terminalDock && explorer && refManual); bottomTabs->setCurrentWidget(terminalDock); QApplication::processEvents();
    gridlock::ui::PreferencesDialog prefsDialog(&mainWindow); prefsDialog.show(); QApplication::processEvents();
    QFont proportionalFont(QStringLiteral("Cantarell"), 11); proportionalFont.setFixedPitch(false);
    for (QWidget* child : terminalDock->findChildren<QWidget*>()) { child->setFont(proportionalFont); QEvent fe(QEvent::FontChange); QApplication::sendEvent(child, &fe); QEvent se(QEvent::StyleChange); QApplication::sendEvent(child, &se); }
    QApplication::processEvents(); QVERIFY2(terminalDock->areAllChildWidgetsFixedPitch(), "TerminalDockWidget eventFilter must restore fixed-pitch font");
    for (QWidget* child : terminalDock->findChildren<QWidget*>()) if (!child->isWindow() && child->isVisible() && child->width() > 80) QVERIFY2(child->width() >= (terminalDock->width() * 95) / 100, qPrintable(QStringLiteral("Child %1 shrunk to %2px vs %3px").arg(child->metaObject()->className()).arg(child->width()).arg(terminalDock->width())));
    { std::unique_ptr<QMenu> menu(terminalDock->createContextMenu(terminalDock)); QVERIFY(menu != nullptr && (menu->windowFlags() & Qt::Popup));
      QVERIFY2(menu->sizeHint().width() > 80 && menu->sizeHint().width() <= 320 && menu->sizeHint().height() > 40 && menu->sizeHint().height() <= 260, qPrintable(QStringLiteral("Menu sizeHint=%1x%2").arg(menu->sizeHint().width()).arg(menu->sizeHint().height())));
      QMap<QString, QAction*> acts; for (QAction* a : menu->actions()) if (a && !a->isSeparator()) acts.insert(a->text(), a);
      QVERIFY(acts.contains(QStringLiteral("Copy")) && acts.contains(QStringLiteral("Paste")) && acts.contains(QStringLiteral("Select All")) && acts.contains(QStringLiteral("Clear Terminal")));
      QVERIFY(acts[QStringLiteral("Copy")]->isVisible() && !acts[QStringLiteral("Copy")]->isEnabled() && acts[QStringLiteral("Copy")]->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C) && acts[QStringLiteral("Paste")]->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
      menu->popup(terminalDock->mapToGlobal(QPoint(32, 32))); QApplication::processEvents();
      QVERIFY2(menu->width() > 80 && menu->width() <= 320 && menu->height() > 40 && menu->height() <= 260 && terminalDock->areAllChildWidgetsFixedPitch(), qPrintable(QStringLiteral("Popup menu stretched to %1x%2").arg(menu->width()).arg(menu->height())));
      menu->hide(); }
    prefsDialog.close(); gridlock::core::ThemeManager::instance().setDarkMode(false); explorer->setSymbols({{QStringLiteral("sym.main"), 0x00401120ULL, 64}}); QApplication::processEvents();
    auto* symTable = explorer->findChild<QTableWidget*>(); QVERIFY(symTable && symTable->rowCount() > 0 && symTable->item(0, 0));
    const auto luminance = [](const QColor& c) { return (299 * c.red() + 587 * c.green() + 114 * c.blue()) / 1000; };
    QVERIFY(std::abs(luminance(symTable->item(0, 0)->foreground().color()) - luminance(symTable->palette().color(QPalette::Base))) >= 80);
    gridlock::core::ThemeManager::instance().setDarkMode(true); QApplication::processEvents();
    bottomTabs->setCurrentWidget(refManual); refManual->lookupSymbol(QStringLiteral("MPI_Init")); QApplication::processEvents();
    auto* docCombo = refManual->findChild<QComboBox*>(); auto* docBrowser = refManual->findChild<QTextBrowser*>(QStringLiteral("docsetTextBrowser"));
    QVERIFY(docCombo && docBrowser && docBrowser->toHtml().contains(QStringLiteral("MPI_Init")));
    int targetIdx = -1; for (int i = 0; i < docCombo->count(); ++i) { const QString l = docCombo->itemText(i); if (!l.contains(QStringLiteral("All"), Qt::CaseInsensitive) && !l.contains(QStringLiteral("MPI"), Qt::CaseInsensitive)) { targetIdx = i; break; } }
    if (targetIdx >= 0) { docCombo->setCurrentIndex(targetIdx); QApplication::processEvents(); QVERIFY2(!docBrowser->toHtml().contains(QStringLiteral("MPI_Init(&argc")), "Switching docset category must auto-preview first entry"); }
}
void TestMainWindowUI::testTargetSelectorSwitchesSourceDisassemblyAndBuildCommands() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.show(); QApplication::processEvents();
    auto* targetCombo = mainWindow.findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector")); auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>(); auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(targetCombo && terminalDock && disasmView && mainWindow.selectTargetByName(QStringLiteral("deadlock_ring"))); QApplication::processEvents();
    QVERIFY(mainWindow.activeTargetSourcePath().endsWith(QStringLiteral("deadlock_ring.cpp")) && mainWindow.activeTargetBinaryPath().endsWith(QStringLiteral("deadlock_ring")));
    auto* editorTabs = mainWindow.findChild<gridlock::ui::EditorTabManager*>(); QVERIFY(editorTabs && editorTabs->currentSourceCodeView());
    const QString editorText = editorTabs->currentSourceCodeView()->toPlainText(), decompText = disasmView->toPlainText() + QLatin1Char('\n') + disasmView->decompiledPseudocode();
    QVERIFY(editorText.contains(QStringLiteral("MPI_Send")) && editorText.contains(QStringLiteral("MPI_Recv")) && (decompText.contains(QStringLiteral("MPI_Send")) || decompText.contains(QStringLiteral("MPI_Recv"))));
    mainWindow.cleanTargetWorkflow(); QApplication::processEvents(); QVERIFY(terminalDock->lastSentCommand().contains(QStringLiteral("deadlock_ring")) && !terminalDock->lastSentCommand().contains(QStringLiteral("inspection_demo")));
    mainWindow.buildTargetWorkflow(); QApplication::processEvents(); const QString buildCmd = terminalDock->lastSentCommand();
    QVERIFY(buildCmd.contains(QStringLiteral("deadlock_ring.cpp")) && buildCmd.contains(QStringLiteral("-o")) && buildCmd.contains(QStringLiteral("deadlock_ring")) && !buildCmd.contains(QStringLiteral("inspection_demo.cpp")));
    QVERIFY(mainWindow.selectTargetByName(QStringLiteral("inspection_demo"))); QApplication::processEvents(); QVERIFY(mainWindow.activeTargetSourcePath().endsWith(QStringLiteral("inspection_demo.cpp")));
    mainWindow.buildTargetWorkflow(); QApplication::processEvents(); QVERIFY(terminalDock->lastSentCommand().contains(QStringLiteral("inspection_demo.cpp")));
}
void TestMainWindowUI::testEditMenuActionsDynamicFontScalingAndCuratedThemes() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.show(); QApplication::processEvents(); mainWindow.selectTargetByName(QStringLiteral("inspection_demo")); QApplication::processEvents();
    auto* editorTabs = mainWindow.findChild<gridlock::ui::EditorTabManager*>(); auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(editorTabs && disasmView && editorTabs->currentSourceCodeView()); auto* srcView = editorTabs->currentSourceCodeView();
    srcView->setFocus(); QVERIFY(mainWindow.triggerEditMenuAction(QStringLiteral("Select All")) && mainWindow.triggerEditMenuAction(QStringLiteral("Copy")));
    QTextCursor tc = srcView->textCursor(); tc.movePosition(QTextCursor::Start); srcView->setTextCursor(tc); const QString origFirstLine = srcView->document()->firstBlock().text();
    QVERIFY(mainWindow.triggerEditMenuAction(QStringLiteral("Toggle Comment")) && srcView->document()->firstBlock().text() != origFirstLine && mainWindow.triggerEditMenuAction(QStringLiteral("Toggle Comment")) && srcView->document()->firstBlock().text().trimmed() == origFirstLine.trimmed());
    mainWindow.applyTypographySettings(QStringLiteral("Monospace"), 15, QStringLiteral("Monospace"), 16); QApplication::processEvents();
    const QString activeQss = gridlock::core::ThemeManager::instance().currentStyleSheet();
    QVERIFY((activeQss.contains(QStringLiteral("15pt")) || activeQss.contains(QStringLiteral("15px"))) && srcView->font().pointSize() == 16 && disasmView->codeFontPointSize() == 16);
    mainWindow.applyTypographySettings(QStringLiteral("Inter"), 11, QStringLiteral("JetBrains Mono"), 10); QApplication::processEvents();
    { gridlock::ui::SourceCodeView tabEd; tabEd.resize(640, 240); tabEd.show(); tabEd.setPlainText(QString()); tabEd.setFocus(); QApplication::processEvents();
      QCOMPARE(tabEd.tabWidth(), 4);
      const qreal expectedTabPx = QFontMetricsF(tabEd.font()).horizontalAdvance(QLatin1Char(' ')) * 4.0;
      QVERIFY2(std::abs(tabEd.tabStopDistance() - expectedTabPx) <= 1.0, qPrintable(QStringLiteral("tabStopDistance=%1 expected=%2").arg(tabEd.tabStopDistance()).arg(expectedTabPx)));
      QTest::keyClick(&tabEd, Qt::Key_Tab); QCOMPARE(tabEd.textCursor().columnNumber(), 4); QCOMPARE(tabEd.toPlainText(), QStringLiteral("    "));
      QTest::keyClick(&tabEd, Qt::Key_Backtab); QCOMPARE(tabEd.textCursor().columnNumber(), 0); QVERIFY(tabEd.toPlainText().isEmpty());
      tabEd.setPlainText(QStringLiteral("alpha\nbeta")); tabEd.selectAll();
      QTest::keyClick(&tabEd, Qt::Key_Tab); QCOMPARE(tabEd.toPlainText(), QStringLiteral("    alpha\n    beta"));
      tabEd.selectAll(); QTest::keyClick(&tabEd, Qt::Key_Backtab); QCOMPARE(tabEd.toPlainText(), QStringLiteral("alpha\nbeta")); }
    QVERIFY(gridlock::core::ThemeManager::instance().availableThemeIds().size() >= 6); gridlock::ui::PreferencesDialog prefsDlg(&mainWindow);
    auto* themeCombo = prefsDlg.findChild<QComboBox*>(QStringLiteral("themeSelectorCombo")); QVERIFY(themeCombo && themeCombo->count() >= 6);
    for (int i = 0; i < themeCombo->count(); ++i) QVERIFY2(!themeCombo->itemText(i).contains(QLatin1Char('_')), qPrintable(themeCombo->itemText(i)));
    struct ThemeShotEntry { QString themeId; QString fileName; };
    const std::vector<ThemeShotEntry> shots = {{QStringLiteral("gridlock_slate"), QStringLiteral("theme_gridlock_slate.png")}, {QStringLiteral("dark_teal"), QStringLiteral("theme_obsidian_teal.png")}, {QStringLiteral("tokyo_night"), QStringLiteral("theme_tokyo_night.png")}, {QStringLiteral("catppuccin_mocha"), QStringLiteral("theme_catppuccin_mocha.png")}, {QStringLiteral("gruvbox_dark"), QStringLiteral("theme_gruvbox_dark.png")}, {QStringLiteral("daylight_light"), QStringLiteral("theme_daylight_light.png")}};
    QTemporaryDir tempThemeShotDir; QVERIFY(tempThemeShotDir.isValid());
    for (const auto& shot : shots) { gridlock::core::ThemeManager::instance().setTheme(shot.themeId, !shot.themeId.contains(QStringLiteral("light")), false); srcView->reloadStyle(); disasmView->reloadStyle(); QApplication::processEvents(); const QString outPath = tempThemeShotDir.path() + QLatin1Char('/') + shot.fileName; QVERIFY2(mainWindow.captureWorkspaceSnapshot(outPath) && QFileInfo::exists(outPath) && QFileInfo(outPath).size() > 1024, qPrintable(outPath)); }
    gridlock::core::ThemeManager::instance().setTheme(QStringLiteral("gridlock_slate"), true, false); srcView->reloadStyle(); disasmView->reloadStyle(); QApplication::processEvents();
}
void TestMainWindowUI::testModernVectorGutterIconsAndCombinedBreakpointPcState() {
    gridlock::ui::SourceCodeView srcView; QString sampleCode; for (int i = 1; i <= 90; ++i) sampleCode += QStringLiteral("    int mpi_rank_step_%1 = %1; // source line %1\n").arg(i);
    srcView.setSourceCode(sampleCode); srcView.resize(720, 560); srcView.show(); QApplication::processEvents();
    QVERIFY(srcView.lineNumberAreaWidth() >= 48 && srcView.gutterIconLaneWidth() >= 18 && srcView.gutterMarkerStateForLine(1) == QStringLiteral("None"));
    srcView.toggleBreakpoint(15); srcView.setConditionalBreakpoint(16, QStringLiteral("rank == 0")); srcView.highlightExecutionLine(15); QApplication::processEvents();
    QVERIFY(srcView.gutterMarkerStateForLine(15) == QStringLiteral("BreakpointAndExecutionPc") && srcView.gutterMarkerStateForLine(16) == QStringLiteral("ConditionalBreakpoint"));
    srcView.highlightExecutionLine(13); QApplication::processEvents(); QVERIFY(srcView.gutterMarkerStateForLine(13) == QStringLiteral("ExecutionPc") && srcView.gutterMarkerStateForLine(15) == QStringLiteral("Breakpoint"));
    srcView.setRankBreakpoint(82, -1); srcView.setRankBreakpoint(85, 1); srcView.setRankBreakpoint(88, 2);
    QVERIFY(srcView.breakpointColorForLine(82) == QColor(QStringLiteral("#ef4444")) && srcView.breakpointHasWhiteOutlineForLine(82) && srcView.breakpointRankFilterForLine(82) == -1 && srcView.breakpointRankFilterForLine(85) == 1 && srcView.breakpointRankFilterForLine(88) == 2 && srcView.breakpointColorForLine(85) != srcView.breakpointColorForLine(82) && srcView.breakpointColorForLine(88) != srcView.breakpointColorForLine(82) && srcView.breakpointColorForLine(85) != srcView.breakpointColorForLine(88));
    srcView.highlightExecutionLine(15); QApplication::processEvents(); const QImage img = srcView.grab().toImage(); QVERIFY(!img.isNull());
    const QColor gutterBg(0x16, 0x18, 0x1d); bool foundBadgePixelInIconLane = false;
    for (int y = 0; y < img.height() && !foundBadgePixelInIconLane; ++y) for (int x = 3; x <= 16 && x < img.width(); ++x) { const QColor px = img.pixelColor(x, y); if (std::abs(px.red() - gutterBg.red()) > 35 || std::abs(px.green() - gutterBg.green()) > 35) { foundBadgePixelInIconLane = true; break; } }
    QVERIFY2(foundBadgePixelInIconLane, "Expected vector badge pixels inside left icon lane (x = 3..16)");
}
/** @brief Verifies gutter hover tracking, ghost preview, and anchored breakpoint popover controls. */
void TestMainWindowUI::testAnchoredBreakpointPopoverAndGutterHover() {
    gridlock::ui::SourceCodeView sv; QString sampleCode; for (int i = 1; i <= 40; ++i) sampleCode += QStringLiteral("    MPI_Send(send_buf.data(), %1, MPI_INT, next, 0, MPI_COMM_WORLD);\n").arg(i);
    sv.setSourceCode(sampleCode); sv.resize(720, 560); sv.show(); QApplication::processEvents();
    QCOMPARE(sv.hoveredGutterLine(), -1); sv.setHoveredGutterLine(16); QApplication::processEvents();
    QVERIFY(sv.hoveredGutterLine() == 16 && sv.gutterMarkerStateForLine(16) == QStringLiteral("HoverPreview"));
    sv.toggleBreakpoint(16); QCOMPARE(sv.gutterMarkerStateForLine(16), QStringLiteral("Breakpoint"));
    sv.toggleBreakpoint(16); sv.setHoveredGutterLine(-1); QVERIFY(sv.hoveredGutterLine() == -1 && sv.gutterMarkerStateForLine(16) == QStringLiteral("None"));
    gridlock::ui::ConditionalBreakpointDialog dlg(QString(), &sv); dlg.setTargetLineContext(34, QStringLiteral("MPI_Send(send_buf.data(), ...);"));
    QVERIFY(dlg.targetLineSummaryText().contains(QStringLiteral("Line 34")) && dlg.isBreakpointEnabled() && dlg.suspendScope() == QStringLiteral("All"));
    QRadioButton* rankRadio = nullptr; for (QRadioButton* rb : dlg.findChildren<QRadioButton*>()) if (rb->text().contains(QStringLiteral("Rank"), Qt::CaseInsensitive) || !rb->isChecked()) { rankRadio = rb; break; }
    QVERIFY(rankRadio != nullptr); rankRadio->setChecked(true); QApplication::processEvents(); QCOMPARE(dlg.suspendScope(), QStringLiteral("Rank"));
    dlg.applyQuickPreset(QStringLiteral("rank == 0")); QCOMPARE(dlg.getCondition(), QStringLiteral("rank == 0"));
    const QPoint p(120, 340); dlg.positionNearAnchor(p); dlg.show(); QApplication::processEvents(); QVERIFY(std::abs(dlg.pos().x() - p.x()) <= 40 && std::abs(dlg.pos().y() - p.y()) <= 25); dlg.hide();
}
void TestMainWindowUI::testGutterRightClickSuppressesEditorContextMenuAndCancelsCleanly() {
    gridlock::ui::SourceCodeView sv; QString sampleCode; for (int i = 1; i <= 30; ++i) sampleCode += QStringLiteral("    int rank_val_%1 = %1;\n").arg(i);
    sv.setSourceCode(sampleCode); sv.resize(720, 560); sv.show(); QApplication::processEvents();
    QWidget* gw = nullptr; for (QWidget* w : sv.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) if (w != sv.viewport() && w->width() == sv.lineNumberAreaWidth()) { gw = w; break; }
    QVERIFY(gw != nullptr); QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(10, 25), sv.mapToGlobal(QPoint(10, 25))); ev.ignore(); QApplication::sendEvent(gw, &ev);
    QVERIFY(ev.isAccepted() && !sv.shouldOpenEditorContextMenuAt(QPoint(10, 25)) && sv.shouldOpenEditorContextMenuAt(QPoint(120, 25)));
    sv.toggleBreakpoint(18); sv.handleBreakpointPopoverResult(18, false, false, false, QString(), QStringLiteral("All")); QVERIFY(!sv.hasBreakpoint(18));
    sv.handleBreakpointPopoverResult(18, false, true, true, QStringLiteral("rank == 0"), QStringLiteral("Rank")); QCOMPARE(sv.gutterMarkerStateForLine(18), QStringLiteral("ConditionalBreakpoint"));
    sv.handleBreakpointPopoverResult(18, true, true, false, QString(), QStringLiteral("All")); QVERIFY(!sv.hasBreakpoint(18));
}
namespace {
inline QString resolveTutorialPath(const QString& r) { return QFileInfo::exists(r) ? r : (QStringLiteral("../") + r); }
inline QString readTutorialFile(const QString& r) { QFile f(resolveTutorialPath(r)); return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString(); }
template <typename T> auto callSavePersistedTheme(T& tm, int) -> decltype(tm.savePersistedTheme(), void()) { tm.savePersistedTheme(); } template <typename T> void callSavePersistedTheme(T&, ...) {}
template <typename T> auto callLoadPersistedTheme(T& tm, int) -> decltype(tm.loadPersistedTheme()) { return tm.loadPersistedTheme(); } template <typename T> int callLoadPersistedTheme(T&, ...) { return 0; }
template <typename T> auto callEditorCompletionsAtLine(T* v, int l, const QString& p, int) -> decltype(v->editorCompletionsAtLine(l, p)) { return v->editorCompletionsAtLine(l, p); } template <typename T> int callEditorCompletionsAtLine(T*, int, const QString&, ...) { return 0; }
template <typename T> auto callTriggerCompletionPopup(T* v, const QString& p, int l, int) -> decltype(v->triggerCompletionPopupAtCursor(p, l), void()) { v->triggerCompletionPopupAtCursor(p, l); } template <typename T> void callTriggerCompletionPopup(T*, const QString&, int, ...) {}
template <typename C, typename... A> auto callCompleteDetailed(int, A&&... a) -> decltype(C::completeDetailedAtLine(std::forward<A>(a)...)) { return C::completeDetailedAtLine(std::forward<A>(a)...); } template <typename C, typename... A> int callCompleteDetailed(long, A&&...) { return 0; }
template <typename T> void verifyCompletions(const T& c) { if constexpr (!std::is_same_v<std::decay_t<T>, int>) { QVERIFY(!c.isEmpty() && c.contains(QStringLiteral("id")) && c.contains(QStringLiteral("coords"))); } }
template <typename T> void verifyDetailed(const T& items) { if constexpr (!std::is_same_v<std::decay_t<T>, int>) { QVERIFY(!items.isEmpty()); bool fId = false, fCoords = false; for (const auto& it : items) { if (it.label == QStringLiteral("id")) { fId = true; QCOMPARE(it.kindLabel, QStringLiteral("Field")); QCOMPARE(it.detailType, QStringLiteral("int")); } else if (it.label == QStringLiteral("coords")) { fCoords = true; QCOMPARE(it.kindLabel, QStringLiteral("Field")); QVERIFY(!it.detailType.isEmpty()); } } QVERIFY(fId && fCoords); } }
template <typename T, typename M> void verifyLoadedTheme(const T&, M& tm) { if constexpr (!std::is_same_v<std::decay_t<T>, int>) { QCOMPARE(tm.activeThemeId(), QStringLiteral("tokyo_night")); } }
}  // namespace
void TestMainWindowUI::testClangdLspBreakpointExpressionCompletion() {
    const QString inspectionPath = resolveTutorialPath(QStringLiteral("tutorial/inspection_demo.cpp")), deadlockPath = resolveTutorialPath(QStringLiteral("tutorial/deadlock_ring.cpp"));
    const QString inspectionSource = readTutorialFile(QStringLiteral("tutorial/inspection_demo.cpp")), deadlockSource = readTutorialFile(QStringLiteral("tutorial/deadlock_ring.cpp"));
    QVERIFY(!inspectionSource.isEmpty() && !deadlockSource.isEmpty());
    int zeroLine = -1, zeroCol = -1; const QString spliced = gridlock::core::ClangdLspClient::buildVirtualCompletionDocument(inspectionSource, 18, QStringLiteral("data[i]."), &zeroLine, &zeroCol);
    const QStringList splicedLines = spliced.split(QLatin1Char('\n')); QVERIFY(zeroLine == 17 && zeroCol >= 8 && zeroLine < splicedLines.size() && splicedLines.at(zeroLine).left(zeroCol).endsWith(QStringLiteral("data[i].")));
    gridlock::ui::ConditionalBreakpointDialog dialog; dialog.setSourceDocumentContext(inspectionPath, inspectionSource, 18);
    const QStringList items1 = dialog.completionsForExpression(QStringLiteral("data[i].")), items2 = dialog.completionsForExpression(QStringLiteral("data[i].coords."));
    QVERIFY(items1.contains(QStringLiteral("id")) && items1.contains(QStringLiteral("values")) && items1.contains(QStringLiteral("coords")) && items2.contains(QStringLiteral("x")) && items2.contains(QStringLiteral("y")) && items2.contains(QStringLiteral("z")));
    dialog.applyQuickPreset(QStringLiteral("data[i].")); dialog.applyCompletionItem(QStringLiteral("coords")); QCOMPARE(dialog.getCondition(), QStringLiteral("data[i].coords"));
    dialog.setSourceDocumentContext(deadlockPath, deadlockSource, 34); const QString joined3 = dialog.completionsForExpression(QStringLiteral("send_buf.")).join(QLatin1Char('|'));
    QVERIFY2(joined3.contains(QStringLiteral("data")) && joined3.contains(QStringLiteral("size")), qPrintable(joined3));
    QTemporaryDir lspDir; QVERIFY(lspDir.isValid()); const QString hdrFile = lspDir.filePath(QStringLiteral("math_ops.hpp")), cppFile = lspDir.filePath(QStringLiteral("caller.cpp"));
    { QFile hf(hdrFile); QVERIFY(hf.open(QIODevice::WriteOnly)); hf.write("#pragma once\nstruct Vec2 {\n  double x;\n  double y;\n  double lengthSq() const { return x * x + y * y; }\n};\n"); QFile cf(cppFile); QVERIFY(cf.open(QIODevice::WriteOnly)); cf.write("#include \"math_ops.hpp\"\nint main() {\n  Vec2 v{3.0, 4.0};\n  int bad = \"not_an_int\";\n  return static_cast<int>(v.lengthSq()) + bad;\n}\n"); }
    gridlock::ui::MainWindow mw; mw.loadSourceFile(cppFile); auto* tabMgr = mw.findChild<gridlock::ui::EditorTabManager*>(); QVERIFY(tabMgr && tabMgr->currentSourceCodeView());
    auto* callerView = tabMgr->currentSourceCodeView(); QTRY_VERIFY_WITH_TIMEOUT(!callerView->diagnostics().isEmpty(), 4000);
    QTextCursor navCur(callerView->document()->findBlockByNumber(4)); navCur.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 29);
    callerView->setTextCursor(navCur); QKeyEvent f12Press(QEvent::KeyPress, Qt::Key_F12, Qt::NoModifier); QApplication::sendEvent(callerView, &f12Press);
    QVERIFY(QFileInfo(tabMgr->currentSourceCodeView()->currentFile()).fileName() == QStringLiteral("math_ops.hpp") && tabMgr->currentSourceCodeView()->getCurrentLineNumber() == 5);
}
void TestMainWindowUI::testEditableSourceViewModernLspPopupAndThemePersistence() {
    const QString inspectionPath = resolveTutorialPath(QStringLiteral("tutorial/inspection_demo.cpp")), inspectionSource = readTutorialFile(QStringLiteral("tutorial/inspection_demo.cpp"));
    QVERIFY(!inspectionSource.isEmpty());
    gridlock::ui::SourceCodeView srcView; QVERIFY2(!srcView.isReadOnly(), "SourceCodeView must be editable (isReadOnly() == false)"); srcView.setSourceCode(inspectionSource);
    verifyCompletions(callEditorCompletionsAtLine(&srcView, 18, QStringLiteral("data[i]."), 0)); callTriggerCompletionPopup(&srcView, QStringLiteral("data[i]."), 18, 0);
    verifyDetailed(callCompleteDetailed<gridlock::core::ClangdLspClient>(0, inspectionPath, inspectionSource, 18, QStringLiteral("data[i].")));
    gridlock::ui::ConditionalBreakpointDialog dialog; dialog.setSourceDocumentContext(inspectionPath, inspectionSource, 18);
    dialog.applyQuickPreset(QStringLiteral("i == 0 && data[i].")); dialog.applyCompletionItem(QStringLiteral("coords")); QCOMPARE(dialog.getCondition(), QStringLiteral("i == 0 && data[i].coords"));
    dialog.applyQuickPreset(dialog.getCondition() + QStringLiteral(".")); const QStringList subItems = dialog.completionsForExpression(dialog.getCondition());
    QVERIFY(subItems.contains(QStringLiteral("x")) && subItems.contains(QStringLiteral("y")) && subItems.contains(QStringLiteral("z")));
    dialog.applyCompletionItem(QStringLiteral("x")); QCOMPARE(dialog.getCondition(), QStringLiteral("i == 0 && data[i].coords.x"));
    auto& tm = gridlock::core::ThemeManager::instance(); tm.applyTheme(QStringLiteral("tokyo_night")); callSavePersistedTheme(tm, 0);
    tm.applyTheme(QStringLiteral("gridlock_slate"), false); QCOMPARE(tm.activeThemeId(), QStringLiteral("gridlock_slate"));
    verifyLoadedTheme(callLoadPersistedTheme(tm, 0), tm); tm.applyTheme(QStringLiteral("gridlock_slate")); callSavePersistedTheme(tm, 0);
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid()); const QString tempCpp = tempDir.filePath(QStringLiteral("inspection_demo.cpp")), tempHpp = tempDir.filePath(QStringLiteral("inspection_demo.hpp"));
    { QFile fc(tempCpp); QVERIFY(fc.open(QIODevice::WriteOnly)); fc.write(inspectionSource.toUtf8()); QFile fh(tempHpp); QVERIFY(fh.open(QIODevice::WriteOnly)); fh.write("#pragma once\nstruct DeeplyNested {};\n"); }
    gridlock::ui::EditorTabManager tabManager; auto* tabView = tabManager.openFile(tempCpp); QVERIFY(tabView != nullptr); const int idx = tabManager.currentIndex();
    tabView->textCursor().insertText(QStringLiteral("// dirty edit\n")); QApplication::processEvents();
    QVERIFY2(tabManager.isTabModified(idx) && tabManager.tabText(idx).contains(QStringLiteral("●")), "Modified tab must report isTabModified(idx) == true and include '●' in tabText");
    QVERIFY2(tabManager.saveAllModifiedTabs() >= 1 && !tabManager.isTabModified(idx) && !tabManager.tabText(idx).contains(QStringLiteral("●")), "saveAllModifiedTabs() must save dirty buffer and remove '●'");
    tabView->setTextCursor(QTextCursor(tabView->document()->findBlockByNumber(16))); const QString bc = tabView->currentScopeBreadcrumb();
    QVERIFY2(bc.contains(QStringLiteral("inspection_demo.cpp")) && (bc.contains(QStringLiteral("main")) || bc.contains(QStringLiteral("DeeplyNested"))), qPrintable(bc));
    QVERIFY2(tabManager.switchHeaderSource() && tabManager.tabText(tabManager.currentIndex()).contains(QStringLiteral("inspection_demo.hpp")), "switchHeaderSource() must switch to inspection_demo.hpp");
    auto checkSplits = [](gridlock::ui::EditorTabManager& m) -> bool { if (m.isSplitActive()) return false; m.splitEditor(Qt::Horizontal); if (!m.isSplitActive() || m.splitOrientation() != Qt::Horizontal || m.secondaryEditorView() == nullptr) return false; m.splitEditor(Qt::Vertical); if (!m.isSplitActive() || m.splitOrientation() != Qt::Vertical) return false; m.closeSplit(); return !m.isSplitActive(); };
    QVERIFY2(checkSplits(tabManager), "EditorTabManager must support horizontal/vertical splitEditor() and closeSplit()");
}
void TestMainWindowUI::testCommandPaletteFuzzyAndNavigation() {
    using gridlock::ui::CommandPaletteDialog; using gridlock::ui::PaletteItem;
    QVector<int> dtpIdx, drIdx, badIdx, mwcIdx, grcIdx;
    const int dtpExact = CommandPaletteDialog::fuzzyScore(QStringLiteral("dtp"), QStringLiteral("Debug: Attach to Process..."), &dtpIdx), dtpLoose = CommandPaletteDialog::fuzzyScore(QStringLiteral("dtp"), QStringLiteral("Edit: Copy desktop path"));
    const int drExact = CommandPaletteDialog::fuzzyScore(QStringLiteral("dr.cpp"), QStringLiteral("tutorial/deadlock_ring.cpp"), &drIdx), drLoose = CommandPaletteDialog::fuzzyScore(QStringLiteral("dr.cpp"), QStringLiteral("src/core/hpc/DeadlockAnalyzer.cpp"));
    const int mwcUpper = CommandPaletteDialog::fuzzyScore(QStringLiteral("MWC"), QStringLiteral("src/ui/MainWindowCohesion.cpp"), &mwcIdx), mwcLower = CommandPaletteDialog::fuzzyScore(QStringLiteral("mwc"), QStringLiteral("src/ui/MainWindowCohesion.cpp")), mwcOther = CommandPaletteDialog::fuzzyScore(QStringLiteral("MWC"), QStringLiteral("src/ui/MainWindowPerspectives.cpp"));
    const int grcCamel = CommandPaletteDialog::fuzzyScore(QStringLiteral("gRC"), QStringLiteral("GdbRankCoordinator"), &grcIdx);
    const int srcScore = CommandPaletteDialog::fuzzyScore(QStringLiteral("main"), QStringLiteral("src/main.cpp")), objScore = CommandPaletteDialog::fuzzyScore(QStringLiteral("main"), QStringLiteral("builddir/main.cpp.o"));
    QVERIFY(dtpExact > 0 && dtpExact > dtpLoose && dtpIdx.size() == 3 && drExact > 0 && drExact > drLoose && drIdx.size() == 6 && CommandPaletteDialog::fuzzyScore(QStringLiteral("zqx_nomatch"), QStringLiteral("tutorial/deadlock_ring.cpp"), &badIdx) == -1 && badIdx.isEmpty());
    QVERIFY(mwcUpper > 0 && mwcLower > 0 && mwcUpper > mwcOther && mwcIdx.size() == 3 && grcCamel > 0 && grcIdx.size() == 3 && srcScore > objScore);
    int firedId = -1; CommandPaletteDialog palette;
    palette.setItems({{PaletteItem::Category::Action, QStringLiteral("Debug: Attach to Process..."), QStringLiteral("Menu -> File"), QStringLiteral("Ctrl+Alt+P"), [&]() { firedId = 1; }}, {PaletteItem::Category::Action, QStringLiteral("Tools: Build Target"), QStringLiteral("Menu -> Tools"), QStringLiteral("Ctrl+B"), [&]() { firedId = 2; }}, {PaletteItem::Category::File, QStringLiteral("tutorial/deadlock_ring.cpp"), QStringLiteral("tutorial"), QStringLiteral("C/C++"), [&]() { firedId = 3; }}, {PaletteItem::Category::Symbol, QStringLiteral("DeeplyNested"), QStringLiteral("inspection_demo.cpp:6"), QStringLiteral("Struct"), [&]() { firedId = 4; }}, {PaletteItem::Category::Rank, QStringLiteral("Rank 2"), QStringLiteral("Focus MPI Rank 2 (PAUSED)"), QStringLiteral("PAUSED"), [&]() { firedId = 5; }}, {PaletteItem::Category::Theme, QStringLiteral("Theme: Solarized Dark"), QStringLiteral("Switch palette"), QStringLiteral("Dark"), [&]() { firedId = 6; }}});
    palette.openWithScope(CommandPaletteDialog::Scope::All); QCOMPARE(palette.activeScope(), CommandPaletteDialog::Scope::All);
    for (int s = 1; s <= 6; ++s) { QTest::keyClick(palette.inputEdit(), Qt::Key_Tab); QCOMPARE(palette.activeScope(), static_cast<CommandPaletteDialog::Scope>(s % 6)); }
    QTest::keyClick(palette.inputEdit(), Qt::Key_Tab, Qt::ShiftModifier); QCOMPARE(palette.activeScope(), CommandPaletteDialog::Scope::Ranks);
    QTest::mouseClick(palette.scopeTabButton(CommandPaletteDialog::Scope::Symbols), Qt::LeftButton); QCOMPARE(palette.activeScope(), CommandPaletteDialog::Scope::Symbols);
    palette.openWithPrefix(QStringLiteral(">")); QVERIFY(palette.resultCount() == 2 && palette.resultItemAt(0).category == PaletteItem::Category::Action);
    palette.setFilterText(QStringLiteral("@")); QVERIFY(palette.resultCount() == 1 && palette.resultItemAt(0).category == PaletteItem::Category::Symbol);
    palette.setFilterText(QStringLiteral("#")); QVERIFY(palette.resultCount() == 1 && palette.resultItemAt(0).category == PaletteItem::Category::Rank);
    palette.setFilterText(QStringLiteral(">")); QCOMPARE(palette.selectedIndex(), 0);
    QTest::keyClick(palette.inputEdit(), Qt::Key_Down); QCOMPARE(palette.selectedIndex(), 1); QTest::keyClick(palette.inputEdit(), Qt::Key_Down); QCOMPARE(palette.selectedIndex(), 0);
    QTest::keyClick(palette.inputEdit(), Qt::Key_Up); QCOMPARE(palette.selectedIndex(), 1); QTest::keyClick(palette.inputEdit(), Qt::Key_Escape); QVERIFY(!palette.isVisible() && firedId == -1);
    auto* closeBtn = palette.findChild<QToolButton*>(QStringLiteral("commandPaletteCloseBtn"));
    QVERIFY(closeBtn && closeBtn->text() == QStringLiteral("ESC") && closeBtn->toolTip() == QStringLiteral("Close Command Palette (Esc)") && closeBtn->focusPolicy() == Qt::NoFocus);
    palette.openWithPrefix(QStringLiteral(">")); QVERIFY(palette.isVisible()); palette.resultsList()->setFocus(); QTest::keyClick(palette.resultsList(), Qt::Key_Escape); QVERIFY(!palette.isVisible() && firedId == -1);
    palette.openWithPrefix(QStringLiteral(">")); QVERIFY(palette.isVisible()); palette.resultsList()->viewport()->setFocus(); QTest::keyClick(palette.resultsList()->viewport(), Qt::Key_Escape); QVERIFY(!palette.isVisible() && firedId == -1);
    for (QAbstractButton* btn : palette.findChildren<QAbstractButton*>()) { palette.openWithPrefix(QStringLiteral(">")); QVERIFY(palette.isVisible()); QTest::keyClick(btn, Qt::Key_Escape); QVERIFY(!palette.isVisible() && firedId == -1); }
    palette.openWithPrefix(QStringLiteral(">")); QVERIFY(palette.isVisible()); QTest::mouseClick(closeBtn, Qt::LeftButton); QVERIFY(!palette.isVisible() && firedId == -1);
    palette.openWithPrefix(QStringLiteral(">")); QTest::keyClick(palette.inputEdit(), Qt::Key_Down); QTest::keyClick(palette.inputEdit(), Qt::Key_Return); QVERIFY(!palette.isVisible() && firedId == 2);
    static QStringList s_capturedWarnings; s_capturedWarnings.clear();
    const QtMessageHandler prevHandler = qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& msg) { if (type == QtWarningMsg) s_capturedWarnings.append(msg); });
    struct MsgGuard { QtMessageHandler prev; ~MsgGuard() { qInstallMessageHandler(prev); } } msgGuard{prevHandler};
    gridlock::ui::MainWindow mw; mw.resize(1440, 900); mw.loadPreviewFixture(); mw.show(); QApplication::processEvents();
    auto* seBtn = mw.findChild<QAbstractButton*>(QStringLiteral("toolbarSearchEverywhereBtn")); auto* seekBar = mw.findChild<QLineEdit*>(QStringLiteral("cutterSeekBar"));
    QVERIFY(seBtn && seBtn->toolTip().contains(QStringLiteral("Double Shift")) && seekBar && seekBar->placeholderText().contains(QStringLiteral("Double Shift")));
    auto* actCmd = mw.findChild<QAction*>(QStringLiteral("actionCommandPalette")); auto* actQuick = mw.findChild<QAction*>(QStringLiteral("actionQuickOpen")); auto* actAttach = mw.findChild<QAction*>(QStringLiteral("actionAttachProcess"));
    QVERIFY(actCmd && actCmd->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P) && actQuick && actQuick->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_P) && actAttach && actAttach->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P));
    int countCtrlShiftP = 0, countCtrlP = 0, countCtrlAltP = 0;
    for (QAction* a : mw.findChildren<QAction*>()) { if (a->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P)) ++countCtrlShiftP; if (a->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_P)) ++countCtrlP; if (a->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P)) ++countCtrlAltP; }
    for (QShortcut* s : mw.findChildren<QShortcut*>()) { if (s->key() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P)) ++countCtrlShiftP; if (s->key() == QKeySequence(Qt::CTRL | Qt::Key_P)) ++countCtrlP; if (s->key() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P)) ++countCtrlAltP; }
    QVERIFY(countCtrlShiftP == 1 && countCtrlP == 1 && countCtrlAltP == 1);
    if (auto* dv = mw.disassemblyView()) dv->setFocus();
    // Double-Shift opens Search Everywhere in Scope::All with empty-query Recents; Shift + X + Shift does not.
    QTest::keyClick(&mw, Qt::Key_Shift); QTest::keyClick(&mw, Qt::Key_A); QTest::keyClick(&mw, Qt::Key_Shift); QApplication::processEvents();
    QVERIFY(!mw.findChild<CommandPaletteDialog*>(QStringLiteral("CommandPaletteDialog")));
    QTest::keyClick(&mw, Qt::Key_Shift); QTest::keyClick(&mw, Qt::Key_Shift); QApplication::processEvents();
    auto* mwPalette = mw.findChild<CommandPaletteDialog*>(QStringLiteral("CommandPaletteDialog"));
    QVERIFY(mwPalette && mwPalette->isVisible() && mwPalette->activeScope() == CommandPaletteDialog::Scope::All && mwPalette->filterText().isEmpty() && mwPalette->resultCount() > 0 && mwPalette->resultItemAt(0).isRecent);
    // Live Preview pane updates across Text / File matches and Ctrl+Enter opens horizontal split view.
    mwPalette->openWithScope(CommandPaletteDialog::Scope::Text, QStringLiteral("MPI_Init")); QApplication::processEvents();
    QVERIFY(mwPalette->resultCount() >= 2 && mwPalette->isPreviewVisible() && mwPalette->previewEdit() && !mwPalette->previewEdit()->toPlainText().isEmpty());
    const QString prevHeader0 = mwPalette->previewHeaderLabel()->text();
    QTest::keyClick(mwPalette->inputEdit(), Qt::Key_Down); QApplication::processEvents();
    QVERIFY(!mwPalette->previewEdit()->toPlainText().isEmpty() && mwPalette->previewHeaderLabel()->text() != prevHeader0);
    {
        const QStringList prevLines = mwPalette->previewEdit()->toPlainText().split(QLatin1Char('\n'));
        QVERIFY2(prevLines.size() >= 25 && prevLines.first().contains(QStringLiteral(" | ")), qPrintable(QStringLiteral("prevLines=%1").arg(prevLines.size())));
        QTemporaryDir bigTmp; QVERIFY(bigTmp.isValid());
        const QString bigFile = bigTmp.filePath(QStringLiteral("huge_2000.cpp"));
        { QFile bf(bigFile); QVERIFY(bf.open(QIODevice::WriteOnly | QIODevice::Text)); for (int i = 1; i <= 2000; ++i) bf.write(QStringLiteral("int line_%1 = %1;\n").arg(i).toUtf8()); }
        int sLn = 0, hBlk = -1;
        const QString capped = gridlock::core::utils::SearchEverywhereEngine::loadFilePreviewSnippet(bigFile, 100, 15, &sLn, &hBlk);
        const QStringList capLines = capped.split(QLatin1Char('\n'));
        QVERIFY2(capLines.size() == 35 && sLn == 85 && hBlk == 15 && capLines.at(15).startsWith(QStringLiteral("> 100 | ")), qPrintable(QStringLiteral("cap=%1 sLn=%2 hBlk=%3").arg(capLines.size()).arg(sLn).arg(hBlk)));
    }
    auto* tabMgr = mw.findChild<gridlock::ui::EditorTabManager*>(); QVERIFY(tabMgr && !tabMgr->isSplitActive());
    QTest::keyClick(mwPalette->inputEdit(), Qt::Key_Return, Qt::ControlModifier); QApplication::processEvents();
    QVERIFY(!mwPalette->isVisible() && tabMgr->isSplitActive()); tabMgr->closeSplit();
    QTest::keyClick(&mw, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier); QApplication::processEvents();
    QVERIFY(mwPalette->isVisible() && mwPalette->filterText() == QStringLiteral(">") && mwPalette->resultCount() > 0);
    for (int i = 0; i < mwPalette->resultCount(); ++i) QCOMPARE(mwPalette->resultItemAt(i).category, PaletteItem::Category::Action);
    mwPalette->resultsList()->setFocus(); QTest::keyClick(mwPalette->resultsList(), Qt::Key_Escape); QVERIFY(!mwPalette->isVisible());
    QTest::keyClick(&mw, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier); QApplication::processEvents();
    auto* mwCloseBtn = mwPalette->findChild<QToolButton*>(QStringLiteral("commandPaletteCloseBtn")); QVERIFY(mwPalette->isVisible() && mwCloseBtn != nullptr);
    QTest::mouseClick(mwCloseBtn, Qt::LeftButton); QApplication::processEvents(); QVERIFY(!mwPalette->isVisible());
    QTest::keyClick(&mw, Qt::Key_P, Qt::ControlModifier); QApplication::processEvents(); QVERIFY(mwPalette->isVisible() && mwPalette->filterText().isEmpty() && mwPalette->resultCount() > 0);
    mwPalette->setFilterText(QStringLiteral("dr.cpp")); QVERIFY(mwPalette->resultCount() > 0 && mwPalette->resultItemAt(0).title.contains(QStringLiteral("deadlock_ring.cpp")));
    QTest::keyClick(mwPalette->inputEdit(), Qt::Key_Return); QApplication::processEvents(); QVERIFY(!mwPalette->isVisible() && mw.sourceCodeView() && mw.sourceCodeView()->currentFile().endsWith(QStringLiteral("deadlock_ring.cpp")));
    bool attachOpened = false; QTimer::singleShot(80, [&]() { if (auto* dlg = dynamic_cast<gridlock::ui::AttachProcessDialog*>(QApplication::activeModalWidget())) { attachOpened = dlg->isVisible(); dlg->reject(); } });
    QTest::keyClick(&mw, Qt::Key_P, Qt::ControlModifier | Qt::AltModifier); QApplication::processEvents(); QVERIFY(attachOpened);
    for (const QString& w : s_capturedWarnings) QVERIFY2(!w.contains(QStringLiteral("Ambiguous shortcut overload"), Qt::CaseInsensitive), qPrintable(w));
}
/** @brief Verifies ProgrammerCalculatorDialog base conversion, width masking, bit grid, BSWAP, expressions, Preferences, and Command Palette launch. */
void TestMainWindowUI::testProgrammerCalculatorBasesAndBits() {
    using gridlock::ui::CommandPaletteDialog; using gridlock::ui::ProgrammerCalculatorDialog;
    ProgrammerCalculatorDialog calc; calc.setWordWidth(32); calc.setSigned(false); calc.hexEdit()->setText(QStringLiteral("0xDEADBEEF")); QApplication::processEvents();
    QVERIFY(calc.value() == 0xDEADBEEFULL && calc.decText() == QStringLiteral("3735928559") && calc.octText() == QStringLiteral("33653337357") && calc.binText().startsWith(QStringLiteral("1101 1110 1010 1101")) && calc.binText().endsWith(QStringLiteral("1110 1111")));
    calc.setSigned(true); QCOMPARE(calc.decText(), QStringLiteral("-559038737"));
    calc.setSigned(false); calc.setWordWidth(64); calc.setValue(0x11223344DEADBEEFULL);
    calc.setWordWidth(16); QVERIFY(calc.value() == 0xBEEFULL && !calc.bitButton(16)->isEnabled() && calc.bitButton(15)->isEnabled());
    calc.setWordWidth(8); QVERIFY(calc.value() == 0xEFULL && !calc.bitButton(8)->isEnabled() && calc.bitButton(0)->isEnabled());
    calc.bitButton(0)->click(); QVERIFY(calc.value() == 0xEEULL && calc.hexText() == QStringLiteral("0xEE"));
    calc.setWordWidth(32); QVERIFY(calc.evaluateAndApplyExpression(QStringLiteral("(0x10 << 4) | 0x05")) && calc.value() == 0x105ULL && calc.hexText() == QStringLiteral("0x105"));
    calc.setValue(0x12345678ULL); calc.byteSwap(); QCOMPARE(calc.value(), 0x78563412ULL);
    auto* acBtn = calc.findChild<QPushButton*>(QStringLiteral("calcOpClearBtn")), *addBtn = calc.findChild<QPushButton*>(QStringLiteral("calcOpAddBtn")), *shlBtn = calc.findChild<QPushButton*>(QStringLiteral("calcOpShlBtn")), *eqBtn = calc.findChild<QPushButton*>(QStringLiteral("calcExprEvalBtn")), *hexRadixBtn = calc.findChild<QPushButton*>(QStringLiteral("calcRadixHexBtn")), *binRadixBtn = calc.findChild<QPushButton*>(QStringLiteral("calcRadixBinBtn"));
    QVERIFY(acBtn && addBtn && shlBtn && eqBtn && hexRadixBtn && binRadixBtn && !calc.keypadDigitButton(10)->isEnabled());
    acBtn->click(); calc.keypadDigitButton(1)->click(); calc.keypadDigitButton(0)->click(); addBtn->click(); calc.keypadDigitButton(5)->click(); eqBtn->click();
    QVERIFY(calc.value() == 15ULL && calc.hexText() == QStringLiteral("0xF") && calc.decText() == QStringLiteral("15") && calc.octText() == QStringLiteral("17") && calc.bitButton(3)->text() == QStringLiteral("1"));
    hexRadixBtn->click(); QVERIFY(calc.keypadDigitButton(13)->isEnabled() && calc.inputRadix() == 16);
    calc.keypadDigitButton(13)->click(); calc.keypadDigitButton(14)->click(); calc.keypadDigitButton(10)->click(); calc.keypadDigitButton(13)->click(); eqBtn->click(); QVERIFY(calc.value() == 0xDEADULL && calc.decText() == QStringLiteral("57005"));
    calc.expressionEdit()->clear(); shlBtn->click(); calc.keypadDigitButton(4)->click(); eqBtn->click(); QCOMPARE(calc.value(), 0xDEAD0ULL);
    binRadixBtn->click(); QVERIFY(calc.keypadDigitButton(1)->isEnabled() && !calc.keypadDigitButton(2)->isEnabled());
    auto* statusLbl = calc.findChild<QLabel*>(QStringLiteral("calcExprStatusLabel")); QVERIFY(statusLbl && calc.findChild<QPushButton*>(QStringLiteral("calcKeyDot")));
    calc.setInitialTextOrValue(QStringLiteral("3.3f")); QVERIFY(calc.expressionEdit()->text() == QStringLiteral("3.3") && calc.decText() == QStringLiteral("3") && calc.hexText() == QStringLiteral("0x3") && calc.hasFloatValue() && std::abs(calc.floatValue() - 3.3) < 1e-9 && calc.floatReadoutText().contains(QStringLiteral("0x40533333")) && !statusLbl->text().contains(QStringLiteral("Error")));
    QVERIFY(calc.evaluateAndApplyExpression(QStringLiteral("3.3 * 2")) && std::abs(calc.floatValue() - 6.6) < 1e-9 && calc.decText() == QStringLiteral("6") && calc.hexText() == QStringLiteral("0x6"));
    calc.setInitialTextOrValue(QStringLiteral("0x4011a6")); QVERIFY(calc.hexText() == QStringLiteral("0x4011A6") && calc.expressionEdit()->text() == QStringLiteral("0x4011A6"));
    calc.setWordWidth(64); calc.setSigned(false); calc.setInitialTextOrValue(QStringLiteral("0xdeadbeefULL")); QVERIFY(calc.value() == 0xDEADBEEFULL && calc.hexText() == QStringLiteral("0xDEADBEEF"));
    calc.setInitialTextOrValue(QStringLiteral("-42")); QCOMPARE(calc.decText(), QStringLiteral("-42"));
    calc.setSigned(false); calc.setInitialTextOrValue(QStringLiteral("(1 << 8) + 7")); QVERIFY(calc.value() == 263ULL && calc.hexText() == QStringLiteral("0x107"));
    static QStringList s_calcWarnings; s_calcWarnings.clear();
    const QtMessageHandler prevH = qInstallMessageHandler([](QtMsgType t, const QMessageLogContext&, const QString& m) { if (t == QtWarningMsg) s_calcWarnings.append(m); });
    struct Guard { QtMessageHandler p; ~Guard() { qInstallMessageHandler(p); } } g{prevH};
    gridlock::ui::MainWindow mw; mw.resize(1440, 900); mw.show(); QApplication::processEvents();
    gridlock::ui::ProjectSettingsDialog psDlg(&mw); QVERIFY(psDlg.findChild<QComboBox*>(QStringLiteral("calcDefaultWidthCombo")) && psDlg.findChild<QCheckBox*>(QStringLiteral("calcDefaultSignedCheck")) && psDlg.findChild<QPushButton*>(QStringLiteral("openProgrammerCalculatorBtn")));
    QTest::keyClick(&mw, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier); QApplication::processEvents();
    auto* pal = mw.findChild<CommandPaletteDialog*>(QStringLiteral("CommandPaletteDialog")); QVERIFY(pal && pal->isVisible());
    pal->setFilterText(QStringLiteral("> Programmer")); QVERIFY(pal->resultCount() > 0 && pal->resultItemAt(0).title.contains(QStringLiteral("Programmer's Calculator")));
    QTest::keyClick(pal->inputEdit(), Qt::Key_Return); QApplication::processEvents();
    auto* floatingCalc = mw.programmerCalculatorDialog(); QVERIFY(floatingCalc && floatingCalc->isVisible() && !floatingCalc->isModal() && (floatingCalc->windowFlags() & Qt::Window) && floatingCalc->keypadDigitButton(0)->height() >= 26);
    floatingCalc->close(); QTest::keyClick(&mw, Qt::Key_C, Qt::ControlModifier | Qt::AltModifier); QApplication::processEvents(); QVERIFY(floatingCalc->isVisible()); floatingCalc->close();
    QVERIFY(mw.sourceCodeView() && mw.variablesDockWidget() && mw.registerView() && mw.memView() && mw.disassemblyView());
    emit mw.sourceCodeView()->sendToCalculatorRequested(QStringLiteral("0x4011a6")); QApplication::processEvents();
    QVERIFY(floatingCalc->isVisible() && floatingCalc->value() == 0x4011A6ULL && floatingCalc->hexText() == QStringLiteral("0x4011A6") && floatingCalc->expressionEdit()->text() == QStringLiteral("0x4011A6"));
    emit mw.variablesDockWidget()->sendToCalculatorRequested(QStringLiteral("0x7fffffffe5a8")); QCOMPARE(floatingCalc->value(), 0x7FFFFFFFE5A8ULL); floatingCalc->close();
    for (const QString& w : s_calcWarnings) QVERIFY2(!w.contains(QStringLiteral("Ambiguous shortcut overload"), Qt::CaseInsensitive), qPrintable(w));
}
/** @brief Verifies PointerGraphDialog layered DAG layout, manual drag preservation, incremental multi-root pinning, and dynamic alias splitting/merging. */
static void testPointerGraphDagLayoutAndAliasSplitting() {
    using gridlock::ui::PointerGraphDialog; using F = PointerGraphDialog::StructFieldEntry;
    PointerGraphDialog pg; auto* layoutBtn = pg.findChild<QPushButton*>(QStringLiteral("pointerGraphLayoutBtn")); auto* clearBtn = pg.findChild<QPushButton*>(QStringLiteral("pointerGraphClearBtn")); auto* pruneChk = pg.findChild<QCheckBox*>(QStringLiteral("pointerGraphPruneCheck"));
    QVERIFY(layoutBtn && clearBtn && pruneChk && pruneChk->isChecked());
    pg.upsertStructNode(QStringLiteral("root"), QStringLiteral("Tree"), QStringLiteral("0x100"), {F{QStringLiteral("left"), QStringLiteral("0x200"), QStringLiteral("Node*")}, F{QStringLiteral("right"), QStringLiteral("0x210"), QStringLiteral("Node*")}});
    pg.upsertStructNode(QStringLiteral("child"), QStringLiteral("Node"), QStringLiteral("0x200"), {F{QStringLiteral("next"), QStringLiteral("0x300"), QStringLiteral("Node*")}}, QStringLiteral("root"), QStringLiteral("left"));
    pg.upsertStructNode(QStringLiteral("altChild"), QStringLiteral("Node"), QStringLiteral("0x210"), {F{QStringLiteral("val"), QStringLiteral("7"), QStringLiteral("int")}}, QStringLiteral("root"), QStringLiteral("right"));
    pg.upsertStructNode(QStringLiteral("grandchild"), QStringLiteral("Node"), QStringLiteral("0x300"), {F{QStringLiteral("val"), QStringLiteral("9"), QStringLiteral("int")}}, QStringLiteral("child"), QStringLiteral("next")); pg.autoLayoutNodes();
    QVERIFY(pg.nodeLayer(QStringLiteral("root")) == 0 && pg.nodeLayer(QStringLiteral("child")) == 1 && pg.nodeLayer(QStringLiteral("grandchild")) == 2 && pg.nodePosition(QStringLiteral("root")).x() < pg.nodePosition(QStringLiteral("child")).x() && pg.nodePosition(QStringLiteral("child")).x() < pg.nodePosition(QStringLiteral("grandchild")).x() && pg.nodePosition(QStringLiteral("child")).y() < pg.nodePosition(QStringLiteral("altChild")).y());
    const QPointF manualPos(555.0, 444.0); QVERIFY(pg.setNodePosition(QStringLiteral("child"), manualPos) && pg.isNodeManuallyPositioned(QStringLiteral("child")));
    pg.onTargetStopped(0); pg.upsertStructNode(QStringLiteral("child"), QStringLiteral("Node"), QStringLiteral("0x200"), {F{QStringLiteral("next"), QStringLiteral("0x300"), QStringLiteral("Node*")}, F{QStringLiteral("tag"), QStringLiteral("1"), QStringLiteral("int")}});
    pg.upsertStructNode(QStringLiteral("extra"), QStringLiteral("Node"), QStringLiteral("0x310"), {F{QStringLiteral("val"), QStringLiteral("3"), QStringLiteral("int")}}, QStringLiteral("child"), QStringLiteral("tag")); pg.autoLayoutNodes(false); QCOMPARE(pg.nodePosition(QStringLiteral("child")), manualPos);
    layoutBtn->click(); QVERIFY(!pg.isNodeManuallyPositioned(QStringLiteral("child")) && pg.nodePosition(QStringLiteral("root")).x() < pg.nodePosition(QStringLiteral("child")).x() && pg.nodePosition(QStringLiteral("child")).x() < pg.nodePosition(QStringLiteral("grandchild")).x());
    clearBtn->click(); QVERIFY(pg.nodeCount() == 0 && pg.edgeCount() == 0 && pg.pinnedRootExpressions().isEmpty());
    pg.addAndEvaluateRootExpression(QStringLiteral("headA"), QStringLiteral("Holder*"), QStringLiteral("0x100")); pg.upsertStructNode(QStringLiteral("headA"), QStringLiteral("Holder"), QStringLiteral("0x100"), {F{QStringLiteral("ptr"), QStringLiteral("0x1000"), QStringLiteral("Node*")}});
    pg.addAndEvaluateRootExpression(QStringLiteral("headB"), QStringLiteral("Holder*"), QStringLiteral("0x200")); pg.upsertStructNode(QStringLiteral("headB"), QStringLiteral("Holder"), QStringLiteral("0x200"), {F{QStringLiteral("ptr"), QStringLiteral("0x1000"), QStringLiteral("Node*")}});
    QVERIFY(pg.nodeCount() == 2 && pg.pinnedRootExpressions() == (QStringList{QStringLiteral("headA"), QStringLiteral("headB")}) && pg.nodePosition(QStringLiteral("headA")).y() != pg.nodePosition(QStringLiteral("headB")).y() && pg.unpinRootExpression(QStringLiteral("headB")) && pg.nodeCount() == 1 && pg.pinnedRootExpressions() == (QStringList{QStringLiteral("headA")}));
    pg.addAndEvaluateRootExpression(QStringLiteral("headB"), QStringLiteral("Holder*"), QStringLiteral("0x200")); pg.upsertStructNode(QStringLiteral("headB"), QStringLiteral("Holder"), QStringLiteral("0x200"), {F{QStringLiteral("ptr"), QStringLiteral("0x1000"), QStringLiteral("Node*")}});
    pg.upsertStructNode(QStringLiteral("n1"), QStringLiteral("Node"), QStringLiteral("0x1000"), {F{QStringLiteral("val"), QStringLiteral("10"), QStringLiteral("int")}}, QStringLiteral("headA"), QStringLiteral("ptr"));
    QVERIFY(pg.connectPointerField(QStringLiteral("headB"), QStringLiteral("ptr"), QStringLiteral("0x1000")) && pg.nodeCount() == 3 && pg.edgeCount() == 2 && pg.hasCycleOrSharedAlias());
    pg.onTargetStopped(0); pg.upsertStructNode(QStringLiteral("headB"), QStringLiteral("Holder"), QStringLiteral("0x200"), {F{QStringLiteral("ptr"), QStringLiteral("0x2000"), QStringLiteral("Node*")}}); pg.upsertStructNode(QStringLiteral("n2"), QStringLiteral("Node"), QStringLiteral("0x2000"), {F{QStringLiteral("val"), QStringLiteral("20"), QStringLiteral("int")}}, QStringLiteral("headB"), QStringLiteral("ptr"));
    QVERIFY(pg.nodeCount() == 4 && pg.edgeCount() == 2 && pg.edgeTargetAddressOrExpr(QStringLiteral("headA"), QStringLiteral("ptr")) == QStringLiteral("0x1000") && pg.edgeTargetAddressOrExpr(QStringLiteral("headB"), QStringLiteral("ptr")) == QStringLiteral("0x2000") && !pg.hasCycleOrSharedAlias() && pg.fieldMutationSummary(QStringLiteral("headB"), QStringLiteral("ptr")) == QStringLiteral("0x1000 -> 0x2000"));
    pg.onTargetStopped(0); pg.upsertStructNode(QStringLiteral("headA"), QStringLiteral("Holder"), QStringLiteral("0x100"), {F{QStringLiteral("ptr"), QStringLiteral("0x2000"), QStringLiteral("Node*")}});
    QVERIFY(pg.nodeCount() == 3 && pg.edgeCount() == 2 && pg.edgeTargetAddressOrExpr(QStringLiteral("headA"), QStringLiteral("ptr")) == QStringLiteral("0x2000") && pg.hasCycleOrSharedAlias()); clearBtn->click(); QVERIFY(pg.nodeCount() == 0 && pg.edgeCount() == 0);
}
/** @brief Verifies PointerGraphDialog wheel zoom, corner minimap, field suppression, horizontal orientation, /x /d /b overrides, and SVG/PNG export. */
static void testPointerGraphControlsAndExport() {
    using gridlock::ui::PointerGraphDialog; using F = PointerGraphDialog::StructFieldEntry;
    PointerGraphDialog pg; pg.resize(960, 620); pg.show(); QApplication::processEvents();
    QVERIFY(pg.findChild<QPushButton*>(QStringLiteral("pointerGraphExportSvgBtn")) && pg.findChild<QPushButton*>(QStringLiteral("pointerGraphExportPngBtn")) && pg.findChild<QPushButton*>(QStringLiteral("pointerGraphResetZoomBtn")) && pg.findChild<QPushButton*>(QStringLiteral("pointerGraphFitBtn")));
    pg.upsertStructNode(QStringLiteral("pkt"), QStringLiteral("Packet"), QStringLiteral("0x5000"), {F{QStringLiteral("magic"), QStringLiteral("42"), QStringLiteral("int")}, F{QStringLiteral("seq"), QStringLiteral("7"), QStringLiteral("int")}, F{QStringLiteral("crc"), QStringLiteral("255"), QStringLiteral("uint32_t")}, F{QStringLiteral("next"), QStringLiteral("0x5100"), QStringLiteral("Packet*")}});
    pg.upsertStructNode(QStringLiteral("peer"), QStringLiteral("Packet"), QStringLiteral("0x5100"), {F{QStringLiteral("magic"), QStringLiteral("99"), QStringLiteral("int")}}, QStringLiteral("pkt"), QStringLiteral("next")); QApplication::processEvents();
    QVERIFY(pg.visibleFieldCount(QStringLiteral("pkt")) == 4 && pg.hiddenFieldCount(QStringLiteral("pkt")) == 0 && pg.setNodeFieldHidden(QStringLiteral("pkt"), QStringLiteral("crc"), true) && pg.visibleFieldCount(QStringLiteral("pkt")) == 3 && pg.hiddenFieldCount(QStringLiteral("pkt")) == 1 && pg.hiddenFieldsBadgeText(QStringLiteral("pkt")).contains(QStringLiteral("(+1 hidden)")));
    pg.onTargetStopped(0); pg.upsertStructNode(QStringLiteral("pkt"), QStringLiteral("Packet"), QStringLiteral("0x5000"), {F{QStringLiteral("magic"), QStringLiteral("42"), QStringLiteral("int")}, F{QStringLiteral("seq"), QStringLiteral("8"), QStringLiteral("int")}, F{QStringLiteral("crc"), QStringLiteral("255"), QStringLiteral("uint32_t")}, F{QStringLiteral("next"), QStringLiteral("0x5100"), QStringLiteral("Packet*")}});
    QVERIFY(pg.hiddenFieldCount(QStringLiteral("pkt")) == 1 && pg.restoreAllHiddenFields(QStringLiteral("pkt")) && pg.visibleFieldCount(QStringLiteral("pkt")) == 4 && pg.hiddenFieldCount(QStringLiteral("pkt")) == 0 && !pg.isNodeHorizontal(QStringLiteral("pkt")) && pg.toggleNodeOrientation(QStringLiteral("pkt")) && pg.isNodeHorizontal(QStringLiteral("pkt")) && pg.nodeCardSize(QStringLiteral("pkt")).width() > pg.nodeCardSize(QStringLiteral("pkt")).height());
    QVERIFY(pg.setNodeFormatOverride(QStringLiteral("pkt"), QStringLiteral("/x")) && pg.fieldValue(QStringLiteral("pkt"), QStringLiteral("magic")) == QStringLiteral("0x2a") && pg.setNodeFormatOverride(QStringLiteral("pkt"), QStringLiteral("/b")) && pg.fieldValue(QStringLiteral("pkt"), QStringLiteral("magic")) == QStringLiteral("0b101010") && pg.setNodeFormatOverride(QStringLiteral("pkt"), QStringLiteral("/d")) && pg.fieldValue(QStringLiteral("pkt"), QStringLiteral("magic")) == QStringLiteral("42"));
    auto* gv = pg.findChild<QGraphicsView*>(QStringLiteral("pointerGraphView")); QVERIFY(gv != nullptr && !pg.minimapViewportRect().isEmpty());
    QWheelEvent wheelIn(QPointF(100, 100), gv->mapToGlobal(QPointF(100, 100)), QPoint(0, 0), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(gv->viewport(), &wheelIn); QVERIFY(pg.zoomFactor() > 1.05); pg.setZoomFactor(9.0); QVERIFY(std::abs(pg.zoomFactor() - 2.5) < 1e-3); pg.setZoomFactor(0.05); QVERIFY(std::abs(pg.zoomFactor() - 0.25) < 1e-3); pg.resetZoom(); QVERIFY(std::abs(pg.zoomFactor() - 1.0) < 1e-3);
    QTest::mouseClick(gv->viewport(), Qt::LeftButton, Qt::NoModifier, pg.minimapViewportRect().center().toPoint());
    QTemporaryDir expDir; QVERIFY(expDir.isValid()); const QString pngOut = expDir.filePath(QStringLiteral("graph.png")), svgOut = expDir.filePath(QStringLiteral("graph.svg"));
    QVERIFY(pg.exportGraphToPng(pngOut) && pg.exportGraphToSvg(svgOut) && QImage(pngOut).width() >= 400); QFile sf(svgOut); QVERIFY(sf.open(QIODevice::ReadOnly) && QString::fromUtf8(sf.readAll()).contains(QStringLiteral("<svg")));

    // Regression coverage: (1) un-elided per-row hover tooltips, (2) Shift+Click width expansion without neighbor overlap, (3) double-click pointer deref expansion.
    pg.clearGraph(); pg.resetZoom(); QApplication::processEvents();
    const QString longEnumVal = QStringLiteral("mfem::Operator::Complex_Hypre_ParCSR");
    const QString longEnumType = QStringLiteral("mfem::Operator::Type");
    const QString expectedRowTip = QStringLiteral("type_id : mfem::Operator::Complex_Hypre_ParCSR (mfem::Operator::Type)");
    pg.upsertStructNode(QStringLiteral("A"), QStringLiteral("mfem::HypreSmoother"), QStringLiteral("0x6000"),
                        {F{QStringLiteral("type_id"), longEnumVal, longEnumType},
                         F{QStringLiteral("oper"), QStringLiteral("0x6100"), QStringLiteral("mfem::Operator *")}});
    pg.upsertStructNode(QStringLiteral("A.oper"), QStringLiteral("mfem::Operator *"), QStringLiteral("0x6100"), {},
                        QStringLiteral("A"), QStringLiteral("oper"));
    pg.autoLayoutNodes(true); gv->centerOn(pg.nodePosition(QStringLiteral("A")) + QPointF(145.0, 40.0)); QApplication::processEvents();
    QCOMPARE(pg.nodeFieldTooltip(QStringLiteral("A"), QStringLiteral("type_id")), expectedRowTip);
    QCOMPARE(pg.nodeTooltipAt(QStringLiteral("A"), QPointF(24.0, 42.0)), expectedRowTip);
    QCOMPARE(pg.nodeTooltipAt(QStringLiteral("A"), QPointF(24.0, 14.0)), QStringLiteral("A : mfem::HypreSmoother @ 0x6000"));
    const QPoint aRowViewPt = gv->mapFromScene(pg.nodePosition(QStringLiteral("A")) + QPointF(40.0, 42.0));
    QTest::mouseMove(gv->viewport(), aRowViewPt); QApplication::processEvents();
    if (QGraphicsItem* hitItem = gv->itemAt(aRowViewPt)) QCOMPARE(hitItem->toolTip(), expectedRowTip);

    const qreal compactW = pg.nodeCardSize(QStringLiteral("A")).width();
    const qreal operBeforeX = pg.nodePosition(QStringLiteral("A.oper")).x();
    QVERIFY(!pg.isNodeExpandedWidth(QStringLiteral("A")) && std::abs(compactW - 290.0) < 1e-3);
    QTest::mouseClick(gv->viewport(), Qt::LeftButton, Qt::ShiftModifier, aRowViewPt); QApplication::processEvents();
    const qreal expandedW = pg.nodeCardSize(QStringLiteral("A")).width();
    const qreal operAfterX = pg.nodePosition(QStringLiteral("A.oper")).x();
    QVERIFY2(pg.isNodeExpandedWidth(QStringLiteral("A")) && expandedW > compactW + 80.0 && expandedW <= 900.0,
             qPrintable(QStringLiteral("isExpanded=%1 compactW=%2 expandedW=%3 viewPt=(%4,%5)")
                            .arg(pg.isNodeExpandedWidth(QStringLiteral("A"))).arg(compactW).arg(expandedW).arg(aRowViewPt.x()).arg(aRowViewPt.y())));
    QVERIFY(operAfterX > operBeforeX && operAfterX >= pg.nodePosition(QStringLiteral("A")).x() + expandedW + 30.0);
    const QPoint aCollapsedPt = gv->mapFromScene(pg.nodePosition(QStringLiteral("A")) + QPointF(40.0, 42.0));
    QTest::mouseClick(gv->viewport(), Qt::LeftButton, Qt::ShiftModifier, aCollapsedPt); QApplication::processEvents();
    QVERIFY(!pg.isNodeExpandedWidth(QStringLiteral("A")) && std::abs(pg.nodeCardSize(QStringLiteral("A")).width() - compactW) < 1e-3);

    gv->centerOn(pg.nodePosition(QStringLiteral("A.oper")) + QPointF(145.0, 30.0)); QApplication::processEvents();
    QCOMPARE(pg.nodePlaceholderText(QStringLiteral("A.oper")), QStringLiteral("(double-click or expand fields...)"));
    QSignalSpy expandSpy(&pg, &PointerGraphDialog::nodeExpansionRequested);
    const QPoint operBodyPt = gv->mapFromScene(pg.nodePosition(QStringLiteral("A.oper")) + QPointF(60.0, 42.0));
    QTest::mouseDClick(gv->viewport(), Qt::LeftButton, Qt::NoModifier, operBodyPt); QApplication::processEvents();
    QCOMPARE(expandSpy.count(), 1);
    QCOMPARE(expandSpy.first().at(0).toString(), QStringLiteral("A.oper"));
    QCOMPARE(expandSpy.first().at(1).toString(), QStringLiteral("*(A.oper)"));
    QCOMPARE(pg.nodePlaceholderText(QStringLiteral("A.oper")), QStringLiteral("(loading fields...)"));
    pg.onVariableChildrenUpdated(0, QStringLiteral("*(A.oper)"), {});
    QCOMPARE(pg.nodePlaceholderText(QStringLiteral("A.oper")), QStringLiteral("(no fields / opaque type)"));
    QTest::mouseDClick(gv->viewport(), Qt::LeftButton, Qt::NoModifier, operBodyPt); QApplication::processEvents();
    QCOMPARE(pg.nodePlaceholderText(QStringLiteral("A.oper")), QStringLiteral("(loading fields...)"));
    pg.onVariableChildrenUpdated(0, QStringLiteral("*(A.oper)"),
                                 {QVariantMap{{QStringLiteral("name"), QStringLiteral("height")}, {QStringLiteral("value"), QStringLiteral("1024")}, {QStringLiteral("type"), QStringLiteral("int")}},
                                  QVariantMap{{QStringLiteral("name"), QStringLiteral("width")}, {QStringLiteral("value"), QStringLiteral("1024")}, {QStringLiteral("type"), QStringLiteral("int")}}});
    QVERIFY(pg.nodeHasField(QStringLiteral("A.oper"), QStringLiteral("height")) && pg.fieldValue(QStringLiteral("A.oper"), QStringLiteral("height")) == QStringLiteral("1024") && pg.nodePlaceholderText(QStringLiteral("A.oper")).isEmpty());
    pg.upsertStructNode(QStringLiteral("nullOp"), QStringLiteral("mfem::Operator *"), QStringLiteral("0x0"), {});
    QVERIFY(pg.nodePlaceholderText(QStringLiteral("nullOp")) == QStringLiteral("(null pointer)") && !pg.expandNode(QStringLiteral("nullOp")) && pg.nodePlaceholderText(QStringLiteral("nullOp")) == QStringLiteral("(null pointer)"));
    pg.close();
}
/** @brief Verifies ArrayPlotDialog v0.8.8 multi-rank 1D overlay, start:end:stride slicing, Log10 scale, drag-box zoom, and Scalar History mode. */
static void testArrayPlotterScientificExtensions() {
    using gridlock::ui::ArrayPlotDialog; using gridlock::ui::parseArraySliceExpression; using gridlock::ui::parseRankSubset;
    const auto s1 = parseArraySliceExpression(QStringLiteral("arr[2:10:2]")), s2 = parseArraySliceExpression(QStringLiteral("u[16:128:2]")), s3 = parseArraySliceExpression(QStringLiteral("pressure_field[0..63]")), s4 = parseArraySliceExpression(QStringLiteral("&vec[0]"));
    QVERIFY(s1.hasSlice && s1.baseExpression == QStringLiteral("arr") && s1.offset == 2 && s1.count == 4 && s1.stride == 2 && s2.hasSlice && s2.offset == 16 && s2.count == 56 && s2.stride == 2 && s3.hasSlice && s3.offset == 0 && s3.count == 64 && s3.stride == 1 && !s4.hasSlice && s4.baseExpression == QStringLiteral("&vec[0]"));
    QCOMPARE(parseRankSubset(QStringLiteral("0..3"), 8), (QVector<int>{0, 1, 2, 3})); QCOMPARE(parseRankSubset(QStringLiteral("0, 2, 3"), 4), (QVector<int>{0, 2, 3}));
    gridlock::GdbRankCoordinator coord; coord.initializeMockSession(4); ArrayPlotDialog dlg(&coord); dlg.resize(980, 600); dlg.show(); QApplication::processEvents();
    auto* logChk = dlg.findChild<QCheckBox*>(QStringLiteral("arrayPlotLogScaleCheck")); auto* overlayChk = dlg.findChild<QCheckBox*>(QStringLiteral("arrayPlotOverlayAllRanksCheck")); auto* resetZoomBtn = dlg.findChild<QPushButton*>(QStringLiteral("arrayPlotResetZoomBtn")); auto* clearHistBtn = dlg.findChild<QPushButton*>(QStringLiteral("arrayPlotClearHistoryBtn"));
    QVERIFY(logChk && overlayChk && resetZoomBtn && clearHistBtn && dlg.findChild<QSpinBox*>(QStringLiteral("arrayPlotOffsetSpin")) && dlg.findChild<QSpinBox*>(QStringLiteral("arrayPlotStrideSpin")) && dlg.findChild<QLineEdit*>(QStringLiteral("arrayPlotRankSubsetEdit")));
    QVector<double> buf12(12); for (int i = 0; i < 12; ++i) buf12[i] = static_cast<double>(i); dlg.setExpression(QStringLiteral("arr[2:10:2]"));
    QVERIFY(dlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(buf12.constData()), buf12.size() * sizeof(double)), ArrayPlotDialog::ElementType::Float64) && dlg.currentSamples() == (QVector<double>{2.0, 4.0, 6.0, 8.0}) && dlg.sliceOffset() == 2 && dlg.sliceStride() == 2);
    QSignalSpy multiSpy(&dlg, &ArrayPlotDialog::multiRankPlotRequested); dlg.setExpression(QStringLiteral("halo[0:4]")); dlg.setRankSubsetSpec(QStringLiteral("0..3")); overlayChk->setChecked(true); QApplication::processEvents();
    QVERIFY(multiSpy.count() >= 1 && multiSpy.first().at(0).toString() == QStringLiteral("halo") && multiSpy.first().at(1).toInt() == 32);
    for (int r = 0; r < 4; ++r) { const double vals[4] = {r * 10.0 + 1.0, r * 10.0 + 2.0, r * 10.0 + 3.0, r * 10.0 + 4.0}; emit coord.memoryRead(r, QStringLiteral("&(halo)[0]"), QByteArray(reinterpret_cast<const char*>(vals), sizeof(vals))); }
    QVERIFY(dlg.rankOverlays().size() == 4 && dlg.rankOverlays().value(3).first() == 31.0 && !dlg.grab().isNull()); overlayChk->setChecked(false); QVERIFY(dlg.rankOverlays().isEmpty());
    dlg.setSliceOffset(0); dlg.setSliceStride(1); dlg.setSamples({1e-4, 1e-2, 1.0, 0.0, -5.0}, 1, 5); logChk->setChecked(true); QApplication::processEvents(); QVERIFY(dlg.isLogScale() && !dlg.grab().isNull());
    dlg.setSamples({1e-3, 1.0, 0.0, -2.0}, 2, 2); QApplication::processEvents(); QVERIFY(!dlg.grab().isNull()); logChk->setChecked(false);
    QVector<double> wave(64); for (int i = 0; i < 64; ++i) wave[i] = std::sin(i * 0.15) + 2.0; dlg.setPlotMode(ArrayPlotDialog::PlotMode::Line1D); dlg.setSamples(wave, 1, 64); QApplication::processEvents();
    auto* canvas = dlg.canvas(); QVERIFY(canvas != nullptr && !dlg.isZoomed() && !resetZoomBtn->isEnabled());
    const QRect pr = canvas->plotAreaRect(); const QPoint p0(pr.left() + pr.width() / 4, pr.top() + pr.height() / 4), p1(pr.left() + (pr.width() * 3) / 4, pr.top() + (pr.height() * 3) / 4);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, p0); QTest::mouseMove(canvas, p1); QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, p1); QApplication::processEvents();
    QVERIFY(dlg.isZoomed() && resetZoomBtn->isEnabled() && dlg.visibleXRange().first > 5 && dlg.visibleXRange().second < 58); resetZoomBtn->click(); QVERIFY(!dlg.isZoomed() && !resetZoomBtn->isEnabled() && dlg.visibleXRange() == (QPair<int, int>{0, 63}));
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, p0); QTest::mouseMove(canvas, p1); QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, p1); QVERIFY(dlg.isZoomed()); QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, pr.center()); QVERIFY(!dlg.isZoomed());
    dlg.setPlotMode(ArrayPlotDialog::PlotMode::ScalarHistory); dlg.setExpression(QStringLiteral("residual"));
    for (const QString& rv : {QStringLiteral("$1 = 1.0"), QStringLiteral("$2 = 0.1"), QStringLiteral("$3 = 0.01"), QStringLiteral("$4 = 0.001")}) { dlg.onTargetStopped(0); emit coord.expressionEvaluated(0, QStringLiteral("residual"), rv); }
    QVERIFY(dlg.scalarHistory().size() == 4 && dlg.currentSamples().size() == 4 && std::abs(dlg.scalarHistory().constLast() - 0.001) < 1e-12 && !dlg.grab().isNull()); clearHistBtn->click(); QVERIFY(dlg.scalarHistory().isEmpty() && dlg.currentSamples().isEmpty()); dlg.close();
}
/** @brief Verifies Complex64/Complex128 decoding, 2D splot/pm3d gnuplot script generation, and virtualized C/Fortran 2D/3D TensorSliceTableModel. */
static void testComplexAndTensorTableExtensions() {
    using gridlock::ui::ArrayPlotDialog; using gridlock::ui::TensorSliceTableModel;
    ArrayPlotDialog dlg; const float c64[2] = {3.0f, 4.0f}; const double c128[4] = {-1.0, 0.0, 1.0, std::numeric_limits<double>::quiet_NaN()};
    QVERIFY(dlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(c64), sizeof(c64)), ArrayPlotDialog::ElementType::Complex64, 1, 1) && std::abs(dlg.currentSamples().first() - 5.0) < 1e-6);
    dlg.setComplexComponent(ArrayPlotDialog::ComplexComponent::Phase); QVERIFY(std::abs(dlg.currentSamples().first() - std::atan2(4.0, 3.0)) < 1e-6);
    dlg.setComplexComponent(ArrayPlotDialog::ComplexComponent::Real); QCOMPARE(dlg.currentSamples().first(), 3.0); dlg.setComplexComponent(ArrayPlotDialog::ComplexComponent::Imaginary); QCOMPARE(dlg.currentSamples().first(), 4.0); dlg.setComplexComponent(ArrayPlotDialog::ComplexComponent::Magnitude);
    QVERIFY(dlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(c128), sizeof(c128)), ArrayPlotDialog::ElementType::Complex128, 1, 2) && std::abs(dlg.currentSamples().first() - 1.0) < 1e-12 && dlg.nanCount() == 1);
    dlg.setComplexComponent(ArrayPlotDialog::ComplexComponent::Phase); QVERIFY(std::abs(dlg.currentSamples().first() - std::acos(-1.0)) < 1e-12 && dlg.nanCount() == 1);
    dlg.setComplexComponent(ArrayPlotDialog::ComplexComponent::Real); QCOMPARE(dlg.currentSamples().first(), -1.0); dlg.setComplexComponent(ArrayPlotDialog::ComplexComponent::Imaginary); QCOMPARE(dlg.currentSamples().first(), 0.0);
    QVector<double> m32(32, 1.5); dlg.setSamples(m32, 4, 8); const QString gp2d = dlg.buildGnuplotScript(QStringLiteral("/tmp/m.dat")); QVERIFY(gp2d.contains(QStringLiteral("splot")) && gp2d.contains(QStringLiteral("with pm3d")) && gp2d.contains(QStringLiteral("matrix")));
    dlg.setSamples({1.0, 2.0, 3.0, 4.0}, 1, 4); const QString gp1d = dlg.buildGnuplotScript(QStringLiteral("/tmp/l.dat")); QVERIFY(gp1d.contains(QStringLiteral("plot '")) && !gp1d.contains(QStringLiteral("splot")));
    QVector<double> t12(12); for (int i = 0; i < 12; ++i) t12[i] = static_cast<double>(i); dlg.setTensorDimensions(3, 2, 2); dlg.setSamples(t12, 2, 3); dlg.setTensorTableActive(true); auto* tm = dlg.tensorTableModel(); QVERIFY(dlg.isTensorTableActive() && tm && tm->rowCount() == 2 && tm->columnCount() == 3);
    dlg.setMemoryOrder(TensorSliceTableModel::MemoryOrder::CRowMajor); dlg.setZSlice(0); QVERIFY(tm->cellValue(0, 2) == 2.0 && tm->cellValue(1, 0) == 3.0 && tm->data(tm->index(1, 0), Qt::ToolTipRole).toString().contains(QStringLiteral("[3]"))); dlg.setZSlice(1); QVERIFY(tm->cellValue(0, 0) == 6.0 && tm->cellValue(1, 2) == 11.0);
    dlg.setMemoryOrder(TensorSliceTableModel::MemoryOrder::FortranColMajor); dlg.setZSlice(0); QVERIFY(tm->cellValue(1, 0) == 1.0 && tm->cellValue(0, 1) == 2.0 && tm->cellValue(1, 2) == 5.0); dlg.setZSlice(1); QVERIFY(tm->cellValue(0, 1) == 8.0 && tm->cellValue(1, 2) == 11.0);
    QVector<double> big(120000); for (int i = 0; i < big.size(); ++i) big[i] = static_cast<double>(i); dlg.setTensorDimensions(400, 300, 1); dlg.setMemoryOrder(TensorSliceTableModel::MemoryOrder::CRowMajor); dlg.setSamples(big, 300, 400);
    QVERIFY(tm->rowCount() == 300 && tm->columnCount() == 400 && tm->cellValue(299, 399) == 119999.0);
}
/** @brief Verifies expression autocomplete in PointerGraphDialog/ArrayPlotDialog, SourceCodeView struct/pointer graph actions, underscore-free menu labels, dropdown tooltips, and editable Keybindings preferences. */
static void testAutocompleteContextGraphAndKeybindPreferences() {
    struct ShortcutResetGuard { ~ShortcutResetGuard() { gridlock::core::ShortcutManager::instance().resetAllToDefaults(true); } } resetGuard;
    gridlock::ui::MainWindow mw; mw.resize(1440, 900); mw.show(); QApplication::processEvents();
    mw.selectTargetByName(QStringLiteral("inspection_demo")); gridlock::core::managers::SessionManager::instance().addRecentSession(QStringLiteral("/tmp/mpi_test_workspace")); QApplication::processEvents();
    auto* sv = mw.sourceCodeView(); QVERIFY(sv != nullptr && sv->toPlainText().contains(QStringLiteral("struct DeeplyNested")));
    mw.openPointerGraphVisualizer(); auto* plotAct = mw.findChild<QAction*>(QStringLiteral("actionArrayPlotter")); QVERIFY(plotAct != nullptr); plotAct->trigger(); QApplication::processEvents();
    auto* pgDlg = mw.findChild<gridlock::ui::PointerGraphDialog*>(QStringLiteral("PointerGraphDialog")); auto* apDlg = mw.findChild<gridlock::ui::ArrayPlotDialog*>(QStringLiteral("ArrayPlotDialog")); QVERIFY(pgDlg && apDlg && pgDlg->expressionCompleter() && apDlg->expressionCompleter());
    auto matchesFor = [](QCompleter* comp, const QString& prefix) { comp->setCompletionPrefix(prefix); QStringList res; if (auto* m = comp->completionModel()) for (int r = 0; r < m->rowCount(); ++r) res.append(m->data(m->index(r, 0)).toString()); return res; };
    const QStringList pgDa = matchesFor(pgDlg->expressionCompleter(), QStringLiteral("da")), apDa = matchesFor(apDlg->expressionCompleter(), QStringLiteral("da"));
    QVERIFY(pgDa.contains(QStringLiteral("data")) && pgDa.contains(QStringLiteral("data[0]")) && apDa.contains(QStringLiteral("data")) && apDa.contains(QStringLiteral("data[0]")) && matchesFor(pgDlg->expressionCompleter(), QStringLiteral("Dee")).contains(QStringLiteral("DeeplyNested")));
    int structLine = -1, dataLine = -1;
    for (QTextBlock b = sv->document()->begin(); b.isValid(); b = b.next()) { if (structLine < 0 && b.text().contains(QStringLiteral("struct DeeplyNested"))) structLine = b.blockNumber() + 1; if (dataLine < 0 && b.text().contains(QStringLiteral("data[i].id"))) dataLine = b.blockNumber() + 1; }
    QVERIFY(structLine > 0 && dataLine > 0); QSignalSpy structSpy(sv, &gridlock::ui::SourceCodeView::graphStructTypeRequested); QSignalSpy exprSpy(sv, &gridlock::ui::SourceCodeView::graphExpressionRequested);
    QVERIFY(sv->triggerContextActionAtLine(structLine, QStringLiteral("Graph 'DeeplyNested' Struct Instances")) && structSpy.count() == 1 && structSpy.first().at(0).toString() == QStringLiteral("DeeplyNested") && pgDlg->pinnedRootExpressions().contains(QStringLiteral("data[0]")));
    QTextBlock db = sv->document()->findBlockByNumber(dataLine - 1); const int dPos = db.text().indexOf(QStringLiteral("data[i]")); QVERIFY(dPos >= 0);
    QTextCursor selCur(db); selCur.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, dPos); selCur.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 7); sv->setTextCursor(selCur);
    QVERIFY(sv->triggerContextActionAtLine(dataLine, QStringLiteral("Graph 'data[i]' in Pointer Graph")) && exprSpy.count() == 1 && exprSpy.first().at(0).toString() == QStringLiteral("data[i]"));
    auto checkMenus = [&](auto& self, QMenu* m) -> void { if (!m) return; emit m->aboutToShow(); for (QAction* a : m->actions()) { if (!a || a->isSeparator()) continue; QVERIFY2(!a->text().contains(QLatin1Char('_')), qPrintable(a->text())); if (a->menu()) self(self, a->menu()); } };
    for (QAction* top : mw.menuBar()->actions()) if (top && top->menu()) checkMenus(checkMenus, top->menu());
    for (QComboBox* cb : apDlg->findChildren<QComboBox*>()) for (int i = 0; i < cb->count(); ++i) QVERIFY(!cb->itemData(i, Qt::ToolTipRole).toString().isEmpty() && cb->itemText(i).size() <= 24);
    gridlock::ui::PreferencesDialog prefs(&mw); prefs.show(); QApplication::processEvents();
    QVERIFY(prefs.width() >= 880 && prefs.height() >= 620); auto* catList = prefs.findChild<QListWidget*>(QStringLiteral("preferencesCategoryList")); auto* clearBtn = prefs.findChild<QPushButton*>(QStringLiteral("keybindingsClearShortcutBtn")); auto* resetBtn = prefs.findChild<QPushButton*>(QStringLiteral("keybindingsResetDefaultsBtn")); auto* kbTbl = prefs.findChild<QTableWidget*>(QStringLiteral("keybindingsTable")); auto* stack = prefs.findChild<QStackedWidget*>(); QVERIFY(catList && stack && clearBtn && resetBtn && kbTbl);
    int kbRow = -1, hpcRow = -1; for (int i = 0; i < catList->count(); ++i) { if (catList->item(i)->text() == QStringLiteral("Keybindings")) kbRow = i; if (catList->item(i)->text() == QStringLiteral("HPC Integration")) hpcRow = i; catList->setCurrentRow(i); QApplication::processEvents(); if (auto* sa = qobject_cast<QScrollArea*>(stack->currentWidget())) QVERIFY2(sa->horizontalScrollBar()->maximum() == 0, qPrintable(QStringLiteral("Page %1 overflow: %2").arg(i).arg(sa->horizontalScrollBar()->maximum()))); }
    QVERIFY(kbRow > 0 && hpcRow > 0); catList->setCurrentRow(hpcRow); QApplication::processEvents(); if (auto* hpcPage = prefs.findChild<gridlock::ui::HpcIntegrationSettingsPage*>()) { for (auto* w : hpcPage->findChildren<QLineEdit*>()) if (!qobject_cast<QComboBox*>(w->parentWidget()) && !qobject_cast<QAbstractSpinBox*>(w->parentWidget())) QVERIFY2(w->width() >= 110, qPrintable(w->placeholderText())); for (auto* w : hpcPage->findChildren<QComboBox*>()) QVERIFY(w->width() >= 110); for (auto* w : hpcPage->findChildren<QSpinBox*>()) QVERIFY(w->width() >= 110); }
    catList->setCurrentRow(kbRow); QApplication::processEvents();
    QVERIFY(kbTbl->rowHeight(0) >= 38 && kbTbl->columnWidth(0) >= 115 && kbTbl->columnWidth(2) >= 200 && kbTbl->columnViewportPosition(2) >= kbTbl->columnViewportPosition(1) + kbTbl->columnWidth(1) && kbTbl->viewport()->findChildren<QKeySequenceEdit*>().isEmpty());
    // Verify zero "edit: editing failed" warnings when clicking non-editable cols & editing col 2, plus clamped geometry and duplicate conflict reassignment.
    static QStringList s_kbWarnings; s_kbWarnings.clear();
    const QtMessageHandler prevHandler = qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& msg) { if (msg.contains(QStringLiteral("edit: editing failed"), Qt::CaseInsensitive)) s_kbWarnings.append(msg); });
    int plotRow = -1, contRow = -1;
    for (int r = 0; r < kbTbl->rowCount(); ++r) { if (kbTbl->item(r, 1) && kbTbl->item(r, 1)->data(Qt::UserRole).toString() == QStringLiteral("actionArrayPlotter")) plotRow = r; if (kbTbl->item(r, 1) && kbTbl->item(r, 1)->data(Qt::UserRole).toString() == QStringLiteral("debug.continue")) contRow = r; }
    QVERIFY(plotRow >= 0 && contRow >= 0); emit kbTbl->cellClicked(plotRow, 0); emit kbTbl->cellClicked(plotRow, 1); QApplication::processEvents();
    kbTbl->scrollToItem(kbTbl->item(plotRow, 2)); kbTbl->setCurrentCell(plotRow, 2); emit kbTbl->cellClicked(plotRow, 2); emit kbTbl->cellClicked(plotRow, 2); QApplication::processEvents();
    auto* seqRec = kbTbl->viewport()->findChild<gridlock::ui::KeySequenceRecorderWidget*>(); const QRect cellRect = kbTbl->visualRect(kbTbl->model()->index(plotRow, 2));
    QVERIFY2(seqRec && seqRec->autoFillBackground() && seqRec->height() >= 30 && cellRect.contains(seqRec->geometry()), qPrintable(QStringLiteral("seqRec=%1 autoFill=%2 geom=(%3,%4 %5x%6) cell=(%7,%8 %9x%10)").arg(seqRec != nullptr).arg(seqRec && seqRec->autoFillBackground()).arg(seqRec ? seqRec->x() : -1).arg(seqRec ? seqRec->y() : -1).arg(seqRec ? seqRec->width() : -1).arg(seqRec ? seqRec->height() : -1).arg(cellRect.x()).arg(cellRect.y()).arg(cellRect.width()).arg(cellRect.height())));
    const QKeySequence customSeq(QStringLiteral("Ctrl+Alt+Y")); QTest::keyClick(seqRec, Qt::Key_Y, Qt::ControlModifier | Qt::AltModifier); QApplication::processEvents();
    QVERIFY(plotAct->shortcut() == customSeq && gridlock::core::ConfigManager::instance().customShortcuts().value(QStringLiteral("actionArrayPlotter")) == customSeq);
    // Re-open editor on plotRow and assign Continue's F5 shortcut via keyClick to verify conflict banner and clearing.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); emit kbTbl->cellClicked(plotRow, 2); QApplication::processEvents();
    auto* seqRec2 = kbTbl->viewport()->findChild<gridlock::ui::KeySequenceRecorderWidget*>(); QVERIFY(seqRec2 && seqRec2->isVisible()); seqRec2->setKeySequence(QKeySequence(Qt::Key_F5)); QApplication::processEvents();
    auto* conflictLbl = prefs.findChild<QLabel*>(QStringLiteral("keybindingsConflictLabel"));
    QVERIFY2(conflictLbl && conflictLbl->isVisible() && conflictLbl->text().contains(QStringLiteral("Continue")) && kbTbl->item(contRow, 2)->text() == QStringLiteral("Unassigned"), "Duplicate shortcut must display conflict banner and clear conflicting action");
    qInstallMessageHandler(prevHandler); QVERIFY2(s_kbWarnings.isEmpty(), qPrintable(s_kbWarnings.join(QStringLiteral("; "))));
    kbTbl->setCurrentCell(plotRow, 0); clearBtn->click(); QApplication::processEvents(); QVERIFY(plotAct->shortcut().isEmpty()); resetBtn->click(); QApplication::processEvents(); QVERIFY(plotAct->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_G) && gridlock::core::ConfigManager::instance().customShortcuts().isEmpty());
    // Verify Light-theme selected row contrast and active KeySequenceRecorderWidget on row 4 (Shift+F5).
    auto& tm = gridlock::core::managers::ThemeManager::instance(); const QString savedTheme = tm.activeThemeId();
    for (const QString& tid : tm.availableThemes()) if (!gridlock::core::managers::ThemeManager::paletteForTheme(tid).isDark) { tm.applyTheme(tid, false); break; }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QApplication::processEvents();
    const QColor selBg = kbTbl->palette().color(QPalette::Highlight), selFg = kbTbl->palette().color(QPalette::HighlightedText);
    QVERIFY2(std::abs(selBg.lightness() - selFg.lightness()) >= 110, "Light-theme selected row text must have crisp contrast against highlight background");
    kbTbl->setCurrentCell(4, 2); emit kbTbl->cellClicked(4, 2); QApplication::processEvents();
    auto* activeRec = kbTbl->viewport()->findChild<gridlock::ui::KeySequenceRecorderWidget*>();
    QVERIFY2(activeRec && activeRec->isVisible() && activeRec->autoFillBackground() && activeRec->height() >= 30 && kbTbl->visualRect(kbTbl->model()->index(4, 2)).contains(activeRec->geometry()), "Active KeySequenceRecorderWidget on row 4 col 2 must be opaque and strictly within cell bounds");
    if (const QString png = qEnvironmentVariable("GRIDLOCK_VERIFY_KEYBINDS_RECORDER_PNG"); !png.isEmpty()) prefs.grab().toImage().save(png);
    tm.applyTheme(savedTheme, false); prefs.close(); apDlg->close(); pgDlg->close();
}
/** @brief Verifies ArrayPlotDialog 1D/2D buffer decoding, stats, stop mutation diffing, CSV export, PointerGraphDialog persistent auto-updating roots, QSS disk cache, and lazy bottom tabs. */
void TestMainWindowUI::testArrayPlotterAndPersistentPointerGraph() {
    using gridlock::ui::ArrayPlotDialog; using gridlock::ui::PointerGraphDialog;
    ArrayPlotDialog plotDlg; QVERIFY(plotDlg.findChild<QLineEdit*>(QStringLiteral("arrayPlotExprEdit")) && plotDlg.findChild<QCheckBox*>(QStringLiteral("arrayPlotAutoRefreshCheck")) && plotDlg.isAutoRefreshEnabled());
    QVERIFY(!plotDlg.openInGnuplot(false) && plotDlg.statusText().contains(QStringLiteral("no plotted samples"), Qt::CaseInsensitive));
    const double d0[4] = {1.0, 2.0, 3.0, 4.0}, d1[4] = {1.0, 9.0, 3.0, std::numeric_limits<double>::quiet_NaN()}; const float f0[3] = {-2.5f, 0.0f, 5.5f}; const int32_t i0[4] = {10, 20, 30, 40};
    QVERIFY(plotDlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(d0), sizeof(d0)), ArrayPlotDialog::ElementType::Float64, 1, 4) && plotDlg.currentSamples().size() == 4 && plotDlg.minValue() == 1.0 && plotDlg.maxValue() == 4.0 && plotDlg.meanValue() == 2.5 && std::abs(plotDlg.l2Norm() - std::sqrt(30.0)) < 1e-9 && plotDlg.nanInfCount() == 0);
    QVERIFY(plotDlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(d1), sizeof(d1)), ArrayPlotDialog::ElementType::Float64, 2, 2) && plotDlg.plotMode() == ArrayPlotDialog::PlotMode::Heatmap2D && plotDlg.nanCount() == 1 && plotDlg.changedElementCount() == 2);
    QVERIFY(plotDlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(f0), sizeof(f0)), QStringLiteral("float"), 1, 3) && plotDlg.minValue() == -2.5 && plotDlg.maxValue() == 5.5 && plotDlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(i0), sizeof(i0)), QStringLiteral("int32_t"), 2, 2) && plotDlg.meanValue() == 25.0);
    QTemporaryDir tmpDir; QVERIFY(tmpDir.isValid()); const QString csvPath = tmpDir.filePath(QStringLiteral("field.csv")), pngPath = tmpDir.filePath(QStringLiteral("field.png"));
    QVERIFY(plotDlg.exportCsv(csvPath) && plotDlg.savePng(pngPath) && QFileInfo::exists(pngPath)); QFile cf(csvPath); QVERIFY(cf.open(QIODevice::ReadOnly)); const QString csvText = QString::fromUtf8(cf.readAll()); QVERIFY(csvText.contains(QStringLiteral("10")) && csvText.contains(QStringLiteral("40")));
    plotDlg.setGnuplotExecutableOverride(QStringLiteral("/nonexistent/gridlock_missing_gnuplot_bin")); QVERIFY(!plotDlg.openInGnuplot(false) && plotDlg.statusText().contains(QStringLiteral("not found"), Qt::CaseInsensitive));
    const QString badGp = tmpDir.filePath(QStringLiteral("mock_fail_gnuplot.sh")); { QFile gf(badGp); QVERIFY(gf.open(QIODevice::WriteOnly)); gf.write("#!/bin/sh\necho 'gnuplot syntax error: bad terminal' >&2\nexit 1\n"); gf.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner); }
    plotDlg.setGnuplotExecutableOverride(badGp); QVERIFY(plotDlg.openInGnuplot(false)); QTRY_VERIFY_WITH_TIMEOUT(plotDlg.statusText().contains(QStringLiteral("gnuplot syntax error")), 3000);
    PointerGraphDialog pgDlg; QVERIFY(pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphRefreshBtn")) && pgDlg.findChild<QCheckBox*>(QStringLiteral("pointerGraphAutoRefreshCheck")) && pgDlg.isAutoRefreshEnabled());
    pgDlg.pinRootExpression(QStringLiteral("head")); pgDlg.upsertStructNode(QStringLiteral("head"), QStringLiteral("ListNode"), QStringLiteral("0x7fff1000"), {{QStringLiteral("val"), QStringLiteral("42"), QStringLiteral("int")}, {QStringLiteral("next"), QStringLiteral("0x7fff2000"), QStringLiteral("ListNode*")}}); QVERIFY(pgDlg.pinnedRootExpressions() == QStringList{QStringLiteral("head")} && pgDlg.mutatedFieldCount() == 0);
    pgDlg.onTargetStopped(0); pgDlg.upsertStructNode(QStringLiteral("head"), QStringLiteral("ListNode"), QStringLiteral("0x7fff1000"), {{QStringLiteral("val"), QStringLiteral("99"), QStringLiteral("int")}, {QStringLiteral("next"), QStringLiteral("0x7fff2000"), QStringLiteral("ListNode*")}});
    QVERIFY(pgDlg.pinnedRootExpressions() == QStringList{QStringLiteral("head")} && pgDlg.isFieldMutated(QStringLiteral("head"), QStringLiteral("val")) && !pgDlg.isFieldMutated(QStringLiteral("head"), QStringLiteral("next")) && pgDlg.fieldMutationSummary(QStringLiteral("head"), QStringLiteral("val")) == QStringLiteral("42 -> 99") && pgDlg.mutatedFieldCount() == 1);
    testPointerGraphDagLayoutAndAliasSplitting(); testPointerGraphControlsAndExport(); testArrayPlotterScientificExtensions(); testComplexAndTensorTableExtensions(); testAutocompleteContextGraphAndKeybindPreferences();
    auto& tm = gridlock::core::ThemeManager::instance(); tm.applyTheme(QStringLiteral("gridlock_slate"), true); const QString qssCache = tm.cachedQssFilePath(QStringLiteral("gridlock_slate")); QVERIFY(QFileInfo::exists(qssCache) && QFileInfo(qssCache).size() > 100);
    QElapsedTimer startupTimer; startupTimer.start(); tm.initialize(); gridlock::ui::MainWindow mw; mw.resize(1280, 800); mw.show(); const qint64 startupMs = startupTimer.elapsed(); QVERIFY2(startupMs < 550, qPrintable(QStringLiteral("Startup took %1 ms").arg(startupMs)));
    QApplication::processEvents(); auto* bottomTabs = mw.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs")); auto* pm = static_cast<gridlock::ui::HpcConsoleWidget*>(mw.findChild<QWidget*>(QStringLiteral("HpcConsoleDock"))); auto* term = mw.findChild<gridlock::ui::TerminalDockWidget*>(); auto* ref = mw.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY(bottomTabs && pm && term && ref && !pm->isPackagesLoaded() && !term->isShellStarted() && !ref->isDocsetsInitialized());
    bottomTabs->setCurrentWidget(term); QApplication::processEvents(); bottomTabs->setCurrentWidget(ref); QApplication::processEvents(); bottomTabs->setCurrentWidget(pm); QApplication::processEvents(); QVERIFY(term->isShellStarted() && ref->isDocsetsInitialized() && pm->isPackagesLoaded());
    // Progressive disclosure & <=1180px toolbar audit across Source, Debug, Binary, and MPI (ranks=8) at 1280x800.
    auto* mainTb = mw.findChild<QToolBar*>(QStringLiteral("MainToolbar")); auto* navTb = mw.findChild<QToolBar*>(QStringLiteral("AddressNavToolbar"));
    auto* rackDock = mw.findChild<QDockWidget*>(QStringLiteral("ServerRackDock")); auto* rankSpin = mw.findChild<QSpinBox*>(QStringLiteral("toolbarRankSpin"));
    auto* targetSel = mw.findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector")); auto* scopeCombo = mw.findChild<QComboBox*>(QStringLiteral("toolbarExecutionScopeCombo")); auto* pauseAllBtn = mw.findChild<QToolButton*>(QStringLiteral("toolbarPauseAllRanksBtn"));
    auto* contBtn = mw.findChild<QToolButton*>(QStringLiteral("toolbarContinueBtn")); auto* launchOptBtn = mw.findChild<QToolButton*>(QStringLiteral("toolbarLaunchOptionsBtn"));
    QVERIFY(mainTb && navTb && rackDock && rankSpin && targetSel && scopeCombo && pauseAllBtn && contBtn && launchOptBtn);
    QVERIFY2(contBtn->toolButtonStyle() == Qt::ToolButtonTextBesideIcon && contBtn->text() == QStringLiteral("Cont") && launchOptBtn->toolButtonStyle() == Qt::ToolButtonTextBesideIcon && !launchOptBtn->text().trimmed().isEmpty() && launchOptBtn->text().contains(QStringLiteral("Config")), qPrintable(QStringLiteral("cont='%1' opt='%2'").arg(contBtn->text(), launchOptBtn->text())));
    QMenu* debugMenu = nullptr; for (QAction* ma : mw.menuBar()->actions()) if (ma->menu() && ma->menu()->title().remove(QLatin1Char('&')) == QLatin1String("Debug")) debugMenu = ma->menu();
    QAction* runMenuAct = nullptr; for (QAction* da : debugMenu ? debugMenu->actions() : QList<QAction*>()) { if (da->objectName() == QLatin1String("actionRunTarget") || da->text().contains(QStringLiteral("Run Target"))) runMenuAct = da; QVERIFY2(!da->isCheckable(), qPrintable(QStringLiteral("Non-toggle Debug menu action '%1' must not be checkable").arg(da->text()))); }
    QVERIFY(debugMenu != nullptr && runMenuAct != nullptr && !runMenuAct->isCheckable());
    auto* searchBtn = mw.findChild<QPushButton*>(QStringLiteral("toolbarSearchEverywhereBtn"));
    QVERIFY2(searchBtn && !searchBtn->icon().isNull() && searchBtn->width() >= 26 && searchBtn->height() >= 22, qPrintable(QStringLiteral("searchBtn=%1x%2 iconNull=%3").arg(searchBtn ? searchBtn->width() : -1).arg(searchBtn ? searchBtn->height() : -1).arg(!searchBtn || searchBtn->icon().isNull())));
    {
        struct SzGuard { ~SzGuard() { gridlock::core::ConfigManager::instance().setSearchPopupSize(QStringLiteral("large")); } } szGuard;
        gridlock::core::ConfigManager::instance().setSearchPopupSize(QStringLiteral("large"));
        searchBtn->click(); QApplication::processEvents();
        auto* palDlg = mw.findChild<gridlock::ui::CommandPaletteDialog*>(QStringLiteral("CommandPaletteDialog"));
        QVERIFY(palDlg && palDlg->isVisible() && palDlg->headerBar() && palDlg->inputEdit() && palDlg->sizePresetButton());
        const int hdrBot = palDlg->headerBar()->geometry().bottom();
        const int inpTop = palDlg->inputEdit()->geometry().top();
        QVERIFY2(inpTop - hdrBot >= 6, qPrintable(QStringLiteral("hdrBot=%1 inpTop=%2 gap=%3").arg(hdrBot).arg(inpTop).arg(inpTop - hdrBot)));
        auto& cfg = gridlock::core::ConfigManager::instance();
        cfg.setSearchPopupSize(QStringLiteral("small")); QApplication::processEvents(); const QSize szS = palDlg->size(); QCOMPARE(palDlg->sizePresetButton()->text(), QStringLiteral("S"));
        cfg.setSearchPopupSize(QStringLiteral("medium")); QApplication::processEvents(); const QSize szM = palDlg->size(); QCOMPARE(palDlg->sizePresetButton()->text(), QStringLiteral("M"));
        cfg.setSearchPopupSize(QStringLiteral("large")); QApplication::processEvents(); const QSize szL = palDlg->size(); QCOMPARE(palDlg->sizePresetButton()->text(), QStringLiteral("L"));
        cfg.setSearchPopupSize(QStringLiteral("xlarge")); QApplication::processEvents(); const QSize szXL = palDlg->size(); QCOMPARE(palDlg->sizePresetButton()->text(), QStringLiteral("XL"));
        QVERIFY2(szS.width() < szM.width() && szM.width() < szL.width() && szL.width() < szXL.width() && szS.height() < szM.height() && szM.height() < szL.height() && szL.height() < szXL.height(), qPrintable(QStringLiteral("S=%1x%2 M=%3x%4 L=%5x%6 XL=%7x%8").arg(szS.width()).arg(szS.height()).arg(szM.width()).arg(szM.height()).arg(szL.width()).arg(szL.height()).arg(szXL.width()).arg(szXL.height())));
        palDlg->sizePresetButton()->click(); QApplication::processEvents(); QCOMPARE(cfg.searchPopupSize(), QStringLiteral("small"));
        gridlock::ui::PreferencesDialog szPrefs(&mw);
        auto* szCombo = szPrefs.findChild<QComboBox*>(QStringLiteral("searchPopupSizeCombo"));
        QVERIFY(szCombo && szCombo->count() == 4 && szCombo->currentData().toString() == QStringLiteral("small"));
        palDlg->reject(); QApplication::processEvents();
    }
    rankSpin->setValue(1); mw.setWorkspacePerspective(QStringLiteral("Source")); QApplication::processEvents();
    auto* extBtn = mainTb->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"));
    QVERIFY2(!navTb->isVisible() && !mw.disassemblyView()->isVisible() && !rackDock->isVisible() && !scopeCombo->isVisible() && !pauseAllBtn->isVisible() && targetSel->width() >= 165 && rankSpin->width() >= 80 && scopeCombo->minimumWidth() >= 115 && mainTb->sizeHint().width() <= 1180 && (!extBtn || !extBtn->isVisible()), qPrintable(QStringLiteral("src w=%1 ts=%2 rs=%3 scMin=%4 ext=%5").arg(mainTb->sizeHint().width()).arg(targetSel->width()).arg(rankSpin->width()).arg(scopeCombo->minimumWidth()).arg(extBtn && extBtn->isVisible())));
    const QImage tbSrc1280 = mainTb->grab().toImage();
    mw.setWorkspacePerspective(QStringLiteral("Debug")); QApplication::processEvents(); QVERIFY(!navTb->isVisible() && !mw.disassemblyView()->isVisible() && !rackDock->isVisible() && !scopeCombo->isVisible() && mainTb->sizeHint().width() <= 1180);
    mw.setWorkspacePerspective(QStringLiteral("Binary")); QApplication::processEvents(); QVERIFY2(navTb->isVisible() && mw.disassemblyView()->isVisible() && !rackDock->isVisible() && !scopeCombo->isVisible() && mainTb->sizeHint().width() <= 1180, qPrintable(QStringLiteral("bin w=%1").arg(mainTb->sizeHint().width())));
    rankSpin->setValue(8); mw.setWorkspacePerspective(QStringLiteral("MPI")); QApplication::processEvents();
    QVERIFY2(!navTb->isVisible() && !mw.disassemblyView()->isVisible() && rackDock->isVisible() && scopeCombo->isVisible() && pauseAllBtn->isVisible() && targetSel->width() >= 165 && rankSpin->width() >= 80 && scopeCombo->width() >= 115 && mainTb->sizeHint().width() <= 1180 && (!extBtn || !extBtn->isVisible()), qPrintable(QStringLiteral("mpi w=%1 ts=%2 rs=%3 sc=%4 ext=%5").arg(mainTb->sizeHint().width()).arg(targetSel->width()).arg(rankSpin->width()).arg(scopeCombo->width()).arg(extBtn && extBtn->isVisible())));
    if (const QString tbPng = qEnvironmentVariable("GRIDLOCK_VERIFY_TOOLBAR_PNG"); !tbPng.isEmpty()) {
        const QImage tbMpi1280 = mainTb->grab().toImage(); mw.resize(1600, 900); QApplication::processEvents(); const QImage tbMpi1600 = mainTb->grab().toImage();
        QImage out(1600, tbSrc1280.height() + tbMpi1280.height() + tbMpi1600.height() + 24, QImage::Format_ARGB32_Premultiplied); out.fill(QColor(15, 23, 42)); QPainter p(&out); p.drawImage(0, 4, tbSrc1280); p.drawImage(0, tbSrc1280.height() + 12, tbMpi1280); p.drawImage(0, tbSrc1280.height() + tbMpi1280.height() + 20, tbMpi1600); p.end(); out.save(tbPng);
    }
    {
        auto& cfg = gridlock::core::ConfigManager::instance();
        struct TbLayoutGuard { ~TbLayoutGuard() { gridlock::core::ConfigManager::instance().setToolbarLayout(gridlock::core::ConfigManager::defaultToolbarLayout()); } } tbGuard;
        QCOMPARE(cfg.defaultToolbarLayout(), (QStringList{QStringLiteral("target_selector"), QStringLiteral("config_menu"), QStringLiteral("rank_spin"), QStringLiteral("separator"), QStringLiteral("build"), QStringLiteral("run"), QStringLiteral("continue"), QStringLiteral("step_over"), QStringLiteral("step_into"), QStringLiteral("step_out"), QStringLiteral("pause"), QStringLiteral("stop"), QStringLiteral("record"), QStringLiteral("spacer"), QStringLiteral("search_everywhere"), QStringLiteral("perspectives")}));
        QVERIFY(targetSel->x() < launchOptBtn->x() && launchOptBtn->x() < rankSpin->x() && rankSpin->x() < searchBtn->x());
        cfg.setToolbarLayout({QStringLiteral("search_everywhere"), QStringLiteral("spacer"), QStringLiteral("run"), QStringLiteral("continue"), QStringLiteral("target_selector"), QStringLiteral("rank_spin"), QStringLiteral("perspectives")});
        QApplication::processEvents();
        QVERIFY2(searchBtn->isVisible() && targetSel->isVisible() && !launchOptBtn->isVisible() && searchBtn->x() < targetSel->x() && mw.findChild<QToolButton*>(QStringLiteral("toolbarLaunchOptionsBtn")) != nullptr && mw.findChild<QToolButton*>(QStringLiteral("toolbarContinueBtn")) != nullptr && mainTb->findChild<QWidget*>(QStringLiteral("toolbarFlexSpacer")) != nullptr, qPrintable(QStringLiteral("reordered sVis=%1 tVis=%2 optVis=%3 sX=%4 tX=%5 cont=%6 sp=%7").arg(searchBtn->isVisible()).arg(targetSel->isVisible()).arg(launchOptBtn->isVisible()).arg(searchBtn->x()).arg(targetSel->x()).arg(mw.findChild<QToolButton*>(QStringLiteral("toolbarContinueBtn")) != nullptr).arg(mainTb->findChild<QWidget*>(QStringLiteral("toolbarFlexSpacer")) != nullptr)));
        gridlock::ui::PreferencesDialog tbPrefs(&mw);
        auto* tbList = tbPrefs.findChild<QListWidget*>(QStringLiteral("toolbarLayoutList"));
        auto* hiddenCombo = tbPrefs.findChild<QComboBox*>(QStringLiteral("toolbarHiddenControlsCombo"));
        auto* resetTbBtn = tbPrefs.findChild<QPushButton*>(QStringLiteral("toolbarResetDefaultBtn"));
        auto* bb = tbPrefs.findChild<QDialogButtonBox*>();
        QVERIFY(tbList && hiddenCombo && resetTbBtn && bb && hiddenCombo->isEnabled() && hiddenCombo->findData(QStringLiteral("config_menu")) >= 0);
        resetTbBtn->click();
        bb->button(QDialogButtonBox::Apply)->click();
        QApplication::processEvents();
        QCOMPARE(cfg.toolbarLayout(), cfg.defaultToolbarLayout());
        QVERIFY2(launchOptBtn->isVisible() && targetSel->x() < launchOptBtn->x() && launchOptBtn->x() < searchBtn->x(), qPrintable(QStringLiteral("reset targetX=%1 optX=%2 searchX=%3").arg(targetSel->x()).arg(launchOptBtn->x()).arg(searchBtn->x())));
    }
    // Performance regression: 5+ MB (~100,000-row) CSV must open via virtualized CsvTablePreviewWidget in < 1000 ms.
    const QString bigCsv = tmpDir.filePath(QStringLiteral("telemetry_5mb.csv"));
    { QFile cf(bigCsv); QVERIFY(cf.open(QIODevice::WriteOnly)); QByteArray chunk; chunk.reserve(6 * 1024 * 1024); chunk.append("rank,step,pressure,velocity,tag\n"); for (int r = 0; r < 100000; ++r) { chunk.append(QByteArray::number(r)); chunk.append(",42,101325.125000,340.500000,\"halo,cell_partition_"); chunk.append(QByteArray::number(r % 1000)); chunk.append("\"\n"); } QVERIFY(cf.write(chunk) > 5 * 1024 * 1024); }
    gridlock::ui::EditorTabManager csvTabs; csvTabs.resize(900, 600); csvTabs.show(); QElapsedTimer csvTimer; csvTimer.start();
    QVERIFY(csvTabs.openFile(bigCsv) == nullptr); QApplication::processEvents(); const qint64 csvMs = csvTimer.elapsed(); QVERIFY2(csvMs < 1000, qPrintable(QStringLiteral("Opening 5 MB CSV took %1 ms").arg(csvMs)));
    auto* csvPreview = qobject_cast<gridlock::ui::CsvTablePreviewWidget*>(csvTabs.currentWidget()); QVERIFY(csvPreview && csvPreview->rowCount() == 100000 && csvPreview->columnCount() == 5);
    auto* csvModel = csvPreview->model(); QVERIFY(csvModel->headerData(0, Qt::Horizontal).toString() == QStringLiteral("rank") && csvModel->headerData(4, Qt::Horizontal).toString() == QStringLiteral("tag") && csvModel->data(csvModel->index(0, 0)).toString() == QStringLiteral("0") && csvModel->data(csvModel->index(0, 4)).toString() == QStringLiteral("halo,cell_partition_0") && csvModel->data(csvModel->index(99999, 0)).toString() == QStringLiteral("99999"));
    csvPreview->rawTextButton()->click(); QApplication::processEvents(); auto* rawView = qobject_cast<gridlock::ui::SourceCodeView*>(csvTabs.currentWidget()); QVERIFY(rawView && rawView->isLargeFileMode() && rawView->lineWrapMode() == QPlainTextEdit::NoWrap && rawView->toPlainText().contains(QStringLiteral("Truncated at")));
    const QString bigLog = tmpDir.filePath(QStringLiteral("solver_600kb.log")); { QFile lf(bigLog); QVERIFY(lf.open(QIODevice::WriteOnly)); lf.write(QByteArray(600 * 1024, 'A')); }
    auto* logView = csvTabs.openFile(bigLog); QVERIFY(logView && logView->isLargeFileMode() && logView->lineWrapMode() == QPlainTextEdit::NoWrap); csvTabs.close();
}
/** @brief Verifies GdbConsoleWidget persistent gdb_history, Up/Down draft preservation, Ctrl+R reverse search, and Tab completion popup. */
void TestMainWindowUI::testGdbConsoleReadlineErgonomics() {
    QTemporaryDir tmpDir; QVERIFY(tmpDir.isValid()); gridlock::core::ConfigManager::instance().setGdbHistoryFilePathOverride(tmpDir.filePath(QStringLiteral("gdb_history")));
    struct HistGuard { ~HistGuard() { gridlock::core::ConfigManager::instance().setGdbHistoryFilePathOverride(QString()); } } guard;
    gridlock::ui::GdbConsoleWidget console; console.resize(640, 320); console.show(); QApplication::processEvents(); auto* edit = console.commandEdit(); QVERIFY(edit != nullptr);
    for (const QString& cmd : {QStringLiteral("info registers"), QStringLiteral("break main"), QStringLiteral("x/16gx $rsp"), QStringLiteral("break main")}) { edit->setText(cmd); QTest::keyClick(edit, Qt::Key_Return); }
    QCOMPARE(console.commandHistory(), (QStringList{QStringLiteral("info registers"), QStringLiteral("x/16gx $rsp"), QStringLiteral("break main")}));
    edit->setText(QStringLiteral("print draft_val")); QTest::keyClick(edit, Qt::Key_Up); QCOMPARE(edit->text(), QStringLiteral("break main")); QTest::keyClick(edit, Qt::Key_Up); QCOMPARE(edit->text(), QStringLiteral("x/16gx $rsp"));
    QTest::keyClick(edit, Qt::Key_Down); QCOMPARE(edit->text(), QStringLiteral("break main")); QTest::keyClick(edit, Qt::Key_Down); QCOMPARE(edit->text(), QStringLiteral("print draft_val"));
    gridlock::ui::GdbConsoleWidget reloadedConsole; QCOMPARE(reloadedConsole.commandHistory(), console.commandHistory());
    edit->setText(QStringLiteral("initial_input")); QTest::keyClick(edit, Qt::Key_R, Qt::ControlModifier); QVERIFY(console.isReverseSearchActive() && console.reverseSearchLabel() && console.reverseSearchLabel()->isVisible());
    QTest::keyClicks(edit, QStringLiteral("in")); QVERIFY(console.reverseSearchQuery() == QStringLiteral("in") && edit->text() == QStringLiteral("break main"));
    QTest::keyClick(edit, Qt::Key_R, Qt::ControlModifier); QCOMPARE(edit->text(), QStringLiteral("info registers")); QTest::keyClick(edit, Qt::Key_Escape); QVERIFY(!console.isReverseSearchActive() && !console.reverseSearchLabel()->isVisible() && edit->text() == QStringLiteral("initial_input"));
    edit->setText(QStringLiteral("disas")); edit->setCursorPosition(5); QTest::keyClick(edit, Qt::Key_Tab); QCOMPARE(edit->text(), QStringLiteral("disassemble"));
    edit->setText(QStringLiteral("info ")); edit->setCursorPosition(5); QTest::keyClick(edit, Qt::Key_Tab); QVERIFY(console.completionPopup() && console.completionPopup()->isVisible() && console.completionPopup()->count() >= 4); QTest::keyClick(edit, Qt::Key_Escape); QVERIFY(!console.completionPopup()->isVisible());
    gridlock::GdbRankCoordinator coord; coord.initializeMockSession(1); coord.processGdbOutput(0, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401000\",func=\"main\",fullname=\"/tmp/main.cpp\",line=\"10\",args=[]}\n"));
    connect(&console, &gridlock::ui::GdbConsoleWidget::completionRequested, &coord, &gridlock::GdbRankCoordinator::requestConsoleCompletions); connect(&coord, &IBackendCoordinator::consoleCompletionsReady, &console, &gridlock::ui::GdbConsoleWidget::onConsoleCompletionsReady);
    edit->setText(QStringLiteral("b ma")); edit->setCursorPosition(4); QTest::keyClick(edit, Qt::Key_Tab); coord.processGdbOutput(0, QStringLiteral("~\"b main\\n\"\n~\"b malloc\\n\"\n211^done\n"));
    QVERIFY(console.completionPopup()->isVisible() && console.completionPopup()->count() == 2); QTest::keyClick(edit, Qt::Key_Down); QTest::keyClick(edit, Qt::Key_Return); QVERIFY(!console.completionPopup()->isVisible() && edit->text() == QStringLiteral("b malloc"));
}
