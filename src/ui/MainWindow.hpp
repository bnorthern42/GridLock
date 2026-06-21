#pragma once
#include <QMainWindow>
#include "../RankState.hpp"
#include <QCloseEvent>
#include <unordered_map>
#include <QSet>
#include <QMap>

namespace gridlock {
class GdbRankCoordinator;
}

namespace gridlock::ui {

class ServerRackView;
class SourceCodeView;
class DisassemblyView;
class ReferenceDock;
class DifferentialGrid;
class TerminalDock;
class GdbConsoleWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

    ServerRackView* serverRackView() const { return m_serverRackView; }
    SourceCodeView* sourceCodeView() const { return m_sourceCodeView; }
    SourceCodeView* getSourceCodeView() const { return m_sourceCodeView; }
    DisassemblyView* disassemblyView() const { return m_disassemblyView; }
    ReferenceDock* referenceDock() const { return m_referenceDock; }
    DifferentialGrid* differentialGrid() const { return m_differentialGrid; }
    TerminalDock* terminalDock() const { return m_terminalDock; }
    GdbConsoleWidget* gdbConsoleWidget() const { return m_gdbConsoleWidget; }

    void setCoordinator(gridlock::GdbRankCoordinator* coord);

public slots:
    void onRankStateChanged(int rankId, const RankState& state);
    void onRankSelected(int rankId);
    void openFile();
    void openPreferences();
    void loadSourceFile(const QString& filePath);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUi();
    void setupMenu();
    void setupToolbar();
    void setupDocks();

private slots:
    void startDebuggingSession(const QString& binaryPath, int ranks);

signals:
    void runTargetRequested();

private:
    ServerRackView* m_serverRackView = nullptr;
    SourceCodeView* m_sourceCodeView = nullptr;
    DisassemblyView* m_disassemblyView = nullptr;
    ReferenceDock* m_referenceDock = nullptr;
    DifferentialGrid* m_differentialGrid = nullptr;
    TerminalDock* m_terminalDock = nullptr;
    GdbConsoleWidget* m_gdbConsoleWidget = nullptr;

    gridlock::GdbRankCoordinator* m_coordinator = nullptr;

    int m_focusedRank = 0;
    std::unordered_map<int, RankState> m_latestStates;
    QString m_currentFile = "tests/mpi_mm.c";
    QMap<QString, QSet<int>> m_persistentBreakpoints;
};

} // namespace gridlock::ui
