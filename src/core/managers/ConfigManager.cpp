/**
 * @file ConfigManager.cpp
 * @brief Core ConfigManager singleton lifecycle, QSettings-backed PODs, and live editor/shortcut preferences.
 *
 * Implements singleton initialization, active workspace switching, QSettings-backed
 * DebuggerSettings, HpcSettings, SshSettings, SlurmSettings, docset/dialog path
 * helpers, live editor highlight/treemap toggles, and custom keybinding overrides.
 */

#include "ConfigManager.hpp"

#include <string>

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>

namespace gridlock::core {

/** @brief Initializes the singleton with the current working directory and loads `config.toml`. */
ConfigManager::ConfigManager() {
  if (!qEnvironmentVariableIsSet("GRIDLOCK_TEST_CONFIG_DIR") &&
      QCoreApplication::applicationName().endsWith(QStringLiteral("_tests"))) {
    static QTemporaryDir s_testCfgDir;
    if (s_testCfgDir.isValid()) {
      qputenv("GRIDLOCK_TEST_CONFIG_DIR", s_testCfgDir.path().toUtf8());
      QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, s_testCfgDir.path());
      QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, s_testCfgDir.path());
    }
  }
  m_workspacePath = QDir::currentPath();
  m_debuggerEngine.clear();
  loadConfig();
}

/** @brief Updates the active workspace root directory used for `.gridlock/` TOML files. */
void ConfigManager::setWorkspace(const QString& path) {
  m_workspacePath = path;
  m_debuggerEngine.clear();
}

// ─── Debugger settings (QSettings-backed, seeded from TOML) ──────────────────

/** @brief Retrieves local MPI and GDB execution settings from QSettings. */
DebuggerSettings ConfigManager::getDebuggerSettings() const {
  QSettings s(kOrg, kApp);

  const int tomlRanks = m_config["debugger"]["default_ranks"].value_or(2);
  const QString tomlGdb = QString::fromStdString(m_config["debugger"]["gdb_path"].value_or("gdb"));

  DebuggerSettings ds;
  ds.gdbPath = s.value("debugger/gdb_path", tomlGdb).toString();
  ds.mpiExecutable = s.value("debugger/mpi_executable", "mpiexec").toString();
  ds.mpiArgs = s.value("debugger/mpi_args", "--oversubscribe").toString();
  ds.defaultRanks = s.value("debugger/default_ranks", tomlRanks).toInt();
  ds.trapFpe = s.value("debugger/trap_fpe", false).toBool();

  if (ds.defaultRanks < 1) {
    ds.defaultRanks = 1;
  }
  return ds;
}

/** @brief Persists local MPI and GDB execution settings to QSettings. */
void ConfigManager::saveDebuggerSettings(const DebuggerSettings& ds) {
  QSettings s(kOrg, kApp);
  s.setValue("debugger/gdb_path", ds.gdbPath);
  s.setValue("debugger/mpi_executable", ds.mpiExecutable);
  s.setValue("debugger/mpi_args", ds.mpiArgs);
  s.setValue("debugger/default_ranks", qMax(1, ds.defaultRanks));
  s.setValue("debugger/trap_fpe", ds.trapFpe);
  s.sync();
}

// ─── HPC settings ────────────────────────────────────────────────────────────

/** @brief Retrieves HPC cluster hostfile, environment, and affinity settings. */
HpcSettings ConfigManager::getHpcSettings() const {
  QSettings s(kOrg, kApp);
  HpcSettings hs;
  hs.hostsFile = s.value("hpc/hosts_file", "").toString();
  hs.envVars = s.value("hpc/env_vars", "").toString();
  hs.strictAffinity = s.value("hpc/strict_affinity", false).toBool();
  return hs;
}

/** @brief Persists HPC cluster hostfile, environment, and affinity settings. */
void ConfigManager::saveHpcSettings(const HpcSettings& hs) {
  QSettings s(kOrg, kApp);
  s.setValue("hpc/hosts_file", hs.hostsFile);
  s.setValue("hpc/env_vars", hs.envVars);
  s.setValue("hpc/strict_affinity", hs.strictAffinity);
  s.sync();
}

// ─── SSH settings ────────────────────────────────────────────────────────────

/** @brief Retrieves remote cluster SSH login node settings from QSettings. */
SshSettings ConfigManager::getSshSettings() const {
  QSettings s(kOrg, kApp);
  SshSettings ss;
  ss.host = s.value("ssh/host", "").toString();
  ss.user = s.value("ssh/user", "").toString();
  ss.keyPath = s.value("ssh/key_path", "").toString();
  return ss;
}

/** @brief Persists remote cluster SSH login node settings to QSettings. */
void ConfigManager::saveSshSettings(const SshSettings& ss) {
  QSettings s(kOrg, kApp);
  s.setValue("ssh/host", ss.host);
  s.setValue("ssh/user", ss.user);
  s.setValue("ssh/key_path", ss.keyPath);
  s.sync();
}

// ─── SLURM / Spack settings ─────────────────────────────────────────────────

/** @brief Retrieves SLURM batch allocation and remote Spack root settings. */
SlurmSettings ConfigManager::getSlurmSettings() const {
  QSettings s(kOrg, kApp);
  SlurmSettings sl;
  sl.scriptTemplate = s.value("slurm/script_template", "").toString();
  sl.partition = s.value("slurm/partition", "batch").toString();
  sl.nodes = s.value("slurm/nodes", 1).toInt();
  sl.tasksPerNode = s.value("slurm/tasks_per_node", 4).toInt();
  sl.requestGpus = s.value("slurm/request_gpus", false).toBool();
  sl.gpusPerNode = s.value("slurm/gpus_per_node", 1).toInt();
  sl.spackRoot = s.value("slurm/spack_root", "/opt/spack").toString();
  return sl;
}

/** @brief Persists SLURM batch allocation and remote Spack root settings. */
void ConfigManager::saveSlurmSettings(const SlurmSettings& sl) {
  QSettings s(kOrg, kApp);
  s.setValue("slurm/script_template", sl.scriptTemplate);
  s.setValue("slurm/partition", sl.partition);
  s.setValue("slurm/nodes", qMax(1, sl.nodes));
  s.setValue("slurm/tasks_per_node", qMax(1, sl.tasksPerNode));
  s.setValue("slurm/request_gpus", sl.requestGpus);
  s.setValue("slurm/gpus_per_node", qMax(1, sl.gpusPerNode));
  s.setValue("slurm/spack_root", sl.spackRoot);
  s.sync();
}

// ─── Convenience shims & dialog/docset directories ──────────────────────────

/** @brief Returns the configured default MPI rank count. */
int ConfigManager::getDefaultRanks() const { return getDebuggerSettings().defaultRanks; }

/** @brief Returns the active project's custom GDB path. */
QString ConfigManager::getGdbPath() const { return QString::fromStdString(loadProjectSettings().customGdbPath); }

/** @brief Returns the configured MPI launcher executable name. */
QString ConfigManager::getMpiExecutable() const { return getDebuggerSettings().mpiExecutable; }

/** @brief Returns the configured MPI launcher CLI arguments. */
QString ConfigManager::getMpiArgs() const { return getDebuggerSettings().mpiArgs; }

/** @brief Returns the last directory visited by a file-open dialog. */
QString ConfigManager::getLastOpenDir() const {
  QSettings s(kOrg, kApp);
  return s.value("ui/last_open_dir", QDir::currentPath()).toString();
}

/** @brief Saves the last directory visited by a file-open dialog. */
void ConfigManager::setLastOpenDir(const QString& path) {
  QSettings s(kOrg, kApp);
  s.setValue("ui/last_open_dir", path);
  s.sync();
}

/** @brief Returns the configured Zeal/Dash docset directory path. */
QString ConfigManager::getDocsetDirectory() const {
  QSettings s(kOrg, kApp);
  return s.value("docsets/directory", "").toString();
}

/** @brief Saves the configured Zeal/Dash docset directory path. */
void ConfigManager::setDocsetDirectory(const QString& path) {
  QSettings s(kOrg, kApp);
  s.setValue("docsets/directory", path);
  s.sync();
}

// ─── Live editor, visualization, and shortcut preferences ───────────────────

/** @brief Returns whether nested Rainbow Brackets depth coloring is enabled (default: true). */
bool ConfigManager::rainbowBracketsEnabled() const {
  QSettings s(kOrg, kApp);
  if (s.contains(QStringLiteral("editing/rainbow_brackets"))) return s.value(QStringLiteral("editing/rainbow_brackets"), true).toBool();
  if (auto* ed = m_config["editing"].as_table()) if (auto v = ed->get("rainbow_brackets"); v && v->is_boolean()) return v->as_boolean()->get();
  return true;
}

/** @brief Returns whether TOML, YAML, Markdown, and CSV syntax highlighting is enabled (default: true). */
bool ConfigManager::dataDocHighlightingEnabled() const {
  QSettings s(kOrg, kApp);
  if (s.contains(QStringLiteral("editing/data_doc_highlighting"))) return s.value(QStringLiteral("editing/data_doc_highlighting"), true).toBool();
  if (auto* ed = m_config["editing"].as_table()) if (auto v = ed->get("data_doc_highlighting"); v && v->is_boolean()) return v->as_boolean()->get();
  return true;
}

/** @brief Sets and persists Rainbow Brackets enabled state and broadcasts a live editor rehighlight property. */
void ConfigManager::setRainbowBracketsEnabled(bool enabled) {
  toml::table edTbl; if (auto* ex = m_config["editing"].as_table()) edTbl = *ex;
  edTbl.insert_or_assign("rainbow_brackets", enabled); m_config.insert_or_assign("editing", edTbl); saveGlobalConfig();
  QSettings s(kOrg, kApp); s.setValue(QStringLiteral("editing/rainbow_brackets"), enabled); s.sync();
  if (auto* app = QCoreApplication::instance()) {
    app->setProperty("gridlock_rainbow_brackets", enabled);
    app->setProperty("gridlock_editor_highlight_rev", app->property("gridlock_editor_highlight_rev").toInt() + 1);
  }
}

/** @brief Sets and persists TOML/YAML/Markdown/CSV syntax highlighting and broadcasts a live editor rehighlight property. */
void ConfigManager::setDataDocHighlightingEnabled(bool enabled) {
  toml::table edTbl; if (auto* ex = m_config["editing"].as_table()) edTbl = *ex;
  edTbl.insert_or_assign("data_doc_highlighting", enabled); m_config.insert_or_assign("editing", edTbl); saveGlobalConfig();
  QSettings s(kOrg, kApp); s.setValue(QStringLiteral("editing/data_doc_highlighting"), enabled); s.sync();
  if (auto* app = QCoreApplication::instance()) {
    app->setProperty("gridlock_data_doc_highlighting", enabled);
    app->setProperty("gridlock_editor_highlight_rev", app->property("gridlock_editor_highlight_rev").toInt() + 1);
  }
}

/** @brief Returns whether CLion-style inline variable ghost values are shown when paused (default: true). */
bool ConfigManager::inlineVariableValuesEnabled() const {
  QSettings s(kOrg, kApp);
  if (s.contains(QStringLiteral("editor/inline_variable_values"))) return s.value(QStringLiteral("editor/inline_variable_values"), true).toBool();
  if (auto* ed = m_config["editor"].as_table()) if (auto v = ed->get("inline_variable_values"); v && v->is_boolean()) return v->as_boolean()->get();
  return true;
}

/** @brief Sets and persists whether inline variable ghost values are shown and broadcasts live update. */
void ConfigManager::setInlineVariableValuesEnabled(bool enabled) {
  toml::table edTbl; if (auto* ex = m_config["editor"].as_table()) edTbl = *ex;
  edTbl.insert_or_assign("inline_variable_values", enabled); m_config.insert_or_assign("editor", edTbl); saveGlobalConfig();
  QSettings s(kOrg, kApp); s.setValue(QStringLiteral("editor/inline_variable_values"), enabled); s.sync();
  if (auto* app = QCoreApplication::instance()) {
    app->setProperty("gridlock_inline_var_values", enabled);
    app->setProperty("gridlock_editor_highlight_rev", app->property("gridlock_editor_highlight_rev").toInt() + 1);
  }
}

/** @brief Returns the active Memory Map treemap color scheme (`Distinct`, `Viridis`, `Plasma`, `Ocean`, `HighContrast`). */
QString ConfigManager::memoryMapColorScheme() const {
  QSettings s(kOrg, kApp);
  if (s.contains(QStringLiteral("appearance/memory_map_color_scheme"))) {
    return s.value(QStringLiteral("appearance/memory_map_color_scheme"), QStringLiteral("Distinct")).toString();
  }
  if (auto* ap = m_config["appearance"].as_table()) {
    if (auto v = ap->get("memory_map_color_scheme"); v && v->is_string()) {
      return QString::fromStdString(std::string(v->as_string()->get()));
    }
  }
  return QStringLiteral("Distinct");
}

/** @brief Persists Memory Map color scheme to TOML/QSettings and notifies live MemoryProfilerWidget instances. */
void ConfigManager::setMemoryMapColorScheme(const QString& scheme) {
  static const QStringList kValid = {
      QStringLiteral("Distinct"), QStringLiteral("Viridis"), QStringLiteral("Plasma"),
      QStringLiteral("Ocean"), QStringLiteral("HighContrast")};
  const QString norm = kValid.contains(scheme) ? scheme : QStringLiteral("Distinct");
  toml::table apTbl; if (auto* ex = m_config["appearance"].as_table()) apTbl = *ex;
  apTbl.insert_or_assign("memory_map_color_scheme", norm.toStdString()); m_config.insert_or_assign("appearance", apTbl); saveGlobalConfig();
  QSettings s(kOrg, kApp); s.setValue(QStringLiteral("appearance/memory_map_color_scheme"), norm); s.sync();
  if (auto* app = QCoreApplication::instance()) {
    app->setProperty("gridlock_memmap_color_scheme", norm);
    app->setProperty("gridlock_memmap_scheme_rev", app->property("gridlock_memmap_scheme_rev").toInt() + 1);
  }
}

/** @brief Loads persisted user-customized shortcut overrides keyed by action identifier. */
QMap<QString, QKeySequence> ConfigManager::customShortcuts() const {
  QMap<QString, QKeySequence> out;
  if (auto* kb = m_config["keybindings"].as_table()) {
    for (const auto& [k, v] : *kb) {
      if (v.is_string()) {
        out.insert(QString::fromStdString(std::string(k.str())),
                   QKeySequence::fromString(QString::fromStdString(v.as_string()->get()), QKeySequence::PortableText));
      }
    }
  }
  QSettings s(kOrg, kApp); s.beginGroup(QStringLiteral("keybindings"));
  for (const QString& key : s.childKeys()) {
    out.insert(key, QKeySequence::fromString(s.value(key).toString(), QKeySequence::PortableText));
  }
  s.endGroup();
  return out;
}

/** @brief Persists user-customized shortcut overrides to QSettings and global TOML configuration. */
void ConfigManager::setCustomShortcuts(const QMap<QString, QKeySequence>& overrides) {
  toml::table kbTbl;
  for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
    kbTbl.insert_or_assign(it.key().toStdString(), it.value().toString(QKeySequence::PortableText).toStdString());
  }
  m_config.insert_or_assign("keybindings", kbTbl);
  saveGlobalConfig();
  QSettings s(kOrg, kApp); s.remove(QStringLiteral("keybindings")); s.beginGroup(QStringLiteral("keybindings"));
  for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
    s.setValue(it.key(), it.value().toString(QKeySequence::PortableText));
  }
  s.endGroup();
  s.sync();
}

/** @brief Returns true when @p pluginId is listed in `[plugins] disabled_ids`. */
bool ConfigManager::isPluginDisabled(const QString& pluginId) const {
  const QString trimmed = pluginId.trimmed();
  return !trimmed.isEmpty() && disabledPluginIds().contains(trimmed);
}

/** @brief Inserts or removes @p pluginId in `[plugins] disabled_ids` and persists the list. */
void ConfigManager::setPluginDisabled(const QString& pluginId, bool disabled) {
  const QString trimmed = pluginId.trimmed();
  if (trimmed.isEmpty()) return;
  QStringList ids = disabledPluginIds();
  if (disabled) {
    if (!ids.contains(trimmed)) ids.append(trimmed);
  } else {
    ids.removeAll(trimmed);
  }
  setDisabledPluginIds(ids);
}

/** @brief Returns true when @p pluginId is enabled globally across workspaces. */
bool ConfigManager::isPluginGloballyEnabled(const QString& pluginId) const {
  const QString trimmed = pluginId.trimmed();
  return !trimmed.isEmpty() && !disabledPluginIds().contains(trimmed);
}

/** @brief Sets global enablement for @p pluginId by updating `[plugins] disabled_ids`. */
void ConfigManager::setPluginGloballyEnabled(const QString& pluginId, bool enabled) {
  setPluginDisabled(pluginId, !enabled);
}

/** @brief Returns true when @p pluginId is enabled in the active workspace. */
bool ConfigManager::isPluginWorkspaceEnabled(const QString& pluginId) const {
  const QString trimmed = pluginId.trimmed();
  return !trimmed.isEmpty() && !workspaceDisabledPluginIds().contains(trimmed);
}

/** @brief Updates active-workspace enablement for @p pluginId in user-global `config.toml`. */
void ConfigManager::setPluginWorkspaceEnabled(const QString& pluginId, bool enabled) {
  const QString trimmed = pluginId.trimmed();
  if (trimmed.isEmpty()) return;
  QStringList ids = workspaceDisabledPluginIds();
  if (!enabled) {
    if (!ids.contains(trimmed)) ids.append(trimmed);
  } else {
    ids.removeAll(trimmed);
  }
  setWorkspaceDisabledPluginIds(ids);
}

/** @brief Returns true only when @p pluginId is enabled both globally and in the active workspace. */
bool ConfigManager::effectivePluginEnabled(const QString& pluginId) const {
  return isPluginGloballyEnabled(pluginId) && isPluginWorkspaceEnabled(pluginId);
}

/** @brief Returns the canonical default `#MainToolbar` ordered item token list. */
QStringList ConfigManager::defaultToolbarLayout() {
  return {
      QStringLiteral("target_selector"),
      QStringLiteral("config_menu"),
      QStringLiteral("rank_spin"),
      QStringLiteral("separator"),
      QStringLiteral("build"),
      QStringLiteral("run"),
      QStringLiteral("continue"),
      QStringLiteral("step_over"),
      QStringLiteral("step_into"),
      QStringLiteral("step_out"),
      QStringLiteral("pause"),
      QStringLiteral("stop"),
      QStringLiteral("record"),
      QStringLiteral("flex_spacer"),
      QStringLiteral("search_everywhere"),
      QStringLiteral("perspectives"),
  };
}

/** @brief Registers a `QPointer`-guarded callback invoked whenever `setToolbarLayout()` updates the toolbar layout. */
void ConfigManager::registerToolbarLayoutListener(
    QObject* context, std::function<void(const QStringList&)> callback) {
  if (!context || !callback) return;
  m_toolbarLayoutListeners.emplace_back(QPointer<QObject>(context), std::move(callback));
}

/** @brief Broadcasts the active toolbar layout to all live `QPointer`-guarded listeners, pruning destroyed receivers. */
void ConfigManager::toolbarLayoutChanged() {
  const QStringList current = toolbarLayout();
  auto it = m_toolbarLayoutListeners.begin();
  while (it != m_toolbarLayoutListeners.end()) {
    if (it->first.isNull()) {
      it = m_toolbarLayoutListeners.erase(it);
    } else {
      auto cb = it->second;
      ++it;
      if (cb) cb(current);
    }
  }
}

/** @brief Registers a `QPointer`-guarded callback invoked whenever `setSearchPopupSize()` updates the size preset. */
void ConfigManager::registerSearchPopupSizeListener(
    QObject* context, std::function<void(const QString&)> callback) {
  if (!context || !callback) return;
  m_searchPopupSizeListeners.emplace_back(QPointer<QObject>(context), std::move(callback));
}

/** @brief Registers a `QPointer`-guarded callback invoked whenever `setShowMenuBarWindowControls()` updates menu-bar window control visibility. */
void ConfigManager::registerMenuBarWindowControlsListener(
    QObject* context, std::function<void(bool)> callback) {
  if (!context || !callback) return;
  m_menuBarWindowControlsListeners.emplace_back(QPointer<QObject>(context), std::move(callback));
}

}  // namespace gridlock::core