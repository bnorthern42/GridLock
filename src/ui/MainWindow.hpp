/**
 * @file MainWindow.hpp
 * @brief Declaration of the primary application window for GridLock.
 *
 * Defines the MainWindow class, hosting the four-zone dockable workbench,
 * multi-rank MPI coordination views, cross-pane address navigation, disassembly
 * and decompiler synchronization, and session lifecycle management.
 */

#pragma once

#include "../RankState.hpp"
#include "../core/hpc/IBackendCoordinator.hpp"
#include "../core/hpc/system/ProcessScanner.hpp"
#include "../core/utils/ElfBinaryInspector.hpp"
#include "MainWindowInternal.hpp"

#include <QCloseEvent>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMap>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QVector>
#include <memory>
#include <unordered_map>

namespace gridlock::core::plugins {
class PluginManager;
}  // namespace gridlock::core::plugins

namespace gridlock::ui {

class PluginReportDockWidget;

/**
 * @class MainWindow
 * @brief Central workstation window integrating all parallel debugging and disassembly views.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /** @brief Constructs the MainWindow and initializes UI subsystems. */
    explicit MainWindow(QWidget* parent = nullptr);

    /** @brief Virtual destructor shutting down live coordinators before child dock teardown. */
    ~MainWindow() override;

    /** @brief Associates an initialized Vulkan instance with the main window. */
    void setVulkanInstance(QVulkanInstance* inst);

    using WorkspaceSearchResult = gridlock::ui::WorkspaceSearchResult;
    using DockHomeSlot = gridlock::ui::DockHomeSlot;

    // Subview Accessors
    ServerRackView* serverRackView() const { return m_serverRackView; }
    EditorTabManager* editorTabManager() const { return m_editorTabManager; }
    SourceCodeView* sourceCodeView() const;
    SourceCodeView* getSourceCodeView() const;
    DisassemblyView* disassemblyView() const { return m_disassemblyView; }
    ReferenceManualWidget* referenceManualWidget() const { return m_referenceManualWidget; }
    DifferentialGrid* differentialGrid() const { return m_differentialGrid; }
    TerminalDockWidget* terminalDock() const { return m_terminalDock; }
    MpiNetworkLogWidget* networkLog() const;
    GdbConsoleWidget* gdbConsoleWidget() const { return m_gdbConsoleWidget; }
    MemView* memView() const { return m_memView; }
    RegisterView* registerView() const { return m_registerView; }
    ProjectExplorerWidget* projectExplorerWidget() const { return m_projectExplorerWidget; }
    gridlock::core::ElfBinaryInspector* elfInspector() const { return m_elfInspector.get(); }
    gridlock::VariablesDockWidget* variablesDockWidget() const { return m_variablesDockWidget; }
    CallStackDockWidget* callStackDockWidget() const { return m_callStackDockWidget; }
    MpiDiagnosticsWidget* mpiDiagnosticsWidget() const { return m_mpiDiagnosticsWidget; }
    ArrayPlotDialog* arrayPlotDialog() const { return m_arrayPlotDialog; }
    CheckpointDialog* checkpointDialog() const { return m_checkpointDialog; }
    PointerGraphDialog* pointerGraphDialog() const { return m_pointerGraphDialog; }
    CommandPaletteDialog* commandPaletteDialog() const { return m_commandPaletteDialog; }
    ProgrammerCalculatorDialog* programmerCalculatorDialog() const { return m_programmerCalculatorDialog; }
    PluginReportDockWidget* pluginReportDockWidget() const { return m_pluginReportDock; }
    gridlock::core::plugins::PluginManager* pluginManager() const { return m_pluginManager; }
    IBackendCoordinator* coordinator() const { return m_coordinator; }
    int focusedRank() const { return m_focusedRank; }
    int configuredRankCount() const { return m_rankCountSpin ? m_rankCountSpin->value() : 4; }
    const std::unordered_map<int, RankState>& latestRankStates() const { return m_latestStates; }

    // Execution, Launch & Stepping Workflows
    /** @brief Launches an MPI debug session on @p binaryPath with @p ranks processes. */
    void startDebuggingSession(const QString& binaryPath, int ranks);
    /** @brief Returns whether an interactive or live debugging session is currently active. */
    bool isDebugSessionActive() const { return m_debugSessionActive; }
    /** @brief Returns whether zero-breakpoint launches automatically pause at `main()`. */
    bool stopAtMainOnLaunch() const { return m_stopAtMainOnLaunch; }
    /** @brief Enables or disables automatic temporary `main()` breakpoint insertion on launch. */
    void setStopAtMainOnLaunch(bool enabled);
    /** @brief Applies a CLI positional target binary/workspace/source path and `-- <program_args...>`. */
    bool applyCliLaunchOptions(const QString& targetPath, const QStringList& programArgs);
    /** @brief Starts the target debugging workflow, spawning configured MPI ranks and pausing at entry. */
    void startTargetWorkflow();
    /** @brief Steps the focused rank by one machine instruction and synchronizes all inspection views. */
    bool stepInstructionWorkflow();
    /** @brief Continues execution from current PC until the next active breakpoint or exit. */
    bool continueExecutionWorkflow();
    /** @brief Terminates the active debugging session and resets the workstation to IDLE state. */
    void stopTargetWorkflow();
    /** @brief Synchronously disconnects signals and stops/reaps active debug sessions during shutdown. */
    void shutdownActiveSessionSync();
    /** @brief Returns true once window close or destructor teardown has started. */
    bool isShuttingDown() const { return m_isShuttingDown; }
    /** @brief Steps over the current source line on the active execution scope. */
    bool stepOverWorkflow();
    /** @brief Steps into the callee on the active execution scope. */
    bool stepIntoWorkflow();
    /** @brief Finishes the current stack frame on the active execution scope. */
    bool stepOutWorkflow();
    /** @brief Interrupts/pauses execution on the active execution scope. */
    bool pauseRankWorkflow();
    /** @brief Steps backward over the previous source line (`-exec-next --reverse` / DAP `stepBack`). */
    bool reverseStepOverWorkflow();
    /** @brief Runs backward in recorded history (`-exec-continue --reverse` / DAP `reverseContinue`). */
    bool reverseContinueWorkflow();
    /** @brief Steps over a single machine instruction on the active MPI rank (`nexti`). */
    bool nextInstructionWorkflow();
    /** @brief Executes until `filePath:lineOneBased` (or active cursor/instruction) is reached (`Ctrl+F10`). */
    bool runToCursorWorkflow(const QString& filePath = QString(), int lineOneBased = -1);
    /** @brief Executes until machine instruction @p address is reached (`-exec-until *0x...`). */
    bool runToAddressWorkflow(uint64_t address);
    /** @brief Relocates PC to `filePath:lineOneBased` (`-exec-jump`) with lexical scope safety confirmation. */
    bool jumpToLocationWorkflow(const QString& filePath, int lineOneBased, bool skipPrompt = false);
    /** @brief Relocates PC to machine instruction @p address (`-exec-jump *0x...`) with safety check. */
    bool jumpToAddressWorkflow(uint64_t address, bool skipPrompt = false);
    /** @brief Forces an immediate return from the current frame (`-exec-return [expr]`). */
    bool forceEarlyReturnWorkflow(const QString& explicitExpr = QString(), bool promptIfEmpty = true);
    /** @brief Installs function-symbol regex breakpoints via GDB `rbreak <pattern>`. */
    bool insertRegexBreakpointWorkflow(const QString& explicitPattern = QString(), bool promptIfEmpty = true);

    /** @brief Analyzes whether jumping between lines crosses function, block, or variable init boundaries. */
    static inline QString checkJumpLexicalScopeHazard(const QString& sourceText, int currentLine,
                                                      int targetLine,
                                                      const QString& currentFunction = QString()) {
        return detail::checkJumpLexicalScopeHazardImpl(sourceText, currentLine, targetLine, currentFunction);
    }

    // Toolbar, Source Mapping & Build Orchestration
    /** @brief Updates main toolbar action enablement and latched Run Target state. */
    void updateDebugToolbarState();
    /** @brief Returns whether the checkable Run Target toolbar action is latched down as Running. */
    bool isRunButtonLatched() const;
    /** @brief Maps a 1-based C/C++ source line number to its 64-bit instruction address. */
    uint64_t sourceLineToAddress(int lineOneBased) const;
    /** @brief Maps a 64-bit virtual instruction address to its 1-based C/C++ source line. */
    int addressToSourceLine(uint64_t address) const;
    /** @brief Dynamically rebuilds the bidirectional source-line-to-instruction map. */
    void rebuildDynamicSourceLineMap();
    /** @brief Finds the 1-based source line containing a specific statement pattern. */
    int findSourceLineForStatement(const QString& pattern) const;
    /** @brief Returns the 0-based index of the currently focused MPI rank for stepping. */
    int activeSteppingRank() const;
    /** @brief Computes the next instruction address for an MPI rank in preview-fixture mode. */
    uint64_t advanceRankProgramCounter(int rank, uint64_t currentPc);
    /** @brief Detects the build system ("Meson", "CMake", "Make", or "SingleFileMPI") for @p targetPath. */
    QString detectBuildSystem(const QString& targetPath) const;
    /** @brief Constructs the shell build command for a project directory or standalone source file. */
    QString buildCommandForPath(const QString& targetPath) const;
    /** @brief Constructs the shell clean command for a project directory or standalone source file. */
    QString cleanCommandForPath(const QString& targetPath) const;
    /** @brief Switches BottomConsoleTabs to Terminal and dispatches the detected build command. */
    bool buildTargetWorkflow();
    /** @brief Switches BottomConsoleTabs to Terminal and dispatches the detected clean command. */
    bool cleanTargetWorkflow();
    /** @brief Switches BottomConsoleTabs to Terminal and dispatches a chained clean && build command. */
    bool rebuildTargetWorkflow();
    /** @brief Selects an active target in `#toolbarTargetSelector` by name or path and syncs all views. */
    bool selectTargetByName(const QString& targetName);
    /** @brief Compiles the active C/C++ target on-demand with `-g -O0` and loads ELF64 + DWARF info. */
    bool ensureTargetBinaryCompiled();
    /** @brief Returns the resolved source file path for the currently active target. */
    QString activeTargetSourcePath() const;
    /** @brief Returns the resolved output executable binary path for the currently active target. */
    QString activeTargetBinaryPath() const;
    /** @brief Dispatches an Edit menu action to the currently focused or active source editor. */
    bool triggerEditMenuAction(const QString& actionName);
    /** @brief Applies UI and Code/Disassembly typography settings live across the workstation. */
    void applyTypographySettings(const QString& uiFamily, int uiSize,
                                 const QString& codeFamily, int codeSize);

    // Perspectives, Execution Scope & Theme Propagation
    /** @brief Switches the workstation layout between "Source", "Debug", "Binary", and "MPI" perspectives. */
    void setWorkspacePerspective(const QString& perspective);
    /** @brief Synchronizes ServerRackDock and multi-rank selector visibility for single vs multi-rank runs. */
    void syncRankCountChrome(int rankCount = -1);
    /** @brief Returns true when multi-rank execution commands target All Ranks rather than Focused Rank. */
    bool isAllRanksExecutionScope() const;
    /** @brief Returns `-1` when scoped to All Ranks, or `activeSteppingRank()` when scoped to Focused Rank. */
    int executionTargetRank() const;
    /** @brief Switches the toolbar execution scope selector between All Ranks and Focused Rank. */
    void setExecutionScopeAllRanks(bool allRanks);
    /** @brief Interrupts execution across all active ranks via `m_coordinator->haltAll()` (`F6`). */
    bool pauseAllRanksWorkflow();
    /** @brief Synchronizes `#toolbarExecutionScopeCombo` visibility, tooltips, and coordinator continue scope. */
    void syncExecutionScopeUi();
    /** @brief Returns the currently active workspace perspective identifier. */
    QString workspacePerspective() const { return m_workspacePerspective; }
    /** @brief Toggles the bottom console dock between standard height (260px) and maximized height (65%). */
    void toggleMaximizeBottomConsole();
    /** @brief Returns the ordered list of bottom tool-window icon rail group identifiers. */
    QStringList bottomToolGroups() const;
    /** @brief Returns the currently active bottom tool-window group identifier. */
    QString activeBottomToolGroup() const { return m_activeBottomToolGroup; }
    /** @brief Activates a bottom tool-window group, filtering visible tabs strictly to its linked views. */
    void selectBottomToolGroup(const QString& groupName);
    /** @brief Synchronizes the bottom tool rail button state with the specified tab index. */
    void syncBottomToolRailToTab(int tabIndex);
    /** @brief Propagates the active ThemeManager palette across all chrome toolbars, docks, and views. */
    Q_INVOKABLE void propagateThemeToAllWidgets();
    /** @brief Reloads workstation theme styling according to ThemeManager palette. */
    Q_INVOKABLE void reloadStyle() { propagateThemeToAllWidgets(); }
    /** @brief Verifies that all chrome toolbars, ribbons, and dock headers align with the active theme. */
    bool areAllChromeToolbarsAndDockHeadersThemeAligned() const;

    // Dock Management, Navigation, Binary Inspection & Attachment
    /** @brief Checks if a dock widget is currently detached in a top-level floating window. */
    bool isDockFloating(const QDockWidget* dock) const;
    /** @brief Toggles a dock widget between floating top-level state and docked workspace slot. */
    void toggleDockFloating(QDockWidget* dock);
    /** @brief Re-docks a floating dock widget into its recorded home splitter or dock area. */
    void redockWidget(QDockWidget* dock);
    /** @brief Calculates the global screen rectangle of a dock widget's home drop zone. */
    QRect dockHomeGlobalRect(const QDockWidget* dock) const;
    /** @brief Evaluates mouse release position during dock drag and redocks if dropped in home zone. */
    bool handleDockDragRelease(QDockWidget* dock, const QPoint& globalReleasePos);
    /** @brief Restores all floating or detached panels back into their home workstation slots. */
    void redockAllFloatingPanels();
    /** @brief Intercepts and handles mouse/context-menu events on custom dock headers. */
    bool handleDockHeaderEvent(QObject* watched, QEvent* event);
    /** @brief Creates and mounts a custom title bar widget on the specified dock widget. */
    QWidget* setupDockTitleBar(QDockWidget* dock, const QString& titleText);
    /** @brief Dispatches a debug command through the coordinator. */
    void executeCommand(std::unique_ptr<gridlock::core::commands::IDebugCommand> cmd);
    /** @brief Attaches a backend coordinator instance. */
    void setCoordinator(IBackendCoordinator* coord);
    /** @brief Instantiates or switches `m_coordinator` to match `ConfigManager::debuggerEngine()`. */
    void syncCoordinatorWithConfig();
    /** @brief Renders the entire workspace offscreen to an image file. */
    bool captureWorkspaceSnapshot(const QString& outputPath);
    /** @brief Applies standard workstation dock arrangement and metric constraints. */
    void applyCutterWorkspaceLayout();
    /** @brief Reorders, shows/hides, and inserts spacers/separators on `#MainToolbar` from @p layout tokens. */
    void rebuildToolbarFromLayout(const QStringList& layout);
    /** @brief Shows (`true`, default) or hides (`false`) the `Minimize / Maximize / Close` corner controls in `menuBar()`. */
    void setMenuBarWindowControlsVisible(bool visible);
    /** @brief Returns true when the menu-bar window control buttons are visible. */
    bool areMenuBarWindowControlsVisible() const { return m_menuBarWindowControlsVisible; }
    /** @brief Seeks memory, disassembly, and decompiler views to an address or symbol. */
    bool seekToAddressOrExpression(const QString& expr);
    /** @brief Synthesizes a deterministic memory window for preview/fallback targets. */
    QByteArray synthesizeMemoryWindow(uint64_t addr, int length) const;
    /** @brief Loads a 64-bit ELF binary executable or shared object for static inspection. */
    bool loadBinaryTarget(const QString& binaryPath);
    /** @brief Exports the in-memory patched ELF binary image to disk. */
    bool exportPatchedBinary(const QString& outputPath) const;
    /** @brief Performs unified multi-domain case-insensitive search across the workspace. */
    QVector<WorkspaceSearchResult> searchWorkspace(const QString& query) const;
    /** @brief Detects wait-for cycle among MPI ranks and highlights deadlocked nodes. */
    QVector<int> detectAndHighlightDeadlock(const QMap<int, int>& waitForGraph);
    /** @brief Resolves symbol documentation, switches to Reference Manual, and renders HTML. */
    bool lookupSymbolDocumentation(const QString& rawSymbol);
    /** @brief Switches to @p tabIndex in BottomConsoleTabs and captures a PNG audit snapshot. */
    bool captureTabAuditSnapshot(int tabIndex, const QString& outputPath);
    /** @brief Captures all 1600x960 workstation, feature, and theme documentation screenshots. */
    bool captureDocumentationScreenshots(const QString& outputDir);
    bool canNavigateBack() const;
    bool canNavigateForward() const;
    bool navigateBack();
    bool navigateForward();
    /** @brief Jumps the active source editor cursor to the clangd-resolved symbol definition (F12). */
    bool goToDefinitionAtCursor();
    bool isShowingDecompilerFallback() const { return m_showingDecompilerFallback; }
    void enableDecompilerFallback(bool enabled);
    QScreen* resolvePreferredScreen(const QString& monitorHint) const;
    void applyPreferredScreenRouting(const QString& monitorHint, bool openUnfocused);
    QString preferredMonitorHint() const { return m_preferredMonitorHint; }
    /** @brief Opens `AttachProcessDialog` or attaches directly to @p explicitPid via `m_coordinator`. */
    bool attachToProcessWorkflow(qint64 explicitPid = 0);
    /** @brief Attaches `m_coordinator` to one or more running processes or an entire MPI job. */
    bool attachToRunningProcesses(const QList<qint64>& explicitPids = {});
    /** @brief Connects `GdbRankCoordinator` to remote `gdbserver` / `qemu-user -g` endpoints. */
    bool attachToRemoteTargets(const QList<RemoteTargetSpec>& explicitTargets);
    /** @brief Populates preview fixtures for automated headless snapshots. */
    void loadPreviewFixture();
    /** @brief Returns the canonical or absolute path of the active workspace directory. */
    QString activeWorkspaceDirectory() const { return m_activeWorkspaceDir; }

public slots:
    /** @brief Atomically switches the workstation to an opened folder or `.toml` workspace root. */
    bool switchWorkspaceDirectory(const QString& dirPath);
    /** @brief Enforces DisassemblyView pseudo-C decompiler and CFG/Linear visibility policy. */
    void syncDisassemblyDecompilerVisibility(bool applyDefaultMode);
    void syncDisassemblyDecompilerVisibility() { syncDisassemblyDecompilerVisibility(true); }
    void onRankStateChanged(int rankId, const RankState& state);
    /** @brief Master rank-focus synchronization hub across all docks, dialogs, and toolbar controls. */
    void setFocusedRank(int rank);
    void onRankSelected(int rankId);
    /** @brief Evaluates @p expr against the target selected in `#evaluatorRankCombo`. */
    void evaluateExpressionFromWidget(const QString& expr);
    void onRankBreakpointChanged(const QString& file, int line, bool isSet, int rankFilter,
                                 const QString& condition);
    void syncTabBreakpointMetadata(SourceCodeView* view) const;
    void openFile();
    void openPreferences();
    void loadSourceFile(const QString& filePath);
    /** @brief Opens @p filePath in the source editor and positions the cursor at 1-based `(lineOneBased, columnOneBased)`. */
    bool openFileAtLine(const QString& filePath, int lineOneBased, int columnOneBased = 1);
    bool saveSession(bool showStatusMessage = true);
    void saveSessionAs();
    void loadSession();
    void onTutorialLaunchRequested(const QString& absoluteFilePath);
    bool execTutorialDialog();
    /** @brief Opens the modeless DDD Pointer & Struct Graph Visualizer and optionally graphs @p exprPath. */
    void openPointerGraphVisualizer(const QString& exprPath = QString(),
                                    const QString& varValue = QString(),
                                    const QString& varType = QString());
    /** @brief Opens or raises the live 1D/2D Array & Field Plotter dialog and optionally plots @p exprPath. */
    void openArrayPlotter(const QString& exprPath = QString(), const QString& typeHint = QString());
    /** @brief Opens the unified fuzzy Command Palette overlay pre-populated with @p initialPrefix. */
    void openCommandPalette(const QString& initialPrefix = QStringLiteral(">"));
    /** @brief Opens JetBrains-style Search Everywhere in the requested scope (0 = All, 1 = Files, 4 = Actions). */
    void openSearchEverywhere(int scope = 0, const QString& initialQuery = QString());
    /** @brief Opens Search Everywhere in Files mode (Ctrl+P). */
    void openQuickOpenPalette() { openCommandPalette(QString()); }
    /** @brief Opens or raises the floating Programmer's Calculator tool window (Ctrl+Alt+C). */
    void openProgrammerCalculator();
    /** @brief Opens or raises the floating Programmer's Calculator pre-populated with @p text. */
    void openProgrammerCalculatorWithText(const QString& text);
    /** @brief Opens or raises the Signal Disposition Matrix & Catchpoints dialog (Ctrl+Alt+S). */
    void openSignalsAndCatchpoints();
    /** @brief Opens or raises the Copy-on-Write Process Checkpoints dialog (Ctrl+Alt+K). */
    void openProcessCheckpoints();
    /** @brief Forwards debug session start metadata to all enabled native plugins. */
    void notifySessionStart(int numRanks, const QString& targetPath = QString());
    /** @brief Forwards MPI rank stop metadata to all enabled native plugins. */
    void notifyRankStopped(int rank, const QString& file, int line, const QString& reason = QStringLiteral("stopped"));
    /** @brief Forwards debug session termination to all enabled native plugins. */
    void notifySessionEnd();
    /** @brief Rebuilds the Tools -> Plugins submenu from active plugin command registrations. */
    void rebuildToolsPluginsMenu();

signals:
    void runTargetRequested();
    void focusedRankChanged(int rank);
    void pluginStructuredEventEmitted(const QString& channel, const QString& jsonPayload);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    using BreakpointRankMeta = gridlock::ui::BreakpointRankMeta;

    void setupUi();
    void setupMenu();
    void rebuildRecentSessionsMenu();
    void recordRecentWorkspace(const QString& path);
    void setupToolbar();
    void registerViewMenuToolbarAndDockActions();
    void wireTemporaryBreakpointHit();
    void applyPreviewStopState(int rank, uint64_t addr, const QString& status);
    void setupDocks();
    void setupCallStackDock();
    void setupStatusBar();
    void bindLiveMemViewHandlers();
    void bindCallStackCoordinatorRoutes();
    void bindConsoleAndWatchpointRoutes();
    void harmonizeBottomConsoleTabs();
    QString bottomToolGroupForTabIndex(int tabIndex) const;
    void updateStatusTelemetry(const QString& pcOrSeek = QString(), const QString& targetState = QString());
    void refreshStatusBarMemoryTelemetry();
    void updateNavigationButtons();
    void hideHoverTooltipPopup();
    QWidget* createProgramOutputTab();
    void appendProgramOutput(int rank, const QString& text, bool isError);
    Q_INVOKABLE void flushProgramOutputBatch();
    void syncProgramOutputForLaunch(int rankCount);
    void initializePluginSubsystem();
    void bindPluginCoordinatorSignals(IBackendCoordinator* coord);
    static QString resolveWorkspacePath(const QString& rawPath);

    // Dock, View & Chrome Widget Pointers
    bool m_syncingFocusedRank = false;
    ServerRackView* m_serverRackView = nullptr; QMenu* m_recentSessionsMenu = nullptr;
    EditorTabManager* m_editorTabManager = nullptr; DisassemblyView* m_disassemblyView = nullptr;
    ReferenceManualWidget* m_referenceManualWidget = nullptr; DifferentialGrid* m_differentialGrid = nullptr;
    ProjectExplorerWidget* m_projectExplorerWidget = nullptr; TerminalDockWidget* m_terminalDock = nullptr;
    GdbConsoleWidget* m_gdbConsoleWidget = nullptr; MemView* m_memView = nullptr; RegisterView* m_registerView = nullptr;
    ExpressionEvaluatorWidget* m_expressionEvaluatorWidget = nullptr;
    gridlock::VariablesDockWidget* m_variablesDockWidget = nullptr;
    CallStackDockWidget* m_callStackDockWidget = nullptr; MpiDiagnosticsWidget* m_mpiDiagnosticsWidget = nullptr;
    QWidget* m_bottomToolRail = nullptr; QTabWidget* m_bottomTabs = nullptr;
    QLineEdit* m_programLaunchCommandEdit = nullptr; QPlainTextEdit* m_programOutputView = nullptr;
    QComboBox* m_programOutputRankFilter = nullptr;
    QVector<QPair<int, QString>> m_programOutputLines;
    QTimer* m_programOutputFlushTimer = nullptr; QTimer* m_telemetryTimer = nullptr;
    QStringList m_pendingProgramOutputBatch;
    QString m_activeSessionFilePath;
    QMap<QString, QToolButton*> m_bottomRailButtons;
    QString m_activeBottomToolGroup = QStringLiteral("Debug");
    bool m_syncingBottomToolGroup = false;
    bool m_isClosing = false;
    bool m_isShuttingDown = false;

    // Toolbar, Execution Scope & Launch Options Controls
    QComboBox* m_targetSelector = nullptr; QToolButton* m_launchOptionsBtn = nullptr;
    QLineEdit* m_launchOptionsArgsEdit = nullptr; QLineEdit* m_launchOptionsWorkDirEdit = nullptr;
    QCheckBox* m_launchOptionsStopAtMainCheck = nullptr;
    bool m_stopAtMainOnLaunch = true, m_stopAtMainExplicit = false, m_sessionRestoredEmptyBps = false;
    QSpinBox* m_rankCountSpin = nullptr; QComboBox* m_executionScopeCombo = nullptr;
    QAction* m_executionScopeAction = nullptr; QAction* m_runAction = nullptr; QAction* m_continueAction = nullptr;
    QAction* m_stepOverAction = nullptr; QAction* m_stepIntoAction = nullptr; QAction* m_stepOutAction = nullptr;
    QAction* m_stepAction = nullptr; QAction* m_pauseAction = nullptr; QAction* m_pauseAllAction = nullptr; QAction* m_stopAction = nullptr;
    bool m_syncingScopeUi = false;
    QPushButton* m_navBackBtn = nullptr; QPushButton* m_navForwardBtn = nullptr;
    QLineEdit* m_seekBar = nullptr; QWidget* m_addressNavBar = nullptr; QLabel* m_statusTelemetryLabel = nullptr;
    QString m_currentSeekOrPc = QStringLiteral("0x00000000"), m_currentTargetState = QStringLiteral("IDLE"), m_preferredMonitorHint;
    QVector<QString> m_seekHistory; int m_seekHistoryIndex = -1;
    bool m_isNavigatingHistory = false, m_showingDecompilerFallback = false, m_forceDecompilerFallback = false;
    bool m_debugSessionActive = false, m_suppressMemViewBroadcast = false, m_suppressExecutionTabSwitch = false, m_bottomConsoleMaximized = false;
    QString m_workspacePerspective = QStringLiteral("Source");
    QPushButton* m_perspSourceBtn = nullptr; QPushButton* m_perspDebugBtn = nullptr;
    QPushButton* m_perspBinaryBtn = nullptr; QPushButton* m_perspMpiBtn = nullptr;
    QAction* m_targetSelectorAct = nullptr; QAction* m_launchOptionsAct = nullptr; QAction* m_rankCountAct = nullptr;
    QAction* m_buildSplitAct = nullptr; QAction* m_pauseAllTbAct = nullptr; QAction* m_recordHistoryAct = nullptr;
    QAction* m_stepBackTbAct = nullptr; QAction* m_revContinueTbAct = nullptr;
    QAction* m_navBackTbAct = nullptr; QAction* m_navForwardTbAct = nullptr;
    QAction* m_searchEverywhereAct = nullptr; QAction* m_seekBarTbAct = nullptr;
    QMenu* m_stepIntoMenu = nullptr;
    QList<QAction*> m_perspActs, m_dynamicToolbarActions;
    QList<QPointer<QWidget>> m_dynamicToolbarWidgets;

    // Backend Coordinators, Rank Caches & Modeless Tool Dialogs
    IBackendCoordinator* m_coordinator = nullptr;
    IBackendCoordinator* m_ownedCoordinator = nullptr;
    gridlock::core::LspCoordinator* m_lspCoordinator = nullptr;
    PlainJaneTooltip* m_plainJaneTooltip = nullptr;
    gridlock::core::HpcBackend* m_hpcBackend = nullptr;
    gridlock::core::DeadlockAnalyzer* m_deadlockAnalyzer = nullptr;
    SpackManager* m_spackManager = nullptr;
    int m_focusedRank = 0;
    std::unordered_map<int, RankState> m_latestStates;
    QMap<int, QVector<StackFrameEntry>> m_cachedStackFrames;
    QMap<int, QVector<ThreadEntry>> m_cachedThreads;
    QMap<int, int> m_selectedFrameLevel;
    QString m_currentFile;
    QString m_activeWorkspaceDir;
    bool m_switchingWorkspace = false;
    QMap<QString, QMap<int, BreakpointRankMeta>> m_breakpointRankMeta;
    QMap<QString, QSet<int>> m_persistentBreakpoints;
    std::unique_ptr<gridlock::core::ElfBinaryInspector> m_elfInspector;
    QMap<int, uint64_t> m_dynamicLineToAddress;
    QMap<uint64_t, int> m_dynamicAddressToLine;
    QMap<int, uint64_t> m_dwarfLineToAddress;
    QMap<uint64_t, int> m_dwarfAddressToLine;
    QMap<int, int> m_rankLoopCounter;
    QString m_loadedBinaryPath;
    bool m_useRealBinaryTarget = false;
    bool m_previewFixtureOnly = false;
    QMap<QDockWidget*, DockHomeSlot> m_dockHomeSlots;
    QPointer<QDockWidget> m_draggingDock = nullptr;
    QPoint m_dragOffset;
    ArrayPlotDialog* m_arrayPlotDialog = nullptr;
    PointerGraphDialog* m_pointerGraphDialog = nullptr;
    CommandPaletteDialog* m_commandPaletteDialog = nullptr;
    ProgrammerCalculatorDialog* m_programmerCalculatorDialog = nullptr;
    SignalCatchpointDialog* m_signalCatchpointDialog = nullptr;
    CheckpointDialog* m_checkpointDialog = nullptr;
    qint64 m_lastBareShiftReleaseMs = 0;
    bool m_bareShiftArmed = false;
    QWidget* m_windowControlsWidget = nullptr;
    QToolButton* m_windowMinimizeBtn = nullptr;
    QToolButton* m_windowMaximizeBtn = nullptr;
    QToolButton* m_windowCloseBtn = nullptr;
    bool m_menuBarWindowControlsVisible = true;
    gridlock::core::plugins::PluginManager* m_pluginManager = nullptr;
    PluginReportDockWidget* m_pluginReportDock = nullptr;
    QMenu* m_toolsPluginsMenu = nullptr;
    QList<QMetaObject::Connection> m_pluginCoordConns;
    bool m_pluginSessionActive = false;

    struct SessionShutdownGuard {
        MainWindow* self = nullptr;
        ~SessionShutdownGuard() {
            if (self && !self->m_isShuttingDown && self->m_coordinator) self->shutdownActiveSessionSync();
        }
    } m_shutdownGuard{this};
};

}  // namespace gridlock::ui
