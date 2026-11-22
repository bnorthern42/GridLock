# Core Utilities (`src/core/utils/`)

Standalone infrastructure utilities for CLI argument parsing, Search Everywhere fuzzy ranking, native ELF64 binary inspection, and virtual-buffer `clangd` expression autocomplete.

## Contents

- `CliParser.hpp` (`114` lines), `CliParser.cpp`:
  - Defines `CliOptions` and `CliParser`, wrapping `QCommandLineParser` for `--test-mode`, `--tutorial-mode`, `--verbose`, `--headless`, and `--snapshot <file>`.
  - Exposes `shouldEnableOffscreenPlatform()` prior to `QApplication` initialization (`tests/core/test_cliparser.cpp`).
- `SearchEverywhereEngine.hpp` (`125` lines), `SearchEverywhereEngine.cpp`:
  - Defines `SearchScope`, `IndexedSymbol`, `TextSearchHit`, and `IndexedFile`.
  - Implements deterministic JetBrains-style CamelCase, `snake_case`, and path-segment fuzzy scoring.
  - Caches workspace file/symbol indices, performs fast capped text grep, and extracts context-window source snippets.
- `ElfBinaryInspector.hpp` (`267` lines), `ElfBinaryParser.cpp` (`494` lines), `ElfBinaryInspector.cpp` (`455` lines):
  - Overflow-checked `<elf.h>` ELF64 header validation, `.shstrtab` section metadata, `.symtab`/`.dynsym` symbol tables, printable ASCII strings, virtual-address memory slicing, and binary patching (`ElfBinaryParser.cpp`).
  - DWARF-annotated `objdump` symbol/range disassembly, deterministic x86_64 fallback decoding, and `/proc/<pid>/smaps` + `/proc/meminfo` + `/proc/stat` `ProcessMemoryTelemetry` parsing (`ElfBinaryInspector.cpp`).
- `ClangdLspClient.hpp` (`358` lines), `ClangdLspClient.cpp` (`907` lines), `ClangdLspNavigation.cpp` (`854` lines):
  - Manages a persistent `clangd` child process with `Content-Length` JSON-RPC framing.
  - Provides virtual source-line splicing for conditional breakpoint autocomplete, local C++ scope fallback, and go-to-definition navigation.

## Expected Behavior & Invariants

- **Bounded Indexing & ELF Bounds Validation**:
  - `SearchEverywhereEngine` enforces file count, binary exclusion, and result caps to keep Search Everywhere interactive.
  - `ElfBinaryInspector` bounds-checks section headers, symbol tables, and string offsets before reading binary buffers (`tests/core/test_stride_security.cpp`).
- **Graceful `clangd` Fallback**:
  - When `clangd` is absent or slow, `ClangdLspClient` falls back reliably to deterministic local C++ struct/STL scope analysis.
- **Translation Unit Line Budget**:
  - `ElfBinaryParser.cpp` (`494` lines) and `ElfBinaryInspector.cpp` (`455` lines) sit strictly `< 500` lines; `ClangdLspClient.cpp` (`907` lines) sits near `< 950` lines and should be split before adding new LSP features.
- **Verification**:
  - Run `meson test -C builddir cliparser_tests stride_security_tests ui_tests --print-errorlogs`.

## Maintainer & Agent Notes

- Keep `CliParser` completely free of `QtWidgets` so `src/main.cpp` resolves headless QPA flags before instantiating `QApplication`.
- Keep all Search Everywhere fuzzy scoring, workspace file walking, and grep caps inside `SearchEverywhereEngine` rather than inside `CommandPaletteDialog.cpp`.
- Coordinate LSP JSON-RPC framing fixes across both `ClangdLspClient` and `src/core/hpc/lsp/LspCoordinator` so protocol edge cases never diverge.
- Keep `ElfBinaryParser.cpp` and `ElfBinaryInspector.cpp` dependency-free (`<elf.h>` + QtCore) and `< 500` lines each; note `tests/core/test_stride_security.cpp` includes `ElfBinaryInspector.cpp` directly and links `ElfBinaryParser.cpp` via `stride_security_test_src` in `meson.build`.
- Update this `README.md` and `meson.build` whenever splitting utility units.
