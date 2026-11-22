# Core Visualization & Editor Views (`src/ui/views/`)

Primary source editor, fast CSV table preview, disassembly/CFG/decompiler panes, live hex memory view, memory treemap profiler, CPU registers, and MPI cluster blade rack.

## Contents

- `SourceCodeView.hpp` (`837`), `SourceCodeView.cpp` (`947`), `EditorSyntaxHighlighter.hpp`, `EditorSyntaxHighlighter.cpp`:
  - Breakpoint-gutter editor (`LineNumberArea`), active-line stepper highlight, rainbow brackets, hover inspect, `clangd` integration, and `Ctrl+F` find bar.
  - Files `>= 512 KB` (`kLargeFileThresholdBytes`) or `.csv`/`.tsv` files (`isPlainDataFile`) disable synchronous C++ syntax highlighting, rainbow brackets, and `clangd`.
- `CsvTablePreviewWidget.hpp` (`140`), `CsvTablePreviewWidget.cpp`:
  - Virtualized `QAbstractTableModel` (`CsvVirtualTableModel`) + `QTableView` indexing newline offsets over large `.csv`/`.tsv` files (`>= 256 KB`), with a raw-text preview toggle.
- `DisassemblyView.hpp` (`395`), `DisassemblyView.cpp` (`927`), `DisassemblyAnalysis.cpp` (`836`), `DisassemblyDecompiler.cpp` (`938`):
  - Assembly tracker (`AsmSyntaxHighlighter`), basic-block CFG (`BranchFlowGutter`), cross-references, bookmarks, and structured C/C++/pseudocode decompiler.
- `MemView.hpp` (`174`), `MemView.cpp` (`944`), `MemoryProfilerWidget.hpp` (`178`), `MemoryProfilerWidget.cpp` (`944`):
  - Live hex/ASCII memory viewer, cross-rank diffing, and interactive treemap (`MemoryTreemapCanvas`).
- `RegisterView.hpp` (`175`), `RegisterView.cpp`:
  - Per-rank/per-thread GP/SIMD register table with value-delta highlighting.
- `ServerRackView.hpp` (`182`), `ServerRackView.cpp`:
  - Custom-painted vertical MPI rank hardware blade slivers showing stopped, running, and deadlocked status.

## Expected Behavior & Invariants

- **Fast Large-File & CSV Handling**:
  - Large `.csv`/`.tsv` files (`>= 256 KB`) load virtually via `CsvTablePreviewWidget`; source files `>= 512 KB` bypass synchronous highlighting and `clangd` to eliminate UI freezes.
- **Critical Line Ceiling Watch**:
  - `SourceCodeView.cpp` (`947`), `MemView.cpp` (`944`), `MemoryProfilerWidget.cpp` (`944`), `DisassemblyDecompiler.cpp` (`938`), and `DisassemblyView.cpp` (`927`) sit directly beneath `< 950` lines. Split helpers before adding features.
- **Strict `QPainter` Geometry**:
  - Custom gutters (`LineNumberArea`, `BranchFlowGutter`) and `ServerRackView` must calculate exact `QRect` bounds and reset pens/brushes inside every loop iteration.
- **Verification**:
  - Run `meson test -C builddir ui_tests --print-errorlogs`.

## Maintainer & Agent Notes

- Line Ceiling & Splitting Rules:
  - Never add new features directly into `925+`-line view files (`SourceCodeView.cpp`, `MemView.cpp`, `MemoryProfilerWidget.cpp`, `DisassemblyDecompiler.cpp`, `DisassemblyView.cpp`).
  - Extract cohesive rendering, analysis, or model helpers into dedicated `.cpp` units first and register them in `meson.build`.
- Cohesion & Static Test Checks:
  - Preserve static source checks in `tests/ui/test_ui_cohesion.cpp` (e.g., `../src/ui/views/DisassemblyAnalysis.cpp`) and toolbar/dock chrome alignment tests.
  - Keep `EditorSyntaxHighlighter` dependencies lightweight (`QtGui` / `QtWidgets`) because `dap_commands_tests` in `meson.build` compiles `SourceCodeView.cpp` and `EditorSyntaxHighlighter.cpp`.
- Large-File Safeguards:
  - Keep plain `.csv`/`.tsv` (`>= 256 KB`) virtualized preview and raw plain-text toggles responsive without synchronous lexer passes.
  - Update this `README.md` whenever extracting new view helpers or adding visualizations.
