/**
 * @file ConfigManager.hpp
 * @brief Central configuration manager bridging TOML files and QSettings persistence.
 *
 * Manages global toolchain paths (`config.toml`), per-workspace breakpoints and
 * watch expressions (`.gridlock/workspace.toml`), project target settings
 * (`.gridlock/settings.toml`), and user-level debugger/HPC/SSH/SLURM preferences
 * stored via QSettings.
 */

#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <QDir>
#include <QKeySequence>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <toml++/toml.h>

namespace gridlock::core {

/**
 * @struct DebuggerSettings
 * @brief Canonical configuration for local MPI and GDB process spawning.
 */
struct DebuggerSettings {
  QString gdbPath = "gdb";                  ///< Path to the GDB executable.
  QString mpiExecutable = "mpiexec";        ///< Path to the MPI launcher (`mpiexec` or `mpirun`).
  QString mpiArgs = "--oversubscribe";      ///< Additional CLI flags passed to the MPI launcher.
  int defaultRanks = 2;                     ///< Default number of MPI ranks to spawn.
  bool trapFpe = false;                     ///< Whether to trap floating-point exceptions in GDB.
};

/**
 * @struct HpcSettings
 * @brief Cluster hostfile, environment, and NUMA affinity parameters.
 */
struct HpcSettings {
  QString hostsFile = "";          ///< Path to MPI hostfile (empty = local execution).
  QString envVars = "";            ///< Newline-separated `KEY=VALUE` environment variables.
  bool strictAffinity = false;     ///< Pass `--map-by node` to enforce strict node affinity.
  bool enableSlurmMenu = false;    ///< Opt-in toggle showing "Submit Slurm Job..." in the File menu.
  int baseGdbserverPort = 2345;    ///< Starting TCP port (`base + rank`) for Slurm/remote `gdbserver` listeners.
  int attachRetryTimeoutSec = 15;  ///< Startup retry window (seconds) for remote `gdbserver` sockets.
};

/**
 * @struct SshSettings
 * @brief SSH credentials for connecting to a remote HPC login node.
 */
struct SshSettings {
  QString host = "";    ///< Hostname or IP address of the remote login node.
  QString user = "";    ///< SSH username on the remote cluster.
  QString keyPath = ""; ///< Filesystem path to the private SSH key (`~/.ssh/id_ed25519`).
};

/**
 * @struct SlurmSettings
 * @brief SLURM batch job, scheduler tool paths, and remote Spack root configuration.
 */
struct SlurmSettings {
  QString scriptTemplate = "";             ///< Default `sbatch` script body (`{FILE}` / `%%FILE%%` placeholder).
  QString partition = "batch";             ///< SLURM partition or queue name.
  int nodes = 1;                           ///< Number of compute nodes to allocate.
  int tasksPerNode = 4;                    ///< Number of MPI tasks per allocated node.
  bool requestGpus = false;                ///< Whether to include `--gres=gpu` in the allocation.
  int gpusPerNode = 1;                     ///< Number of GPUs requested per node when `requestGpus` is true.
  QString spackRoot = "/opt/spack";        ///< Spack installation prefix on the cluster.
  QString jobName = "gridlock_debug";      ///< SLURM job name (`--job-name` / `-J`).
  QString account = "";                    ///< Optional billing/project account (`--account` / `-A`).
  QString qos = "(default)";               ///< Optional Quality of Service tier (`--qos` / `-q`).
  QString wallTime = "01:00:00";           ///< Job wall-clock limit (`--time` / `-t`).
  int cpusPerTask = 1;                     ///< CPU cores per MPI task (`--cpus-per-task` / `-c`).
  QString memory = "";                     ///< Optional per-node memory spec (`--mem` or `per-cpu:<val>`).
  QString gpuType = "";                    ///< Optional GPU model token (`a100`, `h100`) for `--gres=gpu:<type>:<n>`.
  QString constraint = "";                 ///< Optional node feature constraint (`--constraint` / `-C`).
  QString mpiLauncher = "srun";            ///< MPI job launcher inside the batch script (`srun`, `mpirun`, `mpiexec`).
  QString modules = "";                    ///< Space-separated environment modules for `module load`.
  QString outputLog = "gridlock_%j.log";   ///< Standard output log pattern (`--output` / `-o`).
  QString errorLog = "";                   ///< Optional standard error log pattern (`--error` / `-e`).
  QString mailType = "NONE";               ///< Notification event filter (`--mail-type`).
  QString mailUser = "";                   ///< Notification email recipient (`--mail-user`).
  QString scriptPath = "";                 ///< Last loaded `.sbatch`/`.slurm` script file path.
  QString sbatchPath = "sbatch";           ///< Path or command name for `sbatch`.
  QString squeuePath = "squeue";           ///< Path or command name for `squeue`.
  QString scontrolPath = "scontrol";       ///< Path or command name for `scontrol`.
};

/**
 * @struct TerminalSettings
 * @brief Embedded terminal shell and typography configuration.
 */
struct TerminalSettings {
  QString shellPath = "";                    ///< Shell executable path (defaults to `$SHELL` or `/bin/bash`).
  QString fontFamily = "FiraCode Nerd Font"; ///< Monospace font family for the embedded terminal.
  int fontSize = 12;                         ///< Terminal font point size.
};

/**
 * @struct ProjectSettings
 * @brief Per-workspace target executable, arguments, environment, and watch list.
 */
struct ProjectSettings {
  std::string targetBinary;                  ///< Path to the compiled target binary.
  std::string programArguments;              ///< Command-line arguments passed to the target binary.
  QString targetArguments;                   ///< QString mirror of programArguments for UI/launch integration.
  std::string environmentVariables;          ///< Environment overrides for the target process.
  std::string workingDirectory;              ///< Initial working directory for the target process.
  std::string customGdbPath = "gdb";         ///< Workspace-specific GDB executable override.
  int mpiRanks = 0;                          ///< Workspace-scoped MPI rank count override (0 = debugger default).
  QString mpiArgs;                           ///< Workspace-scoped mpirun extra flags override.
  std::vector<std::string> watchExpressions; ///< Persisted watch expressions evaluated on stops.
};

/**
 * @struct BreakpointConfig
 * @brief Per-breakpoint predicate, hit ignore count, and non-suspending Logpoint message.
 */
struct BreakpointConfig {
  QString file;       ///< Source file path.
  int line = 0;       ///< One-based source line number.
  int rankFilter = -1; ///< Target zero-based MPI rank, or -1 for all ranks.
  QString condition;  ///< Optional boolean C/C++ condition expression.
  int ignoreCount = 0; ///< Number of hits to ignore before stopping (0 = stop immediately).
  QString logMessage; ///< Non-empty formatted trace message for non-suspending Logpoints.
};

/**
 * @class ConfigManager
 * @brief Singleton facade over `config.toml`, `.gridlock/` TOML files, and `QSettings`.
 */
class ConfigManager {
 public:
  /**
   * @brief Returns the process-wide ConfigManager singleton instance.
   */
  static ConfigManager& instance() {
    static ConfigManager instance;
    return instance;
  }

  /**
   * @brief Loads (or initializes if missing) the global `config.toml` file.
   */
  void loadConfig();

  /** @brief Returns the configured source editor background color hex string. */
  QString getSourceBackground() const;
  /** @brief Returns the configured source editor foreground text color hex string. */
  QString getSourceText() const;
  /** @brief Returns the configured source editor active execution line color hex string. */
  QString getSourceActiveLine() const;

  /** @brief Returns the configured assembly opcode syntax highlight color. */
  QString getAssemblyOpcode() const;
  /** @brief Returns the configured assembly register syntax highlight color. */
  QString getAssemblyRegister() const;
  /** @brief Returns the configured assembly address gutter color. */
  QString getAssemblyAddress() const;

  /** @brief Returns the offline Zeal/Dash docset root directory path. */
  QString getDocsetDirectory() const;
  /** @brief Persists the offline Zeal/Dash docset root directory path. */
  void setDocsetDirectory(const QString& path);

  /** @brief Reads the active MPI/GDB settings from QSettings (seeded from TOML). */
  DebuggerSettings getDebuggerSettings() const;
  /** @brief Persists MPI/GDB settings to QSettings. */
  void saveDebuggerSettings(const DebuggerSettings& s);

  /** @brief Reads HPC hostfile and affinity settings from QSettings. */
  HpcSettings getHpcSettings() const;
  /** @brief Persists HPC hostfile and affinity settings to QSettings. */
  void saveHpcSettings(const HpcSettings& s);

  /** @brief Reads remote SSH login settings from QSettings. */
  SshSettings getSshSettings() const;
  /** @brief Persists remote SSH login settings to QSettings. */
  void saveSshSettings(const SshSettings& s);

  /** @brief Reads SLURM batch and Spack settings from QSettings. */
  SlurmSettings getSlurmSettings() const;
  /** @brief Persists SLURM batch and Spack settings to QSettings. */
  void saveSlurmSettings(const SlurmSettings& s);

  /** @brief Reads embedded terminal shell and font settings from `config.toml`. */
  TerminalSettings getTerminalSettings() const;
  /** @brief Persists embedded terminal shell and font settings to `config.toml`. */
  void saveTerminalSettings(const TerminalSettings& s);

  /** @brief Convenience accessor returning the default MPI rank count. */
  int getDefaultRanks() const;
  /** @brief Convenience accessor returning the project or global GDB path. */
  QString getGdbPath() const;
  /** @brief Convenience accessor returning the configured MPI launcher executable. */
  QString getMpiExecutable() const;
  /** @brief Convenience accessor returning the configured MPI launcher arguments. */
  QString getMpiArgs() const;

  /** @brief Returns the last directory visited by a file-open dialog. */
  QString getLastOpenDir() const;
  /** @brief Persists the last directory visited by a file-open dialog. */
  void setLastOpenDir(const QString& path);

  /**
   * @brief Loads source-line breakpoints for @p targetExecutable from `.gridlock/workspace.toml`.
   */
  QMap<QString, QSet<int>> getBreakpoints(QString targetExecutable = QString()) const;

  /**
   * @brief Saves source-line breakpoints for @p targetExecutable to `.gridlock/workspace.toml`.
   */
  void saveBreakpoints(const QMap<QString, QSet<int>>& breakpoints,
                       QString targetExecutable = QString());

  /** @brief Loads extended per-breakpoint metadata (condition, ignoreCount, logMessage). */
  QList<BreakpointConfig> getBreakpointConfigs(const QString& targetExecutable = QString()) const;
  /** @brief Persists extended per-breakpoint metadata (condition, ignoreCount, logMessage). */
  void saveBreakpointConfigs(const QList<BreakpointConfig>& configs, const QString& targetExecutable = QString());

  /** @brief Loads per-workspace target and watch settings from `.gridlock/settings.toml`. */
  ProjectSettings loadProjectSettings() const;
  /** @brief Convenience alias returning current project settings. */
  ProjectSettings projectSettings() const { return loadProjectSettings(); }
  /** @brief Saves per-workspace target and watch settings to `.gridlock/settings.toml`. */
  void saveProjectSettings(const ProjectSettings& ps) const;

  /** @brief Returns CLI arguments strictly scoped to @p targetBinaryPath (workspace TOML or in-memory map; never global QSettings). */
  QString programArgs(const QString& targetBinaryPath = QString()) const;
  /** @brief Persists CLI arguments strictly for @p targetBinaryPath in `.gridlock/settings.toml` and in-memory target map. */
  void setProgramArgs(const QString& targetBinaryPath, const QString& args) const;

  /** @brief Sets the active project workspace root directory path. */
  void setWorkspace(const QString& path);
  /** @brief Returns the active project workspace root directory path. */
  QString workspacePath() const { return m_workspacePath; }

  /** @brief Returns the global `clangd` executable path from `config.toml`. */
  QString getGlobalClangdPath() const;
  /** @brief Updates and saves the global `clangd` executable path in `config.toml`. */
  void setGlobalClangdPath(const QString& path);

  /** @brief Returns the global `lldb-dap` adapter path from `config.toml`. */
  QString getGlobalDapAdapterPath() const;
  /** @brief Updates and saves the global `lldb-dap` adapter path in `config.toml`. */
  void setGlobalDapAdapterPath(const QString& path);

  /** @brief Returns the global `gdb` executable path from `config.toml`. */
  QString getGlobalGdbPath() const;
  /** @brief Updates and saves the global `gdb` executable path in `config.toml`. */
  void setGlobalGdbPath(const QString& path);

  /** @brief Returns the global MPI launcher arguments from `config.toml`. */
  QString getGlobalMpiArgs() const;
  /** @brief Updates and saves the global MPI launcher arguments in `config.toml`. */
  void setGlobalMpiArgs(const QString& args);

  /** @brief Returns the preferred output monitor name or substring from `[ui]` in `config.toml`. */
  QString preferredMonitor() const;
  /** @brief Updates and saves the preferred output monitor hint in `config.toml`. */
  void setPreferredMonitor(const QString& monitorHint);

  /** @brief Returns whether `[ui] open_unfocused` is enabled in `config.toml`. */
  bool openUnfocused() const;
  /** @brief Updates and saves `[ui] open_unfocused` in `config.toml`. */
  void setOpenUnfocused(bool unfocused);

  /** @brief Returns whether `[ui] show_menubar_window_controls` is enabled (default: `true`). */
  bool showMenuBarWindowControls() const;
  /** @brief Persists `[ui] show_menubar_window_controls` to `config.toml` and `QSettings`, then notifies live `MainWindow` listeners. */
  void setShowMenuBarWindowControls(bool enabled);
  /** @brief Registers a `QPointer`-guarded callback invoked whenever `setShowMenuBarWindowControls()` updates menu-bar window control visibility. */
  void registerMenuBarWindowControlsListener(QObject* context, std::function<void(bool)> callback);

  /** @brief Returns the canonical default `#MainToolbar` ordered item token list. */
  static QStringList defaultToolbarLayout();
  /** @brief Returns the persisted `#MainToolbar` item token list from `[ui] toolbar_layout` in `config.toml` or `QSettings`. */
  QStringList toolbarLayout() const;
  /** @brief Persists @p items to `[ui] toolbar_layout` in `config.toml` and `QSettings`, then notifies listeners via `toolbarLayoutChanged()`. */
  void setToolbarLayout(const QStringList& items);
  /** @brief Broadcasts the updated toolbar layout to all live `QPointer`-guarded `MainWindow` listeners. */
  void toolbarLayoutChanged();
  /** @brief Registers a `QPointer`-guarded callback invoked whenever `setToolbarLayout()` updates the toolbar layout. */
  void registerToolbarLayoutListener(QObject* context, std::function<void(const QStringList&)> callback);

  /** @brief Returns the Search Everywhere popup size preset (`"small"`, `"medium"`, `"large"`, or `"xlarge"`; default: `"large"`). */
  QString searchPopupSize() const;
  /** @brief Persists the Search Everywhere popup size preset to `[ui] search_popup_size` in `config.toml` and `QSettings`. */
  void setSearchPopupSize(const QString& preset);
  /** @brief Registers a `QPointer`-guarded callback invoked whenever `setSearchPopupSize()` updates the size preset. */
  void registerSearchPopupSizeListener(QObject* context, std::function<void(const QString&)> callback);

  /** @brief Returns the persisted theme name from .gridlock/workspace.toml or QSettings. */
  QString getThemeName() const;
  /** @brief Persists the theme name to .gridlock/workspace.toml and QSettings. */
  void setThemeName(const QString& name);

  /** @brief Returns the configured local debugger backend engine ("gdb", "lldb-dap", "cuda-gdb", "rocgdb"). */
  QString debuggerEngine() const;
  /** @brief Sets and persists the local debugger backend engine in workspace.toml and QSettings. */
  void setDebuggerEngine(const QString& engine);

  /** @brief Returns whether the pseudo-C decompiler is allowed to stay visible alongside the C++ Source Editor (default: false). */
  bool showDecompilerWithSource() const;
  /** @brief Sets and persists whether the pseudo-C decompiler may stay visible alongside the C++ Source Editor. */
  void setShowDecompilerWithSource(bool show);

  /** @brief Returns the default DisassemblyView presentation mode ("cfg", "linear", or "split"; default: "cfg"). */
  QString defaultDisassemblyMode() const;
  /** @brief Sets and persists the default DisassemblyView presentation mode ("cfg", "linear", or "split"). */
  void setDefaultDisassemblyMode(const QString& mode);

  /** @brief Returns the status bar resource telemetry display format ("gb", "percent", or "both"; default: "gb"). */
  QString telemetryDisplayFormat() const;
  /** @brief Sets and persists the status bar resource telemetry display format ("gb", "percent", or "both"). */
  void setTelemetryDisplayFormat(const QString& fmt);

  /** @brief Returns whether nested (), {}, [] Rainbow Brackets depth coloring is enabled (default: true). */
  bool rainbowBracketsEnabled() const;
  /** @brief Sets and persists whether Rainbow Brackets depth coloring is enabled and broadcasts live update. */
  void setRainbowBracketsEnabled(bool enabled);

  /** @brief Returns whether TOML, YAML, Markdown, and CSV syntax highlighting is enabled (default: true). */
  bool dataDocHighlightingEnabled() const;
  /** @brief Sets and persists whether TOML/YAML/Markdown/CSV syntax highlighting is enabled and broadcasts live update. */
  void setDataDocHighlightingEnabled(bool enabled);

  /** @brief Returns whether CLion-style inline variable ghost values are shown when paused (default: true). */
  bool inlineVariableValuesEnabled() const;
  /** @brief Sets and persists whether inline variable ghost values are shown and broadcasts live update. */
  void setInlineVariableValuesEnabled(bool enabled);

  /** @brief Returns the active Memory Map treemap color scheme (`Distinct`, `Viridis`, `Plasma`, `Ocean`, `HighContrast`). */
  QString memoryMapColorScheme() const;
  /** @brief Sets and persists the Memory Map treemap color scheme and broadcasts live update. */
  void setMemoryMapColorScheme(const QString& scheme);

  /** @brief Returns persisted user-customized shortcut overrides keyed by stable action identifier. */
  QMap<QString, QKeySequence> customShortcuts() const;
  /** @brief Persists user-customized shortcut overrides to QSettings and global TOML configuration. */
  void setCustomShortcuts(const QMap<QString, QKeySequence>& overrides);

  /** @brief Returns deduplicated plugin IDs disabled under `[plugins] disabled_ids` in `config.toml`. */
  QStringList disabledPluginIds() const;
  /** @brief Persists `[plugins] disabled_ids` to `config.toml` and `QSettings`. */
  void setDisabledPluginIds(const QStringList& ids);
  /** @brief Returns true if @p pluginId appears in `disabledPluginIds()`. */
  bool isPluginDisabled(const QString& pluginId) const;
  /** @brief Adds or removes @p pluginId in `[plugins] disabled_ids` and persists immediately. */
  void setPluginDisabled(const QString& pluginId, bool disabled);

  /** @brief Returns plugin IDs disabled for the active workspace under `[plugins.workspaces."<path>"]`. */
  QStringList workspaceDisabledPluginIds() const;
  /** @brief Persists `workspace_disabled_ids` for the active workspace in user-global `config.toml`. */
  void setWorkspaceDisabledPluginIds(const QStringList& ids);
  /** @brief Returns true when @p pluginId is enabled globally (not in `[plugins] disabled_ids`). */
  bool isPluginGloballyEnabled(const QString& pluginId) const;
  /** @brief Persists global enablement state for @p pluginId in user-global `config.toml`. */
  void setPluginGloballyEnabled(const QString& pluginId, bool enabled);
  /** @brief Returns true when @p pluginId is enabled in the active workspace. */
  bool isPluginWorkspaceEnabled(const QString& pluginId) const;
  /** @brief Persists active-workspace enablement state for @p pluginId in user-global `config.toml`. */
  void setPluginWorkspaceEnabled(const QString& pluginId, bool enabled);
  /** @brief Returns true when @p pluginId is enabled both globally and for the active workspace. */
  bool effectivePluginEnabled(const QString& pluginId) const;

  /** @brief Returns true when @p canonicalPath matches @p sha256Hex in `[plugins.trusted_workspace_binaries]`. */
  bool isWorkspacePluginTrusted(const QString& canonicalPath, const QString& sha256Hex) const;
  /** @brief Records @p canonicalPath -> lowercase @p sha256Hex under `[plugins.trusted_workspace_binaries]`. */
  void trustWorkspacePlugin(const QString& canonicalPath, const QString& sha256Hex);
  /** @brief Removes @p canonicalPath from `[plugins.trusted_workspace_binaries]` in `config.toml`. */
  void revokeWorkspacePluginTrust(const QString& canonicalPath);

  /** @brief Returns the canonical GridLock configuration directory (`~/.config/gridlock`, or `GRIDLOCK_TEST_CONFIG_DIR`). */
  static QString configDirectory() {
    if (qEnvironmentVariableIsSet("GRIDLOCK_TEST_CONFIG_DIR")) {
      const QString overrideDir = qEnvironmentVariable("GRIDLOCK_TEST_CONFIG_DIR").trimmed();
      if (!overrideDir.isEmpty()) return overrideDir;
    }
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return (base.isEmpty() ? (QDir::homePath() + QStringLiteral("/.config")) : base) + QStringLiteral("/gridlock");
  }
  /** @brief Returns the canonical INI path for workstation preferences (`~/.config/gridlock/workstation.conf`). */
  static QString workstationSettingsPath() { return configDirectory() + QStringLiteral("/workstation.conf"); }
  /** @brief Returns the canonical INI path for debugger preferences (`~/.config/gridlock/debugger.conf`). */
  static QString debuggerSettingsPath() { return configDirectory() + QStringLiteral("/debugger.conf"); }
  /** @brief Returns the active global `config.toml` file path. */
  QString configFilePath() const { return m_globalConfigPath; }

  /** @brief Returns deduplicated existing recent session/workspace paths (most recent first, capped at 10). */
  QStringList recentSessions() const;
  /** @brief Records @p path at index 0 of `recent_sessions` in `config.toml` and flushes immediately. */
  void recordRecentSession(const QString& path);
  /** @brief Clears all persisted recent session entries from `config.toml` and INI settings. */
  void clearRecentSessions();

  /** @brief Returns the persistent GDB console command history file path (`~/.config/gridlock/gdb_history` or test override). */
  QString gdbHistoryFilePath() const { return resolveGdbHistoryFilePath(); }
  /** @brief Overrides the GDB console command history file path for isolated unit tests (empty restores default). */
  void setGdbHistoryFilePathOverride(const QString& path) { s_gdbHistoryPathOverride() = path; }
  /** @brief Static helper resolving the active GDB history file path without requiring `ConfigManager::instance()`. */
  static QString resolveGdbHistoryFilePath() {
    if (!s_gdbHistoryPathOverride().trimmed().isEmpty()) return s_gdbHistoryPathOverride().trimmed();
    return configDirectory() + QStringLiteral("/gdb_history");
  }
  static QString& s_gdbHistoryPathOverride() { static QString p; return p; }

  /** @brief Registers a `QPointer`-guarded listener invoked whenever `hpc/enable_slurm_menu` changes. */
  void registerSlurmMenuVisibilityListener(QObject* context, std::function<void(bool)> callback);

  /** @brief Flushes the in-memory `m_config` TOML table to `m_globalConfigPath`. */
  void saveGlobalConfig();

 private:
  ConfigManager();
  ~ConfigManager() = default;

  ConfigManager(const ConfigManager&) = delete;
  ConfigManager& operator=(const ConfigManager&) = delete;

  toml::table m_config;       ///< In-memory representation of the global `config.toml`.
  QString m_workspacePath;    ///< Active project workspace root directory.
  QString m_globalConfigPath; ///< Absolute path to the active `config.toml` file.
  QString m_debuggerEngine = QStringLiteral("gdb"); ///< Active local debugger backend engine identifier.
  mutable QMap<QString, QString> m_targetProgramArgs; ///< In-memory per-target CLI arguments for standalone binaries.
  std::vector<std::pair<QPointer<QObject>, std::function<void(const QStringList&)>>> m_toolbarLayoutListeners; ///< Live toolbar layout observers.
  std::vector<std::pair<QPointer<QObject>, std::function<void(const QString&)>>> m_searchPopupSizeListeners; ///< Live Search Everywhere size observers.
  std::vector<std::pair<QPointer<QObject>, std::function<void(bool)>>> m_menuBarWindowControlsListeners; ///< Live menu-bar window controls visibility observers.
  std::vector<std::pair<QPointer<QObject>, std::function<void(bool)>>> m_slurmMenuVisibilityListeners; ///< Live File-menu Submit Slurm Job visibility observers.

  static constexpr const char* kOrg = "gridlock"; ///< QSettings organization identifier.
  static constexpr const char* kApp = "debugger"; ///< QSettings application identifier.
};

}  // namespace gridlock::core