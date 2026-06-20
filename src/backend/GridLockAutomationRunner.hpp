#pragma once
#include <QObject>
#include <QTimer>
#include <memory>
#include <QString>
#include "../GdbRankCoordinator.hpp"

namespace gridlock::ui {
class MainWindow;
}

namespace gridlock::backend {

class GridLockAutomationRunner : public QObject {
    Q_OBJECT
public:
    explicit GridLockAutomationRunner(ui::MainWindow* mainWindow, QObject* parent = nullptr);
    ~GridLockAutomationRunner() override = default;

public slots:
    void runNextStep();
    void startTestSequence();

private slots:
    void onRankStateChanged(int rankId, const RankState& state);

private:
    bool generateAndCompileTestTarget();
    ui::MainWindow* m_mainWindow;
    std::unique_ptr<GdbRankCoordinator> m_coordinator;
    QTimer m_timer;
    int m_step = 0;
    int m_rankCount = 4;
    QString m_testSourceCode;
};

} // namespace gridlock::backend
