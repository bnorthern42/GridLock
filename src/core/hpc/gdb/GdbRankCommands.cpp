/**
 * @file GdbRankCommands.cpp
 * @brief Execution control, breakpoint, watchpoint, catchpoint, signal, reverse-debugging, and variable/register write MI command builders for GdbRankCoordinator.
 */

#include "GdbRankCoordinator.hpp"

#include <signal.h>

#include <algorithm>

#include <QFileInfo>

namespace gridlock {

namespace detail {

bool validateSafeDebuggerInput(const QString& rawInput);

/** @brief Escapes backslashes, quotes, and control characters for GDB/MI string literals. */
QString escapeGdbMiString(const QString& str) {
  QString res;
  for (int i = 0; i < str.length(); ++i) {
    const QChar c = str[i];
    if (c == '\\') res += "\\\\";
    else if (c == '"') res += "\\\"";
    else if (c == '\n') res += "\\n";
    else if (c == '\r') res += "\\r";
    else if (c == '\t') res += "\\t";
    else res += c;
  }
  return "\"" + res + "\"";
}

/** @brief Builds a GDB/MI `-break-insert` command string for quoted/escaped `file:line`, optional condition, ignoreCount, `-t`, and `-d`. */
QString buildBreakInsertCmd(const QString& file, int line, const QString& condition,
                            int ignoreCount = 0, bool temporary = false, bool disabled = false) {
  const QString loc = escapeGdbMiString(QStringLiteral("%1:%2").arg(file).arg(line));
  const QString flags = QStringLiteral("%1%2%3")
                            .arg(temporary ? QStringLiteral("-t ") : QString(),
                                 disabled ? QStringLiteral("-d ") : QString(),
                                 ignoreCount > 0 ? QStringLiteral("-i %1 ").arg(ignoreCount) : QString());
  if (!condition.trimmed().isEmpty()) {
    return QStringLiteral("-break-insert %1-c %2 %3\n")
        .arg(flags, escapeGdbMiString(condition.trimmed()), loc);
  }
  return QStringLiteral("-break-insert %1-f %2\n").arg(flags, loc);
}

}  // namespace detail

void GdbRankCoordinator::setBreakpoint(const QString& file, int line, const QString& condition, int rankFilter) {
  if (file.isEmpty() || line <= 0) return;
  m_breakpointsCleared = false;
  const auto extra = m_breakpointExtras.value(QStringLiteral("%1:%2").arg(file).arg(line));
  m_pendingBreakpoints.erase(
      std::remove_if(m_pendingBreakpoints.begin(), m_pendingBreakpoints.end(),
                     [&](const PendingBreakpoint& bp) { return bp.file == file && bp.line == line; }),
      m_pendingBreakpoints.end());
  m_pendingBreakpoints.push_back({file, line, condition.trimmed(), rankFilter, extra.ignoreCount, extra.logMessage,
                                  extra.enabled, extra.temporary, extra.commands});
  const QString locFull = QString("%1:%2").arg(file).arg(line);
  const QString locBase = QString("%1:%2").arg(QFileInfo(file).fileName()).arg(line);
  const QString cmd = detail::buildBreakInsertCmd(file, line, condition, extra.ignoreCount, extra.temporary, !extra.enabled);
  for (size_t i = 0; i < m_processes.size(); ++i) {
    auto& rp = m_processes[i];
    if (rankFilter >= 0 && rankFilter != static_cast<int>(i)) {
      if (rp) {
        QList<int> rem;
        for (auto it = rp->state.breakpoints.constBegin(); it != rp->state.breakpoints.constEnd(); ++it) {
          if (it.value() == locFull || it.value() == locBase) rem.append(it.key());
        }
        for (int id : rem) {
          queueOrWriteCmd(static_cast<int>(i), QString("-break-delete %1\n").arg(id));
          rp->state.breakpoints.remove(id);
        }
      }
      continue;
    }
    queueOrWriteCmd(static_cast<int>(i), cmd);
  }
}

void GdbRankCoordinator::configureBreakpointOptions(const QString& file, int line, int ignoreCount, const QString& logMessage) {
  IBackendCoordinator::configureBreakpointOptions(file, line, ignoreCount, logMessage);
  const int cleanIgn = qMax(0, ignoreCount);
  const QString cleanLog = logMessage.trimmed();
  for (auto& bp : m_pendingBreakpoints) {
    if (bp.file == file && bp.line == line) { bp.ignoreCount = cleanIgn; bp.logMessage = cleanLog; }
  }
  const QString locFull = QString("%1:%2").arg(file).arg(line);
  const QString locBase = QString("%1:%2").arg(QFileInfo(file).fileName()).arg(line);
  for (size_t i = 0; i < m_processes.size(); ++i) {
    if (!m_processes[i]) continue;
    for (auto it = m_processes[i]->state.breakpoints.constBegin(); it != m_processes[i]->state.breakpoints.constEnd(); ++it) {
      if (it.value() == locFull || it.value() == locBase) {
        queueOrWriteCmd(static_cast<int>(i), QString("-break-after %1 %2\n").arg(it.key()).arg(cleanIgn));
      }
    }
  }
}

void GdbRankCoordinator::setBreakpointEnabled(const QString& filePath, int line, bool enabled) {
  if (filePath.trimmed().isEmpty() || line <= 0) return;
  IBackendCoordinator::setBreakpointEnabled(filePath, line, enabled);
  const QString locFull = QStringLiteral("%1:%2").arg(filePath).arg(line), locBase = QStringLiteral("%1:%2").arg(QFileInfo(filePath).fileName()).arg(line);
  for (auto& bp : m_pendingBreakpoints)
    if (bp.line == line && (bp.file == filePath || QFileInfo(bp.file).fileName() == QFileInfo(filePath).fileName())) bp.enabled = enabled;
  const QString verb = enabled ? QStringLiteral("-break-enable") : QStringLiteral("-break-disable");
  for (size_t i = 0; i < m_processes.size(); ++i) {
    if (!m_processes[i]) continue;
    for (auto it = m_processes[i]->state.breakpoints.constBegin(); it != m_processes[i]->state.breakpoints.constEnd(); ++it) {
      if (it.value() == locFull || it.value() == locBase || QFileInfo(it.value().section(QLatin1Char(':'), 0, -2)).fileName() + QLatin1Char(':') + it.value().section(QLatin1Char(':'), -1) == locBase)
        queueOrWriteCmd(static_cast<int>(i), QStringLiteral("%1 %2\n").arg(verb).arg(it.key()));
    }
  }
}

void GdbRankCoordinator::insertTemporaryBreakpoint(const QString& filePath, int line, const QString& condition, int targetRank) {
  if (filePath.trimmed().isEmpty() || line <= 0) return;
  m_breakpointsCleared = false;
  IBackendCoordinator::insertTemporaryBreakpoint(filePath, line, condition, targetRank);
  const QString cleanCond = condition.trimmed();
  m_pendingBreakpoints.erase(std::remove_if(m_pendingBreakpoints.begin(), m_pendingBreakpoints.end(), [&](const PendingBreakpoint& bp) { return bp.file == filePath && bp.line == line; }), m_pendingBreakpoints.end());
  m_pendingBreakpoints.push_back({filePath, line, cleanCond, targetRank, 0, QString(), true, true, {}});
  QString escCond = cleanCond;
  escCond.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
  const QString loc = QStringLiteral("%1:%2").arg(filePath).arg(line);
  const QString cmd = cleanCond.isEmpty() ? QStringLiteral("-break-insert -t -f %1\n").arg(loc) : QStringLiteral("-break-insert -t -c \"%1\" %2\n").arg(escCond, loc);
  for (size_t i = 0; i < m_processes.size(); ++i)
    if (m_processes[i] && (targetRank < 0 || targetRank == static_cast<int>(i))) queueOrWriteCmd(static_cast<int>(i), cmd);
}

void GdbRankCoordinator::insertRegexBreakpoint(const QString& regexPattern) {
  const QString pat = regexPattern.trimmed();
  if (pat.isEmpty() || pat.contains(QLatin1Char('\n')) || pat.contains(QLatin1Char('\r')) || pat.contains(QChar('\0'))) return;
  if (!m_pendingRegexBreakpoints.contains(pat)) m_pendingRegexBreakpoints.append(pat);
  QString esc = pat;
  esc.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
  const QString cmd = QStringLiteral("-interpreter-exec console \"rbreak %1\"\n").arg(esc);
  for (size_t i = 0; i < m_processes.size(); ++i) if (m_processes[i]) queueOrWriteCmd(static_cast<int>(i), cmd);
}

void GdbRankCoordinator::setBreakpointCommands(const QString& filePath, int line, const QStringList& commands) {
  if (filePath.trimmed().isEmpty() || line <= 0) return;
  QStringList cleaned;
  for (const QString& ln : commands) if (const QString t = ln.trimmed(); !t.isEmpty() && !t.contains(QLatin1Char('\n')) && !t.contains(QLatin1Char('\r'))) cleaned.append(t);
  IBackendCoordinator::setBreakpointCommands(filePath, line, cleaned);
  const QString locFull = QStringLiteral("%1:%2").arg(filePath).arg(line), locBase = QStringLiteral("%1:%2").arg(QFileInfo(filePath).fileName()).arg(line);
  for (auto& bp : m_pendingBreakpoints)
    if (bp.line == line && (bp.file == filePath || QFileInfo(bp.file).fileName() == QFileInfo(filePath).fileName())) bp.commands = cleaned;
  for (size_t i = 0; i < m_processes.size(); ++i) {
    if (!m_processes[i]) continue;
    for (auto it = m_processes[i]->state.breakpoints.constBegin(); it != m_processes[i]->state.breakpoints.constEnd(); ++it) {
      if (it.value() == locFull || it.value() == locBase || QFileInfo(it.value().section(QLatin1Char(':'), 0, -2)).fileName() + QLatin1Char(':') + it.value().section(QLatin1Char(':'), -1) == locBase)
        queueOrWriteCmd(static_cast<int>(i), formatBreakCommandsMi(it.key(), cleaned));
    }
  }
}

void GdbRankCoordinator::clearBreakpoints() {
  m_pendingBreakpoints.clear();
  m_pendingRegexBreakpoints.clear();
  m_regexCreatedBreakpoints.clear();
  m_breakpointsCleared = true;
  for (size_t i = 0; i < m_processes.size(); ++i) {
    if (m_processes[i]) {
      m_processes[i]->state.breakpoints.clear();
      queueOrWriteCmd(static_cast<int>(i), "-break-delete\n");
    }
  }
}

void GdbRankCoordinator::registerWatchVariable(const QString& name) {
  if (std::find(m_watchVariables.begin(), m_watchVariables.end(), name) == m_watchVariables.end()) {
    m_watchVariables.push_back(name);
  }
  const int varIdx = static_cast<int>(std::distance(
      m_watchVariables.begin(), std::find(m_watchVariables.begin(), m_watchVariables.end(), name)));
  for (size_t i = 0; i < m_processes.size(); ++i) {
    auto& rp = m_processes[i];
    const bool stoppedActive = rp && rp->state.currentState == QLatin1String("stopped") &&
                               ((rp->fifoInFd >= 0) || !rp->process || rp->process->state() == QProcess::Running);
    if (stoppedActive && !m_varNameMap[static_cast<int>(i)].contains(name)) {
      writeCmd(static_cast<int>(i), QString("30%1-var-create - * %2\n").arg(varIdx).arg(name));
    }
  }
}

void GdbRankCoordinator::insertBreakpoint(const QString& location) {
  QString fileName = QFileInfo(location).fileName();
  if (fileName.isEmpty()) fileName = location;
  broadcastCommand(QString("-break-insert -f %1\n").arg(fileName));
}

void GdbRankCoordinator::broadcastCommand(const QString& cmd) {
  for (size_t i = 0; i < m_processes.size(); ++i) writeCmd(static_cast<int>(i), cmd);
}

void GdbRankCoordinator::sendCommand(int rankId, const QString& cmd) {
  if (rankId == -1) broadcastCommand(cmd + "\n"); else writeCmd(rankId, cmd + "\n");
}

void GdbRankCoordinator::broadcastBreakpoint(const QString& file, int line, bool isSet, const QString& condition) {
  int resolvedRank = -1;
  const QString locStr = QString("%1:%2").arg(file).arg(line);
  if (property("gl_pending_bp_rank_key").toString() == locStr) {
    resolvedRank = property("gl_pending_bp_rank_val").toInt();
    setProperty("gl_pending_bp_rank_key", QString());
  }
  if (isSet) { setBreakpoint(file, line, condition, resolvedRank); return; }
  m_pendingBreakpoints.erase(
      std::remove_if(m_pendingBreakpoints.begin(), m_pendingBreakpoints.end(),
                     [&](const PendingBreakpoint& bp) { return bp.file == file && bp.line == line; }),
      m_pendingBreakpoints.end());
  const QString locBase = QString("%1:%2").arg(QFileInfo(file).fileName()).arg(line);
  for (size_t i = 0; i < m_processes.size(); ++i) {
    auto& rp = m_processes[i];
    if (!rp) continue;
    int bkptIdToDelete = -1;
    for (auto it = rp->state.breakpoints.constBegin(); it != rp->state.breakpoints.constEnd(); ++it) {
      if (it.value() == locStr || it.value() == locBase) { bkptIdToDelete = it.key(); break; }
    }
    if (bkptIdToDelete != -1) {
      queueOrWriteCmd(static_cast<int>(i), QString("-break-delete %1\n").arg(bkptIdToDelete));
      rp->state.breakpoints.remove(bkptIdToDelete);
    }
  }
}

void GdbRankCoordinator::stepAll() {
  for (size_t i = 0; i < m_processes.size(); ++i) {
    if (const auto& st = m_processes[i]->state.currentState; st != "running" && st != "exited") {
      writeCmd(static_cast<int>(i), "-exec-next\n");
    }
  }
}

void GdbRankCoordinator::stepInto(int threadId) {
  if (m_processes.empty()) { stepAll(); return; }
  dispatchStoppedExecCmd((threadId > 0) ? (threadId - 1) : -1, QStringLiteral("-exec-step\n"));
}

void GdbRankCoordinator::stepInstruction(int threadId) {
  if (m_processes.empty()) return;
  const int rankId = std::clamp(threadId - 1, 0, static_cast<int>(m_processes.size()) - 1);
  if (const auto& st = m_processes[rankId]->state.currentState; st != "running" && st != "exited") {
    writeCmd(rankId, "-exec-step-instruction\n");
  }
}

void GdbRankCoordinator::dispatchStoppedExecCmd(int rank, const QString& cmd) {
  if (m_processes.empty()) return;
  const int lo = (rank < 0) ? 0 : qMax(0, rank);
  const int hi = (rank < 0) ? static_cast<int>(m_processes.size()) - 1 : qMin(rank, static_cast<int>(m_processes.size()) - 1);
  for (int r = lo; r <= hi; ++r) {
    if (m_processes[r] && m_processes[r]->state.currentState != "running" && m_processes[r]->state.currentState != "exited") {
      writeCmd(r, cmd);
    }
  }
}

void GdbRankCoordinator::stepOut(int threadId) { dispatchStoppedExecCmd((threadId > 0) ? (threadId - 1) : -1, QStringLiteral("-exec-finish\n")); }
void GdbRankCoordinator::nextInstruction(int rank) { dispatchStoppedExecCmd(rank, QStringLiteral("-exec-next-instruction\n")); }
void GdbRankCoordinator::runToCursor(const QString& filePath, int line, int rank) {
  if (!filePath.trimmed().isEmpty() && line > 0) dispatchStoppedExecCmd(rank, QStringLiteral("-exec-until %1\n").arg(detail::escapeGdbMiString(QStringLiteral("%1:%2").arg(filePath.trimmed()).arg(line))));
}
void GdbRankCoordinator::runToAddress(uint64_t address, int rank) { if (address != 0ULL) dispatchStoppedExecCmd(rank, QStringLiteral("-exec-until *0x%1\n").arg(address, 0, 16)); }
void GdbRankCoordinator::jumpToLocation(const QString& filePath, int line, int rank) {
  if (!filePath.trimmed().isEmpty() && line > 0) dispatchStoppedExecCmd(rank, QStringLiteral("-exec-jump %1\n").arg(detail::escapeGdbMiString(QStringLiteral("%1:%2").arg(filePath.trimmed()).arg(line))));
}
void GdbRankCoordinator::jumpToAddress(uint64_t address, int rank) { if (address != 0ULL) dispatchStoppedExecCmd(rank, QStringLiteral("-exec-jump *0x%1\n").arg(address, 0, 16)); }
void GdbRankCoordinator::forceEarlyReturn(const QString& returnExpr, int rank) {
  const QString expr = returnExpr.trimmed();
  if (!expr.contains(QLatin1Char('\n')) && !expr.contains(QLatin1Char('\r')) && !expr.contains(QChar('\0'))) {
    dispatchStoppedExecCmd(rank, expr.isEmpty() ? QStringLiteral("-exec-return\n") : QStringLiteral("-exec-return %1\n").arg(expr));
  }
}

void GdbRankCoordinator::continueAll() {
  for (size_t i = 0; i < m_processes.size(); ++i) {
    if (auto& st = m_processes[i]->state; st.currentState != "running" && st.currentState != "exited") {
      st.currentState = "running";
      st.executionTimer.start();
      writeCmd(static_cast<int>(i), "-exec-continue\n");
    }
  }
}

void GdbRankCoordinator::continueExecution(int threadId) {
  const QVariant scoped = property("gl_scoped_continue_rank");
  const int targetRank = (threadId > 0) ? (threadId - 1) : (scoped.isValid() ? scoped.toInt() : -1);
  if (targetRank < 0 || m_processes.empty()) { continueAll(); return; }
  if (targetRank < static_cast<int>(m_processes.size()) && m_processes[targetRank]) {
    if (auto& st = m_processes[targetRank]->state; st.currentState != "running" && st.currentState != "exited") {
      st.currentState = "running";
      st.executionTimer.start();
      writeCmd(targetRank, "-exec-continue\n");
    }
  }
}

void GdbRankCoordinator::runAll() { broadcastCommand("-exec-run\n"); }
void GdbRankCoordinator::haltAll() { for (size_t i = 0; i < m_processes.size(); ++i) pauseFocusedRank(static_cast<int>(i)); }

void GdbRankCoordinator::pauseFocusedRank(int rankId) {
  if (rankId >= 0 && rankId < static_cast<int>(m_processes.size()) && m_processes[rankId] &&
      m_processes[rankId]->inferiorPid > 0 && m_processes[rankId]->state.currentState == "running") {
    ::kill(m_processes[rankId]->inferiorPid, SIGINT);
  }
  writeCmd(rankId, "-exec-interrupt\n");
}

void GdbRankCoordinator::requestDisassemblyFallback(int rankId) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId];
  if (!rp || rp->state.currentState != "stopped") return;
  writeCmd(rankId, "-thread-info\n");
  if (!rp->state.currentFile.isEmpty() && rp->state.currentLine > 0) {
    writeCmd(rankId, QString("200-data-disassemble -f %1 -l %2 -n 30 -- 0\n").arg(rp->state.currentFile).arg(rp->state.currentLine));
  } else {
    writeCmd(rankId, "200-data-disassemble -s $pc -e \"$pc + 64\" -- 0\n");
  }
}

void GdbRankCoordinator::sendDebuggerCommand(int rank, const QString& command) {
  const QString trimmed = command.trimmed();
  if (trimmed.isEmpty()) return;
  if (!detail::validateSafeDebuggerInput(command)) {
    emit debuggerConsoleOutput(rank < 0 ? 0 : rank, QStringLiteral("Error: Command blocked by MiSanitizer security policy."), true);
    return;
  }
  auto sendToSingleRank = [&](int targetRank) {
    if (targetRank < 0 || targetRank >= static_cast<int>(m_processes.size()) || !m_processes[targetRank]) return;
    const bool isInterrupt = (trimmed == QLatin1String("-exec-interrupt") || trimmed == QLatin1String("interrupt"));
    if (!isInterrupt && m_processes[targetRank]->state.currentState != QLatin1String("stopped")) {
      emit debuggerConsoleOutput(targetRank, QStringLiteral("Rank %1 is running; pause execution before inspecting state.").arg(targetRank), true);
      return;
    }
    if (trimmed.startsWith(QLatin1Char('-'))) {
      writeCmd(targetRank, QStringLiteral("205%1\n").arg(trimmed));
    } else {
      QString escaped = trimmed;
      escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\")).replace(QStringLiteral("\""), QStringLiteral("\\\""));
      m_pendingConsoleCmdCount[targetRank] = m_pendingConsoleCmdCount.value(targetRank, 0) + 1;
      writeCmd(targetRank, QStringLiteral("204-interpreter-exec console \"%1\"\n").arg(escaped));
    }
  };
  if (rank < 0) { for (int r = 0; r < static_cast<int>(m_processes.size()); ++r) sendToSingleRank(r); }
  else sendToSingleRank(rank);
}

void GdbRankCoordinator::setDataWatchpoint(const QString& expression, WatchpointAccessMode mode, int rankFilter) {
  const QString expr = expression.trimmed();
  if (expr.isEmpty() || !detail::validateSafeDebuggerInput(expr)) return;
  m_pendingWatchpoints.erase(
      std::remove_if(m_pendingWatchpoints.begin(), m_pendingWatchpoints.end(),
                     [&](const PendingWatchpoint& wp) { return wp.expression == expr && wp.rankFilter == rankFilter; }),
      m_pendingWatchpoints.end());
  m_pendingWatchpoints.push_back({expr, mode, rankFilter});
  const QString modeFlag = (mode == WatchpointAccessMode::Read) ? QStringLiteral("-r ")
                           : (mode == WatchpointAccessMode::ReadWrite ? QStringLiteral("-a ") : QString());
  const QString miCmd = QStringLiteral("206-break-watch %1%2\n").arg(modeFlag, expr);
  for (int r = 0; r < static_cast<int>(m_processes.size()); ++r) {
    if ((rankFilter == -1 || rankFilter == r) && m_processes[r]) queueOrWriteCmd(r, miCmd);
  }
}

void GdbRankCoordinator::removeDataWatchpoint(const QString& expression) {
  const QString expr = expression.trimmed();
  if (expr.isEmpty()) return;
  m_pendingWatchpoints.erase(
      std::remove_if(m_pendingWatchpoints.begin(), m_pendingWatchpoints.end(),
                     [&](const PendingWatchpoint& wp) { return wp.expression == expr; }),
      m_pendingWatchpoints.end());
  for (int r = 0; r < static_cast<int>(m_processes.size()); ++r) {
    if (m_watchpointIds.contains(r) && m_watchpointIds[r].contains(expr)) {
      queueOrWriteCmd(r, QStringLiteral("-break-delete %1\n").arg(m_watchpointIds[r].take(expr)));
    }
  }
}

void GdbRankCoordinator::setReverseRecordingEnabled(int rank, bool enabled) {
  const int start = (rank < 0) ? 0 : rank;
  const int end = (rank < 0) ? static_cast<int>(m_processes.size()) - 1 : rank;
  for (int r = qMax(0, start); r <= end; ++r) {
    IBackendCoordinator::setReverseRecordingEnabled(r, enabled);
    if (r < static_cast<int>(m_processes.size()) && m_processes[r]) {
      if (enabled) {
        queueOrWriteCmd(r, QStringLiteral("-gdb-set confirm off\n"));
        queueOrWriteCmd(r, QStringLiteral("-gdb-set scheduler-locking on\n"));
        queueOrWriteCmd(r, QStringLiteral("-gdb-set record full stop-at-limit off\n"));
        queueOrWriteCmd(r, QStringLiteral("-gdb-set record full insn-number-max 200000\n"));
        queueOrWriteCmd(r, QStringLiteral("-interpreter-exec console \"record full\"\n"));
      } else {
        queueOrWriteCmd(r, QStringLiteral("-interpreter-exec console \"record stop\"\n"));
        queueOrWriteCmd(r, QStringLiteral("-gdb-set scheduler-locking off\n"));
      }
    }
  }
}

bool GdbRankCoordinator::isReverseRecordingEnabled(int rank) const { return IBackendCoordinator::isReverseRecordingEnabled(rank); }

void GdbRankCoordinator::reverseContinue(int rank) {
  const int r = qMax(0, rank); if (r >= static_cast<int>(m_processes.size()) || !m_processes[r]) return;
  if (!isReverseRecordingEnabled(r)) setReverseRecordingEnabled(r, true);
  writeCmd(r, QStringLiteral("-exec-continue --reverse\n"));
}
void GdbRankCoordinator::reverseStepOver(int rank) {
  const int r = qMax(0, rank); if (r >= static_cast<int>(m_processes.size()) || !m_processes[r]) return;
  if (!isReverseRecordingEnabled(r)) setReverseRecordingEnabled(r, true);
  writeCmd(r, QStringLiteral("-exec-next --reverse\n"));
}
void GdbRankCoordinator::reverseStepInto(int rank) {
  const int r = qMax(0, rank); if (r >= static_cast<int>(m_processes.size()) || !m_processes[r]) return;
  if (!isReverseRecordingEnabled(r)) setReverseRecordingEnabled(r, true);
  writeCmd(r, QStringLiteral("-exec-step --reverse\n"));
}
void GdbRankCoordinator::reverseStepInstruction(int rank) {
  const int r = qMax(0, rank); if (r >= static_cast<int>(m_processes.size()) || !m_processes[r]) return;
  if (!isReverseRecordingEnabled(r)) setReverseRecordingEnabled(r, true);
  writeCmd(r, QStringLiteral("-exec-step-instruction --reverse\n"));
}

void GdbRankCoordinator::setSignalDisposition(const QString& signalName, bool stop, bool print, bool pass) {
  const QString sig = signalName.trimmed().toUpper(); if (sig.isEmpty()) return;
  SignalDisposition disp = m_signalDispositions.value(sig, {sig, stop, print, pass, QString()});
  disp.signal = sig; disp.stop = stop; disp.print = print; disp.pass = pass; m_signalDispositions.insert(sig, disp);
  const QString cmd = QStringLiteral("-interpreter-exec console \"handle %1 %2 %3 %4\"\n").arg(sig, stop ? QStringLiteral("stop") : QStringLiteral("nostop"), print ? QStringLiteral("print") : QStringLiteral("noprint"), pass ? QStringLiteral("pass") : QStringLiteral("nopass"));
  for (int r = 0; r < static_cast<int>(m_processes.size()); ++r) if (m_processes[r]) queueOrWriteCmd(r, cmd);
}

void GdbRankCoordinator::requestSignalDispositions() {
  for (int r = 0; r < static_cast<int>(m_processes.size()); ++r) {
    if (m_processes[r]) { m_pendingInfoHandle[r] = true; m_infoHandleBuffer[r].clear(); queueOrWriteCmd(r, QStringLiteral("210-interpreter-exec console \"info handle\"\n")); return; }
  }
  if (!m_signalDispositions.isEmpty()) {
    QVector<SignalDisposition> rows; for (auto it = m_signalDispositions.constBegin(); it != m_signalDispositions.constEnd(); ++it) rows.append(it.value());
    emit signalDispositionsReady(0, rows);
  }
}

void GdbRankCoordinator::insertCatchpoint(const CatchpointSpec& spec) {
  CatchpointSpec entry = spec; entry.filter = entry.filter.trimmed();
  if (entry.gdbId <= 0) entry.gdbId = m_nextCatchpointId++; else if (entry.gdbId >= m_nextCatchpointId) m_nextCatchpointId = entry.gdbId + 1;
  entry.enabled = true; m_activeCatchpoints.append(entry); QString cmd;
  if (entry.kind == CatchpointSpec::Kind::Syscall) {
    QString f = entry.filter; f.replace(QLatin1Char(','), QLatin1Char(' ')); f = f.simplified();
    cmd = f.isEmpty() ? QStringLiteral("-interpreter-exec console \"catch syscall\"\n") : QStringLiteral("-interpreter-exec console \"catch syscall %1\"\n").arg(f);
  } else {
    const QString base = (entry.kind == CatchpointSpec::Kind::CppThrow) ? QStringLiteral("-catch-throw") : (entry.kind == CatchpointSpec::Kind::CppRethrow) ? QStringLiteral("-catch-rethrow") : (entry.kind == CatchpointSpec::Kind::CppCatch) ? QStringLiteral("-catch-catch") : (entry.kind == CatchpointSpec::Kind::LoadLib) ? QStringLiteral("-catch-load") : QStringLiteral("-catch-unload");
    cmd = entry.filter.isEmpty() ? (base + QLatin1Char('\n')) : QStringLiteral("%1 -r %2\n").arg(base, entry.filter);
  }
  for (int r = 0; r < static_cast<int>(m_processes.size()); ++r) if (m_processes[r]) queueOrWriteCmd(r, cmd);
  emit catchpointsUpdated(m_activeCatchpoints);
}

void GdbRankCoordinator::removeCatchpoint(int gdbId) {
  if (gdbId <= 0) return;
  for (int i = m_activeCatchpoints.size() - 1; i >= 0; --i) if (m_activeCatchpoints[i].gdbId == gdbId) m_activeCatchpoints.removeAt(i);
  const QString cmd = QStringLiteral("-break-delete %1\n").arg(gdbId);
  for (int r = 0; r < static_cast<int>(m_processes.size()); ++r) if (m_processes[r]) queueOrWriteCmd(r, cmd);
  emit catchpointsUpdated(m_activeCatchpoints);
}

void GdbRankCoordinator::requestConsoleCompletions(const QString& prefix) {
  if (prefix.contains(QChar('\0')) || prefix.contains(QLatin1Char('\n')) || prefix.contains(QLatin1Char('\r'))) return;
  QString escaped = prefix; escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\")).replace(QStringLiteral("\""), QStringLiteral("\\\""));
  for (int r = 0; r < static_cast<int>(m_processes.size()); ++r) {
    if (m_processes[r]) { m_pendingCompletePrefix[r] = prefix; m_completeBuffer[r].clear(); queueOrWriteCmd(r, QStringLiteral("211-interpreter-exec console \"complete %1\"\n").arg(escaped)); return; }
  }
}

void GdbRankCoordinator::writeVariableValue(int rank, const QString& expression, const QString& newValue) {
  const QString expr = expression.trimmed(), val = newValue.trimmed();
  if (expr.isEmpty() || val.isEmpty() || !detail::validateSafeDebuggerInput(expr) || !detail::validateSafeDebuggerInput(val)) return;
  const int r = qMax(0, rank);
  if (r >= static_cast<int>(m_processes.size()) || !m_processes[r]) return;
  queueOrWriteCmd(r, QStringLiteral("-gdb-set var %1=%2\n").arg(expr, val));
  if (m_processes[r]->state.currentState == QLatin1String("stopped")) {
    writeCmd(r, QStringLiteral("201-stack-list-variables --simple-values\n"));
    for (const QString& w : m_watchVariables) evaluateExpression(r, w);
  }
}

void GdbRankCoordinator::writeRegisterValue(int rank, const QString& regName, const QString& newValue) {
  QString reg = regName.trimmed().toLower(); if (reg.startsWith(QLatin1Char('$'))) reg = reg.mid(1).trimmed();
  const QString val = newValue.trimmed();
  if (reg.isEmpty() || val.isEmpty() || !detail::validateSafeDebuggerInput(reg) || !detail::validateSafeDebuggerInput(val)) return;
  const int r = qMax(0, rank);
  if (r >= static_cast<int>(m_processes.size()) || !m_processes[r]) return;
  queueOrWriteCmd(r, QStringLiteral("-gdb-set $%1=%2\n").arg(reg, val));
  if (m_processes[r]->state.currentState == QLatin1String("stopped")) writeCmd(r, QStringLiteral("-data-list-register-values r\n"));
}

}  // namespace gridlock
