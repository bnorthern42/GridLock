/**
 * @file ConfigManagerToml.cpp
 * @brief Global `config.toml` bootstrap, legacy migration, and TOML-backed global/terminal/UI accessors.
 *
 * Implements first-run `config.toml` seeding, non-destructive migration from legacy
 * `~/.config/gridlock/gridlock/` and `~/.config/GridLock/` stores, `saveGlobalConfig()`,
 * global toolchain paths (`clangd`, `lldb-dap`, `gdb`, `mpi_launch_args`), syntax color
 * tokens, `[ui]` monitor routing hints, and `[terminal]` shell/font persistence.
 */

#include "ConfigManager.hpp"

#include <fstream>
#include <iostream>

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace gridlock::core {

/**
 * @brief Loads `config.toml` from disk, creating a default file on first launch.
 *
 * Respects `GRIDLOCK_TEST_CONFIG_DIR` when running under the automated test suite
 * so test cases never mutate the developer's personal `~/.config/gridlock/config.toml`.
 */
void ConfigManager::loadConfig() {
  const QString configDir = configDirectory();
  QDir dir(configDir);
  if (!dir.exists()) dir.mkpath(QStringLiteral("."));
  m_globalConfigPath = dir.filePath(QStringLiteral("config.toml"));

  // Non-destructive one-time migration from legacy ~/.config/gridlock/gridlock/config.toml and ~/.config/GridLock/*.conf.
  if (!QFile::exists(m_globalConfigPath) && !qEnvironmentVariableIsSet("GRIDLOCK_TEST_CONFIG_DIR")) {
    const QString legacyNested = dir.filePath(QStringLiteral("gridlock/config.toml"));
    if (QFile::exists(legacyNested)) QFile::copy(legacyNested, m_globalConfigPath);
  }
  if (!QFile::exists(workstationSettingsPath()) && !qEnvironmentVariableIsSet("GRIDLOCK_TEST_CONFIG_DIR")) {
    const QString genericBase = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    for (const QString& legacyName : {QStringLiteral("GridLock/GridLockWorkstation.conf"), QStringLiteral("GridLock/Workstation.conf")}) {
      const QString legacyFile = QDir(genericBase).filePath(legacyName);
      if (!QFile::exists(legacyFile)) continue;
      QSettings oldS(legacyFile, QSettings::IniFormat);
      QSettings newS(workstationSettingsPath(), QSettings::IniFormat);
      for (const QString& k : oldS.allKeys()) newS.setValue(k, oldS.value(k));
      newS.sync();
      break;
    }
  }

  // Seed a default `config.toml` if none exists on disk yet.
  if (!QFile::exists(m_globalConfigPath)) {
    QFile file(m_globalConfigPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream out(&file);
      out << "[global]\nclangd_path = \"clangd\"\ndap_adapter_path = \"lldb-dap\"\ngdb_path = \"gdb\"\nmpi_launch_args = \"--oversubscribe\"\n\n"
          << "[theme.source]\nbackground = \"#1e1e1e\"\ntext = \"#f0f0f0\"\nactive_line = \"#505000\"\n\n"
          << "[theme.assembly]\nopcode = \"#00ff7f\"\nregister = \"#ff7f50\"\naddress = \"#555555\"\n\n"
          << "[debugger]\ndefault_ranks = 2\ngdb_path = \"gdb\"\n\n"
          << "[ui]\npreferred_monitor = \"\"\nopen_unfocused = false\n";
      file.close();
    }
  }

  try {
    m_config = toml::parse_file(m_globalConfigPath.toStdString());
  } catch (const toml::parse_error& err) {
    std::cerr << "Parsing failed:\n" << err << "\n";
    m_config = toml::table();
  }
}

/** @brief Serializes the in-memory `m_config` TOML table to `m_globalConfigPath`. */
void ConfigManager::saveGlobalConfig() {
  std::ofstream out(m_globalConfigPath.toStdString());
  if (out.is_open()) {
    out << m_config;
  }
}

/** @brief Returns `[theme.source] background` from `config.toml`. */
QString ConfigManager::getSourceBackground() const {
  return QString::fromStdString(m_config["theme"]["source"]["background"].value_or("#1e1e1e"));
}

/** @brief Returns `[theme.source] text` from `config.toml`. */
QString ConfigManager::getSourceText() const {
  return QString::fromStdString(m_config["theme"]["source"]["text"].value_or("#f0f0f0"));
}

/** @brief Returns `[theme.source] active_line` from `config.toml`. */
QString ConfigManager::getSourceActiveLine() const {
  return QString::fromStdString(m_config["theme"]["source"]["active_line"].value_or("#505000"));
}

/** @brief Returns `[theme.assembly] opcode` from `config.toml`. */
QString ConfigManager::getAssemblyOpcode() const {
  return QString::fromStdString(m_config["theme"]["assembly"]["opcode"].value_or("#00ff7f"));
}

/** @brief Returns `[theme.assembly] register` from `config.toml`. */
QString ConfigManager::getAssemblyRegister() const {
  return QString::fromStdString(m_config["theme"]["assembly"]["register"].value_or("#ff7f50"));
}

/** @brief Returns `[theme.assembly] address` from `config.toml`. */
QString ConfigManager::getAssemblyAddress() const {
  return QString::fromStdString(m_config["theme"]["assembly"]["address"].value_or("#555555"));
}

/** @brief Returns `[global] clangd_path` from `config.toml`. */
QString ConfigManager::getGlobalClangdPath() const {
  return QString::fromStdString(m_config["global"]["clangd_path"].value_or("clangd"));
}

/** @brief Sets `[global] clangd_path` in `config.toml` and flushes to disk. */
void ConfigManager::setGlobalClangdPath(const QString& path) {
  if (!m_config.contains("global")) m_config.insert("global", toml::table{});
  m_config["global"].as_table()->insert_or_assign("clangd_path", path.toStdString());
  saveGlobalConfig();
}

/** @brief Returns `[global] dap_adapter_path` from `config.toml`. */
QString ConfigManager::getGlobalDapAdapterPath() const {
  return QString::fromStdString(m_config["global"]["dap_adapter_path"].value_or("lldb-dap"));
}

/** @brief Sets `[global] dap_adapter_path` in `config.toml` and flushes to disk. */
void ConfigManager::setGlobalDapAdapterPath(const QString& path) {
  if (!m_config.contains("global")) m_config.insert("global", toml::table{});
  m_config["global"].as_table()->insert_or_assign("dap_adapter_path", path.toStdString());
  saveGlobalConfig();
}

/** @brief Returns `[global] gdb_path` from `config.toml`. */
QString ConfigManager::getGlobalGdbPath() const {
  return QString::fromStdString(m_config["global"]["gdb_path"].value_or("gdb"));
}

/** @brief Sets `[global] gdb_path` in `config.toml` and flushes to disk. */
void ConfigManager::setGlobalGdbPath(const QString& path) {
  if (!m_config.contains("global")) m_config.insert("global", toml::table{});
  m_config["global"].as_table()->insert_or_assign("gdb_path", path.toStdString());
  saveGlobalConfig();
}

/** @brief Returns `[global] mpi_launch_args` from `config.toml`. */
QString ConfigManager::getGlobalMpiArgs() const {
  return QString::fromStdString(m_config["global"]["mpi_launch_args"].value_or("--oversubscribe"));
}

/** @brief Sets `[global] mpi_launch_args` in `config.toml` and flushes to disk. */
void ConfigManager::setGlobalMpiArgs(const QString& args) {
  if (!m_config.contains("global")) m_config.insert("global", toml::table{});
  m_config["global"].as_table()->insert_or_assign("mpi_launch_args", args.toStdString());
  saveGlobalConfig();
}

/** @brief Returns `[ui] preferred_monitor` from `config.toml`. */
QString ConfigManager::preferredMonitor() const {
  return QString::fromStdString(m_config["ui"]["preferred_monitor"].value_or(""));
}

/** @brief Sets `[ui] preferred_monitor` in `config.toml` and flushes to disk. */
void ConfigManager::setPreferredMonitor(const QString& monitorHint) {
  if (!m_config.contains("ui")) m_config.insert("ui", toml::table{});
  m_config["ui"].as_table()->insert_or_assign("preferred_monitor", monitorHint.toStdString());
  saveGlobalConfig();
}

/** @brief Returns `[ui] open_unfocused` from `config.toml`. */
bool ConfigManager::openUnfocused() const {
  return m_config["ui"]["open_unfocused"].value_or(false);
}

/** @brief Sets `[ui] open_unfocused` in `config.toml` and flushes to disk. */
void ConfigManager::setOpenUnfocused(bool unfocused) {
  if (!m_config.contains("ui")) m_config.insert("ui", toml::table{});
  m_config["ui"].as_table()->insert_or_assign("open_unfocused", unfocused);
  saveGlobalConfig();
}

/** @brief Retrieves embedded terminal shell and font settings from `config.toml`. */
TerminalSettings ConfigManager::getTerminalSettings() const {
  TerminalSettings ts;
  QString defaultShell = qEnvironmentVariable("SHELL");
  if (defaultShell.isEmpty()) {
    defaultShell = "/bin/bash";
  }
  ts.shellPath = QString::fromStdString(m_config["terminal"]["shell_path"].value_or(defaultShell.toStdString()));
  ts.fontFamily = QString::fromStdString(m_config["terminal"]["font_family"].value_or("FiraCode Nerd Font"));
  ts.fontSize = m_config["terminal"]["font_size"].value_or(12);
  return ts;
}

/** @brief Persists embedded terminal shell and font settings to `config.toml`. */
void ConfigManager::saveTerminalSettings(const TerminalSettings& ts) {
  if (!m_config.contains("terminal")) {
    m_config.insert("terminal", toml::table{});
  }
  auto* table = m_config["terminal"].as_table();
  table->insert_or_assign("shell_path", ts.shellPath.toStdString());
  table->insert_or_assign("font_family", ts.fontFamily.toStdString());
  table->insert_or_assign("font_size", qMax(1, ts.fontSize));
  saveGlobalConfig();
}

}  // namespace gridlock::core
