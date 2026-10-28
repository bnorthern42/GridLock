/**
 * @file ShortcutManager.hpp
 * @brief Multi-key chord and function-key shortcut dispatcher for the GridLock workstation.
 *
 * Intercepts application-wide key press events via MainWindow::eventFilter() to
 * support single-key debugger controls (F5, F10, F11, Alt+R, Alt+B) as well as
 * multi-stroke pane navigation chords (e.g., Ctrl+W, H and Ctrl+W, L).
 */

#pragma once

#include <functional>
#include <vector>

#include <QHash>
#include <QKeyEvent>
#include <QKeySequence>
#include <QObject>

namespace gridlock::ui {
class MainWindow;
} // namespace gridlock::ui

namespace gridlock::core {

/**
 * @brief Singleton keyboard chord state machine and action router.
 */
class ShortcutManager : public QObject {
    Q_OBJECT
public:
    /** @brief Returns the process-wide ShortcutManager singleton instance. */
    static ShortcutManager& instance() {
        static ShortcutManager instance;
        return instance;
    }

    /**
     * @brief Binds default debugging and pane-focus shortcuts to the given MainWindow.
     * @param mainWindow Owning MainWindow instance used to dispatch IDebugCommand objects.
     */
    void initialize(gridlock::ui::MainWindow* mainWindow);

    /**
     * @brief Processes a key press event and advances or resets the active key chord.
     * @param watched Target QObject receiving the key press event.
     * @param event Key press event descriptor.
     * @return True if the key press matched a complete shortcut or valid chord prefix.
     */
    bool handleKeyPress(QObject* watched, QKeyEvent* event);

private:
    ShortcutManager() = default;
    ~ShortcutManager() override = default;

    std::vector<int> m_currentChord;
    gridlock::ui::MainWindow* m_mainWindow = nullptr;
    QHash<QKeySequence, std::function<void()>> m_actions;
};

} // namespace gridlock::core
