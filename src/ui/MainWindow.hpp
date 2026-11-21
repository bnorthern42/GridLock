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
#include "../core/hpc/IBackendCoordinator.hpp"
#include "../core/hpc/ProcessScanner.hpp"
#include "../core/utils/ElfBinaryInspector.hpp"

#include <QCloseEvent>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMap>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QVector>
#include <memory>
#include <unordered_map>

class QVulkanInstance;
class QDockWidget;
class QMenu;
class QPlainTextEdit;
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

class CallStackDockWidget;
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
class ArrayPlotDialog;
class CheckpointDialog;
class CommandPaletteDialog;
class PointerGraphDialog;
class ProgrammerCalculatorDialog;
class SignalCatchpointDialog;

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
     * @brief Virtual destructor shutting down live coordinators before child dock teardown.
     */
    ~MainWindow() override;

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
    CallStackDockWidget* callStackDockWidget() const { return m_callStackDockWidget; }
    MpiDiagnosticsWidget* mpiDiagnosticsWidget() const { return m_mpiDiagnosticsWidget; }
    ArrayPlotDialog* arrayPlotDialog() const { return m_arrayPlotDialog; }
    CheckpointDialog* checkpointDialog() const { return m_checkpointDialog; }
    PointerGraphDialog* pointerGraphDialog() const { return m_pointerGraphDialog; }
    CommandPaletteDialog* commandPaletteDialog() const { return m_commandPaletteDialog; }
    ProgrammerCalculatorDialog* programmerCalculatorDialog() const { return m_programmerCalculatorDialog; }
    IBackendCoordinator* coordinator() const { return m_coordinator; }
    int focusedRank() const { return m_focusedRank; }
    int configuredRankCount() const { return m_rankCountSpin ? m_rankCountSpin->value() : 4; }
    const std::unordered_map<int, RankState>& latestRankStates() const { return m_latestStates; }

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

    /** @brief Terminates the active debugging session and resets the workstation to IDLE state. */
    void stopTargetWorkflow();
    /** @brief Synchronously disconnects signals and stops/reaps active debug sessions during shutdown. */
    void shutdownActiveSessionSync();
    /** @brief Returns true once window close or destructor teardown has started. */
    bool isShuttingDown() const { return m_isShuttingDown; }

    /**
     * @brief Steps over the current source line on the active MPI rank (`IBackendCoordinator::stepOver`).
     * @return True if a live coordinator step-over request was dispatched; false otherwise.
     */
    bool stepOverWorkflow();

    /**
     * @brief Steps into the callee on the active MPI rank (`IBackendCoordinator::stepInto`).
     * @return True if a live coordinator step-into request was dispatched; false otherwise.
     */
    bool stepIntoWorkflow();

    /** @brief Finishes the current stack frame on the active MPI rank (`IBackendCoordinator::stepOut`). */
    bool stepOutWorkflow();

    /**
     * @brief Interrupts/pauses execution on the active MPI rank (`IBackendCoordinator::pauseRank`).
     * @return True if a live coordinator pause request was dispatched; false otherwise.
     */
    bool pauseRankWorkflow();

    /** @brief Steps backward over the previous source line (`-exec-next --reverse` / DAP `stepBack`). */
    bool reverseStepOverWorkflow();

    /** @brief Runs backward in recorded history (`-exec-continue --reverse` / DAP `reverseContinue`). */
    bool reverseContinueWorkflow();

    /** @brief Steps over a single machine instruction on the active MPI rank (`nexti` / `-exec-next-instruction`). */
    bool nextInstructionWorkflow();

    /** @brief Executes until `filePath:lineOneBased` (or active cursor/instruction) is reached (`Ctrl+F10` / `-exec-until`). */
    bool runToCursorWorkflow(const QString& filePath = QString(), int lineOneBased = -1);

    /** @brief Executes until machine instruction @p address is reached (`-exec-until *0x...`). */
    bool runToAddressWorkflow(uint64_t address);

    /** @brief Relocates PC to `filePath:lineOneBased` (`-exec-jump`) with lexical scope safety confirmation. */
    bool jumpToLocationWorkflow(const QString& filePath, int lineOneBased, bool skipPrompt = false);

    /** @brief Relocates PC to machine instruction @p address (`-exec-jump *0x...`) with safety check. */
    bool jumpToAddressWorkflow(uint64_t address, bool skipPrompt = false);

    /** @brief Forces an immediate return from the current frame (`-exec-return [expr]`), optionally prompting for @p explicitExpr. */
    bool forceEarlyReturnWorkflow(const QString& explicitExpr = QString(), bool promptIfEmpty = true);

    /** @brief Installs function-symbol regex breakpoints via GDB `rbreak <pattern>`, optionally prompting for @p explicitPattern. */
    bool insertRegexBreakpointWorkflow(const QString& explicitPattern = QString(), bool promptIfEmpty = true);

    /**
     * @brief Analyzes whether jumping from @p currentLine to @p targetLine in @p sourceText crosses function,
     *        lexical block, or local variable initialization boundaries.
     * @return Empty string when safe, or a human-readable hazard description for confirmation dialogs.
     */
    static inline QString checkJumpLexicalScopeHazard(const QString& sourceText, int currentLine,
                                                      int targetLine, const QString& currentFunction = QString()) {
        if (sourceText.isEmpty() || targetLine <= 0) return QString();
        const QStringList rawLines = sourceText.split(QLatin1Char('\n'));
        if (targetLine > rawLines.size()) return QString();

        // Strip comments and string literals line-by-line while tracking brace depth and scope stacks.
        bool inBlockComment = false;
        QStringList cleanLines;
        cleanLines.reserve(rawLines.size());
        for (const QString& rl : rawLines) {
            QString out;
            bool inStr = false;
            for (int i = 0; i < rl.size(); ++i) {
                if (inBlockComment) {
                    if (i + 1 < rl.size() && rl[i] == QLatin1Char('*') && rl[i + 1] == QLatin1Char('/')) { inBlockComment = false; ++i; }
                    continue;
                }
                if (inStr) {
                    if (rl[i] == QLatin1Char('\\') && i + 1 < rl.size()) { ++i; continue; }
                    if (rl[i] == QLatin1Char('"')) inStr = false;
                    continue;
                }
                if (i + 1 < rl.size() && rl[i] == QLatin1Char('/') && rl[i + 1] == QLatin1Char('/')) break;
                if (i + 1 < rl.size() && rl[i] == QLatin1Char('/') && rl[i + 1] == QLatin1Char('*')) { inBlockComment = true; ++i; continue; }
                if (rl[i] == QLatin1Char('"')) { inStr = true; out.append(QLatin1Char(' ')); continue; }
                out.append(rl[i]);
            }
            cleanLines.append(out.trimmed());
        }

        struct FuncRange { QString name; int startLine = 0; int endLine = 0; int bodyDepth = 1; };
        QVector<FuncRange> funcs;
        QVector<int> braceStack;
        QVector<int> curScopeStack, tgtScopeStack;
        QString pendingFuncName;
        int pendingFuncLine = -1;
        int activeFuncIdx = -1;
        static const QRegularExpression kFuncHdrRe(QStringLiteral("(?:^|[\\s*&>])([~A-Za-z_][A-Za-z0-9_:]*)\\s*\\([^;]*\\)\\s*(?:const|noexcept|override|final|\\s)*\\{?$"));
        static const QSet<QString> kCtrlWords = {
            QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("for"), QStringLiteral("while"),
            QStringLiteral("switch"), QStringLiteral("catch"), QStringLiteral("return"), QStringLiteral("sizeof")};

        for (int i = 0; i < cleanLines.size(); ++i) {
            const int ln = i + 1;
            const QString& cl = cleanLines.at(i);
            if (ln == currentLine) curScopeStack = braceStack;
            if (ln == targetLine) tgtScopeStack = braceStack;

            if (activeFuncIdx < 0 && !cl.isEmpty() && !cl.endsWith(QLatin1Char(';'))) {
                if (const auto fm = kFuncHdrRe.match(cl); fm.hasMatch() && !kCtrlWords.contains(fm.captured(1))) {
                    pendingFuncName = fm.captured(1);
                    pendingFuncLine = ln;
                }
            }
            for (QChar ch : cl) {
                if (ch == QLatin1Char('{')) {
                    braceStack.append(ln);
                    if (activeFuncIdx < 0 && pendingFuncLine > 0 && ln - pendingFuncLine <= 3) {
                        funcs.append({pendingFuncName, pendingFuncLine, static_cast<int>(rawLines.size()), static_cast<int>(braceStack.size())});
                        activeFuncIdx = funcs.size() - 1;
                        pendingFuncLine = -1;
                    }
                } else if (ch == QLatin1Char('}') && !braceStack.isEmpty()) {
                    if (activeFuncIdx >= 0 && static_cast<int>(braceStack.size()) == funcs[activeFuncIdx].bodyDepth) {
                        funcs[activeFuncIdx].endLine = ln;
                        activeFuncIdx = -1;
                    }
                    braceStack.removeLast();
                }
            }
        }

        // 1. Check whether targetLine leaves the enclosing function frame.
        const FuncRange* curFunc = nullptr;
        for (const auto& fr : funcs) {
            if (currentLine > 0 && currentLine >= fr.startLine && currentLine <= fr.endLine) { curFunc = &fr; break; }
            if (currentLine <= 0 && !currentFunction.isEmpty() && fr.name.endsWith(currentFunction)) { curFunc = &fr; break; }
        }
        if (curFunc && (targetLine < curFunc->startLine || targetLine > curFunc->endLine)) {
            return QStringLiteral("Target line %1 lies outside the current function '%2' (lines %3-%4). Jumping across function frames can corrupt the call stack, saved registers, and C++ destructors.")
                .arg(targetLine).arg(curFunc->name).arg(curFunc->startLine).arg(curFunc->endLine);
        }
        if (currentLine <= 0 || targetLine == currentLine) return QString();

        // 2. Check whether jumping crosses nested lexical { ... } block boundaries.
        if (curScopeStack != tgtScopeStack) {
            return QStringLiteral("Jumping from line %1 to line %2 crosses a lexical block scope boundary. Skipping block entry/exit may bypass C++ constructors or RAII destructors.")
                .arg(currentLine).arg(targetLine);
        }

        // 3. Check whether jumping crosses local variable declarations/initializations in the same block.
        static const QRegularExpression kVarDeclRe(
            QStringLiteral("^(?:(?:const|constexpr|static|volatile|mutable|unsigned|signed|long|short|struct|class)\\s+)*"
                           "(auto|bool|char|short|int|long|float|double|size_t|int\\d+_t|uint\\d+_t|[A-Za-z_][A-Za-z0-9_:<>]*)\\s*"
                           "(?:[*&]+\\s*)*([A-Za-z_][A-Za-z0-9_]*)\\s*(?:=|\\{|\\(|\\[|;)"));
        static const QSet<QString> kNonTypeWords = {
            QStringLiteral("return"), QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("for"),
            QStringLiteral("while"), QStringLiteral("do"), QStringLiteral("switch"), QStringLiteral("case"),
            QStringLiteral("break"), QStringLiteral("continue"), QStringLiteral("goto"), QStringLiteral("throw"),
            QStringLiteral("delete"), QStringLiteral("typedef"), QStringLiteral("using"), QStringLiteral("co_return")};
        const int scanStart = qMin(currentLine, targetLine);
        const int scanEnd = qMax(currentLine, targetLine) - 1;
        QStringList crossedDecls;
        for (int ln = scanStart; ln <= scanEnd && ln <= cleanLines.size(); ++ln) {
            const QString& cl = cleanLines.at(ln - 1);
            if (cl.isEmpty() || cl.startsWith(QLatin1Char('#'))) continue;
            if (const auto dm = kVarDeclRe.match(cl); dm.hasMatch() && !kNonTypeWords.contains(dm.captured(1))) {
                crossedDecls.append(QString::number(ln));
            }
        }
        if (!crossedDecls.isEmpty()) {
            return QStringLiteral("Jumping from line %1 to line %2 crosses local variable initialization(s) on line(s) %3. Uninitialized stack variables or skipped C++ destructors may cause undefined behavior.")
                .arg(currentLine).arg(targetLine).arg(crossedDecls.join(QStringLiteral(", ")));
        }
        return QString();
    }

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

    /** @brief Switches the workstation layout between "Source", "Debug", "Binary", and "MPI" perspectives. */
    void setWorkspacePerspective(const QString& perspective);
    /** @brief Synchronizes ServerRackDock and multi-rank selector visibility for single-rank vs multi-rank workflows. */
    void syncRankCountChrome(int rankCount = -1);
    /** @brief Returns true when multi-rank execution commands target All Ranks rather than Focused Rank. */
    bool isAllRanksExecutionScope() const;
    /** @brief Returns `-1` when scoped to All Ranks, or `activeSteppingRank()` when scoped to Focused Rank. */
    int executionTargetRank() const;
    /** @brief Switches the toolbar execution scope selector between All Ranks (`true`) and Focused Rank (`false`). */
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
    /** @brief Propagates the active ThemeManager palette across all chrome toolbars, dock headers, and workstation views. */
    Q_INVOKABLE void propagateThemeToAllWidgets();
    /** @brief Reloads workstation theme styling according to ThemeManager palette. */
    Q_INVOKABLE void reloadStyle() { propagateThemeToAllWidgets(); }
    /** @brief Verifies that all chrome toolbars, ribbons, and dock headers align with the active theme. */
    bool areAllChromeToolbarsAndDockHeadersThemeAligned() const;

    /** @brief Records the original splitter or QMainWindow dock area home container for a panel. */
    struct DockHomeSlot {
        QSplitter* splitter = nullptr;
        int splitterIndex = -1;
        Qt::DockWidgetArea area = Qt::RightDockWidgetArea;
    };

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

    /**
     * @brief Attaches a backend coordinator instance.
     * @param coord Pointer to backend coordinator.
     */
    void setCoordinator(IBackendCoordinator* coord);

    /**
     * @brief Instantiates or switches the active `IBackendCoordinator` (`GdbRankCoordinator` vs `DapCoordinator`)
     *        to match `ConfigManager::instance().debuggerEngine()` when no debugging session is running.
     */
    void syncCoordinatorWithConfig();

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

    /**
     * @brief Captures all 1600x960 workstation, feature, and theme documentation screenshots into outputDir.
     * @param outputDir Target directory path (e.g., "docs/screenshots").
     * @return True if all screenshots were rendered and saved; false otherwise.
     */
    bool captureDocumentationScreenshots(const QString& outputDir);

    bool canNavigateBack() const;
    bool canNavigateForward() const;
    bool navigateBack();
    bool navigateForward();

    /**
     * @brief Jumps the active source editor cursor to the clangd-resolved symbol definition (F12).
     * @return True if a local or cross-file definition location was resolved and activated.
     */
    bool goToDefinitionAtCursor();

    bool isShowingDecompilerFallback() const { return m_showingDecompilerFallback; }
    void enableDecompilerFallback(bool enabled);

    QScreen* resolvePreferredScreen(const QString& monitorHint) const;
    void applyPreferredScreenRouting(const QString& monitorHint, bool openUnfocused);
    QString preferredMonitorHint() const { return m_preferredMonitorHint; }

    /**
     * @brief Opens `AttachProcessDialog` (or attaches directly to @p explicitPid) via `m_coordinator`.
     * @param explicitPid Optional explicit OS PID (`<= 0` opens the interactive `/proc` picker).
     * @return True if the coordinator attached to the target PID and activated the debug session.
     */
    bool attachToProcessWorkflow(qint64 explicitPid = 0);

    /**
     * @brief Attaches `m_coordinator` to one or more running processes or an entire MPI job.
     * @param explicitPids Optional explicit list of PIDs (empty opens `AttachProcessDialog`).
     * @return True if attachment succeeded and the workstation entered active session state.
     */
    bool attachToRunningProcesses(const QList<qint64>& explicitPids = {});

    /**
     * @brief Connects `GdbRankCoordinator` to one or more remote `gdbserver` / `qemu-user -g` endpoints.
     */
    bool attachToRemoteTargets(const QList<RemoteTargetSpec>& explicitTargets);

    /**
     * @brief Populates preview fixtures for automated headless snapshots.
     */
    void loadPreviewFixture();

    /**
     * @brief Returns the canonical or absolute path of the currently opened external/active workspace directory.
     * @return Active workspace directory path, or empty string prior to explicit workspace selection.
     */
    QString activeWorkspaceDirectory() const { return m_activeWorkspaceDir; }

public slots:
    /**
     * @brief Atomically switches the workstation to an opened folder or `.toml` workspace session root.
     *
     * Re-roots `ProjectExplorerWidget` (`FILES` mode), scans the workspace and build subdirectories
     * (`build_meson`, `build_cmake`, `builddir`, `build`, `bin`) for runnable ELF executables and
     * C/C++ entry targets to repopulate `m_targetSelector`, updates `ConfigManager` and window title,
     * and opens the primary target binary/source.
     *
     * @param dirPath Target workspace directory path or `.gridlock/workspace.toml` / `.toml` session path.
     * @return True if the workspace directory exists and was activated; false otherwise.
     */
    bool switchWorkspaceDirectory(const QString& dirPath);

    /**
     * @brief Enforces DisassemblyView pseudo-C decompiler and CFG/Linear mode visibility policy.
     * @param applyDefaultMode True to also apply `ConfigManager::defaultDisassemblyMode()` when source is visible.
     */
    void syncDisassemblyDecompilerVisibility(bool applyDefaultMode);
    void syncDisassemblyDecompilerVisibility() { syncDisassemblyDecompilerVisibility(true); }

    void onRankStateChanged(int rankId, const RankState& state);
    /** @brief Master rank-focus synchronization hub across all docks, dialogs, and toolbar controls. */
    void setFocusedRank(int rank);
    void onRankSelected(int rankId);
    /** @brief Evaluates @p expr against the target selected in `#evaluatorRankCombo` (`All Ranks` or `Rank k`). */
    void evaluateExpressionFromWidget(const QString& expr);
    void onRankBreakpointChanged(const QString& file, int line, bool isSet, int rankFilter,
                                 const QString& condition);
    void syncTabBreakpointMetadata(SourceCodeView* view) const;
    void openFile();
    void openPreferences();
    void loadSourceFile(const QString& filePath);
    bool saveSession(bool showStatusMessage = true);
    void saveSessionAs();
    void loadSession();
    void onTutorialLaunchRequested(const QString& absoluteFilePath);
    bool execTutorialDialog();
    /** @brief Opens the modeless DDD Pointer & Struct Graph Visualizer and optionally graphs @p exprPath. */
    void openPointerGraphVisualizer(const QString& exprPath = QString(), const QString& varValue = QString(), const QString& varType = QString());
    /** @brief Opens or raises the live 1D/2D Array & Field Plotter dialog and optionally plots @p exprPath. */
    void openArrayPlotter(const QString& exprPath = QString(), const QString& typeHint = QString());
    /** @brief Opens the unified fuzzy Command Palette overlay pre-populated with @p initialPrefix (e.g., ">" or ""). */
    void openCommandPalette(const QString& initialPrefix = QStringLiteral(">"));
    /** @brief Opens the unified Command Palette in Quick Open File mode (Ctrl+P). */
    void openQuickOpenPalette() { openCommandPalette(QString()); }
    /** @brief Opens or raises the floating Programmer's Calculator tool window (Ctrl+Alt+C). */
    void openProgrammerCalculator();
    /** @brief Opens or raises the floating Programmer's Calculator pre-populated with @p text. */
    void openProgrammerCalculatorWithText(const QString& text);
    /** @brief Opens or raises the Signal Disposition Matrix & Catchpoints manager dialog (Ctrl+Alt+S). */
    void openSignalsAndCatchpoints();
    /** @brief Opens or raises the Copy-on-Write Process Checkpoints manager dialog (Ctrl+Alt+K). */
    void openProcessCheckpoints();
protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void setupUi();
    void setupMenu();
    /** @brief Rebuilds File -> Recent Sessions from persisted MRU history. */
    void rebuildRecentSessionsMenu();
    /** @brief Records a workspace/session path in MRU history and refreshes Recent Sessions menu. */
    void recordRecentWorkspace(const QString& path);
    void setupToolbar();
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

signals:
    void runTargetRequested();
    void focusedRankChanged(int rank);

private:
    bool m_syncingFocusedRank = false;
    ServerRackView* m_serverRackView = nullptr;
    QMenu* m_recentSessionsMenu = nullptr;
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
    CallStackDockWidget* m_callStackDockWidget = nullptr;
    QMap<int, QVector<StackFrameEntry>> m_cachedStackFrames;
    QMap<int, QVector<ThreadEntry>> m_cachedThreads;
    QMap<int, int> m_selectedFrameLevel;
    MpiDiagnosticsWidget* m_mpiDiagnosticsWidget = nullptr;
    QWidget* m_bottomToolRail = nullptr;
    QWidget* createProgramOutputTab();
    void appendProgramOutput(int rank, const QString& text, bool isError);
    Q_INVOKABLE void flushProgramOutputBatch();
    void syncProgramOutputForLaunch(int rankCount);
    QTabWidget* m_bottomTabs = nullptr;
    QLineEdit* m_programLaunchCommandEdit = nullptr;
    QPlainTextEdit* m_programOutputView = nullptr;
    QComboBox* m_programOutputRankFilter = nullptr;
    QVector<QPair<int, QString>> m_programOutputLines;
    class QTimer* m_programOutputFlushTimer = nullptr;
    class QTimer* m_telemetryTimer = nullptr;
    QStringList m_pendingProgramOutputBatch;
    QString m_activeSessionFilePath;
    QMap<QString, QToolButton*> m_bottomRailButtons;
    QString m_activeBottomToolGroup = QStringLiteral("Debug");
    bool m_syncingBottomToolGroup = false;
    bool m_isClosing = false;
    bool m_isShuttingDown = false;
    QComboBox* m_targetSelector = nullptr;
    QSpinBox* m_rankCountSpin = nullptr;
    QComboBox* m_executionScopeCombo = nullptr;
    QAction* m_executionScopeAction = nullptr;
    QAction* m_runAction = nullptr;
    QAction* m_continueAction = nullptr;
    QAction* m_stepOverAction = nullptr;
    QAction* m_stepIntoAction = nullptr;
    QAction* m_stepOutAction = nullptr;
    QAction* m_stepAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_pauseAllAction = nullptr;
    QAction* m_stopAction = nullptr;
    bool m_syncingScopeUi = false;
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
    bool m_suppressMemViewBroadcast = false;
    bool m_suppressExecutionTabSwitch = false;
    bool m_bottomConsoleMaximized = false;
    QString m_workspacePerspective = QStringLiteral("Source");
    QPushButton* m_perspSourceBtn = nullptr;
    QPushButton* m_perspDebugBtn = nullptr;
    QPushButton* m_perspBinaryBtn = nullptr;
    QPushButton* m_perspMpiBtn = nullptr;

    IBackendCoordinator* m_coordinator = nullptr;
    IBackendCoordinator* m_ownedCoordinator = nullptr;
    gridlock::core::LspCoordinator* m_lspCoordinator = nullptr;
    PlainJaneTooltip* m_plainJaneTooltip = nullptr;
    gridlock::core::HpcBackend* m_hpcBackend = nullptr;
    gridlock::core::DeadlockAnalyzer* m_deadlockAnalyzer = nullptr;
    SpackManager* m_spackManager = nullptr;

    int m_focusedRank = 0;
    std::unordered_map<int, RankState> m_latestStates;
    QString m_currentFile;
    QString m_activeWorkspaceDir;
    bool m_switchingWorkspace = false;
    struct BreakpointRankMeta { int rankFilter = -1; QString condition; };
    QMap<QString, QMap<int, BreakpointRankMeta>> m_breakpointRankMeta;
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
    struct SessionShutdownGuard {
        MainWindow* self = nullptr;
        ~SessionShutdownGuard() { if (self && !self->m_isShuttingDown && self->m_coordinator) self->shutdownActiveSessionSync(); }
    } m_shutdownGuard{this};
};

}  // namespace gridlock::ui
