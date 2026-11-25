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
          << "[ui]\npreferred_monitor = \"\"\nopen_unfocused = false\nshow_menubar_window_controls = true\n";
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

/** @brief Reads `[ui] show_menubar_window_controls` from `config.toml` (falling back to `QSettings` or `true`). */
bool ConfigManager::showMenuBarWindowControls() const {
  if (const auto* uiTbl = m_config["ui"].as_table()) {
    if (auto bv = (*uiTbl)["show_menubar_window_controls"].value<bool>()) return *bv;
  }
  QSettings s(kOrg, kApp);
  return s.value(QStringLiteral("ui/show_menubar_window_controls"), true).toBool();
}

/** @brief Persists `[ui] show_menubar_window_controls` to `config.toml` and `QSettings`, then notifies live listeners. */
void ConfigManager::setShowMenuBarWindowControls(bool enabled) {
  if (!m_config.contains("ui")) m_config.insert("ui", toml::table{});
  m_config["ui"].as_table()->insert_or_assign("show_menubar_window_controls", enabled);
  saveGlobalConfig();
  QSettings s(kOrg, kApp);
  s.setValue(QStringLiteral("ui/show_menubar_window_controls"), enabled);
  s.sync();
  auto it = m_menuBarWindowControlsListeners.begin();
  while (it != m_menuBarWindowControlsListeners.end()) {
    if (it->first.isNull()) {
      it = m_menuBarWindowControlsListeners.erase(it);
    } else {
      auto cb = it->second;
      ++it;
      if (cb) cb(enabled);
    }
  }
}

namespace {
QStringList normalizeToolbarTokens(const QStringList& raw) {
  static const QSet<QString> kKnownControls = {
      QStringLiteral("target_selector"), QStringLiteral("config_menu"),
      QStringLiteral("rank_spin"),       QStringLiteral("build"),
      QStringLiteral("run"),             QStringLiteral("continue"),
      QStringLiteral("step_over"),       QStringLiteral("step_into"),
      QStringLiteral("step_out"),        QStringLiteral("pause"),
      QStringLiteral("stop"),            QStringLiteral("record"),
      QStringLiteral("search_everywhere"), QStringLiteral("perspectives")};
  QStringList out;
  QSet<QString> seenControls;
  for (const QString& entry : raw) {
    const QString tok = entry.trimmed().toLower();
    if (tok == QStringLiteral("spacer") || tok == QStringLiteral("flex_spacer") || tok == QStringLiteral("separator")) {
      out.append(tok);
    } else if (kKnownControls.contains(tok) && !seenControls.contains(tok)) {
      seenControls.insert(tok);
      out.append(tok);
    }
  }
  static const QStringList kLegacyDefault = {
      QStringLiteral("target_selector"), QStringLiteral("config_menu"),
      QStringLiteral("rank_spin"),       QStringLiteral("separator"),
      QStringLiteral("build"),           QStringLiteral("run"),
      QStringLiteral("continue"),        QStringLiteral("step_over"),
      QStringLiteral("step_into"),       QStringLiteral("step_out"),
      QStringLiteral("pause"),           QStringLiteral("stop"),
      QStringLiteral("record"),          QStringLiteral("spacer"),
      QStringLiteral("search_everywhere"), QStringLiteral("perspectives")};
  if (out == kLegacyDefault) out[13] = QStringLiteral("flex_spacer");
  return out;
}
}  // namespace

/** @brief Reads `[ui] toolbar_layout` from `config.toml` (falling back to `QSettings` or `defaultToolbarLayout()`). */
QStringList ConfigManager::toolbarLayout() const {
  if (const auto* uiTbl = m_config["ui"].as_table()) {
    if (const auto* arr = (*uiTbl)["toolbar_layout"].as_array()) {
      QStringList raw;
      for (const auto& node : *arr) {
        if (auto sv = node.value<std::string>()) raw.append(QString::fromStdString(*sv));
      }
      const QStringList norm = normalizeToolbarTokens(raw);
      if (!norm.isEmpty()) return norm;
    }
  }
  QSettings s(kOrg, kApp);
  if (s.contains(QStringLiteral("ui/toolbar_layout"))) {
    const QStringList norm = normalizeToolbarTokens(s.value(QStringLiteral("ui/toolbar_layout")).toStringList());
    if (!norm.isEmpty()) return norm;
  }
  return defaultToolbarLayout();
}

/** @brief Persists `[ui] toolbar_layout` to `config.toml` and `QSettings`, then broadcasts `toolbarLayoutChanged()`. */
void ConfigManager::setToolbarLayout(const QStringList& items) {
  QStringList norm = normalizeToolbarTokens(items);
  if (norm.isEmpty()) norm = defaultToolbarLayout();
  if (!m_config.contains("ui")) m_config.insert("ui", toml::table{});
  toml::array arr;
  for (const QString& tok : norm) arr.push_back(tok.toStdString());
  m_config["ui"].as_table()->insert_or_assign("toolbar_layout", std::move(arr));
  saveGlobalConfig();
  QSettings s(kOrg, kApp);
  s.setValue(QStringLiteral("ui/toolbar_layout"), norm);
  s.sync();
  toolbarLayoutChanged();
}

namespace {
QString normalizeSearchPopupPreset(const QString& raw) {
  const QString v = raw.trimmed().toLower();
  if (v == QStringLiteral("small") || v == QStringLiteral("s")) return QStringLiteral("small");
  if (v == QStringLiteral("medium") || v == QStringLiteral("m")) return QStringLiteral("medium");
  if (v == QStringLiteral("xlarge") || v == QStringLiteral("x-large") || v == QStringLiteral("xl")) return QStringLiteral("xlarge");
  if (v == QStringLiteral("large") || v == QStringLiteral("l")) return QStringLiteral("large");
  return QString();
}
}  // namespace

/** @brief Reads `[ui] search_popup_size` from `config.toml` (falling back to `QSettings` or `"large"`). */
QString ConfigManager::searchPopupSize() const {
  if (const auto* uiTbl = m_config["ui"].as_table()) {
    if (auto sv = (*uiTbl)["search_popup_size"].value<std::string>()) {
      const QString norm = normalizeSearchPopupPreset(QString::fromStdString(*sv));
      if (!norm.isEmpty()) return norm;
    }
  }
  QSettings s(kOrg, kApp);
  if (s.contains(QStringLiteral("ui/search_popup_size"))) {
    const QString norm = normalizeSearchPopupPreset(s.value(QStringLiteral("ui/search_popup_size")).toString());
    if (!norm.isEmpty()) return norm;
  }
  return QStringLiteral("large");
}

/** @brief Persists `[ui] search_popup_size` to `config.toml` and `QSettings`, then notifies live listeners. */
void ConfigManager::setSearchPopupSize(const QString& preset) {
  QString norm = normalizeSearchPopupPreset(preset);
  if (norm.isEmpty()) norm = QStringLiteral("large");
  if (!m_config.contains("ui")) m_config.insert("ui", toml::table{});
  m_config["ui"].as_table()->insert_or_assign("search_popup_size", norm.toStdString());
  saveGlobalConfig();
  QSettings s(kOrg, kApp);
  s.setValue(QStringLiteral("ui/search_popup_size"), norm);
  s.sync();
  auto it = m_searchPopupSizeListeners.begin();
  while (it != m_searchPopupSizeListeners.end()) {
    if (it->first.isNull()) {
      it = m_searchPopupSizeListeners.erase(it);
    } else {
      auto cb = it->second;
      ++it;
      if (cb) cb(norm);
    }
  }
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

/** @brief Reads `[plugins] disabled_ids` from `config.toml` (falling back to `QSettings`). */
QStringList ConfigManager::disabledPluginIds() const {
  QStringList out;
  auto appendUnique = [&out](const QString& raw) {
    const QString trimmed = raw.trimmed();
    if (!trimmed.isEmpty() && !out.contains(trimmed)) out.append(trimmed);
  };
  if (const auto* plugTbl = m_config["plugins"].as_table()) {
    if (const auto* arr = (*plugTbl)["disabled_ids"].as_array()) {
      for (const auto& node : *arr) {
        if (auto sv = node.value<std::string>()) appendUnique(QString::fromStdString(*sv));
      }
      return out;
    }
  }
  QSettings s(kOrg, kApp);
  for (const QString& id : s.value(QStringLiteral("plugins/disabled_ids")).toStringList()) {
    appendUnique(id);
  }
  return out;
}

/** @brief Persists `[plugins] disabled_ids` to `config.toml` and `QSettings`. */
void ConfigManager::setDisabledPluginIds(const QStringList& ids) {
  QStringList norm;
  for (const QString& raw : ids) {
    const QString trimmed = raw.trimmed();
    if (!trimmed.isEmpty() && !norm.contains(trimmed)) norm.append(trimmed);
  }
  if (!m_config.contains("plugins")) m_config.insert("plugins", toml::table{});
  toml::array arr;
  for (const QString& id : norm) arr.push_back(id.toStdString());
  m_config["plugins"].as_table()->insert_or_assign("disabled_ids", std::move(arr));
  saveGlobalConfig();
  QSettings s(kOrg, kApp);
  s.setValue(QStringLiteral("plugins/disabled_ids"), norm);
  s.sync();
}

}  // namespace gridlock::core
