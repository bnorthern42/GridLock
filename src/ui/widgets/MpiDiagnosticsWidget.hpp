#pragma once
#include <QWidget>
#include <QTabWidget>
#include <QListWidget>

namespace gridlock::core {
struct DeadlockInfo;
}

namespace gridlock::ui {

class MpiDiagnosticsWidget : public QWidget {
    Q_OBJECT
public:
    explicit MpiDiagnosticsWidget(QWidget* parent = nullptr);

signals:
    void jumpToFrameRequested(int rankId);

public slots:
    void onDeadlockDetected(const gridlock::core::DeadlockInfo& info);
    void onRankCleared(int rankId);

private slots:
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    QTabWidget* m_tabWidget;
    QListWidget* m_deadlockList;
};

} // namespace gridlock::ui
