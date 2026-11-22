# Core Visualization & Editor Views (`src/ui/views/`)

Primary source editor, fast CSV table preview, disassembly/CFG/decompiler panes, live hex memory view, memory treemap profiler, CPU registers, and MPI cluster blade rack.

## Contents

- `SourceCodeView.hpp` (`837`), `SourceCodeView.cpp` (`446`), `SourceCodeViewInteraction.cpp` (`463`), `EditorSyntaxHighlighter.{hpp,cpp}`:
  - Breakpoint-gutter editor (`LineNumberArea`), active-line stepper highlight, rainbow brackets, and two-lane gutter badge painting (`SourceCodeView.cpp`).
  - Context menus, breakpoint popovers, PC drag-and-drop, hover inspect, inline variable ghost values, `clangd` completion/F12/rename, and Git diff hunk parsing (`SourceCodeViewInteraction.cpp`).
  - Files `>= 512 KB` (`kLargeFileThresholdBytes`) or `.csv`/`.tsv` files (`isPlainDataFile`) disable synchronous C++ syntax highlighting, rainbow brackets, and `clangd`.
- `CsvTablePreviewWidget.hpp` (`140`), `CsvTablePreviewWidget.cpp`:
  - Virtualized `QAbstractTableModel` (`CsvVirtualTableModel`) + `QTableView` indexing newline offsets over large `.csv`/`.tsv` files (`>= 256 KB`), with a raw-text preview toggle.
- `DisassemblyView.hpp` (`395`), `DisassemblyView.cpp` (`927`), `DisassemblyAnalysis.cpp` (`836`), `DisassemblyDecompiler.cpp` (`938`):
  - Assembly tracker (`AsmSyntaxHighlighter`), basic-block CFG (`BranchFlowGutter`), cross-references, bookmarks, and structured C/C++/pseudocode decompiler.
- `MemView.hpp` (`174`), `MemView.cpp` (`468`), `MemViewRender.cpp` (`316`):
  - Segmented `HEX DUMP | LIBRARY MEMORY MAP` container, pop-out windowing, rank pinning, live reads, byte patching, CSV/binary export, and diff dispatch (`MemView.cpp`), plus HTML/plain-text hex formatting, diff spans, styling, and byte-offset word extraction (`MemViewRender.cpp`).
- `MemoryProfilerWidget.hpp` (`178`), `MemoryProfilerWidget.cpp` (`476`), `MemoryProfilerChart.cpp` (`360`):
  - Controls, `>= 4 KB` `LD_PRELOAD` interposer compilation, and `/proc/<pid>/smaps` + `.tsv` VMA aggregation (`MemoryProfilerWidget.cpp`), plus `MemoryTreemapCanvas`, scientific/Okabe-Ito palettes, and sortable breakdown table population (`MemoryProfilerChart.cpp`).
- `RegisterView.hpp` (`175`), `RegisterView.cpp`:
  - Per-rank/per-thread GP/SIMD register table with value-delta highlighting.
- `ServerRackView.hpp` (`182`), `ServerRackView.cpp`:
  - Custom-painted vertical MPI rank hardware blade slivers showing stopped, running, and deadlocked status.

## Expected Behavior & Invariants

- **Fast Large-File & CSV Handling**:
  - Large `.csv`/`.tsv` files (`>= 256 KB`) load virtually via `CsvTablePreviewWidget`; source files `>= 512 KB` bypass synchronous highlighting and `clangd` to eliminate UI freezes.
- **Translation Unit Line Budget**:
  - `SourceCodeView*.cpp` (`446`, `463`), `MemView*.cpp` (`468`, `316`), and `MemoryProfiler*.cpp` (`476`, `360`) sit strictly `< 500` lines; `DisassemblyDecompiler.cpp` (`938`) and `DisassemblyView.cpp` (`927`) sit near `< 950` lines and must be split before adding features.
- **Strict `QPainter` Geometry**:
  - Custom gutters (`LineNumberArea`, `BranchFlowGutter`), `MemoryTreemapCanvas`, and `ServerRackView` must calculate exact `QRect` bounds and reset pens/brushes inside every loop iteration.
- **Verification**:
  - Run `meson test -C builddir ui_tests --print-errorlogs`.

## Maintainer & Agent Notes

- Line Ceiling & Splitting Rules:
  - Keep split view units (`SourceCodeView*.cpp`, `MemView*.cpp`, `MemoryProfiler*.cpp`) under `500` lines and never expand `Disassembly*.cpp` past `< 950` lines without extracting helpers first.
- Build Sync & Static Cohesion Checks:
  - Preserve static source checks in `tests/ui/test_ui_cohesion.cpp` (e.g., `../src/ui/views/DisassemblyAnalysis.cpp`) and toolbar/dock chrome alignment tests.
  - Keep `SourceCodeView*.cpp` and `EditorSyntaxHighlighter.cpp` registered in `lsp_test_src` and `dap_commands_test_src` in `meson.build` alongside `src_files` and `ui_test_src`.
- Large-File Safeguards:
  - Keep plain `.csv`/`.tsv` (`>= 256 KB`) virtualized preview and raw plain-text toggles responsive without synchronous lexer passes, and update this `README.md` whenever splitting view units.
