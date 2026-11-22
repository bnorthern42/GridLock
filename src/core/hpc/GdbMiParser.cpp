/**
 * @file GdbMiParser.cpp
 * @brief Incremental GDB/MI3 asynchronous/result record parser and evaluation stream routing.
 *
 * Implements `GdbRankCoordinator::processGdbOutput`, watchpoint trigger extraction,
 * hover/expression evaluation token routing, register/memory/locals MI tuple decoding,
 * and batched inferior console/target output filtering.
 */

#include "GdbRankCoordinator.hpp"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

namespace gridlock {

namespace detail {
QString escapeGdbMiString(const QString& str);
QString unescapeMiValue(QString raw);
}  // namespace detail

namespace {

/** @brief Maps standard x86_64 GDB register numbers (0..17) to canonical lowercase mnemonics. */
QString x86RegName(int idx) {
  static const char* const kNames[] = {"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp", "r8",
                                       "r9",  "r10", "r11", "r12", "r13", "r14", "r15", "rip", "eflags"};
  return (idx >= 0 && idx < 18) ? QLatin1String(kNames[idx]) : QString();
}

/** @brief Formats `address="..."` and `inst="..."` tuples from `-data-disassemble` MI output. */
QString formatMiDisassembly(const QString& miLine) {
  QString prettyAsm;
  static const QRegularExpression instRe("address=\"([^\"]+)\".*?inst=\"([^\"]+)\"");
  auto it = instRe.globalMatch(miLine);
  while (it.hasNext()) {
    const auto m = it.next();
    prettyAsm += QString("%1: %2\n").arg(m.captured(1), m.captured(2));
  }
  return prettyAsm;
}

QHash<int, QString>& evalTokenExprMap() {
  static QHash<int, QString> m;
  return m;
}

}  // namespace

/** @brief Sends `100-data-evaluate-expression` for a source-editor mouse hover on @p rankId. */
void GdbRankCoordinator::evaluateHoverVariable(int rankId, const QString& varName, QPoint globalPos) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId];
  if (!rp || rp->state.currentState != "stopped") return;
  rp->lastHoverVarName = varName;
  rp->lastHoverPos = globalPos;
  writeCmd(rankId, QString("100-data-evaluate-expression %1\n").arg(detail::escapeGdbMiString(varName)));
}

/** @brief Sends `101xxx-data-evaluate-expression` for an interactive watch/console expression on @p rankId. */
void GdbRankCoordinator::evaluateExpression(int rankId, const QString& expression) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId];
  if (!rp || rp->state.currentState != "stopped") return;
  rp->lastEvalExpression = expression;
  static int s_evalSeq = 0;
  const int tok = 101000 + (++s_evalSeq % 900);
  evalTokenExprMap().insert(tok, expression);
  writeCmd(rankId, QString("%1-data-evaluate-expression %2\n").arg(tok).arg(detail::escapeGdbMiString(expression)));
}

/** @brief Sends `-data-read-memory-bytes` to read @p length bytes at @p address on @p rankId. */
void GdbRankCoordinator::readMemory(int rankId, const QString& address, int length) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId];
  if (!rp || rp->state.currentState != "stopped") return;
  writeCmd(rankId, QString("-data-read-memory-bytes %1 %2\n").arg(address).arg(length));
}

/** @brief Extracts `exp`, `old`, and `new` fields from a GDB/MI `*stopped,reason=".*watchpoint-trigger"` record. */
void GdbRankCoordinator::handleWatchpointStopRecord(int rankId, const QString& line) {
  static const QRegularExpression expRe(
      QStringLiteral("(?:wpt|hw-awpt|hw-rwpt)=\\{[^}]*exp=\"((?:\\\\.|[^\"])*)\""));
  static const QRegularExpression oldRe(
      QStringLiteral("value=\\{[^}]*old=\"((?:\\\\.|[^\"])*)\""));
  static const QRegularExpression newRe(
      QStringLiteral("value=\\{[^}]*(?:new|value)=\"((?:\\\\.|[^\"])*)\""));

  const auto em = expRe.match(line);
  const QString expr = em.hasMatch() ? detail::unescapeMiValue(em.captured(1)) : QStringLiteral("watchpoint");
  const auto om = oldRe.match(line);
  const QString oldVal = om.hasMatch() ? detail::unescapeMiValue(om.captured(1)) : QString();
  const auto nm = newRe.match(line);
  const QString newVal = nm.hasMatch() ? detail::unescapeMiValue(nm.captured(1)) : QString();
  emit watchpointHit(rankId, expr, oldVal, newVal);
}

/** @brief Incremental GDB/MI3 state machine parser for a single MPI rank. */
void GdbRankCoordinator::processGdbOutput(int rankId, const QString& output) {
  if (m_shuttingDown || rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId];
  rp->buffer += output;
  QStringList batchedProgramOut;
  int newlineIdx = -1;
  while (!m_shuttingDown && (newlineIdx = rp->buffer.indexOf('\n')) != -1) {
    const QString line = rp->buffer.left(newlineIdx).trimmed();
    rp->buffer.remove(0, newlineIdx + 1);
    if (line.isEmpty()) continue;
    emit gdbOutputReceived(rankId, line);

    if (line.startsWith("=thread-group-started") || line.startsWith("^done,groups=[")) {
      static const QRegularExpression pidRe("pid=\"(\\d+)\"");
      const auto pm = pidRe.match(line);
      if (pm.hasMatch()) {
        const qint64 pid = pm.captured(1).toLongLong();
        rp->inferiorPid = static_cast<pid_t>(pid);
        m_rankPids[rankId] = pid;
        emit rankStateChanged(rankId, rp->state);
      }
      continue;
    }
    if (line.startsWith("=thread-group-exited")) {
      rp->inferiorPid = 0;
      m_rankPids.remove(rankId);
      continue;
    }

    if (line.startsWith("*stopped") || line.startsWith("^connected")) {
      rp->pendingAttach = false;
      if (line.contains("reason=\"exited") || line.contains("reason=\"exited-normally\"")) {
        rp->state.currentState = "exited";
        rp->inferiorPid = 0;
        m_rankPids.remove(rankId);
        emit rankStateChanged(rankId, rp->state);
        continue;
      }
      const bool syntheticSigint = line.contains("signal-name=\"SIGINT\"") && rp->pendingInterruptSignals > 0;
      if (syntheticSigint) --rp->pendingInterruptSignals;
      if (rp->autoContinueAfterPending || syntheticSigint) {
        rp->autoContinueAfterPending = false;
        const QStringList pending = std::move(rp->pendingAfterStopCmds);
        rp->pendingAfterStopCmds.clear();
        for (const QString& c : pending) writeCmd(rankId, c);
        static const QRegularExpression rxBkptNo("bkptno=\"(\\d+)\"");
        const auto mBkpt = rxBkptNo.match(line);
        const bool activeBkptHit = line.contains("breakpoint-hit") &&
                                   (!mBkpt.hasMatch() || rp->state.breakpoints.contains(mBkpt.captured(1).toInt()));
        if (syntheticSigint || (!activeBkptHit && !line.contains("watchpoint-trigger") && !line.contains("end-stepping-range"))) {
          writeCmd(rankId, "-exec-continue\n");
          rp->state.currentState = "running";
          continue;
        }
      }
      rp->state.currentState = "stopped";
      if (rp->state.executionTimer.isValid()) {
        rp->state.totalRuntimeMs += rp->state.executionTimer.elapsed();
        rp->state.executionTimer.invalidate();
      }
      static const QRegularExpression rxFile("fullname=\"([^\"]+)\""), rxLine("frame=\\{.*?line=\"(\\d+)\"");
      static const QRegularExpression rxFunc("frame=\\{[^}]*func=\"([^\"]+)\""), rxAddr("frame=\\{[^}]*addr=\"([^\"]+)\"");
      const auto matchFile = rxFile.match(line), matchLine = rxLine.match(line);
      rp->state.currentFile = matchFile.hasMatch() ? matchFile.captured(1) : rp->state.currentFile;
      rp->state.currentLine = matchLine.hasMatch() ? matchLine.captured(1).toInt() : (rp->state.currentLine > 0 ? rp->state.currentLine : -1);
      if (const auto matchFunc = rxFunc.match(line); matchFunc.hasMatch()) rp->state.currentFunction = matchFunc.captured(1);
      const auto matchAddr = rxAddr.match(line);
      const QString stopAddress = matchAddr.hasMatch() ? matchAddr.captured(1) : QStringLiteral("$pc");
      if (matchAddr.hasMatch()) rp->state.registers[16] = stopAddress;
      const bool isWatchTrigger = line.contains("watchpoint-trigger");
      if (isWatchTrigger) handleWatchpointStopRecord(rankId, line);
      if (!isWatchTrigger && rp->state.lastFiredTimestamp.isEmpty()) {
        rp->state.lastFiredTimestamp = "synced";
        flushCachedBreakpoints(rankId);
        continue;
      }
      if (line.contains("breakpoint-hit")) {
        bool handledLogpoint = false;
        const QString curBase = QFileInfo(rp->state.currentFile).fileName();
        for (const auto& bp : m_pendingBreakpoints) {
          if (!bp.logMessage.isEmpty() && bp.line == rp->state.currentLine &&
              (bp.file == rp->state.currentFile || QFileInfo(bp.file).fileName() == curBase)) {
            QMap<QString, QString> vals;
            static const QRegularExpression rxArg(QStringLiteral("\\{name=\"([^\"]+)\",value=\"((?:\\\\.|[^\"])*)\"\\}")),
                rxPh(QStringLiteral("\\{([A-Za-z_][A-Za-z0-9_]*)\\}"));
            for (auto it = rxArg.globalMatch(line); it.hasNext();) {
              const auto m = it.next();
              vals.insert(m.captured(1), m.captured(2));
            }
            bool needLocals = false;
            for (auto phIt = rxPh.globalMatch(bp.logMessage); phIt.hasNext();) {
              if (!vals.contains(phIt.next().captured(1))) { needLocals = true; break; }
            }
            if (needLocals) {
              rp->pendingLogpointMessage = bp.logMessage;
              rp->pendingLogpointArgs = vals;
              writeCmd(rankId, "209-stack-list-variables --simple-values\n");
            } else {
              QString msg = bp.logMessage;
              for (auto vIt = vals.constBegin(); vIt != vals.constEnd(); ++vIt) {
                msg.replace(QStringLiteral("{%1}").arg(vIt.key()), vIt.value());
              }
              emit programOutputReceived(rankId, QStringLiteral("[LOGPOINT Rank %1] %2\n").arg(rankId).arg(msg), false);
              writeCmd(rankId, "-exec-continue\n");
              rp->state.currentState = "running";
            }
            handledLogpoint = true;
            break;
          }
        }
        if (handledLogpoint) continue;
      }
      const bool isStepOrSignal = isWatchTrigger || line.startsWith("^connected") ||
                                  line.contains("reason=\"end-stepping-range\"") ||
                                  line.contains("reason=\"location-reached\"") ||
                                  line.contains("reason=\"no-history-boundary\"") ||
                                  line.contains("reason=\"signal-received\"");
      if ((rp->state.currentFile.isEmpty() || rp->state.currentFile == "??") && !isStepOrSignal) {
        writeCmd(rankId, "-exec-continue\n");
        rp->state.currentState = "running";
      } else {
        writeCmd(rankId, QString("200-data-disassemble -s %1 -e \"%1 + 64\" -- 0\n").arg(stopAddress));
        writeCmd(rankId, "-data-list-register-values r\n");
        writeCmd(rankId, "201-stack-list-variables --simple-values\n");
        static const QRegularExpression threadRe("thread-id=\"(\\d+)\"");
        const auto matchThread = threadRe.match(line);
        writeCmd(rankId, matchThread.hasMatch() ? QString("-thread-select %1\n").arg(matchThread.captured(1))
                                                : QStringLiteral("-thread-info\n"));
        for (int varIdx = 0; varIdx < static_cast<int>(m_watchVariables.size()); ++varIdx) {
          const QString& varName = m_watchVariables[varIdx];
          if (!m_varNameMap[rankId].contains(varName)) {
            writeCmd(rankId, QString("30%1-var-create - * %2\n").arg(varIdx).arg(varName));
          }
        }
        if (matchThread.hasMatch()) m_activeThreadIds[rankId] = matchThread.captured(1).toInt();
        requestStackAndThreads(rankId);
        emit rankStateChanged(rankId, rp->state);
        if (rp->state.currentLine > 0) emit locationChanged(rankId, rp->state.currentFile, rp->state.currentLine);
      }
    } else if (line.startsWith("*running")) {
      rp->state.currentState = "running";
      rp->state.executionTimer.start();
      emit rankStateChanged(rankId, rp->state);
    } else if (line.startsWith("30")) {
      if (line.contains("^done")) {
        static const QRegularExpression evalRe("30(\\d+)\\^done,name=\"([^\"]+)\".*?value=\"([^\"]+)\"");
        if (const auto match = evalRe.match(line); match.hasMatch()) {
          const int varIdx = match.captured(1).toInt();
          if (varIdx >= 0 && varIdx < static_cast<int>(m_watchVariables.size())) {
            const QString& varName = m_watchVariables[varIdx];
            const QString varId = match.captured(2);
            m_varNameMap[rankId][varName] = varId;
            m_varNameRevMap[rankId][varId] = varName;
            rp->state.variableWatches[varName] = match.captured(3);
            emit rankStateChanged(rankId, rp->state);
          }
        }
      } else if (line.contains("^error")) {
        static const QRegularExpression errRe("30(\\d+)\\^error");
        if (const auto match = errRe.match(line); match.hasMatch()) {
          const int varIdx = match.captured(1).toInt();
          if (varIdx >= 0 && varIdx < static_cast<int>(m_watchVariables.size())) {
            const QString& varName = m_watchVariables[varIdx];
            rp->state.variableWatches[varName] = "<Out of Scope>";
            m_varNameMap[rankId].remove(varName);
            emit rankStateChanged(rankId, rp->state);
          }
        }
      }
    } else if (line.contains("^done,changelist=")) {
      static const QRegularExpression objRe("\\{([^}]+)\\}"), nameRe("name=\"([^\"]+)\""),
          valRe("value=\"([^\"]+)\""), inScopeRe("in_scope=\"([^\"]+)\"");
      auto it = objRe.globalMatch(line);
      bool updated = false;
      while (it.hasNext()) {
        const QString objStr = it.next().captured(1);
        const auto nameMatch = nameRe.match(objStr);
        if (!nameMatch.hasMatch()) continue;
        const QString varId = nameMatch.captured(1);
        if (!m_varNameRevMap.contains(rankId) || !m_varNameRevMap[rankId].contains(varId)) continue;
        const QString varName = m_varNameRevMap[rankId][varId];
        const auto scopeMatch = inScopeRe.match(objStr);
        if (!scopeMatch.hasMatch() || scopeMatch.captured(1) == "true") {
          if (const auto valMatch = valRe.match(objStr); valMatch.hasMatch()) {
            rp->state.variableWatches[varName] = valMatch.captured(1);
            updated = true;
          }
        } else {
          rp->state.variableWatches[varName] = "Out of scope";
          writeCmd(rankId, QString("-var-delete %1\n").arg(varId));
          m_varNameMap[rankId].remove(varName);
          m_varNameRevMap[rankId].remove(varId);
          updated = true;
        }
      }
      if (updated) emit rankStateChanged(rankId, rp->state);
    } else if (line.startsWith("^done,memory=[")) {
      static const QRegularExpression memRe("begin=\"([^\"]+)\".*?contents=\"([^\"]+)\"");
      if (const auto match = memRe.match(line); match.hasMatch()) {
        bool ok = false;
        const qint64 beginAddress = match.captured(1).toULongLong(&ok, 16);
        const QString hex = match.captured(2);
        emit memoryDataReady(rankId, beginAddress, hex);
        emit memoryRead(rankId, match.captured(1), QByteArray::fromHex(hex.toLatin1()));
      }
    } else if (line.startsWith("^done,register-values=[")) {
      static const QRegularExpression regRe("number=\"(\\d+)\".*?value=\"([^\"]+)\"");
      auto it = regRe.globalMatch(line);
      QJsonArray jsonRegs;
      bool updated = false;
      while (it.hasNext()) {
        const auto m = it.next();
        const int regNum = m.captured(1).toInt();
        const QString regVal = m.captured(2);
        rp->state.registers[regNum] = regVal;
        if (const QString name = x86RegName(regNum); !name.isEmpty()) jsonRegs.append(QJsonObject{{"name", name}, {"value", regVal}});
        updated = true;
      }
      if (updated) {
        if (!jsonRegs.isEmpty()) emit registersUpdated(rankId, jsonRegs);
        emit rankStateChanged(rankId, rp->state);
      }
    } else if (handleBreakpointMiRecord(rankId, line)) {
      // Handled `^done,bkpt={...}`, `=breakpoint-created`, and `=breakpoint-deleted` records.
    } else if (line.startsWith("100")) {
      static const QRegularExpression val100Re("100\\^done,value=\"([^\"]+)\""), err100Re("msg=\"([^\"]+)\"");
      if (const auto match = line.contains("^done") ? val100Re.match(line) : err100Re.match(line); match.hasMatch()) {
        emit hoverEvaluationComplete(rp->lastHoverVarName, match.captured(1), rp->lastHoverPos);
      }
    } else if (line.startsWith("101")) {
      const int caret = line.indexOf('^');
      const QString resolvedExpr = (caret > 0 && evalTokenExprMap().contains(line.left(caret).toInt()))
                                       ? evalTokenExprMap().take(line.left(caret).toInt())
                                       : rp->lastEvalExpression;
      auto unesc = [](QString s) { return s.replace(QLatin1String("\\\""), QLatin1String("\"")).replace(QLatin1String("\\\\"), QLatin1String("\\")); };
      if (line.contains("^done")) {
        static const QRegularExpression valRe("\\^done,value=\"((?:\\\\.|[^\"])*)\"");
        if (const auto m = valRe.match(line); m.hasMatch()) emit expressionEvaluated(rankId, resolvedExpr, unesc(m.captured(1)));
      } else if (line.contains("^error")) {
        static const QRegularExpression errRe("msg=\"((?:\\\\.|[^\"])*)\"");
        if (const auto m = errRe.match(line); m.hasMatch()) emit expressionEvaluated(rankId, resolvedExpr, "Error: " + unesc(m.captured(1)));
      }
    } else if (line.startsWith("200")) {
      if (line.contains("asm_insns=")) {
        const QString prettyAsm = formatMiDisassembly(line);
        if (!prettyAsm.isEmpty()) { rp->state.disassemblyText = prettyAsm; emit rankStateChanged(rankId, rp->state); }
      }
      writeCmd(rankId, "-var-update 1 *\n");
    } else if (line.startsWith("201^done,variables=[")) {
      static const QRegularExpression varTupleRe("\\{([^{}]+)\\}"), vNameRe("name=\"([^\"]+)\""),
          vTypeRe("type=\"([^\"]+)\""), vValRe("value=\"((?:\\\\.|[^\"])*)\"");
      QJsonArray locals;
      QHash<QString, int> idxByName;
      auto it = varTupleRe.globalMatch(line);
      while (it.hasNext()) {
        const QString tuple = it.next().captured(1);
        const auto nm = vNameRe.match(tuple);
        if (!nm.hasMatch()) continue;
        const QString varName = nm.captured(1);
        if (varName.endsWith(QStringLiteral("@entry"))) continue;
        const auto tm = vTypeRe.match(tuple);
        const auto vm = vValRe.match(tuple);
        const QString varVal = vm.hasMatch() ? vm.captured(1).replace(QLatin1String("\\\""), QLatin1String("\"")).replace(QLatin1String("\\\\"), QLatin1String("\\"))
                                             : QStringLiteral("{...}");
        const QJsonObject entry{{"name", varName}, {"type", tm.hasMatch() ? tm.captured(1) : QStringLiteral("compound")}, {"value", varVal}, {"variablesReference", 0}};
        if (idxByName.contains(varName)) {
          const int prevIdx = idxByName.value(varName);
          const QString prevVal = locals.at(prevIdx).toObject().value("value").toString();
          if ((prevVal == QLatin1String("{...}") || prevVal.isEmpty()) && varVal != QLatin1String("{...}") && !varVal.isEmpty()) locals[prevIdx] = entry;
          continue;
        }
        idxByName.insert(varName, locals.size());
        locals.append(entry);
      }
      emit localsUpdated(rankId, locals);
    } else if (handleMiStackOrThreadRecord(rankId, line)) {
      // Handled tokens 202..213 and >= 4000 lazy varobj responses.
    } else if (line.startsWith("^error")) {
      static const QRegularExpression errMsgRe("msg=\"((?:\\\\.|[^\"])*)\"");
      const auto m = errMsgRe.match(line);
      QString msg = m.hasMatch() ? m.captured(1) : line;
      msg.replace(QLatin1String("\\\""), QLatin1String("\"")).replace(QLatin1String("\\n"), QLatin1String(" ")).replace(QLatin1String("\\\\"), QLatin1String("\\"));
      emit debuggerConsoleOutput(rankId, QStringLiteral("Error: ") + msg, true);
      if (rp->pendingAttach || msg.contains(QStringLiteral("ptrace"), Qt::CaseInsensitive) ||
          msg.contains(QStringLiteral("Operation not permitted"), Qt::CaseInsensitive) ||
          msg.contains(QStringLiteral("Cannot attach"), Qt::CaseInsensitive)) {
        terminateAllSessions();
        emit sessionErrorOccurred(msg);
        return;
      }
    } else if (line.contains("asm_insns=")) {
      const QString prettyAsm = formatMiDisassembly(line);
      if (!prettyAsm.isEmpty()) { rp->state.disassemblyText = prettyAsm; emit rankStateChanged(rankId, rp->state); }
    } else if (line.startsWith("@\"")) {
      QString txt = line.mid(2, line.endsWith('"') ? line.size() - 3 : line.size() - 2);
      txt.replace("\\n", "\n").replace("\\t", "\t").replace("\\\"", "\"").replace("\\\\", "\\");
      if (const QString cleaned = txt.trimmed(); !cleaned.isEmpty()) batchedProgramOut << cleaned;
    } else {
      static const QRegularExpression miPrefRe("^\\d*[\\^*+=~&@]|^\\(gdb\\)");
      if (!miPrefRe.match(line).hasMatch() && !line.startsWith("[New ") && !line.startsWith("[Thread ") &&
          !line.startsWith("[Inferior ") && !line.startsWith("[Detaching") && !line.startsWith("Reading symbols") && !line.startsWith("warning:"))
        batchedProgramOut << line;
    }
  }
  if (!batchedProgramOut.isEmpty()) emit programOutputReceived(rankId, batchedProgramOut.join(QLatin1Char('\n')), false);
}

}  // namespace gridlock
