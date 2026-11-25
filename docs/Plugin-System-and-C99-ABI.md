# Plugin System and C99 ABI Reference

GridLock supports dynamically loaded native extensions written in C99 or C++23 (or any language capable of exporting a C ABI symbol). Plugins inspect stopped MPI ranks, evaluate expressions, read or write target memory, manage breakpoints, display modal dialogs and non-blocking toasts, register slash-delimited nested menus under **Tools -> Plugins**, mount clickable status-bar items, inject context-menu actions across five workstation surfaces, navigate and annotate source files, query and track active theme tokens, publish declarative report panels (`MARKDOWN`, `KEY_VALUE_TABLE`, `JSON_TREE`), and emit structured JSON telemetry without linking Qt6 or internal GridLock libraries.

---

## 1. Overview, Discovery Directories & Workspace SHA-256 Trust Model

`PluginManager` (`src/core/plugins/PluginManager.{hpp,cpp}`) scans two plugin directories in order at startup, on workspace switches, and whenever plugins are reloaded:

1. **Per-user global plugins**: `~/.config/gridlock/plugins/` (or `$GRIDLOCK_TEST_CONFIG_DIR/plugins/` in isolated test runs).
2. **Workspace plugins**: `<workspace>/.gridlock/plugins/`.

### Workspace SHA-256 Trust Allowlist

To prevent unprompted code execution when opening an untrusted project directory containing `<workspace>/.gridlock/plugins/`, `PluginManager` enforces pre-load binary verification:

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

* **Strict C99 Boundary & Calling Convention**: `plugin_api.h` compiles cleanly under both `-std=c99 -Wall -Wextra -Wpedantic -Werror` and `-std=c++23 -Wall -Wextra -Wpedantic -Werror` with zero GUI headers. Every function pointer typedef and `gridlock_plugin_init_v1` uses `GRIDLOCK_CALL` (`__cdecl` on `_WIN32`, empty elsewhere). Host builds define `-DGRIDLOCK_BUILDING_HOST` so `GRIDLOCK_PLUGIN_EXPORT` never exports `gridlock_plugin_init_v1` from the main executable on Windows.
* **16-Byte Versioned Header at Offset `0`**: Every ABI struct (`GridLockHostApi` `296 B`, `GridLockPluginDescriptor` `160 B`, `GridLockStopInfo` `56 B`, `GridLockVariableContext` `80 B`, `GridLockCommandSpec` `56 B`, `GridLockSurfaceContext` `80 B`, and `GridLockThemeInfo` `212 B`) begins with:
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
  * `tests/core/test_plugin_abi.cpp` locks exact `sizeof` and `offsetof` values on 64-bit targets via compile-time `static_assert`.
* **Pointer Storage Duration**:
  * Strings on `GridLockPluginDescriptor` (`plugin_id_utf8`, `plugin_name_utf8`, `plugin_version_utf8`, `description_utf8`) must remain valid until `shutdown` returns.
  * Borrowed strings on `GridLockStopInfo`, `GridLockVariableContext`, `GridLockCommandSpec`, and `GridLockSurfaceContext` are valid strictly for the synchronous duration of that callback invocation.
* **Per-Plugin Host Contexts & Safe Teardown**: Each loaded plugin receives a dedicated `GridLockHostApi` table and `PluginBindingContext`. During teardown, `PluginManager` sets `shuttingDown = true` to lock out late registrations or modal UI invocations, runs `on_session_end` (if active) and `shutdown`, purges all actions, menus, status items, context actions, editor annotations, breakpoints, and report panels owned by that plugin via `unregisterPluginResources(pluginId)`, and only afterward calls `QLibrary::unload()`. Re-entrant `unloadAll()` or `on_rank_stopped` events arriving while a callback is active (`m_callbackDepth > 0`) are deferred safely through a FIFO queue.

---

## 3. C99 Host API, Enums & Struct Reference

### `GridLockStatus` (`int32_t`)

| Constant | Value | Meaning |
| :--- | :---: | :--- |
| `GRIDLOCK_OK` | `0` | Operation succeeded. |
| `GRIDLOCK_ERR_INVALID_ARG` | `-1` | Null pointer, control character, quota exceeded, oversized regex/payload, or out-of-range argument. |
| `GRIDLOCK_ERR_ABI_MISMATCH` | `-2` | Incompatible `abi_major` or invalid `struct_size`. |
| `GRIDLOCK_ERR_BUFFER_TOO_SMALL` | `-3` | Caller output buffer `cap` is too small; `*out_len` holds required byte count (excluding NUL). |
| `GRIDLOCK_ERR_NOT_STOPPED` | `-4` | Target MPI rank is running or exited rather than stopped. |
| `GRIDLOCK_ERR_INVALID_RANK` | `-5` | Requested rank index is outside `[0, rank_count)`. |
| `GRIDLOCK_ERR_EVAL_FAILED` | `-6` | Debugger expression evaluation failed. |
| `GRIDLOCK_ERR_MEMORY_READ_FAILED` | `-7` | Inferior memory read or write faulted. |
| `GRIDLOCK_ERR_UNSUPPORTED` | `-8` | Operation is unsupported or disallowed during plugin shutdown. |
| `GRIDLOCK_ERR_TIMEOUT` | `-9` | Synchronous GDB/MI request timed out before completion. |
| `GRIDLOCK_ERR_CANCELLED` | `-10` | Interactive input prompt cancelled by the user. |

### UI, Surface & Report Enumerations (`int32_t`)

* **`GridLockLogLevel`**: `GRIDLOCK_LOG_TRACE = 0`, `DEBUG = 1`, `INFO = 2`, `WARN = 3`, `ERROR = 4` (`TRACE` and `DEBUG` are filtered from console output unless `GRIDLOCK_PLUGIN_VERBOSE_LOGS=1` is set).
* **`GridLockDialogSeverity`**: `GRIDLOCK_DIALOG_INFO = 0`, `WARNING = 1`, `ERROR = 2`, `QUESTION = 3`.
* **`GridLockDialogButtons`**: `GRIDLOCK_BUTTONS_OK = 1`, `OK_CANCEL = 2`, `YES_NO = 3`.
* **`GridLockDialogResult`**: `GRIDLOCK_RESULT_OK = 1`, `CANCEL = 2`, `YES = 3`, `NO = 4`.
* **`GridLockContextSurface`**:
  * `GRIDLOCK_SURFACE_VARIABLES = 1` (`VariablesDockWidget` tree rows)
  * `GRIDLOCK_SURFACE_CODE_EDITOR = 2` (`SourceCodeView` editor viewport and selection)
  * `GRIDLOCK_SURFACE_CALL_STACK = 3` (`CallStackDockWidget` frame rows)
  * `GRIDLOCK_SURFACE_BREAKPOINTS = 4` (`SourceCodeView` breakpoint gutter menu)
  * `GRIDLOCK_SURFACE_RANK_LIST = 5` (`ServerRackView` MPI rank blade entries)
* **`GridLockReportFormat`**:
  * `GRIDLOCK_REPORT_MARKDOWN = 1` (`QTextBrowser` Markdown view)
  * `GRIDLOCK_REPORT_KEY_VALUE_TABLE = 2` (2-column `QTableWidget` from JSON object/array or `key=value` lines)
  * `GRIDLOCK_REPORT_JSON_TREE = 3` (2-column expandable `QTreeWidget` from hierarchical JSON)

### `GridLockSurfaceContext` & `GridLockThemeInfo`

* **`GridLockSurfaceContext`**: Delivers `surface`, `rank`, `frame_level`, `line`, `column`, `bp_id`, `address`, `symbol_utf8`, `type_utf8`, `file_utf8`, and `selected_text_utf8` to `GridLockContextActionFn`.
* **`GridLockThemeInfo`**: Fixed-size `212`-byte POD struct containing `is_dark` (`1` dark, `0` light), `theme_id_utf8[64]`, and `#RRGGBB` tokens `bg_primary_hex`, `bg_secondary_hex`, `fg_primary_hex`, `fg_muted_hex`, `accent_hex`, `selection_hex`, `warning_hex`, and `error_hex`.

### `GridLockHostApi` Callbacks (All 26 Entry Points)

`PluginHostBridge` (`src/core/plugins/PluginHostBridge.{hpp,cpp}`, `PluginHostBridgeUi.cpp`) implements every callback with `noexcept` boundaries, GUI/coordinator thread marshaling (`Qt::BlockingQueuedConnection` when invoked off-thread), tokenized private memory MI channels (`<= 64 MiB` read with local `process_vm_readv` fast path, `<= 16 MiB` synchronous write), GDB/MI breakpoint sanitization, string/item quotas (`title <= 256 B` without control characters, `body/content <= 1 MiB`, `<= 16` status items per plugin, `<= 256` annotations per plugin, `<= 4` menu path segments), and PCRE2 match/recursion limits (`(*LIMIT_MATCH=10000)(*LIMIT_RECURSION=250)`) on regexes (`<= 512` bytes).

| Callback | Signature Summary | Behavior |
| :--- | :--- | :--- |
| `alloc_bytes` | `void* (host_ctx, num_bytes)` | Allocates `num_bytes` on the host heap. |
| `free_bytes` | `void (host_ctx, ptr)` | Releases memory previously returned by `alloc_bytes`. |
| `log_message` | `GridLockStatus (host_ctx, level, utf8_msg)` | Writes a bounded (`<= 16 KiB`) log entry coalesced onto the console sink. |
| `get_rank_count` | `GridLockStatus (host_ctx, out_ranks)` | Returns the active MPI session rank count. |
| `get_active_rank` | `GridLockStatus (host_ctx, out_rank)` | Returns the currently focused zero-based MPI rank index. |
| `eval_expr` | `GridLockStatus (host_ctx, rank, expr_utf8, out_utf8, cap, out_len)` | Evaluates a C/C++ expression on a stopped rank into caller-owned `out_utf8`. |
| `read_memory` | `GridLockStatus (host_ctx, rank, address, out_bytes, num_bytes, out_read)` | Reads `num_bytes` (`<= 64 MiB`) from `address` on `rank` without disturbing `MemView`. |
| `write_memory` | `GridLockStatus (host_ctx, rank, address, in_bytes, num_bytes, out_written)` | Writes `num_bytes` (`<= 16 MiB`) synchronously via tokenized `-data-write-memory-bytes`. |
| `set_breakpoint` | `GridLockStatus (host_ctx, file_utf8, line, condition_utf8, out_bp_id)` | Validates and quotes `file_utf8:line` and optional condition before installing a breakpoint. |
| `remove_breakpoint` | `GridLockStatus (host_ctx, bp_id)` | Removes a breakpoint previously created via `set_breakpoint`. |
| `register_variable_action` | `GridLockStatus (host_ctx, action_id, menu_label, type_regex, cb, plugin_ctx)` | Registers a PCRE2-bounded regex context action for `VariablesDockWidget`. |
| `register_command_action` | `GridLockStatus (host_ctx, command_id, menu_label, shortcut_hint, cb, plugin_ctx)` | Registers a flat command under **Tools -> Plugins**. |
| `emit_structured_event` | `GridLockStatus (host_ctx, channel_utf8, json_payload_utf8)` | Emits a UTF-8 JSON payload (`<= 1 MiB`) on `channel_utf8`. |
| `show_message_box` | `GridLockStatus (host_ctx, severity, buttons, title_utf8, body_utf8, out_result)` | Marshals to the GUI thread, displays a modal `QMessageBox`, and writes `GridLockDialogResult`. |
| `prompt_input` | `GridLockStatus (host_ctx, title_utf8, label_utf8, default_utf8, out_utf8, cap, out_len)` | Marshals to the GUI thread for single-line UTF-8 input (`GRIDLOCK_ERR_CANCELLED` on cancel). |
| `show_toast` | `GridLockStatus (host_ctx, severity, title_utf8, body_utf8, duration_ms)` | Shows a timed non-modal toast banner (`500..30000 ms`) and status-bar message. |
| `register_menu_action` | `GridLockStatus (host_ctx, action_id, menu_path_utf8, shortcut_hint, cb, plugin_ctx)` | Builds slash-delimited nested submenus (`<= 4` segments) under **Tools -> Plugins**. |
| `set_status_item` | `GridLockStatus (host_ctx, item_id, text_utf8, tooltip_utf8, color_token_utf8, cb, plugin_ctx)` | Creates or updates up to `16` clickable status-bar badges (`accent`, `warning`, `error`, or `#RRGGBB`). |
| `remove_status_item` | `GridLockStatus (host_ctx, item_id)` | Removes a status-bar badge owned by the calling plugin. |
| `register_context_action` | `GridLockStatus (host_ctx, surface, action_id, menu_label, filter_regex, cb, plugin_ctx)` | Adds an optional-regex **Plugin Actions** item on any `GridLockContextSurface`. |
| `navigate_to_location` | `GridLockStatus (host_ctx, file_utf8, line, column)` | Opens `file_utf8` in `SourceCodeView` at 1-based `line:column`. |
| `set_editor_annotation` | `GridLockStatus (host_ctx, annotation_id, file_utf8, line, badge_utf8, detail_utf8, color_token_utf8)` | Renders up to `256` inline pill badges, line tints, and hover tooltips in `SourceCodeView`. |
| `clear_editor_annotations` | `GridLockStatus (host_ctx, id_prefix_utf8)` | Clears owned editor annotations matching `id_prefix_utf8` (`NULL` or `""` clears all). |
| `get_theme_info` | `GridLockStatus (host_ctx, out_theme)` | Populates `GridLockThemeInfo` from `ThemeManager` on the GUI thread. |
| `publish_report_view` | `GridLockStatus (host_ctx, panel_id, title_utf8, format, content_utf8)` | Opens or updates a declarative tab in `PluginReportDockWidget`. |
| `close_report_view` | `GridLockStatus (host_ctx, panel_id)` | Closes a report tab owned by the calling plugin. |

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
| `on_theme_changed` | `GridLockThemeChangedCallback` | No | Called whenever `ThemeManager` switches dark/light polarity or active palette (`const GridLockThemeInfo*`). |

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
  return host->log_message(host->host_ctx, GRIDLOCK_LOG_INFO, "Rank stopped");
}

static void GRIDLOCK_CALL on_theme(void* ctx, const GridLockThemeInfo* theme) {
  (void)ctx;
  (void)theme;
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
  out_desc->on_theme_changed = &on_theme;
  out_desc->shutdown = &on_shutdown;
  return GRIDLOCK_OK;
}
```

---

## 5. Optional C++23 Convenience Helpers (`include/gridlock/plugin_cpp_helpers.hpp`)

C++23 plugins may include [`include/gridlock/plugin_cpp_helpers.hpp`](../include/gridlock/plugin_cpp_helpers.hpp) (`namespace gridlock::plugin`) for `noexcept` `std::expected` error handling, `16 MiB`-bounded `evalExpr` and `promptInput` two-pass buffer retries, `getThemeInfo`, `std::is_trivially_copyable_v` `readPod<T>` helpers, `std::span` memory reads, forward-compatible `validateHostAbi`, and RAII host allocations:

```cpp
#include "gridlock/plugin_api.h"
#include "gridlock/plugin_cpp_helpers.hpp"

namespace gp = gridlock::plugin;

GridLockStatus inspectRank(const GridLockHostApi* host, int32_t rank, uint64_t ptrAddr) {
  auto theme = gp::getThemeInfo(host);
  if (!theme) return theme.error();

  auto tag = gp::promptInput(host, "Mesh Snapshot", "Label:", "step_0");
  if (!tag && tag.error() != GRIDLOCK_ERR_CANCELLED) return tag.error();

  auto countStr = gp::evalExpr(host, rank, "mesh.num_vertices");
  if (!countStr) return countStr.error();

  auto firstCoord = gp::readPod<double>(host, rank, ptrAddr);
  if (!firstCoord) return firstCoord.error();
  return GRIDLOCK_OK;
}
```

---

## 6. Workstation UI Integration, Staged Preferences, Self-Restart & GitHub Installer

* **Tools -> Plugins Nested Menus & Shortcut Checks (`MainWindowPlugins.cpp`)**: Materializes flat commands (`register_command_action`) and `"/"`-delimited submenu trees (`register_menu_action`, e.g., `"Mesh Tools/Refinement/Export VTK"`). Shortcuts are checked against `ShortcutManager::registeredBindings()` before assignment so plugin hints never override built-in debugger keybindings.
* **Multi-Surface Context Menus**: Populates a **Plugin Actions** submenu on right-click across `VariablesDockWidget` (`GRIDLOCK_SURFACE_VARIABLES` + `register_variable_action`), `SourceCodeView` (`GRIDLOCK_SURFACE_CODE_EDITOR` and `GRIDLOCK_SURFACE_BREAKPOINTS` gutter menu), `CallStackDockWidget` (`GRIDLOCK_SURFACE_CALL_STACK`), and `ServerRackView` (`GRIDLOCK_SURFACE_RANK_LIST`).
* **Status-Bar Badges, Editor Annotations & `PluginReportDockWidget`**:
  * `set_status_item` mounts clickable `QToolButton` pills on `MainWindow::statusBar()` styled with active theme tokens.
  * `navigate_to_location` opens the target source file at `line:column` via `MainWindow::openFileAtLine()`, while `set_editor_annotation` renders line background tints, inline rounded pill badges, detail text, and hover tooltips in `SourceCodeView`.
  * `PluginReportDockWidget` (`src/ui/docks/PluginReportDockWidget.{hpp,cpp}`) renders closable tabs for `MARKDOWN`, `KEY_VALUE_TABLE`, and `JSON_TREE` reports without exposing GUI pointers across the C99 boundary.
* **Preferences -> Plugins (`Installed` Tab)**:
  * Displays a 7-column table: **Global** (checkbox), **Workspace** (checkbox), **Name**, **Version**, **Scope / ABI**, **Status**, and **Trust** (**Trust & Approve (<sha8>)** for untrusted workspace binaries).
  * Checkbox and trust edits are staged in memory without mutating `ConfigManager` on click; **Cancel** discards staged edits and **Apply** / **OK** commits them.
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
gcc -std=c99 -O2 -Wall -Wextra -Wpedantic -Werror -fvisibility=hidden -fPIC -shared \
    -Iinclude my_plugin.c -o libmy_plugin.so

# C++23 plugin
g++ -std=c++23 -O2 -Wall -Wextra -Wpedantic -Werror -fvisibility=hidden -fvisibility-inlines-hidden \
    -fPIC -shared -Iinclude my_plugin.cpp -o libmy_plugin.so

# Verify that only gridlock_plugin_init_v1 is exported
nm -D --defined-only libmy_plugin.so | awk '$2 == "T" { print $3 }'
```

### Sample Plugin: `examples/plugins/domain_mesh_probe/`

[`examples/plugins/domain_mesh_probe/`](../examples/plugins/domain_mesh_probe/) provides a standalone C++23 plugin (`meson.build` and `Makefile`) that registers **Dump Mesh Refinement Step** for `.*Mesh.*` variables, reads stopped-rank mesh vertex coordinates, writes `0600` VTK snapshots inside a `0700` `$XDG_RUNTIME_DIR/gridlock/mesh_probe` runtime directory (`O_NOFOLLOW`), and emits JSON events on channel `domain.mesh_probe.step`.
