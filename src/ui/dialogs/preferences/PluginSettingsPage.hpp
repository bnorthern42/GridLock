/**
 * @file PluginSettingsPage.hpp
 * @brief Preferences page for inspecting, staging enablement/trust edits, and reloading native C99 plugins.
 *
 * Displays configured user and workspace plugin search paths with **Open Folder** and
 * **Reload Plugins** actions, an inline restart banner with a **Restart GridLock Now**
 * trigger, and a 7-column table (`Global`, `Workspace`, `Name`, `Version`, `Scope / ABI`,
 * `Status`, `Trust`) staging all edits in memory until `applyChanges()` is invoked.
 */

#pragma once

#include <QComboBox>
#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QWidget>

namespace gridlock::core::plugins {
class PluginManager;
}  // namespace gridlock::core::plugins

namespace gridlock::ui {

class MainWindow;

/**
 * @class PluginSettingsPage
 * @brief Preferences settings panel managing native GridLock C99 ABI plugins with staged Apply semantics.
 */
class PluginSettingsPage : public QWidget {
  Q_OBJECT

 public:
  /**
   * @brief Constructs the Plugins preferences page bound to @p pluginManager.
   * @param pluginManager Active host `PluginManager` (resolved from `MainWindow` if null).
   * @param parent Optional parent widget.
   */
  explicit PluginSettingsPage(gridlock::core::plugins::PluginManager* pluginManager = nullptr,
                              QWidget* parent = nullptr);

  /** @brief Destroys the page. */
  ~PluginSettingsPage() override = default;

  /** @brief Returns the underlying 7-column `QTableWidget` displaying discovered plugins. */
  QTableWidget* pluginTable() const noexcept { return m_pluginTable; }

  /** @brief Returns the search-directory selector combo box. */
  QComboBox* searchDirectoriesCombo() const noexcept { return m_searchDirsCombo; }

  /** @brief Returns the **Open Folder** button widget. */
  QPushButton* openFolderButton() const noexcept { return m_openFolderBtn; }

  /** @brief Returns the **Reload Plugins** button widget. */
  QPushButton* reloadPluginsButton() const noexcept { return m_reloadBtn; }

  /** @brief Returns the inline restart-required notification banner frame. */
  QFrame* restartBanner() const noexcept { return m_restartBanner; }

  /** @brief Returns the **Restart GridLock Now** button inside `restartBanner()`. */
  QPushButton* restartNowButton() const noexcept { return m_restartNowBtn; }

  /** @brief Returns true when committed plugin enablement or trust changes require an application restart. */
  bool isRestartRequired() const noexcept { return m_restartRequired; }

  /** @brief Repopulates search paths and plugin rows from `PluginManager` while preserving staged edits. */
  void refreshTable();

  /** @brief Discards staged edits, unloads active plugins, and rescans plugin directories immediately. */
  void reloadPlugins();

  /**
   * @brief Commits staged `Global`, `Workspace`, and `Trust` edits to `PluginManager` / `ConfigManager`.
   * @return True when one or more plugin enablement or trust states changed on disk.
   */
  bool applyChanges();

  /** @brief Flushes pending preferences/session state, spawns a detached GridLock instance, and quits. */
  void triggerRestartNow();

 signals:
  /** @brief Emitted when `applyChanges()` transitions the page into a restart-required state. */
  void restartRequiredChanged(bool required);

  /** @brief Emitted immediately before **Restart GridLock Now** relaunches the application. */
  void restartRequested();

 private:
  /**
   * @struct StagedPluginState
   * @brief In-memory staged checkbox and workspace trust state for a single plugin row prior to Apply.
   */
  struct StagedPluginState {
    QString pluginId;
    QString libraryPath;
    QString sha256Hex;
    bool isWorkspaceScope = false;
    bool globalEnabled = true;
    bool workspaceEnabled = true;
    bool workspaceTrusted = true;
  };

  gridlock::core::plugins::PluginManager* resolvePluginManager();
  MainWindow* resolveMainWindow() const;
  void updateRowPreview(int row, const QString& rowKey);
  void setRestartBannerVisible(bool visible);

  QPointer<gridlock::core::plugins::PluginManager> m_pluginManager;
  QComboBox* m_searchDirsCombo = nullptr;
  QPushButton* m_openFolderBtn = nullptr;
  QPushButton* m_reloadBtn = nullptr;
  QFrame* m_restartBanner = nullptr;
  QLabel* m_restartBannerLabel = nullptr;
  QPushButton* m_restartNowBtn = nullptr;
  QTableWidget* m_pluginTable = nullptr;
  QHash<QString, StagedPluginState> m_stagedStates;
  bool m_restartRequired = false;
};

}  // namespace gridlock::ui
