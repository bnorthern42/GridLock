/**
 * @file ElfBinaryInspector.hpp
 * @brief Native 64-bit ELF binary parsing, symbol discovery, and memory inspection.
 *
 * Implements ElfBinaryInspector, providing zero-dependency ELF64 binary parsing
 * using native <elf.h> structures. Extracts section headers (.plt, .text, .rodata,
 * .data, .bss), symbol tables (.symtab, .dynsym), printable ASCII strings,
 * virtual-address memory mappings, and disassembled instruction ranges.
 */

#pragma once

#include <elf.h>
#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <cstddef>
#include <cstdint>

namespace gridlock {
namespace core {

/**
 * @struct ElfSectionInfo
 * @brief Metadata describing an ELF64 section header.
 */
struct ElfSectionInfo {
    QString name;          ///< Section name (e.g., .text, .rodata, .data).
    uint64_t address = 0;  ///< Virtual memory address in process space.
    uint64_t offset = 0;   ///< File byte offset from binary header.
    uint64_t size = 0;     ///< Size of section in bytes.
    bool isNobits = false; ///< True if SHT_NOBITS (occupies no disk space, e.g. .bss).
};

/**
 * @struct ElfSymbolInfo
 * @brief Representation of an ELF symbol table entry.
 */
struct ElfSymbolInfo {
    QString name;            ///< Symbol name (demangled or raw).
    uint64_t address = 0;    ///< Virtual memory address.
    uint64_t size = 0;       ///< Symbol size in bytes.
    bool isFunction = false; ///< True if STT_FUNC; false if STT_OBJECT or other.
};

/**
 * @struct ElfStringInfo
 * @brief Printable ASCII/UTF-8 string extracted from binary data sections.
 */
struct ElfStringInfo {
    uint64_t address = 0;    ///< Virtual memory address of first character.
    QString section;         ///< Containing section name (.rodata, .dynstr, etc.).
    QString value;           ///< Printable string payload.
};

/**
 * @class ElfBinaryInspector
 * @brief Native 64-bit ELF executable and shared library binary inspector.
 *
 * Parses 64-bit ELF files directly from disk or memory buffers without
 * external tool dependencies, providing section enumeration, symbol discovery,
 * string extraction, virtual memory reads, and bounded disassembly ranges.
 */
class ElfBinaryInspector {
public:
    /** @brief Constructs an uninitialized inspector. */
    ElfBinaryInspector() = default;

    /**
     * @brief Constructs an inspector and immediately attempts to parse filePath.
     * @param filePath Path to binary on disk.
     */
    explicit ElfBinaryInspector(const QString& filePath);

    /**
     * @brief Loads and parses a 64-bit ELF executable or shared library from disk.
     * @param filePath Absolute or relative path to ELF binary.
     * @return True if file is valid 64-bit ELF and successfully parsed; false otherwise.
     */
    bool loadFile(const QString& filePath);

    /**
     * @brief Loads and parses a 64-bit ELF from raw in-memory byte buffer.
     * @param data Raw binary data buffer.
     * @param sourcePath Optional label or origin path for error reporting.
     * @return True if valid ELF64.
     */
    bool loadFromData(const QByteArray& data, const QString& sourcePath = QString());

    /** @brief Returns true if an ELF64 binary was successfully loaded and parsed. */
    bool isValid() const { return m_valid; }

    /** @brief Returns virtual address of the program entry point (e_entry). */
    uint64_t entryPoint() const { return m_entryPoint; }

    /** @brief Returns disk path of the inspected binary. */
    QString filePath() const { return m_filePath; }

    /** @brief Returns parsed section headers. */
    const QList<ElfSectionInfo>& sections() const { return m_sections; }

    /** @brief Returns extracted function and object symbols. */
    const QList<ElfSymbolInfo>& symbols() const { return m_symbols; }

    /** @brief Returns extracted printable ASCII strings from data sections. */
    const QList<ElfStringInfo>& strings() const { return m_strings; }

    /**
     * @brief Reads bytes from the virtual address space mapped from the ELF sections.
     * @param vaddr Target virtual memory address.
     * @param length Number of bytes to read.
     * @return Byte array containing read data, or zero-filled for BSS regions.
     */
    QByteArray readVirtualMemory(uint64_t vaddr, size_t length) const;

    /**
     * @brief Writes bytes into the virtual memory image loaded in memory.
     * @param vaddr Target virtual memory address.
     * @param data Byte payload to write.
     * @return True if vaddr maps to a valid writable section and data was written; false otherwise.
     */
    bool writeVirtualMemory(uint64_t vaddr, const QByteArray& data);

    /**
     * @brief Saves the patched ELF binary image to disk with executable permissions.
     * @param outputPath Target file system path.
     * @return True if the binary was successfully written; false otherwise.
     */
    bool savePatchedBinary(const QString& outputPath) const;

    /** @brief Returns total count of bytes patched into the loaded binary. */
    int patchedByteCount() const { return m_patchedByteCount; }

    /**
     * @brief Disassembles an instruction range into formatted assembly lines.
     * @param startAddr Starting virtual memory address.
     * @param size Byte size of region to disassemble.
     * @param symbolName Optional enclosing function symbol name.
     * @return Formatted multiline disassembly text suitable for DisassemblyView.
     */
    QString disassembleRange(uint64_t startAddr, uint64_t size,
                             const QString& symbolName = QString()) const;

    /**
     * @brief Sets an optional C/C++ source filename basename filter for DWARF line table extraction.
     * @param sourceFileName Basename or path of primary source file (e.g., "deadlock_ring.cpp").
     */
    void setSourceFileFilter(const QString& sourceFileName);

    /** @brief Returns the configured DWARF source filename filter. */
    QString sourceFileFilter() const { return m_sourceFileFilter; }

    /**
     * @brief Disassembles a named function symbol (e.g. "main") using objdump -d -l -C -M intel
     *        and populates DWARF source-line-to-address and address-to-line lookup maps.
     * @param symbolName Function symbol name (e.g., "main" or "sym.main").
     * @param sourceFilter Optional basename filter for DWARF file entries.
     * @param outLineToAddr Optional output map of 1-based source line -> first instruction address.
     * @param outAddrToLine Optional output map of instruction address -> 1-based source line.
     * @return Formatted multiline disassembly text suitable for DisassemblyView.
     */
    QString disassembleSymbol(const QString& symbolName,
                              const QString& sourceFilter = QString(),
                              QMap<int, uint64_t>* outLineToAddr = nullptr,
                              QMap<uint64_t, int>* outAddrToLine = nullptr) const;

    /** @brief Returns the last extracted DWARF line-to-address map. */
    const QMap<int, uint64_t>& dwarfLineToAddress() const { return m_dwarfLineToAddr; }

    /** @brief Returns the last extracted DWARF address-to-line map. */
    const QMap<uint64_t, int>& dwarfAddressToLine() const { return m_dwarfAddrToLine; }

private:
    /** @brief Iterates section headers and populates m_sections. */
    void parseSections();

    /** @brief Inspects symbol tables (.symtab, .dynsym) and populates m_symbols. */
    void parseSymbols();

    /** @brief Scans read-only data sections for printable ASCII strings. */
    void extractStrings();

    QString m_filePath;               ///< Filesystem path to inspected binary.
    QByteArray m_fileData;            ///< Raw binary file contents in memory.
    bool m_valid = false;             ///< True if file is valid ELF64.
    uint64_t m_entryPoint = 0;        ///< Program entry point virtual address.
    QList<ElfSectionInfo> m_sections; ///< List of parsed section headers.
    QList<ElfSymbolInfo> m_symbols;   ///< List of parsed symbols.
    QList<ElfStringInfo> m_strings;   ///< List of extracted printable strings.
    int m_patchedByteCount = 0;       ///< Total number of patched bytes.
    QString m_sourceFileFilter;       ///< Optional source file basename filter for DWARF entries.
    mutable QMap<int, uint64_t> m_dwarfLineToAddr; ///< Cached DWARF 1-based line -> first instruction address.
    mutable QMap<uint64_t, int> m_dwarfAddrToLine; ///< Cached DWARF instruction address -> 1-based line.
    mutable uint64_t m_cachedDisasmStart = 0;      ///< Start address of cached disassembly region.
    mutable uint64_t m_cachedDisasmSize = 0;       ///< Byte size of cached disassembly region.
    mutable int m_cachedPatchGen = -1;             ///< Patch generation counter of cached disassembly.
    mutable QString m_cachedDisasmText;            ///< Cached formatted disassembly listing.
};

namespace utils {
// Backward-compatibility aliases under gridlock::core::utils
using ElfSectionInfo = core::ElfSectionInfo;
using ElfSymbolInfo = core::ElfSymbolInfo;
using ElfStringInfo = core::ElfStringInfo;
using ElfBinaryInspector = core::ElfBinaryInspector;
}  // namespace utils

}  // namespace core
}  // namespace gridlock
