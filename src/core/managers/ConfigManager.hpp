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

#include <string>
#include <vector>

#include <QMap>
#include <QSet>
#include <QSettings>
#include <QString>
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
  QString hostsFile = "";      ///< Path to MPI hostfile (empty = local execution).
  QString envVars = "";        ///< Newline-separated `KEY=VALUE` environment variables.
  bool strictAffinity = false; ///< Pass `--map-by node` to enforce strict node affinity.
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
 * @brief SLURM batch job and remote Spack root configuration.
 */
struct SlurmSettings {
  QString scriptTemplate = "";      ///< Default `sbatch` script body (`%%FILE%%` placeholder).
  QString partition = "batch";      ///< SLURM partition or queue name.
  int nodes = 1;                    ///< Number of compute nodes to allocate.
  int tasksPerNode = 4;             ///< Number of MPI tasks per allocated node.
  bool requestGpus = false;         ///< Whether to include `--gres=gpu` in the allocation.
  int gpusPerNode = 1;              ///< Number of GPUs requested per node when `requestGpus` is true.
  QString spackRoot = "/opt/spack"; ///< Spack installation prefix on the cluster.
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
  std::string environmentVariables;          ///< Environment overrides for the target process.
  std::string workingDirectory;              ///< Initial working directory for the target process.
  std::string customGdbPath = "gdb";         ///< Workspace-specific GDB executable override.
  std::vector<std::string> watchExpressions; ///< Persisted watch expressions evaluated on stops.
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

  /** @brief Loads per-workspace target and watch settings from `.gridlock/settings.toml`. */
  ProjectSettings loadProjectSettings() const;
  /** @brief Saves per-workspace target and watch settings to `.gridlock/settings.toml`. */
  void saveProjectSettings(const ProjectSettings& ps) const;

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

  static constexpr const char* kOrg = "gridlock"; ///< QSettings organization identifier.
  static constexpr const char* kApp = "debugger"; ///< QSettings application identifier.
};

}  // namespace gridlock::core