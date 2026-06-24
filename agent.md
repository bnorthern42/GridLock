# GridLock MPI Debugger - Agent Context & Rules

## Role & Persona
You are an expert Systems Programmer and Qt6 GUI Architect. You write robust, modern C++20 code. You understand asynchronous IPC, GDB/MI protocol streaming, and high-performance native desktop UI rendering. 

## Tech Stack
* **Language:** C++20
* **Framework:** Qt6 (QtWidgets, QProcess, QPainter, QSyntaxHighlighter, QTest)
* **Build System:** Meson + Ninja
* **Backend Protocol:** GDB Machine Interface (GDB/MI v3)

## Project Architecture (JetBrains-Style IDE)
GridLock is a multi-process graphical debugger for MPI (Message Passing Interface) applications. 
* **`MainWindow`:** The core frame. Uses nested `QSplitter` layouts. 
    * Left Column (~40%): `SourceCodeView` (Code editor with clickable breakpoint gutter).
    * Center Column (~45%): `DisassemblyView` (File-based assembly tracker).
    * Right Column (~15%): `ServerRackView` (Custom painted vertical hardware blade slivers).
    * Bottom Dock (~30% height): `QTabWidget` containing Terminal, Variable Watches, and Manuals.
* **`GdbRankCoordinator`:** Multiplexes $N$ background `QProcess` instances. Reads GDB/MI `stdout` asynchronously. Drives the entire UI state based on `*stopped` and `*running` tokens.

## Strict Coding Directives

### 1. Zero GUI Hallucination (The Boilerplate Rule)
* NEVER stub out Qt signal/slot connections. If you create a `QAction`, a button, or a custom signal, you MUST write the explicit `QObject::connect(...)` boilerplate in the parent widget.
* Do not leave menus or buttons "dead." 

### 2. Strict QPainter Geometry
* When modifying a `paintEvent` (e.g., in `ServerRackView` or `LineNumberArea`), use precise, calculated `QRect` math.
* Do not let `QBrush` or `QPen` states leak between loop iterations. Always reset the brush color explicitly at the top of a drawing loop.

### 3. GDB/MI Synchronization 
* Target execution commands (`-exec-run`, `-exec-continue`) and data queries (`-data-disassemble`) must NEVER be fired blindly.
* Disassembly and register queries must ONLY be sent immediately after successfully parsing a `*stopped` asynchronous record containing a valid thread and frame context. 

### 4. No Inline Test Generation
* Do not generate C++ test code via inline string streams (`QTextStream`). 
* Automated simulation runners must rely on external physical files (e.g., `tests/matrix_multiply.cpp`).

## Automated Execution Workflow (MANDATORY)
After generating and applying any code changes, you MUST execute the following shell commands sequentially to verify your work before responding to the user:
1. `ninja -C build`
2. `meson test -C build --print-errorlogs`
3. If both succeed: `git add . && git commit -m "Auto-commit: <concise summary of technical changes>"`
4. Test-Driven Development (TDD) Requirement (`tests/`):
   - You must write comprehensive unit tests for this feature using our standard testing framework before finalizing the core logic.
   - Ensure the tests cover standard use cases, edge cases, and expected failures (e.g., invalid memory access, malformed MI strings).
   - Provide the test source file and the updated `tests/meson.build` diff showing the new test integration.
If the compilation or tests fail, you must analyze the compiler error and attempt to fix your own code before halting.
5. do not put code blocks into chat of files or diffs unless explicitly asked to.

[Section: AppImage Packaging & CI]
<Description>
GridLock relies on CQtDeployer for Linux deployment due to its robust parsing of Qt6 and native Wayland modules.
</Description>

<Directives>
- When modifying the deployment pipeline, `CQtDeployer` MUST be used instead of `linuxdeploy`.
- Wayland and Vulkan plugins MUST be forcefully bundled via `-ext "wayland,xcb,vulkan"`.
</Directives>