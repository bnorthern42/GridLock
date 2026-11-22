/**
 * @file ConfigManagerWorkspace.cpp
 * @brief Per-workspace `.gridlock/` TOML persistence, breakpoints, project settings, and recent sessions.
 *
 * Implements source-line and conditional/logpoint breakpoint persistence,
 * `.gridlock/settings.toml` and `.gridlock/workspace.toml` project/watch state,
 * workspace-scoped theme/debugger-engine/disassembly/telemetry overrides, and
 * recent workspace/session MRU list management.
 */

#include "ConfigManager.hpp"

#include <fstream>
#include <string>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace gridlock::core {

namespace {

QString normalizeRecentPath(const QString& raw) {
  const QString t = raw.trimmed();
  if (t.isEmpty()) return QString();
  const QFileInfo fi(t);
  return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : QDir::cleanPath(fi.absoluteFilePath());
}

}  // namespace

/**
 * @brief Loads source breakpoints for @p targetExecutable from `.gridlock/workspace.toml`.
 *
 * Normalizes relative file keys against `m_workspacePath` so GDB always receives
 * canonical absolute file paths.
 */
QMap<QString, QSet<int>> ConfigManager::getBreakpoints(QString targetExecutable) const {
  QMap<QString, QSet<int>> breakpoints;
  if (m_workspacePath.isEmpty()) return breakpoints;

  if (targetExecutable.isEmpty()) {
    targetExecutable = QString::fromStdString(loadProjectSettings().targetBinary);
  }

  const QString workspaceFile = QDir(m_workspacePath).filePath(".gridlock/workspace.toml");
  toml::table tbl;
  try {
    tbl = toml::parse_file(workspaceFile.toStdString());
  } catch (...) {
    return breakpoints;
  }

  if (auto* bps = tbl["breakpoints"].as_table()) {
    if (auto* targetBps = bps->get(targetExecutable.toStdString())) {
      if (auto* targetTbl = targetBps->as_table()) {
        for (auto& [file, lines] : *targetTbl) {
          if (auto* linesArr = lines.as_array()) {
            QString path = QString::fromStdString(std::string(file.str()));
            if (QFileInfo(path).isRelative()) path = QDir(m_workspacePath).filePath(path);
            QSet<int> lineSet;
            for (auto& elem : *linesArr) {
              if (auto* val = elem.as_integer()) lineSet.insert(val->get());
            }
            breakpoints[QFileInfo(path).absoluteFilePath()] = lineSet;
          }
        }
      }
    }
  }
  return breakpoints;
}

/** @brief Saves source breakpoints for @p targetExecutable to `.gridlock/workspace.toml`. */
void ConfigManager::saveBreakpoints(const QMap<QString, QSet<int>>& breakpoints, QString targetExecutable) {
  if (m_workspacePath.isEmpty()) return;

  if (targetExecutable.isEmpty()) {
    targetExecutable = QString::fromStdString(loadProjectSettings().targetBinary);
  }

  QDir dir(m_workspacePath);
  if (!dir.exists(".gridlock")) dir.mkdir(".gridlock");
  const QString workspaceFile = dir.filePath(".gridlock/workspace.toml");

  toml::table tbl;
  try { tbl = toml::parse_file(workspaceFile.toStdString()); } catch (...) {}

  toml::table allBpsTable;
  if (auto* existingBps = tbl["breakpoints"].as_table()) allBpsTable = *existingBps;

  // Store canonical absolute source paths to prevent GDB `No source file named...` errors.
  toml::table targetBpsTable;
  for (auto it = breakpoints.constBegin(); it != breakpoints.constEnd(); ++it) {
    toml::array linesArr;
    for (int line : *it) linesArr.push_back(line);
    const QString absPath = QFileInfo(it.key()).absoluteFilePath();
    targetBpsTable.insert(absPath.toStdString(), linesArr);
  }

  allBpsTable.insert_or_assign(targetExecutable.toStdString(), targetBpsTable);
  tbl.insert_or_assign("breakpoints", allBpsTable);

  std::ofstream out(workspaceFile.toStdString());
  if (out.is_open()) out << tbl;
}

/** @brief Loads extended breakpoint metadata (condition, ignoreCount, logMessage) from QSettings. */
QList<BreakpointConfig> ConfigManager::getBreakpointConfigs(const QString& targetExecutable) const {
  QSettings s("gridlock", "debugger");
  const int n = s.beginReadArray(QStringLiteral("bp_meta/%1").arg(targetExecutable.isEmpty() ? QStringLiteral("default") : targetExecutable));
  QList<BreakpointConfig> out;
  for (int i = 0; i < n; ++i) {
    s.setArrayIndex(i);
    out.append({s.value("file").toString(), s.value("line").toInt(), s.value("rank", -1).toInt(),
                s.value("cond").toString(), s.value("ignore", 0).toInt(), s.value("log").toString()});
  }
  s.endArray();
  return out;
}

/** @brief Persists extended breakpoint metadata (condition, ignoreCount, logMessage) to INI settings. */
void ConfigManager::saveBreakpointConfigs(const QList<BreakpointConfig>& configs, const QString& targetExecutable) {
  QSettings s(debuggerSettingsPath(), QSettings::IniFormat);
  s.beginWriteArray(QStringLiteral("bp_meta/%1").arg(targetExecutable.isEmpty() ? QStringLiteral("default") : targetExecutable));
  for (int i = 0; i < configs.size(); ++i) {
    s.setArrayIndex(i);
    s.setValue("file", configs[i].file);
    s.setValue("line", configs[i].line);
    s.setValue("rank", configs[i].rankFilter);
    s.setValue("cond", configs[i].condition);
    s.setValue("ignore", qMax(0, configs[i].ignoreCount));
    s.setValue("log", configs[i].logMessage);
  }
  s.endArray();
}

/** @brief Loads target binary settings from `.gridlock/settings.toml` and watches from `workspace.toml`. */
ProjectSettings ConfigManager::loadProjectSettings() const {
  const DebuggerSettings ds = getDebuggerSettings();
  ProjectSettings ps;
  ps.mpiRanks = ds.defaultRanks;
  ps.mpiArgs = ds.mpiArgs;
  if (m_workspacePath.isEmpty()) {
    QSettings s(kOrg, kApp);
    ps.targetBinary = s.value("project/targetBinary", "").toString().toStdString();
    ps.targetArguments = s.value("project/targetArguments", "").toString();
    ps.programArguments = ps.targetArguments.toStdString();
    ps.workingDirectory = s.value("project/workingDirectory", "").toString().toStdString();
    ps.mpiRanks = qMax(1, s.value("project/mpiRanks", ds.defaultRanks).toInt());
    ps.mpiArgs = s.value("project/mpiArgs", ds.mpiArgs).toString();
    return ps;
  }
  const QString settingsFile = QDir(m_workspacePath).filePath(".gridlock/settings.toml");
  toml::table tbl;
  try { tbl = toml::parse_file(settingsFile.toStdString()); } catch (...) {}
  if (auto* proj = tbl["project"].as_table()) {
    ps.targetBinary = (*proj)["targetBinary"].value_or("");
    ps.programArguments = (*proj)["programArguments"].value_or("");
    const std::string tArgs = (*proj)["targetArguments"].value_or(ps.programArguments);
    ps.targetArguments = QString::fromStdString(tArgs);
    if (ps.programArguments.empty()) ps.programArguments = tArgs;
    ps.environmentVariables = (*proj)["environmentVariables"].value_or("");
    ps.workingDirectory = (*proj)["workingDirectory"].value_or("");
    ps.customGdbPath = (*proj)["customGdbPath"].value_or("gdb");
    ps.mpiRanks = qMax(1, (*proj)["mpiRanks"].value_or(ds.defaultRanks));
    if (proj->contains("mpiArgs")) ps.mpiArgs = QString::fromStdString((*proj)["mpiArgs"].value_or(""));
  }
  const QString workspaceFile = QDir(m_workspacePath).filePath(".gridlock/workspace.toml");
  toml::table wTbl;
  try { wTbl = toml::parse_file(workspaceFile.toStdString()); } catch (...) {}
  if (auto* proj = wTbl["workspace"].as_table()) {
    if (auto* watches = (*proj)["watchExpressions"].as_array()) {
      for (auto& elem : *watches) {
        if (auto* str = elem.as_string()) ps.watchExpressions.push_back(str->get());
      }
    }
  }
  return ps;
}

/** @brief Persists project target settings to `.gridlock/settings.toml` and watches to `workspace.toml`. */
void ConfigManager::saveProjectSettings(const ProjectSettings& ps) const {
  const QString effArgs = !ps.targetArguments.isEmpty() ? ps.targetArguments : QString::fromStdString(ps.programArguments);
  QSettings s(kOrg, kApp);
  s.setValue("project/targetBinary", QString::fromStdString(ps.targetBinary));
  s.setValue("project/targetArguments", effArgs);
  s.setValue("project/workingDirectory", QString::fromStdString(ps.workingDirectory));
  s.setValue("project/mpiRanks", ps.mpiRanks > 0 ? ps.mpiRanks : getDefaultRanks());
  s.setValue("project/mpiArgs", ps.mpiArgs);
  if (m_workspacePath.isEmpty()) { s.sync(); return; }
  QDir dir(m_workspacePath);
  if (!dir.exists(".gridlock")) dir.mkdir(".gridlock");
  const QString settingsFile = dir.filePath(".gridlock/settings.toml");
  toml::table tbl;
  try { tbl = toml::parse_file(settingsFile.toStdString()); } catch (...) {}
  toml::table projTbl;
  projTbl.insert_or_assign("targetBinary", ps.targetBinary.empty() ? "" : ps.targetBinary);
  projTbl.insert_or_assign("programArguments", effArgs.toStdString());
  projTbl.insert_or_assign("targetArguments", effArgs.toStdString());
  projTbl.insert_or_assign("environmentVariables", ps.environmentVariables.empty() ? "" : ps.environmentVariables);
  projTbl.insert_or_assign("workingDirectory", ps.workingDirectory.empty() ? "" : ps.workingDirectory);
  projTbl.insert_or_assign("customGdbPath", ps.customGdbPath.empty() ? "gdb" : ps.customGdbPath);
  projTbl.insert_or_assign("mpiRanks", ps.mpiRanks > 0 ? ps.mpiRanks : getDefaultRanks());
  projTbl.insert_or_assign("mpiArgs", ps.mpiArgs.toStdString());
  tbl.insert_or_assign("project", projTbl);
  if (std::ofstream out(settingsFile.toStdString()); out.is_open()) out << tbl;

  const QString workspaceFile = dir.filePath(".gridlock/workspace.toml");
  toml::table wTbl;
  try { wTbl = toml::parse_file(workspaceFile.toStdString()); } catch (...) {}
  toml::table wProjTbl;
  if (auto* existing = wTbl["workspace"].as_table()) wProjTbl = *existing;
  toml::array watches;
  for (const auto& w : ps.watchExpressions) {
    if (!w.empty()) watches.push_back(w);
  }
  wProjTbl.insert_or_assign("watchExpressions", watches);
  wTbl.insert_or_assign("workspace", wProjTbl);
  std::ofstream wOut(workspaceFile.toStdString());
  if (wOut.is_open()) wOut << wTbl;
}

/** @brief Returns the persisted theme identifier from `.gridlock/workspace.toml` or INI settings. */
QString ConfigManager::getThemeName() const {
  if (!m_workspacePath.isEmpty()) {
    try {
      auto tbl = toml::parse_file(QDir(m_workspacePath).filePath(QStringLiteral(".gridlock/workspace.toml")).toStdString());
      if (auto* app = tbl["appearance"].as_table()) if (auto val = app->get("theme_id"); val && val->is_string()) return QString::fromStdString(val->as_string()->get());
      if (auto* thm = tbl["theme"].as_table()) if (auto val = thm->get("name"); val && val->is_string()) return QString::fromStdString(val->as_string()->get());
    } catch (...) {}
  }
  QSettings ws(workstationSettingsPath(), QSettings::IniFormat);
  QString t = ws.value(QStringLiteral("appearance/theme_id"), ws.value(QStringLiteral("appearance/theme"))).toString().trimmed();
  if (t.isEmpty()) {
    QSettings ds(debuggerSettingsPath(), QSettings::IniFormat);
    t = ds.value(QStringLiteral("appearance/theme_id"), ds.value(QStringLiteral("appearance/theme"), QStringLiteral("gridlock_slate"))).toString().trimmed();
  }
  return t.isEmpty() ? QStringLiteral("gridlock_slate") : t;
}

/** @brief Persists the theme identifier to `.gridlock/workspace.toml` and unified `~/.config/gridlock/` INI stores. */
void ConfigManager::setThemeName(const QString& name) {
  const QString id = name.trimmed().isEmpty() ? QStringLiteral("gridlock_slate") : name.trimmed();
  if (!m_workspacePath.isEmpty()) {
    QDir dir(m_workspacePath); dir.mkpath(QStringLiteral(".gridlock"));
    const QString wf = dir.filePath(QStringLiteral(".gridlock/workspace.toml"));
    toml::table tbl; try { tbl = toml::parse_file(wf.toStdString()); } catch (...) {}
    toml::table appTbl; if (auto* ex = tbl["appearance"].as_table()) appTbl = *ex;
    appTbl.insert_or_assign("theme_id", id.toStdString()); tbl.insert_or_assign("appearance", appTbl);
    std::ofstream out(wf.toStdString()); if (out.is_open()) out << tbl;
  }
  for (const QString& iniPath : {workstationSettingsPath(), debuggerSettingsPath()}) {
    QSettings s(iniPath, QSettings::IniFormat);
    s.setValue(QStringLiteral("appearance/theme_id"), id); s.setValue(QStringLiteral("appearance/theme"), id); s.sync();
  }
}

/** @brief Returns the configured local debugger backend engine ("gdb", "lldb-dap", "cuda-gdb", "rocgdb"). */
QString ConfigManager::debuggerEngine() const {
  if (!m_debuggerEngine.trimmed().isEmpty()) return m_debuggerEngine.trimmed();
  if (!m_workspacePath.isEmpty()) {
    try {
      auto tbl = toml::parse_file(QDir(m_workspacePath).filePath(QStringLiteral(".gridlock/workspace.toml")).toStdString());
      if (auto* dbg = tbl["debugger"].as_table()) if (auto val = dbg->get("engine"); val && val->is_string()) { const QString eng = QString::fromStdString(val->as_string()->get()).trimmed(); if (!eng.isEmpty()) return eng; }
    } catch (...) {}
  }
  const QString persisted = QSettings(kOrg, kApp).value(QStringLiteral("debugger/engine"), QStringLiteral("gdb")).toString().trimmed();
  return persisted.isEmpty() ? QStringLiteral("gdb") : persisted;
}

/** @brief Sets and persists the local debugger backend engine in `workspace.toml` and QSettings. */
void ConfigManager::setDebuggerEngine(const QString& engine) {
  const QString clean = engine.trimmed().isEmpty() ? QStringLiteral("gdb") : engine.trimmed();
  m_debuggerEngine = clean;
  if (!m_workspacePath.isEmpty()) {
    QDir dir(m_workspacePath); dir.mkpath(QStringLiteral(".gridlock"));
    const QString wf = dir.filePath(QStringLiteral(".gridlock/workspace.toml"));
    toml::table tbl; try { tbl = toml::parse_file(wf.toStdString()); } catch (...) {}
    toml::table dbgTbl; if (auto* ex = tbl["debugger"].as_table()) dbgTbl = *ex;
    dbgTbl.insert_or_assign("engine", clean.toStdString()); tbl.insert_or_assign("debugger", dbgTbl);
    std::ofstream out(wf.toStdString()); if (out.is_open()) out << tbl;
  }
  QSettings s(kOrg, kApp); s.setValue(QStringLiteral("debugger/engine"), clean); s.sync();
  if (auto* app = QCoreApplication::instance()) app->setProperty("gridlock_debugger_engine", clean);
}

/** @brief Returns whether the pseudo-C decompiler may stay visible alongside the C++ Source Editor (default: false). */
bool ConfigManager::showDecompilerWithSource() const {
  if (!m_workspacePath.isEmpty()) {
    try { auto tbl = toml::parse_file(QDir(m_workspacePath).filePath(QStringLiteral(".gridlock/workspace.toml")).toStdString()); if (auto* dbg = tbl["debugger"].as_table()) if (auto v = dbg->get("show_decompiler_with_source"); v && v->is_boolean()) return v->as_boolean()->get(); } catch (...) {}
  }
  return QSettings(kOrg, kApp).value(QStringLiteral("debugger/show_decompiler_with_source"), false).toBool();
}

/** @brief Sets and persists whether the pseudo-C decompiler may stay visible alongside the C++ Source Editor. */
void ConfigManager::setShowDecompilerWithSource(bool show) {
  if (!m_workspacePath.isEmpty()) {
    QDir dir(m_workspacePath); dir.mkpath(QStringLiteral(".gridlock")); const QString wf = dir.filePath(QStringLiteral(".gridlock/workspace.toml"));
    toml::table tbl; try { tbl = toml::parse_file(wf.toStdString()); } catch (...) {}
    toml::table dbgTbl; if (auto* ex = tbl["debugger"].as_table()) dbgTbl = *ex;
    dbgTbl.insert_or_assign("show_decompiler_with_source", show); tbl.insert_or_assign("debugger", dbgTbl);
    std::ofstream out(wf.toStdString()); if (out.is_open()) out << tbl;
  }
  QSettings s(kOrg, kApp); s.setValue(QStringLiteral("debugger/show_decompiler_with_source"), show); s.sync();
}

/** @brief Returns the default DisassemblyView presentation mode ("cfg", "linear", or "split"; default: "cfg"). */
QString ConfigManager::defaultDisassemblyMode() const {
  if (!m_workspacePath.isEmpty()) {
    try { auto tbl = toml::parse_file(QDir(m_workspacePath).filePath(QStringLiteral(".gridlock/workspace.toml")).toStdString()); if (auto* dbg = tbl["debugger"].as_table()) if (auto v = dbg->get("default_disassembly_mode"); v && v->is_string()) return QString::fromStdString(v->as_string()->get()); } catch (...) {}
  }
  return QSettings(kOrg, kApp).value(QStringLiteral("debugger/default_disassembly_mode"), QStringLiteral("cfg")).toString();
}

/** @brief Sets and persists the default DisassemblyView presentation mode ("cfg", "linear", or "split"). */
void ConfigManager::setDefaultDisassemblyMode(const QString& mode) {
  const QString clean = mode.trimmed().isEmpty() ? QStringLiteral("cfg") : mode.trimmed().toLower();
  if (!m_workspacePath.isEmpty()) {
    QDir dir(m_workspacePath); dir.mkpath(QStringLiteral(".gridlock")); const QString wf = dir.filePath(QStringLiteral(".gridlock/workspace.toml"));
    toml::table tbl; try { tbl = toml::parse_file(wf.toStdString()); } catch (...) {}
    toml::table dbgTbl; if (auto* ex = tbl["debugger"].as_table()) dbgTbl = *ex;
    dbgTbl.insert_or_assign("default_disassembly_mode", clean.toStdString()); tbl.insert_or_assign("debugger", dbgTbl);
    std::ofstream out(wf.toStdString()); if (out.is_open()) out << tbl;
  }
  QSettings s(kOrg, kApp); s.setValue(QStringLiteral("debugger/default_disassembly_mode"), clean); s.sync();
}

/** @brief Returns the status bar resource telemetry display format ("gb", "percent", or "both"; default: "gb"). */
QString ConfigManager::telemetryDisplayFormat() const {
  if (!m_workspacePath.isEmpty()) {
    try { auto tbl = toml::parse_file(QDir(m_workspacePath).filePath(QStringLiteral(".gridlock/workspace.toml")).toStdString()); if (auto* uiTbl = tbl["ui"].as_table()) if (auto v = uiTbl->get("telemetry_display_format"); v && v->is_string()) return QString::fromStdString(v->as_string()->get()); } catch (...) {}
  }
  return QSettings(kOrg, kApp).value(QStringLiteral("ui/telemetry_display_format"), QStringLiteral("gb")).toString();
}

/** @brief Sets and persists the status bar resource telemetry display format ("gb", "percent", or "both"). */
void ConfigManager::setTelemetryDisplayFormat(const QString& fmt) {
  const QString clean = fmt.trimmed().isEmpty() ? QStringLiteral("gb") : fmt.trimmed().toLower();
  if (!m_workspacePath.isEmpty()) {
    QDir dir(m_workspacePath); dir.mkpath(QStringLiteral(".gridlock")); const QString wf = dir.filePath(QStringLiteral(".gridlock/workspace.toml"));
    toml::table tbl; try { tbl = toml::parse_file(wf.toStdString()); } catch (...) {}
    toml::table uiTbl; if (auto* ex = tbl["ui"].as_table()) uiTbl = *ex;
    uiTbl.insert_or_assign("telemetry_display_format", clean.toStdString()); tbl.insert_or_assign("ui", uiTbl);
    std::ofstream out(wf.toStdString()); if (out.is_open()) out << tbl;
  }
  QSettings s(kOrg, kApp); s.setValue(QStringLiteral("ui/telemetry_display_format"), clean); s.sync();
}

/** @brief Returns deduplicated existing recent session/workspace paths (most recent first, capped at 10). */
QStringList ConfigManager::recentSessions() const {
  QStringList out;
  auto push = [&](const QString& raw) {
    const QString norm = normalizeRecentPath(raw);
    if (!norm.isEmpty() && QFileInfo::exists(norm) && !out.contains(norm) && out.size() < 10) out.append(norm);
  };
  if (auto* sess = m_config["session"].as_table()) {
    if (auto* arr = sess->get_as<toml::array>("recent_sessions")) {
      for (const auto& elem : *arr) if (elem.is_string()) push(QString::fromStdString(elem.as_string()->get()));
    }
  }
  QSettings ws(workstationSettingsPath(), QSettings::IniFormat);
  for (const QString& p : ws.value(QStringLiteral("session/recent_sessions")).toStringList()) push(p);
  QSettings ds(debuggerSettingsPath(), QSettings::IniFormat);
  for (const QString& p : ds.value(QStringLiteral("recentSessions")).toStringList()) push(p);
  return out;
}

/** @brief Records @p path at index 0 of `recent_sessions` in `config.toml` and INI stores immediately. */
void ConfigManager::recordRecentSession(const QString& path) {
  const QString norm = normalizeRecentPath(path);
  if (norm.isEmpty()) return;
  QStringList entries = recentSessions();
  entries.removeAll(norm);
  entries.prepend(norm);
  while (entries.size() > 10) entries.removeLast();
  toml::table sessTbl; if (auto* ex = m_config["session"].as_table()) sessTbl = *ex;
  toml::array arr; for (const QString& e : entries) arr.push_back(e.toStdString());
  sessTbl.insert_or_assign("recent_sessions", arr); m_config.insert_or_assign("session", sessTbl);
  saveGlobalConfig();
  QSettings ws(workstationSettingsPath(), QSettings::IniFormat); ws.setValue(QStringLiteral("session/recent_sessions"), entries); ws.sync();
  QSettings ds(debuggerSettingsPath(), QSettings::IniFormat); ds.setValue(QStringLiteral("recentSessions"), entries); ds.sync();
}

/** @brief Clears all persisted recent session entries from `config.toml` and INI stores. */
void ConfigManager::clearRecentSessions() {
  toml::table sessTbl; if (auto* ex = m_config["session"].as_table()) sessTbl = *ex;
  sessTbl.insert_or_assign("recent_sessions", toml::array{}); m_config.insert_or_assign("session", sessTbl);
  saveGlobalConfig();
  QSettings ws(workstationSettingsPath(), QSettings::IniFormat); ws.remove(QStringLiteral("session/recent_sessions")); ws.sync();
  QSettings ds(debuggerSettingsPath(), QSettings::IniFormat); ds.remove(QStringLiteral("recentSessions")); ds.sync();
}

}  // namespace gridlock::core
