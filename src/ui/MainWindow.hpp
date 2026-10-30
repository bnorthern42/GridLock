/**
 * @file MainWindow.hpp
 * @brief Declaration of the primary application window for GridLock.
 *
 * Defines the MainWindow class, which hosts the four-zone dockable workbench,
 * multi-rank MPI coordination views, cross-pane address navigation, disassembly
 * and decompiler synchronization, and session lifecycle management.
 */

#pragma once

#include "../RankState.hpp"
#include "../core/utils/ElfBinaryInspector.hpp"

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

class IBackendCoordinator;
class QVulkanInstance;
class QDockWidget;
class QSplitter;

namespace gridlock {
class GdbRankCoordinator;
class VariablesDockWidget;
class PlainJaneTooltip;
}  // namespace gridlock

namespace gridlock::core {
class LspCoordinator;
class HpcBackend;
class DeadlockAnalyzer;
}  // namespace gridlock::core

namespace gridlock::core::commands {
class IDebugCommand;
}  // namespace gridlock::core::commands

namespace gridlock::ui {

class ServerRackView;
class SourceCodeView;
class EditorTabManager;
class DisassemblyView;
class ReferenceManualWidget;
class DifferentialGrid;
class TerminalDockWidget;
class MpiNetworkLogWidget;
class ProjectExplorerWidget;
class GdbConsoleWidget;
class MemView;
class RegisterView;
class SpackManager;
class ExpressionEvaluatorWidget;
class MpiDiagnosticsWidget;

/**
 * @class MainWindow
 * @brief Central workstation window integrating all parallel debugging and disassembly views.
 *
 * Orchestrates docking layouts, command bus execution, multi-rank telemetry updates,
 * navigation history, and secondary monitor display routing.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief Constructs the MainWindow and initializes UI subsystems.
     * @param parent Optional parent widget context.
     */
    explicit MainWindow(QWidget* parent = nullptr);

    /**
     * @brief Virtual destructor for clean resource teardown.
     */
    ~MainWindow() override = default;

    /**
     * @brief Associates an initialized Vulkan instance with the main window.
     * @param inst Pointer to the active QVulkanInstance.
     */
    void setVulkanInstance(QVulkanInstance* inst);

    struct WorkspaceSearchResult {
        QString category;       ///< Domain classification ("Symbol", "String", "Disassembly", "Decompiler").
        uint64_t address = 0;   ///< Associated 64-bit virtual memory address.
        QString label;          ///< Symbol or identifier label.
        QString contextSnippet; ///< Surrounding line or contextual snippet.
    };

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
    MpiDiagnosticsWidget* mpiDiagnosticsWidget() const { return m_mpiDiagnosticsWidget; }
    IBackendCoordinator* coordinator() const { return m_coordinator; }
    int focusedRank() const { return m_focusedRank; }

    /**
     * @brief Launches an MPI debug session on the specified target binary.
     * @param binaryPath Path to executable.
     * @param ranks Initial rank count.
     */
    void startDebuggingSession(const QString& binaryPath, int ranks);

    /**
     * @brief Returns whether an interactive or live debugging session is currently active.
     * @return True if running or paused at a breakpoint; false when idle.
     */
    bool isDebugSessionActive() const { return m_debugSessionActive; }

    /**
     * @brief Starts the target debugging workflow, spawning configured MPI ranks and pausing at entry.
     */
    void startTargetWorkflow();

    /**
     * @brief Steps the focused rank to the next instruction and synchronizes all 5 inspection views.
     * @return True if the instruction pointer advanced; false otherwise.
     */
    bool stepInstructionWorkflow();

    /**
     * @brief Continues execution from current PC until the next active breakpoint or procedure return.
     * @return True if execution advanced; false otherwise.
     */
    bool continueExecutionWorkflow();

    /**
     * @brief Terminates the active debugging session and resets the workstation to IDLE state.
     */
    void stopTargetWorkflow();

    /**
     * @brief Updates main toolbar action enablement and latched Run Target state.
     */
    void updateDebugToolbarState();

    /**
     * @brief Returns whether the checkable Run Target toolbar action is latched down as Running.
     * @return True while an active debugging session latches Run Target; false when idle.
     */
    bool isRunButtonLatched() const;

    /**
     * @brief Maps a 1-based C/C++ source line number to its corresponding 64-bit instruction address.
     * @param lineOneBased 1-based source line index.
     * @return 64-bit virtual instruction address.
     */
    uint64_t sourceLineToAddress(int lineOneBased) const;

    /**
     * @brief Maps a 64-bit virtual instruction address to its corresponding 1-based C/C++ source line.
     * @param address 64-bit virtual instruction address.
     * @return 1-based source line number.
     */
    int addressToSourceLine(uint64_t address) const;

    /**
     * @brief Dynamically rebuilds the bidirectional source-line-to-instruction map.
     */
    void rebuildDynamicSourceLineMap();

    /**
     * @brief Finds the 1-based source line containing a specific statement pattern.
     * @param pattern C/C++ token or statement text to match.
     * @return 1-based line number, or -1 if not found.
     */
    int findSourceLineForStatement(const QString& pattern) const;

    /**
     * @brief Returns the index of the currently active MPI rank for debugging/stepping.
     * @return 0-based rank index.
     */
    int activeSteppingRank() const;

    /**
     * @brief Computes the next instruction address for an MPI rank considering control-flow branches and loops.
     * @param rank 0-based MPI rank index.
     * @param currentPc Current instruction pointer address.
     * @return Next instruction pointer address.
     */
    uint64_t advanceRankProgramCounter(int rank, uint64_t currentPc);

    /**
     * @brief Detects the build system ("Meson", "CMake", "Make", or "SingleFileMPI") for a directory or source file.
     * @param targetPath Project directory path or source file path.
     * @return Detected build system identifier string.
     */
    QString detectBuildSystem(const QString& targetPath) const;

    /**
     * @brief Constructs the shell build command for a given project directory or standalone source file.
     * @param targetPath Project directory path or source file path.
     * @return Shell command string to compile the target.
     */
    QString buildCommandForPath(const QString& targetPath) const;

    /**
     * @brief Constructs the shell clean command for a given project directory or standalone source file.
     * @param targetPath Project directory path or source file path.
     * @return Shell command string to clean build artifacts.
     */
    QString cleanCommandForPath(const QString& targetPath) const;

    /**
     * @brief Switches BottomConsoleTabs to Terminal and dispatches the detected build command.
     * @return True if the build command was generated and dispatched to TerminalDockWidget.
     */
    bool buildTargetWorkflow();

    /**
     * @brief Switches BottomConsoleTabs to Terminal and dispatches the detected clean command.
     * @return True if the clean command was generated and dispatched to TerminalDockWidget.
     */
    bool cleanTargetWorkflow();

    /**
     * @brief Switches BottomConsoleTabs to Terminal and dispatches a chained clean && build command.
     * @return True if the rebuild command was generated and dispatched to TerminalDockWidget.
     */
    bool rebuildTargetWorkflow();

    /**
     * @brief Selects an active target in `#toolbarTargetSelector` by name or path and synchronizes all views.
     * @param targetName Target name substring or file path (e.g., "deadlock_ring" or "inspection_demo").
     * @return True if a matching target was found and activated; false otherwise.
     */
    bool selectTargetByName(const QString& targetName);

    /**
     * @brief Compiles the active C/C++ target on-demand with `-g -O0` if missing or stale,
     *        loads its ELF64 binary into ElfBinaryInspector, and populates DWARF line mappings.
     * @return True if a valid ELF64 target binary was compiled/loaded; false otherwise.
     */
    bool ensureTargetBinaryCompiled();

    /**
     * @brief Returns the resolved source file path for the currently active target.
     * @return Absolute or resolved path to the active target's C/C++ source file.
     */
    QString activeTargetSourcePath() const;

    /**
     * @brief Returns the resolved output executable binary path for the currently active target.
     * @return Absolute or resolved path to the active target's compiled binary executable.
     */
    QString activeTargetBinaryPath() const;

    /**
     * @brief Dispatches an Edit menu action ("Undo", "Redo", "Cut", "Copy", "Paste",
     *        "Select All", "Find", "Go to Line", "Toggle Comment", or "Preferences")
     *        to the currently focused or active source editor.
     * @param actionName Name or substring of the Edit menu action.
     * @return True if the action was recognized and executed; false otherwise.
     */
    bool triggerEditMenuAction(const QString& actionName);

    /**
     * @brief Applies UI and Code/Disassembly typography settings live across the entire workstation.
     * @param uiFamily UI font family name.
     * @param uiSize UI font point size.
     * @param codeFamily Code/Disassembly monospace font family name.
     * @param codeSize Code/Disassembly font point size.
     */
    void applyTypographySettings(const QString& uiFamily, int uiSize,
                                 const QString& codeFamily, int codeSize);

    /**
     * @brief Switches the workstation layout between "Code", "Reverse", and "MPI" perspectives.
     * @param perspective Target perspective name ("Code", "Reverse", or "MPI").
     */
    void setWorkspacePerspective(const QString& perspective);

    /**
     * @brief Returns the currently active workspace perspective identifier ("Code", "Reverse", or "MPI").
     * @return Active perspective name.
     */
    QString workspacePerspective() const { return m_workspacePerspective; }

    /**
     * @brief Toggles the bottom console dock between standard height (260px) and maximized height (65%).
     */
    void toggleMaximizeBottomConsole();

    /**
     * @brief Returns the ordered list of bottom tool-window icon rail group identifiers.
     * @return List containing {"Debug", "MPI", "Terminal", "Docs"}.
     */
    QStringList bottomToolGroups() const;

    /**
     * @brief Returns the currently active bottom tool-window group identifier.
     * @return Active group name ("Debug", "MPI", "Terminal", or "Docs").
     */
    QString activeBottomToolGroup() const { return m_activeBottomToolGroup; }

    /**
     * @brief Activates a bottom tool-window group, filtering visible tabs strictly to its linked views.
     * @param groupName Target group name ("Debug", "MPI", "Terminal", or "Docs").
     */
    void selectBottomToolGroup(const QString& groupName);

    /**
     * @struct DockHomeSlot
     * @brief Records the original splitter or QMainWindow dock area home container for a panel.
     */
    struct DockHomeSlot {
        QSplitter* splitter = nullptr;
        int splitterIndex = -1;
        Qt::DockWidgetArea area = Qt::RightDockWidgetArea;
    };

    /**
     * @brief Checks if a dock widget is currently detached in a top-level floating window.
     * @param dock Pointer to the dock widget to inspect.
     * @return True if dock is floating or detached as top-level window.
     */
    bool isDockFloating(const QDockWidget* dock) const;

    /**
     * @brief Toggles a dock widget between floating top-level state and docked workspace slot.
     * @param dock Pointer to the dock widget to float or redock.
     */
    void toggleDockFloating(QDockWidget* dock);

    /**
     * @brief Re-docks a floating dock widget into its recorded home splitter or dock area.
     * @param dock Pointer to the dock widget to re-dock.
     */
    void redockWidget(QDockWidget* dock);

    /**
     * @brief Calculates the global screen rectangle of a dock widget's home drop zone.
     * @param dock Pointer to the dock widget.
     * @return Global screen rectangle of the home dock or splitter region.
     */
    QRect dockHomeGlobalRect(const QDockWidget* dock) const;

    /**
     * @brief Evaluates mouse release position during dock drag and redocks if dropped in home zone.
     * @param dock Pointer to the dock widget being dragged.
     * @param globalReleasePos Global cursor release position.
     * @return True if redocked, false otherwise.
     */
    bool handleDockDragRelease(QDockWidget* dock, const QPoint& globalReleasePos);

    /**
     * @brief Restores all floating or detached panels back into their home workstation slots.
     */
    void redockAllFloatingPanels();

    /**
     * @brief Creates and mounts a custom dark-slate title bar widget on the specified dock widget.
     * @param dock Pointer to the target dock widget.
     * @param titleText Header display title string.
     * @return Pointer to created custom title bar header widget.
     */
    QWidget* setupDockTitleBar(QDockWidget* dock, const QString& titleText);

    /**
     * @brief Dispatches a debug command through the coordinator.
     * @param cmd Unique pointer to command object.
     */
    void executeCommand(std::unique_ptr<gridlock::core::commands::IDebugCommand> cmd);

    /**
     * @brief Attaches a backend coordinator instance.
     * @param coord Pointer to backend coordinator.
     */
    void setCoordinator(IBackendCoordinator* coord);

    /**
     * @brief Renders the entire workspace offscreen to an image file.
     * @param outputPath Target file path.
     * @return True on success.
     */
    bool captureWorkspaceSnapshot(const QString& outputPath);

    /**
     * @brief Applies standard workstation dock arrangement and metric constraints.
     */
    void applyCutterWorkspaceLayout();

    /**
     * @brief Seeks memory, disassembly, and decompiler views to an address or symbol.
     * @param expr Address or symbolic expression.
     * @return True if target location resolved and synchronized.
     */
    bool seekToAddressOrExpression(const QString& expr);

    /**
     * @brief Synthesizes a deterministic memory window for preview/fallback targets.
     * @param addr 64-bit virtual memory address.
     * @param length Number of bytes to generate.
     * @return Byte array containing section-specific machine code, rodata strings, or stack bytes.
     */
    QByteArray synthesizeMemoryWindow(uint64_t addr, int length) const;

    /**
     * @brief Loads a 64-bit ELF binary executable or shared object for static inspection.
     * @param binaryPath Path to executable file on disk.
     * @return True if file is valid ELF64 and successfully parsed.
     */
    bool loadBinaryTarget(const QString& binaryPath);

    /**
     * @brief Exports the in-memory patched ELF binary image to disk.
     * @param outputPath Filesystem destination path.
     * @return True if exported and permissions configured; false otherwise.
     */
    bool exportPatchedBinary(const QString& outputPath) const;

    /**
     * @brief Performs unified multi-domain case-insensitive search across the workspace.
     * @param query Search string.
     * @return List of matching results sorted by domain and address.
     */
    QVector<WorkspaceSearchResult> searchWorkspace(const QString& query) const;

    /**
     * @brief Detects wait-for cycle among MPI ranks and highlights deadlocked nodes.
     * @param waitForGraph Directed edges where key rank waits for value rank.
     * @return List of ranks participating in the deadlock cycle.
     */
    QVector<int> detectAndHighlightDeadlock(const QMap<int, int>& waitForGraph);

    /**
     * @brief Resolves symbol documentation (stripping @plt/@got suffixes), switches to Reference Manual, and renders HTML.
     * @param rawSymbol Raw symbol name or topic identifier.
     * @return True if documentation was rendered and tab activated; false otherwise.
     */
    bool lookupSymbolDocumentation(const QString& rawSymbol);

    /**
     * @brief Switches to the specified bottom console tab index and captures a PNG audit snapshot.
     * @param tabIndex Zero-based index in BottomConsoleTabs.
     * @param outputPath Destination PNG file path.
     * @return True if PNG snapshot was rendered and saved; false otherwise.
     */
    bool captureTabAuditSnapshot(int tabIndex, const QString& outputPath);

    bool canNavigateBack() const;
    bool canNavigateForward() const;
    bool navigateBack();
    bool navigateForward();

    bool isShowingDecompilerFallback() const { return m_showingDecompilerFallback; }
    void enableDecompilerFallback(bool enabled);

    QScreen* resolvePreferredScreen(const QString& monitorHint) const;
    void applyPreferredScreenRouting(const QString& monitorHint, bool openUnfocused);
    QString preferredMonitorHint() const { return m_preferredMonitorHint; }

    /**
     * @brief Populates preview fixtures for automated headless snapshots.
     */
    void loadPreviewFixture();

public slots:
    void onRankStateChanged(int rankId, const RankState& state);
    void onRankSelected(int rankId);
    void openFile();
    void openPreferences();
    void loadSourceFile(const QString& filePath);
    void saveSessionAs();
    void loadSession();
    void onTutorialLaunchRequested(const QString& absoluteFilePath);
    bool execTutorialDialog();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUi();
    void setupMenu();
    void setupToolbar();
    void setupDocks();
    void setupStatusBar();
    void harmonizeBottomConsoleTabs();
    QString bottomToolGroupForTabIndex(int tabIndex) const;
    void updateStatusTelemetry(const QString& pcOrSeek = QString(),
                               const QString& targetState = QString());
    void updateNavigationButtons();
    void hideHoverTooltipPopup();

signals:
    void runTargetRequested();

private:
    ServerRackView* m_serverRackView = nullptr;
    EditorTabManager* m_editorTabManager = nullptr;
    DisassemblyView* m_disassemblyView = nullptr;
    ReferenceManualWidget* m_referenceManualWidget = nullptr;
    DifferentialGrid* m_differentialGrid = nullptr;
    ProjectExplorerWidget* m_projectExplorerWidget = nullptr;
    TerminalDockWidget* m_terminalDock = nullptr;
    GdbConsoleWidget* m_gdbConsoleWidget = nullptr;
    MemView* m_memView = nullptr;
    RegisterView* m_registerView = nullptr;
    ExpressionEvaluatorWidget* m_expressionEvaluatorWidget = nullptr;

    gridlock::VariablesDockWidget* m_variablesDockWidget = nullptr;
    MpiDiagnosticsWidget* m_mpiDiagnosticsWidget = nullptr;
    QWidget* m_bottomToolRail = nullptr;
    QTabWidget* m_bottomTabs = nullptr;
    QMap<QString, QToolButton*> m_bottomRailButtons;
    QString m_activeBottomToolGroup = QStringLiteral("Debug");
    bool m_syncingBottomToolGroup = false;
    QComboBox* m_targetSelector = nullptr;
    QSpinBox* m_rankCountSpin = nullptr;
    QAction* m_runAction = nullptr;
    QAction* m_continueAction = nullptr;
    QAction* m_stepAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_terminateAction = nullptr;
    QPushButton* m_navBackBtn = nullptr;
    QPushButton* m_navForwardBtn = nullptr;
    QLineEdit* m_seekBar = nullptr;
    QWidget* m_addressNavBar = nullptr;
    QLabel* m_statusTelemetryLabel = nullptr;
    QString m_currentSeekOrPc = QStringLiteral("0x00000000");
    QString m_currentTargetState = QStringLiteral("IDLE");
    QString m_preferredMonitorHint;
    QVector<QString> m_seekHistory;
    int m_seekHistoryIndex = -1;
    bool m_isNavigatingHistory = false;
    bool m_showingDecompilerFallback = false;
    bool m_forceDecompilerFallback = false;
    bool m_debugSessionActive = false;
    bool m_bottomConsoleMaximized = false;
    QString m_workspacePerspective = QStringLiteral("Code");
    QPushButton* m_perspCodeBtn = nullptr;
    QPushButton* m_perspReverseBtn = nullptr;
    QPushButton* m_perspMpiBtn = nullptr;

    IBackendCoordinator* m_coordinator = nullptr;
    gridlock::core::LspCoordinator* m_lspCoordinator = nullptr;
    PlainJaneTooltip* m_plainJaneTooltip = nullptr;
    gridlock::core::HpcBackend* m_hpcBackend = nullptr;
    gridlock::core::DeadlockAnalyzer* m_deadlockAnalyzer = nullptr;
    SpackManager* m_spackManager = nullptr;

    int m_focusedRank = 0;
    std::unordered_map<int, RankState> m_latestStates;
    QString m_currentFile;
    QMap<QString, QSet<int>> m_persistentBreakpoints;
    std::unique_ptr<gridlock::core::ElfBinaryInspector> m_elfInspector;

    static QString resolveWorkspacePath(const QString& rawPath);

    QMap<int, uint64_t> m_dynamicLineToAddress;
    QMap<uint64_t, int> m_dynamicAddressToLine;
    QMap<int, uint64_t> m_dwarfLineToAddress;
    QMap<uint64_t, int> m_dwarfAddressToLine;
    QMap<int, int> m_rankLoopCounter;
    QString m_loadedBinaryPath;
    bool m_useRealBinaryTarget = false;
    QMap<QDockWidget*, DockHomeSlot> m_dockHomeSlots;
    QPointer<QDockWidget> m_draggingDock = nullptr;
    QPoint m_dragOffset;
};

}  // namespace gridlock::ui
