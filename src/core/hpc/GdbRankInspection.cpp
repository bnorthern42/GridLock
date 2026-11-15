/**
 * @file GdbRankInspection.cpp
 * @brief GDB/MI3 64-frame backtrace, OS/OpenMP thread list, and caller frame switching handlers.
 *
 * Implements `GdbRankCoordinator::requestStackAndThreads`, `selectStackFrame`, and
 * brace-balanced GDB/MI3 `stack=[...]` / `threads=[...]` record parsers so selecting
 * any stack frame or thread inside an MPI rank updates `SourceCodeView`, `DisassemblyView`,
 * `VariablesDockWidget`, and `RegisterView` to that caller frame's context.
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
 * @param payload Raw debugger input string view to inspect for metacharacters and nesting depth.
 * @return Sanitization outcome detailing truncation, nesting-depth overflow, or metacharacter drop.
 */
__attribute__((weak)) MiSanitizer::SanitizationResult MiSanitizer::sanitize(
    std::string_view payload) {
  SanitizationResult result{"", false, false, false};
  // Enforce the hard 64 KB MI payload ceiling prior to character scanning.
  if (payload.size() > MAX_PAYLOAD_BYTES) {
    payload = payload.substr(0, MAX_PAYLOAD_BYTES);
    result.wasTruncated = true;
  }
  size_t depth = 0;
  bool inQuotes = false;
  bool escaped = false;
  std::ostringstream out;
  // Walk each byte tracking quote state, escape sequences, shell metacharacters, and brace depth.
  for (size_t i = 0; i < payload.size(); ++i) {
    const char c = payload[i];
    if (c == '\0') { result.dropped = true; return result; }
    if (escaped) escaped = false;
    else if (c == '\\') escaped = true;
    else if (c == '"') inQuotes = !inQuotes;
    // Drop unquoted shell chaining, pipe, subshell, and backgrounding operators immediately.
    if (!inQuotes && (c == ';' || c == '|' || c == '&' || c == '`' || c == '$')) {
      result.dropped = true;
      return result;
    }
    // Reject embedded multi-line command smuggling when non-whitespace trails a newline.
    if (c == '\n' || c == '\r') {
      size_t nxt = i + 1;
      while (nxt < payload.size() && (payload[nxt] == ' ' || payload[nxt] == '\t' ||
                                      payload[nxt] == '\r' || payload[nxt] == '\n')) ++nxt;
      if (nxt < payload.size()) { result.dropped = true; return result; }
    }
    // Enforce bounded tuple/list nesting depth against malformed MI inputs.
    if (c == '{' || c == '[') {
      if (++depth > MAX_MI_NESTING_DEPTH) { result.depthExceeded = true; result.dropped = true; return result; }
    } else if ((c == '}' || c == ']') && depth > 0) {
      --depth;
    }
    // Escape non-printable ASCII control characters into `\xHH` representation.
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
namespace {

/**
 * @brief Validates an interactive GDB command or watchpoint expression via `MiSanitizer`,
 *        blocking shell escapes (`!`, `shell`, `pipe`, `;`, `|`, `` ` ``, `$(`) while allowing
 *        register/address operands (`$rsp`, `&var`).
 * @param rawInput Untrusted user command or watchpoint expression from the UI.
 * @return True if @p rawInput passes all shell-escape and `MiSanitizer` checks; false otherwise.
 */
bool validateSafeDebuggerInput(const QString& rawInput) {
  // Reject NUL bytes and embedded CR/LF line breaks that could smuggle trailing MI records.
  if (rawInput.contains(QChar('\0')) || rawInput.contains(QLatin1Char('\n')) ||
      rawInput.contains(QLatin1Char('\r'))) {
    return false;
  }
  const QString trimmed = rawInput.trimmed();
  if (trimmed.isEmpty()) return false;

  // Reject GDB CLI shell-escape prefixes (`!`, `shell`, `pipe`, `make`) and subshell operators.
  static const QRegularExpression shellPrefixRe(
      QStringLiteral("^(?:!|shell(?:\\s|$)|pipe(?:\\s|\\||$)|make(?:\\s|$))"),
      QRegularExpression::CaseInsensitiveOption);
  if (shellPrefixRe.match(trimmed).hasMatch() || trimmed.contains(QStringLiteral("$(")) ||
      trimmed.contains(QStringLiteral("${")) || trimmed.contains(QStringLiteral("&&")) ||
      trimmed.contains(QStringLiteral("||"))) {
    return false;
  }

  // Mask benign GDB `$reg` and C++ `&lvalue` tokens before delegating to `MiSanitizer::sanitize`.
  QString masked = trimmed;
  static const QRegularExpression benignRegAddrRe(QStringLiteral("[$&](?=[A-Za-z0-9_])"));
  masked.replace(benignRegAddrRe, QStringLiteral("_GL_OP_"));
  const std::string utf8 = masked.toStdString();
  const auto res = core::MiSanitizer::sanitize(utf8);
  return !res.dropped && !res.depthExceeded && !res.wasTruncated;
}

/**
 * @brief Extracts top-level `{...}` tuple interiors from a GDB/MI list body while respecting quotes and nested braces.
 * @param listText Inner contents of an MI `[...]` list record.
 * @return Ordered vector of top-level tuple body strings.
 */
QVector<QString> extractTopLevelMiTuples(const QString& listText) {
  QVector<QString> tuples;
  bool inQuotes = false;
  bool escaped = false;
  int braceDepth = 0;
  int tupleStart = -1;

  // Walk character-by-character so nested `frame={...,args=[{...}]}` blocks stay intact.
  for (int i = 0; i < listText.size(); ++i) {
    const QChar ch = listText.at(i);
    if (escaped) { escaped = false; continue; }
    if (inQuotes) {
      if (ch == QLatin1Char('\\')) escaped = true;
      else if (ch == QLatin1Char('"')) inQuotes = false;
      continue;
    }
    if (ch == QLatin1Char('"')) {
      inQuotes = true;
    } else if (ch == QLatin1Char('{')) {
      if (braceDepth == 0) tupleStart = i + 1;
      ++braceDepth;
    } else if (ch == QLatin1Char('}')) {
      --braceDepth;
      if (braceDepth == 0 && tupleStart >= 0) {
        tuples.append(listText.mid(tupleStart, i - tupleStart));
        tupleStart = -1;
      }
    }
  }
  return tuples;
}

/**
 * @brief Extracts the substring enclosed by `[` and its matching `]` after @p keyPrefix.
 * @param line Full GDB/MI response record line.
 * @param keyPrefix Prefix ending in `[` (e.g., `"stack=["` or `"threads=["`).
 * @return Balanced bracket body string, or empty string if @p keyPrefix is absent.
 */
QString extractBracketBody(const QString& line, const QString& keyPrefix) {
  const int startPos = line.indexOf(keyPrefix);
  if (startPos < 0) return QString();
  const int contentStart = startPos + keyPrefix.size();
  bool inQuotes = false;
  bool escaped = false;
  int bracketDepth = 1;

  // Scan forward until the matching closing `]` at depth 0 is reached.
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

/**
 * @brief Unescapes GDB/MI C-style string escapes (`\"`, `\\`, `\n`, `\t`) inside a captured value.
 * @param raw Escaped MI string value.
 * @return Human-readable unescaped string.
 */
QString unescapeMiValue(QString raw) {
  raw.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
  raw.replace(QStringLiteral("\\t"), QStringLiteral("\t"));
  raw.replace(QStringLiteral("\\\""), QStringLiteral("\""));
  raw.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
  return raw;
}

}  // namespace

/**
 * @brief Queries up to 64 call-stack frames (`-stack-list-frames 0 63`) and live threads (`-thread-info`) on @p rank.
 * @param rank Zero-based MPI rank index to inspect.
 */
void GdbRankCoordinator::requestStackAndThreads(int rank) {
  if (rank < 0 || rank >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rank];
  if (!rp || rp->state.currentState != QLatin1String("stopped")) return;

  // Emit cached snapshots immediately for responsive rank switching before live MI replies arrive.
  if (m_rankThreads.contains(rank)) emit threadsUpdated(rank, m_rankThreads.value(rank));
  if (m_rankFrames.contains(rank)) emit stackTraceUpdated(rank, m_rankFrames.value(rank));

  // Query 64-frame backtrace (token 202) and OS/OpenMP thread list (token 203).
  writeCmd(rank, QStringLiteral("202-stack-list-frames 0 63\n"));
  writeCmd(rank, QStringLiteral("203-thread-info\n"));
}

/**
 * @brief Scrubs @p command via `MiSanitizer` and dispatches CLI (`204-interpreter-exec console`) or MI (`205-...`) input.
 * @param rank Target MPI rank index (`< 0` broadcasts across all active ranks).
 * @param command Interactive CLI or MI command string from `GdbConsoleWidget`.
 */
void GdbRankCoordinator::sendDebuggerCommand(int rank, const QString& command) {
  const QString trimmed = command.trimmed();
  if (trimmed.isEmpty()) return;

  // Reject shell-escape or metacharacter-injected commands before touching any GDB pipe.
  if (!validateSafeDebuggerInput(command)) {
    emit debuggerConsoleOutput(
        rank < 0 ? 0 : rank,
        QStringLiteral("Error: Command blocked by MiSanitizer security policy."),
        true);
    return;
  }

  // Dispatch either direct MI (`-data-evaluate-expression ...`) or CLI (`-interpreter-exec console "..."`).
  auto sendToSingleRank = [&](int targetRank) {
    if (targetRank < 0 || targetRank >= static_cast<int>(m_processes.size()) || !m_processes[targetRank]) return;
    // Never write inspection/CLI commands to a running all-stop GDB pipe unless explicitly interrupting.
    const bool isInterrupt = (trimmed == QLatin1String("-exec-interrupt") || trimmed == QLatin1String("interrupt"));
    if (!isInterrupt && m_processes[targetRank]->state.currentState != QLatin1String("stopped")) {
      emit debuggerConsoleOutput(
          targetRank,
          QStringLiteral("Rank %1 is running; pause execution before inspecting state.").arg(targetRank),
          true);
      return;
    }
    if (trimmed.startsWith(QLatin1Char('-'))) {
      // Token 205 tags direct MI evaluation commands so replies route to `debuggerConsoleOutput`.
      writeCmd(targetRank, QStringLiteral("205%1\n").arg(trimmed));
    } else {
      // Token 204 wraps human CLI commands inside GDB's console interpreter safely.
      QString escaped = trimmed;
      escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
      escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
      m_pendingConsoleCmdCount[targetRank] = m_pendingConsoleCmdCount.value(targetRank, 0) + 1;
      writeCmd(targetRank, QStringLiteral("204-interpreter-exec console \"%1\"\n").arg(escaped));
    }
  };

  // Broadcast when `rank < 0`, otherwise route solely to the selected rank process.
  if (rank < 0) {
    for (int r = 0; r < static_cast<int>(m_processes.size()); ++r) sendToSingleRank(r);
  } else {
    sendToSingleRank(rank);
  }
}

/**
 * @brief Arms a hardware data watchpoint (`-break-watch` / `-r` / `-a`) on @p expression across @p rankFilter.
 * @param expression Variable name or memory lvalue expression to watch.
 * @param mode Hardware debug register trigger mode (`Write`, `Read`, or `ReadWrite`).
 * @param rankFilter Target MPI rank (`-1` arms the watchpoint across all ranks).
 */
void GdbRankCoordinator::setDataWatchpoint(const QString& expression,
                                           WatchpointAccessMode mode,
                                           int rankFilter) {
  const QString expr = expression.trimmed();
  if (expr.isEmpty() || !validateSafeDebuggerInput(expr)) return;

  // Replace any duplicate watchpoint entry for `expr` and `rankFilter` in the session cache.
  m_pendingWatchpoints.erase(
      std::remove_if(m_pendingWatchpoints.begin(), m_pendingWatchpoints.end(),
                     [&](const PendingWatchpoint& wp) {
                       return wp.expression == expr && wp.rankFilter == rankFilter;
                     }),
      m_pendingWatchpoints.end());
  m_pendingWatchpoints.push_back({expr, mode, rankFilter});

  // Format GDB/MI `-break-watch` flags (`-r` for read, `-a` for access/read-write, none for write).
  const QString modeFlag = (mode == WatchpointAccessMode::Read)
                               ? QStringLiteral("-r ")
                               : (mode == WatchpointAccessMode::ReadWrite ? QStringLiteral("-a ")
                                                                          : QString());
  const QString miCmd = QStringLiteral("206-break-watch %1%2\n").arg(modeFlag, expr);

  // Dispatch immediately when stopped or queue safely behind a transient stop when running.
  for (int r = 0; r < static_cast<int>(m_processes.size()); ++r) {
    if ((rankFilter == -1 || rankFilter == r) && m_processes[r]) {
      queueOrWriteCmd(r, miCmd);
    }
  }
}

/**
 * @brief Removes any hardware data watchpoint matching @p expression across active GDB rank processes.
 * @param expression Watched variable or memory expression to delete.
 */
void GdbRankCoordinator::removeDataWatchpoint(const QString& expression) {
  const QString expr = expression.trimmed();
  if (expr.isEmpty()) return;
  // Purge matching watchpoint specification from the session-start replay vector.
  m_pendingWatchpoints.erase(
      std::remove_if(m_pendingWatchpoints.begin(), m_pendingWatchpoints.end(),
                     [&](const PendingWatchpoint& wp) { return wp.expression == expr; }),
      m_pendingWatchpoints.end());

  // Issue `-break-delete <id>` for every rank that recorded a live GDB watchpoint number.
  for (int r = 0; r < static_cast<int>(m_processes.size()); ++r) {
    if (m_watchpointIds.contains(r) && m_watchpointIds[r].contains(expr)) {
      const int wpNum = m_watchpointIds[r].take(expr);
      queueOrWriteCmd(r, QStringLiteral("-break-delete %1\n").arg(wpNum));
    }
  }
}

/**
 * @brief Extracts `exp`, `old`, and `new` fields from a GDB/MI `*stopped,reason=".*watchpoint-trigger"` record.
 * @param rankId Zero-based MPI rank index where the hardware watchpoint fired.
 * @param line Raw `*stopped` MI async record line.
 */
void GdbRankCoordinator::handleWatchpointStopRecord(int rankId, const QString& line) {
  // Match write (`wpt`), access (`hw-awpt`), and read (`hw-rwpt`) watchpoint stop tuples.
  static const QRegularExpression expRe(
      QStringLiteral("(?:wpt|hw-awpt|hw-rwpt)=\\{[^}]*exp=\"((?:\\\\.|[^\"])*)\""));
  static const QRegularExpression oldRe(
      QStringLiteral("value=\\{[^}]*old=\"((?:\\\\.|[^\"])*)\""));
  static const QRegularExpression newRe(
      QStringLiteral("value=\\{[^}]*(?:new|value)=\"((?:\\\\.|[^\"])*)\""));

  const auto em = expRe.match(line);
  const QString expr = em.hasMatch() ? unescapeMiValue(em.captured(1)) : QStringLiteral("watchpoint");
  const auto om = oldRe.match(line);
  const QString oldVal = om.hasMatch() ? unescapeMiValue(om.captured(1)) : QString();
  const auto nm = newRe.match(line);
  const QString newVal = nm.hasMatch() ? unescapeMiValue(nm.captured(1)) : QString();
  emit watchpointHit(rankId, expr, oldVal, newVal);
}

/**
 * @brief Parses MI `202..206` stack, thread, console REPL, and watchpoint records for @p rankId.
 * @param rankId Zero-based MPI rank index producing @p line.
 * @param line Trimmed GDB/MI stdout line.
 * @return True if @p line was consumed by an inspection/REPL/watchpoint handler; false otherwise.
 */
bool GdbRankCoordinator::handleMiStackOrThreadRecord(int rankId, const QString& line) {
  // Buffer `~"..."` / `&"..."` console output produced while a `204-interpreter-exec console` command is active.
  if (m_pendingConsoleCmdCount.value(rankId, 0) > 0 &&
      (line.startsWith(QLatin1Char('~')) || line.startsWith(QLatin1Char('&')))) {
    QString payload = line.mid(1).trimmed();
    if (payload.startsWith(QLatin1Char('"')) && payload.endsWith(QLatin1Char('"')) && payload.size() >= 2) {
      payload = unescapeMiValue(payload.mid(1, payload.size() - 2));
    }
    if (!payload.trimmed().isEmpty()) {
      m_consoleReplyBuffer[rankId] += payload;
    }
    return true;
  }

  // Token 204: Complete `204-interpreter-exec console` CLI evaluation and emit `debuggerConsoleOutput`.
  if (line.startsWith(QLatin1String("204^done")) || line.startsWith(QLatin1String("204^error"))) {
    const int rem = std::max(0, m_pendingConsoleCmdCount.value(rankId, 0) - 1);
    m_pendingConsoleCmdCount[rankId] = rem;
    const QString buffered = m_consoleReplyBuffer.take(rankId).trimmed();
    if (line.startsWith(QLatin1String("204^error"))) {
      static const QRegularExpression msgRe(QStringLiteral("msg=\"((?:\\\\.|[^\"])*)\""));
      const auto mm = msgRe.match(line);
      const QString err = mm.hasMatch() ? unescapeMiValue(mm.captured(1)) : buffered;
      emit debuggerConsoleOutput(rankId, QStringLiteral("Error: %1").arg(err), true);
    } else if (!buffered.isEmpty()) {
      emit debuggerConsoleOutput(rankId, buffered, false);
    }
    return true;
  }

  // Token 205: Direct MI command response from GdbConsoleWidget (e.g., `-data-evaluate-expression 21 + 21`).
  if (line.startsWith(QLatin1String("205^done")) || line.startsWith(QLatin1String("205^error"))) {
    static const QRegularExpression valRe(QStringLiteral("value=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression msgRe(QStringLiteral("msg=\"((?:\\\\.|[^\"])*)\""));
    if (line.startsWith(QLatin1String("205^error"))) {
      const auto mm = msgRe.match(line);
      emit debuggerConsoleOutput(
          rankId,
          QStringLiteral("Error: %1").arg(mm.hasMatch() ? unescapeMiValue(mm.captured(1)) : line),
          true);
    } else {
      const auto vm = valRe.match(line);
      const QString out = vm.hasMatch() ? unescapeMiValue(vm.captured(1))
                                        : line.mid(QStringLiteral("205^done").size()).trimmed();
      emit debuggerConsoleOutput(rankId, out.isEmpty() ? QStringLiteral("OK") : out, false);
    }
    return true;
  }

  // Token 206: Record GDB watchpoint number from `-break-watch` creation response.
  if (line.startsWith(QLatin1String("206^done")) || line.startsWith(QLatin1String("206^error"))) {
    if (line.startsWith(QLatin1String("206^done"))) {
      static const QRegularExpression wpRe(
          QStringLiteral("(?:wpt|hw-awpt|hw-rwpt)=\\{number=\"(\\d+)\",exp=\"((?:\\\\.|[^\"])*)\""));
      const auto wm = wpRe.match(line);
      if (wm.hasMatch()) {
        m_watchpointIds[rankId][unescapeMiValue(wm.captured(2))] = wm.captured(1).toInt();
      }
    } else {
      static const QRegularExpression msgRe(QStringLiteral("msg=\"((?:\\\\.|[^\"])*)\""));
      const auto mm = msgRe.match(line);
      if (mm.hasMatch()) emit debuggerConsoleOutput(rankId, QStringLiteral("Watchpoint error: %1").arg(unescapeMiValue(mm.captured(1))), true);
    }
    return true;
  }

  // 1. Parse 64-frame backtrace response (`202^done,stack=[frame={...},...]`).
  if (line.startsWith(QLatin1String("202^done,stack=["))) {
    static const QRegularExpression levelRe(QStringLiteral("level=\"(\\d+)\""));
    static const QRegularExpression addrRe(QStringLiteral("addr=\"(0x[0-9a-fA-F]+)\""));
    static const QRegularExpression funcRe(QStringLiteral("func=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression fullRe(QStringLiteral("fullname=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression fileRe(QStringLiteral("file=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression lineRe(QStringLiteral("line=\"(\\d+)\""));

    // Extract top-level `frame={...}` tuples from the GDB/MI `stack=[...]` payload.
    const QString body = extractBracketBody(line, QStringLiteral("stack=["));
    const QVector<QString> tuples = extractTopLevelMiTuples(body);
    QVector<StackFrameEntry> frames;
    frames.reserve(tuples.size());

    // Materialize a StackFrameEntry for each unwound MI `frame={...}` tuple.
    for (int idx = 0; idx < tuples.size(); ++idx) {
      const QString& t = tuples.at(idx);
      StackFrameEntry entry;
      const auto lm = levelRe.match(t);
      entry.level = lm.hasMatch() ? lm.captured(1).toInt() : idx;
      entry.frameId = entry.level;

      // Parse 64-bit hexadecimal instruction pointer (`addr="0x..."`) for disassembly sync.
      const auto am = addrRe.match(t);
      if (am.hasMatch()) {
        bool ok = false;
        entry.address = am.captured(1).mid(2).toULongLong(&ok, 16);
      }
      // Unescape function identifier (`??` when debug symbols are stripped).
      const auto fnm = funcRe.match(t);
      entry.function = fnm.hasMatch() ? unescapeMiValue(fnm.captured(1)) : QStringLiteral("??");

      // Prefer canonical DWARF `fullname` path when available so `EditorTabManager` opens the exact source file.
      const auto flm = fullRe.match(t);
      const auto fim = fileRe.match(t);
      entry.file = (flm.hasMatch() && !flm.captured(1).isEmpty())
                       ? unescapeMiValue(flm.captured(1))
                       : (fim.hasMatch() ? unescapeMiValue(fim.captured(1)) : QString());

      // Extract 1-based DWARF source line number (`0` when line table entry is absent).
      const auto lnm = lineRe.match(t);
      entry.line = lnm.hasMatch() ? lnm.captured(1).toInt() : 0;
      frames.append(entry);
    }

    // Cache the parsed 64-frame stack snapshot and notify `CallStackDockWidget` + `DeadlockAnalyzer`.
    m_rankFrames[rankId] = frames;
    emit stackTraceUpdated(rankId, frames);

    // Complete deferred location/disassembly sync if a cross-thread frame switch triggered this query.
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

  // 2. Parse OS / OpenMP thread list response (`203^done,threads=[...]` or untokenized `^done,threads=[...]`).
  if (line.startsWith(QLatin1String("203^done,threads=[")) ||
      line.startsWith(QLatin1String("^done,threads=["))) {
    static const QRegularExpression curThreadRe(QStringLiteral("current-thread-id=\"(\\d+)\""));
    static const QRegularExpression idRe(QStringLiteral("\\bid=\"(\\d+)\""));
    static const QRegularExpression nameRe(QStringLiteral("\\bname=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression targetIdRe(QStringLiteral("target-id=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression stateRe(QStringLiteral("\\bstate=\"([^\"]+)\""));
    static const QRegularExpression funcRe(QStringLiteral("func=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression fileRe(QStringLiteral("file=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression lineRe(QStringLiteral("line=\"(\\d+)\""));

    // Record GDB's currently selected thread ID so subsequent frame queries track the active thread.
    const auto curMatch = curThreadRe.match(line);
    const int currentThreadId = curMatch.hasMatch() ? curMatch.captured(1).toInt()
                                                    : m_activeThreadIds.value(rankId, 1);
    if (currentThreadId > 0) m_activeThreadIds[rankId] = currentThreadId;

    const QString body = extractBracketBody(line, QStringLiteral("threads=["));
    const QVector<QString> tuples = extractTopLevelMiTuples(body);
    QVector<ThreadEntry> threads;
    threads.reserve(tuples.size());

    // Extract thread ID, OS/OpenMP name, stopped/running state, and top frame summary for each thread.
    for (int idx = 0; idx < tuples.size(); ++idx) {
      const QString& t = tuples.at(idx);
      // Split thread metadata header from nested `frame={...}` attributes to avoid key collisions.
      const int framePos = t.indexOf(QStringLiteral("frame={"));
      const QString headerPart = (framePos >= 0) ? t.left(framePos) : t;
      const QString framePart = (framePos >= 0) ? t.mid(framePos) : QString();

      ThreadEntry entry;
      const auto idm = idRe.match(headerPart);
      entry.threadId = idm.hasMatch() ? idm.captured(1).toInt() : (idx + 1);

      // Prefer OS thread name (`pthread_setname_np` / OpenMP team name), falling back to LWP target-id.
      const auto nm = nameRe.match(headerPart);
      const auto tm = targetIdRe.match(headerPart);
      entry.name = (nm.hasMatch() && !nm.captured(1).isEmpty())
                       ? unescapeMiValue(nm.captured(1))
                       : (tm.hasMatch() ? unescapeMiValue(tm.captured(1)) : QStringLiteral("main"));

      const auto sm = stateRe.match(t);
      entry.state = sm.hasMatch() ? sm.captured(1) : QStringLiteral("stopped");

      // Format top-of-stack `func @ file:line` summary for the `#threadSelectorCombo` dropdown.
      const auto fnm = funcRe.match(framePart);
      const auto fim = fileRe.match(framePart);
      const auto lnm = lineRe.match(framePart);
      if (fnm.hasMatch()) {
        entry.topFrameSummary = unescapeMiValue(fnm.captured(1));
        if (fim.hasMatch() && lnm.hasMatch()) {
          entry.topFrameSummary += QStringLiteral(" @ %1:%2")
                                       .arg(QFileInfo(unescapeMiValue(fim.captured(1))).fileName(),
                                            lnm.captured(1));
        }
      }
      entry.isCurrent = (entry.threadId == currentThreadId);
      threads.append(entry);
    }

    // Cache parsed thread descriptors per rank and notify CallStackDockWidget.
    m_rankThreads[rankId] = threads;
    emit threadsUpdated(rankId, threads);
    return true;
  }

  // Route tokenized varobj child expansion (`>= 4000`) for lazy `{...}` struct inspection.
  static const QRegularExpression varTokRe(QStringLiteral("^(\\d+)\\^(done|error)(.*)$"));
  if (const auto tm = varTokRe.match(line); tm.hasMatch()) {
    const int tok = tm.captured(1).toInt();
    const bool isErr = (tm.captured(2) == QLatin1String("error"));
    // Step 1: Handle `-var-create` response and either finish immediately or list root children.
    if (m_tokenToVarCreateQuery.contains(tok)) {
      const int qId = m_tokenToVarCreateQuery.take(tok);
      if (!m_varChildQueries.contains(qId)) return true;
      static const QRegularExpression nameRe(QStringLiteral("\\bname=\"((?:\\\\.|[^\"])*)\""));
      static const QRegularExpression numRe(QStringLiteral("\\bnumchild=\"(\\d+)\""));
      const auto nm = nameRe.match(line);
      // Emit empty child list on evaluation error or missing varobj handle.
      if (isErr || !nm.hasMatch()) {
        const auto q = m_varChildQueries.take(qId);
        emit variableChildrenUpdated(rankId, q.parentExpression, {});
        return true;
      }
      auto& q = m_varChildQueries[qId];
      q.rootVarObj = unescapeMiValue(nm.captured(1));
      const int nCh = numRe.match(line).hasMatch() ? numRe.match(line).captured(1).toInt() : 1;
      // Clean up temporary zero-child varobj before returning empty results.
      if (nCh <= 0) {
        const auto doneQ = m_varChildQueries.take(qId);
        writeCmd(rankId, QStringLiteral("-var-delete \"%1\"\n").arg(doneQ.rootVarObj));
        emit variableChildrenUpdated(rankId, doneQ.parentExpression, {});
        return true;
      }
      // Dispatch tokenized `-var-list-children` for the newly allocated root varobj.
      const int listTok = m_nextVarQueryToken++;
      m_tokenToVarListQuery[listTok] = qId;
      q.inFlightLists = 1;
      writeCmd(rankId, QStringLiteral("%1-var-list-children --all-values \"%2\"\n").arg(listTok).arg(q.rootVarObj));
      return true;
    }
    // Step 2: Handle `-var-list-children` responses recursively until all scopes finish.
    if (m_tokenToVarListQuery.contains(tok)) {
      const int qId = m_tokenToVarListQuery.take(tok);
      if (!m_varChildQueries.contains(qId)) return true;
      auto& q = m_varChildQueries[qId];
      q.inFlightLists = std::max(0, q.inFlightLists - 1);
      if (!isErr && line.contains(QLatin1String("children=["))) {
        const QString body = extractBracketBody(line, QStringLiteral("children=["));
        const QVector<QString> tuples = extractTopLevelMiTuples(body);
        static const QRegularExpression nameRe(QStringLiteral("\\bname=\"((?:\\\\.|[^\"])*)\""));
        static const QRegularExpression expRe(QStringLiteral("\\bexp=\"((?:\\\\.|[^\"])*)\""));
        static const QRegularExpression numRe(QStringLiteral("\\bnumchild=\"(\\d+)\""));
        static const QRegularExpression valRe(QStringLiteral("\\bvalue=\"((?:\\\\.|[^\"])*)\""));
        static const QRegularExpression typeRe(QStringLiteral("\\btype=\"((?:\\\\.|[^\"])*)\""));
        // Collect member fields while unwrapping GDB's public/private/protected and base subobjects.
        for (const QString& t : tuples) {
          const QString childObj = nameRe.match(t).hasMatch() ? unescapeMiValue(nameRe.match(t).captured(1)) : QString();
          const QString exp = expRe.match(t).hasMatch() ? unescapeMiValue(expRe.match(t).captured(1)) : QString();
          const int numCh = numRe.match(t).hasMatch() ? numRe.match(t).captured(1).toInt() : 0;
          QString val = valRe.match(t).hasMatch() ? unescapeMiValue(valRe.match(t).captured(1)) : QString();
          const QString typeStr = typeRe.match(t).hasMatch() ? unescapeMiValue(typeRe.match(t).captured(1)) : QString();
          // Expand C++ access visibility nodes and base-class pseudo-nodes transparently.
          const bool isAccess = (exp == QLatin1String("public") || exp == QLatin1String("private") || exp == QLatin1String("protected"));
          const bool isBase = (!typeStr.isEmpty() && exp == typeStr && numCh > 0);
          if ((isAccess || isBase) && !childObj.isEmpty()) {
            const int subTok = m_nextVarQueryToken++;
            m_tokenToVarListQuery[subTok] = qId;
            q.inFlightLists++;
            writeCmd(rankId, QStringLiteral("%1-var-list-children --all-values \"%2\"\n").arg(subTok).arg(childObj));
          } else if (!exp.isEmpty()) {
            // Normalize empty composite summaries to `{...}` so views retain expand indicators.
            if (val.isEmpty() && numCh > 0) val = QStringLiteral("{...}");
            const bool hasCh = (numCh > 0) || val.trimmed() == QLatin1String("{...}") || val.trimmed().startsWith(QLatin1Char('{'));
            q.resolved.append(QVariantMap{{QStringLiteral("name"), exp}, {QStringLiteral("value"), val},
                                          {QStringLiteral("type"), typeStr}, {QStringLiteral("hasChildren"), hasCh},
                                          {QStringLiteral("variablesReference"), 0}});
          }
        }
      }
      // Once every unwrapped sub-query completes, delete the temporary varobj and emit results.
      if (q.inFlightLists <= 0) {
        const auto doneQ = m_varChildQueries.take(qId);
        if (!doneQ.rootVarObj.isEmpty()) writeCmd(rankId, QStringLiteral("-var-delete \"%1\"\n").arg(doneQ.rootVarObj));
        emit variableChildrenUpdated(rankId, doneQ.parentExpression, doneQ.resolved);
      }
      return true;
    }
  }

  return false;
}

/**
 * @brief Creates a temporary GDB varobj for @p parentExpression and lists unwrapped child fields on @p rank.
 */
void GdbRankCoordinator::requestVariableChildren(int rank, const QString& parentExpression) {
  const QString expr = parentExpression.trimmed();
  if (rank < 0 || rank >= static_cast<int>(m_processes.size()) || !m_processes[rank] ||
      m_processes[rank]->state.currentState != QLatin1String("stopped") ||
      expr.isEmpty() || !validateSafeDebuggerInput(expr)) {
    emit variableChildrenUpdated(rank, parentExpression, {});
    return;
  }
  QString escaped = expr;
  escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
  escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
  const int qId = m_nextVarQueryId++;
  const int tok = m_nextVarQueryToken++;
  m_varChildQueries[qId] = PendingVarChildQuery{rank, expr, QString(), 0, {}};
  m_tokenToVarCreateQuery[tok] = qId;
  writeCmd(rank, QStringLiteral("%1-var-create - * \"%2\"\n").arg(tok).arg(escaped));
}

/**
 * @brief Switches @p rank to @p threadId and @p frameLevel, refreshing locals, registers, disassembly, and source line.
 * @param rank Zero-based MPI rank index.
 * @param threadId 1-based GDB thread ID (`<= 0` keeps current thread).
 * @param frameLevel Zero-based call-stack frame level (`0` = innermost frame).
 */
void GdbRankCoordinator::selectStackFrame(int rank, int threadId, int frameLevel) {
  if (rank < 0 || rank >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rank];
  if (!rp || rp->state.currentState != QLatin1String("stopped")) return;

  const int clampedFrame = std::max(0, frameLevel);
  const int prevThread = m_activeThreadIds.value(rank, 1);
  const bool threadChanged = (threadId > 0 && threadId != prevThread);

  // 1. Switch active OS/OpenMP thread when requested and update cached `isCurrent` flags.
  if (threadId > 0) {
    m_activeThreadIds[rank] = threadId;
    writeCmd(rank, QStringLiteral("-thread-select %1\n").arg(threadId));
    if (m_rankThreads.contains(rank)) {
      auto& list = m_rankThreads[rank];
      for (auto& th : list) th.isCurrent = (th.threadId == threadId);
      emit threadsUpdated(rank, list);
    }
  }

  // 2. Select target stack frame and re-query frame locals (`201`) + unwound CPU registers.
  writeCmd(rank, QStringLiteral("-stack-select-frame %1\n").arg(clampedFrame));
  writeCmd(rank, QStringLiteral("201-stack-list-variables --simple-values\n"));
  writeCmd(rank, QStringLiteral("-data-list-register-values r\n"));

  // If switching to a different thread, refresh the 64-frame backtrace for that thread first.
  if (threadChanged) {
    m_pendingFrameSelect[rank] = clampedFrame;
    writeCmd(rank, QStringLiteral("202-stack-list-frames 0 63\n"));
    return;
  }

  // 3. Emit the selected frame's DWARF `file:line` and `$pc` immediately from cached backtrace entries.
  const auto frames = m_rankFrames.value(rank);
  for (const auto& frame : frames) {
    if (frame.level == clampedFrame) {
      rp->state.currentFunction = frame.function;
      // Synchronize register slot 16 (`$rip`/`$pc`) and request disassembly around the caller PC.
      if (frame.address != 0ULL) {
        const QString hexAddr = QStringLiteral("0x%1").arg(frame.address, 0, 16);
        rp->state.registers[16] = hexAddr;
        writeCmd(rank, QStringLiteral("200-data-disassemble -s %1 -e \"%1 + 64\" -- 0\n").arg(hexAddr));
      }
      // Update source file and 1-based line cursor in `SourceCodeView` when DWARF info is present.
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
