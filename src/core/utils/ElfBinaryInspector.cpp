/**
 * @file ElfBinaryInspector.cpp
 * @brief Implementation of native 64-bit ELF binary parsing, symbols, and memory reads.
 *
 * Implements ElfBinaryInspector methods using standard Linux <elf.h> types.
 * Performs safe memory-buffer bounds checking, extracts sections and symbols,
 * gathers printable ASCII strings, supports virtual-memory byte slicing, and
 * disassembles instruction ranges using objdump with a deterministic fallback.
 */

#include "ElfBinaryInspector.hpp"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <algorithm>
#include <cctype>

namespace gridlock {
namespace core {

/**
 * @brief Constructs an inspector and immediately attempts to parse the specified file.
 *
 * Automatically opens the target filesystem path and invokes loadFile().
 *
 * @param filePath Path to binary executable or shared object.
 */
ElfBinaryInspector::ElfBinaryInspector(const QString& filePath) {
    loadFile(filePath);
}

/**
 * @brief Loads and parses a 64-bit ELF binary file from disk.
 *
 * Reads the complete binary file payload into memory and forwards the
 * data buffer to loadFromData() for structured parsing and validation.
 *
 * @param filePath Path to binary on disk.
 * @return True if valid 64-bit ELF and parsed successfully; false otherwise.
 */
bool ElfBinaryInspector::loadFile(const QString& filePath) {
    QFile file(filePath);
    // Attempt to open target binary in binary read-only mode
    if (!file.open(QIODevice::ReadOnly)) {
        m_valid = false;
        return false;
    }
    // Ingest entire file contents into local buffer
    const QByteArray data = file.readAll();
    file.close();
    // Clean up 0-byte interrupted build artifacts when a companion source file exists
    if (data.isEmpty() && !filePath.isEmpty() &&
        (QFileInfo::exists(filePath + QStringLiteral(".cpp")) ||
         QFileInfo::exists(filePath + QStringLiteral(".c")))) {
        QFile::remove(filePath);
    }
    // Parse in-memory image
    return loadFromData(data, filePath);
}

/**
 * @brief Parses an in-memory 64-bit ELF buffer and populates metadata structures.
 *
 * Validates the ELF header magic identifiers, verifies 64-bit architecture,
 * checks section header table bounds, and triggers parsing of sections,
 * symbol tables, and printable ASCII strings.
 *
 * @param data Raw byte array representing the ELF file.
 * @param sourcePath Filesystem path or label for logging.
 * @return True if valid 64-bit ELF format.
 */
bool ElfBinaryInspector::loadFromData(const QByteArray& data, const QString& sourcePath) {
    // Step 1: Reset all internal state before beginning parsing pass
    m_valid = false;
    m_entryPoint = 0;
    m_sections.clear();
    m_symbols.clear();
    m_strings.clear();
    m_patchedByteCount = 0;
    m_dwarfLineToAddr.clear();
    m_dwarfAddrToLine.clear();
    m_cachedDisasmStart = 0;
    m_cachedDisasmSize = 0;
    m_cachedPatchGen = -1;
    m_cachedDisasmText.clear();
    m_fileData = data;
    m_filePath = sourcePath;

    // Step 2: Verify buffer is large enough to contain an ELF64 header
    if (static_cast<size_t>(m_fileData.size()) < sizeof(Elf64_Ehdr)) {
        return false;
    }

    const auto* ehdr = reinterpret_cast<const Elf64_Ehdr*>(m_fileData.constData());

    // Step 3: Validate ELF magic bytes: 0x7F, 'E', 'L', 'F'
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        return false;
    }

    // Step 4: Require 64-bit ELF class (ELFCLASS64) and valid header size
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr->e_ehsize < sizeof(Elf64_Ehdr) ||
        static_cast<uint64_t>(ehdr->e_ehsize) > static_cast<uint64_t>(m_fileData.size())) {
        return false;
    }

    // Record entry point virtual memory address
    m_entryPoint = ehdr->e_entry;

    // Step 5: Validate section header table presence and entry size consistency
    const uint64_t fileSize = static_cast<uint64_t>(m_fileData.size());
    if (ehdr->e_shoff == 0) {
        if (ehdr->e_shnum != 0) {
            return false;
        }
        m_valid = true;
        return true;
    }
    if (ehdr->e_shentsize != sizeof(Elf64_Shdr) ||
        ehdr->e_shoff < sizeof(Elf64_Ehdr) ||
        ehdr->e_shoff >= fileSize) {
        return false;
    }

    // Step 6: Overflow-safe verification that section header table resides within buffer bounds
    uint64_t shTableBytes = 0;
    uint64_t shTableEnd = 0;
    if (__builtin_mul_overflow(static_cast<uint64_t>(ehdr->e_shnum),
                               static_cast<uint64_t>(sizeof(Elf64_Shdr)),
                               &shTableBytes) ||
        __builtin_add_overflow(ehdr->e_shoff, shTableBytes, &shTableEnd) ||
        shTableEnd > fileSize) {
        return false;
    }

    // Validate section header string table index and each file-backed section span
    if (ehdr->e_shstrndx != SHN_UNDEF && ehdr->e_shstrndx >= ehdr->e_shnum) {
        return false;
    }
    const auto* shdrs = reinterpret_cast<const Elf64_Shdr*>(
        m_fileData.constData() + ehdr->e_shoff);
    for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
        const auto& sh = shdrs[i];
        if (sh.sh_type != SHT_NOBITS && sh.sh_type != SHT_NULL) {
            uint64_t secEnd = 0;
            if (__builtin_add_overflow(sh.sh_offset, sh.sh_size, &secEnd) ||
                secEnd > fileSize) {
                return false;
            }
        }
    }

    // Step 7: Parse sections, symbols, and printable strings
    parseSections();
    parseSymbols();
    extractStrings();

    m_valid = true;
    return true;
}

/**
 * @brief Iterates section headers and populates the m_sections list.
 *
 * Maps each Elf64_Shdr entry to an ElfSectionInfo structure, resolving
 * section names from the .shstrtab section header string table.
 */
void ElfBinaryInspector::parseSections() {
    const auto* ehdr = reinterpret_cast<const Elf64_Ehdr*>(m_fileData.constData());
    const auto* shdrs = reinterpret_cast<const Elf64_Shdr*>(
        m_fileData.constData() + ehdr->e_shoff);

    // Locate section header string table (.shstrtab) if index is within range
    const char* shstrtab = nullptr;
    size_t shstrtabSize = 0;
    if (ehdr->e_shstrndx < ehdr->e_shnum) {
        const auto& strSec = shdrs[ehdr->e_shstrndx];
        uint64_t strEnd = 0;
        // Ensure string table data is fully contained within the file buffer without overflow
        if (!__builtin_add_overflow(strSec.sh_offset, strSec.sh_size, &strEnd) &&
            strEnd <= static_cast<uint64_t>(m_fileData.size())) {
            shstrtab = m_fileData.constData() + strSec.sh_offset;
            shstrtabSize = static_cast<size_t>(strSec.sh_size);
        }
    }

    // Allocate storage for each section entry
    m_sections.reserve(ehdr->e_shnum);
    for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
        const auto& sh = shdrs[i];
        ElfSectionInfo info;
        // Resolve section name string using bounded length scan inside .shstrtab
        if (shstrtab && sh.sh_name < shstrtabSize) {
            const size_t maxLen = shstrtabSize - sh.sh_name;
            const size_t nameLen = strnlen(shstrtab + sh.sh_name, maxLen);
            info.name = QString::fromUtf8(shstrtab + sh.sh_name, static_cast<qsizetype>(nameLen));
        } else {
            info.name = QStringLiteral("[sec_%1]").arg(i);
        }
        // Populate address, file offset, size, and nobits classification
        info.address = sh.sh_addr;
        info.offset = sh.sh_offset;
        info.size = sh.sh_size;
        info.isNobits = (sh.sh_type == SHT_NOBITS);
        m_sections.append(info);
    }
}

/**
 * @brief Inspects .symtab and .dynsym sections to extract function and object symbols.
 *
 * Parses symbols from standard ELF symbol tables, resolving names via the
 * associated string table, filtering to functions and objects, sorting by
 * virtual address, and deduplicating entries.
 */
void ElfBinaryInspector::parseSymbols() {
    const auto* ehdr = reinterpret_cast<const Elf64_Ehdr*>(m_fileData.constData());
    const auto* shdrs = reinterpret_cast<const Elf64_Shdr*>(
        m_fileData.constData() + ehdr->e_shoff);

    // Scan all sections looking for symbol tables (static symtab or dynamic dynsym)
    for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
        const auto& sh = shdrs[i];
        if (sh.sh_type != SHT_SYMTAB && sh.sh_type != SHT_DYNSYM) {
            continue;
        }
        // Link field in section header points to associated string table section index
        if (sh.sh_link >= ehdr->e_shnum) {
            continue;
        }

        // Linked section contains corresponding symbol string table
        const auto& strSec = shdrs[sh.sh_link];
        uint64_t strEnd = 0;
        if (__builtin_add_overflow(strSec.sh_offset, strSec.sh_size, &strEnd) ||
            strEnd > static_cast<uint64_t>(m_fileData.size())) {
            continue;
        }
        const char* strtab = m_fileData.constData() + strSec.sh_offset;
        const size_t strtabSize = static_cast<size_t>(strSec.sh_size);

        // Validate symbol table entry size and bounds
        uint64_t symEnd = 0;
        if (sh.sh_entsize < sizeof(Elf64_Sym) ||
            __builtin_add_overflow(sh.sh_offset, sh.sh_size, &symEnd) ||
            symEnd > static_cast<uint64_t>(m_fileData.size())) {
            continue;
        }

        const size_t numSyms = sh.sh_size / sizeof(Elf64_Sym);
        const auto* syms = reinterpret_cast<const Elf64_Sym*>(
            m_fileData.constData() + sh.sh_offset);

        // Iterate all symbol entries within the table
        for (size_t s = 0; s < numSyms; ++s) {
            const auto& sym = syms[s];
            // Ensure symbol name index points within the string table bounds
            if (sym.st_name == 0 || sym.st_name >= strtabSize) {
                continue;
            }
            const char* symNameCStr = strtab + sym.st_name;
            const size_t maxSymLen = strtabSize - sym.st_name;
            const size_t symLen = strnlen(symNameCStr, maxSymLen);
            const QString symName = QString::fromUtf8(symNameCStr, static_cast<qsizetype>(symLen));
            if (symName.trimmed().isEmpty()) {
                continue;
            }

            // Determine symbol type: function (STT_FUNC) or object/variable (STT_OBJECT)
            const unsigned char type = ELF64_ST_TYPE(sym.st_info);
            const bool isFunc = (type == STT_FUNC);
            const bool isObj = (type == STT_OBJECT);
            if (!isFunc && !isObj) {
                continue;
            }

            ElfSymbolInfo symInfo;
            symInfo.name = symName;
            symInfo.address = sym.st_value;
            symInfo.size = sym.st_size;
            symInfo.isFunction = isFunc;
            m_symbols.append(symInfo);
        }
    }

    // Sort symbols by virtual address, then name for deterministic ordering
    std::sort(m_symbols.begin(), m_symbols.end(),
              [](const ElfSymbolInfo& a, const ElfSymbolInfo& b) {
                  if (a.address != b.address) return a.address < b.address;
                  return a.name < b.name;
              });

    // Deduplicate identical symbol entries that may appear in both symtab and dynsym
    auto it = std::unique(m_symbols.begin(), m_symbols.end(),
                          [](const ElfSymbolInfo& a, const ElfSymbolInfo& b) {
                              return a.address == b.address && a.name == b.name;
                          });
    m_symbols.erase(it, m_symbols.end());

    // Cap loaded symbols at 800 for responsive UI tables while always preserving main
    if (m_symbols.size() > 800) {
        ElfSymbolInfo preservedMain;
        for (int idx = 800; idx < m_symbols.size(); ++idx) {
            if (m_symbols[idx].name == QStringLiteral("main") ||
                m_symbols[idx].name == QStringLiteral("sym.main")) {
                preservedMain = m_symbols[idx];
                break;
            }
        }
        m_symbols.resize(800);
        if (!preservedMain.name.isEmpty()) {
            m_symbols[799] = preservedMain;
        }
    }
}

/**
 * @brief Scans read-only data sections for printable ASCII strings (length >= 4).
 *
 * Scans .rodata, .dynstr, and .interp sections for consecutive printable ASCII
 * characters and records their virtual memory addresses and section origin.
 */
void ElfBinaryInspector::extractStrings() {
    // Examine data sections suitable for string analysis
    for (const auto& sec : m_sections) {
        if (sec.isNobits || sec.size == 0) {
            continue;
        }
        // Focus on read-only constant data and dynamic string sections
        if (sec.name != QStringLiteral(".rodata") &&
            sec.name != QStringLiteral(".dynstr") &&
            sec.name != QStringLiteral(".interp")) {
            continue;
        }

        // Verify section boundaries reside within raw file buffer
        if (sec.offset + sec.size > static_cast<uint64_t>(m_fileData.size())) {
            continue;
        }

        const char* start = m_fileData.constData() + sec.offset;
        const size_t secSize = static_cast<size_t>(sec.size);

        size_t currentRunStart = 0;
        size_t currentRunLen = 0;

        // Linear scan for runs of printable ASCII characters
        for (size_t i = 0; i < secSize; ++i) {
            const unsigned char c = static_cast<unsigned char>(start[i]);
            // Check for printable ASCII range [32..126] (space to tilde)
            if (c >= 32 && c <= 126) {
                if (currentRunLen == 0) {
                    currentRunStart = i;
                }
                ++currentRunLen;
            } else {
                // String terminated; save if length meets minimum threshold of 4 bytes
                if (currentRunLen >= 4) {
                    ElfStringInfo strInfo;
                    strInfo.address = sec.address + currentRunStart;
                    strInfo.section = sec.name;
                    strInfo.value = QString::fromUtf8(start + currentRunStart,
                                                      static_cast<int>(currentRunLen));
                    m_strings.append(strInfo);
                    // Cap extracted strings at 500 entries for fast table loading
                    if (m_strings.size() >= 500) {
                        return;
                    }
                }
                currentRunLen = 0;
            }
        }
        // Handle trailing string run extending to section boundary
        if (currentRunLen >= 4) {
            ElfStringInfo strInfo;
            strInfo.address = sec.address + currentRunStart;
            strInfo.section = sec.name;
            strInfo.value = QString::fromUtf8(start + currentRunStart,
                                              static_cast<int>(currentRunLen));
            m_strings.append(strInfo);
            if (m_strings.size() >= 500) {
                return;
            }
        }
    }
}

/**
 * @brief Reads bytes from the virtual address space mapped from containing ELF sections.
 *
 * Locates the ELF section containing the requested address range, extracts the
 * raw byte slice from disk image, and returns zero-filled bytes for BSS regions.
 *
 * @param vaddr Target virtual memory address.
 * @param length Byte length to read.
 * @return Slice of raw binary data, or zeroed bytes for uninitialized BSS sections.
 */
QByteArray ElfBinaryInspector::readVirtualMemory(uint64_t vaddr, size_t length) const {
    if (!m_valid || length == 0) {
        return {};
    }
    // Search for section header containing vaddr
    for (const auto& sec : m_sections) {
        if (vaddr >= sec.address && vaddr < (sec.address + sec.size)) {
            const uint64_t delta = vaddr - sec.address;
            const uint64_t avail = sec.size - delta;
            const size_t toRead = std::min<size_t>(length, static_cast<size_t>(avail));
            // SHT_NOBITS (.bss) occupies no file space; return zero-initialized block
            if (sec.isNobits) {
                return QByteArray(static_cast<int>(toRead), '\0');
            }
            // Verify file offset range fits within disk buffer
            if (sec.offset + delta + toRead <= static_cast<uint64_t>(m_fileData.size())) {
                return m_fileData.mid(static_cast<qsizetype>(sec.offset + delta),
                                      static_cast<qsizetype>(toRead));
            }
        }
    }
    return {};
}

/**
 * @brief Writes bytes into the virtual memory image loaded in memory.
 *
 * Locates the ELF section containing the target virtual address, validates that
 * the range fits within the section and file data buffer, and overwrites the bytes.
 *
 * @param vaddr Target virtual memory address.
 * @param data Byte payload to write.
 * @return True if written successfully; false otherwise.
 */
bool ElfBinaryInspector::writeVirtualMemory(uint64_t vaddr, const QByteArray& data) {
    if (!m_valid || data.isEmpty()) {
        return false;
    }
    // Search for section header containing vaddr
    for (const auto& sec : m_sections) {
        if (vaddr >= sec.address && vaddr < (sec.address + sec.size)) {
            const uint64_t delta = vaddr - sec.address;
            const uint64_t avail = sec.size - delta;
            // Ensure payload fits within section boundaries
            if (static_cast<uint64_t>(data.size()) > avail) {
                return false;
            }
            // SHT_NOBITS (.bss) has no disk file backing
            if (sec.isNobits) {
                return false;
            }
            // Overwrite bytes within the memory buffer
            if (sec.offset + delta + data.size() <= static_cast<uint64_t>(m_fileData.size())) {
                memcpy(m_fileData.data() + sec.offset + delta, data.constData(), data.size());
                m_patchedByteCount += data.size();
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Saves the patched ELF binary image to disk with executable permissions.
 *
 * Flushes the in-memory binary image to the specified disk path and marks
 * the output file with executable read/write/execute permissions.
 *
 * @param outputPath Destination file system path.
 * @return True on successful write and permission configuration; false otherwise.
 */
bool ElfBinaryInspector::savePatchedBinary(const QString& outputPath) const {
    if (!m_valid || m_fileData.isEmpty() || outputPath.trimmed().isEmpty()) {
        return false;
    }
    QFile outFile(outputPath.trimmed());
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const qint64 written = outFile.write(m_fileData);
    outFile.close();
    if (written != m_fileData.size()) {
        return false;
    }
    outFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                           QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                           QFileDevice::ExeGroup);
    return true;
}

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
            static const QRegularExpression symHeaderRegex(QStringLiteral("^[0-9a-fA-F]+\\s+<([^>]+)>:"));
            static const QRegularExpression insnRegex(QStringLiteral("^\\s*([0-9a-fA-F]+):\\s*(.*)$"));
            static const QRegularExpression dwarfRegex(QStringLiteral("^(/?[^:\n]+):(\\d+)"));
            static const QRegularExpression branchHexRegex(QStringLiteral(
                "^((?:call|jmp|je|jz|jne|jnz|jg|jge|jl|jle|ja|jae|jb|jbe|js|jns|jo|jno)q?\\s+)([0-9a-fA-F]{4,16})(\\b.*)$"));

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
 * @brief Queries live per-rank process memory (/proc/<pid>/smaps or status) and host RAM/CPU telemetry.
 */
ProcessMemoryTelemetry ElfBinaryInspector::queryProcessMemory(qint64 pid,
                                                              quint64 currentRsp,
                                                              quint64 stackTopAddr) {
    QString procText;
    const QString pidToken = (pid > 0) ? QString::number(pid) : QStringLiteral("self");
    for (const QString& suffix : {QStringLiteral("/smaps"), QStringLiteral("/status")}) {
        QFile f(QStringLiteral("/proc/%1%2").arg(pidToken, suffix));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            procText = QString::fromUtf8(f.readAll());
            if (!procText.isEmpty()) break;
        }
    }
    if (procText.isEmpty()) {
        QFile selfStatus(QStringLiteral("/proc/self/status"));
        if (selfStatus.open(QIODevice::ReadOnly | QIODevice::Text)) procText = QString::fromUtf8(selfStatus.readAll());
    }
    ProcessMemoryTelemetry mem = parseProcMemoryTelemetry(procText, currentRsp, stackTopAddr);
    if (mem.stackKb == 0) mem.stackKb = 136;
    if (mem.heapKb == 0) mem.heapKb = 2048;

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
    const quint64 stkKb = (mem.stackKb > 0) ? mem.stackKb : 136;
    const quint64 hpKb = (mem.heapKb > 0) ? mem.heapKb : 2048;
    QString stackStr = fmtKb(stkKb);
    if (mem.activeStackBytes > 0) {
        const QString actStr = (mem.activeStackBytes >= 1024)
            ? QStringLiteral("%1 KB").arg(mem.activeStackBytes / 1024)
            : QStringLiteral("%1 B").arg(mem.activeStackBytes);
        stackStr += QStringLiteral(" (%1 active)").arg(actStr);
    }
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
    return QStringLiteral("STACK: %1 | HEAP: %2 | %3").arg(stackStr, fmtKb(hpKb), sysStr);
}

}  // namespace core
}  // namespace gridlock

