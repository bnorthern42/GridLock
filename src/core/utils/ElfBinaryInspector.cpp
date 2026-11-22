/**
 * @file ElfBinaryInspector.cpp
 * @brief DWARF-annotated symbol/range disassembly and `/proc` process memory/CPU telemetry.
 *
 * Disassembles ELF64 symbols and instruction ranges via `objdump -d -l -C -M intel`
 * with a deterministic built-in x86_64 fallback, and parses Linux `/proc/<pid>/smaps`,
 * `/proc/meminfo`, and `/proc/stat` into per-rank `ProcessMemoryTelemetry` snapshots.
 */

#include "ElfBinaryInspector.hpp"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace gridlock {
namespace core {

/**
 * @brief Sets an optional C/C++ source filename basename filter for DWARF line table extraction.
 * @param sourceFileName Basename or path of primary source file (e.g., "deadlock_ring.cpp").
 */
void ElfBinaryInspector::setSourceFileFilter(const QString& sourceFileName) {
    const QString cleaned = sourceFileName.trimmed().isEmpty()
                                ? QString()
                                : QFileInfo(sourceFileName.trimmed()).fileName();
    if (m_sourceFileFilter != cleaned) {
        m_sourceFileFilter = cleaned;
        m_cachedDisasmText.clear();
        m_cachedDisasmStart = 0;
        m_cachedDisasmSize = 0;
    }
}

/**
 * @brief Disassembles a named function symbol using objdump -d -l -C -M intel and extracts DWARF lines.
 * @param symbolName Function symbol name (e.g., "main" or "sym.main").
 * @param sourceFilter Optional basename filter for DWARF file entries.
 * @param outLineToAddr Optional output map of 1-based source line -> first instruction address.
 * @param outAddrToLine Optional output map of instruction address -> 1-based source line.
 * @return Formatted multiline disassembly text suitable for DisassemblyView.
 */
QString ElfBinaryInspector::disassembleSymbol(const QString& symbolName,
                                              const QString& sourceFilter,
                                              QMap<int, uint64_t>* outLineToAddr,
                                              QMap<uint64_t, int>* outAddrToLine) const {
    if (!m_valid) return {};
    QString cleanSym = symbolName.trimmed();
    if (cleanSym.startsWith(QStringLiteral("sym."))) {
        cleanSym = cleanSym.mid(4);
    }
    if (!sourceFilter.trimmed().isEmpty()) {
        const_cast<ElfBinaryInspector*>(this)->setSourceFileFilter(sourceFilter);
    }

    // Locate target symbol in parsed ELF symbol table.
    uint64_t symAddr = 0;
    uint64_t symSize = 0;
    for (const auto& s : m_symbols) {
        if (s.address != 0 && s.name == cleanSym) {
            symAddr = s.address;
            symSize = (s.size > 0) ? s.size : 256;
            break;
        }
    }
    if (symAddr == 0) return {};

    const QString disasm = disassembleRange(symAddr, symSize, cleanSym);
    if (outLineToAddr) *outLineToAddr = m_dwarfLineToAddr;
    if (outAddrToLine) *outAddrToLine = m_dwarfAddrToLine;
    return disasm;
}

/**
 * @brief Disassembles an instruction range into formatted assembly lines with DWARF line markers.
 *
 * Expands any address inside a known ELF function symbol to the full enclosing function
 * boundaries so seeking mid-function preserves the complete procedure listing, invokes
 * `objdump -d -l -C -M intel --no-show-raw-insn`, filters DWARF `<file>:<line>` entries
 * against the primary source file basename, and normalizes branch target hex literals.
 *
 * @param startAddr Starting virtual memory address.
 * @param size Byte size of range to disassemble.
 * @param symbolName Enclosing symbol name or placeholder.
 * @return Formatted assembly text.
 */
QString ElfBinaryInspector::disassembleRange(uint64_t startAddr, uint64_t size,
                                             const QString& symbolName) const {
    if (!m_valid || size == 0) {
        return {};
    }

    // Expand mid-function requests to full enclosing ELF function symbol bounds.
    uint64_t effectiveStart = startAddr;
    uint64_t effectiveSize = size;
    QString effectiveSymName = symbolName;
    for (const auto& s : m_symbols) {
        if (s.isFunction && s.size > 0 && startAddr >= s.address && startAddr < (s.address + s.size)) {
            effectiveStart = s.address;
            effectiveSize = s.size;
            if (effectiveSymName.isEmpty()) effectiveSymName = s.name;
            break;
        }
    }

    // Return cached function disassembly when bounds and patch generation are unchanged.
    if (effectiveStart == m_cachedDisasmStart &&
        effectiveSize == m_cachedDisasmSize &&
        m_patchedByteCount == m_cachedPatchGen &&
        !m_cachedDisasmText.isEmpty()) {
        return m_cachedDisasmText;
    }

    // Step 1: Invoke objdump with C++ demangling (-C) and DWARF line numbers (-l) if binary exists on disk.
    uint64_t stopAddr = 0;
    const bool validRange = !__builtin_add_overflow(effectiveStart, effectiveSize, &stopAddr);
    if (validRange && !m_filePath.isEmpty() && !m_filePath.contains(QChar('\0')) &&
        QFileInfo::exists(m_filePath)) {
        QProcess proc;
        const QStringList args = {
            QStringLiteral("-d"),
            QStringLiteral("-l"),
            QStringLiteral("-C"),
            QStringLiteral("-M"),
            QStringLiteral("intel"),
            QStringLiteral("--no-show-raw-insn"),
            QStringLiteral("--start-address=0x%1").arg(effectiveStart, 0, 16),
            QStringLiteral("--stop-address=0x%1").arg(stopAddr, 0, 16),
            QStringLiteral("--"),
            m_filePath
        };
        proc.start(QStringLiteral("objdump"), args);
        if (proc.waitForFinished(2500) &&
            proc.exitStatus() == QProcess::NormalExit &&
            proc.exitCode() == 0) {
            const QString out = QString::fromUtf8(proc.readAllStandardOutput());
            const QStringList lines = out.split('\n');
            QString formatted;
            QString currentSym = effectiveSymName.isEmpty() ? QStringLiteral("entry") : effectiveSymName;
            // Match full <symbol> header including nested C++ template angle brackets (<...>).
            static const QRegularExpression symHeaderRegex(QStringLiteral("^[0-9a-fA-F]+\\s+<(.+)>:$"));
            static const QRegularExpression insnRegex(QStringLiteral("^\\s*([0-9a-fA-F]+):\\s*(.*)$"));
            static const QRegularExpression dwarfRegex(QStringLiteral("^(/?[^:\n]+):(\\d+)"));
            static const QRegularExpression branchHexRegex(QStringLiteral(
                "^((?:call|jmp|je|jz|jne|jnz|jg|jge|jl|jle|ja|jae|jb|jbe|js|jns|jo|jno)q?\\s+)([0-9a-fA-F]{4,16})(\\b.*)$"));
            static const QRegularExpression gluedMnemRe(QStringLiteral(
                "(\\))>(x?(?:callq?|jmpq?|je|jz|jne|jnz|jg|jge|jl|jle|ja|jae|jb|jbe|js|jns|jo|jno))\\b"));

            int currentDwarfLine = -1;
            QMap<int, uint64_t> parsedLineToAddr;
            QMap<uint64_t, int> parsedAddrToLine;

            // Parse objdump lines, filtering DWARF file annotations to primary target source file.
            for (const QString& line : lines) {
                const QString trimmedLine = line.trimmed();
                const auto symMatch = symHeaderRegex.match(trimmedLine);
                if (symMatch.hasMatch()) {
                    currentSym = symMatch.captured(1);
                    continue;
                }
                const auto dwarfMatch = dwarfRegex.match(trimmedLine);
                if (dwarfMatch.hasMatch()) {
                    const QString dwarfPath = dwarfMatch.captured(1).trimmed();
                    const QString dwarfBase = QFileInfo(dwarfPath).fileName();
                    const int parsedLine = dwarfMatch.captured(2).toInt();
                    if (!m_sourceFileFilter.isEmpty()) {
                        currentDwarfLine = (dwarfBase.compare(m_sourceFileFilter, Qt::CaseInsensitive) == 0)
                                               ? parsedLine
                                               : -1;
                    } else {
                        currentDwarfLine = !dwarfPath.startsWith(QStringLiteral("/usr/include"))
                                               ? parsedLine
                                               : -1;
                    }
                    continue;
                }
                const auto insnMatch = insnRegex.match(line);
                if (!insnMatch.hasMatch()) continue;

                bool ok = false;
                const uint64_t addr = insnMatch.captured(1).toULongLong(&ok, 16);
                if (!ok) continue;

                QString body = insnMatch.captured(2).trimmed();
                if (body.isEmpty()) continue;
                // Separate any demangled template closure glued onto a mnemonic (e.g. "const&)>call").
                body.replace(gluedMnemRe, QStringLiteral("\\1>: \\2"));

                // Prefix immediate branch/call hex targets with 0x for CFG & Xref parser compatibility.
                const auto brMatch = branchHexRegex.match(body);
                if (brMatch.hasMatch()) {
                    bool brOk = false;
                    const uint64_t targetAddr = brMatch.captured(2).toULongLong(&brOk, 16);
                    if (brOk) {
                        body = QStringLiteral("%10x%2%3")
                                   .arg(brMatch.captured(1))
                                   .arg(targetAddr, 8, 16, QChar('0'))
                                   .arg(brMatch.captured(3));
                    }
                }

                const uint64_t offset = (addr >= effectiveStart) ? (addr - effectiveStart) : 0;
                if (currentDwarfLine > 0) {
                    if (!parsedLineToAddr.contains(currentDwarfLine)) {
                        parsedLineToAddr.insert(currentDwarfLine, addr);
                    }
                    parsedAddrToLine.insert(addr, currentDwarfLine);
                    formatted += QStringLiteral("0x%1 <%2+%3>: %4 ; line %5\n")
                                     .arg(addr, 8, 16, QChar('0'))
                                     .arg(currentSym)
                                     .arg(offset)
                                     .arg(body)
                                     .arg(currentDwarfLine);
                } else {
                    formatted += QStringLiteral("0x%1 <%2+%3>: %4\n")
                                     .arg(addr, 8, 16, QChar('0'))
                                     .arg(currentSym)
                                     .arg(offset)
                                     .arg(body);
                }
            }
            const QString cleanFormatted = formatted.trimmed();
            if (!cleanFormatted.isEmpty()) {
                m_dwarfLineToAddr = parsedLineToAddr;
                m_dwarfAddrToLine = parsedAddrToLine;
                m_cachedDisasmStart = effectiveStart;
                m_cachedDisasmSize = effectiveSize;
                m_cachedPatchGen = m_patchedByteCount;
                m_cachedDisasmText = cleanFormatted;
                return cleanFormatted;
            }
        }
    }

    // Step 2: Fallback to built-in deterministic x86_64 disassembler
    const QByteArray rawBytes = readVirtualMemory(effectiveStart, effectiveSize);
    if (rawBytes.isEmpty()) {
        return {};
    }

    const QString effectiveSym = effectiveSymName.isEmpty() ? QStringLiteral("entry") : effectiveSymName;
    QString result;
    size_t i = 0;
    // Iterate raw bytes and match common x86_64 instruction opcodes
    while (i < static_cast<size_t>(rawBytes.size())) {
        const uint64_t addr = effectiveStart + i;
        const unsigned char b = static_cast<unsigned char>(rawBytes[static_cast<qsizetype>(i)]);
        QString mnemonic;
        size_t insnLen = 1;

        if (b == 0x55) {
            mnemonic = QStringLiteral("push   rbp");
        } else if (b == 0x48 && i + 2 < static_cast<size_t>(rawBytes.size()) &&
                   static_cast<unsigned char>(rawBytes[static_cast<qsizetype>(i + 1)]) == 0x89 &&
                   static_cast<unsigned char>(rawBytes[static_cast<qsizetype>(i + 2)]) == 0xe5) {
            mnemonic = QStringLiteral("mov    rbp, rsp");
            insnLen = 3;
        } else if (b == 0x48 && i + 2 < static_cast<size_t>(rawBytes.size()) &&
                   static_cast<unsigned char>(rawBytes[static_cast<qsizetype>(i + 1)]) == 0x83 &&
                   static_cast<unsigned char>(rawBytes[static_cast<qsizetype>(i + 2)]) == 0xec) {
            mnemonic = QStringLiteral("sub    rsp, 0x20");
            insnLen = 4;
        } else if (b == 0xc3) {
            mnemonic = QStringLiteral("ret");
        } else if (b == 0x90) {
            mnemonic = QStringLiteral("nop");
        } else if (b == 0x5d) {
            mnemonic = QStringLiteral("pop    rbp");
        } else if (b == 0x31 && i + 1 < static_cast<size_t>(rawBytes.size()) &&
                   static_cast<unsigned char>(rawBytes[static_cast<qsizetype>(i + 1)]) == 0xc0) {
            mnemonic = QStringLiteral("xor    eax, eax");
            insnLen = 2;
        } else if (b == 0x0f && i + 3 < static_cast<size_t>(rawBytes.size()) &&
                   static_cast<unsigned char>(rawBytes[static_cast<qsizetype>(i + 1)]) == 0x1e &&
                   static_cast<unsigned char>(rawBytes[static_cast<qsizetype>(i + 2)]) == 0xfa) {
            mnemonic = QStringLiteral("endbr64");
            insnLen = 4;
        } else {
            mnemonic = QStringLiteral(".byte  0x%1").arg(static_cast<unsigned int>(b), 2, 16, QChar('0'));
        }

        result += QStringLiteral("0x%1 <%2+%3>: %4\n")
                      .arg(addr, 8, 16, QChar('0'))
                      .arg(effectiveSym)
                      .arg(i)
                      .arg(mnemonic);
        i += insnLen;
    }

    return result.trimmed();
}

/**
 * @brief Parses Linux /proc/<pid>/smaps (or /proc/<pid>/status) and /proc/meminfo text into telemetry.
 */
ProcessMemoryTelemetry ElfBinaryInspector::parseProcMemoryTelemetry(const QString& statusOrSmapsText,
                                                                    quint64 currentRsp,
                                                                    quint64 stackTopAddr,
                                                                    const QString& meminfoText) {
    ProcessMemoryTelemetry mem;
    // Compute active stack bytes when stackTopAddr and currentRsp bracket a valid frame depth (< 64 MB).
    if (currentRsp > 0 && stackTopAddr > currentRsp && (stackTopAddr - currentRsp) < (64ULL << 20)) {
        mem.activeStackBytes = stackTopAddr - currentRsp;
    }

    // Parse smaps ([heap] / [stack] blocks) or status (VmData / VmStk lines) line by line.
    enum class SmapsSection { None, Heap, Stack };
    SmapsSection currentSec = SmapsSection::None;
    static const QRegularExpression kKvNumRe(QStringLiteral("^([A-Za-z_]+):\\s*(\\d+)"));
    for (const QString& rawLine : statusOrSmapsText.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;
        if (line.contains(QStringLiteral("[heap]"))) { currentSec = SmapsSection::Heap; continue; }
        if (line.contains(QStringLiteral("[stack"))) { currentSec = SmapsSection::Stack; continue; }
        if (line.contains(QLatin1Char('-')) && line.indexOf(QLatin1Char('-')) < 20 && !line.contains(QLatin1Char(':'))) {
            currentSec = SmapsSection::None;
            continue;
        }
        const auto m = kKvNumRe.match(line);
        if (!m.hasMatch()) continue;
        const QString key = m.captured(1);
        const quint64 valKb = m.captured(2).toULongLong();
        if (key == QStringLiteral("VmStk")) mem.stackKb = valKb;
        else if (key == QStringLiteral("VmData")) mem.heapKb = valKb;
        else if (key == QStringLiteral("Size") && currentSec == SmapsSection::Heap) mem.heapKb += valKb;
        else if (key == QStringLiteral("Rss") && currentSec == SmapsSection::Heap) mem.heapRssKb += valKb;
        else if (key == QStringLiteral("Size") && currentSec == SmapsSection::Stack) mem.stackKb += valKb;
        else if (key == QStringLiteral("Rss") && currentSec == SmapsSection::Stack) mem.stackRssKb += valKb;
    }

    // Parse system-wide MemTotal and MemAvailable from meminfoText or /proc/meminfo fallback.
    QString effectiveMeminfo = meminfoText;
    if (effectiveMeminfo.isEmpty()) {
        QFile f(QStringLiteral("/proc/meminfo"));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) effectiveMeminfo = QString::fromUtf8(f.readAll());
    }
    quint64 memAvailKb = 0, memFreeKb = 0;
    for (const QString& rawLine : effectiveMeminfo.split(QLatin1Char('\n'))) {
        const auto m = kKvNumRe.match(rawLine.trimmed());
        if (!m.hasMatch()) continue;
        const QString key = m.captured(1);
        const quint64 valKb = m.captured(2).toULongLong();
        if (key == QStringLiteral("MemTotal")) mem.sysMemTotalKb = valKb;
        else if (key == QStringLiteral("MemAvailable")) memAvailKb = valKb;
        else if (key == QStringLiteral("MemFree")) memFreeKb = valKb;
    }
    const quint64 freeOrAvail = (memAvailKb > 0) ? memAvailKb : memFreeKb;
    if (mem.sysMemTotalKb > freeOrAvail) mem.sysMemUsedKb = mem.sysMemTotalKb - freeOrAvail;
    return mem;
}

/**
 * @brief Resolves the upper `[stack]` VMA address from `/proc/<pid>/maps` for active stack depth math.
 */
quint64 ElfBinaryInspector::queryStackTopAddress(qint64 pid) {
    if (pid <= 0) return 0;
    QFile mapsFile(QStringLiteral("/proc/%1/maps").arg(pid));
    if (!mapsFile.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    // Read `/proc/<pid>/maps` via `readAll()` (`st_size == 0` on `/proc` makes `atEnd()` true at pos 0).
    static const QRegularExpression stackMapRe(QStringLiteral("^([0-9a-fA-F]+)-([0-9a-fA-F]+)\\s+.*\\[stack\\]"));
    for (const QByteArray& rawLine : mapsFile.readAll().split('\n')) {
        const auto m = stackMapRe.match(QString::fromUtf8(rawLine).trimmed());
        if (m.hasMatch()) return m.captured(2).toULongLong(nullptr, 16);
    }
    return 0;
}

/**
 * @brief Queries live per-rank process memory (/proc/<pid>/smaps or status) and host RAM/CPU telemetry.
 */
ProcessMemoryTelemetry ElfBinaryInspector::queryProcessMemory(qint64 pid,
                                                              quint64 currentRsp,
                                                              quint64 stackTopAddr) {
    // Auto-resolve `[stack]` top boundary only for live inferior PIDs (`pid > 0`).
    if (pid > 0 && currentRsp > 0 && stackTopAddr == 0) stackTopAddr = queryStackTopAddress(pid);
    QString procText;
    // Never read `/proc/self` when idle (`pid <= 0`): only inspect a real target PID's procfs files.
    if (pid > 0) {
        for (const QString& suffix : {QStringLiteral("/smaps"), QStringLiteral("/status")}) {
            QFile f(QStringLiteral("/proc/%1%2").arg(pid).arg(suffix));
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                procText = QString::fromUtf8(f.readAll());
                if (!procText.isEmpty()) break;
            }
        }
    }
    ProcessMemoryTelemetry mem = parseProcMemoryTelemetry(procText, (pid > 0) ? currentRsp : 0, (pid > 0) ? stackTopAddr : 0);

    // Sample aggregate host CPU utilization delta from /proc/stat across consecutive queries.
    QFile statFile(QStringLiteral("/proc/stat"));
    if (statFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString firstLine = QString::fromUtf8(statFile.readLine()).trimmed();
        const QStringList parts = firstLine.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() >= 5 && parts[0] == QStringLiteral("cpu")) {
            quint64 totalTicks = 0, idleTicks = 0;
            for (int i = 1; i < parts.size(); ++i) {
                const quint64 v = parts[i].toULongLong();
                totalTicks += v;
                if (i == 4 || i == 5) idleTicks += v;
            }
            static quint64 s_prevTotal = 0, s_prevIdle = 0;
            const quint64 dTotal = (s_prevTotal > 0 && totalTicks > s_prevTotal) ? (totalTicks - s_prevTotal) : totalTicks;
            const quint64 dIdle = (s_prevTotal > 0 && idleTicks >= s_prevIdle) ? (idleTicks - s_prevIdle) : idleTicks;
            s_prevTotal = totalTicks;
            s_prevIdle = idleTicks;
            if (dTotal > 0 && dTotal >= dIdle) {
                mem.sysCpuPercent = qBound(0.0, 100.0 * static_cast<double>(dTotal - dIdle) / static_cast<double>(dTotal), 100.0);
            }
        }
    }
    return mem;
}

/**
 * @brief Formats a ProcessMemoryTelemetry snapshot into a compact status bar ribbon summary.
 */
QString ElfBinaryInspector::formatStatusBarSummary(const ProcessMemoryTelemetry& mem,
                                                   const QString& displayFormat) {
    auto fmtKb = [](quint64 kb) -> QString {
        if (kb >= 1024) return QStringLiteral("%1 MB").arg(static_cast<double>(kb) / 1024.0, 0, 'f', 1);
        return QStringLiteral("%1 KB").arg(kb);
    };
    const double usedGb = static_cast<double>(mem.sysMemUsedKb) / (1024.0 * 1024.0);
    const double totalGb = static_cast<double>(mem.sysMemTotalKb) / (1024.0 * 1024.0);
    const double ramPct = (mem.sysMemTotalKb > 0)
        ? (100.0 * static_cast<double>(mem.sysMemUsedKb) / static_cast<double>(mem.sysMemTotalKb))
        : 0.0;
    const QString fmt = displayFormat.trimmed().toLower();
    QString sysStr;
    if (fmt == QStringLiteral("percent")) {
        sysStr = QStringLiteral("RAM: %1% | CPU: %2%").arg(ramPct, 0, 'f', 1).arg(mem.sysCpuPercent, 0, 'f', 1);
    } else if (fmt == QStringLiteral("both")) {
        sysStr = QStringLiteral("RAM: %1 / %2 GB (%3%) | CPU: %4%")
                     .arg(usedGb, 0, 'f', 1).arg(totalGb, 0, 'f', 1).arg(qRound(ramPct)).arg(mem.sysCpuPercent, 0, 'f', 1);
    } else {
        sysStr = QStringLiteral("RAM: %1 / %2 GB | CPU: %3%")
                     .arg(usedGb, 0, 'f', 1).arg(totalGb, 0, 'f', 1).arg(mem.sysCpuPercent, 0, 'f', 1);
    }
    // Emit honest idle status when no live inferior process memory is reported.
    if (mem.stackKb == 0 && mem.heapKb == 0 && mem.activeStackBytes == 0) {
        return QStringLiteral("TARGET: IDLE | HOST %1").arg(sysStr);
    }
    QString stackStr = fmtKb(mem.stackKb);
    if (mem.activeStackBytes > 0) {
        const QString actStr = (mem.activeStackBytes >= 1024)
            ? QStringLiteral("%1 KB").arg(mem.activeStackBytes / 1024)
            : QStringLiteral("%1 B").arg(mem.activeStackBytes);
        stackStr += QStringLiteral(" (%1 active)").arg(actStr);
    }
    return QStringLiteral("STACK: %1 | HEAP: %2 | %3").arg(stackStr, fmtKb(mem.heapKb), sysStr);
}

}  // namespace core
}  // namespace gridlock
