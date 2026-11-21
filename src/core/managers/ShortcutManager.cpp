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
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>

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

QString ShortcutManager::sanitizeActionLabel(const QString& rawText) {
  QString out;
  out.reserve(rawText.size());
  for (int i = 0; i < rawText.size(); ++i) {
    if (rawText[i] == QLatin1Char('&')) {
      if (i + 1 < rawText.size() && rawText[i + 1] == QLatin1Char('&')) {
        out.append(QLatin1Char('&'));
        ++i;
      } else if (i > 0 && i + 1 < rawText.size() && rawText[i - 1].isSpace() && rawText[i + 1].isSpace()) {
        out.append(QLatin1Char('&'));
      }
      continue;
    }
    out.append(rawText[i] == QLatin1Char('_') ? QLatin1Char(' ') : rawText[i]);
  }
  out = out.simplified();
  if (out.endsWith(QStringLiteral("..."))) out.chop(3);
  return out.trimmed();
}

void ShortcutManager::registerChordBinding(const QString& id, const QString& category, const QString& label,
                                           const QKeySequence& defaultSeq, std::function<void()> cb) {
  ActionBinding b;
  b.id = id;
  b.category = category;
  b.label = sanitizeActionLabel(label);
  b.defaultShortcut = defaultSeq;
  b.currentShortcut = m_customOverrides.contains(id) ? m_customOverrides.value(id) : defaultSeq;
  b.callback = std::move(cb);
  b.isMenuAction = false;
  m_indexById.insert(id, m_bindings.size());
  m_bindings.append(std::move(b));
}

void ShortcutManager::rebuildChordTable() {
  m_actions.clear();
  for (const ActionBinding& b : m_bindings) {
    if (!b.isMenuAction && b.callback && !b.currentShortcut.isEmpty()) {
      m_actions.insert(b.currentShortcut, b.callback);
    }
  }
}

void ShortcutManager::initialize(gridlock::ui::MainWindow* mainWindow) {
  m_mainWindow = mainWindow;
  m_currentChord.clear();
  m_bindings.clear();
  m_indexById.clear();
  m_customOverrides = ConfigManager::instance().customShortcuts();

  registerChordBinding(QStringLiteral("debug.continue"), QStringLiteral("Debug"),
                       QStringLiteral("Continue Execution"), QKeySequence(Qt::Key_F5), [this]() {
                         if (!m_mainWindow) return;
                         if (!m_mainWindow->continueExecutionWorkflow() && m_mainWindow->coordinator()) {
                           const int threadId = m_mainWindow->focusedRank() + 1;
                           auto cmd = std::make_unique<commands::ContinueCommand>(m_mainWindow->coordinator(), threadId);
                           m_mainWindow->executeCommand(std::move(cmd));
                         }
                       });

  registerChordBinding(QStringLiteral("debug.step_over"), QStringLiteral("Debug"),
                       QStringLiteral("Step Over"), QKeySequence(Qt::Key_F10), [this]() {
                         if (m_mainWindow) m_mainWindow->stepOverWorkflow();
                       });

  registerChordBinding(QStringLiteral("debug.step_into"), QStringLiteral("Debug"),
                       QStringLiteral("Step Into"), QKeySequence(Qt::Key_F11), [this]() {
                         if (m_mainWindow) m_mainWindow->stepIntoWorkflow();
                       });

  registerChordBinding(QStringLiteral("debug.step_out"), QStringLiteral("Debug"),
                       QStringLiteral("Step Out"), QKeySequence(Qt::SHIFT | Qt::Key_F11), [this]() {
                         if (m_mainWindow) m_mainWindow->stepOutWorkflow();
                       });

  registerChordBinding(QStringLiteral("debug.stop"), QStringLiteral("Debug"),
                       QStringLiteral("Stop Session"), QKeySequence(Qt::SHIFT | Qt::Key_F5), [this]() {
                         if (m_mainWindow) m_mainWindow->stopTargetWorkflow();
                       });

  registerChordBinding(QStringLiteral("file.switch_header_source"), QStringLiteral("File"),
                       QStringLiteral("Switch Header/Source"), QKeySequence(Qt::Key_F4), [this]() {
                         if (m_mainWindow && m_mainWindow->editorTabManager()) m_mainWindow->editorTabManager()->switchHeaderSource();
                       });

  registerChordBinding(QStringLiteral("debug.launch_session"), QStringLiteral("Debug"),
                       QStringLiteral("Launch Parallel Session"), QKeySequence(QStringLiteral("Alt+R")), [this]() {
                         if (!m_mainWindow) return;
                         const auto ps = ConfigManager::instance().loadProjectSettings();
                         const auto target = ps.targetBinary.empty() ? "build/mpi_mm_bin" : ps.targetBinary;
                         auto cmd = std::make_unique<commands::LaunchCommand>(
                             m_mainWindow, QString::fromStdString(target), ConfigManager::instance().getDefaultRanks());
                         m_mainWindow->executeCommand(std::move(cmd));
                       });

  registerChordBinding(QStringLiteral("debug.toggle_breakpoint"), QStringLiteral("Debug"),
                       QStringLiteral("Toggle Line Breakpoint"), QKeySequence(QStringLiteral("Alt+B")), [this]() {
                         if (m_mainWindow && m_mainWindow->editorTabManager()) {
                           if (auto* view = m_mainWindow->editorTabManager()->currentSourceCodeView()) {
                             auto* dapCoord = dynamic_cast<DapCoordinator*>(m_mainWindow->coordinator());
                             auto cmd = std::make_unique<commands::ToggleBreakpointCommand>(
                                 dapCoord, view->currentFile(), view->getCurrentLineNumber());
                             m_mainWindow->executeCommand(std::move(cmd));
                           }
                         }
                       });

  registerChordBinding(QStringLiteral("nav.focus_editor"), QStringLiteral("Navigation"),
                       QStringLiteral("Focus Source Editor"), QKeySequence(QStringLiteral("Ctrl+W, H")), [this]() {
                         if (m_mainWindow && m_mainWindow->editorTabManager()) {
                           if (auto* sv = m_mainWindow->editorTabManager()->currentSourceCodeView()) sv->setFocus();
                           else m_mainWindow->editorTabManager()->setFocus();
                         }
                       });

  registerChordBinding(QStringLiteral("nav.focus_explorer"), QStringLiteral("Navigation"),
                       QStringLiteral("Focus Project Explorer"), QKeySequence(QStringLiteral("Ctrl+W, L")), [this]() {
                         if (m_mainWindow && m_mainWindow->projectExplorerWidget()) m_mainWindow->projectExplorerWidget()->setFocus();
                       });

  registerChordBinding(QStringLiteral("nav.goto_address"), QStringLiteral("Navigation"),
                       QStringLiteral("Go to Address / Symbol"), QKeySequence(Qt::CTRL | Qt::Key_G), [this]() {
                         if (m_mainWindow) m_mainWindow->triggerEditMenuAction(QStringLiteral("Go to Address"));
                       });

  rebuildChordTable();
  syncMenuActionsFromMainWindow();
}

void ShortcutManager::syncMenuActionsFromMainWindow() {
  if (!m_mainWindow || !m_mainWindow->variablesDockWidget() || !m_mainWindow->menuBar()) return;
  m_customOverrides = ConfigManager::instance().customShortcuts();
  static const QRegularExpression kNonAlnum(QStringLiteral("[^a-z0-9]+"));

  auto walkMenu = [&](auto& self, QMenu* menu, const QString& category) -> void {
    if (!menu) return;
    const bool isRecentMenu = (menu->objectName() == QStringLiteral("menuRecentSessions"));
    for (QAction* act : menu->actions()) {
      if (!act || act->isSeparator()) continue;
      if (QMenu* sub = act->menu()) {
        if (act->text().contains(QLatin1Char('_'))) {
          QString cleanSub = act->text();
          cleanSub.replace(QLatin1Char('_'), QLatin1Char(' '));
          act->setText(cleanSub);
        }
        const QString subCat = sanitizeActionLabel(act->text());
        self(self, sub, category.isEmpty() ? subCat : category);
        continue;
      }
      // Ensure zero raw underscores or ALL-CAPS dock toggle titles remain in menu labels.
      QString rawText = act->text();
      if (rawText.contains(QLatin1Char('_'))) {
        rawText.replace(QLatin1Char('_'), QLatin1Char(' '));
        act->setText(rawText);
      } else if (act->isCheckable() && rawText == QStringLiteral("WATCH")) {
        rawText = QStringLiteral("Watch");
        act->setText(rawText);
      } else if (act->isCheckable() && rawText == QStringLiteral("CALL STACK")) {
        rawText = QStringLiteral("Call Stack");
        act->setText(rawText);
      }
      if (isRecentMenu && rawText != QStringLiteral("Clear Recent Sessions")) continue;

      const QString cleanLabel = sanitizeActionLabel(rawText);
      if (cleanLabel.isEmpty()) continue;
      QString id = act->objectName().trimmed();
      if (id.isEmpty()) {
        QString catSlug = category.toLower();
        catSlug.replace(kNonAlnum, QStringLiteral("."));
        QString lblSlug = cleanLabel.toLower();
        lblSlug.replace(kNonAlnum, QStringLiteral("."));
        id = QStringLiteral("menu.%1.%2").arg(catSlug, lblSlug);
      }
      if (m_indexById.contains(id)) {
        ActionBinding& existing = m_bindings[m_indexById.value(id)];
        if (existing.boundAction != act) {
          existing.boundAction = act;
          if (existing.defaultShortcut.isEmpty() && !act->shortcut().isEmpty()) existing.defaultShortcut = act->shortcut();
        }
        existing.label = cleanLabel;
        existing.category = category;
        existing.currentShortcut = m_customOverrides.contains(id) ? m_customOverrides.value(id) : existing.defaultShortcut;
        if (act->shortcut() != existing.currentShortcut) act->setShortcut(existing.currentShortcut);
      } else {
        ActionBinding b;
        b.id = id;
        b.category = category;
        b.label = cleanLabel;
        b.defaultShortcut = act->shortcut();
        b.currentShortcut = m_customOverrides.contains(id) ? m_customOverrides.value(id) : b.defaultShortcut;
        b.boundAction = act;
        b.isMenuAction = true;
        if (act->shortcut() != b.currentShortcut) act->setShortcut(b.currentShortcut);
        m_indexById.insert(id, m_bindings.size());
        m_bindings.append(std::move(b));
      }
    }
  };

  for (QAction* topAct : m_mainWindow->menuBar()->actions()) {
    if (!topAct || !topAct->menu()) continue;
    walkMenu(walkMenu, topAct->menu(), sanitizeActionLabel(topAct->text()));
  }
}

QKeySequence ShortcutManager::shortcutForAction(const QString& actionId) const {
  if (!m_indexById.contains(actionId)) return {};
  return m_bindings[m_indexById.value(actionId)].currentShortcut;
}

bool ShortcutManager::setShortcutForAction(const QString& actionId, const QKeySequence& sequence, bool persist) {
  if (!m_indexById.contains(actionId)) return false;
  ActionBinding& b = m_bindings[m_indexById.value(actionId)];
  b.currentShortcut = sequence;
  if (b.boundAction) b.boundAction->setShortcut(sequence);
  if (sequence == b.defaultShortcut) m_customOverrides.remove(actionId);
  else m_customOverrides.insert(actionId, sequence);
  rebuildChordTable();
  if (persist) ConfigManager::instance().setCustomShortcuts(m_customOverrides);
  emit shortcutsChanged();
  return true;
}

void ShortcutManager::resetAllToDefaults(bool persist) {
  m_customOverrides.clear();
  for (ActionBinding& b : m_bindings) {
    b.currentShortcut = b.defaultShortcut;
    if (b.boundAction) b.boundAction->setShortcut(b.defaultShortcut);
  }
  rebuildChordTable();
  if (persist) ConfigManager::instance().setCustomShortcuts({});
  emit shortcutsChanged();
}

bool ShortcutManager::handleKeyPress(QObject* watched, QKeyEvent* event) {
  if (!m_mainWindow || !watched) return false;
  if (watched->inherits("QKeySequenceEdit") || (watched->parent() && watched->parent()->inherits("QKeySequenceEdit"))) {
    return false;
  }

  const int keyInt = event->key();
  if (keyInt == Qt::Key_unknown || keyInt == Qt::Key_Control || keyInt == Qt::Key_Shift ||
      keyInt == Qt::Key_Alt || keyInt == Qt::Key_Meta) {
    return false;
  }

  const bool isInputWidget = watched->inherits("QLineEdit") || watched->inherits("QPlainTextEdit") ||
                             watched->inherits("QTextEdit") || watched->inherits("gridlock::ui::SourceCodeView");
  const Qt::KeyboardModifiers mods = event->modifiers();
  m_currentChord.push_back(keyInt | mods);

  const int k1 = m_currentChord.size() > 0 ? m_currentChord[0] : 0;
  const int k2 = m_currentChord.size() > 1 ? m_currentChord[1] : 0;
  const int k3 = m_currentChord.size() > 2 ? m_currentChord[2] : 0;
  const int k4 = m_currentChord.size() > 3 ? m_currentChord[3] : 0;
  const QKeySequence seq(k1, k2, k3, k4);

  if (m_actions.contains(seq)) {
    if (isInputWidget && m_currentChord.size() == 1 && mods == Qt::NoModifier && keyInt < Qt::Key_Escape) {
      m_currentChord.clear();
      return false;
    }
    m_actions[seq]();
    m_currentChord.clear();
    return true;
  }

  for (auto it = m_actions.constBegin(); it != m_actions.constEnd(); ++it) {
    if (it.key().matches(seq) == QKeySequence::PartialMatch) return true;
  }
  m_currentChord.clear();
  return false;
}

}  // namespace gridlock::core
