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
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMap>
#include <QPushButton>
#include <QSet>
#include <QString>
#include <QVector>
#include <memory>
#include <unordered_map>

class IBackendCoordinator;
class QVulkanInstance;

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
    QTabWidget* m_bottomTabs = nullptr;
    QAction* m_runAction = nullptr;
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
};

}  // namespace gridlock::ui
