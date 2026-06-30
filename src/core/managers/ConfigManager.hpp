#pragma once
#include <QMap>
#include <QSet>
#include <QSettings>
#include <QString>
#include <toml++/toml.h>

namespace gridlock::core {

/// Canonical storage for MPI/GDB configuration.
/// QSettings ("GridLock"/"Debugger") is the live layer;
/// the TOML file holds theme colours and breakpoints.
struct DebuggerSettings {
  QString gdbPath = "gdb";
  QString mpiExecutable = "mpiexec";
  QString mpiArgs = "--oversubscribe";
  int defaultRanks = 2;
  bool trapFpe = false;
};

/// HPC cluster / node configuration. Stored in QSettings under hpc/*.
struct HpcSettings {
  QString hostsFile = "";      ///< Path to MPI hostfile (empty = not used)
  QString envVars = "";        ///< Newline-separated key=value pairs
  bool strictAffinity = false; ///< Pass --map-by node to the MPI launcher
};

/// SSH connection to a remote HPC login node. Stored under ssh/*.
struct SshSettings {
  QString host = "";    ///< hostname or IP of the login node
  QString user = "";    ///< login username
  QString keyPath = ""; ///< path to private SSH key (~/.ssh/id_rsa style)
};

/// SLURM batch configuration. Stored under slurm/*.
struct SlurmSettings {
  QString scriptTemplate =
      ""; ///< Default sbatch script body (may contain %%FILE%% placeholder)
  QString partition = "batch"; ///< SLURM partition / queue
  int nodes = 1;
  int tasksPerNode = 4;
  bool requestGpus = false;
  int gpusPerNode = 1;
  QString spackRoot = "/opt/spack"; ///< Spack install prefix on remote
};

/// Terminal configuration for QTermWidget integration. Stored in QSettings under terminal/*.
struct TerminalSettings {
  QString shellPath = "";      ///< Shell executable path (defaults to $SHELL or /bin/bash if empty)
  QString fontFamily = "FiraCode Nerd Font"; ///< Default monospace font
  int fontSize = 12;           ///< Font size in points
};

struct ProjectSettings {
  std::string targetBinary;
  std::string programArguments;
  std::string environmentVariables;
  std::string workingDirectory;
  std::string customGdbPath = "gdb";
  std::vector<std::string> watchExpressions;
};

class ConfigManager {
public:
  static ConfigManager &instance() {
    static ConfigManager instance;
    return instance;
  }

  void loadConfig();

  // ── Theme / colour accessors (TOML-backed) ───────────────────────────
  QString getSourceBackground() const;
  QString getSourceText() const;
  QString getSourceActiveLine() const;

  QString getAssemblyOpcode() const;
  QString getAssemblyRegister() const;

  QString getDocsetDirectory() const;
  void setDocsetDirectory(const QString &path);
  QString getAssemblyAddress() const;

  // ── Debugger settings (QSettings-backed, single source of truth) ─────
  DebuggerSettings getDebuggerSettings() const;
  void saveDebuggerSettings(const DebuggerSettings &s);

  // ── HPC / cluster settings (QSettings-backed) ─────────────────────────
  HpcSettings getHpcSettings() const;
  void saveHpcSettings(const HpcSettings &s);

  // ── SSH connection settings (QSettings-backed) ─────────────────────
  SshSettings getSshSettings() const;
  void saveSshSettings(const SshSettings &s);

  // ── SLURM / Spack settings (QSettings-backed) ────────────────────
  SlurmSettings getSlurmSettings() const;
  void saveSlurmSettings(const SlurmSettings &s);

  // ── Terminal settings (QSettings-backed) ─────────────────────────────
  TerminalSettings getTerminalSettings() const;
  void saveTerminalSettings(const TerminalSettings &s);

  /// Convenience shims used by existing call-sites.
  int getDefaultRanks() const;
  QString getGdbPath() const;
  QString getMpiExecutable() const;
  QString getMpiArgs() const;

  /// Persist the last directory visited by a file-open dialog so the IDE
  /// restores it across launches.
  QString getLastOpenDir() const;
  void setLastOpenDir(const QString &path);

  // ── Breakpoints (TOML-backed) ─────────────────────────────────────────
  QMap<QString, QSet<int>> getBreakpoints(QString targetExecutable = QString()) const;
  void saveBreakpoints(const QMap<QString, QSet<int>> &breakpoints, QString targetExecutable = QString());

  // ── Project Settings (TOML-backed) ────────────────────────────────────
  ProjectSettings loadProjectSettings() const;
  void saveProjectSettings(const ProjectSettings &ps) const;

  void setWorkspace(const QString &path);
  QString workspacePath() const { return m_workspacePath; }

  // ── Global Tools (TOML-backed) ────────────────────────────────────────
  QString getGlobalClangdPath() const;
  void setGlobalClangdPath(const QString &path);

  QString getGlobalDapAdapterPath() const;
  void setGlobalDapAdapterPath(const QString &path);

  QString getGlobalGdbPath() const;
  void setGlobalGdbPath(const QString &path);

  QString getGlobalMpiArgs() const;
  void setGlobalMpiArgs(const QString &args);

  void saveGlobalConfig();

private:
  ConfigManager();
  ~ConfigManager() = default;

  ConfigManager(const ConfigManager &) = delete;
  ConfigManager &operator=(const ConfigManager &) = delete;

  toml::table m_config;
  QString m_workspacePath;
  QString m_globalConfigPath;

  static constexpr const char *kOrg = "gridlock";
  static constexpr const char *kApp = "debugger";
};

} // namespace gridlock::core