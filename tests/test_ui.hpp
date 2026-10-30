/**
 * @file test_ui.hpp
 * @brief End-to-end integration test suite for GridLock UI workflows and subsystems.
 *
 * Verifies main window docking layout, editor margins, CFG minimap overlays,
 * rank occupancy badges, decompiler syncing, and repository-wide code style audit.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestMainWindowUI
 * @brief Comprehensive UI integration test harness for MainWindow and all child views.
 */
class TestMainWindowUI : public QObject {
    Q_OBJECT

private slots:
    /** @brief Verifies source file loading into the editor. */
    void testSourceFileLoading();

    /** @brief Verifies main window layout, splitters, and dock hierarchy. */
    void testMainLayoutStructure();

    /** @brief Verifies text editor viewport margin geometry. */
    void testSourceCodeViewportMargins();

    /** @brief Verifies ServerRackView telemetry card updates. */
    void testServerRackStateUpdate();

    /** @brief Verifies assembly syntax highlighting token coloring. */
    void testAsmSyntaxHighlighter();

    /** @brief Verifies source code line highlighting bounds safety. */
    void testSourceCodeHighlightBounds();

    /** @brief Verifies differential memory grid cell expansion. */
    void testDifferentialGridExpansion();

    /** @brief Verifies differential memory grid signal emissions. */
    void testDifferentialGridEmits();

    /** @brief Verifies GDB console log filtering and output formatting. */
    void testGdbConsoleFiltering();

    /** @brief Verifies gutter breakpoint click propagation. */
    void testGutterBreakpointPropagation();

    /** @brief Verifies Nerd Font icon font family loading. */
    void testNerdFontLoading();

    /** @brief Verifies MIME type and filename icon resolution. */
    void testFileIconProvider();

    /** @brief Verifies cutter dock density and headless snapshot output. */
    void testCutterDockDensityAndSnapshot();

    /** @brief Verifies interactive tutorial breakpoint insertion. */
    void testTutorialBreakpointInjection();

    /** @brief Verifies memory zone grouping and seek bar synchronization. */
    void testCutterZoneGroupingAndSeekBar();

    /** @brief Verifies CFG basic-block layout and branch edge drawing. */
    void testCutterDisassemblyCfgAndBranchFlow();

    /** @brief Verifies hexdump difference tracking and register seek sync. */
    void testCutterHexdumpDiffAndRegisterSeekSync();

    /** @brief Verifies symbol tree sidebar and address space nav ribbon. */
    void testSymbolSidebarAndNavRibbon();

    /** @brief Verifies file line limits and length checks. */
    void testSourceFileLineLimits();

    /** @brief Verifies dock resizability constraints and xref decompiler sync. */
    void testDockResizabilityAndXrefDecompiler();

    /** @brief Verifies memory view toolbar layout without text truncation. */
    void testMemViewToolbarNoTruncationAndDisassemblyLayout();

    /** @brief Verifies offscreen launch mode and preferred screen selection. */
    void testOffscreenDefaultAndPreferredScreenRouting();

    /** @brief Verifies address history back/forward navigation and bookmarks. */
    void testNavigationHistoryStringsAndBookmarks();

    /** @brief Verifies multi-language decompiler and source code fallback. */
    void testCfgGraphDecompilerLanguagesAndSourceFallback();

    /** @brief Verifies unified dark slate chrome metrics and split layout. */
    void testUnifiedChromeMetricsAndSplitGraphDecompiler();

    /** @brief Verifies cross-pane choreography history and string search. */
    void testCrossPaneChoreographyHistoryAndStrings();

    /** @brief Verifies CFG rank divergence badges, minimap, and rack sync. */
    void testCfgRankDivergenceMinimapAndRackSync();

    /** @brief Verifies dynamic rank telemetry without constructor mocks. */
    void testDynamicRankSyncWithoutHardcodedConstructorMocks();

    /** @brief Audits entire repository for Google C++ Style and documentation. */
    void testFullRepoGoogleStyleAndDocumentation();

    /** @brief Verifies native ELF64 binary parsing, symbols, .rodata strings, and target loading. */
    void testElfBinaryInspectorAndStaticTargetLoad();

    /** @brief Verifies interactive Xref extraction, incoming/outgoing call graph analysis, and inspector dialog sync. */
    void testInteractiveXrefInspectorAndCallGraphSync();

    /** @brief Verifies terminal monospace font enforcement, memory/disassembly live patching, and deadlock cycle badges. */
    void testTerminalFontIsolationLivePatchingAndDeadlockCycle();

    /** @brief Verifies patched ELF export to disk, unified multi-domain workspace search, and deadlock propagation. */
    void testPatchedElfExportWorkspaceSearchAndDeadlockPropagation();

    /** @brief Verifies workstation UI cohesion across all bottom tabs, F1 docset lookup, and tab audit snapshots. */
    void testWorkstationUiCohesionAndIntegratedDocViewer();

    /** @brief Verifies dark-slate visual cohesion across modal dialogs, category sidebars, and table/tree headers. */
    void testDialogsAndTablesVisualCohesion();

    /** @brief Verifies cross-pane workflow bridges (Evaluator/Variables -> Watch, Disassembly context actions -> Reference Manual) and DisassemblyAnalysis line count. */
    void testCrossPaneWorkflowBridgesAndContextActions();

    /** @brief Verifies Zeal docset UI integration, unclipped CFG basic-block comments, and MpiDiagnosticsWidget rank wait-state table. */
    void testZealUiIntegrationCfgUnclippedAndMpiDiagnosticsTable();

    /** @brief Verifies SourceCodeView uniform canvas without right-half grey band, structured MpiNetworkLogWidget 6-column table, single-row ReferenceManualWidget header, and monochrome toolbar glyphs. */
    void testSourceEditorCanvasNetworkLogTableAndHeaderDensity();

    /** @brief Verifies DifferentialGrid uniform empty viewport without vertical header grey block, stretched watch columns, and GDB Console / Evaluator prompt bar cohesion. */
    void testWatchGridBackgroundAndConsolePromptCohesion();

    /** @brief Verifies toolbar target/rank controls, state-aware debugger action enablement, and synchronized Run -> Step -> Register/Source/Rack -> Stop workflow. */
    void testEndToEndStepDebuggingWorkflowAndToolbarState();

    /** @brief Verifies bidirectional SourceCodeView <-> DisassemblyView line/address sync and Code/Reverse/MPI workspace perspective switching. */
    void testSourceToAssemblySyncAndWorkspacePerspectives();

    /** @brief Verifies breakpoint-aware Run Target / Continue execution, granular source-to-RIP line mapping, and CFG active instruction markers. */
    void testBreakpointAwareRunContinueAndActiveCfgInstructionMarker();

    /** @brief Verifies dynamic statement-based source line mapping, loop back-edge/branch control-flow stepping, and active rank context. */
    void testDynamicSourceMappingLoopBranchSteppingAndRankContext();

    /** @brief Verifies Seek in Memory View tab switching, base address update, and mnemonic F1 doc lookup. */
    void testSeekInMemoryViewAndMnemonicDocLookup();

    /** @brief Verifies smart Meson/CMake/Make/SingleFileMPI build system detection and Terminal build/clean/rebuild workflow. */
    void testSmartBuildSystemDetectionAndTerminalBuildCleanWorkflow();

    /** @brief Verifies TerminalDockWidget eventFilter font/geometry lock during PreferencesDialog, Light Mode contrast, and ReferenceManualWidget docset auto-preview. */
    void testTerminalDialogEventFilterLightModeContrastAndDocsetAutoDetect();

    /** @brief Verifies selecting a target in #toolbarTargetSelector switches SourceCodeView, DisassemblyView, and Build/Clean target paths. */
    void testTargetSelectorSwitchesSourceDisassemblyAndBuildCommands();

    /** @brief Verifies Edit menu actions, dynamic UI/Code font scaling across QSS and editors, and 6 curated themes with human-readable titles. */
    void testEditMenuActionsDynamicFontScalingAndCuratedThemes();

    /** @brief Verifies two-lane vector gutter geometry, distinct breakpoint/conditional/PC marker states, and non-overlapping antialiased rendering. */
    void testModernVectorGutterIconsAndCombinedBreakpointPcState();

    /** @brief Verifies on-demand real ELF compilation, DWARF line mapping, breakpoint-first Run/Continue, and latched toolbar state. */
    void testRealCompiledTargetDwarfMappingBreakpointRunAndLatchedToolbar();

    /** @brief Verifies gutter hover ghost preview tracking and cursor-anchored breakpoint popover controls/positioning. */
    void testAnchoredBreakpointPopoverAndGutterHover();

    /** @brief Verifies left tool-window icon rail, linked contextual tab filtering, auto-group sync, and compact MPI Diagnostics header. */
    void testBottomToolWindowRailAndLinkedTabGroups();

    /** @brief Verifies gutter QContextMenuEvent isolation from editor viewport menu and clean rollback of canceled breakpoints. */
    void testGutterRightClickSuppressesEditorContextMenuAndCancelsCleanly();

    /** @brief Verifies floating dock pop-out, drag-back redocking via drop zone, and 1-click/double-click restore. */
    void testFloatingDockPopOutAndDragBackRedock();

    /** @brief Verifies Preferences sidebar inactive contrast, luminance palette swatches, 15+ terminal fonts, and SLURM script auto-detection. */
    void testPreferencesSidebarContrastTerminalFontsAndSlurmAutoDetect();
};

