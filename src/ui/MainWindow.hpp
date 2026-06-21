#pragma once
#include <QMainWindow>
#include "../RankState.hpp"
#include <QCloseEvent>
#include <unordered_map>
#include <QSet>
#include <QMap>
#include <memory>

namespace gridlock {
class GdbRankCoordinator;
}

namespace gridlock::core {
class LspCoordinator;
class HpcBackend;
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

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

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
    gridlock::GdbRankCoordinator* coordinator() const { return m_coordinator; }

    void startDebuggingSession(const QString& binaryPath, int ranks);
    void executeCommand(std::unique_ptr<gridlock::core::commands::IDebugCommand> cmd);

    void setCoordinator(gridlock::GdbRankCoordinator* coord);

public slots:
    void onRankStateChanged(int rankId, const RankState& state);
    void onRankSelected(int rankId);
    void openFile();
    void openPreferences();
    void loadSourceFile(const QString& filePath);

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
    QTabWidget* m_bottomTabs = nullptr;

    gridlock::GdbRankCoordinator* m_coordinator = nullptr;
    gridlock::core::LspCoordinator* m_lspCoordinator = nullptr;
    gridlock::core::HpcBackend* m_hpcBackend = nullptr;
    SpackManager* m_spackManager = nullptr;

    int m_focusedRank = 0;
    std::unordered_map<int, RankState> m_latestStates;
    QString m_currentFile = "tests/mpi_mm.c";
    QMap<QString, QSet<int>> m_persistentBreakpoints;
};

} // namespace gridlock::ui
