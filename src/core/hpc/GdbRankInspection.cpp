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

#include <algorithm>

#include <QFileInfo>
#include <QRegularExpression>

namespace gridlock {
namespace {

/**
 * @brief Extracts top-level `{...}` tuple interiors from a GDB/MI list body while respecting quotes and nested braces.
 * @param listText Substring inside `stack=[...]` or `threads=[...]`.
 * @return Ordered list of top-level brace-delimited tuple contents.
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
    if (escaped) {
      escaped = false;
      continue;
    }
    if (inQuotes) {
      if (ch == QLatin1Char('\\')) {
        escaped = true;
      } else if (ch == QLatin1Char('"')) {
        inQuotes = false;
      }
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
 * @param line Raw GDB/MI result line.
 * @param keyPrefix Prefix ending with `[` (e.g., `"stack=["` or `"threads=["`).
 * @return Bracket interior substring, or empty string if not present.
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
    if (escaped) {
      escaped = false;
      continue;
    }
    if (inQuotes) {
      if (ch == QLatin1Char('\\')) escaped = true;
      else if (ch == QLatin1Char('"')) inQuotes = false;
      continue;
    }
    if (ch == QLatin1Char('"')) {
      inQuotes = true;
    } else if (ch == QLatin1Char('[')) {
      ++bracketDepth;
    } else if (ch == QLatin1Char(']')) {
      if (--bracketDepth == 0) return line.mid(contentStart, i - contentStart);
    }
  }
  return line.mid(contentStart);
}

/**
 * @brief Unescapes GDB/MI C-style string escapes (`\"` and `\\`) inside a captured value.
 */
QString unescapeMiValue(QString raw) {
  raw.replace(QStringLiteral("\\\""), QStringLiteral("\""));
  raw.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
  return raw;
}

}  // namespace

/**
 * @brief Queries up to 64 call-stack frames (`-stack-list-frames 0 63`) and live threads (`-thread-info`) on @p rank.
 * @param rank Zero-based MPI rank index.
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
 * @brief Parses GDB/MI `202^done,stack=[...]` and `203^done,threads=[...]` (or `^done,threads=[...]`) records.
 * @param rankId Zero-based MPI rank index emitting @p line.
 * @param line Single GDB/MI output line.
 * @return True if @p line was consumed as a stack or thread inspection record.
 */
bool GdbRankCoordinator::handleMiStackOrThreadRecord(int rankId, const QString& line) {
  // 1. Parse 64-frame backtrace response (`202^done,stack=[frame={...},...]`).
  if (line.startsWith(QLatin1String("202^done,stack=["))) {
    static const QRegularExpression levelRe(QStringLiteral("level=\"(\\d+)\""));
    static const QRegularExpression addrRe(QStringLiteral("addr=\"(0x[0-9a-fA-F]+)\""));
    static const QRegularExpression funcRe(QStringLiteral("func=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression fullRe(QStringLiteral("fullname=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression fileRe(QStringLiteral("file=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression lineRe(QStringLiteral("line=\"(\\d+)\""));

    const QString body = extractBracketBody(line, QStringLiteral("stack=["));
    const QVector<QString> tuples = extractTopLevelMiTuples(body);
    QVector<StackFrameEntry> frames;
    frames.reserve(tuples.size());

    // Materialize a StackFrameEntry for each unwound MI `frame={...}` tuple.
    for (int idx = 0; idx < tuples.size(); ++idx) {
      const QString& t = tuples.at(idx);
      StackFrameEntry entry;
      // Extract 0-based GDB stack level (`0` = innermost frame, `1+` = caller frames).
      const auto lm = levelRe.match(t);
      entry.level = lm.hasMatch() ? lm.captured(1).toInt() : idx;
      entry.frameId = entry.level;

      // Parse 64-bit hexadecimal instruction pointer (`addr="0x..."`) for disassembly sync.
      const auto am = addrRe.match(t);
      if (am.hasMatch()) {
        bool ok = false;
        entry.address = am.captured(1).mid(2).toULongLong(&ok, 16);
      }
      // Decode C++/MPI function identifier, falling back to `"??"` for stripped symbols.
      const auto fnm = funcRe.match(t);
      entry.function = fnm.hasMatch() ? unescapeMiValue(fnm.captured(1)) : QStringLiteral("??");

      // Prefer canonical DWARF `fullname` path when available so `EditorTabManager` opens the exact source file.
      const auto flm = fullRe.match(t);
      const auto fim = fileRe.match(t);
      entry.file = (flm.hasMatch() && !flm.captured(1).isEmpty())
                       ? unescapeMiValue(flm.captured(1))
                       : (fim.hasMatch() ? unescapeMiValue(fim.captured(1)) : QString());

      // Capture 1-based DWARF source line number (`0` when debug line info is absent).
      const auto lnm = lineRe.match(t);
      entry.line = lnm.hasMatch() ? lnm.captured(1).toInt() : 0;
      frames.append(entry);
    }

    // Cache the parsed 64-frame stack snapshot and notify `CallStackDockWidget`.
    m_rankFrames[rankId] = frames;
    emit stackTraceUpdated(rankId, frames);

    // Complete deferred location/disassembly sync if a cross-thread frame switch triggered this query.
    if (m_pendingFrameSelect.contains(rankId)) {
      const int targetLevel = m_pendingFrameSelect.take(rankId);
      for (const auto& f : frames) {
        if (f.level == targetLevel && rankId >= 0 && rankId < static_cast<int>(m_processes.size()) && m_processes[rankId]) {
          auto& rp = m_processes[rankId];
          rp->state.currentFunction = f.function;
          // Refresh disassembly around the newly selected thread's target frame PC.
          if (f.address != 0ULL) {
            const QString hexAddr = QStringLiteral("0x%1").arg(f.address, 0, 16);
            rp->state.registers[16] = hexAddr;
            writeCmd(rankId, QStringLiteral("200-data-disassemble -s %1 -e \"%1 + 64\" -- 0\n").arg(hexAddr));
          }
          // Broadcast resolved DWARF file:line so SourceCodeView highlights the caller site.
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
    // Compiled regular expressions matching GDB/MI `-thread-info` thread and innermost-frame fields.
    static const QRegularExpression curThreadRe(QStringLiteral("current-thread-id=\"(\\d+)\""));
    static const QRegularExpression idRe(QStringLiteral("\\bid=\"(\\d+)\""));
    static const QRegularExpression nameRe(QStringLiteral("\\bname=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression targetIdRe(QStringLiteral("target-id=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression stateRe(QStringLiteral("\\bstate=\"([^\"]+)\""));
    static const QRegularExpression funcRe(QStringLiteral("func=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression fileRe(QStringLiteral("file=\"((?:\\\\.|[^\"])*)\""));
    static const QRegularExpression lineRe(QStringLiteral("line=\"(\\d+)\""));

    // Resolve `current-thread-id` reported by GDB so the active OS/OpenMP thread is pre-selected.
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
      // Split thread metadata attributes ahead of `frame={` from the nested innermost frame tuple.
      const int framePos = t.indexOf(QStringLiteral("frame={"));
      const QString headerPart = (framePos >= 0) ? t.left(framePos) : t;
      const QString framePart = (framePos >= 0) ? t.mid(framePos) : QString();

      ThreadEntry entry;
      const auto idm = idRe.match(headerPart);
      entry.threadId = idm.hasMatch() ? idm.captured(1).toInt() : (idx + 1);

      // Prefer explicit OS/OpenMP thread `name`, falling back to `target-id` (e.g., LWP) or `"main"`.
      const auto nm = nameRe.match(headerPart);
      const auto tm = targetIdRe.match(headerPart);
      entry.name = (nm.hasMatch() && !nm.captured(1).isEmpty())
                       ? unescapeMiValue(nm.captured(1))
                       : (tm.hasMatch() ? unescapeMiValue(tm.captured(1)) : QStringLiteral("main"));

      // Record thread execution state (`"stopped"` or `"running"`).
      const auto sm = stateRe.match(t);
      entry.state = sm.hasMatch() ? sm.captured(1) : QStringLiteral("stopped");

      // Summarize the thread's innermost frame (`func @ file:line`) when present in `-thread-info`.
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

    // Cache thread descriptors for `rankId` and notify `CallStackDockWidget`.
    m_rankThreads[rankId] = threads;
    emit threadsUpdated(rankId, threads);
    return true;
  }

  return false;
}

/**
 * @brief Switches @p rank to @p threadId and @p frameLevel, refreshing locals, registers, disassembly, and source line.
 * @param rank Zero-based MPI rank index.
 * @param threadId One-based GDB thread ID (`<= 0` keeps the rank's current thread).
 * @param frameLevel Zero-based stack frame level (`0` = innermost frame, `1+` = caller frames).
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
