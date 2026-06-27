#pragma once
#include <QMainWindow>
#include "../RankState.hpp"
#include <QCloseEvent>
#include <unordered_map>
#include <QSet>
#include <QMap>
#include <memory>

class IBackendCoordinator;
class QVulkanInstance;

namespace gridlock {
class GdbRankCoordinator;
class VariablesDockWidget;
}

namespace gridlock::core {
class LspCoordinator;
class HpcBackend;
class DeadlockAnalyzer;
}

namespace gridlock::core::commands {
class IDebugCommand;
}

namespace gridlock::ui {

class ServerRackView;
class SourceCodeView;
class EditorTabManager;
class DisassemblyView;
class ReferenceManualWidget;
class DifferentialGrid;
class TerminalDock;
class ProjectExplorerWidget;
class GdbConsoleWidget;
class MemView;
class RegisterView;
class SpackManager;
class ExpressionEvaluatorWidget;

class MpiDiagnosticsWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

    void setVulkanInstance(QVulkanInstance *inst);

    ServerRackView* serverRackView() const { return m_serverRackView; }
    EditorTabManager* editorTabManager() const { return m_editorTabManager; }
    SourceCodeView* sourceCodeView() const;
    SourceCodeView* getSourceCodeView() const;
    DisassemblyView* disassemblyView() const { return m_disassemblyView; }
    ReferenceManualWidget* referenceManualWidget() const { return m_referenceManualWidget; }
    DifferentialGrid* differentialGrid() const { return m_differentialGrid; }
    TerminalDock* terminalDock() const { return m_terminalDock; }
    GdbConsoleWidget* gdbConsoleWidget() const { return m_gdbConsoleWidget; }
    MemView* memView() const { return m_memView; }
    RegisterView* registerView() const { return m_registerView; }
    ProjectExplorerWidget* projectExplorerWidget() const { return m_projectExplorerWidget; }

    gridlock::VariablesDockWidget* variablesDockWidget() const { return m_variablesDockWidget; }
    MpiDiagnosticsWidget* mpiDiagnosticsWidget() const { return m_mpiDiagnosticsWidget; }
    IBackendCoordinator* coordinator() const { return m_coordinator; }
    int focusedRank() const { return m_focusedRank; }

    void startDebuggingSession(const QString& binaryPath, int ranks);
    void executeCommand(std::unique_ptr<gridlock::core::commands::IDebugCommand> cmd);

    void setCoordinator(IBackendCoordinator* coord);

public slots:
    void onRankStateChanged(int rankId, const RankState& state);
    void onRankSelected(int rankId);
    void openFile();
    void openPreferences();
    void loadSourceFile(const QString& filePath);
    void saveSessionAs();
    void loadSession();
    void onTutorialLaunchRequested(const QString& absoluteFilePath);


protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUi();
    void setupMenu();
    void setupToolbar();
    void setupDocks();



signals:
    void runTargetRequested();

private:
    ServerRackView* m_serverRackView = nullptr;
    EditorTabManager* m_editorTabManager = nullptr;
    DisassemblyView* m_disassemblyView = nullptr;
    ReferenceManualWidget* m_referenceManualWidget = nullptr;
    DifferentialGrid* m_differentialGrid = nullptr;
    ProjectExplorerWidget* m_projectExplorerWidget = nullptr;
    TerminalDock* m_terminalDock = nullptr;
    GdbConsoleWidget* m_gdbConsoleWidget = nullptr;
    MemView* m_memView = nullptr;
    RegisterView* m_registerView = nullptr;
    ExpressionEvaluatorWidget* m_expressionEvaluatorWidget = nullptr;

    gridlock::VariablesDockWidget* m_variablesDockWidget = nullptr;
    MpiDiagnosticsWidget* m_mpiDiagnosticsWidget = nullptr;
    QTabWidget* m_bottomTabs = nullptr;
    QAction* m_runAction = nullptr;

    IBackendCoordinator* m_coordinator = nullptr;
    gridlock::core::LspCoordinator* m_lspCoordinator = nullptr;
    gridlock::core::HpcBackend* m_hpcBackend = nullptr;
    gridlock::core::DeadlockAnalyzer* m_deadlockAnalyzer = nullptr;
    SpackManager* m_spackManager = nullptr;

    int m_focusedRank = 0;
    std::unordered_map<int, RankState> m_latestStates;
    QString m_currentFile;
    QMap<QString, QSet<int>> m_persistentBreakpoints;
};

} // namespace gridlock::ui
