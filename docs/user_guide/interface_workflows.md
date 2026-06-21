# Interface & Workflows

GridLock provides a specialized interface designed for parallel C++ application debugging. This document covers the primary UI components and the methodologies for effectively inspecting distributed state.

## Core UI Layout

The GridLock interface is divided into several specialized views to maximize context without cluttering the screen:

* **Source & Disassembly View**: The central editor displays your C++ source code with inline execution markers. When stepping through unoptimized binaries or low-level primitives, you can seamlessly toggle the Disassembly View to inspect the underlying machine code instructions corresponding to the active source line.
* **Server Rack View**: Positioned horizontally along the top of the interface, the Server Rack provides a high-level overview of all active MPI ranks. It displays the current execution state (running, paused, exited, crashed) of each rank, allowing you to quickly identify diverging processes or synchronization deadlocks in your communication fabric.
* **Variables Tree**: Located in the side dock, this provides a structured hierarchy of local and global variables for the currently selected rank.

## Multi-Rank Differential Grid

Debugging parallel applications often requires comparing the state of a single variable across hundreds of MPI ranks to isolate numerical drift or corrupted payloads. 

The **Multi-Rank Differential Grid** ("Watch Expressions") solves this by displaying variable values horizontally across all active ranks. 
* **Usage**: Right-click a variable in the Source View or Variables Tree and select "Add to Differential Grid". 
* **Behavior**: The grid will populate a row for the watched expression, with columns corresponding to each MPI rank. Values that differ from Rank 0 (or a user-defined baseline rank) are highlighted automatically, allowing rapid visual identification of out-of-sync state.

## Hierarchical Variables Dock

For deep vertical inspection of complex nested C++ objects (e.g., `std::vector`, `std::map`, or custom scientific data structures), use the **Variables Dock**.

* **Navigation**: The dock uses a lazy-loaded hierarchical tree model. Expanding a node triggers an on-demand evaluation of the object's members via the underlying GDB/MI backend, keeping the interface responsive even when inspecting massive data structures.
* **Type Resolution**: GridLock automatically resolves dynamic types and displays the most derived class type, stripping away unnecessary boilerplate.

## Expression Evaluator

The **Expression Evaluator** provides an interactive REPL-like environment for evaluating arbitrary C++ expressions within the current scope of the selected MPI rank.

* **Capabilities**: You can execute standard arithmetic, dereference pointers, call functions with no side-effects (where permitted by the debugger backend), and evaluate language-level operators such as `sizeof(int)` or `decltype(var)`.
* **Workflow**: Open the Evaluator dock, type your expression, and press `Enter`. The result is evaluated against the current stack frame and displayed with its deduced type.

## Conditional Breakpoints

To halt execution only when specific criteria are met (crucial for time-stepping simulations or large loops), GridLock supports rich conditional breakpoints.

* **Toggling**: Left-click in the editor's left gutter to toggle a standard breakpoint.
* **Conditions**: Right-click an existing breakpoint in the gutter to attach a condition. You can input any valid C++ boolean expression (e.g., `iteration > 1000 && local_error > 1e-4`). The debugger will only suspend execution when the expression evaluates to true for the encountering rank.
