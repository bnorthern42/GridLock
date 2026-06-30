# ⚙️ Architecture Core: Backend & HPC Subsystems

> **Last updated: v0.6.0**

GridLock handles extreme data concurrency by separating process coordination, memory reading, and security into distinct C++ components.

## 🔄 Lifecycle & Coordination Engine

GridLock's entry point and primary event loop are managed here:

*   **`src/main.cpp` & `GridLockApp.cpp`**: Bootstraps the `QApplication`, configures the Wayland QPA (`QT_QPA_PLATFORM=wayland`), and initializes the core singleton managers before mounting the `MainWindow`.
*   **`VersionManager.cpp`** *(v0.6.0)*: A lightweight singleton exposing the build-system-generated version constants. `meson.build` is the **single source of truth** for version numbers. At configure time, Meson processes `src/core/VersionConfig.hpp.in` via `configure_file()`, substituting `@VERSION_MAJOR@`, `@VERSION_MINOR@`, and `@VERSION_PATCH@` tokens. All UI components and the `--version` CLI flag consume `VersionManager::instance()->versionString()` rather than hardcoded literals.

    ```meson
    # meson.build (excerpt)
    version_conf = configuration_data()
    version_conf.set('VERSION_MAJOR', version_split[0])
    version_conf.set('VERSION_MINOR', version_split[1])
    version_conf.set('VERSION_PATCH', version_split[2])
    configure_file(
      input  : 'src/core/VersionConfig.hpp.in',
      output : 'VersionConfig.hpp',
      configuration : version_conf
    )
    ```
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

## 🧵 Threading and Memory Safety Rules

To maintain high responsiveness when dealing with HPC workloads and strict ASAN compliance, GridLock enforces strict threading rules:

1. **No UI Blocking**: Never perform blocking I/O, heavy JSON parsing, or `process_vm_readv` syscalls on the main GUI thread.
2. **Use QtConcurrent**: All background workloads (like parsing large DAP payloads or reading matrices) must be dispatched via `QtConcurrent::run`.
3. **Strict Signal/Slot Boundary**: Worker threads must never touch QWidgets directly. They must communicate results back to the main thread exclusively via Qt Signals (queued connections).

---

## ⚙️ Configuration Resolution Hierarchy (v0.6.0)

`ConfigManager` implements a **two-tier override system** to support both global user preferences and project-local settings without conflict.

| Priority | Source | Path |
| :---: | :--- | :--- |
| **1 (Highest)** | Project-local override | `<workspace_root>/.gridlock/workspace.toml` |
| **2 (Fallback)** | Global XDG user config | `~/.config/gridlock/config.toml` |

**Resolution logic** (called on session load and `ConfigManager::instance()->reload()`):

```cpp
// ConfigManager.cpp (simplified)
void ConfigManager::reload(const QString& workspaceRoot) {
    // 1. Always load the global baseline first
    loadFromFile(xdgConfigPath());

    // 2. If a project-local override exists, merge it on top
    QString localPath = workspaceRoot + "/.gridlock/workspace.toml";
    if (QFile::exists(localPath)) {
        mergeFromFile(localPath);  // project values win on key collision
    }
}
```

This design means a project can ship a `.gridlock/workspace.toml` in source control to enforce a specific `lldb-dap` path, MPI rank count, or argument template without touching the developer's global `~/.config/gridlock/config.toml`.

---

## 💥 DAP Exception Handling Lifecycle (v0.6.0)

Prior to v0.6.0, a target crash (e.g., `SIGFPE`, segfault) during a DAP session would cause the IDE to silently hang — the debug adapter process would exit but the UI would never receive a terminal state transition.

In v0.6.0, `DapCoordinator` explicitly handles `stopped` events where `reason` is `"exception"`:

**Signal emitted:**
```cpp
// DapCoordinator.hpp
signals:
    void targetCrashed(const QString& exceptionDescription);
```

**Dispatch logic in `DapCoordinator.cpp`:**
```cpp
void DapCoordinator::handleStoppedEvent(const QJsonObject& body) {
    const QString reason = body["reason"].toString();
    if (reason == "exception") {
        QString desc = body.value("description").toString("Fatal exception");
        emit targetCrashed(desc);
        terminateSession();  // clean up the adapter process
        return;
    }
    // ... normal stopped handling (breakpoint, step, etc.)
}
```

**`MainWindow` connection:**
```cpp
connect(m_dapCoordinator, &DapCoordinator::targetCrashed,
        this, [this](const QString& desc) {
    QMessageBox::critical(this, tr("Target Crashed"),
        tr("The debug target terminated with a fatal exception:\n\n%1\n\n"
           "The debug session has been closed.").arg(desc));
});
```

This guarantees the UI always reaches a clean idle state after a crash, rather than waiting indefinitely for a `terminated` event that will never arrive.
