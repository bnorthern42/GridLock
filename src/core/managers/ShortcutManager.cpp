/**
 * @file ShortcutManager.cpp
 * @brief Implementation of the multi-stroke keyboard chord state machine and debugger shortcuts.
 *
 * Registers standard execution control bindings (F5 Continue, F10 Step Over,
 * F11 Step Into, Alt+R Launch, Alt+B Toggle Breakpoint) and split-pane focus
 * chords (Ctrl+W, H and Ctrl+W, L), while ensuring unmodified printable keys
 * inside text inputs are never swallowed.
 */

#include "ShortcutManager.hpp"

#include <QDebug>

#include "../../ui/EditorTabManager.hpp"
#include "../../ui/MainWindow.hpp"
#include "../../ui/views/SourceCodeView.hpp"
#include "../../ui/widgets/ProjectExplorerWidget.hpp"
#include "../commands/DebugCommands.hpp"
#include "../hpc/DapCoordinator.hpp"
#include "../hpc/GdbRankCoordinator.hpp"
#include "../hpc/IBackendCoordinator.hpp"
#include "ConfigManager.hpp"

namespace gridlock::core {

/**
 * @brief Populates the shortcut action table bound to mainWindow.
 */
void ShortcutManager::initialize(gridlock::ui::MainWindow* mainWindow) {
    m_mainWindow = mainWindow;

    // F5: Resume execution on the currently focused MPI rank (0-based rank -> 1-based DAP threadId).
    m_actions[QKeySequence(Qt::Key_F5)] = [this]() {
        const int threadId = m_mainWindow->focusedRank() + 1;
        auto cmd = std::make_unique<commands::ContinueCommand>(m_mainWindow->coordinator(), threadId);
        m_mainWindow->executeCommand(std::move(cmd));
    };

    // F10: Step over the current source line on the focused MPI rank.
    m_actions[QKeySequence(Qt::Key_F10)] = [this]() {
        const int threadId = m_mainWindow->focusedRank() + 1;
        auto cmd = std::make_unique<commands::StepCommand>(m_mainWindow->coordinator(), threadId, false);
        m_mainWindow->executeCommand(std::move(cmd));
    };

    // F11: Step into the function call on the current source line.
    m_actions[QKeySequence(Qt::Key_F11)] = [this]() {
        const int threadId = m_mainWindow->focusedRank() + 1;
        auto cmd = std::make_unique<commands::StepCommand>(m_mainWindow->coordinator(), threadId, true);
        m_mainWindow->executeCommand(std::move(cmd));
    };

    // Ctrl+W, H: Focus the central source code editor tab manager.
    m_actions[QKeySequence("Ctrl+W, H")] = [this]() {
        if (m_mainWindow && m_mainWindow->editorTabManager()) {
            m_mainWindow->editorTabManager()->setFocus();
        }
    };

    // Ctrl+W, L: Focus the left project/symbol/strings explorer dock.
    m_actions[QKeySequence("Ctrl+W, L")] = [this]() {
        if (m_mainWindow && m_mainWindow->projectExplorerWidget()) {
            m_mainWindow->projectExplorerWidget()->setFocus();
        }
    };

    // Alt+R: Launch a parallel debugging session using the configured project binary and rank count.
    m_actions[QKeySequence("Alt+R")] = [this]() {
        const auto ps = ConfigManager::instance().loadProjectSettings();
        const auto target = ps.targetBinary.empty() ? "build/mpi_mm_bin" : ps.targetBinary;
        auto cmd = std::make_unique<commands::LaunchCommand>(
            m_mainWindow,
            QString::fromStdString(target),
            ConfigManager::instance().getDefaultRanks());
        m_mainWindow->executeCommand(std::move(cmd));
    };

    // Alt+B: Toggle a source-line breakpoint at the active editor cursor line.
    m_actions[QKeySequence("Alt+B")] = [this]() {
        if (m_mainWindow && m_mainWindow->editorTabManager()) {
            if (auto* view = m_mainWindow->editorTabManager()->currentSourceCodeView()) {
                const int lineNum = view->getCurrentLineNumber();
                const QString filePath = view->currentFile();
                auto* dapCoord = dynamic_cast<DapCoordinator*>(m_mainWindow->coordinator());
                auto cmd = std::make_unique<commands::ToggleBreakpointCommand>(dapCoord, filePath, lineNum);
                m_mainWindow->executeCommand(std::move(cmd));
            }
        }
    };
}

/**
 * @brief Evaluates a key press against registered single-key and multi-key chord sequences.
 */
bool ShortcutManager::handleKeyPress(QObject* watched, QKeyEvent* event) {
    if (!m_mainWindow) {
        return false;
    }

    // Ignore standalone modifier key presses so they do not corrupt in-progress chords.
    const int keyInt = event->key();
    if (keyInt == Qt::Key_unknown || keyInt == Qt::Key_Control ||
        keyInt == Qt::Key_Shift || keyInt == Qt::Key_Alt || keyInt == Qt::Key_Meta) {
        return false;
    }

    // Detect whether the focused widget is a text input or code editor surface.
    const bool isInputWidget = watched->inherits("QLineEdit") ||
                               watched->inherits("QPlainTextEdit") ||
                               watched->inherits("QTextEdit") ||
                               watched->inherits("gridlock::ui::SourceCodeView");

    const Qt::KeyboardModifiers mods = event->modifiers();
    const int keyWithMods = keyInt | mods;

    // Append the combined key+modifier bitmask to the active up-to-4-key chord buffer.
    m_currentChord.push_back(keyWithMods);

    const int k1 = m_currentChord.size() > 0 ? m_currentChord[0] : 0;
    const int k2 = m_currentChord.size() > 1 ? m_currentChord[1] : 0;
    const int k3 = m_currentChord.size() > 2 ? m_currentChord[2] : 0;
    const int k4 = m_currentChord.size() > 3 ? m_currentChord[3] : 0;

    const QKeySequence seq(k1, k2, k3, k4);

    // If the chord matches a registered action, execute it unless it's a plain character in an editor.
    if (m_actions.contains(seq)) {
        if (isInputWidget && m_currentChord.size() == 1 && mods == Qt::NoModifier && keyInt < Qt::Key_Escape) {
            m_currentChord.clear();
            return false;
        }

        m_actions[seq]();
        m_currentChord.clear();
        return true;
    }

    // Check whether the current key buffer is a valid prefix of any multi-key chord.
    bool isPrefix = false;
    for (auto it = m_actions.constBegin(); it != m_actions.constEnd(); ++it) {
        if (it.key().matches(seq) == QKeySequence::PartialMatch) {
            isPrefix = true;
            break;
        }
    }

    // If no registered chord starts with this sequence, reset the chord state and pass the event through.
    if (!isPrefix) {
        m_currentChord.clear();
        return false;
    }

    return true;
}

} // namespace gridlock::core
