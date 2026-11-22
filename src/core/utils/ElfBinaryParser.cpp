/**
 * @file ElfBinaryParser.cpp
 * @brief Native 64-bit ELF header validation, section/symbol/string extraction, and virtual memory I/O.
 *
 * Implements overflow-checked `<elf.h>` parsing of ELF64 headers, `.shstrtab` section
 * metadata, `.symtab`/`.dynsym` symbol tables, printable ASCII string extraction,
 * virtual-address memory slicing, and in-memory binary byte patching.
 */

#include "ElfBinaryInspector.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#include <QFile>
#include <QFileInfo>

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

}  // namespace core
}  // namespace gridlock
