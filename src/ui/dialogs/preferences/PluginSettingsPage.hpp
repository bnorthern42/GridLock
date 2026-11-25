/**
 * @file PluginSettingsPage.hpp
 * @brief Two-tab Preferences -> Plugins page for installed plugin management, multi-forge Git builds, and `plugins.toml` sync.
 *
 * Organizes plugin administration into two tabs inside `#pluginSettingsTabs`:
 * 1. **Installed**: Search-directory selector, **Open Folder** / **Reload Plugins**,
 *    staged **Global** / **Workspace** checkboxes, workspace SHA-256 **Trust & Approve**,
 *    and an inline **Restart GridLock Now** notification banner.
 * 2. **Install from Git** (`#pluginInstallTab`): **Git Host** selector (`#pluginGitHostCombo`:
 *    GitHub, GitLab, Codeberg, or Custom Git URL), **Repository / URL** input (`#pluginGitRepoEdit`),
 *    optional branch/tag/SHA ref, **Global** vs **This Workspace** scope selector,
 *    **Clone & Build**, **Sync `plugins.toml`**, **Open `plugins.toml`**, and live compiler diagnostics.
 */

#pragma once

#include <QComboBox>
#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QTabWidget>
#include <QTableWidget>
#include <QWidget>

namespace gridlock::core::plugins {
class PluginInstaller;
class PluginManager;
struct PluginBuildResult;
}  // namespace gridlock::core::plugins

namespace gridlock::ui {

class MainWindow;

/**
 * @class PluginSettingsPage
 * @brief Preferences settings panel managing installed C99 ABI plugins, multi-forge Git builds, and `plugins.toml` sync.
 */
class PluginSettingsPage : public QWidget {
  Q_OBJECT

 public:
  /**
   * @brief Constructs the two-tab Plugins preferences page.
   * @param pluginManager Active host `PluginManager` (resolved from `MainWindow` if null).
   * @param parent Optional parent widget.
   */
  explicit PluginSettingsPage(gridlock::core::plugins::PluginManager* pluginManager = nullptr,
                              QWidget* parent = nullptr);

  /** @brief Destroys the page. */
  ~PluginSettingsPage() override = default;

  /** @brief Returns the two-tab container (`Installed` and `Install from Git`). */
  QTabWidget* tabWidget() const noexcept { return m_tabs; }

  /** @brief Returns the 7-column `QTableWidget` displaying discovered plugins. */
  QTableWidget* pluginTable() const noexcept { return m_pluginTable; }

  /** @brief Returns the search-directory selector combo box. */
  QComboBox* searchDirectoriesCombo() const noexcept { return m_searchDirsCombo; }

  /** @brief Returns the **Open Folder** button widget. */
  QPushButton* openFolderButton() const noexcept { return m_openFolderBtn; }

  /** @brief Returns the **Reload Plugins** button widget. */
  QPushButton* reloadPluginsButton() const noexcept { return m_reloadBtn; }

  /** @brief Returns the inline restart-required notification banner frame on the `Installed` tab. */
  QFrame* restartBanner() const noexcept { return m_restartBanner; }

  /** @brief Returns the **Restart GridLock Now** button inside `restartBanner()`. */
  QPushButton* restartNowButton() const noexcept { return m_restartNowBtn; }

  /** @brief Returns the **Git Host** selector combo box (`#pluginGitHostCombo`). */
  QComboBox* gitHostCombo() const noexcept { return m_gitHostCombo; }

  /** @brief Returns the **Repository / URL** input (`#pluginGitRepoEdit`). */
  QLineEdit* gitRepoEdit() const noexcept { return m_repoUrlEdit; }

  /** @brief Backward-compatible alias returning `gitRepoEdit()`. */
  QLineEdit* githubUrlEdit() const noexcept { return m_repoUrlEdit; }

  /** @brief Returns the optional git branch/tag/SHA input on the `Install from Git` tab. */
  QLineEdit* gitRefEdit() const noexcept { return m_gitRefEdit; }

  /** @brief Backward-compatible alias returning `gitRefEdit()`. */
  QLineEdit* githubRefEdit() const noexcept { return m_gitRefEdit; }

  /** @brief Returns the installation scope selector (`Global` vs `This Workspace`). */
  QComboBox* installScopeCombo() const noexcept { return m_installScopeCombo; }

  /** @brief Returns the **Clone & Build** / **Cancel Build** action button. */
  QPushButton* cloneAndBuildButton() const noexcept { return m_cloneBuildBtn; }

  /** @brief Returns the **Sync `plugins.toml`** button triggering declarative manifest synchronization. */
  QPushButton* syncManifestButton() const noexcept { return m_syncManifestBtn; }

  /** @brief Returns the **Open `plugins.toml`** button opening the target scope's manifest file. */
  QPushButton* openManifestButton() const noexcept { return m_openManifestBtn; }

  /** @brief Returns the warning/error build summary badge label. */
  QLabel* buildSummaryBadge() const noexcept { return m_buildSummaryBadge; }

  /** @brief Returns the **Restart GridLock to Load Installed Plugin** button shown on build success. */
  QPushButton* installRestartButton() const noexcept { return m_installRestartBtn; }

  /** @brief Returns the read-only monospace compiler diagnostics log view. */
  QPlainTextEdit* compilerLogView() const noexcept { return m_compilerLogView; }

  /** @brief Returns the owned `PluginInstaller` instance driving Git clone/build and manifest sync tasks. */
  gridlock::core::plugins::PluginInstaller* pluginInstaller() const noexcept { return m_installer; }

  /** @brief Returns true when committed plugin enablement, trust, or installation requires a restart. */
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
  /** @brief Emitted when plugin enablement, trust, or build installation marks a restart required. */
  void restartRequiredChanged(bool required);

  /** @brief Emitted immediately before **Restart GridLock** relaunches the application. */
  void restartRequested();

 private:
  struct StagedPluginState {
    QString pluginId;
    QString libraryPath;
    QString sha256Hex;
    bool isWorkspaceScope = false;
    bool globalEnabled = true;
    bool workspaceEnabled = true;
    bool workspaceTrusted = true;
  };

  QWidget* createInstalledTab(QWidget* parent);
  QWidget* createGitInstallTab(QWidget* parent);
  void updateGitRepoPlaceholder();
  gridlock::core::plugins::PluginManager* resolvePluginManager();
  MainWindow* resolveMainWindow() const;
  void updateRowPreview(int row, const QString& rowKey);
  void setRestartBannerVisible(bool visible);
  void onCloneOrCancelClicked();
  void onSyncManifestClicked();
  void onOpenManifestClicked();
  void onInstallFinished(const gridlock::core::plugins::PluginBuildResult& result);

  QPointer<gridlock::core::plugins::PluginManager> m_pluginManager;
  gridlock::core::plugins::PluginInstaller* m_installer = nullptr;
  QTabWidget* m_tabs = nullptr;
  QComboBox* m_searchDirsCombo = nullptr;
  QPushButton* m_openFolderBtn = nullptr;
  QPushButton* m_reloadBtn = nullptr;
  QFrame* m_restartBanner = nullptr;
  QLabel* m_restartBannerLabel = nullptr;
  QPushButton* m_restartNowBtn = nullptr;
  QTableWidget* m_pluginTable = nullptr;
  QComboBox* m_gitHostCombo = nullptr;
  QLineEdit* m_repoUrlEdit = nullptr;
  QLineEdit* m_gitRefEdit = nullptr;
  QComboBox* m_installScopeCombo = nullptr;
  QPushButton* m_cloneBuildBtn = nullptr;
  QPushButton* m_syncManifestBtn = nullptr;
  QPushButton* m_openManifestBtn = nullptr;
  QLabel* m_buildSummaryBadge = nullptr;
  QPushButton* m_installRestartBtn = nullptr;
  QPlainTextEdit* m_compilerLogView = nullptr;
  QHash<QString, StagedPluginState> m_stagedStates;
  bool m_restartRequired = false;
};

}  // namespace gridlock::ui
