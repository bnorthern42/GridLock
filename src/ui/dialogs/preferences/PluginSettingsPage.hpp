/**
 * @file PluginSettingsPage.hpp
 * @brief Preferences page for inspecting, enabling/disabling, and reloading native C99 plugins.
 *
 * Displays configured user and workspace plugin search paths with an **Open Folder**
 * action, a table of discovered plugins (`Enabled`, `Name`, `Version`, `ABI`, `Status`),
 * and a live **Reload Plugins** trigger backed by `PluginManager` and `ConfigManager`.
 */

#pragma once

#include <QComboBox>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

namespace gridlock::core::plugins {
class PluginManager;
}  // namespace gridlock::core::plugins

namespace gridlock::ui {

/**
 * @class PluginSettingsPage
 * @brief Preferences settings panel managing native GridLock C99 ABI plugins.
 */
class PluginSettingsPage : public QWidget {
  Q_OBJECT

 public:
  /**
   * @brief Constructs the Plugins preferences page.
   * @param pluginManager Active `PluginManager` (falls back to an owned manager when null).
   * @param parent Optional parent widget.
   */
  explicit PluginSettingsPage(gridlock::core::plugins::PluginManager* pluginManager = nullptr,
                              QWidget* parent = nullptr);

  /** @brief Destroys the page. */
  ~PluginSettingsPage() override = default;

  /** @brief Returns the underlying `QTableWidget` displaying discovered plugins. */
  QTableWidget* pluginTable() const noexcept { return m_pluginTable; }

  /** @brief Returns the search-directory selector combo box. */
  QComboBox* searchDirectoriesCombo() const noexcept { return m_searchDirsCombo; }

  /** @brief Returns the **Open Folder** button widget. */
  QPushButton* openFolderButton() const noexcept { return m_openFolderBtn; }

  /** @brief Returns the **Reload Plugins** button widget. */
  QPushButton* reloadPluginsButton() const noexcept { return m_reloadBtn; }

  /** @brief Repopulates search paths and plugin rows from `PluginManager`. */
  void refreshTable();

  /** @brief Unloads and rescans plugin directories immediately without restarting GridLock. */
  void reloadPlugins();

  /** @brief Flushes any pending checkbox states to `PluginManager` / `ConfigManager`. */
  void applyChanges();

 private:
  gridlock::core::plugins::PluginManager* resolvePluginManager();

  QPointer<gridlock::core::plugins::PluginManager> m_pluginManager;
  gridlock::core::plugins::PluginManager* m_ownedFallbackManager = nullptr;
  QComboBox* m_searchDirsCombo = nullptr;
  QPushButton* m_openFolderBtn = nullptr;
  QPushButton* m_reloadBtn = nullptr;
  QTableWidget* m_pluginTable = nullptr;
};

}  // namespace gridlock::ui
