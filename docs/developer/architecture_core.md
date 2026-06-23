# ⚙️ Architecture Core: Backend & HPC Subsystems

GridLock handles extreme data concurrency by separating process coordination, memory reading, and security into distinct C++ components.

## 🔄 Lifecycle & Coordination Engine

GridLock's entry point and primary event loop are managed here:

*   **`src/main.cpp` & `GridLockApp.cpp`**: Bootstraps the `QApplication`, configures the Wayland QPA (`QT_QPA_PLATFORM=wayland`), and initializes the core singleton managers before mounting the `MainWindow`.
*   **`GdbRankCoordinator.cpp`**: The heart of the debugger. Manages an asynchronous subprocess pool. It routes multiplexed GDB/MI (Machine Interface) commands to the correct MPI rank, parsing the async JSON/Tuple responses back into Qt Signals.
*   **`DapCoordinator.cpp`**: Implements the Debug Adapter Protocol (DAP) over `stdin`/`stdout`. Handles state requests, stack trace formatting, and JSON-RPC bridging.
*   **`LspCoordinator.cpp`**: Spawns and manages a local `clangd` instance. Provides semantic intelligence (hovers, go-to-definition) constrained to the user's workspace to prevent scope leaks.

## ⚡ Zero-Copy Memory Engine

Reading memory via standard `ptrace` for large matrices is computationally unviable. We bypass the debugger entirely for raw dumps:

*   **`NativeMemoryReader.cpp`**: Uses Linux `process_vm_readv` to perform zero-copy reads directly from the target PID's virtual memory space into GridLock's memory space.
*   **`MemoryBoundsValidator.cpp`**: Ensures that requested read lengths and base pointers do not page fault or read outside the target's `.data` or `.bss` segments, preventing cascading crashes.
*   **`MemoryExporter.cpp`**: Takes validated memory buffers and converts them efficiently to `.csv` matrices or binary blobs on disk.

## 🛡️ Security & Auditing

Because GridLock interacts with remote execution environments and processes user input as commands, strict sanitization is enforced:

*   **`MiSanitizer.cpp`**: Intercepts all outgoing GDB/MI commands. Validates that the abstract syntax tree depth does not exceed **max nesting 32** and the payload does not exceed **64KB**. Prevents buffer overflow attacks against the underlying parser.
*   **`TelemetryObfuscator.cpp`**: Ensures diagnostic data sent over the network is masked. Implements **MTU padding** and **constant-time flushing** to defend against side-channel traffic analysis.

## 🖥️ HPC & Cluster Management

*   **`SpackManager.cpp`**: Queries the target environment for `spack env list` and resolves dependency paths, injecting them into the localized run environment.
*   **`MockHpcBackend.cpp`**: A critical abstraction used in CI/TDD. Simulates a multi-node SLURM cluster to validate rank coordination logic without requiring physical HPC hardware.
