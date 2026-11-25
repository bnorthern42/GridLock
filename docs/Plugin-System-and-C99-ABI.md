# Plugin System and C99 ABI Reference

GridLock supports dynamically loaded native extensions written in C99 or C++23 (or any language capable of exporting a C ABI symbol). Plugins inspect stopped MPI ranks, evaluate expressions, read or write target memory, manage breakpoints, add type-matched actions to `VariablesDockWidget`, register commands under **Tools -> Plugins**, and emit structured JSON telemetry without linking Qt6 or internal GridLock libraries.

---

## 1. Overview, Discovery Directories & Workspace SHA-256 Trust Model

`PluginManager` (`src/core/plugins/PluginManager.{hpp,cpp}`) scans two plugin directories in order at startup, on workspace switches, and whenever **Reload Plugins** is triggered:

1. **Per-user global plugins**: `~/.config/gridlock/plugins/` (or `$GRIDLOCK_TEST_CONFIG_DIR/plugins/` in isolated test runs).
2. **Workspace plugins**: `<workspace>/.gridlock/plugins/`.

### Workspace SHA-256 Trust Allowlist

To prevent arbitrary code execution when opening an untrusted project directory containing `<workspace>/.gridlock/plugins/`, `PluginManager` enforces pre-load binary verification:

* **Pre-`dlopen` Hashing**: Before calling `QLibrary::load()` on any workspace plugin, `PluginManager` rejects symbolic links and world-writable files, opens the regular binary, and computes its lowercase `SHA-256` hex digest.
* **User-Global Trust Storage**: Workspace trust is stored exclusively in `~/.config/gridlock/config.toml` under `[plugins.trusted_workspace_binaries]` mapping canonical binary path to approved `SHA-256` digest. Trust metadata is never read from `<workspace>/.gridlock/`.
* **Zero Constructor Execution Until Approved**: Untrusted workspace binaries are never passed to `dlopen()` / `QLibrary::load()`, preventing ELF `.init_array` constructors from executing. Instead, they appear in **Preferences -> Plugins** with status `Untrusted Workspace Plugin (Approval Required)` and a **Trust & Approve (<sha8>)** button. Modifying even a single byte of an approved workspace binary changes its `SHA-256` digest and invalidates trust automatically.

### Dual Global & Per-Workspace Enablement

* **Global Scope (`[plugins] disabled_ids`)**: Controls whether a plugin is enabled across all workspaces.
* **Workspace Scope (`[plugins.workspaces."<canonical_workspace>"]`)**: Stores `workspace_disabled_ids` in `~/.config/gridlock/config.toml` so individual plugins can be toggled per project without mutating global state.
* **Effective Enablement**: A plugin loads only when it is enabled globally, enabled in the active workspace, and (for workspace plugins) `SHA-256` trusted. Switching workspaces via `MainWindow::switchWorkspaceDirectory()` re-runs discovery automatically.

---

## 2. Binary Compatibility & ABI Stability Rules

The public binary contract is frozen in [`include/gridlock/plugin_api.h`](../include/gridlock/plugin_api.h) at `GRIDLOCK_PLUGIN_ABI_MAJOR = 1u` and `GRIDLOCK_PLUGIN_ABI_MINOR = 0u`.

* **Strict C99 Boundary & Calling Convention**: `plugin_api.h` compiles cleanly under both `-std=c99 -Wall -Wextra -Werror` and `-std=c++23 -Wall -Wextra -Werror`. Every function pointer typedef and `gridlock_plugin_init_v1` uses `GRIDLOCK_CALL` (`__cdecl` on `_WIN32`, empty elsewhere). Host builds define `-DGRIDLOCK_BUILDING_HOST` so `GRIDLOCK_PLUGIN_EXPORT` never exports `gridlock_plugin_init_v1` from the main executable on Windows.
* **16-Byte Versioned Header at Offset `0`**: Every ABI struct (`GridLockHostApi`, `GridLockPluginDescriptor`, `GridLockStopInfo`, `GridLockVariableContext`, `GridLockCommandSpec`) begins with:
  ```c
  uint32_t struct_size;
  uint32_t abi_major;
  uint32_t abi_minor;
  uint32_t reserved_flags;  /* Must be 0 in ABI v1.0 */
  ```
* **Append-Only Minor Evolution (`reserved[8]`)**: Both `GridLockHostApi` and `GridLockPluginDescriptor` end with `void* reserved[8]` zero-initialized padding slots. `PluginManager` enforces:
  * `status == GRIDLOCK_OK`
  * `desc.abi_major == GRIDLOCK_PLUGIN_ABI_MAJOR`
  * `desc.struct_size >= offsetof(GridLockPluginDescriptor, reserved)`
  * `tests/core/test_plugin_abi.cpp` locks exact `sizeof` and `offsetof` values via compile-time `static_assert`.
* **Pointer Storage Duration**:
  * Strings on `GridLockPluginDescriptor` (`plugin_id_utf8`, `plugin_name_utf8`, `plugin_version_utf8`, `description_utf8`) must remain valid until `shutdown` returns.
  * Borrowed strings on `GridLockStopInfo`, `GridLockVariableContext`, and `GridLockCommandSpec` are valid only for the duration of that callback invocation.
* **Per-Plugin Host Contexts & Safe Teardown**: Each loaded plugin receives a dedicated `GridLockHostApi` table and `PluginBindingContext`. During teardown, `PluginManager` locks out new registrations, runs `on_session_end` (if active) and `shutdown`, unregisters all actions and breakpoints owned by that plugin, and only then calls `QLibrary::unload()`. Re-entrant `unloadAll()` or `on_rank_stopped` events arriving while a callback is on the call stack are deferred through a FIFO queue.

---

## 3. C99 Host API & Status Reference

### `GridLockStatus` (`int32_t`)

| Constant | Value | Meaning |
| :--- | :---: | :--- |
| `GRIDLOCK_OK` | `0` | Operation succeeded. |
| `GRIDLOCK_ERR_INVALID_ARG` | `-1` | Null pointer, control character, oversized regex/payload, or out-of-range argument. |
| `GRIDLOCK_ERR_ABI_MISMATCH` | `-2` | Incompatible `abi_major` or invalid `struct_size`. |
| `GRIDLOCK_ERR_BUFFER_TOO_SMALL` | `-3` | Caller output buffer `cap` is too small; `*out_len` holds required byte count. |
| `GRIDLOCK_ERR_NOT_STOPPED` | `-4` | Target MPI rank is running or exited rather than stopped. |
| `GRIDLOCK_ERR_INVALID_RANK` | `-5` | Requested rank index is outside `[0, rank_count)`. |
| `GRIDLOCK_ERR_EVAL_FAILED` | `-6` | Debugger expression evaluation failed. |
| `GRIDLOCK_ERR_MEMORY_READ_FAILED` | `-7` | Inferior memory read or write faulted. |
| `GRIDLOCK_ERR_UNSUPPORTED` | `-8` | Operation is unsupported or disallowed during plugin shutdown. |
| `GRIDLOCK_ERR_TIMEOUT` | `-9` | Synchronous GDB/MI request timed out before completion. |

### `GridLockLogLevel` (`int32_t`)

`GRIDLOCK_LOG_TRACE = 0`, `GRIDLOCK_LOG_DEBUG = 1`, `GRIDLOCK_LOG_INFO = 2`, `GRIDLOCK_LOG_WARN = 3`, `GRIDLOCK_LOG_ERROR = 4`. (`TRACE` and `DEBUG` are filtered from console output unless `GRIDLOCK_PLUGIN_VERBOSE_LOGS=1` is set.)

### `GridLockHostApi` Callbacks

`PluginHostBridge` (`src/core/plugins/PluginHostBridge.{hpp,cpp}`) implements all 13 callbacks with `noexcept` boundaries, thread-safe coordinator marshaling, tokenized private memory MI channels (`<= 64 MiB` read with local `process_vm_readv` fast path, `<= 16 MiB` synchronous write), GDB/MI breakpoint input sanitization, and PCRE2 match/recursion limits on `type_regex` (`<= 512` bytes).

| Callback | Signature | Behavior |
| :--- | :--- | :--- |
| `alloc_bytes` | `void* (void* host_ctx, size_t num_bytes)` | Allocates `num_bytes` on the host heap. |
| `free_bytes` | `void (void* host_ctx, void* ptr)` | Releases memory previously returned by `alloc_bytes`. |
| `log_message` | `GridLockStatus (void* host_ctx, GridLockLogLevel level, const char* utf8_msg)` | Writes a bounded (`<= 16 KiB`) log entry coalesced onto the console sink. |
| `get_rank_count` | `GridLockStatus (void* host_ctx, int32_t* out_ranks)` | Returns the active MPI session rank count. |
| `get_active_rank` | `GridLockStatus (void* host_ctx, int32_t* out_rank)` | Returns the currently focused zero-based MPI rank index. |
| `eval_expr` | `GridLockStatus (void* host_ctx, int32_t rank, const char* expr_utf8, char* out_utf8, size_t cap, size_t* out_len)` | Evaluates a C/C++ expression on a stopped rank into caller-owned `out_utf8`. |
| `read_memory` | `GridLockStatus (void* host_ctx, int32_t rank, uint64_t address, void* out_bytes, size_t num_bytes, size_t* out_read)` | Reads `num_bytes` (`<= 64 MiB`) from virtual `address` on `rank` without disturbing open `MemView` docks. |
| `write_memory` | `GridLockStatus (void* host_ctx, int32_t rank, uint64_t address, const void* in_bytes, size_t num_bytes, size_t* out_written)` | Writes `num_bytes` (`<= 16 MiB`) synchronously via tokenized `-data-write-memory-bytes`. |
| `set_breakpoint` | `GridLockStatus (void* host_ctx, const char* file_utf8, int32_t line, const char* condition_utf8, int32_t* out_bp_id)` | Validates and quotes `file_utf8:line` and optional condition before installing a breakpoint. |
| `remove_breakpoint` | `GridLockStatus (void* host_ctx, int32_t bp_id)` | Removes a breakpoint previously created via `set_breakpoint`. |
| `register_variable_action` | `GridLockStatus (void* host_ctx, const char* action_id, const char* menu_label, const char* type_regex, GridLockVariableActionCallback cb, void* plugin_ctx)` | Registers a PCRE2-bounded regex context-menu action for `VariablesDockWidget`. |
| `register_command_action` | `GridLockStatus (void* host_ctx, const char* command_id, const char* menu_label, const char* shortcut_hint, GridLockCommandActionCallback cb, void* plugin_ctx)` | Registers a menu command under **Tools -> Plugins**. |
| `emit_structured_event` | `GridLockStatus (void* host_ctx, const char* channel_utf8, const char* json_payload_utf8)` | Emits a UTF-8 JSON payload (`<= 1 MiB`) on `channel_utf8`. |

---

## 4. Plugin Descriptor & Lifecycle Callbacks

Every plugin populates `GridLockPluginDescriptor` inside `gridlock_plugin_init_v1`:

| Field | Type | Required | Purpose |
| :--- | :--- | :---: | :--- |
| `struct_size` / `abi_major` / `abi_minor` / `reserved_flags` | `uint32_t` (x4) | Yes | 16-byte ABI header validated by `PluginManager`. |
| `plugin_id_utf8` | `const char*` | Yes | Globally unique stable identifier (e.g., `"org.example.mesh_probe"`). |
| `plugin_name_utf8` | `const char*` | Yes | Human-readable display name shown in **Preferences -> Plugins**. |
| `plugin_version_utf8` | `const char*` | No | Semantic version string (e.g., `"1.0.0"`). |
| `description_utf8` | `const char*` | No | Brief capability summary. |
| `plugin_ctx` | `void*` | No | Plugin-owned instance state pointer passed back to all callbacks. |
| `on_session_start` | `GridLockSessionStartCallback` | No | Called when a debug session starts (`rank_count`). |
| `on_rank_stopped` | `GridLockRankStoppedCallback` | No | Called strictly on GDB/MI `*stopped` events (`const GridLockStopInfo*`). |
| `on_session_end` | `GridLockSessionEndCallback` | No | Called when the active debug session terminates. |
| `shutdown` | `GridLockPluginShutdownCallback` | No | Called before `QLibrary::unload()` to release `plugin_ctx`. |

### Minimal C99 Plugin Skeleton

```c
#include "gridlock/plugin_api.h"
#include <stdlib.h>
#include <string.h>

typedef struct { int32_t stops_seen; } MyCtx;

static GridLockStatus GRIDLOCK_CALL on_stopped(
    void* ctx, const GridLockHostApi* host, const GridLockStopInfo* info) {
  MyCtx* state = (MyCtx*)ctx;
  if (!state || !host || !info) return GRIDLOCK_ERR_INVALID_ARG;
  state->stops_seen += 1;
  if (host->log_message) {
    host->log_message(host->host_ctx, GRIDLOCK_LOG_INFO, "Rank stopped");
  }
  return GRIDLOCK_OK;
}

static void GRIDLOCK_CALL on_shutdown(void* ctx, const GridLockHostApi* host) {
  (void)host;
  free(ctx);
}

GRIDLOCK_PLUGIN_EXPORT GridLockStatus GRIDLOCK_CALL gridlock_plugin_init_v1(
    const GridLockHostApi* host, GridLockPluginDescriptor* out_desc) {
  if (!host || !out_desc) return GRIDLOCK_ERR_INVALID_ARG;
  if (host->abi_major != GRIDLOCK_PLUGIN_ABI_MAJOR ||
      host->struct_size < offsetof(GridLockHostApi, reserved)) {
    return GRIDLOCK_ERR_ABI_MISMATCH;
  }
  memset(out_desc, 0, sizeof(*out_desc));
  out_desc->struct_size = (uint32_t)sizeof(*out_desc);
  out_desc->abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
  out_desc->abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
  out_desc->plugin_id_utf8 = "org.example.minimal_c99";
  out_desc->plugin_name_utf8 = "Minimal C99 Plugin";
  out_desc->plugin_version_utf8 = "1.0.0";
  out_desc->plugin_ctx = calloc(1, sizeof(MyCtx));
  out_desc->on_rank_stopped = &on_stopped;
  out_desc->shutdown = &on_shutdown;
  return GRIDLOCK_OK;
}
```

---

## 5. Optional C++23 Convenience Helpers (`include/gridlock/plugin_cpp_helpers.hpp`)

C++23 plugins may include [`include/gridlock/plugin_cpp_helpers.hpp`](../include/gridlock/plugin_cpp_helpers.hpp) (`namespace gridlock::plugin`) for `noexcept` `std::expected` error handling, `16 MiB`-bounded `eval_expr` buffer retries, `std::is_trivially_copyable_v` `readPod<T>` helpers, `std::span` memory reads, forward-compatible `validateHostAbi`, and RAII host allocations:

```cpp
#include "gridlock/plugin_api.h"
#include "gridlock/plugin_cpp_helpers.hpp"

namespace gp = gridlock::plugin;

GridLockStatus inspectRank(const GridLockHostApi* host, int32_t rank, uint64_t ptrAddr) {
  auto countStr = gp::evalExpr(host, rank, "mesh.num_vertices");
  if (!countStr) return countStr.error();

  auto firstCoord = gp::readPod<double>(host, rank, ptrAddr);
  if (!firstCoord) return firstCoord.error();

  std::array<std::byte, 24> vertexBytes{};
  auto bytesRead = gp::readBytes(host, rank, ptrAddr, vertexBytes);
  if (!bytesRead) return bytesRead.error();

  auto scratch = gp::allocHostBytes(host, 256);
  if (!scratch) return scratch.error();
  return GRIDLOCK_OK;
}
```

---

## 6. UI Integration, Staged Preferences, Self-Restart & GitHub Installer

* **`VariablesDockWidget` (**Plugin Actions** Submenu)**: Right-clicking a variable row matches its type string against active `register_variable_action` patterns outside the bridge mutex and populates `ctx.frame_level` from `CallStackDockWidget::activeFrameLevel()`.
* **Tools -> Plugins Menu (`MainWindowPlugins.cpp`)**: Lists registered plugin commands alongside **Manage Plugins...**. Console telemetry previews are truncated to `<= 1024` characters and coalesced on a `50 ms` timer.
* **Preferences -> Plugins (`Installed` Tab)**:
  * Displays a 7-column table: **Global** (checkbox), **Workspace** (checkbox), **Name**, **Version**, **Scope / ABI**, **Status**, and **Trust** (**Trust & Approve (<sha8>)** for untrusted workspace binaries).
  * All checkbox and trust edits are staged in memory without mutating `ConfigManager` on click; clicking **Cancel** discards staged edits, whereas **Apply** / **OK** commits them.
  * When applied changes alter plugin enablement or workspace trust, both `PluginSettingsPage` (`#pluginRestartBanner`) and `MainWindow`'s status bar (`#mainWindowPluginRestartBanner`) display a **Restart GridLock Now** button that saves open workspace/preferences state, launches `QProcess::startDetached`, and exits cleanly.
* **Preferences -> Plugins (`Install from GitHub` Tab & `PluginInstaller`)**:
  * Accepts validated `https://github.com/<owner>/<repo>[.git]` or `git@github.com:<owner>/<repo>.git` URLs and an optional branch/tag ref, rejecting leading `-` flags, whitespace, and shell metacharacters.
  * Clones with `git clone --depth 1` via shell-free `QProcess` argument lists into a temporary staging directory and auto-detects build systems in priority order:
    1. `meson.build`: `meson setup build -Dgridlock_include_dir=<includePath>` followed by `ninja -C build`
    2. `CMakeLists.txt`: `cmake -B build -DGRIDLOCK_INCLUDE_DIR=<includePath>` followed by `cmake --build build`
    3. `Makefile`: `make GRIDLOCK_INCLUDE_DIR=<includePath>`
    4. Single-file `.cpp` / `.c` fallback: `c++ -std=c++23` or `cc -std=c99` shared-library build with `-fvisibility=hidden -fPIC -shared -I<includePath>`
  * Streams compiler output into a monospace log view highlighting `warning:` and `error:` lines, installs built `.so`/`.dylib`/`.dll` artifacts atomically into `~/.config/gridlock/plugins` (**Global**) or `<workspace>/.gridlock/plugins` (**This Workspace**, auto-registering `SHA-256` trust), and presents **Restart GridLock to Load Installed Plugin** on completion.

---

## 7. Building & Installing Out-of-Tree Plugins Manually

```bash
# C99 plugin
gcc -std=c99 -O2 -Wall -Wextra -Werror -fvisibility=hidden -fPIC -shared \
    -Iinclude my_plugin.c -o libmy_plugin.so

# C++23 plugin
g++ -std=c++23 -O2 -Wall -Wextra -Werror -fvisibility=hidden -fvisibility-inlines-hidden \
    -fPIC -shared -Iinclude my_plugin.cpp -o libmy_plugin.so

# Verify that only gridlock_plugin_init_v1 is exported
nm -D --defined-only libmy_plugin.so | awk '$2 == "T" { print $3 }'
```

### Sample Plugin: `examples/plugins/domain_mesh_probe/`

[`examples/plugins/domain_mesh_probe/`](../examples/plugins/domain_mesh_probe/) provides a standalone C++23 plugin (`meson.build` and `Makefile`) that registers **Dump Mesh Refinement Step** for `.*Mesh.*` variables, reads stopped-rank mesh vertex coordinates, writes `0600` VTK snapshots inside a `0700` `$XDG_RUNTIME_DIR/gridlock/mesh_probe` runtime directory (`O_NOFOLLOW`), and emits JSON events on channel `domain.mesh_probe.step`.
