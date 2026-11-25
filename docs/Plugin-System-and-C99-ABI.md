# Plugin System and C99 ABI Reference

GridLock supports dynamically loaded native extensions written in C99 or C++23 (or any language capable of exporting a C ABI symbol). Plugins inspect stopped MPI ranks, evaluate expressions, read or write target memory, manage breakpoints, add type-matched actions to `VariablesDockWidget`, register commands under **Tools -> Plugins**, and emit structured JSON telemetry without linking Qt6 or internal GridLock libraries.

---

## 1. Overview & Discovery Directories

`PluginManager` (`src/core/plugins/PluginManager.{hpp,cpp}`) scans two plugin directories in order at startup and whenever **Reload Plugins** is triggered:

1. **Per-user plugins**: `~/.config/gridlock/plugins/` (or `$GRIDLOCK_TEST_CONFIG_DIR/plugins/` in isolated test runs).
2. **Workspace plugins**: `<workspace>/.gridlock/plugins/`.

### Shared-Library Loading & Persistence

* **Recognized Extensions**: `.so` (Linux), `.dylib` (macOS), and `.dll` (Windows).
* **Symbol Resolution**: Each candidate shared library is opened with `QLibrary` using `QLibrary::ResolveAllSymbolsHint` so unresolved dependencies fail immediately during load rather than lazily at runtime.
* **Duplicate ID Rejection**: Every plugin must report a non-empty `plugin_id_utf8` and `plugin_name_utf8`. If a second shared library reports an already-loaded `plugin_id_utf8`, `PluginManager` rejects it and records an error diagnostic without disturbing the active plugin.
* **Disabled Plugin Persistence**: Disabling a plugin writes its `plugin_id_utf8` into `[plugins] disabled_ids = [...]` inside `~/.config/gridlock/config.toml` via `ConfigManager`. Disabled plugins are skipped on startup and cleanly unloaded (`shutdown` -> action unregistration -> `QLibrary::unload()`) when toggled off live.

---

## 2. Binary Compatibility & ABI Stability Rules

The public binary contract is frozen in [`include/gridlock/plugin_api.h`](../include/gridlock/plugin_api.h) at `GRIDLOCK_PLUGIN_ABI_MAJOR = 1u` and `GRIDLOCK_PLUGIN_ABI_MINOR = 0u`.

* **Strict C99 Boundary**: `plugin_api.h` compiles cleanly under both `-std=c99 -Wall -Wextra -Werror` and `-std=c++23 -Wall -Wextra -Werror`. All declarations are wrapped in `extern "C"`, use fixed-width integer types (`int32_t`, `uint32_t`, `uint64_t`, `size_t`), and avoid `std::*` or Qt types across the `.so` boundary.
* **16-Byte Versioned Header at Offset `0`**: Every ABI struct (`GridLockHostApi`, `GridLockPluginDescriptor`, `GridLockStopInfo`, `GridLockVariableContext`, `GridLockCommandSpec`) begins with:
  ```c
  uint32_t struct_size;
  uint32_t abi_major;
  uint32_t abi_minor;
  uint32_t reserved_flags;  /* Must be 0 in ABI v1.0 */
  ```
* **Append-Only Minor Evolution (`reserved[8]`)**: Both `GridLockHostApi` and `GridLockPluginDescriptor` end with `void* reserved[8]` zero-initialized padding slots. Future v1.x minor releases may replace leading reserved slots with new callback pointers without altering struct size or existing field offsets. `PluginManager` enforces:
  * `status == GRIDLOCK_OK`
  * `desc.abi_major == GRIDLOCK_PLUGIN_ABI_MAJOR`
  * `desc.struct_size >= offsetof(GridLockPluginDescriptor, reserved)` and `desc.struct_size <= sizeof(GridLockPluginDescriptor)`
  * `tests/core/test_plugin_abi.cpp` locks exact `sizeof` and `offsetof` values via compile-time `static_assert`.
* **Versioned Entry Point (`_v1`) & Symbol Isolation**: `PluginManager` resolves only `"gridlock_plugin_init_v1"` (`GRIDLOCK_PLUGIN_INIT_SYMBOL_V1`). Plugins must compile with `-fvisibility=hidden` and annotate only `gridlock_plugin_init_v1` with `GRIDLOCK_PLUGIN_EXPORT`. Future breaking major revisions can coexist under `_v2` without symbol collisions.
* **Stateless Context Pointers**: Every host callback receives `void* host_ctx` and every plugin callback receives `void* plugin_ctx`, eliminating mutable global state in plugins.
* **Caller-Owned Buffers & Symmetric Heap Helpers**:
  * `eval_expr` writes a NUL-terminated UTF-8 result into caller-provided `(out_utf8, cap, out_len)` and stores the payload byte length (excluding trailing NUL) in `*out_len`. When `cap` is too small, it returns `GRIDLOCK_ERR_BUFFER_TOO_SMALL` and writes the required payload length into `*out_len` so the caller can allocate `out_len + 1` bytes and retry.
  * `alloc_bytes` and `free_bytes` provide a symmetric host allocator pair whenever memory must cross the plugin/host boundary.

---

## 3. Complete C99 Host API & Status Reference

### `GridLockStatus` (`int32_t`)

| Constant | Value | Meaning |
| :--- | :---: | :--- |
| `GRIDLOCK_OK` | `0` | Operation succeeded. |
| `GRIDLOCK_ERR_INVALID_ARG` | `-1` | Null pointer or out-of-range argument. |
| `GRIDLOCK_ERR_ABI_MISMATCH` | `-2` | Incompatible `abi_major` or invalid `struct_size`. |
| `GRIDLOCK_ERR_BUFFER_TOO_SMALL` | `-3` | Caller output buffer `cap` is too small; `*out_len` holds required byte count. |
| `GRIDLOCK_ERR_NOT_STOPPED` | `-4` | Target MPI rank is running or exited rather than stopped. |
| `GRIDLOCK_ERR_INVALID_RANK` | `-5` | Requested rank index is outside `[0, rank_count)`. |
| `GRIDLOCK_ERR_EVAL_FAILED` | `-6` | Debugger expression evaluation failed. |
| `GRIDLOCK_ERR_MEMORY_READ_FAILED` | `-7` | Inferior memory read or write faulted. |
| `GRIDLOCK_ERR_UNSUPPORTED` | `-8` | Operation is unsupported by the active backend. |
| `GRIDLOCK_ERR_TIMEOUT` | `-9` | Synchronous GDB/MI request timed out before completion. |

### `GridLockLogLevel` (`int32_t`)

`GRIDLOCK_LOG_TRACE = 0`, `GRIDLOCK_LOG_DEBUG = 1`, `GRIDLOCK_LOG_INFO = 2`, `GRIDLOCK_LOG_WARN = 3`, `GRIDLOCK_LOG_ERROR = 4`.

### `GridLockHostApi` Callbacks

`PluginHostBridge` (`src/core/plugins/PluginHostBridge.{hpp,cpp}`) implements all 13 callbacks in `GridLockHostApi`. Blocking debugger queries (`eval_expr`, `read_memory`, `write_memory`) execute against `GdbRankCoordinator` through a bounded `QEventLoop` + `QTimer` timeout so hung targets return `GRIDLOCK_ERR_TIMEOUT`.

| Callback | Signature | Behavior |
| :--- | :--- | :--- |
| `alloc_bytes` | `void* (void* host_ctx, size_t num_bytes)` | Allocates `num_bytes` on the host heap. |
| `free_bytes` | `void (void* host_ctx, void* ptr)` | Releases memory previously returned by `alloc_bytes`. |
| `log_message` | `GridLockStatus (void* host_ctx, GridLockLogLevel level, const char* utf8_msg)` | Writes a structured log entry routed to GridLock's console/log sinks. |
| `get_rank_count` | `GridLockStatus (void* host_ctx, int32_t* out_ranks)` | Returns the active MPI session rank count. |
| `get_active_rank` | `GridLockStatus (void* host_ctx, int32_t* out_rank)` | Returns the currently focused zero-based MPI rank index. |
| `eval_expr` | `GridLockStatus (void* host_ctx, int32_t rank, const char* expr_utf8, char* out_utf8, size_t cap, size_t* out_len)` | Evaluates a C/C++ expression on a stopped rank into caller-owned `out_utf8`. |
| `read_memory` | `GridLockStatus (void* host_ctx, int32_t rank, uint64_t address, void* out_bytes, size_t num_bytes, size_t* out_read)` | Reads `num_bytes` from virtual `address` on `rank` into `out_bytes`. |
| `write_memory` | `GridLockStatus (void* host_ctx, int32_t rank, uint64_t address, const void* in_bytes, size_t num_bytes, size_t* out_written)` | Writes `num_bytes` from `in_bytes` into virtual `address` on `rank`. |
| `set_breakpoint` | `GridLockStatus (void* host_ctx, const char* file_utf8, int32_t line, const char* condition_utf8, int32_t* out_bp_id)` | Installs a source breakpoint at `file_utf8:line` with an optional condition. |
| `remove_breakpoint` | `GridLockStatus (void* host_ctx, int32_t bp_id)` | Removes a breakpoint previously created via `set_breakpoint`. |
| `register_variable_action` | `GridLockStatus (void* host_ctx, const char* action_id, const char* menu_label, const char* type_regex, GridLockVariableActionCallback cb, void* plugin_ctx)` | Registers a regex-matched context-menu action for `VariablesDockWidget`. |
| `register_command_action` | `GridLockStatus (void* host_ctx, const char* command_id, const char* menu_label, const char* shortcut_hint, GridLockCommandActionCallback cb, void* plugin_ctx)` | Registers a menu command under **Tools -> Plugins**. |
| `emit_structured_event` | `GridLockStatus (void* host_ctx, const char* channel_utf8, const char* json_payload_utf8)` | Emits a UTF-8 JSON payload on `channel_utf8` to host listeners and console logs. |

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
| `on_rank_stopped` | `GridLockRankStoppedCallback` | No | Called when a rank stops (`const GridLockStopInfo*`). |
| `on_session_end` | `GridLockSessionEndCallback` | No | Called when the active debug session terminates. |
| `shutdown` | `GridLockPluginShutdownCallback` | No | Called immediately before `QLibrary::unload()` to free `plugin_ctx`. |

### Lifecycle Execution Order

```text
gridlock_plugin_init_v1(host, out_desc)
  -> [optional] register_variable_action / register_command_action
  -> on_session_start(plugin_ctx, host, rank_count)
  -> on_rank_stopped(plugin_ctx, host, stop_info)      (repeats per stop)
  -> [user triggers] variable / command callbacks      (while stopped or active)
  -> on_session_end(plugin_ctx, host)
  -> shutdown(plugin_ctx, host)                        (before unload or disable)
```

### Minimal C99 Plugin Skeleton

```c
#include "gridlock/plugin_api.h"
#include <stdlib.h>
#include <string.h>

typedef struct { int32_t stops_seen; } MyCtx;

static GridLockStatus on_stopped(void* ctx, const GridLockHostApi* host,
                                 const GridLockStopInfo* info) {
  MyCtx* state = (MyCtx*)ctx;
  if (!state || !host || !info) return GRIDLOCK_ERR_INVALID_ARG;
  state->stops_seen += 1;
  if (host->log_message) {
    host->log_message(host->host_ctx, GRIDLOCK_LOG_INFO, "Rank stopped");
  }
  return GRIDLOCK_OK;
}

static void on_shutdown(void* ctx, const GridLockHostApi* host) {
  (void)host;
  free(ctx);
}

GRIDLOCK_PLUGIN_EXPORT GridLockStatus gridlock_plugin_init_v1(
    const GridLockHostApi* host, GridLockPluginDescriptor* out_desc) {
  if (!host || !out_desc) return GRIDLOCK_ERR_INVALID_ARG;
  if (host->abi_major != GRIDLOCK_PLUGIN_ABI_MAJOR ||
      host->struct_size < sizeof(GridLockHostApi)) {
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

C++23 plugins may include [`include/gridlock/plugin_cpp_helpers.hpp`](../include/gridlock/plugin_cpp_helpers.hpp) (`namespace gridlock::plugin`) for `std::expected` error handling, automatic `eval_expr` buffer retries, `std::span` memory reads, and RAII host allocations without changing the C99 ABI boundary:

```cpp
#include "gridlock/plugin_api.h"
#include "gridlock/plugin_cpp_helpers.hpp"

namespace gp = gridlock::plugin;

GridLockStatus inspectRank(const GridLockHostApi* host, int32_t rank, uint64_t ptrAddr) {
  // Automatically retries with an exact heap buffer on GRIDLOCK_ERR_BUFFER_TOO_SMALL.
  auto countStr = gp::evalExpr(host, rank, "mesh.num_vertices");
  if (!countStr) return countStr.error();

  // Reads a trivially copyable POD value via std::bit_cast (rejects partial reads).
  auto firstCoord = gp::readPod<double>(host, rank, ptrAddr);
  if (!firstCoord) return firstCoord.error();

  // Reads a raw byte buffer via std::span<std::byte>.
  std::array<std::byte, 24> vertexBytes{};
  auto bytesRead = gp::readBytes(host, rank, ptrAddr, vertexBytes);
  if (!bytesRead) return bytesRead.error();

  // RAII host-allocated buffer released automatically via host->free_bytes.
  auto scratch = gp::allocHostBytes(host, 256);
  if (!scratch) return scratch.error();
  return GRIDLOCK_OK;
}
```

During initialization, `gp::validateHostAbi(host)` and `gp::initPluginDescriptorHeader(outDesc)` validate the host ABI table and stamp the 16-byte header on `GridLockPluginDescriptor`.

---

## 6. UI Integration (Variables Context Menu, Tools Menu & Preferences)

* **`VariablesDockWidget` (**Plugin Actions** Submenu)**: Right-clicking any variable row matches its debugger type string against every active plugin's `register_variable_action` `type_regex` (`QRegularExpression`). Matching actions appear under **Plugin Actions** and invoke `GridLockVariableActionCallback(plugin_ctx, host, &var_ctx)` with the rank, expression, type, value, and address.
* **Tools -> Plugins Menu (`MainWindowPlugins.cpp`)**: Every `register_command_action` entry is added dynamically under **Tools -> Plugins**, alongside **Manage Plugins...** (which opens **Preferences -> Plugins**). All variable and command actions registered by a plugin are unregistered automatically when that plugin unloads.
* **Structured Event Routing (`emit_structured_event`)**: Calling `host->emit_structured_event(host->host_ctx, channel, jsonPayload)` emits `PluginHostBridge::structuredEventEmitted` and `MainWindow::pluginStructuredEventEmitted` and appends formatted output to `HpcConsoleWidget` so external scripts or viewers can observe domain snapshots.
* **Preferences -> Plugins (`PluginSettingsPage`)**: Displays configured search directories with an **Open Folder** button, a table of all discovered plugins (**Enabled** checkbox, **Name**, **Version**, **ABI**, and **Status**: `Loaded`, `Disabled`, or `ABI Rejected: ...`), and a **Reload Plugins** button that unloads and rescans directories live without restarting GridLock.

---

## 7. Building & Installing Out-of-Tree Plugins

### Direct Compiler Commands

```bash
# C99 plugin
gcc -std=c99 -O2 -Wall -Wextra -Werror -fvisibility=hidden -fPIC -shared \
    -Iinclude my_plugin.c -o libmy_plugin.so

# C++23 plugin
g++ -std=c++23 -O2 -Wall -Wextra -Werror -fvisibility=hidden -fvisibility-inlines-hidden \
    -fPIC -shared -Iinclude my_plugin.cpp -o libmy_plugin.so
```

### Verify Exported Symbols

Confirm that only `gridlock_plugin_init_v1` is exported as a defined global text (`T`) symbol:

```bash
nm -D --defined-only libmy_plugin.so | awk '$2 == "T" { print $3 }'
# Expected output:
# gridlock_plugin_init_v1
```

### Walkthrough: `examples/plugins/domain_mesh_probe/`

GridLock ships a complete standalone C++23 sample plugin in [`examples/plugins/domain_mesh_probe/`](../examples/plugins/domain_mesh_probe/) with both a standalone `meson.build` and `Makefile`. It registers a **Dump Mesh Refinement Step** action for `.*Mesh.*` variables, evaluates `(expr).num_vertices`, `(expr).num_elements`, and `(expr).vertices`, reads raw `(x, y, z)` vertex coordinates via `readBytes`, writes `mesh_step_<rank>_<step>.vtk`, and emits a JSON event on channel `domain.mesh_probe.step`.

```bash
# Build with Meson
cd examples/plugins/domain_mesh_probe
meson setup build
meson compile -C build

# Or build with Make
make

# Install for the current user
mkdir -p ~/.config/gridlock/plugins
cp build/libdomain_mesh_probe.so ~/.config/gridlock/plugins/
```

Open **Preferences -> Plugins** inside GridLock and click **Reload Plugins** to load `Domain Mesh Probe` immediately.
