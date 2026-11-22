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

    /** @brief Verifies CFG rank divergence badges, minimap, and rack sync. */
    void testCfgRankDivergenceMinimapAndRackSync();

    /** @brief Verifies dynamic rank telemetry without constructor mocks. */
    void testDynamicRankSyncWithoutHardcodedConstructorMocks();

    /** @brief Verifies native ELF64 binary parsing, symbols, .rodata strings, and target loading. */
    void testElfBinaryInspectorAndStaticTargetLoad();

    /** @brief Verifies interactive Xref extraction, incoming/outgoing call graph analysis, and inspector dialog sync. */
    void testInteractiveXrefInspectorAndCallGraphSync();

    /** @brief Verifies patched ELF export to disk, unified multi-domain workspace search, and deadlock propagation. */
    void testPatchedElfExportWorkspaceSearchAndDeadlockPropagation();

    /** @brief Verifies workstation UI cohesion across all bottom tabs and F1 docset lookup. */
    void testWorkstationUiCohesionAndIntegratedDocViewer();

    /** @brief Verifies cross-pane workflow bridges (Evaluator/Variables -> Watch, Disassembly context actions -> Reference Manual). */
    void testCrossPaneWorkflowBridgesAndContextActions();

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

    /** @brief Verifies gutter QContextMenuEvent isolation from editor viewport menu and clean rollback of canceled breakpoints. */
    void testGutterRightClickSuppressesEditorContextMenuAndCancelsCleanly();

    /** @brief Verifies floating dock pop-out, drag-back redocking via drop zone, and 1-click/double-click restore. */
    void testFloatingDockPopOutAndDragBackRedock();

    /** @brief Verifies Preferences sidebar inactive contrast, luminance palette swatches, 15+ terminal fonts, and SLURM script auto-detection. */
    void testPreferencesSidebarContrastTerminalFontsAndSlurmAutoDetect();

    /** @brief Verifies multi-distro package manager auto-detection, Spack/Pacman/DNF/APT/Zypper CLI output parsing, and Table/List/Raw display views. */
    void testMultiDistroPackageManagerParsingAndViewModes();

    /** @brief Verifies ClangdLspClient virtual-buffer splicing, JSON-RPC framing, and nested struct/vector expression completion in ConditionalBreakpointDialog. */
    void testClangdLspBreakpointExpressionCompletion();

    /** @brief Verifies full workstation light theme propagation, non-colliding palette swatches, and theme switching. */
    void testFullWorkstationLightThemePropagationAndLineCountHeadroom();

    /** @brief Verifies editable SourceCodeView, live editor LSP completion, modern chained breakpoint popover, and theme persistence. */
    void testEditableSourceViewModernLspPopupAndThemePersistence();

    /** @brief Verifies ProjectExplorerWidget file/folder CRUD operations, signals, and meson.build/CMakeLists.txt source registration. */
    void testProjectExplorerFileCrudAndBuildRegistration();

    /** @brief Verifies SourceCodeView unified Git diff gutter parsing/markers and PreferencesDialog debugger engine selector. */
    void testGitDiffGutterAndDebuggerEngineSelector();

    /** @brief Verifies CommandPaletteDialog subsequence fuzzy scoring, prefix filtering (>, @, #), keyboard navigation, and Ctrl+Shift+P / Ctrl+P shortcuts. */
    void testCommandPaletteFuzzyAndNavigation();

    /** @brief Verifies ProgrammerCalculatorDialog Hex/Dec/Oct/Bin conversion, word width masking, 64-bit toggle grid, BSWAP, expression evaluator, Command Palette, and Preferences integration. */
    void testProgrammerCalculatorBasesAndBits();

    /** @brief Verifies ArrayPlotDialog 1D/2D buffer decoding, summary stats, live stop mutation counts, CSV export, and PointerGraphDialog persistent auto-updating roots with field mutation diffs. */
    void testArrayPlotterAndPersistentPointerGraph();

    /** @brief Verifies GdbRankCoordinator signal disposition/catchpoint MI commands and SignalCatchpointDialog UI integration. */
    void testSignalDispositionAndCatchpoints();

    /** @brief Verifies GdbConsoleWidget persistent gdb_history, Up/Down draft preservation, Ctrl+R reverse search, and Tab completion popup. */
    void testGdbConsoleReadlineErgonomics();

    /** @brief Verifies nexti, Ctrl+F10 Run to Cursor, draggable gutter PC jump, lexical scope hazard guard, Force Early Return, and shortcut uniqueness. */
    void testNonLinearExecutionControl();

    /** @brief Verifies non-destructive breakpoint enable/disable hollow markers, tbreak, rbreak, and GDB breakpoint command lists. */
    void testBreakpointLifecycleAndCommands();

    /** @brief Verifies Copy-on-Write GDB checkpoints (checkpoint/restart/info/delete), /proc fd/maps safety checks, and CheckpointDialog integration. */
    void testCowCheckpointsAndProcSafety();

    /** @brief Verifies real QSplitter non-overlapping split editors and Ctrl+F inline search vs Ctrl+G seek bar focus. */
    void testSplitEditorQSplitterAndInlineFindBar();

    /** @brief Verifies <1040px MainToolbar consolidation, F10/F11/Shift+F11 source stepping, F4 Header/Source, single Stop, and state-aware tooltips. */
    void testConsolidatedToolbarSourceSteppingAndTooltips();

    /** @brief Verifies Source/Debug/Binary/MPI progressive disclosure, >=60% startup editor width, non-destructive floating docks, ranks==1 vs ranks==4 ServerRack auto-collapse, 0 fake watch columns, and idle control disablement. */
    void testProgressiveDisclosurePerspectivesAndSingleRank();

    /** @brief Verifies single DisassemblyView context menu + CFG/Decompiler menus, Watch registration/removal/contrast/tooltips, MemView cursor byte offset, and VariableTreeModel tooltips/copy/floating signals. */
    void testCrossViewHandoffsWatchLifecycleAndMemCursor();

    /** @brief Verifies fail-loud build error handling, backend/ptrace preflight recovery, validatePatchBytes + Revert All Patches, confirmation guards, and PointerGraph/ArrayPlot Undo Clear. */
    void testBuildPreflightValidatedPatchingAndSafetyGuards();
};

