#pragma once
#include <QDockWidget>
#include <QListWidget>

namespace gridlock::core {
struct DeadlockInfo;
}

namespace gridlock::ui {

class DeadlockDockWidget : public QDockWidget {
    Q_OBJECT
public:
    explicit DeadlockDockWidget(QWidget* parent = nullptr);

signals:
    void jumpToFrameRequested(int rankId);

public slots:
    void onDeadlockDetected(const gridlock::core::DeadlockInfo& info);
    void onRankCleared(int rankId);

private slots:
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    QListWidget* m_listWidget;
};

} // namespace gridlock::ui
