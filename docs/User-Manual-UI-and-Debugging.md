# User Manual: UI and Debugging Reference

This manual documents every workspace perspective, menu action, toolbar control, dock widget, execution control, breakpoint lifecycle feature, signal/catchpoint manager, copy-on-write checkpoint tool, and keyboard shortcut in GridLock (`v0.8.7`). For a compact lookup table, see [Keyboard Shortcuts and Quick Reference](Keyboard-Shortcuts-and-Reference.md).

![GridLock Code Perspective Overview](screenshots/workspace_overview.png)

---

## 1. Workspace Perspectives (`Alt+1`, `Alt+2`, `Alt+3`) and Command Palette (`Ctrl+Shift+P` / `Ctrl+P`)

Switch perspectives from the right side of the main toolbar (`CODE | REVERSE | MPI`) or via keyboard shortcuts:

* **`CODE` (`Alt+1`)**: Default side-by-side layout pairing the multi-tab source editor (`EditorTabManager` / `SourceCodeView`) on the left with `DisassemblyView` on the right, flanked by `Project / Symbols` on the far left and `Variables`, `Registers`, `WATCH`, `CALL STACK`, and `Server Rack` on the right.
* **`REVERSE` (`Alt+2`)**: Collapses the source editor and gives the central viewport to `DisassemblyView` in 50/50 **`SPLIT (CFG+DECOMP)`** mode (2D basic-block Control Flow Graph paired with the pseudo-C/C++ decompiler).
* **`MPI` (`Alt+3`)**: Activates the `MPI` group on the bottom tool rail, switches `BottomConsoleTabs` to **`MPI Diagnostics`**, and expands the bottom dock for cluster stall inspection.

Press **`Ctrl+M`** at any time to toggle maximizing and restoring the height of the bottom console dock.

### Unified Fuzzy Command Palette (`CommandPaletteDialog`)

* **`Ctrl+Shift+P`** (`View -> Command Palette...`): Opens the keyboard launcher pre-filtered with `>` for commands and actions (`Debug: Run to Cursor`, `Debug: Break on Function Regex (rbreak)...`, `Process Checkpoints...`, `Signals & Catchpoints...`, `Programmer's Calculator`, `Array & Field Plotter...`, perspective switches, and themes).
* **`Ctrl+P`** (`File -> Quick Open`): Opens the unified fuzzy palette across workspace files, commands, symbols, themes, and ranks.
* **Domain Prefixes**: Type `>` for menu/shortcut actions, `@` for `clangd` and ELF64 symbols, or `#` to jump directly to a live MPI rank. Navigate results with `Up`/`Down`, execute with `Return`, and close with `Esc`.

---

## 2. Menu Bar Reference

### `File` Menu

| Menu Action | Shortcut | Behavior |
| :--- | :--- | :--- |
| `New Project...` | - | Opens the 2-page `ProjectWizard` to configure workspace root, binary, arguments, `-np` ranks, and OpenMPI binding/output flags, then starts a session. |
| `Open Workspace...` | - | Switches active workspace root (`switchWorkspaceDirectory`), re-roots `ProjectExplorerWidget`, scans for ELF binaries and sources, and updates the embedded terminal CWD. |
| `Open Source File` | - | Opens a `.cpp`, `.hpp`, `.h`, or `.c` file in `EditorTabManager`. |
| `Quick Open...` | `Ctrl+P` | Opens `CommandPaletteDialog` in unified fuzzy search mode. |
| `Save Source File` | `Ctrl+S` | Saves the active modified buffer in `SourceCodeView`. |
| `Save All` | `Ctrl+Shift+S` | Saves all dirty tabs (`●` indicator) in `EditorTabManager`. |
| `Switch Header/Source` | `F10` | Toggles between matching `.cpp`/`.c` and `.hpp`/`.h`/`.hxx`/`.hh` files in the same directory. |
| `Save Session` / `Save Session As...` / `Load Session...` | - | Saves or restores watched variables, active breakpoints, and focused rank via `.gridlock/workspace.toml`. |
| `Recent Sessions` | - | Lists up to 10 recent workspace/session paths (`debugger.conf`) plus `Clear Recent Sessions`. |
| `Project Settings` | - | Opens `ProjectSettingsDialog` (`Target Binary`, `Program Arguments`, `Working Directory`, `MPI Ranks (-np)`, `MPI Args`, and Programmer's Calculator defaults). |
| `Attach to Process...` | `Ctrl+Alt+P` | Opens the two-tab `AttachProcessDialog` (`Local Processes (/proc)` and `Remote Target (gdbserver / QEMU)`). |
| `Submit SLURM Job` | - | Substitutes `{FILE}` in the configured SLURM template and dispatches a preview submission to `MockHpcBackend`. |
| `Exit` | - | Closes GridLock (prompts on unsaved editor tabs and, if `Confirm Quit` is `Yes`, confirms exit). |

### `Edit` and `View` Menus

| Menu Action | Shortcut | Behavior |
| :--- | :--- | :--- |
| `Edit -> Undo` / `Redo` / `Cut` / `Copy` / `Paste` / `Select All` | `Ctrl+Z` / `Ctrl+Shift+Z` / `Ctrl+X` / `Ctrl+C` / `Ctrl+V` / `Ctrl+A` | Standard edit operations on the focused text editor. |
| `Edit -> Find in View...` / `Go to Line...` / `Toggle Comment` | `Ctrl+F` / `Ctrl+L` / `Ctrl+/` | Seek bar focus, line jump, and `// ` line comment toggle. |
| `Edit -> Go to Definition` | `F12` *(in editor)* | Resolves the symbol under the cursor via `ClangdLspClient` (`textDocument/definition`). |
| `Edit -> Preferences` | `Ctrl+,` | Opens the 8-page `PreferencesDialog`. |
| `View -> Command Palette...` | `Ctrl+Shift+P` | Opens `CommandPaletteDialog` in command mode (`>`). |
| `View -> Toolbars` (`Main Toolbar`, `Address Space Navigation`, `Reset Toolbars`) | - | Toggles or resets top toolbars. |
| `View -> Split Editor Right` / `Split Editor Down` / `Close Split` | - | Splits `EditorTabManager` horizontally or vertically and closes secondary panes. |
| `View -> Redock All Floating Panels` | `Ctrl+Shift+R` | Docks all floating `QDockWidget` panels back into their home slots. |
| `View -> Project / Symbols`, `Variables`, `Registers`, `WATCH`, `Server Rack` | - | Checkable visibility toggles for each dock panel. |

### `Debug`, `Tools`, and `Help` Menus

| Menu Action | Shortcut | Behavior |
| :--- | :--- | :--- |
| `Debug -> Step Over Instruction (nexti)` | `Ctrl+Shift+F10` | Steps over a single machine instruction (`-exec-next-instruction`), treating `call` instructions atomically. |
| `Debug -> Run to Cursor` | `Ctrl+F10` | Runs the focused rank until the active editor line (`-exec-until "<file>:<line>"`) or focused disassembly address (`-exec-until *0x<addr>`). |
| `Debug -> Set Next Statement / Jump...` | - | Relocates execution PC (`-exec-jump`) to the active or prompted source line after running lexical scope safety checks. |
| `Debug -> Force Early Return...` | - | Forces immediate return from the active stack frame (`-exec-return [expr]`) with an optional return expression. |
| `Debug -> Break on Function Regex (rbreak)...` | - | Installs GDB regex function breakpoints (`rbreak <pattern>`) across matching symbols. |
| `Debug -> Process Checkpoints...` | `Ctrl+Alt+K` | Opens floating `CheckpointDialog` for copy-on-write GDB `checkpoint`/`restart` snapshots with `/proc` safety checks. |
| `Debug -> Signals & Catchpoints...` | `Ctrl+Alt+S` | Opens `SignalCatchpointDialog` for per-signal `handle <SIG>` dispositions and C++/syscall/`dlopen` catchpoints. |
| `Tools -> Build Target` / `Clean Target` / `Rebuild Target` | `Ctrl+B` / - / - | Auto-detects `Meson`, `CMake`, `Make`, or standalone `mpicxx`/`mpicc`. |
| `Tools -> Pointer & Struct Graph Visualizer...` | - | Opens floating `PointerGraphDialog` (`1100x720`) with persistent pinned roots. |
| `Tools -> Array & Field Plotter...` | `Ctrl+Alt+G` | Opens floating `ArrayPlotDialog` for live auto-refreshing 1D/2D array and heatmap plotting. |
| `Tools -> Programmer's Calculator...` | `Ctrl+Alt+C` | Opens floating `ProgrammerCalculatorDialog` (`HEX`/`DEC`/`OCT`/`BIN`/IEEE-754/64-bit grid). |
| `Tools -> Run Tests` / `Help -> About GridLock` | - | Triggers `--test-mode` automation runner or displays version metadata. |

---

## 3. Main Toolbar, Non-Linear Execution Control, and Address Ribbon

### Row 1: `Main Toolbar` (Left to Right)

1. **`TARGET:` (`#toolbarTargetSelector`)**: Lists the bundled MPI showcase programs (or workspace ELF/source targets) plus `Open ELF / Binary...`.
2. **`RANKS (-np):` (`#toolbarRankSpin`)**: MPI world size (`1..256`, default `8`).
3. **`⚙ Build`** and **`✕ Clean`**: Compile or clean the selected target/workspace.
4. **`▶ Run Target`** (changes to **`● Running`** while active): Compiles on demand if needed and starts the debugging session.
5. **`⏭ Continue`**, **`↷ Step Inst`**, **`⏸ Pause Rank`**, **`■ Stop`**, **`✕ Terminate Session`**: Session execution controls (enabled only when a debug session is active).
6. **`<`** and **`>`**: Backward and forward address history navigation (`Alt+Left` / `Alt+Right`).
7. **Seek Bar (`Go to address / symbol (Ctrl+G)...`)**: Accepts hex addresses (`0x40113d`), symbol names (`main`, `MPI_Send`), or register expressions (`$rsp`).
8. **`CODE` | `REVERSE` | `MPI`**: Workspace perspective buttons (`Alt+1` / `Alt+2` / `Alt+3`).
9. **`⏺ Record`**, **`↶ Step Back`**, **`⏮ Rev Continue`**: Time-travel reverse debugging controls (`record full`, `-exec-next --reverse`, `-exec-continue --reverse` on GDB; `stepBack` / `reverseContinue` on DAP).

### Non-Linear Execution Controls (`v0.8.7`)

* **Instruction Step-Over (`nexti`, `Ctrl+Shift+F10`)**: Available in the `Debug` menu, Command Palette, and `DisassemblyView` toolbar (`nexti` alongside `stepi`). Issues `-exec-next-instruction` on GDB (`next` with instruction granularity on DAP).
* **Run to Cursor (`Ctrl+F10`)**: Executes until the active cursor line in `SourceCodeView` (`-exec-until "<file>:<line>"`) or the selected instruction address in `DisassemblyView` (`-exec-until *0x<addr>`). Also available via right-click in both views.
* **Draggable Gutter PC Arrow & Set Next Statement / Jump (`-exec-jump`)**: Drag the active execution `▶` arrow vertically inside `SourceCodeView`'s gutter to another line (or use `Debug -> Set Next Statement / Jump...` or right-click **`Set Next Statement / Jump Here`**). Before executing `-exec-jump`, `MainWindow::checkJumpLexicalScopeHazard` checks whether the jump crosses function boundaries, enters/exits lexical `{}` scopes, or skips local variable declarations and asks for confirmation if a hazard is detected.
* **Force Early Return (`-exec-return`)**: Choose `Debug -> Force Early Return...` or right-click any row in **`CALL STACK`** to force an immediate return from the active frame, optionally supplying a C/C++ return expression (leave empty for `void` functions).

### Row 2: `Address Space Navigation` Ribbon (`22px`)

Displays proportional clickable segments for the loaded ELF64 binary: **`.plt`**, **`.text`** (annotated with `sym.main`), **`.rodata`**, **`.data`** (or **`.bss`**), plus a live **`stack 0x<rsp>`** segment while a debugging session is active. Clicking any segment seeks both `DisassemblyView` and `MemView` to that section's base virtual address.

---

## 4. Source Editor, `clangd` LSP, and Project Explorer

### `Project / Symbols` Dock (`ProjectExplorerWidget`)

Switch between three modes at the top of the left dock, all sharing a live wildcard/substring filter bar:
* **`FILES`**: Workspace file tree with `"Open Folder"` and Collapse-All buttons. Double-click a file to open it; press **`F2`** to rename; right-click for **`New Source File (.cpp)...`**, **`New Header File (.hpp)...`** (generates `#pragma once` stub), **`New Folder...`**, **`Rename...`**, and **`Delete...`** (with optional automatic source registration in `meson.build` or `CMakeLists.txt`).
* **`SYMBOLS`**: Sortable 3-column table (`SYMBOL`, `ADDRESS`, `SIZE`) of up to 800 `STT_FUNC` and `STT_OBJECT` symbols extracted by `ElfBinaryInspector`. Double-click any row to seek to that symbol.
* **`STRINGS`**: Sortable 3-column table (`ADDRESS`, `SECTION`, `STRING`) of up to 500 printable ASCII strings (`>= 4` chars) from `.rodata`, `.dynstr`, and `.interp`.

### `SourceCodeView` & `EditorTabManager`

* **Scope Breadcrumbs & Git Diff Gutter**: Displays `File > Class/Struct > Function` above the editor and marks `Added` (green), `Modified` (blue), and `Deleted` (red triangle) lines against `HEAD` on the right edge of the line-number gutter.
* **Multi-Language & Rainbow Bracket Highlighting (`EditorSyntaxHighlighter`)**: Highlights C/C++/CUDA (including bold purple `MPI_*` calls), `TOML`, `YAML`, `Markdown`, and `CSV`/`TSV` (6-color alternating column rotation), with 5-level cyclic rainbow brackets (`()`, `{}`, `[]`) and red underlines on unmatched closing brackets.
* **`clangd` LSP Integration (`ClangdLspClient`)**:
  * Automatically locates `compile_commands.json` across `.`, `builddir/`, `build/`, `build_meson/`, and `build_cmake/`.
  * Draws wavy error (`#f7768e`) and warning (`#e0af68`) underlines from `textDocument/publishDiagnostics`.
  * Triggers `LspCompletionPopup` on `.`, `->`, `::`, or **`Ctrl+Space`**.
  * Shows type/Doxygen hover cards (`HoverWidget`) after a `400 ms` hover, jumps to definitions on **`F12`**, and supports `findReferences` and AST-safe `renameSymbol`.
* **Inline Variable Ghost Values & Quick Watch Pinning**: When paused, appends muted italic `// var = val` annotations (toggle in `Preferences -> Editing`). **`Ctrl+LeftClick`** or **`DoubleClick`** any identifier to pin it to `WATCH` (`DifferentialGrid`).

---

## 5. Breakpoint Lifecycle, Signals/Catchpoints, Checkpoints, and Watchpoints

### Gutter Interactions & `ConditionalBreakpointDialog`

![Cursor-Anchored Conditional Breakpoint Popover at line 34 of deadlock_ring.cpp](screenshots/breakpoint_popover.png)

* **Left-Click Gutter**: Toggles an unconditional All-Ranks breakpoint (`●` crimson circle with white ring).
* **`Shift+Right-Click` Gutter**: Opens a quick gutter context menu (`Disable Breakpoint` / `Enable Breakpoint`, `Insert Temporary Breakpoint (tbreak)`, `Run to Cursor`, `Set Next Statement / Jump Here`).
* **Right-Click Gutter**: Opens the cursor-anchored **`ConditionalBreakpointDialog`** popover:
  * **Non-Destructive `Enabled` Toggle (`-break-enable` / `-break-disable`)**: Unchecking **`Enabled`** keeps the breakpoint along with its condition, ignore count, rank filter, and command list, rendering a **hollow red/amber ring marker** (`[DISABLED]` in hover tooltip) instead of deleting it. Use **`Remove Breakpoint (F9)`** to permanently delete a breakpoint.
  * **Temporary One-Shot Breakpoints (`Temporary (one-shot tbreak)`)**: Inserts `-break-insert -t`, renders a **dashed cyan ring with a `T` glyph**, and automatically removes the gutter marker when triggered (`=breakpoint-deleted`).
  * **`Suspend: All | Rank / Thread`**: Choose **`All`** (`rankFilter = -1`) or **`Rank / Thread`** (focused rank only, rendered with a per-rank badge). Click **`Make Default`** to persist your preferred scope.
  * **`Condition:` & `Ignore:`**: Enter any C/C++ boolean expression with live `clangd` virtual-buffer autocomplete (or **`Presets ▾`**), and optional ignore counts (`0..10,000,000`) via `-break-insert -i N` / `-break-after`.
  * **`Logpoint:`**: Logs `[LOGPOINT Rank r] ...` with `{identifier}` substitution into **`Program Output`** and immediately resumes (`-exec-continue`), rendered as an amber diamond with a cyan border.
  * **`GDB Commands on Hit (commands ... end):`**: Multi-line script box dispatched via `-break-commands <id> "cmd1" "cmd2"` (clearing the editor sends `-break-commands <id>` to remove attached commands).
* **Regex Symbol Breakpoints (`rbreak`)**: Choose **`Debug -> Break on Function Regex (rbreak)...`** (or Command Palette `> rbreak`), enter a regex such as `MPI_.*|Solver::step.*`, and GridLock issues `-interpreter-exec console "rbreak <pattern>"` and tracks newly created `=breakpoint-created` records.

### Signal Disposition Matrix and Catchpoints (`SignalCatchpointDialog`, `Ctrl+Alt+S`)

Open **`Debug -> Signals & Catchpoints...`** (`Ctrl+Alt+S`) to manage runtime signal handling and event catchpoints across all active ranks:
* **Tab 1 (`Signal Disposition Matrix`)**: Filterable table of standard POSIX and HPC fabric signals (`SIGFPE`, `SIGPIPE`, `SIGUSR1`, `SIGUSR2`, `SIGBUS`, `SIGSEGV`, `SIGABRT`, `SIGALRM`, `SIGINT`, `SIGTERM`, `SIGCHLD`, `SIGURG`, `SIGILL`, `SIGTRAP`) with live `Stop`, `Print`, and `Pass` checkboxes issuing `handle <SIG> <stop|nostop> <print|noprint> <pass|nopass>`, plus 1-click **`HPC Quiet Defaults`** and **`Trap Faults`** presets.
* **Tab 2 (`Catchpoints`)**:
  * **C++ Exceptions**: Install `throw` (`-catch-throw`), `rethrow` (`-catch-rethrow`), or `catch` (`-catch-catch`) catchpoints with optional exception-type regex filtering (`-r <regex>`).
  * **Linux Syscalls**: Quick-toggle preset chips (`mmap`, `openat`, `clone3`, `ioctl`, `munmap`, `brk`, `futex`, `socket`) plus custom comma-separated syscall names (`catch syscall ...`).
  * **Shared Libraries**: Install `dlopen` (`-catch-load`) or `dlclose` (`-catch-unload`) catchpoints with optional library regex filtering.

### Copy-on-Write Process Checkpoints (`CheckpointDialog`, `Ctrl+Alt+K`)

Open **`Debug -> Process Checkpoints...`** (`Ctrl+Alt+K`) to save and restore lightweight Linux copy-on-write process snapshots on a paused rank:
* **Pre-Checkpoint `/proc/[pid]/fd` & `/proc/[pid]/maps` Safety Scanner**: Before creating a fork snapshot, `inspectProcessCheckpointSafety()` inspects open file descriptors and memory mappings for the focused rank's PID and displays a color-coded status banner plus expandable `/proc` diagnostics:
  * **`SAFE`** (green): No external sockets, pipes, shared-memory IPC, RDMA verbs, or GPU device mappings detected.
  * **`WARNING: Active Sockets / Pipes / SHM`** (amber): Active `socket:[...]`, `pipe:[...]`, `eventfd`, `/dev/shm/`, `SYSV`, or writable file handles (typical for OpenMPI/MPICH ranks; safe for local single-rank forks if collective barriers are not crossed).
  * **`UNSAFE: RDMA / GPU Device Mapping Detected`** (red): Active `/dev/infiniband/*`, `uverbs`, `mlx5`, `/dev/nvidia*`, `/dev/kfd`, or `/dev/dri/` device handles/VMAs where `fork()` can trigger `EFAULT` or driver faults.
* **Create, Restore, and Delete**: Enter an optional note and click **`Create CoW Checkpoint (Ctrl+Shift+K)`** (prompts confirmation on `WARNING`/`UNSAFE`), then click **`Restore (restart)`** (`restart <id>`) to jump backward in execution and refresh frames/variables/registers, or **`Delete`** (`delete checkpoint <id>`) to reclaim a snapshot.

### Hardware Data Watchpoints (`Variables` Dock)

Right-click any scalar variable, struct member, or array element in the **`Variables`** dock and choose **`Watch Memory Write`**, **`Watch Memory Read`**, or **`Watch Memory Read/Write`** (`-break-watch [-r|-a] <expr>` / DAP `dataBreakpoints`).

---

## 6. Right-Side Inspection Docks, Bottom Tool Rail, and GDB Console Readline

### Right-Side Docks

* **`Variables` (`VariablesDockWidget`)**: Sortable 3-column tree (`Name`, `Value`, `Type`) with a `Scope:` rank selector and recursive filter box. Lazily expands C++ structs, base classes, and `std::vector` elements; highlights mutated scalars in coral (`#f38ba8`); left-clicking a `0x...` pointer reads 256 bytes in `Memory View`; right-clicking promotes to `WATCH`, opens `PointerGraphDialog`, opens `ArrayPlotDialog`, opens `ProgrammerCalculatorDialog`, or sets a hardware watchpoint.
* **`Registers` (`RegisterView`)**: Sortable 2-column table (`REG`, `VALUE / ROLE`) pinning `rip`, `rsp`, and `rbp` at the top, highlighting changed registers in bold red, filtering by register name or hex/unsigned-decimal value, and jumping to pointer targets on double-click.
* **`WATCH` (`DifferentialGrid`)**: Cross-rank expression matrix (`Rank 0 .. Rank N-1` rows by watched expression columns) highlighting minority-consensus outliers in light red (`rgb(255, 200, 200)`).
* **`CALL STACK` (`CallStackDockWidget`)**: Displays an OS/OpenMP **`Thread:`** selector (`-thread-info` / DAP `threads`) above a 4-column backtrace table (`#`, `Function`, `Location`, `Address`) up to 64 frames deep. Right-click any row for **`Force Early Return...`**.
* **`Server Rack` (`ServerRackView`)**: Wrapping grid of rank cards showing `RANK N`, state pill (`RUN`, `BRK`, `IDLE`), and current PC (`0x...`).

### Bottom Tool-Window Icon Rail (`Alt+4` .. `Alt+7`)

![Bottom Tool Rail in Debug Mode showing GDB Console, Memory View, Evaluator, and Program Output](screenshots/tool_rail_debug_tabs.png)

| Rail Button | Shortcut | Hosted Bottom Tabs |
| :--- | :--- | :--- |
| **`Debug`** | `Alt+4` | **`GDB Console`** (target rank selector `All Ranks` / `Rank N`, stream filter, `Raw MI` toggle, `MiSanitizer`-validated `gdb>` prompt with readline ergonomics), **`Memory View`** (`HEX DUMP` and `LIBRARY MEMORY MAP`), **`Evaluator`** (`DWARF / C++ EVALUATOR`), and **`Program Output`** (launch command `COPY`, rank filter, stdout/stderr & `[LOGPOINT]` stream). |
| **`MPI`** | `Alt+5` | **`MPI Diagnostics`** (`DEADLOCK ANALYZER` and `NETWORK LOG`) and **`Package Manager`** (`HpcConsoleWidget`, lazy-loaded on first tab activation). |
| **`Terminal`** | `Alt+6` | **`Terminal`** (`QTermWidget` `xterm-256color` truecolor Linux PTY, lazy-started on first activation). |
| **`Docs`** | `Alt+7` | **`Reference Manual`** (`DOCSETS` SQLite3 Zeal viewer + `MAN PAGES` `man -Thtml` viewer, lazy-indexed on first activation or `F1`). |

### GDB Console Readline Ergonomics (`GdbConsoleWidget`)

* **Persistent Command History (`Up` / `Down`)**: Deduplicated command history persists across sessions in `~/.config/gridlock/gdb_history`. Pressing `Up` / `Down` walks history while preserving any unsent draft line when returning to the bottom.
* **Incremental Reverse Search (`Ctrl+R`)**: Opens an inline `(reverse-i-search)` bar filtering command history live as you type; press `Ctrl+R` repeatedly to cycle older matches, `Tab`/`Right`/`Enter` to accept, or `Esc`/`Ctrl+G` to cancel and restore your original input.
* **Live `complete` Tab Completion (`Tab`)**: Queries GDB's `complete <prefix>` via `GdbRankCoordinator` (with local fallback across common commands when offline) and opens an interactive completion popup (`Up`/`Down` to select, `Enter` to insert, `Esc` to close).

---

## 7. Quick Shortcut Summary

See [Keyboard Shortcuts and Quick Reference](Keyboard-Shortcuts-and-Reference.md) for the complete categorized tables covering execution (`F5`, `F10`, `F11`, `Ctrl+F10`, `Ctrl+Shift+F10`), palettes (`Ctrl+Shift+P`, `Ctrl+P`, `>`, `@`, `#`), floating tools (`Ctrl+Alt+P/C/G/S/K`), perspectives (`Alt+1..3`, `Alt+4..7`), editor/LSP (`Ctrl+Space`, `F12`, `F9`), GDB console readline (`Up`/`Down`, `Ctrl+R`, `Tab`), and disassembly/CFG (`Space`, `X`, `Alt+P`, `Home`).
