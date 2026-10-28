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

### 5. File Size & Modularity Ceiling (< 1,000 Lines)
* Every `.cpp` and `.hpp` file in `src/` and `tests/` MUST remain strictly under 1,000 lines (`< 1000` lines).
* Before adding code to a file approaching 900+ lines, decompose cohesive subsystems, dock builders, or view helpers into dedicated `.hpp`/`.cpp` translation units.

### 6. Native Domain Terminology
* Never reference external third-party tool names in source comments, identifiers, test names, or git commit messages.

### 7. Google C++ Style Guide Compliance
* All C++ headers and implementation files MUST follow the **Google C++ Style Guide**:
  * Use `#pragma once` at the top of every header file.
  * Order `#include` blocks cleanly: corresponding header first, C system headers, C++ standard library headers, Qt/third-party library headers, and project headers.
  * Enforce strict `const` correctness on methods, references, and local variables.
  * Annotate all virtual method overrides with `override` (and never combine `virtual` with `override`).
  * Use clear, self-documenting identifier names (`PascalCase` for types/classes/structs/enums, `camelCase` for functions/methods, `m_camelCase` for private class data members, and named constants instead of magic numbers).
  * Initialize all scalar members in-class (`int m_selectedRank = 0;`, `Widget* m_view = nullptr;`) and avoid raw owning pointers (use Qt parent-child ownership or `std::unique_ptr`).
  * Keep every file strictly under 1,000 lines (`< 1,000` lines, targeting `< 900` lines).

### 8. Comprehensive Architectural & Inline Documentation
* Every `.hpp` and `.cpp` file MUST be thoroughly documented so a systems engineer can understand the architecture, data flow, and invariants immediately:
  * **File Header Block (`/** @file ... */`)**: Every header and implementation file must begin with a concise file-level Doxygen comment explaining the module's responsibility, architectural role, and threading/ownership model.
  * **Type Documentation (`/** ... */`)**: Every class, struct, and enum must have a Doxygen block describing its purpose, invariants, and collaboration with other components.
  * **Method, Signal, & Slot Documentation (`/** ... */`)**: Every public, protected, and private method, signal, and slot must document what it does, its `@param` inputs, `@return` value, and any side effects or preconditions.
  * **Algorithmic & UI Layout Inline Comments (`// ...`)**: Non-trivial control-flow graph layouts, GDB/MI parsers, coordinate transformations, memory diffing routines, and `QPainter` rendering loops must include clear step-by-step inline comments explaining *why* each calculation or state transition occurs.

### 9. Strict Ban on Python/Sed/Regex Bulk Modification Scripts
* **NEVER** use `python`, `python3 -c`, `sed`, `awk`, `perl`, or any automated regex/AST script to bulk-insert comments, reformat code, or modify C++ source files (`*.hpp`, `*.cpp`).
* Every code edit and documentation pass MUST be performed deliberately and file-by-file using the native file-editing tools (`replace_file_content` / `write_to_file`) after reading and understanding the target file.

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