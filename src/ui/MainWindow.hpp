#pragma once
#include <QMainWindow>
#include "../RankState.hpp"
#include <QCloseEvent>
#include <unordered_map>

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

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

    ServerRackView* serverRackView() const { return m_serverRackView; }
    SourceCodeView* sourceCodeView() const { return m_sourceCodeView; }
    DisassemblyView* disassemblyView() const { return m_disassemblyView; }
    ReferenceDock* referenceDock() const { return m_referenceDock; }
    DifferentialGrid* differentialGrid() const { return m_differentialGrid; }
    TerminalDock* terminalDock() const { return m_terminalDock; }

    void setCoordinator(gridlock::GdbRankCoordinator* coord) { m_coordinator = coord; }

public slots:
    void onRankStateChanged(int rankId, const RankState& state);
    void onRankSelected(int rankId);
    void openFile();
    void openPreferences();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUi();
    void setupMenu();
    void setupToolbar();
    void setupDocks();

signals:
    void runTargetRequested();

private:
    ServerRackView* m_serverRackView = nullptr;
    SourceCodeView* m_sourceCodeView = nullptr;
    DisassemblyView* m_disassemblyView = nullptr;
    ReferenceDock* m_referenceDock = nullptr;
    DifferentialGrid* m_differentialGrid = nullptr;
    TerminalDock* m_terminalDock = nullptr;

    gridlock::GdbRankCoordinator* m_coordinator = nullptr;

    int m_focusedRank = 0;
    std::unordered_map<int, RankState> m_latestStates;
    QString m_currentFile = "tests/matrix_multiply.cpp";
};

} // namespace gridlock::ui
