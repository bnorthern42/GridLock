# Keyboard Shortcuts and Quick Reference

Compact lookup reference for GridLock (`v0.8.7`) execution controls, palettes, floating diagnostic windows, breakpoint lifecycle actions, GDB console readline bindings, and disassembly/CFG navigation.

---

## 1. Global Execution and Stepping Controls

| Shortcut / Control | Action | Backend Command | Scope |
| :--- | :--- | :--- | :--- |
| `Alt+R` / **`▶ Run Target`** | Launch parallel debug session | MPMD `mpirun` + GDB/MI3 or DAP `launch` | Global |
| `F5` / **`⏭ Continue`** | Continue execution on active ranks | `-exec-continue` / DAP `continue` | Global |
| **`⏸ Pause Rank`** | Interrupt active MPI rank | `-exec-interrupt` / `SIGINT` | Toolbar |
| `F10` | Step Over source line (or Switch Header/Source in editor) | `-exec-next` / DAP `next` | Global / Editor |
| `F11` | Step Into function call | `-exec-step` / DAP `stepIn` | Global |
| **`↷ Step Inst`** / `DisassemblyView` **`stepi`** | Step Into one machine instruction | `-exec-step-instruction` | Toolbar / Disassembly |
| `Ctrl+Shift+F10` | Step Over one machine instruction (`nexti`) | `-exec-next-instruction` | Global (`Debug` menu) |
| `Ctrl+F10` | **Run to Cursor** (active editor line or disassembly address) | `-exec-until "<file>:<line>"` / `-exec-until *0x<addr>` | Global (`Debug` menu) |
| `Debug -> Set Next Statement / Jump...` or drag gutter `▶` PC arrow | Move execution PC with lexical scope hazard check | `-exec-jump "<file>:<line>"` / `-exec-jump *0x<addr>` | `Debug` menu / Gutter / Disassembly |
| `Debug -> Force Early Return...` or `CALL STACK` right-click | Return immediately from current frame (optional return expr) | `-exec-return [expr]` | `Debug` menu / `CALL STACK` |
| **`⏺ Record`** / **`↶ Step Back`** / **`⏮ Rev Continue`** | Toggle Process Record and step or continue backward | `record full`, `-exec-next --reverse`, `-exec-continue --reverse` | Toolbar |
| **`■ Stop`** / **`✕ Terminate Session`** | Stop session or detach attached processes | `-target-detach` / terminate | Toolbar |

---

## 2. Command Palette, Navigation, and Perspectives

| Shortcut / Prefix | Action | Scope |
| :--- | :--- | :--- |
| `Ctrl+Shift+P` | Open **Command Palette** filtered to commands (`>` prefix) | Global (`View` menu) |
| `Ctrl+P` | Open **Quick Open** unified fuzzy palette (files, commands, symbols, ranks) | Global (`File` menu) |
| `>` *(in palette)* | Filter `ShortcutManager` and menu actions (e.g., `> Process Checkpoints`, `> rbreak`) | `CommandPaletteDialog` |
| `@` *(in palette)* | Jump to `clangd` or ELF64 function/struct symbols across the workspace | `CommandPaletteDialog` |
| `#` *(in palette)* | Focus a live MPI rank card or rank group | `CommandPaletteDialog` |
| `Alt+1` / `Alt+2` / `Alt+3` | Switch workspace perspective: **`CODE`** / **`REVERSE`** / **`MPI`** | Global (`MainWindow`) |
| `Alt+4` / `Alt+5` / `Alt+6` / `Alt+7` | Switch bottom tool rail: **`Debug`** / **`MPI`** / **`Terminal`** / **`Docs`** | Global (`MainWindow`) |
| `Ctrl+M` | Maximize or restore bottom console dock height | Global (`MainWindow`) |
| `Ctrl+Shift+R` | Redock all floating dock panels back to home slots | Global (`View` menu) |
| `Ctrl+W, H` | Focus central source editor (`EditorTabManager`) | Global 2-key chord |
| `Ctrl+W, L` | Focus `Project / Symbols` dock (`ProjectExplorerWidget`) | Global 2-key chord |
| `Ctrl+G` / `Ctrl+F` | Focus toolbar address/symbol seek input (`#cutterSeekBar`) | Global (`MainWindow`) |
| `Ctrl+Shift+F` | Open Workspace Search across symbols, `.rodata` strings, and disassembly | Global (`MainWindow`) |
| `Alt+Left` / `Alt+Right` | Navigate backward / forward in address history | Global (`MainWindow`) |
| `Ctrl+B` | Build active target (`Meson`, `CMake`, `Make`, or standalone `mpicxx`/`mpicc`) | Global (`Tools` menu) |
| `Ctrl+,` | Open 8-page `PreferencesDialog` | Global (`Edit` menu) |
| `F1` | Look up symbol or x86_64 mnemonic under cursor in `Reference Manual` | Global / Disassembly |

---

## 3. Floating Analysis and Diagnostic Windows

| Shortcut | Window / Tool | Menu Path | Key Capabilities |
| :--- | :--- | :--- | :--- |
| `Ctrl+Alt+P` | **Attach to Process / Remote Target** (`AttachProcessDialog`) | `File -> Attach to Process...` | `/proc` MPI job grouping (`OMPI`/`PMI`/`SLURM`), `ptrace_scope` banner, `gdbserver`/`qemu-user` port ranges |
| `Ctrl+Alt+C` | **Programmer's Calculator** (`ProgrammerCalculatorDialog`) | `Tools -> Programmer's Calculator...` | Synchronized `HEX`/`DEC`/`OCT`/`BIN`, IEEE-754 `float`/`double`, `8/16/32/64-bit` width, `BSWAP`, 64-bit bit-toggle grid, radix keypad |
| `Ctrl+Alt+G` | **Array & Field Plotter** (`ArrayPlotDialog`) | `Tools -> Array & Field Plotter...` | Live auto-refreshing 1D line/stem and 2D heatmap plots (`Viridis`, `Plasma`, `Cool-Warm`, `Grayscale`), `NaN`/`Inf` flags, previous-stop ghost delta, CSV/PNG/`gnuplot` |
| `Ctrl+Alt+S` | **Signals & Catchpoints** (`SignalCatchpointDialog`) | `Debug -> Signals & Catchpoints...` | Per-signal `Stop`/`Print`/`Pass` matrix (`handle <SIG>`), C++ `throw`/`rethrow`/`catch` regex, Linux syscalls (`mmap`, `openat`, `clone3`, `ioctl`), `dlopen`/`dlclose` |
| `Ctrl+Alt+K` | **Process Checkpoints** (`CheckpointDialog`) | `Debug -> Process Checkpoints...` | Copy-on-write GDB `checkpoint`/`restart`/`delete checkpoint` with `/proc/[pid]/fd` & `/proc/[pid]/maps` IPC/RDMA/GPU safety scan (`Ctrl+Shift+K` inside dialog creates snapshot) |
| Menu only | **Pointer & Struct Graph Visualizer** (`PointerGraphDialog`) | `Tools -> Pointer & Struct Graph Visualizer...` | Persistent pinned roots across stops, `"old -> new"` field mutation diffs, `➔ Deref`, `nullptr` guards, cycle/alias detection |

---

## 4. Source Editor, Breakpoint Lifecycle, and Watchpoints

| Shortcut / Gesture | Action | Notes |
| :--- | :--- | :--- |
| `Ctrl+S` / `Ctrl+Shift+S` | Save current file / Save all modified tabs (`●` indicator) | `EditorTabManager` |
| `Ctrl+L` / `Ctrl+/` | Go to line / Toggle `// ` line comment | `SourceCodeView` |
| `Ctrl+Space` / `F12` | Trigger `clangd` completion popup / Go to Definition | `SourceCodeView` |
| `Ctrl+MouseWheel` | Zoom editor font size (`6pt .. 72pt`) | `SourceCodeView` |
| `Ctrl+LeftClick` or `DoubleClick` | Pin identifier to `WATCH` (`DifferentialGrid`) dock | `SourceCodeView` |
| `F2` | Rename selected file or folder | `Project / Symbols` (`FILES`) |
| `Alt+B` | Toggle breakpoint at current cursor line | Global (`ShortcutManager`) |
| Left-Click Gutter | Toggle standard All-Ranks breakpoint (`●`) | `SourceCodeView` |
| Right-Click Gutter | Open line-anchored `ConditionalBreakpointDialog` popover | `SourceCodeView` |
| `Shift+Right-Click` Gutter | Open quick gutter menu (`Disable`/`Enable`, `Temporary tbreak`, `Run to Cursor`, `Jump`) | `SourceCodeView` |
| Uncheck **`Enabled`** (popover) | Non-destructively disable breakpoint (`-break-disable`) as hollow gutter ring | Preserves condition, ignore count, rank filter, and commands |
| **`Remove Breakpoint (F9)`** | Permanently delete breakpoint | `ConditionalBreakpointDialog` |
| **`Temporary (one-shot tbreak)`** | Insert one-shot breakpoint (`-break-insert -t`), dashed ring + `T` badge | Auto-clears on hit (`=breakpoint-deleted`) |
| `Debug -> Break on Function Regex (rbreak)...` | Match functions by regex via `rbreak <pattern>` | Also via Command Palette `> rbreak` |
| **`GDB Commands on Hit`** (popover) | Attach multi-line `commands ... end` script via `-break-commands <id>` | Cleared when left empty |

---

## 5. GDB Console Readline Shortcuts (`GdbConsoleWidget`)

| Shortcut | Behavior |
| :--- | :--- |
| `Up` / `Down` | Navigate deduplicated persistent command history (`~/.config/gridlock/gdb_history`), restoring unsent draft text when returning to the bottom |
| `Ctrl+R` | Open inline `(reverse-i-search)` bar; type to filter history live, press `Ctrl+R` again to cycle older matches |
| `Tab` / `Right` / `Left` / `Enter` *(in reverse search)* | Accept the matched history entry into the command line (`Enter` accepts and executes) |
| `Esc` / `Ctrl+G` *(in reverse search)* | Cancel reverse search and restore the original input text |
| `Tab` | Query GDB `complete <prefix>` (with local fallback across common commands) and open the completion popup; `Up`/`Down` selects, `Enter` inserts, `Esc` dismisses |

---

## 6. Disassembly and 2D CFG Controls (`DisassemblyView`)

| Shortcut / Gesture | Action | Mode |
| :--- | :--- | :--- |
| `Space` | Toggle between Linear Assembly and 2D Control Flow Graph (`CfgGraphView`) | `DisassemblyView` |
| `Space` + Left-Drag / Middle-Drag | Pan the 2D CFG canvas | 2D CFG |
| `Left` / `Right` / `Home` | Pan CFG horizontally (`±48px`) / Snap viewport back to active basic block | 2D CFG |
| `X` | Open `XrefInspectorDialog` (`XREFS TO` and `XREFS FROM`) for selected address | `DisassemblyView` |
| `Alt+P` | Patch instruction at active address (default `nop`) and update ELF64 image | `DisassemblyView` |
| Right-Click Instruction | `Run to Instruction Address`, `Jump / Set PC to Address`, `Open Address in Programmer's Calculator`, `Seek in Memory View`, `Inspect Cross-References (X)`, `Lookup in Reference Manual (F1)`, `Toggle Bookmark`, `Patch Instruction` | Linear / CFG |
