# DAP Coordinator Architecture

GridLock's `DapCoordinator` serves as the headless JSON-RPC engine for orchestrating debugging sessions across multiple languages, finalizing the IDE's Phase 5 migration away from the legacy GDB/MI protocol.

## Process Lifecycle and Connection

1. **Initialization:** The `DapCoordinator` spawns the target debug adapter (e.g., `lldb-vscode` or `lldb-dap`) via a standard `QProcess` intercepting standard I/O pipes.
2. **Handshake:** A strict initialization sequence is executed involving the `initialize` and `launch` requests, configuring the adapter to the IDE's capabilities.
3. **Safe Teardown:** To prevent dangling zombie processes common in HPC environments, the `terminateSession()` sequence transmits a `"disconnect"` request (`{"terminateDebuggee": true}`). The coordinator awaits `QProcess::finished` with a safe fallback to `.terminate()` and `.kill()`.

## Event Parsing and Execution Control

The coordinator reads raw stdout bytes into a `QByteArray` buffer, dynamically extracting `Content-Length` headers and resolving fragmented JSON payloads to prevent data loss over high-latency SSH connections.

### Halt and Synchronization
When a `stopped` event is intercepted (e.g., due to a breakpoint or step), the coordinator automatically triggers a state-machine chain:
1. Emits `executionStopped` for the specific MPI rank mapping.
2. Automatically generates and transmits a `"command": "stackTrace"` request.
3. Parses the active `frameId` from the stack trace response and emits `locationChanged` to instantly sync the main UI editor.
4. Transparently spawns subsequent requests to resolve `scopes` (Locals, Registers).

## UI and Memory Synchronization

The coordinator operates as the single source of truth for the GridLock UI components:
* **Categorized Output:** Parses `"event": "output"` messages and routes them to the HPC Console based on their `category` (`stdout`, `stderr`, `console`).
* **Interactive Evaluation:** Synchronizes the `ExpressionEvaluatorWidget` with the active stack frame to resolve arbitrary C++ via the `evaluate` DAP request.
* **Hex Dumps & Registers:** Requests raw memory and extracts standard `base64` encoded payloads, decoding them into native `QByteArray` blocks for 1:1 hardware synchronization with the `MemView` hex dump and `RegisterView` components.
