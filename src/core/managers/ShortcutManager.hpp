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

#include <QAction>
#include <QHash>
#include <QKeyEvent>
#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>

namespace gridlock::ui {
class MainWindow;
}  // namespace gridlock::ui

namespace gridlock::core {

/**
 * @brief Singleton keyboard chord state machine, menu action registry, and keybinding manager.
 */
class ShortcutManager : public QObject {
  Q_OBJECT
 public:
  /** @brief Metadata and live binding descriptor for a customizable workstation action. */
  struct ActionBinding {
    QString id;                         ///< Stable identifier (e.g., "actionCommandPalette", "debug.continue").
    QString category;                   ///< Grouping category ("File", "Edit", "View", "Debug", "Tools", "Navigation").
    QString label;                      ///< Clean spaced, title-cased human-readable action title.
    QKeySequence defaultShortcut;       ///< Built-in default shortcut sequence.
    QKeySequence currentShortcut;       ///< Active shortcut sequence (default or user override).
    QPointer<QAction> boundAction;      ///< Optional live QAction updated via QAction::setShortcut().
    std::function<void()> callback;     ///< Optional chord/key handler for non-menu actions.
    bool isMenuAction = false;          ///< True when synchronized from MainWindow::menuBar().
  };

  /** @brief Returns the process-wide ShortcutManager singleton instance. */
  static ShortcutManager& instance() {
    static ShortcutManager instance;
    return instance;
  }

  /** @brief Binds default debugging/navigation chords and synchronizes menu actions for @p mainWindow. */
  void initialize(gridlock::ui::MainWindow* mainWindow);

  /** @brief Synchronizes all static QActions in MainWindow::menuBar() and applies persisted shortcut overrides. */
  void syncMenuActionsFromMainWindow();

  /** @brief Processes a key press event and advances or resets the active key chord. */
  bool handleKeyPress(QObject* watched, QKeyEvent* event);

  /** @brief Returns all registered customizable action bindings in display order. */
  const QList<ActionBinding>& registeredBindings() const { return m_bindings; }

  /** @brief Returns the active shortcut for @p actionId, or an empty sequence if unbound. */
  QKeySequence shortcutForAction(const QString& actionId) const;

  /** @brief Updates the active shortcut for @p actionId, live-updating any bound QAction and optionally persisting. */
  bool setShortcutForAction(const QString& actionId, const QKeySequence& sequence, bool persist = true);

  /** @brief Restores all registered bindings to their built-in defaults and optionally clears persisted overrides. */
  void resetAllToDefaults(bool persist = true);

  /** @brief Sanitizes a raw menu/action title into clean spaced English without mnemonics or underscores. */
  static QString sanitizeActionLabel(const QString& rawText);

 signals:
  /** @brief Emitted whenever one or more shortcut bindings are customized or reset. */
  void shortcutsChanged();

 private:
  ShortcutManager() = default;
  ~ShortcutManager() override = default;

  void registerChordBinding(const QString& id, const QString& category, const QString& label,
                            const QKeySequence& defaultSeq, std::function<void()> cb);
  void rebuildChordTable();

  std::vector<int> m_currentChord;
  QPointer<gridlock::ui::MainWindow> m_mainWindow;
  QList<ActionBinding> m_bindings;
  QHash<QString, int> m_indexById;
  QMap<QString, QKeySequence> m_customOverrides;
  QHash<QKeySequence, std::function<void()>> m_actions;
};

}  // namespace gridlock::core
