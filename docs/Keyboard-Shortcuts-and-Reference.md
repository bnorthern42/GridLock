# Keyboard Shortcuts and Quick Reference

Compact lookup reference for GridLock (`v0.8.8`) execution controls, Search Everywhere scopes, floating diagnostic windows, breakpoint lifecycle actions, editor/terminal shortcuts, GDB console readline bindings, and disassembly/CFG navigation.

> [!TIP]
> Every global and editor binding listed below can be re-recorded interactively in **Edit -> Preferences -> Keybindings** (`KeySequenceRecorderWidget`), and toolbar button order/spacing can be customized in **Preferences -> Appearance -> Toolbar**.

---

## 1. Global Execution and Stepping Controls

| Shortcut / Control | Action | Backend Command | Scope |
| :--- | :--- | :--- | :--- |
| `Alt+R` / **`▶ Run`** | Launch parallel debug session (with pre-launch build & `+x` guard) | MPMD `mpirun` + GDB/MI3 or DAP `launch` | Global |
| `F5` / **`▶ Cont`** | Continue execution (`All Ranks` or focused `Rank k`) | `-exec-continue` / DAP `continue` | Global |
| `F6` / **`⏸ Pause`** | **Pause All Ranks** (`haltAll`) or focused rank | `-exec-interrupt` / `SIGINT` | Global / Toolbar |
| `F10` / **`Over`** | Step Over source line (or Switch Header/Source when idle) | `-exec-next` / DAP `next` | Global / Editor |
| `F11` / **`Into`** | Step Into function call | `-exec-step` / DAP `stepIn` | Global |
| `Shift+F11` / **`Out`** | Step Out of current function frame | `-exec-finish` / DAP `stepOut` | Global |
| **`Into ▾ -> Step Instruction`** | Step Into one machine instruction (`stepi`) | `-exec-step-instruction` | Toolbar / Disassembly |
| `Ctrl+Shift+F10` | Step Over one machine instruction (`nexti`) | `-exec-next-instruction` | Global (`Debug` menu) |
| `Ctrl+F10` | **Run to Cursor** (active editor line or disassembly address) | `-exec-until "<file>:<line>"` / `-exec-until *0x<addr>` | Global (`Debug` menu) |
| `Debug -> Set Next Statement / Jump...` or drag gutter `▶` PC arrow | Move execution PC with lexical scope hazard check | `-exec-jump "<file>:<line>"` / `-exec-jump *0x<addr>` | `Debug` menu / Gutter / Disassembly |
| `Debug -> Force Early Return...` or `CALL STACK` right-click | Return immediately from current frame (optional return expr) | `-exec-return [expr]` | `Debug` menu / `CALL STACK` |
| **`● REC`** / `Step Back` / `Rev Continue` | Toggle Process Record and step or continue backward | `record full`, `-exec-next --reverse`, `-exec-continue --reverse` | Toolbar |
| **`■ Stop`** | Stop session or detach attached processes cleanly | `-target-detach` / terminate | Toolbar |

---

## 2. Search Everywhere, Navigation, and Perspectives

| Shortcut / Prefix | Action | Scope |
| :--- | :--- | :--- |
| `Double Shift` | Open **Search Everywhere** (`All` scope with 35-line context preview) | Global (`MainWindow`) |
| `Ctrl+P` | Open **Search Everywhere** pre-focused on **`Files`** | Global (`File` menu) |
| `Ctrl+Shift+P` | Open **Search Everywhere** pre-focused on **`Actions`** (`>` prefix) | Global (`View` menu) |
| `Tab` / `Shift+Tab` *(in Search Everywhere)* | Cycle across `All | Files | Symbols | Text | Actions | Ranks` scope tabs | `CommandPaletteDialog` |
| `Enter` / `Ctrl+Enter` *(in Search Everywhere)* | Open selected file/symbol in active editor / split editor pane | `CommandPaletteDialog` |
| `>` / `@` / `/` / `#` *(in Search Everywhere)* | Prefix shortcuts for Actions (`>`), Symbols (`@`), Live Text Grep (`/`), and MPI Ranks (`#`) | `CommandPaletteDialog` |
| **`S` / `M` / `L` / `XL` Pill** *(in Search Everywhere)* | Cycle popup size preset (`Small`, `Medium`, `Large`, `X-Large`) | `CommandPaletteDialog` |
| `Alt+1` / `Alt+2` / `Alt+3` / `Alt+4` | Switch workspace perspective: **`SOURCE`** / **`DEBUG`** / **`BINARY`** / **`MPI`** | Global (`MainWindow`) |
| `Ctrl+M` | Maximize or restore bottom console dock height | Global (`MainWindow`) |
| `Ctrl+Shift+R` | Redock all floating dock panels back to home slots | Global (`View` menu) |
| `Ctrl+W, H` / `Ctrl+W, L` | Focus central source editor / Focus `Project / Symbols` dock | Global 2-key chords |
| `Ctrl+F` | Open inline Find bar in active `SourceCodeView` or `GdbConsoleWidget` | Editor / Console |
| `Ctrl+G` | Focus address/symbol seek input (`#cutterSeekBar`) | Global (`MainWindow`) |
| `Ctrl+Shift+F` | Open Workspace Search across symbols, `.rodata` strings, and disassembly | Global (`MainWindow`) |
| `Alt+Left` / `Alt+Right` | Navigate backward / forward in address history | Global (`MainWindow`) |
| `Ctrl+B` | Build active target (`Meson`, `CMake`, `Make`, or standalone `mpicxx`/`mpicc`) | Global (`Tools` menu) |
| `Ctrl+,` | Open hierarchical `PreferencesDialog` (`Appearance -> Toolbar`, `Keybindings`, etc.) | Global (`Edit` menu) |
| `F1` | Look up symbol or x86_64 mnemonic under cursor in `Reference Manual` | Global / Disassembly |

---

## 3. Floating Analysis and Diagnostic Windows

| Shortcut | Window / Tool | Menu Path | Key Capabilities |
| :--- | :--- | :--- | :--- |
| `Ctrl+Alt+P` | **Attach to Process / Remote Target** (`AttachProcessDialog`) | `File -> Attach to Process...` | `/proc` MPI job grouping (`OMPI`/`PMI`/`SLURM`), `ptrace_scope` banner, `gdbserver`/`qemu-user` port ranges |
| `Ctrl+Alt+C` | **Programmer's Calculator** (`ProgrammerCalculatorDialog`) | `Tools -> Programmer's Calculator...` | Synchronized `HEX`/`DEC`/`OCT`/`BIN`, IEEE-754 `float`/`double`, `8/16/32/64-bit` width, `BSWAP`, 64-bit bit-toggle grid, radix keypad |
| `Ctrl+Alt+G` | **Array & Field Plotter** (`ArrayPlotDialog`) | `Tools -> Array & Field Plotter...` | Multi-rank 1D overlays, `start:end:stride` slicing, `Log10` scale, `Complex64/128`, 2D heatmaps, virtualized 2D/3D C/Fortran tensor tables, CSV/PNG/`gnuplot` |
| `Ctrl+Alt+S` | **Signals & Catchpoints** (`SignalCatchpointDialog`) | `Debug -> Signals & Catchpoints...` | Per-signal `Stop`/`Print`/`Pass` matrix (`handle <SIG>`), C++ `throw`/`rethrow`/`catch` regex, Linux syscalls (`mmap`, `openat`, `clone3`, `ioctl`), `dlopen`/`dlclose` |
| `Ctrl+Alt+K` | **Process Checkpoints** (`CheckpointDialog`) | `Debug -> Process Checkpoints...` | Copy-on-write GDB `checkpoint`/`restart`/`delete checkpoint` with `/proc/[pid]/fd` & `/proc/[pid]/maps` IPC/RDMA/GPU safety scan (`Ctrl+Shift+K` inside dialog creates snapshot) |
| Menu / Right-Click | **Pointer & Struct Graph Visualizer** (`PointerGraphDialog`) | `Tools -> Pointer & Struct Graph Visualizer...` | Sugiyama DAG layout, pinned roots, `"old -> new"` mutation diffs, `Shift+Click` card width toggle, full-value hover tooltips, double-click `Deref`, corner minimap, `2x` PNG/SVG export |

---

## 4. Source Editor, Breakpoint Lifecycle, and Watchpoints

| Shortcut / Gesture | Action | Notes |
| :--- | :--- | :--- |
| `Ctrl+S` / `Ctrl+Shift+S` | Save current file / Save all modified tabs (`●` indicator) | `EditorTabManager` |
| `Tab` / `Shift+Tab` | Indent / unindent selection or line by exact `tabWidth` spaces (default `4`) | `SourceCodeView` |
| `Ctrl+F` / `Ctrl+L` / `Ctrl+/` | Inline Find bar / Go to line / Toggle `// ` line comment | `SourceCodeView` |
| `Ctrl+Space` / `F12` | Trigger `clangd` completion popup / Go to Definition | `SourceCodeView` |
| `Ctrl+MouseWheel` | Zoom editor font size (`6pt .. 72pt`) | `SourceCodeView` |
| Rebindable (`Keybindings`) | Toggle `CodeMinimap` visibility in `SourceCodeView` | `Preferences -> Editing / Keybindings` |
| `Ctrl+LeftClick` or `DoubleClick` | Pin identifier to `WATCH` (`DifferentialGrid`) dock | `SourceCodeView` |
| `F2` | Rename selected file or folder | `Project / Symbols` (`FILES`) |
| `Alt+B` | Toggle breakpoint at current cursor line | Global (`ShortcutManager`) |
| Left-Click Gutter | Toggle standard All-Ranks breakpoint (`●`) | `SourceCodeView` |
| Right-Click Gutter | Open line-anchored `ConditionalBreakpointDialog` popover | `SourceCodeView` |
| `Shift+Right-Click` Gutter | Open quick gutter menu (`Disable`/`Enable`, `Temporary tbreak`, `Run to Cursor`, `Jump`) | `SourceCodeView` |
| Uncheck **`Enabled`** (popover) | Non-destructively disable breakpoint (`-break-disable`) as hollow gutter ring | Preserves condition, ignore count, rank filter, and commands |
| **`Remove Breakpoint (F9)`** | Permanently delete breakpoint | `ConditionalBreakpointDialog` |
| **`Temporary (one-shot tbreak)`** | Insert one-shot breakpoint (`-break-insert -t`), dashed ring + `T` badge | Auto-clears on hit (`=breakpoint-deleted`) |
| `Debug -> Break on Function Regex (rbreak)...` | Match functions by regex via `rbreak <pattern>` | Also via Search Everywhere `> rbreak` |
| **`GDB Commands on Hit`** (popover) | Attach multi-line `commands ... end` script via `-break-commands <id>` | Cleared when left empty |

---

## 5. GDB Console Readline & Embedded Terminal Shortcuts

| Shortcut | Behavior |
| :--- | :--- |
| `Up` / `Down` | Navigate deduplicated persistent command history (`~/.config/gridlock/gdb_history`), restoring unsent draft text when returning to the bottom |
| `Ctrl+R` | Open inline `(reverse-i-search)` bar; type to filter history live, press `Ctrl+R` again to cycle older matches |
| `Tab` / `Right` / `Left` / `Enter` *(in reverse search)* | Accept the matched history entry into the command line (`Enter` accepts and executes) |
| `Esc` / `Ctrl+G` *(in reverse search)* | Cancel reverse search and restore the original input text |
| `Tab` *(in `GdbConsoleWidget`)* | Query GDB `complete <prefix>` (with local fallback across common commands) and open the completion popup |
| `Ctrl+Shift+C` / `Ctrl+Shift+V` *(in `Terminal`)* | Copy selected PTY text / Paste clipboard into `TerminalDockWidget` (smart `Ctrl+C` copies when text is selected and sends `SIGINT` otherwise) |

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

> [!WARNING]
> **Binary Patching (`Alt+P`)**: Patching instructions updates the loaded ELF64 image buffer and marks the site `; [PATCHED]`. Use right-click **`Revert All Patches`** to restore original bytes before calling `savePatchedBinary` if an instruction replacement changes branch offsets or byte lengths unintentionally.
