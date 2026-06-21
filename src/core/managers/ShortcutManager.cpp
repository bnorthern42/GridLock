#include "ShortcutManager.hpp"
#include "../../ui/MainWindow.hpp"
#include "../../ui/views/SourceCodeView.hpp"
#include "../../ui/widgets/ProjectExplorerWidget.hpp"
#include "../../ui/EditorTabManager.hpp"
#include "../hpc/GdbRankCoordinator.hpp"
#include "ConfigManager.hpp"
#include "../commands/DebugCommands.hpp"
#include <QDebug>

namespace gridlock::core {

void ShortcutManager::initialize(gridlock::ui::MainWindow* mainWindow) {
    m_mainWindow = mainWindow;

    m_actions[QKeySequence(Qt::Key_F5)] = [this]() {
        auto cmd = std::make_unique<commands::ContinueCommand>(m_mainWindow->coordinator());
        m_mainWindow->executeCommand(std::move(cmd));
    };

    m_actions[QKeySequence(Qt::Key_F10)] = [this]() {
        auto cmd = std::make_unique<commands::StepCommand>(m_mainWindow->coordinator());
        m_mainWindow->executeCommand(std::move(cmd));
    };

    m_actions[QKeySequence(Qt::Key_F11)] = [this]() {
        auto cmd = std::make_unique<commands::StepCommand>(m_mainWindow->coordinator());
        m_mainWindow->executeCommand(std::move(cmd));
    };

    m_actions[QKeySequence("Ctrl+W, H")] = [this]() {
        if (m_mainWindow && m_mainWindow->editorTabManager()) {
            m_mainWindow->editorTabManager()->setFocus();
        }
    };

    m_actions[QKeySequence("Ctrl+W, L")] = [this]() {
        if (m_mainWindow && m_mainWindow->projectExplorerWidget()) {
            m_mainWindow->projectExplorerWidget()->setFocus();
        }
    };

    m_actions[QKeySequence("Alt+R")] = [this]() {
        auto ps = ConfigManager::instance().loadProjectSettings();
        auto target = ps.targetBinary.empty() ? "build/mpi_mm_bin" : ps.targetBinary;
        auto cmd = std::make_unique<commands::LaunchCommand>(
            m_mainWindow, 
            QString::fromStdString(target), 
            ConfigManager::instance().getDefaultRanks());
        m_mainWindow->executeCommand(std::move(cmd));
    };

    m_actions[QKeySequence("Alt+B")] = [this]() {
        if (m_mainWindow && m_mainWindow->editorTabManager()) {
            if (auto view = m_mainWindow->editorTabManager()->currentSourceCodeView()) {
                int lineNum = view->getCurrentLineNumber();
                auto cmd = std::make_unique<commands::ToggleBreakpointCommand>(view, lineNum);
                m_mainWindow->executeCommand(std::move(cmd));
            }
        }
    };
}

bool ShortcutManager::handleKeyPress(QObject* watched, QKeyEvent* event) {
    if (!m_mainWindow) return false;

    int keyInt = event->key();
    if (keyInt == Qt::Key_unknown || keyInt == Qt::Key_Control || keyInt == Qt::Key_Shift || keyInt == Qt::Key_Alt || keyInt == Qt::Key_Meta) {
        return false;
    }

    bool isInputWidget = watched->inherits("QLineEdit") || watched->inherits("QPlainTextEdit") || watched->inherits("QTextEdit") || watched->inherits("gridlock::ui::SourceCodeView");
    
    Qt::KeyboardModifiers mods = event->modifiers();
    int keyWithMods = keyInt | mods;

    m_currentChord.push_back(keyWithMods);

    int k1 = m_currentChord.size() > 0 ? m_currentChord[0] : 0;
    int k2 = m_currentChord.size() > 1 ? m_currentChord[1] : 0;
    int k3 = m_currentChord.size() > 2 ? m_currentChord[2] : 0;
    int k4 = m_currentChord.size() > 3 ? m_currentChord[3] : 0;

    QKeySequence seq(k1, k2, k3, k4);

    if (m_actions.contains(seq)) {
        if (isInputWidget && m_currentChord.size() == 1 && mods == Qt::NoModifier && keyInt < Qt::Key_Escape) {
            m_currentChord.clear();
            return false;
        }

        m_actions[seq]();
        m_currentChord.clear();
        return true;
    }

    bool isPrefix = false;
    for (auto it = m_actions.constBegin(); it != m_actions.constEnd(); ++it) {
        if (it.key().matches(seq) == QKeySequence::PartialMatch) {
            isPrefix = true;
            break;
        }
    }

    if (!isPrefix) {
        m_currentChord.clear();
        return false;
    }

    return true; 
}

} // namespace gridlock::core
