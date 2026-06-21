#pragma once
#include <QObject>
#include <QKeySequence>
#include <QKeyEvent>
#include <QHash>
#include <functional>
#include <vector>

namespace gridlock::ui {
class MainWindow;
}

namespace gridlock::core {

class ShortcutManager : public QObject {
    Q_OBJECT
public:
    static ShortcutManager& instance() {
        static ShortcutManager instance;
        return instance;
    }

    void initialize(gridlock::ui::MainWindow* mainWindow);
    bool handleKeyPress(QObject* watched, QKeyEvent* event);

private:
    ShortcutManager() = default;
    ~ShortcutManager() override = default;

    std::vector<int> m_currentChord;
    gridlock::ui::MainWindow* m_mainWindow = nullptr;
    QHash<QKeySequence, std::function<void()>> m_actions;
};

} // namespace gridlock::core
