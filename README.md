# GridLock v0.7.0 — MPI Debugger & ELF Binary Inspector

![Version](https://img.shields.io/badge/Release-v0.7.0-brightgreen.svg?style=for-the-badge)
[![Build Status](https://github.com/bnorthern42/GridLock/actions/workflows/release.yml/badge.svg)](https://github.com/bnorthern42/GridLock/actions/workflows/release.yml)
![C++23](https://img.shields.io/badge/C++-23-blue.svg?style=for-the-badge&logo=c%2B%2B)
![Qt6](https://img.shields.io/badge/Qt-6-41CD52.svg?style=for-the-badge&logo=qt)
![Wayland/Niri](https://img.shields.io/badge/Wayland-Niri_Native-orange.svg?style=for-the-badge)
![Meson/Ninja](https://img.shields.io/badge/Build-Meson%20|%20Ninja-blue.svg?style=for-the-badge)
![LLDB/DAP & GDB/MI3](https://img.shields.io/badge/Backend-LLDB--DAP%20|%20GDB--MI3-purple.svg?style=for-the-badge)

**GridLock (`v0.7.0`)** is a C++ and Qt6 debugger for multi-rank MPI applications and ELF binaries on Linux. It lets you step individual MPI ranks, compare variables and memory across processes, inspect disassembly and control-flow graphs, and catch communication deadlocks.

---

## Workspace Perspectives

### 1. Code + Disassembly (`Alt+1` / `CODE`)
Side-by-side C/C++ source stepping (`SourceCodeView`), linear assembly with branch arcs (`DisassemblyView`), CPU registers (`RegisterView`), cross-rank watch comparisons (`DifferentialGrid`), rank status cards (`ServerRackView`), and a hex memory viewer (`MemView`).

![GridLock Workspace Overview](docs/screenshots/workspace_overview.png)

### 2. Reverse Engineering (`Alt+2` / `REVERSE`)
Full-width split view pairing the basic-block Control Flow Graph (with per-block MPI rank badges `[R0]`, `[R1]`, `[R2-R7]` and a corner minimap) with a synchronized decompiler (`Approx C`, `C++`, `Pseudocode`) and cross-reference (`XREF`) comments.

![GridLock CFG & Decompiler Split Perspective](docs/screenshots/cfg_decompiler_split.png)

### 3. MPI Diagnostics (`Alt+3` / `MPI`)
Dedicated view for circular wait-for deadlock detection (`DeadlockAnalyzer`), MPI point-to-point and collective message logs (`MpiDiagnosticsWidget` / `MpiNetworkLogWidget`), and offline documentation via Zeal docsets and system `man` pages (`ReferenceManualWidget`).

![GridLock MPI Diagnostics & Deadlock Analyzer](docs/screenshots/mpi_diagnostics_docsets.png)

---

## Features

### Multi-Rank MPI Debugging
* **Toolbar Target & Rank Controls:** Pick an MPI demo target or load any ELF64 binary from the toolbar, set the process count (`-np 1..64`), and run, step, pause, or stop individual ranks.
* **Synchronized Source & Assembly Stepping:** Stepping an instruction updates the active address across the disassembly view, CFG, decompiler, source editor, register table, rank status panel, and hex memory view.
* **Cross-Rank Watch Table (`DifferentialGrid`):** Evaluate C/C++ expressions (such as `data[0].id`, `data[0].coords.x`, or `mpi_rank`) across all active ranks at once, or promote variables directly from the `Variables` and `Evaluator` panels.
* **Deadlock Cycle Detection (`DeadlockAnalyzer`):** Builds a wait-for graph across blocked `MPI_Send`, `MPI_Recv`, and `MPI_Barrier` calls and flags circular dependencies in both the rank rack and the control-flow graph.
* **Cross-Rank Memory Diffing (`MemoryDiffer`):** Reads process memory directly via `process_vm_readv` and highlights byte differences between any two MPI ranks in `MemView`.

### Binary Inspection & Reverse Engineering
* **ELF64 Parser (`ElfBinaryInspector`):** Reads ELF headers, sections (`.plt`, `.text`, `.rodata`, `.data`), symbol tables (`.symtab`, `.dynsym`), and printable strings directly from disk, displaying section layout in an address-space navigation bar.
* **Control Flow Graph & Rank Badges:** Breaks assembly routines into basic blocks with true, false, and unconditional branch edges, showing which MPI ranks are stopped inside each block.
* **Decompiler & Bidirectional Navigation:** Generates readable `Approx C`, `C++`, or `Pseudocode` alongside assembly. Clicking a line in the source editor or decompiler jumps the disassembly, CFG, and memory views to the matching instruction address, and vice versa.
* **Cross-Reference Inspector (`XrefInspectorDialog`):** Press `X` on any instruction to list callers and incoming branches (`XREFS TO`) as well as outgoing calls and jumps (`XREFS FROM`).
* **Instruction & Byte Patching:** Patch assembly instructions (`Ctrl+Shift+P`) or raw hex bytes (`Ctrl+E` in `MemView`) and write the modified ELF64 binary back to disk (`ElfBinaryInspector::savePatchedBinary`).

### Editor, Cluster Tools & Environment
* **Gutter Breakpoint Popover & `clangd` Completion:** Right-clicking a line in the source gutter opens a compact popover right next to the cursor where you can filter breakpoints by rank (`All MPI Ranks` or `Focused Rank Only`) and write conditional expressions with live `clangd` autocomplete.
* **Bottom Tool Rail:** Groups the bottom dock panels into four categories (`Debug`, `MPI`, `Terminal`, `Docs`) on a vertical icon rail so only relevant tabs are shown at a time.
* **System & HPC Package Search (`HpcConsoleWidget`):** Queries `spack`, `pacman`, `dnf`, `apt`, `zypper`, `rpm`, `dpkg-query`, and `eb` from a single search tab with `Table`, `List`, and `Raw` output modes.
* **SLURM Script Parser & Build Runner:** Imports `#SBATCH` directives and `module load` lines from existing batch scripts in `PreferencesDialog` (`HPC / Cluster`), and builds projects with `Meson`, `CMake`, `Make`, or `mpicxx` (`F7`).
* **Dark & Light Themes:** Includes 5 dark themes and 3 light themes (`Daylight Paper`, `Solarized Warm`, `Catppuccin Latte`) with live UI and code font scaling in Preferences.
* **Offline Zeal Docsets & Man Pages (`ReferenceManualWidget`):** Searches local Zeal `.docset` bundles (`~/.local/share/Zeal/Zeal/docsets`) and system `man` pages (`F1`).
* **Embedded Terminal (`TerminalDockWidget`):** Built-in `QTermWidget` pseudo-terminal for running local shell or cluster commands.

| Conditional Breakpoint Popover | Package Manager (`"mpi"` Search) |
| :---: | :---: |
| ![Breakpoint Popover](docs/screenshots/breakpoint_popover.png) | ![Package Manager MPI Search](docs/screenshots/package_manager_mpi_search.png) |

| Bottom Tool Rail (`Debug` Mode) | `#SBATCH` Script Import |
| :---: | :---: |
| ![Tool Rail Debug Tabs](docs/screenshots/tool_rail_debug_tabs.png) | ![SLURM Auto-Detection](docs/screenshots/preferences_slurm_autodetect.png) |

---

## Keyboard Shortcuts

| Shortcut | Action | Scope |
| :--- | :--- | :--- |
| **`Alt+1`** | Switch to **Code** perspective (`SourceCodeView` + `DisassemblyView`) | Global |
| **`Alt+2`** | Switch to **Reverse** perspective (Split `CFG` + `Decompiler`) | Global |
| **`Alt+3`** | Switch to **MPI** perspective (`MPI Diagnostics` & cluster views) | Global |
| **`Ctrl+M`** | Toggle maximize / restore bottom dock | Global |
| **`Ctrl+G`** | Focus address / symbol seek bar | Global |
| **`Alt+Left` / `Alt+Right`** | Navigate backward / forward in address history | Global |
| **`Space`** | Toggle `DisassemblyView` between linear assembly and CFG | Disassembly / CFG |
| **`X`** | Open cross-reference dialog (`XrefInspectorDialog`) for current address | Disassembly / CFG |
| **`F1`** | Look up symbol in Reference Manual (Zeal / `man`) | Disassembly / Global |
| **`Ctrl+Shift+P`** | Patch instruction in `DisassemblyView` | Disassembly |
| **`Ctrl+E`** | Edit hex bytes at active address in `MemView` | Memory View |
| **`Ctrl+Shift+F`** | Search across symbols, strings, assembly, and decompiler output | Global |
| **`;`** | Add or edit inline comment at active instruction address | Disassembly |
| **`Ctrl+B`** | Toggle bookmark on active instruction | Disassembly |

---

## Building & Running

### Dependencies
* **Compiler:** GCC 13+ or Clang 16+ (C++23)
* **Build tools:** Meson (`>= 1.1`) and Ninja
* **Qt 6:** `Qt6Core`, `Qt6Gui`, `Qt6Widgets`, `Qt6Network`, `Qt6Sql`, `Qt6Concurrent`, `Qt6Svg`, `Qt6OpenGLWidgets`, `Qt6Test`
* **Libraries:** `qtermwidget6`, `tomlplusplus`, and bundled `Qt-Advanced-Stylesheets`

### Compile & Launch
```bash
meson setup builddir
ninja -C builddir gridlock

# Launch GridLock
./builddir/gridlock
```

### Running the Test Suite
GridLock has 26 test suites covering the debugger backend, ELF/DWARF parsers, MPI deadlock detection, and UI workflows:
```bash
find builddir -name "*.gcda" -delete && meson test -C builddir --print-errorlogs
```

### Headless Screenshot Capture
Render a window snapshot offscreen without a display server:
```bash
QT_QPA_PLATFORM=offscreen ./builddir/gridlock --headless --snapshot docs/screenshots/workspace_overview.png
```

### Configuration (`config.toml` & `.gridlock/workspace.toml`)
Global settings live in `~/.config/gridlock/config.toml` and per-project overrides in `.gridlock/workspace.toml`:
```toml
[ui]
preferred_monitor = "2560x1440"
open_unfocused = false

[docsets]
paths = ["/home/user/.local/share/Zeal/Zeal/docsets"]
```

---

## Documentation

* [**Engineering Roadmap (`ROADMAP.md`)**](ROADMAP.md) — Current `v0.7.0` feature set and planned work for `v0.8.0`.
* [**Architecture Guide (`docs/ARCHITECTURE.md`)**](docs/ARCHITECTURE.md) — Window layout, module organization, and signal wiring.
* [**User Guide (`docs/USER_GUIDE.md`)**](docs/USER_GUIDE.md) — Walkthroughs for MPI debugging, conditional breakpoints, package search, SLURM import, and binary patching.
* [**Themes & Fonts (`docs/THEMES.md`)**](docs/THEMES.md) — Screenshots and color reference table for the built-in dark and light themes.
* [**User Manual (`docs/user/README.md`)**](docs/user/README.md) — Reference pages for panels, shortcuts, and cluster setup.
* [**Developer Notes (`docs/developer/README.md`)**](docs/developer/README.md) — Internals on backend coordinators, sanitizers, and UI widgets.

---

## Roadmap (`v0.7.0` → `v0.8.0`)

See [**ROADMAP.md**](ROADMAP.md) (and [`docs/ROADMAP.md`](docs/ROADMAP.md)) for details:

* **Completed in `v0.7.0`:** ELF64/DWARF line mapping (`objdump` + `.debug_line`), breakpoint-first execution, circular `MPI_Send` deadlock detection, `process_vm_readv` cross-rank memory inspection, conditional breakpoint popover with `clangd` completion, bottom tool-window icon rail, Wayland-friendly floating docks, 8 dark/light themes, `#SBATCH` script importer, multi-distro package search, and GitHub Actions AppImage builds.
* **Phase 7 — Static Analysis & Hardening:** `cppcheck`, `clang-tidy`, CLion inspection cleanups, SonarQube quality checks, and ASan/UBSan fuzzing on ELF/DWARF and memory readers.
* **Phase 8 — Theme & Layout Polish:** Light theme contrast fixes (`MemView`, `ProjectExplorerWidget`, section ribbon, mode buttons), `DisassemblyView` header spacing, missing `Solarized Warm` and `Catppuccin Latte` screenshots, and a shared palette token system.
* **Phase 9 — Hands-On Tutorials (`docs/TUTORIAL.md`):** Step-by-step guides for diagnosing `deadlock_ring.cpp`, setting rank-filtered conditional breakpoints on `inspection_demo.cpp`, diffing memory across ranks, and browsing Zeal docsets.
* **Phase 10 (`v0.8.0`) — Editor & `clangd` LSP Integration:** Project Explorer file creation/renaming, unsaved buffer markers, split editors, header/source switching (`F10`), live `clangd` diagnostics and Go-to-Definition (`F12`), Git diff gutter markers, and selectable debugger backends (`gdb`, `lldb-dap`, `cuda-gdb`, `rocgdb`).

---

## License

Licensed under the MIT License. See [LICENSE](LICENSE).
