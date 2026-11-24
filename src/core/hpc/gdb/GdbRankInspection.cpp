/**
 * @file GdbRankInspection.cpp
 * @brief GDB/MI3 64-frame backtrace, OS/OpenMP thread list, lazy varobj expansion, and caller frame switching handlers.
 *
 * Implements `GdbRankCoordinator::requestStackAndThreads`, `selectStackFrame`,
 * `requestVariableChildren`, and brace-balanced GDB/MI3 `stack=[...]` / `threads=[...]` /
 * `children=[...]` record parsers. Watchpoint, catchpoint, breakpoint, signal, and
 * execution MI command builders reside in `GdbRankCommands.cpp`.
 */

#include "GdbRankCoordinator.hpp"
#include "MiSanitizer.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include <QFileInfo>
#include <QRegularExpression>

namespace gridlock::core {

/**
 * @brief Weak linkage fallback for `MiSanitizer::sanitize` in standalone unit-test targets
 *        whose Meson source list omits `MiSanitizer.cpp` (superseded by strong symbol in `gridlock`).
 */
__attribute__((weak)) MiSanitizer::SanitizationResult MiSanitizer::sanitize(std::string_view payload) {
  SanitizationResult result{"", false, false, false};
  if (payload.size() > MAX_PAYLOAD_BYTES) {
    payload = payload.substr(0, MAX_PAYLOAD_BYTES);
    result.wasTruncated = true;
  }
  size_t depth = 0;
  bool inQuotes = false, escaped = false;
  std::ostringstream out;
  for (size_t i = 0; i < payload.size(); ++i) {
    const char c = payload[i];
    if (c == '\0') { result.dropped = true; return result; }
    if (escaped) escaped = false;
    else if (c == '\\') escaped = true;
    else if (c == '"') inQuotes = !inQuotes;
    if (!inQuotes && (c == ';' || c == '|' || c == '&' || c == '`' || c == '$')) { result.dropped = true; return result; }
    if (c == '\n' || c == '\r') {
      size_t nxt = i + 1;
      while (nxt < payload.size() && (payload[nxt] == ' ' || payload[nxt] == '\t' || payload[nxt] == '\r' || payload[nxt] == '\n')) ++nxt;
      if (nxt < payload.size()) { result.dropped = true; return result; }
    }
    if (c == '{' || c == '[') {
      if (++depth > MAX_MI_NESTING_DEPTH) { result.depthExceeded = true; result.dropped = true; return result; }
    } else if ((c == '}' || c == ']') && depth > 0) {
      --depth;
    }
    const unsigned char uc = static_cast<unsigned char>(c);
    if ((uc <= 0x1F || uc == 0x7F) && c != '\t' && c != '\n' && c != '\r') {
      out << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(uc);
    } else {
      out << c;
    }
  }
  result.sanitizedPayload = out.str();
  return result;
}

}  // namespace gridlock::core

namespace gridlock {

namespace detail {

/** @brief Validates interactive GDB input via `MiSanitizer`, blocking shell escapes while permitting `$reg` / `&var`. */
bool validateSafeDebuggerInput(const QString& rawInput) {
  if (rawInput.contains(QChar('\0')) || rawInput.contains(QLatin1Char('\n')) || rawInput.contains(QLatin1Char('\r'))) return false;
  const QString trimmed = rawInput.trimmed();
  if (trimmed.isEmpty()) return false;
  static const QRegularExpression shellPrefixRe(
      QStringLiteral("^(?:!|shell(?:\\s|$)|pipe(?:\\s|\\||$)|make(?:\\s|$))"),
      QRegularExpression::CaseInsensitiveOption);
  if (shellPrefixRe.match(trimmed).hasMatch() || trimmed.contains(QStringLiteral("$(")) ||
      trimmed.contains(QStringLiteral("${")) || trimmed.contains(QStringLiteral("&&")) ||
      trimmed.contains(QStringLiteral("||"))) {
    return false;
  }
  QString masked = trimmed;
  static const QRegularExpression benignRegAddrRe(QStringLiteral("[$&](?=[A-Za-z0-9_])"));
  masked.replace(benignRegAddrRe, QStringLiteral("_GL_OP_"));
  const auto res = core::MiSanitizer::sanitize(masked.toStdString());
  return !res.dropped && !res.depthExceeded && !res.wasTruncated;
}

/** @brief Unescapes GDB/MI C-style string escapes (`\"`, `\\`, `\n`, `\t`) inside a captured value. */
QString unescapeMiValue(QString raw) {
  raw.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
  raw.replace(QStringLiteral("\\t"), QStringLiteral("\t"));
  raw.replace(QStringLiteral("\\\""), QStringLiteral("\""));
  raw.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
  return raw;
}

}  // namespace detail

namespace {

/** @brief Extracts top-level `{...}` tuple interiors from a GDB/MI list body while respecting quotes and nested braces. */
QVector<QString> extractTopLevelMiTuples(const QString& listText) {
  QVector<QString> tuples;
  bool inQuotes = false, escaped = false;
  int braceDepth = 0, tupleStart = -1;
  for (int i = 0; i < listText.size(); ++i) {
    const QChar ch = listText.at(i);
    if (escaped) { escaped = false; continue; }
    if (inQuotes) {
      if (ch == QLatin1Char('\\')) escaped = true;
      else if (ch == QLatin1Char('"')) inQuotes = false;
      continue;
    }
    if (ch == QLatin1Char('"')) inQuotes = true;
    else if (ch == QLatin1Char('{')) { if (braceDepth == 0) tupleStart = i + 1; ++braceDepth; }
    else if (ch == QLatin1Char('}')) {
      if (--braceDepth == 0 && tupleStart >= 0) {
        tuples.append(listText.mid(tupleStart, i - tupleStart));
        tupleStart = -1;
      }
    }
  }
  return tuples;
}

/** @brief Extracts the substring enclosed by `[` and its matching `]` after @p keyPrefix. */
QString extractBracketBody(const QString& line, const QString& keyPrefix) {
  const int startPos = line.indexOf(keyPrefix);
  if (startPos < 0) return QString();
  const int contentStart = startPos + keyPrefix.size();
  bool inQuotes = false, escaped = false;
  int bracketDepth = 1;
  for (int i = contentStart; i < line.size(); ++i) {
    const QChar ch = line.at(i);
    if (escaped) { escaped = false; continue; }
    if (inQuotes) {
      if (ch == QLatin1Char('\\')) escaped = true;
      else if (ch == QLatin1Char('"')) inQuotes = false;
      continue;
    }
    if (ch == QLatin1Char('"')) inQuotes = true;
    else if (ch == QLatin1Char('[')) ++bracketDepth;
    else if (ch == QLatin1Char(']') && --bracketDepth == 0) return line.mid(contentStart, i - contentStart);
  }
  return line.mid(contentStart);
}

QString extractQuotedStreamPayload(const QString& line) {
  QString payload = line.mid(1).trimmed();
  if (payload.startsWith(QLatin1Char('"')) && payload.endsWith(QLatin1Char('"')) && payload.size() >= 2) {
    payload = detail::unescapeMiValue(payload.mid(1, payload.size() - 2));
  }
  return payload;
}

}  // namespace

/** @brief Queries up to 64 call-stack frames (`-stack-list-frames 0 63`) and live threads (`-thread-info`) on @p rank. */
void GdbRankCoordinator::requestStackAndThreads(int rank) {
  if (rank < 0 || rank >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rank];
  if (!rp || rp->state.currentState != QLatin1String("stopped")) return;
  if (m_rankThreads.contains(rank)) emit threadsUpdated(rank, m_rankThreads.value(rank));
  if (m_rankFrames.contains(rank)) emit stackTraceUpdated(rank, m_rankFrames.value(rank));
  writeCmd(rank, QStringLiteral("202-stack-list-frames 0 63\n"));
  writeCmd(rank, QStringLiteral("203-thread-info\n"));
}

/** @brief Parses MI `202..213` stack, thread, console REPL, watchpoint, signal, completion, and `>= 4000` varobj records. */
bool GdbRankCoordinator::handleMiStackOrThreadRecord(int rankId, const QString& line) {
  if (handleCheckpointMiRecord(rankId, line)) return true;

  if (m_pendingCompletePrefix.contains(rankId) && (line.startsWith(QLatin1Char('~')) || line.startsWith(QLatin1Char('&')))) {
    const QString payload = extractQuotedStreamPayload(line);
    if (!payload.isEmpty()) m_completeBuffer[rankId] += payload;
    return true;
  }
  if (line.startsWith(QLatin1String("211^done")) || line.startsWith(QLatin1String("211^error"))) {
    const QString prefix = m_pendingCompletePrefix.take(rankId), buffered = m_completeBuffer.take(rankId);
    QStringList completions;
    if (line.startsWith(QLatin1String("211^done"))) {
      for (const QString& rawLn : buffered.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString item = rawLn.trimmed();
        if (!item.isEmpty() && !completions.contains(item)) {
          completions.append(item);
          if (completions.size() >= 100) break;
        }
      }
    }
    emit consoleCompletionsReady(prefix, completions);
    return true;
  }

  if (m_pendingInfoHandle.value(rankId, false) && (line.startsWith(QLatin1Char('~')) || line.startsWith(QLatin1Char('&')))) {
    const QString payload = extractQuotedStreamPayload(line);
    if (!payload.isEmpty()) m_infoHandleBuffer[rankId] += payload;
    return true;
  }
  if (line.startsWith(QLatin1String("210^done")) || line.startsWith(QLatin1String("210^error"))) {
    m_pendingInfoHandle[rankId] = false;
    const QString buffered = m_infoHandleBuffer.take(rankId);
    if (line.startsWith(QLatin1String("210^done"))) {
      static const QRegularExpression sigRowRe(
          QStringLiteral("^\\s*(SIG[A-Z0-9+-]+)\\s+(Yes|No)\\s+(Yes|No)\\s+(Yes|No)\\s*(.*)$"),
          QRegularExpression::CaseInsensitiveOption);
      QVector<SignalDisposition> rows;
      for (const QString& rawLn : buffered.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (const auto m = sigRowRe.match(rawLn); m.hasMatch()) {
          SignalDisposition disp{m.captured(1).toUpper(),
                                 m.captured(2).compare(QStringLiteral("Yes"), Qt::CaseInsensitive) == 0,
                                 m.captured(3).compare(QStringLiteral("Yes"), Qt::CaseInsensitive) == 0,
                                 m.captured(4).compare(QStringLiteral("Yes"), Qt::CaseInsensitive) == 0,
                                 m.captured(5).trimmed()};
          m_signalDispositions[disp.signal] = disp;
          rows.append(disp);
        }
      }
      if (!rows.isEmpty()) emit signalDispositionsReady(rankId, rows);
    }
    return true;
  }

  if (m_pendingConsoleCmdCount.value(rankId, 0) > 0 && (line.startsWith(QLatin1Char('~')) || line.startsWith(QLatin1Char('&')))) {
    const QString payload = extractQuotedStreamPayload(line);
    if (!payload.trimmed().isEmpty()) m_consoleReplyBuffer[rankId] += payload;
    return true;
  }
  if (line.startsWith(QLatin1String("204^done")) || line.startsWith(QLatin1String("204^error"))) {
    m_pendingConsoleCmdCount[rankId] = std::max(0, m_pendingConsoleCmdCount.value(rankId, 0) - 1);
    const QString buffered = m_consoleReplyBuffer.take(rankId).trimmed();
    if (line.startsWith(QLatin1String("204^error"))) {
      static const QRegularExpression msgRe(QStringLiteral("msg=\"((?:\\\\.|[^\"])*)\""));
      const auto mm = msgRe.match(line);
      emit debuggerConsoleOutput(rankId, QStringLiteral("Error: %1").arg(mm.hasMatch() ? detail::unescapeMiValue(mm.captured(1)) : buffered), true);
    } else if (!buffered.isEmpty()) {
      emit debuggerConsoleOutput(rankId, buffered, false);
    }
    return true;
  }

  if (line.startsWith(QLatin1String("205^done")) || line.startsWith(QLatin1String("205^error"))) {
    static const QRegularExpression valRe(QStringLiteral("value=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression msgRe(QStringLiteral("msg=\"((?:\\\\.|[^\"])*)\""));
    if (line.startsWith(QLatin1String("205^error"))) {
      const auto mm = msgRe.match(line);
      emit debuggerConsoleOutput(rankId, QStringLiteral("Error: %1").arg(mm.hasMatch() ? detail::unescapeMiValue(mm.captured(1)) : line), true);
    } else {
      const auto vm = valRe.match(line);
      const QString out = vm.hasMatch() ? detail::unescapeMiValue(vm.captured(1)) : line.mid(QStringLiteral("205^done").size()).trimmed();
      emit debuggerConsoleOutput(rankId, out.isEmpty() ? QStringLiteral("OK") : out, false);
    }
    return true;
  }

  if (line.startsWith(QLatin1String("209^done")) || line.startsWith(QLatin1String("209^error"))) {
    if (static_cast<size_t>(rankId) < m_processes.size() && m_processes[rankId] && !m_processes[rankId]->pendingLogpointMessage.isEmpty()) {
      auto& rp = m_processes[rankId];
      static const QRegularExpression rxLv(QStringLiteral("\\{name=\"([^\"]+)\",(?:type=\"[^\"]*\",)?value=\"((?:\\\\.|[^\"])*)\"\\}"));
      for (auto it = rxLv.globalMatch(line); it.hasNext();) {
        const auto m = it.next();
        rp->pendingLogpointArgs.insert(m.captured(1), detail::unescapeMiValue(m.captured(2)));
      }
      QString msg = rp->pendingLogpointMessage;
      for (auto vIt = rp->pendingLogpointArgs.constBegin(); vIt != rp->pendingLogpointArgs.constEnd(); ++vIt) {
        msg.replace(QStringLiteral("{%1}").arg(vIt.key()), vIt.value());
      }
      rp->pendingLogpointMessage.clear();
      rp->pendingLogpointArgs.clear();
      emit programOutputReceived(rankId, QStringLiteral("[LOGPOINT Rank %1] %2\n").arg(rankId).arg(msg), false);
      writeCmd(rankId, "-exec-continue\n");
      rp->state.currentState = "running";
    }
    return true;
  }

  if (line.startsWith(QLatin1String("206^done")) || line.startsWith(QLatin1String("206^error"))) {
    if (line.startsWith(QLatin1String("206^done"))) {
      static const QRegularExpression wpRe(QStringLiteral("(?:wpt|hw-awpt|hw-rwpt)=\\{number=\"(\\d+)\",exp=\"((?:\\\\.|[^\"])*)\""));
      if (const auto wm = wpRe.match(line); wm.hasMatch()) m_watchpointIds[rankId][detail::unescapeMiValue(wm.captured(2))] = wm.captured(1).toInt();
    } else {
      static const QRegularExpression msgRe(QStringLiteral("msg=\"((?:\\\\.|[^\"])*)\""));
      if (const auto mm = msgRe.match(line); mm.hasMatch()) emit debuggerConsoleOutput(rankId, QStringLiteral("Watchpoint error: %1").arg(detail::unescapeMiValue(mm.captured(1))), true);
    }
    return true;
  }

  // 1. Parse 64-frame backtrace response (`202^done,stack=[frame={...},...]`).
  if (line.startsWith(QLatin1String("202^done,stack=["))) {
    static const QRegularExpression levelRe(QStringLiteral("level=\"(\\d+)\"")), addrRe(QStringLiteral("addr=\"(0x[0-9a-fA-F]+)\"")),
        funcRe(QStringLiteral("func=\"((?:\\\\.|[^\"])*)\"")), fullRe(QStringLiteral("fullname=\"((?:\\\\.|[^\"])*)\"")),
        fileRe(QStringLiteral("file=\"((?:\\\\.|[^\"])*)\"")), lineRe(QStringLiteral("line=\"(\\d+)\""));
    const QVector<QString> tuples = extractTopLevelMiTuples(extractBracketBody(line, QStringLiteral("stack=[")));
    QVector<StackFrameEntry> frames;
    frames.reserve(tuples.size());
    for (int idx = 0; idx < tuples.size(); ++idx) {
      const QString& t = tuples.at(idx);
      StackFrameEntry entry;
      const auto lm = levelRe.match(t);
      entry.level = lm.hasMatch() ? lm.captured(1).toInt() : idx;
      entry.frameId = entry.level;
      if (const auto am = addrRe.match(t); am.hasMatch()) { bool ok = false; entry.address = am.captured(1).mid(2).toULongLong(&ok, 16); }
      const auto fnm = funcRe.match(t);
      entry.function = fnm.hasMatch() ? detail::unescapeMiValue(fnm.captured(1)) : QStringLiteral("??");
      const auto flm = fullRe.match(t), fim = fileRe.match(t);
      entry.file = (flm.hasMatch() && !flm.captured(1).isEmpty()) ? detail::unescapeMiValue(flm.captured(1))
                                                                  : (fim.hasMatch() ? detail::unescapeMiValue(fim.captured(1)) : QString());
      const auto lnm = lineRe.match(t);
      entry.line = lnm.hasMatch() ? lnm.captured(1).toInt() : 0;
      frames.append(entry);
    }
    m_rankFrames[rankId] = frames;
    emit stackTraceUpdated(rankId, frames);

    if (m_pendingFrameSelect.contains(rankId)) {
      const int targetLevel = m_pendingFrameSelect.take(rankId);
      for (const auto& f : frames) {
        if (f.level == targetLevel && rankId >= 0 && rankId < static_cast<int>(m_processes.size()) && m_processes[rankId]) {
          auto& rp = m_processes[rankId];
          rp->state.currentFunction = f.function;
          if (f.address != 0ULL) {
            const QString hexAddr = QStringLiteral("0x%1").arg(f.address, 0, 16);
            rp->state.registers[16] = hexAddr;
            writeCmd(rankId, QStringLiteral("200-data-disassemble -s %1 -e \"%1 + 64\" -- 0\n").arg(hexAddr));
          }
          if (!f.file.isEmpty() && f.line > 0) {
            rp->state.currentFile = f.file;
            rp->state.currentLine = f.line;
            emit locationChanged(rankId, f.file, f.line);
          }
          emit rankStateChanged(rankId, rp->state);
          break;
        }
      }
    }
    return true;
  }

  // 2. Parse OS / OpenMP / GPU wavefront thread list response (`203^done,threads=[...]` or `^done,threads=[...]`).
  if (line.startsWith(QLatin1String("203^done,threads=[")) || line.startsWith(QLatin1String("^done,threads=["))) {
    static const QRegularExpression curThreadRe(QStringLiteral("current-thread-id=\"(\\d+)\"")), idRe(QStringLiteral("\\bid=\"(\\d+)\"")),
        nameRe(QStringLiteral("\\bname=\"((?:\\\\.|[^\"])*)\"")), targetIdRe(QStringLiteral("target-id=\"((?:\\\\.|[^\"])*)\"")),
        stateRe(QStringLiteral("\\bstate=\"([^\"]+)\"")), funcRe(QStringLiteral("func=\"((?:\\\\.|[^\"])*)\"")),
        fileRe(QStringLiteral("file=\"((?:\\\\.|[^\"])*)\"")), lineRe(QStringLiteral("line=\"(\\d+)\"")),
        archRe(QStringLiteral("arch=\"([^\"]+)\""));
    const auto curMatch = curThreadRe.match(line);
    const int currentThreadId = curMatch.hasMatch() ? curMatch.captured(1).toInt() : m_activeThreadIds.value(rankId, 1);
    if (currentThreadId > 0) m_activeThreadIds[rankId] = currentThreadId;
    const QVector<QString> tuples = extractTopLevelMiTuples(extractBracketBody(line, QStringLiteral("threads=[")));
    QVector<ThreadEntry> threads;
    threads.reserve(tuples.size());
    for (int idx = 0; idx < tuples.size(); ++idx) {
      const QString& t = tuples.at(idx);
      const int framePos = t.indexOf(QStringLiteral("frame={"));
      const QString headerPart = (framePos >= 0) ? t.left(framePos) : t;
      const QString framePart = (framePos >= 0) ? t.mid(framePos) : QString();
      ThreadEntry entry;
      const auto idm = idRe.match(headerPart);
      entry.threadId = idm.hasMatch() ? idm.captured(1).toInt() : (idx + 1);
      const auto nm = nameRe.match(headerPart), tm = targetIdRe.match(headerPart);
      const QString rawName = (nm.hasMatch() && !nm.captured(1).isEmpty()) ? detail::unescapeMiValue(nm.captured(1)) : QString();
      const QString targetId = (tm.hasMatch() && !tm.captured(1).isEmpty()) ? detail::unescapeMiValue(tm.captured(1)) : QString();
      const QString frameArch = archRe.match(framePart).hasMatch() ? archRe.match(framePart).captured(1) : QString();
      const bool isGpuWave = targetId.startsWith(QStringLiteral("AMDGPU ")) || targetId.startsWith(QStringLiteral("CUDA ")) ||
                             frameArch.startsWith(QStringLiteral("amdgcn"));
      if (isGpuWave && !targetId.isEmpty()) {
        entry.name = (!rawName.isEmpty() && !targetId.contains(rawName)) ? QStringLiteral("%1 [%2]").arg(targetId, rawName) : targetId;
      } else {
        entry.name = !rawName.isEmpty() ? rawName : (!targetId.isEmpty() ? targetId : QStringLiteral("main"));
      }
      const auto sm = stateRe.match(t);
      entry.state = sm.hasMatch() ? sm.captured(1) : QStringLiteral("stopped");
      const auto fnm = funcRe.match(framePart), fim = fileRe.match(framePart), lnm = lineRe.match(framePart);
      if (fnm.hasMatch()) {
        entry.topFrameSummary = detail::unescapeMiValue(fnm.captured(1));
        if (fim.hasMatch() && lnm.hasMatch()) {
          entry.topFrameSummary += QStringLiteral(" @ %1:%2").arg(QFileInfo(detail::unescapeMiValue(fim.captured(1))).fileName(), lnm.captured(1));
        }
      }
      entry.isCurrent = (entry.threadId == currentThreadId);
      if (entry.isCurrent && !frameArch.isEmpty()) m_rankArch[rankId] = frameArch;
      threads.append(entry);
    }
    m_rankThreads[rankId] = threads;
    emit threadsUpdated(rankId, threads);
    return true;
  }

  // 3. Route tokenized varobj child expansion (`>= 4000`) for lazy `{...}` struct inspection.
  static const QRegularExpression varTokRe(QStringLiteral("^(\\d+)\\^(done|error)(.*)$"));
  if (const auto tm = varTokRe.match(line); tm.hasMatch()) {
    const int tok = tm.captured(1).toInt();
    const bool isErr = (tm.captured(2) == QLatin1String("error"));
    if (m_tokenToVarCreateQuery.contains(tok)) {
      const int qId = m_tokenToVarCreateQuery.take(tok);
      if (!m_varChildQueries.contains(qId)) return true;
      static const QRegularExpression nameRe(QStringLiteral("\\bname=\"((?:\\\\.|[^\"])*)\"")), numRe(QStringLiteral("\\bnumchild=\"(\\d+)\""));
      const auto nm = nameRe.match(line);
      if (isErr || !nm.hasMatch()) {
        const auto q = m_varChildQueries.take(qId);
        emit variableChildrenUpdated(rankId, q.parentExpression, {});
        return true;
      }
      auto& q = m_varChildQueries[qId];
      q.rootVarObj = detail::unescapeMiValue(nm.captured(1));
      const int nCh = numRe.match(line).hasMatch() ? numRe.match(line).captured(1).toInt() : 1;
      if (nCh <= 0) {
        const auto doneQ = m_varChildQueries.take(qId);
        writeCmd(rankId, QStringLiteral("-var-delete \"%1\"\n").arg(doneQ.rootVarObj));
        emit variableChildrenUpdated(rankId, doneQ.parentExpression, {});
        return true;
      }
      const int listTok = m_nextVarQueryToken++;
      m_tokenToVarListQuery[listTok] = qId;
      q.inFlightLists = 1;
      writeCmd(rankId, QStringLiteral("%1-var-list-children --all-values \"%2\"\n").arg(listTok).arg(q.rootVarObj));
      return true;
    }
    if (m_tokenToVarListQuery.contains(tok)) {
      const int qId = m_tokenToVarListQuery.take(tok);
      if (!m_varChildQueries.contains(qId)) return true;
      auto& q = m_varChildQueries[qId];
      q.inFlightLists = std::max(0, q.inFlightLists - 1);
      if (!isErr && line.contains(QLatin1String("children=["))) {
        const QVector<QString> tuples = extractTopLevelMiTuples(extractBracketBody(line, QStringLiteral("children=[")));
        static const QRegularExpression nameRe(QStringLiteral("\\bname=\"((?:\\\\.|[^\"])*)\"")), expRe(QStringLiteral("\\bexp=\"((?:\\\\.|[^\"])*)\"")),
            numRe(QStringLiteral("\\bnumchild=\"(\\d+)\"")), valRe(QStringLiteral("\\bvalue=\"((?:\\\\.|[^\"])*)\"")),
            typeRe(QStringLiteral("\\btype=\"((?:\\\\.|[^\"])*)\""));
        for (const QString& t : tuples) {
          const QString childObj = nameRe.match(t).hasMatch() ? detail::unescapeMiValue(nameRe.match(t).captured(1)) : QString();
          const QString exp = expRe.match(t).hasMatch() ? detail::unescapeMiValue(expRe.match(t).captured(1)) : QString();
          const int numCh = numRe.match(t).hasMatch() ? numRe.match(t).captured(1).toInt() : 0;
          QString val = valRe.match(t).hasMatch() ? detail::unescapeMiValue(valRe.match(t).captured(1)) : QString();
          const QString typeStr = typeRe.match(t).hasMatch() ? detail::unescapeMiValue(typeRe.match(t).captured(1)) : QString();
          const bool isAccess = (exp == QLatin1String("public") || exp == QLatin1String("private") || exp == QLatin1String("protected"));
          const bool isBase = (!typeStr.isEmpty() && exp == typeStr && numCh > 0);
          if (isAccess || isBase) {
            if (!childObj.isEmpty() && numCh > 0) {
              const int subTok = m_nextVarQueryToken++;
              m_tokenToVarListQuery[subTok] = qId;
              q.inFlightLists++;
              writeCmd(rankId, QStringLiteral("%1-var-list-children --all-values \"%2\"\n").arg(subTok).arg(childObj));
            }
          } else if (!exp.isEmpty()) {
            if (val.isEmpty() && numCh > 0) val = QStringLiteral("{...}");
            const bool hasCh = (numCh > 0) || val.trimmed() == QLatin1String("{...}") || val.trimmed().startsWith(QLatin1Char('{'));
            q.resolved.append(QVariantMap{{QStringLiteral("name"), exp}, {QStringLiteral("value"), val},
                                          {QStringLiteral("type"), typeStr}, {QStringLiteral("hasChildren"), hasCh},
                                          {QStringLiteral("variablesReference"), 0}});
          }
        }
      }
      if (q.inFlightLists <= 0) {
        auto doneQ = m_varChildQueries.take(qId);
        if (!doneQ.rootVarObj.isEmpty()) writeCmd(rankId, QStringLiteral("-var-delete \"%1\"\n").arg(doneQ.rootVarObj));
        QVariantList deduped;
        QSet<QString> seenNames;
        for (const QVariant& entry : doneQ.resolved) {
          const QString n = entry.toMap().value(QStringLiteral("name")).toString();
          if (!n.isEmpty() && !seenNames.contains(n)) { seenNames.insert(n); deduped.append(entry); }
        }
        emit variableChildrenUpdated(rankId, doneQ.parentExpression, deduped);
      }
      return true;
    }
  }
  return false;
}

/** @brief Creates a temporary GDB varobj for @p parentExpression and lists unwrapped child fields on @p rank. */
void GdbRankCoordinator::requestVariableChildren(int rank, const QString& parentExpression) {
  const QString expr = parentExpression.trimmed();
  if (rank < 0 || rank >= static_cast<int>(m_processes.size()) || !m_processes[rank] ||
      m_processes[rank]->state.currentState != QLatin1String("stopped") ||
      expr.isEmpty() || !detail::validateSafeDebuggerInput(expr)) {
    emit variableChildrenUpdated(rank, parentExpression, {});
    return;
  }
  QString escaped = expr;
  escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\")).replace(QStringLiteral("\""), QStringLiteral("\\\""));
  const int qId = m_nextVarQueryId++;
  const int tok = m_nextVarQueryToken++;
  m_varChildQueries[qId] = PendingVarChildQuery{rank, expr, QString(), 0, {}};
  m_tokenToVarCreateQuery[tok] = qId;
  writeCmd(rank, QStringLiteral("%1-var-create - * \"%2\"\n").arg(tok).arg(escaped));
}

/** @brief Switches @p rank to @p threadId and @p frameLevel, refreshing locals, registers, disassembly, and source line. */
void GdbRankCoordinator::selectStackFrame(int rank, int threadId, int frameLevel) {
  if (rank < 0 || rank >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rank];
  if (!rp || rp->state.currentState != QLatin1String("stopped")) return;
  const int clampedFrame = std::max(0, frameLevel);
  const int prevThread = m_activeThreadIds.value(rank, 1);
  const bool threadChanged = (threadId > 0 && threadId != prevThread);

  if (threadId > 0) {
    m_activeThreadIds[rank] = threadId;
    writeCmd(rank, QStringLiteral("-thread-select %1\n").arg(threadId));
    if (m_rankThreads.contains(rank)) {
      auto& list = m_rankThreads[rank];
      for (auto& th : list) th.isCurrent = (th.threadId == threadId);
      emit threadsUpdated(rank, list);
    }
  }
  writeCmd(rank, QStringLiteral("-stack-select-frame %1\n").arg(clampedFrame));
  writeCmd(rank, QStringLiteral("201-stack-list-variables --simple-values\n"));
  writeCmd(rank, QStringLiteral("-data-list-register-values r\n"));

  if (threadChanged) {
    m_pendingFrameSelect[rank] = clampedFrame;
    writeCmd(rank, QStringLiteral("202-stack-list-frames 0 63\n"));
    return;
  }
  for (const auto& frame : m_rankFrames.value(rank)) {
    if (frame.level == clampedFrame) {
      rp->state.currentFunction = frame.function;
      if (frame.address != 0ULL) {
        const QString hexAddr = QStringLiteral("0x%1").arg(frame.address, 0, 16);
        rp->state.registers[16] = hexAddr;
        writeCmd(rank, QStringLiteral("200-data-disassemble -s %1 -e \"%1 + 64\" -- 0\n").arg(hexAddr));
      }
      if (!frame.file.isEmpty() && frame.line > 0) {
        rp->state.currentFile = frame.file;
        rp->state.currentLine = frame.line;
        emit locationChanged(rank, frame.file, frame.line);
      }
      emit rankStateChanged(rank, rp->state);
      break;
    }
  }
}

}  // namespace gridlock
