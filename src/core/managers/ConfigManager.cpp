#include "ConfigManager.hpp"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <fstream>
#include <iostream>

namespace gridlock::core {

// ─── Construction / TOML loading ────────────────────────────────────────────

ConfigManager::ConfigManager() {
  m_workspacePath = QDir::currentPath();
  loadConfig();
}

void ConfigManager::setWorkspace(const QString &path) {
  m_workspacePath = path;
}

void ConfigManager::loadConfig() {
  QString configDir =
      QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  if (qEnvironmentVariableIsSet("GRIDLOCK_TEST_CONFIG_DIR")) {
    configDir = qEnvironmentVariable("GRIDLOCK_TEST_CONFIG_DIR");
  }

  QDir dir(configDir);
  if (!dir.exists()) {
    dir.mkpath(".");
  }
  m_globalConfigPath = dir.filePath("config.toml");

  if (!QFile::exists(m_globalConfigPath)) {
    QFile file(m_globalConfigPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream out(&file);
      out << "[global]\n";
      out << "clangd_path = \"clangd\"\n";
      out << "dap_adapter_path = \"lldb-dap\"\n";
      out << "gdb_path = \"gdb\"\n";
      out << "mpi_launch_args = \"--oversubscribe\"\n\n";

      out << "[theme.source]\n";
      out << "background = \"#1e1e1e\"\n";
      out << "text = \"#f0f0f0\"\n";
      out << "active_line = \"#505000\"\n\n";

      out << "[theme.assembly]\n";
      out << "opcode = \"#00ff7f\"\n";
      out << "register = \"#ff7f50\"\n";
      out << "address = \"#555555\"\n\n";

      out << "[debugger]\n";
      out << "default_ranks = 2\n";
      out << "gdb_path = \"gdb\"\n";
      file.close();
    }
  }

  try {
    m_config = toml::parse_file(m_globalConfigPath.toStdString());
  } catch (const toml::parse_error &err) {
    std::cerr << "Parsing failed:\n" << err << "\n";
    m_config = toml::table();
  }
}

// ─── Theme accessors (TOML) ──────────────────────────────────────────────────

QString ConfigManager::getSourceBackground() const {
  return QString::fromStdString(
      m_config["theme"]["source"]["background"].value_or("#1e1e1e"));
}

QString ConfigManager::getSourceText() const {
  return QString::fromStdString(
      m_config["theme"]["source"]["text"].value_or("#f0f0f0"));
}

QString ConfigManager::getSourceActiveLine() const {
  return QString::fromStdString(
      m_config["theme"]["source"]["active_line"].value_or("#505000"));
}

// ─── Global settings (TOML) ──────────────────────────────────────────────────

QString ConfigManager::getGlobalClangdPath() const {
  return QString::fromStdString(
      m_config["global"]["clangd_path"].value_or("clangd"));
}

void ConfigManager::setGlobalClangdPath(const QString &path) {
  if (!m_config.contains("global"))
    m_config.insert("global", toml::table{});
  m_config["global"].as_table()->insert_or_assign("clangd_path",
                                                  path.toStdString());
  saveGlobalConfig();
}

QString ConfigManager::getAssemblyOpcode() const {
  return QString::fromStdString(
      m_config["theme"]["assembly"]["opcode"].value_or("#00ff7f"));
}

QString ConfigManager::getAssemblyRegister() const {
  return QString::fromStdString(
      m_config["theme"]["assembly"]["register"].value_or("#ff7f50"));
}

QString ConfigManager::getAssemblyAddress() const {
  return QString::fromStdString(
      m_config["theme"]["assembly"]["address"].value_or("#555555"));
}

QString ConfigManager::getGlobalDapAdapterPath() const {
  return QString::fromStdString(
      m_config["global"]["dap_adapter_path"].value_or("lldb-dap"));
}

void ConfigManager::setGlobalDapAdapterPath(const QString &path) {
  if (!m_config.contains("global"))
    m_config.insert("global", toml::table{});
  m_config["global"].as_table()->insert_or_assign("dap_adapter_path",
                                                  path.toStdString());
  saveGlobalConfig();
}

QString ConfigManager::getGlobalGdbPath() const {
  return QString::fromStdString(m_config["global"]["gdb_path"].value_or("gdb"));
}

void ConfigManager::setGlobalGdbPath(const QString &path) {
  if (!m_config.contains("global"))
    m_config.insert("global", toml::table{});
  m_config["global"].as_table()->insert_or_assign("gdb_path",
                                                  path.toStdString());
  saveGlobalConfig();
}

QString ConfigManager::getGlobalMpiArgs() const {
  return QString::fromStdString(
      m_config["global"]["mpi_launch_args"].value_or("--oversubscribe"));
}

void ConfigManager::setGlobalMpiArgs(const QString &args) {
  if (!m_config.contains("global"))
    m_config.insert("global", toml::table{});
  m_config["global"].as_table()->insert_or_assign("mpi_launch_args",
                                                  args.toStdString());
  saveGlobalConfig();
}

void ConfigManager::saveGlobalConfig() {
  std::ofstream out(m_globalConfigPath.toStdString());
  if (out.is_open()) {
    out << m_config;
  }
}

// ─── Debugger settings — QSettings-backed single source of truth ─────────────

DebuggerSettings ConfigManager::getDebuggerSettings() const {
  QSettings s(kOrg, kApp);

  // Seed defaults from TOML so the first-run experience reads the file.
  const int tomlRanks = m_config["debugger"]["default_ranks"].value_or(2);
  const QString tomlGdb =
      QString::fromStdString(m_config["debugger"]["gdb_path"].value_or("gdb"));

  DebuggerSettings ds;
  ds.gdbPath = s.value("debugger/gdb_path", tomlGdb).toString();
  ds.mpiExecutable = s.value("debugger/mpi_executable", "mpiexec").toString();
  ds.mpiArgs = s.value("debugger/mpi_args", "--oversubscribe").toString();
  ds.defaultRanks = s.value("debugger/default_ranks", tomlRanks).toInt();
  ds.trapFpe = s.value("debugger/trap_fpe", false).toBool();

  // Safety clamp: reject non-positive values stored by an old/buggy write.
  if (ds.defaultRanks < 1)
    ds.defaultRanks = 1;

  return ds;
}

void ConfigManager::saveDebuggerSettings(const DebuggerSettings &ds) {
  QSettings s(kOrg, kApp);
  s.setValue("debugger/gdb_path", ds.gdbPath);
  s.setValue("debugger/mpi_executable", ds.mpiExecutable);
  s.setValue("debugger/mpi_args", ds.mpiArgs);
  // Clamp before persisting so we never write an invalid value.
  s.setValue("debugger/default_ranks", qMax(1, ds.defaultRanks));
  s.setValue("debugger/trap_fpe", ds.trapFpe);
  s.sync();
}

// ─── HPC settings
// ─────────────────────────────────────────────────────────────

HpcSettings ConfigManager::getHpcSettings() const {
  QSettings s(kOrg, kApp);
  HpcSettings hs;
  hs.hostsFile = s.value("hpc/hosts_file", "").toString();
  hs.envVars = s.value("hpc/env_vars", "").toString();
  hs.strictAffinity = s.value("hpc/strict_affinity", false).toBool();
  return hs;
}

void ConfigManager::saveHpcSettings(const HpcSettings &hs) {
  QSettings s(kOrg, kApp);
  s.setValue("hpc/hosts_file", hs.hostsFile);
  s.setValue("hpc/env_vars", hs.envVars);
  s.setValue("hpc/strict_affinity", hs.strictAffinity);
  s.sync();
}

// ─── SSH settings ───────────────────────────────────────────────────────────

SshSettings ConfigManager::getSshSettings() const {
  QSettings s(kOrg, kApp);
  SshSettings ss;
  ss.host = s.value("ssh/host", "").toString();
  ss.user = s.value("ssh/user", "").toString();
  ss.keyPath = s.value("ssh/key_path", "").toString();
  return ss;
}

void ConfigManager::saveSshSettings(const SshSettings &ss) {
  QSettings s(kOrg, kApp);
  s.setValue("ssh/host", ss.host);
  s.setValue("ssh/user", ss.user);
  s.setValue("ssh/key_path", ss.keyPath);
  s.sync();
}

// ─── SLURM / Spack settings ────────────────────────────────────────────────

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

void ConfigManager::saveSlurmSettings(const SlurmSettings &sl) {
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

// ─── Convenience shims ───────────────────────────────────────────────────────

int ConfigManager::getDefaultRanks() const {
  return getDebuggerSettings().defaultRanks;
}
QString ConfigManager::getGdbPath() const {
  return QString::fromStdString(loadProjectSettings().customGdbPath);
}
QString ConfigManager::getMpiExecutable() const {
  return getDebuggerSettings().mpiExecutable;
}
QString ConfigManager::getMpiArgs() const {
  return getDebuggerSettings().mpiArgs;
}

QString ConfigManager::getLastOpenDir() const {
  QSettings s(kOrg, kApp);
  return s.value("ui/last_open_dir", QDir::currentPath()).toString();
}

void ConfigManager::setLastOpenDir(const QString &path) {
  QSettings s(kOrg, kApp);
  s.setValue("ui/last_open_dir", path);
  s.sync();
}

QString ConfigManager::getDocsetDirectory() const {
  QSettings s(kOrg, kApp);
  return s.value("docsets/directory", "").toString();
}

void ConfigManager::setDocsetDirectory(const QString &path) {
  QSettings s(kOrg, kApp);
  s.setValue("docsets/directory", path);
  s.sync();
}

// ─── Breakpoints (TOML) ──────────────────────────────────────────────────────

QMap<QString, QSet<int>> ConfigManager::getBreakpoints(QString targetExecutable) const {
  QMap<QString, QSet<int>> breakpoints;
  if (m_workspacePath.isEmpty())
    return breakpoints;

  if (targetExecutable.isEmpty()) {
    targetExecutable = QString::fromStdString(loadProjectSettings().targetBinary);
  }

  QString workspaceFile =
      QDir(m_workspacePath).filePath(".gridlock/workspace.toml");
  toml::table tbl;
  try {
    tbl = toml::parse_file(workspaceFile.toStdString());
  } catch (...) {
    return breakpoints;
  }

  if (auto *bps = tbl["breakpoints"].as_table()) {
    if (auto *targetBps = bps->get(targetExecutable.toStdString())) {
      if (auto *targetTbl = targetBps->as_table()) {
        for (auto &[file, lines] : *targetTbl) {
          if (auto *linesArr = lines.as_array()) {
            // Treat the key as an absolute path as requested (or resolve relative if needed, but we will save absolute paths).
            QString path = QString::fromStdString(std::string(file.str()));
            if (QFileInfo(path).isRelative()) {
              path = QDir(m_workspacePath).filePath(path);
            }
            QSet<int> lineSet;
            for (auto &elem : *linesArr) {
              if (auto *val = elem.as_integer()) {
                lineSet.insert(val->get());
              }
            }
            breakpoints[QFileInfo(path).absoluteFilePath()] = lineSet;
          }
        }
      }
    }
  }
  return breakpoints;
}

void ConfigManager::saveBreakpoints(
    const QMap<QString, QSet<int>> &breakpoints, QString targetExecutable) {
  if (m_workspacePath.isEmpty())
    return;

  if (targetExecutable.isEmpty()) {
    targetExecutable = QString::fromStdString(loadProjectSettings().targetBinary);
  }

  QDir dir(m_workspacePath);
  if (!dir.exists(".gridlock"))
    dir.mkdir(".gridlock");
  QString workspaceFile = dir.filePath(".gridlock/workspace.toml");

  toml::table tbl;
  try {
    tbl = toml::parse_file(workspaceFile.toStdString());
  } catch (...) {
  }

  toml::table allBpsTable;
  if (auto *existingBps = tbl["breakpoints"].as_table()) {
    allBpsTable = *existingBps;
  }

  toml::table targetBpsTable;
  for (auto it = breakpoints.constBegin(); it != breakpoints.constEnd(); ++it) {
    toml::array linesArr;
    for (int line : it.value()) {
      linesArr.push_back(line);
    }
    // We were requested to store absolute paths to avoid 'No source file named...' issues
    QString absPath = QFileInfo(it.key()).absoluteFilePath();
    targetBpsTable.insert(absPath.toStdString(), linesArr);
  }
  
  allBpsTable.insert_or_assign(targetExecutable.toStdString(), targetBpsTable);
  tbl.insert_or_assign("breakpoints", allBpsTable);

  std::ofstream out(workspaceFile.toStdString());
  if (out.is_open())
    out << tbl;
}

ProjectSettings ConfigManager::loadProjectSettings() const {
  ProjectSettings ps;
  if (m_workspacePath.isEmpty())
    return ps;

  QString settingsFile =
      QDir(m_workspacePath).filePath(".gridlock/settings.toml");
  toml::table tbl;
  try {
    tbl = toml::parse_file(settingsFile.toStdString());
  } catch (...) {
  }

  if (auto *proj = tbl["project"].as_table()) {
    ps.targetBinary = (*proj)["targetBinary"].value_or("");
    ps.programArguments = (*proj)["programArguments"].value_or("");
    ps.environmentVariables = (*proj)["environmentVariables"].value_or("");
    ps.workingDirectory = (*proj)["workingDirectory"].value_or("");
    ps.customGdbPath = (*proj)["customGdbPath"].value_or("gdb");
  }

  QString workspaceFile =
      QDir(m_workspacePath).filePath(".gridlock/workspace.toml");
  toml::table wTbl;
  try {
    wTbl = toml::parse_file(workspaceFile.toStdString());
  } catch (...) {
  }

  if (auto *proj = wTbl["workspace"].as_table()) {
    if (auto *watches = (*proj)["watchExpressions"].as_array()) {
      for (auto &elem : *watches) {
        if (auto *str = elem.as_string()) {
          ps.watchExpressions.push_back(str->get());
        }
      }
    }
  }
  return ps;
}

void ConfigManager::saveProjectSettings(const ProjectSettings &ps) const {
  if (m_workspacePath.isEmpty())
    return;

  QDir dir(m_workspacePath);
  if (!dir.exists(".gridlock"))
    dir.mkdir(".gridlock");
  QString settingsFile = dir.filePath(".gridlock/settings.toml");

  toml::table tbl;
  try {
    tbl = toml::parse_file(settingsFile.toStdString());
  } catch (...) {
  }

  toml::table projTbl;
  projTbl.insert_or_assign("targetBinary",
                           ps.targetBinary.empty() ? "" : ps.targetBinary);
  projTbl.insert_or_assign("programArguments", ps.programArguments.empty()
                                                   ? ""
                                                   : ps.programArguments);
  projTbl.insert_or_assign(
      "environmentVariables",
      ps.environmentVariables.empty() ? "" : ps.environmentVariables);
  projTbl.insert_or_assign("workingDirectory", ps.workingDirectory.empty()
                                                   ? ""
                                                   : ps.workingDirectory);
  projTbl.insert_or_assign("customGdbPath",
                           ps.customGdbPath.empty() ? "gdb" : ps.customGdbPath);

  tbl.insert_or_assign("project", projTbl);

  std::ofstream out(settingsFile.toStdString());
  if (out.is_open())
    out << tbl;

  QString workspaceFile = dir.filePath(".gridlock/workspace.toml");
  toml::table wTbl;
  try {
    wTbl = toml::parse_file(workspaceFile.toStdString());
  } catch (...) {
  }

  toml::table wProjTbl;
  if (auto *existing = wTbl["workspace"].as_table()) {
    wProjTbl = *existing;
  }

  toml::array watches;
  for (const auto &w : ps.watchExpressions) {
    if (!w.empty()) {
      watches.push_back(w);
    }
  }
  wProjTbl.insert_or_assign("watchExpressions", watches);
  wTbl.insert_or_assign("workspace", wProjTbl);

  std::ofstream wOut(workspaceFile.toStdString());
  if (wOut.is_open())
    wOut << wTbl;
}

} // namespace gridlock::core