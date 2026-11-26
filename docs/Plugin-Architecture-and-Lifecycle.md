# Plugin Architecture, Lifecycle, Security & Build Pipeline (Developer Encyclopedia - Volume 1)

This reference documents the host-side architecture of GridLock's native C99 plugin subsystem (`src/core/plugins/` and `src/ui/`), including component ownership, `dlopen` pre-verification, sandboxed `PluginVerifier` conformance checking, live in-place `hotReloadPlugins`, silent GDB/MI plugin channels, callback re-entrancy and threading state machines, enforced memory/regex/UI security quotas, and the shell-free multi-forge `PluginInstaller` + `PluginManifest` build and update pipeline.

See also:
* [Using Plugins in GridLock](Plugins.md) (End-User Guide)
* [C99 Plugin ABI & C++23 Wrapper Specification](Plugin-C99-ABI-Reference.md) (Volume 2)
* [Plugin Authoring, UI Surfaces & Build Recipes](Plugin-Authoring-and-UI-Surfaces.md) (Volume 3)

---

## 1. System Architecture & Module Ownership

GridLock isolates dynamically loaded C99/C++23 shared libraries behind a zero-Qt C99 function-pointer table (`GridLockHostApi` in [`include/gridlock/plugin_api.h`](../include/gridlock/plugin_api.h)). Every loaded plugin receives a private `GridLockHostApi` copy whose `host_ctx` points to a dedicated `PluginBindingContext`, enabling `PluginHostBridge` to attribute every allocation, action, menu, status badge, breakpoint, annotation, and report panel to its owning plugin and purge them deterministically prior to `QLibrary::unload()`.

```mermaid
flowchart TD
  subgraph PluginSpace["Out-of-Tree Native Plugins (.so / .dylib / .dll)"]
    P1["Plugin A (gridlock_plugin_init_v1)"]
    P2["Plugin B (gridlock_plugin_init_v1)"]
  end

  subgraph CorePluginSubsystem["Core Plugin Subsystem (src/core/plugins/)"]
    PM["PluginManager (Discovery, SHA-256 Gate, hotReloadPlugins, STB_GNU_UNIQUE Check)"]
    CTX1["PluginBindingContext A + Dedicated GridLockHostApi (312 B)"]
    CTX2["PluginBindingContext B + Dedicated GridLockHostApi (312 B)"]
    PHB["PluginHostBridge + PluginHostBridgeUi (28 C99 Callbacks, Quotas, DWARF Range)"]
    PI["PluginInstaller (Shell-Free QProcess Clone, Build, startUpdatePlugins)"]
    PV["PluginVerifier (verifyBinary: --verify-plugin & Post-Build Sandbox Gate)"]
    PMAN["PluginManifest (Multi-Forge URL Normalizer, plugins.toml, plugins-lock.toml)"]
  end

  subgraph PersistenceAndBackend["Config & Debugger Backend (src/core/)"]
    CFG["ConfigManager (config.toml Trust Allowlist, Global/Workspace Flags)"]
    COORD["GdbRankCoordinator (rankStopped, Silent m_pendingPluginEvals / m_pendingPluginMemReads)"]
    PVM["NativeMemoryReader (Linux process_vm_readv Fast Path)"]
  end

  subgraph WorkstationUI["Workstation UI Surfaces (src/ui/)"]
    MWP["MainWindowPlugins (Dialogs, Toasts, Menus, Status Badges, Hot-Reload Banner)"]
    VDW["VariablesDockWidget (Type-Regex + Surface 1 Context Menus)"]
    SCV["SourceCodeView (Editor Annotations + Surface 2 & 4 Context Menus)"]
    CSD["CallStackDockWidget + ServerRackView (Surface 3 & 5 Context Menus)"]
    PRD["PluginReportDockWidget (Markdown, Key-Value Table, JSON Tree)"]
    PSP["PluginSettingsPage (8-Col Table, Update / Update All, Hot-Reload, Git Installer)"]
  end

  P1 <-->|"C99 ABI v1.0"| CTX1
  P2 <-->|"C99 ABI v1.0"| CTX2
  CTX1 --> PHB
  CTX2 --> PHB
  PM --> CTX1
  PM --> CTX2
  PM <--> PHB
  PM <--> CFG
  PM <--> PMAN
  COORD -->|"IBackendCoordinator::rankStopped"| PM
  PHB <-->|"runOnObjectThread + CoordinatorReplyWaiter"| COORD
  COORD --> PVM
  PHB -->|"runOnGuiThread + Qt Signals"| MWP
  PHB <--> VDW
  PHB <--> SCV
  PHB <--> CSD
  MWP --> PRD
  PSP --> PM
  PSP --> PI
  PI --> PV
  PV -.->|"Isolated Sandbox"| PM
  PI <--> PMAN
  PI --> CFG
```

### Subsystem Translation Units & Thread Affinity

| Translation Unit | Primary Classes / Structs | Thread Affinity & Responsibility |
| :--- | :--- | :--- |
| `include/gridlock/plugin_api.h` | `GridLockHostApi` (`312 B`), `GridLockPluginDescriptor` (`160 B`), 7 POD context/theme/DWARF structs | Pure C99 ABI header (`GRIDLOCK_CALL`, zero Qt types, 16-byte versioned struct headers, `void* reserved[8]`). |
| `include/gridlock/plugin_cpp_helpers.hpp` | `HostDeleter`, `HostByteBuffer`, `evalExpr`, `promptInput`, `getThemeInfo`, `queryTypeInfo`, `resolveContiguousRange`, `readBytes`, `readPod<T>` | Optional header-only `noexcept` C++23 RAII and `std::expected` wrappers with zero Qt dependencies. |
| `src/core/plugins/PluginUiModels.hpp` | `PluginBindingContext`, `RegisteredMenuAction`, `PluginStatusItem`, `RegisteredContextAction`, `PluginEditorAnnotation`, `PluginReportPanel` | Host-side value models and per-plugin `std::atomic<bool> shuttingDown` + `liveAllocCount` / `liveAllocBytes` counters. |
| `src/core/plugins/PluginManager.{hpp,cpp}` | `PluginManager`, `LoadedPluginInfo`, `ActivePluginHandle`, `ScopedCallbackDepth` | GUI/owner thread. Directory scanning, POSIX/symlink checks, pre-`dlopen` `SHA-256` workspace gate, `plugins-lock.toml` provenance matching (`refreshInstalledLockMetadata`), `hasGnuUniqueSymbols` / `hotReloadPlugins`, `m_callbackDepth` deferral, and FIFO `m_pendingStops` dispatch. |
| `src/core/plugins/PluginHostBridge.cpp` | `PluginHostBridge`, `CoordinatorReplyWaiter` | Thread-safe (`m_mutex` + `runOnObjectThread` / `runOnGuiThread`). Lifecycle staging, tracked host heap (`alloc_bytes`/`free_bytes`), silent `eval_expr` (`m_pendingPluginEvals`), silent `read_memory`/`write_memory` (`m_pendingPluginMemReads`), zero-call DWARF `query_type_info` and `resolve_contiguous_range`, and sanitized breakpoints. |
| `src/core/plugins/PluginHostBridgeUi.cpp` | `PluginHostBridge` UI callbacks, `CachedPromptReply` | Thread-safe (`noexcept` + `runOnGuiThread`). Dialogs, two-pass cached `prompt_input`, toasts, nested menus, status badges, 5-surface regex actions, annotations, theme queries, and report views. |
| `src/core/plugins/PluginVerifier.{hpp,cpp}` | `PluginVerifier`, `PluginVerifyReport` | Headless/sandboxed verifier (`verifyBinary`) invoked by `gridlock --verify-plugin <path.so>` and `PluginInstaller::finalizeInstalledArtifacts()`. Checks ELF64 `SHT_DYNSYM` / `nm -D` exports, `STB_GNU_UNIQUE` warnings, ABI headers, action regexes/menus, dry-run session lifecycle, and `outstandingAllocBytes() == 0`. |
| `src/core/plugins/PluginManifest.{hpp,cpp}` | `PluginManifest`, `PluginManifestEntry`, `PluginLockEntry` | Multi-forge Git URL normalization (`GitHub`, `GitLab`, `Codeberg`, `CustomUrl`, `file://`), `toml++` manifest/lockfile I/O, and `git ls-remote` / `SHA-256` freshness checks. |
| `src/core/plugins/PluginInstaller.{hpp,cpp}` | `PluginInstaller`, `PluginBuildResult`, `PluginUpdateTarget`, `GitForgeHost` | Asynchronous `QProcess` state machine driving `git clone`, Meson/CMake/Make/single-file compilation, `PluginVerifier` gating, atomic `.tmp.<pid>` `rename()`, `syncFromManifests()`, and sequential `startUpdatePlugins()`. |
| `src/ui/MainWindowPlugins.cpp` | `MainWindow` plugin slots | GUI thread. Renders `QMessageBox`, `QInputDialog`, floating toast banners, `Tools -> Plugins` submenu trees (`ShortcutManager` collision check), status-bar pills, `#mainWindowPluginRestartBanner` (**Hot-Reload Now**), `ThemeManager` sync, and `PluginReportDockWidget` routing. |
| `src/ui/dialogs/preferences/PluginSettingsPage.{hpp,cpp}` | `PluginSettingsPage` | GUI thread. 8-column `#pluginsTableWidget` (`Version (<commit_short>)`, per-row **Update**, toolbar **Update All**), staged `Global` / `Workspace` / `Trust` edits, `#pluginRestartBanner` (**Hot-Reload Now** / **Restart GridLock Now**), multi-forge Git installer (`#pluginInstallTab`), and `Sync plugins.toml`. |

---

## 2. Discovery, Lockfile Provenance, Init Staging, Verification & Hot-Reload

`PluginManager::discoverAndLoad()` scans `~/.config/gridlock/plugins/` (`PluginScope::UserGlobal`) followed by `<workspace>/.gridlock/plugins/` (`PluginScope::Workspace`), matching each binary's `SHA-256` (falling back to `binary_name`) against `~/.config/gridlock/plugins-lock.toml` and `plugins.toml` (`populateProvenance()`) to populate `gitCommitSha`, `gitCommitShort`, `gitRepo`, `gitRef`, and `buildSystem` on `LoadedPluginInfo` (emitting diagnostics such as `Loaded STL Container Inspector 0.1.0 (a1b2c3d) [ABI 1.0]`).

```mermaid
sequenceDiagram
  participant PM as PluginManager
  participant LOCK as plugins-lock.toml / ConfigManager
  participant PHB as PluginHostBridge
  participant LIB as QLibrary (.so/.dylib/.dll)
  participant COORD as GdbRankCoordinator

  PM->>PM: Scan searchDirectories(), reject world-writable (st_mode & 002) or escaping symlinks
  PM->>PM: Compute lowercase SHA-256 & check hasGnuUniqueSymbols() before dlopen()
  PM->>LOCK: Match SHA-256 / binary_name -> gitCommitShort, gitRepo, gitRef, buildSystem
  alt Workspace Scope & Untrusted SHA-256
    PM->>LOCK: isWorkspacePluginTrusted(canonicalPath, sha256) -> false
    Note over PM,LIB: QLibrary::load() is NOT called (ELF .init_array constructors never run)
  else Trusted & Effectively Enabled
    PM->>LIB: load() with QLibrary::ResolveAllSymbolsHint
    PM->>LIB: resolve("gridlock_plugin_init_v1")
    PM->>PHB: beginInitStaging() + makeHostApi(bindingCtx)
    PM->>LIB: gridlock_plugin_init_v1(pluginApi, &desc) [ScopedCallbackDepth]
    LIB-->>PHB: Stage menus, context actions, status items, breakpoints
    alt ABI / Descriptor / Duplicate ID Invalid or Disabled
      PM->>PHB: bindingCtx->shuttingDown = true, shutdown() if ABI-safe, discardStagedActions()
      PM->>LIB: unload()
    else Validation Passed
      PM->>LOCK: Cache metadata in workstationSettingsPath() (plugin_cache/<hexPath>)
      PM->>PHB: commitStagedActions(pluginId, pluginApi) -> emit UI update signals
      opt Hot-Reload During Active MPI Session (activeSessionRankCount > 0)
        PM->>LIB: notifySessionStarted(activeSessionRankCount) -> on_session_start
      end
      COORD->>PM: IBackendCoordinator::rankStopped -> FIFO notifyRankStopped -> on_rank_stopped
      PM->>PHB: bindingCtx->shuttingDown.store(true, release)
      PM->>LIB: on_session_end (if active) -> shutdown(plugin_ctx, pluginApi) [ScopedCallbackDepth]
      PM->>PHB: unregisterPluginResources(pluginId)
      PM->>LIB: unload()
    end
  end
```

### Live Hot-Reload (`PluginManager::hotReloadPlugins`) & `STB_GNU_UNIQUE` Pinning

* **In-Place Reload & Session Replay**: Calling `PluginManager::hotReloadPlugins(int activeSessionRankCount)` verifies `m_callbackDepth == 0` (returning `-1` if a plugin callback is currently executing on the call stack), invokes `discoverAndLoad()` to cleanly shut down, purge UI widgets/actions, and `QLibrary::unload()` old binaries before loading updated binaries from disk, and replays `notifySessionStarted(activeSessionRankCount)` when an MPI debug session is active (`activeSessionRankCount > 0`).
* **Non-Destructive Provenance Refresh (`refreshInstalledLockMetadata`)**: When `PluginInstaller` finishes building an update on disk, `PluginSettingsPage::onInstallFinished` calls `PluginManager::refreshInstalledLockMetadata()` to re-hash discovered binaries and refresh `gitCommitShort` / `requiresRestart` flags immediately without forcing `dlclose()` until the user clicks **Hot-Reload Now**.
* **Linux `STB_GNU_UNIQUE` Detection (`hasGnuUniqueSymbols`)**: On Linux (`glibc`), C++ inline static locals and template static data members compiled without `-Wl,--version-script` emit `STB_GNU_UNIQUE` (`'u'`) dynamic symbols that cause `dlopen()` to mark the DSO `DF_1_NODELETE`. `PluginManager::hasGnuUniqueSymbols()` inspects dynamic symbols prior to load and records pinned paths in `m_pinnedGnuUniquePaths` so `hasLoadedGnuUniquePlugins()` can warn the user to click **Restart GridLock Now**.

> [!IMPORTANT]
> **Sandboxed Conformance Gate (`PluginVerifier::verifyBinary`)**: Both `PluginInstaller::finalizeInstalledArtifacts()` and `gridlock --verify-plugin <path.so>` execute `PluginVerifier::verifyBinary()` inside a dedicated temporary `PluginManager` + `PluginHostBridge` instance with `GRIDLOCK_TEST_CONFIG_DIR` isolation. A binary fails installation or CLI verification if `gridlock_plugin_init_v1` is missing, ABI headers or required UTF-8 fields (`plugin_id_utf8`, `plugin_name_utf8`, `plugin_version_utf8`) are invalid, staged PCRE2 regexes or menu paths fail validation, lifecycle hooks (`init` -> `on_session_start(2)` -> `on_session_end()` -> `shutdown()`) return non-`GRIDLOCK_OK`, or `outstandingAllocBytes() != 0` after `shutdown()`. Extra global `STT_FUNC` exports and `STB_GNU_UNIQUE` symbols are surfaced in `PluginVerifyReport::warnings`.

---

## 3. Re-Entrancy, Silent MI Channels & Teardown State Machine

Synchronous host queries (`eval_expr`, `read_memory`, `write_memory`, `query_type_info`, `resolve_contiguous_range`) wait on coordinator MI replies using a local `CoordinatorReplyWaiter` (`QEventLoop::ExcludeUserInputEvents`). Because Qt timers and queued socket deliveries remain active while waiting, `PluginManager`, `PluginHostBridge`, and `GdbRankCoordinator` coordinate callback depth, silent MI token routing, and atomic shutdown lockouts.

```mermaid
stateDiagram-v2
  [*] --> Unloaded
  Unloaded --> PreLoadVerify : discoverAndLoad() / loadPluginFromPath() / hotReloadPlugins()
  PreLoadVerify --> BlockedUntrusted : Workspace SHA-256 unapproved or world-writable / symlink escape
  PreLoadVerify --> InitStaging : Regular binary + trusted + enabled
  InitStaging --> Unloaded : ABI mismatch / duplicate ID then discardStagedActions() + unload()
  InitStaging --> ActiveIdle : commitStagedActions() (+ replay on_session_start if hot-reloaded)

  ActiveIdle --> InCallback : ScopedCallbackDepth (++m_callbackDepth)
  InCallback --> StopQueueDrain : rankStopped arrives while m_dispatchingStop == true (enqueue PendingStopEvent)
  StopQueueDrain --> InCallback : Drain next FIFO PendingStopEvent at top-level loop
  InCallback --> DeferredReload : unloadAll() or discoverAndLoad() called while m_callbackDepth > 0 (hotReloadPlugins returns -1)
  InCallback --> ActiveIdle : m_callbackDepth == 0 (no pending deferred flags)
  DeferredReload --> ShuttingDownLockedOut : leaveCallbackScope() unwinds to m_callbackDepth == 0

  ActiveIdle --> ShuttingDownLockedOut : unloadAll() / hotReloadPlugins() / setPluginEnabled(false) / ~PluginManager()
  ShuttingDownLockedOut --> Unloaded : on_session_end then shutdown then unregisterPluginResources then QLibrary::unload()
```

### Concurrency, Silent MI Routing & Teardown Invariants

| Mechanism | Location | Enforced Guarantee |
| :--- | :--- | :--- |
| **`m_callbackDepth` & `ScopedCallbackDepth`** | `PluginManager.{hpp,cpp}`, `PluginHostBridge.cpp` | Incremented across `gridlock_plugin_init_v1`, `on_session_start`, `on_rank_stopped`, `on_session_end`, `on_theme_changed`, `shutdown`, and every UI/context/status callback (`m_callbackScopeHook`). Calling `unloadAll()` or `discoverAndLoad()` while `m_callbackDepth > 0` defers `QLibrary::unload()` until depth `0`; `hotReloadPlugins()` returns `-1` immediately so UI callers fall back safely. |
| **Silent Plugin MI Channels (`m_pendingPluginEvals` & `m_pendingPluginMemReads`)** | `GdbRankCoordinator.{hpp,cpp}`, `GdbMiParser.cpp`, `PluginHostBridge.cpp` | `requestPluginExpressionEval` records MI tokens in `m_pendingPluginEvals` and emits `pluginExpressionReady` **without** emitting `expressionEvaluated`, preventing internal plugin probes (`sizeof(...)`, container bounds) from polluting the user's **Evaluator** dock. Likewise, `m_pendingPluginMemReads` routes `-data-read-memory-bytes` replies to `pluginMemoryChunkReady` without mutating `MemView`. |
| **Zero-Call DWARF Introspection (`query_type_info`, `resolve_contiguous_range`)** | `PluginHostBridge.cpp` | Creates and deletes ephemeral MI variable objects (`-var-create` / `-var-delete`) plus silent `sizeof` and struct-member reads (`libstdc++` `_M_impl._M_start`/`_M_finish`, `libc++` `__begin_`/`__end_`, `std::span`, `std::array`, `std::valarray`, `T[N]`), avoiding inferior function calls that fail on `-O2` inlined templates or core dumps. |
| **FIFO `m_pendingStops` & `m_dispatchingStop`** | `PluginManager.cpp` | Copies `GridLockStopInfo` strings into owned `QByteArray` buffers (`PendingStopEvent`) and enqueues them. If a synchronous `eval_expr` or `read_memory` call inside `on_rank_stopped` spins a `QEventLoop` while another rank stops, the new stop waits in `m_pendingStops` and dispatches strictly after the active stop iteration completes. |
| **Dedicated `IBackendCoordinator::rankStopped`** | `GdbMiParser.cpp`, `PluginManager.cpp` | Emitted exclusively on genuine GDB/MI `*stopped` records. Clicking frames in `CallStackDockWidget` (`selectStackFrame()`) updates `VariablesDockWidget` without firing `on_rank_stopped`. |
| **`PluginBindingContext::shuttingDown` Lockout** | `PluginUiModels.hpp`, `PluginManager.cpp`, `PluginHostBridge*.cpp` | Stored with `std::memory_order_release` before `on_session_end` / `shutdown` and checked with `std::memory_order_acquire`. Rejects late action/menu/context/status/annotation/report registrations, modal dialogs, breakpoint insertions (`GRIDLOCK_ERR_UNSUPPORTED`), and coordinator queries (`GRIDLOCK_ERR_NOT_STOPPED`). |
| **Cross-Thread Marshaling (`runOnGuiThread` / `runOnObjectThread`)** | `PluginHostBridge.cpp` | Executes directly when called from `PluginHostBridge::thread()`; otherwise marshals via `QMetaObject::invokeMethod(..., Qt::BlockingQueuedConnection)`, re-checking `ctx->shuttingDown` inside the target-thread lambda. |
| **`CoordinatorReplyWaiter` Abort & Token Cleanup** | `PluginHostBridge.cpp`, `GdbRankCoordinator` | Aborts the local `QEventLoop` immediately on `rankStateChanged` (`currentState != "stopped"`), `sessionErrorOccurred`, `QObject::destroyed`, or `QTimer::timeout` (`default 2500 ms`), cancelling pending MI tokens so late replies never touch freed stack state. |
| **Two-Pass `CachedPromptReply`** | `PluginHostBridgeUi.cpp` | When `cbPromptInput` is probed with a small buffer (`cap <= payloadLen`) and returns `GRIDLOCK_ERR_BUFFER_TOO_SMALL`, the accepted UTF-8 input is cached under `(pluginId, title, label)` so the immediate resized retry (`plugin_cpp_helpers.hpp`) copies the answer without opening a second `QInputDialog`. |
| **Exception Firewalls (`noexcept` + `try / catch (...)`)** | `PluginHostBridge.{hpp,cpp}`, `PluginHostBridgeUi.cpp` | Every `GRIDLOCK_CALL` static callback is `noexcept` and wrapped in `try { ... } catch (...)`, translating unexpected C++ exceptions into `GRIDLOCK_ERR_UNSUPPORTED` (or `nullptr` for `alloc_bytes`) so exceptions never cross the C99 ABI boundary. |

---

## 4. Security Gates & Enforced Resource Quotas

> [!WARNING]
> Every pointer, string length, integer range, filesystem path, and PCRE2 pattern crossing the C99 ABI or `PluginInstaller` boundary is validated before dereferencing or execution. Violations return negative `GridLockStatus` codes without crashing or stalling the host GUI.

| Boundary / Operation | Enforced Quota or Gate | Rejection Status / Behavior |
| :--- | :--- | :--- |
| **Plugin Directory & Binary Permissions** | Rejects POSIX world-writable directories or shared libraries (`(st.st_mode & 002) != 0`). | Skipped (directory) or recorded as `Rejected world-writable plugin binary` prior to `dlopen()`. |
| **Symlink Containment** | `QFileInfo::canonicalFilePath()` of every candidate `.so`/`.dylib`/`.dll` must begin with `<canonicalPluginRoot>/`. | Recorded as `Rejected symlink escaping plugin directory` without `dlopen()`. |
| **Workspace Binary `SHA-256` Gate** | Computes lowercase `SHA-256` prior to `QLibrary::load()` and checks `[plugins.trusted_workspace_binaries]` in `~/.config/gridlock/config.toml`. | Blocked before `dlopen()`; ELF `.init_array` constructors cannot execute until approved. |
| **`read_memory` Size & Overflow** | `numBytes <= 64 MiB` (`67,108,864 B`), `address <= UINT64_MAX - (numBytes - 1)`, `64 KiB` MI read chunks (`kPluginReadChunkBytes`), Linux `process_vm_readv` local fast path. | `GRIDLOCK_ERR_INVALID_ARG` on bounds/wrap; `GRIDLOCK_ERR_MEMORY_READ_FAILED` or `GRIDLOCK_ERR_TIMEOUT` on fault/timeout. |
| **`write_memory` Size & Overflow** | `numBytes <= 16 MiB` (`16,777,216 B`), `address <= UINT64_MAX - (numBytes - 1)`, `16 KiB` MI write chunks (`kPluginWriteChunkBytes`). | `GRIDLOCK_ERR_INVALID_ARG` on bounds/wrap; `GRIDLOCK_ERR_MEMORY_READ_FAILED` or `GRIDLOCK_ERR_TIMEOUT` on fault/timeout. |
| **`plugin_cpp_helpers.hpp` String Retry Cap** | `kMaxEvalExprBytes = 16 MiB` (`16,777,216 B`) on two-pass `evalExpr` and `promptInput` retries. | `std::unexpected(GRIDLOCK_ERR_BUFFER_TOO_SMALL)` if reported `*out_len >= 16 MiB`. |
| **Breakpoints (`set_breakpoint`)** | `line > 0`, non-empty `file_utf8` with zero control characters (`< 0x20`, `0x7F`, `QChar::Other_Control`), and `validateSafeDebuggerInput(condition)` rejecting newlines/MI command injection. | `GRIDLOCK_ERR_INVALID_ARG`. |
| **PCRE2 Regexes (`type_regex`, `filter_regex`)** | Pattern length `<= 512 B` (`kMaxTypeRegexBytes`), prepended with `(*LIMIT_MATCH=10000)(*LIMIT_RECURSION=250)`, `compiled.optimize()`, and `2048`-char input truncation evaluated outside `m_mutex`. | `GRIDLOCK_ERR_INVALID_ARG` on oversized or invalid regex; ReDoS backtracking aborts cleanly without holding `m_mutex`. |
| **Identifiers, Titles, Labels & Menu Paths** | `<= 256 B` (`kMaxTitleBytes`) via `strnlen`, non-empty where required, and zero ASCII/Unicode control characters. | `GRIDLOCK_ERR_INVALID_ARG`. |
| **Nested Menu Path (`register_menu_action`)** | `1..4` non-empty `"/"`-delimited segments (`kMaxMenuPathDepth = 4`); shortcut hints checked against `ShortcutManager::registeredBindings()`. | `GRIDLOCK_ERR_INVALID_ARG` if depth `> 4` or empty segment; colliding shortcuts silently omitted in UI. |
| **Dialog Bodies, JSON Events & Report Content** | `<= 1 MiB` (`kMaxPayloadBytes = 1,048,576 B`) via `strnlen`. | `GRIDLOCK_ERR_INVALID_ARG`. |
| **`prompt_input` Default Value** | `<= 64 KiB` (`kMaxPromptDefaultBytes = 65,536 B`). | `GRIDLOCK_ERR_INVALID_ARG`. |
| **File Paths, Tooltips & Annotation Details** | `<= 4096 B` (`file_utf8`, `tooltip_utf8`, `detail_utf8`); color token `<= 64 B` (`accent` `#38bdf8`, `warning` `#f59e0b`, `error` `#ef4444`, or `#RRGGBB`). | `GRIDLOCK_ERR_INVALID_ARG`. |
| **Toast Duration (`show_toast`)** | Clamped to `[500, 30000]` ms (`<= 0` defaults to `4000` ms). | Clamped in-place (`GRIDLOCK_OK`). |
| **Per-Plugin Status Items (`set_status_item`)** | `<= 16` distinct `item_id` entries per plugin (`kMaxStatusItemsPerPlugin = 16`). | `GRIDLOCK_ERR_INVALID_ARG` when quota is exceeded or `item_id` belongs to another plugin. |
| **Per-Plugin Editor Annotations** | `<= 256` distinct `annotation_id` entries per plugin (`kMaxAnnotationsPerPlugin = 256`). | `GRIDLOCK_ERR_INVALID_ARG` when quota is exceeded or `annotation_id` belongs to another plugin. |
| **Plugin Log Sink (`log_message`)** | Truncated at `16 KiB` (`kMaxLogMessageBytes`); `TRACE`/`DEBUG` suppressed unless `GRIDLOCK_PLUGIN_VERBOSE_LOGS=1`; UI flushes up to `512` coalesced lines every `30 ms`. | Prevents console UI starvation under high-frequency plugin logging. |

---

## 5. `PluginInstaller` Build/Update Pipeline & `PluginManifest` Reference

`PluginInstaller` and `PluginManifest` never invoke `/bin/sh -c` or shell command strings. Every `git`, `meson`, `ninja`, `cmake`, `make`, `c++`, and `cc` invocation runs directly through `QProcess::start(program, QStringList args)` with `GIT_TERMINAL_PROMPT=0` and `LC_ALL=C.UTF-8`. `CompilerLogHighlighter` colors `==>` steps in `#38bdf8`, warnings in `#f59e0b`, and errors in `#ef4444`.

### Multi-Forge URL & Shorthand Normalization (`PluginManifest::normalizeGitUrl`)

All inputs reject empty strings, leading `-` characters, whitespace, ASCII/Unicode control/format characters, `@` userinfo in HTTPS authorities, and `.` / `..` or leading-`-` path segments:

| Input Pattern | `defaultForge` | Segment Bounds | Normalized Clone URL |
| :--- | :--- | :---: | :--- |
| `github:<owner>/<repo>` | Any | `2` | `https://github.com/<owner>/<repo>.git` |
| `gitlab:<group>[/<sub...>]/<repo>` | Any | `2..8` | `https://gitlab.com/<path>.git` |
| `codeberg:<owner>/<repo>` | Any | `2` | `https://codeberg.org/<owner>/<repo>.git` |
| `<owner>/<repo>` | `GitForgeHost::GitHub` | `2` | `https://github.com/<owner>/<repo>.git` |
| `<group>[/<sub...>]/<repo>` | `GitForgeHost::GitLab` | `2..8` | `https://gitlab.com/<path>.git` |
| `<owner>/<repo>` | `GitForgeHost::Codeberg` | `2` | `https://codeberg.org/<owner>/<repo>.git` |
| `<owner>/<repo>` | `GitForgeHost::CustomUrl` | Rejected | Rejected (requires full URL or explicit forge prefix). |
| `https://<host>[:<port>]/<path>[.git]` | Any | `2` (`github.com`, `codeberg.org`), `2..8` (`gitlab.com`), `1..10` (Gitea/custom) | `https://<lower_host>[:<port>]/<path>.git` |
| `git@<host>:<path>[.git]` | Any | Same host rules (no `:port`) | `git@<lower_host>:<path>.git` |
| `file:///<abs_path>` | Any (unit tests) | `>= 1` | `file:///<clean_abs_path>` (clones with `-c protocol.file.allow=always`). |

Git refs (`PluginInstaller::validateGitRef`) must match `^[A-Za-z0-9._/-]{1,128}$` with no leading `-` or `/`, no trailing `/` or `.lock`, and no `..` or `@{`. Branch/tag refs clone with `git clone --depth 1 [--branch <ref>] -- <url> <dest>`, while 7..40 character hex commit SHAs run `git clone --no-checkout -- <url> <dest>` followed by `git checkout <sha>`.

### Build-System Auto-Detection, Post-Build Verification & In-Place Updates

`PluginInstaller::enqueueDetectedBuildSteps()` inspects `<clonedRepo>` (falling back to `<clonedRepo>/plugin/` if the repository root contains no build manifest or C/C++ sources) and resolves `gridlock/plugin_api.h` via `GRIDLOCK_INCLUDE_DIR`, `<appDir>/../include`, `<appDir>/include`, `$APPDIR/usr/include`, `./include`, `/usr/local/include`, and `/usr/include`:

| Priority | `build` Mode | Trigger File(s) | Exact `QProcess` Invocations (`program`, `args`) |
| :---: | :---: | :--- | :--- |
| `1` | `"meson"` or `"auto"` | `meson.build` | 1. `meson` `["setup", "<staging>/build", "-Dgridlock_include_dir=<incDir>"]`<br>2. `ninja` `["-C", "<staging>/build"]` |
| `2` | `"cmake"` or `"auto"` | `CMakeLists.txt` | 1. `cmake` `["-B", "<staging>/build", "-DGRIDLOCK_INCLUDE_DIR=<incDir>"]`<br>2. `cmake` `["--build", "<staging>/build"]` |
| `3` | `"make"` or `"auto"` | `Makefile` or `GNUmakefile` | 1. `make` `["GRIDLOCK_INCLUDE_DIR=<incDir>"]` |
| `4a` | `"single"` or `"auto"` | `*.cpp`, `*.cc`, `*.cxx` | 1. `c++` (or `g++`) `["-std=c++23", "-Wall", "-Wextra", "-fvisibility=hidden", "-fPIC", "-shared", "-I<incDir>", <sources...>, "-o", "<staging>/build/lib<slug><ext>"]` |
| `4b` | `"single"` or `"auto"` | `*.c` | 1. `cc` (or `gcc`) `["-std=c99", "-Wall", "-Wextra", "-fvisibility=hidden", "-fPIC", "-shared", "-I<incDir>", <sources...>, "-o", "<staging>/build/lib<slug><ext>"]` |

* **Post-Build Conformance & Atomic Replacement (`finalizeInstalledArtifacts`)**: Built shared libraries (excluding `.git/`, `CMakeFiles/`, `meson-private/`, `.p/`, `CompilerId*`, and `sanitycheck*`) must first pass `PluginVerifier::verifyBinary()`. Passing artifacts are copied to `<destDir>/.<name>.tmp.<pid>`, set `0750` (`rwxr-x---`), atomically installed via POSIX `rename()`, and recorded in `~/.config/gridlock/plugins-lock.toml` (setting `PluginBuildResult::anyBinaryChanged = true` whenever the on-disk `SHA-256` changed).
* **Batch In-Place Updates (`PluginInstaller::startUpdatePlugins`)**: Enqueues one or more `PluginUpdateTarget` entries (`pluginId`, `gitRepo`, `gitRef`, `buildSystem`, `scope`, `installDir`, `existingBinaryName`, `preserveWorkspaceTrust`), re-cloning and rebuilding each plugin sequentially into its existing scope directory and refreshing workspace `SHA-256` trust in `ConfigManager` when `preserveWorkspaceTrust` is true.

> [!TIP]
> Out-of-tree repositories scaffolded with `lua scripts/init_plugin.lua` automatically wire `-Dgridlock_include_dir` (Meson), `-DGRIDLOCK_INCLUDE_DIR` (CMake), or `GRIDLOCK_INCLUDE_DIR` (Make) so `PluginInstaller` builds them with zero extra configuration.

### `plugins.toml` & User-Global `plugins-lock.toml` Schemas

| File & Location | TOML Table | Key | Type | Semantics |
| :--- | :---: | :--- | :---: | :--- |
| `~/.config/gridlock/plugins.toml`<br>`<workspace>/.gridlock/plugins.toml` | `[[plugins]]` | `repo` | `string` | Required shorthand or full Git URL validated via `normalizeAndValidateUrl()`. |
| `plugins.toml` | `[[plugins]]` | `ref` | `string` | Optional branch, tag, or 7..40 hex commit SHA validated via `validateGitRef()`. |
| `plugins.toml` | `[[plugins]]` | `build` | `string` | Optional `"auto"` (default), `"meson"`, `"cmake"`, `"make"`, or `"single"`. |
| `plugins.toml` | `[[plugins]]` | `enabled` | `bool` | Optional boolean (`true` default); `false` skips the entry during `syncFromManifests()`. |
| `~/.config/gridlock/plugins-lock.toml` *(user-global only)* | `[[plugins]]` | `repo` | `string` | Canonical normalized clone URL (`https://...git` or `git@...git`). |
| `plugins-lock.toml` | `[[plugins]]` | `scope` | `string` | `"global"` or `"workspace"` (keyed by `(repo, scope)`). |
| `plugins-lock.toml` | `[[plugins]]` | `ref` | `string` | Declared `ref` recorded when the binary was built (omitted if empty). |
| `plugins-lock.toml` | `[[plugins]]` | `commit_sha` | `string` | 40-character lowercase SHA from `git rev-parse HEAD` in the staging clone. |
| `plugins-lock.toml` | `[[plugins]]` | `binary_name` | `string` | Basename of the installed shared library (rejects `/` or `..` on read). |
| `plugins-lock.toml` | `[[plugins]]` | `binary_sha256` | `string` | 64-character lowercase `SHA-256` digest verified against disk during `isManifestEntryUpToDate()`. |
