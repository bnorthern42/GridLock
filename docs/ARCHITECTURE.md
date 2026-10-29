# GridLock Workstation Architecture & Modularity Guide

## 1. Four-Zone Workstation Layout

GridLock organizes all parallel MPI debugging and binary analysis tools into a deterministic 4-zone docking and splitter hierarchy inside `MainWindow`:

```mermaid
flowchart TD
    MW["MainWindow (1440x900 Default)"]
    TB["Top Zone: MainToolbar + AddressNavToolbar (22px Section Ribbon)"]
    LEFT["Left Dock Area: ProjectExplorerWidget (FILES | SYMBOLS | STRINGS)"]
    CENTER["Center Stage: MasterHorizontalSplitter (SourceCodeView + DisassemblyView)"]
    BOTTOM["Bottom Console Zone: BottomConsoleTabs (7 Integrated Consoles)"]
    RIGHT["Right Dock Area: Variables | Registers | Watch Expressions | Server Rack"]
    STATUS["Bottom Status Bar: Architecture | Active MPI Rank | PC/Seek | Session Status"]

    MW --> TB
    MW --> LEFT
    MW --> CENTER
    MW --> BOTTOM
    MW --> RIGHT
    MW --> STATUS
```

### Two-Tier Panel Chrome Standard
Every dock widget and bottom console tab adheres to a strict two-tier visual contract enforced by `ThemeManager` and `MainWindow::harmonizeBottomConsoleTabs()`:
1. **Tier 1 (Control Header Strip, `24px–26px` height):** `#21252b` surface with `1px solid #2c313a` bottom border, `6px` horizontal padding, and compact `20px–22px` buttons, combo boxes, and filter inputs.
2. **Tier 2 (Content Viewport):** `#16181d` dark-slate canvas with `0px` outer `contentsMargins` and `QFrame::NoFrame` so tables, trees, text browsers, and custom-painted views sit flush against the `1px` splitter and dock borders without double-walled frames.

---

## 2. Translation Unit Partitioning & `< 950` Line Ceiling

To prevent monolithic source files and ensure fast incremental compilation, major UI subsystems are partitioned into focused translation units. Every `.cpp` and `.hpp` file across `src/` and `tests/` is continuously audited by `TestMainWindowUI::testFullRepoGoogleStyleAndDocumentation()` to enforce:
* **Strict `< 950` lines per file**
* **`>= 20%` Doxygen and inline comment density**
* **Google C++ Style Guide header guards (`#pragma once`), `@file`/`@brief` file headers, and closing `// namespace` comments**

| Subsystem | Translation Units | Responsibility |
| :--- | :--- | :--- |
| **MainWindow** | `src/ui/MainWindow.cpp`<br>`src/ui/MainWindowWorkspace.cpp`<br>`src/ui/MainWindowNavigation.cpp`<br>`src/ui/MainWindowCohesion.cpp` | Lifecycle & rank state slots (`MainWindow.cpp`), 4-zone dock/splitter/toolbar construction (`MainWindowWorkspace.cpp`), address seek routing & history stack (`MainWindowNavigation.cpp`), and UI cohesion, perspective switching, ELF target loading, deadlock cycle propagation, & step execution (`MainWindowCohesion.cpp`). |
| **Disassembly, CFG & Decompiler** | `src/ui/views/DisassemblyView.cpp`<br>`src/ui/views/DisassemblyAnalysis.cpp`<br>`src/ui/views/DisassemblyDecompiler.cpp` | Linear assembly widget & branch gutter (`DisassemblyView.cpp`), basic-block partitioning, Manhattan CFG edge routing, rank badges, minimap, & Xref extraction (`DisassemblyAnalysis.cpp`), and multi-language `Approx C`/`C++`/`Pseudocode` decompilation (`DisassemblyDecompiler.cpp`). |
| **Preferences Dialog** | `src/ui/dialogs/PreferencesDialog.cpp`<br>`src/ui/dialogs/PreferencesDialogPages.cpp` | Modal dialog shell & category navigation (`PreferencesDialog.cpp`) and per-category settings cards (`PreferencesDialogPages.cpp`). |
| **UI Integration Tests** | `tests/test_ui.cpp`<br>`tests/test_ui_aux.cpp`<br>`tests/test_ui_cohesion.cpp` | Core widget & layout tests (`test_ui.cpp`), CFG/decompiler/style audit tests (`test_ui_aux.cpp`), and ELF64, Xrefs, live patching, Zeal docsets, and step workflow tests (`test_ui_cohesion.cpp`). |

---

## 3. Cross-Pane Signal Choreography

GridLock eliminates behavioral silos between views through deterministic signal bridges wired in `MainWindowWorkspace.cpp` and `MainWindowCohesion.cpp`:

1. **Bidirectional Source <-> Assembly Synchronization:**
   * Clicking a C/C++ line in `SourceCodeView` emits `SourceCodeView::sourceLineSelected(int lineOneBased)`, which resolves `MainWindow::sourceLineToAddress(lineOneBased)` and seeks `DisassemblyView` (Linear, CFG, and Decompiler) and `MemView`.
   * Clicking an instruction or basic block in `DisassemblyView` emits `DisassemblyView::addressSelected(uint64_t addr)`, which seeks `MemView` and highlights `MainWindow::addressToSourceLine(addr)` in `SourceCodeView`.
2. **Synchronized Step Execution Loop (`startTargetWorkflow` / `stepInstructionWorkflow` / `stopTargetWorkflow`):**
   * Launching or stepping advances the active rank's program counter via `DisassemblyView::nextInstructionAddress`, updates `ServerRackView` rank cards, updates `RegisterView` (`rip`, `rsp`, `rbp`, `rax`, `eflags`), seeks `MemView`, and advances the emerald `▶` execution line highlight in `SourceCodeView`.
3. **Cross-View Watch Promotion & Context Actions:**
   * Variables in `VariablesDockWidget` and expressions in `ExpressionEvaluatorWidget` emit `watchExpressionRequested(const QString&)`, adding columns directly to `DifferentialGrid` (`Watch Expressions`).
   * Context actions in `DisassemblyView` and `RegisterView` (`Show in Memory View`, `Open Xrefs Inspector (X)`, `Lookup Documentation (F1)`) route directly to `MemView`, `XrefInspectorDialog`, and `ReferenceManualWidget`.
