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
- `ElfBinaryInspector.hpp` (`266` lines), `ElfBinaryInspector.cpp` (`930` lines):
  - Zero-dependency ELF64 parser using `<elf.h>` (`ElfSectionInfo`, `ElfSymbolInfo`, `ElfStringInfo`, `ProcessMemoryTelemetry`).
  - Extracts `.plt`/`.text`/`.rodata`/`.data`/`.bss` sections, `.symtab`/`.dynsym` symbols, ASCII strings, and disassembled ranges.
- `ClangdLspClient.hpp` (`358` lines), `ClangdLspClient.cpp` (`907` lines), `ClangdLspNavigation.cpp` (`854` lines):
  - Manages a persistent `clangd` child process with `Content-Length` JSON-RPC framing.
  - Provides virtual source-line splicing for conditional breakpoint autocomplete, local C++ scope fallback, and go-to-definition navigation.

## Expected Behavior & Invariants

- **Bounded Indexing & ELF Bounds Validation**:
  - `SearchEverywhereEngine` enforces file count, binary exclusion, and result caps to keep Search Everywhere interactive.
  - `ElfBinaryInspector` bounds-checks section headers, symbol tables, and string offsets before reading binary buffers.
- **Graceful `clangd` Fallback**:
  - When `clangd` is absent or slow, `ClangdLspClient` falls back reliably to deterministic local C++ struct/STL scope analysis.
- **Critical Line Ceiling Watch**:
  - `ElfBinaryInspector.cpp` (`930` lines) and `ClangdLspClient.cpp` (`907` lines) sit directly under `< 950` lines.
  - Split additional binary or navigation parsers into dedicated `.cpp` units (`ClangdLspNavigation.cpp` pattern).
- **Verification**:
  - Run `meson test -C builddir cliparser_tests ui_tests --print-errorlogs`.

## Maintainer & Agent Notes

- Keep `CliParser` completely free of `QtWidgets` so `src/main.cpp` resolves headless QPA flags before instantiating `QApplication`.
- Keep all Search Everywhere fuzzy scoring, workspace file walking, and grep caps inside `SearchEverywhereEngine` rather than inside `CommandPaletteDialog.cpp`.
- Coordinate LSP JSON-RPC framing fixes across both `ClangdLspClient` and `src/core/hpc/lsp/LspCoordinator` so protocol edge cases never diverge.
- Keep `ElfBinaryInspector` dependency-free (`<elf.h>` + QtCore) so binary inspection remains fast and testable without launching GDB.
- Update this `README.md` and `meson.build` whenever splitting `ElfBinaryInspector.cpp` or `ClangdLspClient.cpp`.
