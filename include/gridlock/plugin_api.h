/**
 * @file plugin_api.h
 * @brief Frozen C99 native plugin ABI (v1.0) for GridLock MPI debugger extensions.
 *
 * Defines the binary-stable contract between the GridLock debugger host and
 * dynamically loaded native domain plugins (`.so` / `.dylib` / `.dll`).
 *
 * Binary Compatibility & Threading Invariants:
 * - Strict C99 and C++23 compatibility with zero C++ standard library or GUI
 *   framework dependencies.
 * - Every ABI struct begins with a 16-byte versioned header (`struct_size`,
 *   `abi_major`, `abi_minor`, `reserved_flags`) at offset 0.
 * - `GridLockHostApi` and `GridLockPluginDescriptor` end with `void* reserved[8]`
 *   zero-initialized padding slots so minor ABI revisions can append callbacks
 *   without breaking existing compiled plugins.
 * - Explicit `host_ctx` and `plugin_ctx` opaque user-data pointers are threaded
 *   through every callback so plugins require zero mutable global state.
 * - All string parameters are NUL-terminated UTF-8 unless otherwise noted;
 *   caller-owned output buffers use `(out_buf, capacity, out_written)` semantics.
 */

#ifndef GRIDLOCK_PLUGIN_API_H
#define GRIDLOCK_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Major version of the frozen GridLock native plugin ABI. */
#define GRIDLOCK_PLUGIN_ABI_MAJOR 1u

/** @brief Minor version of the frozen GridLock native plugin ABI. */
#define GRIDLOCK_PLUGIN_ABI_MINOR 0u

/** @brief Canonical symbol name looked up by the host plugin loader for ABI v1.x. */
#define GRIDLOCK_PLUGIN_INIT_SYMBOL_V1 "gridlock_plugin_init_v1"

/** @brief Shared-library symbol export attribute for `gridlock_plugin_init_v1`. */
#ifndef GRIDLOCK_PLUGIN_EXPORT
#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef __cplusplus
#define GRIDLOCK_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define GRIDLOCK_PLUGIN_EXPORT __declspec(dllexport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#ifdef __cplusplus
#define GRIDLOCK_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define GRIDLOCK_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
#else
#ifdef __cplusplus
#define GRIDLOCK_PLUGIN_EXPORT extern "C"
#else
#define GRIDLOCK_PLUGIN_EXPORT
#endif
#endif
#endif

/**
 * @typedef GridLockStatus
 * @brief Fixed-width 32-bit signed status code returned by host and plugin calls.
 */
typedef int32_t GridLockStatus;

/**
 * @enum GridLockStatusCode
 * @brief Canonical status and error constants for `GridLockStatus`.
 */
enum GridLockStatusCode {
  GRIDLOCK_OK = 0,                      /**< Operation succeeded. */
  GRIDLOCK_ERR_INVALID_ARG = -1,        /**< Null pointer or out-of-range parameter. */
  GRIDLOCK_ERR_ABI_MISMATCH = -2,       /**< Incompatible `abi_major` or `struct_size`. */
  GRIDLOCK_ERR_BUFFER_TOO_SMALL = -3,   /**< Caller output buffer capacity is insufficient. */
  GRIDLOCK_ERR_NOT_STOPPED = -4,        /**< Target rank is running or exited, not stopped. */
  GRIDLOCK_ERR_INVALID_RANK = -5,       /**< Requested MPI rank index is outside `[0, ranks)`. */
  GRIDLOCK_ERR_EVAL_FAILED = -6,        /**< Debugger expression evaluation failed. */
  GRIDLOCK_ERR_MEMORY_READ_FAILED = -7, /**< Inferior memory read or write faulted. */
  GRIDLOCK_ERR_UNSUPPORTED = -8,        /**< Operation unsupported by active backend. */
  GRIDLOCK_ERR_TIMEOUT = -9             /**< Backend request timed out before completion. */
};

/**
 * @typedef GridLockLogLevel
 * @brief Fixed-width 32-bit signed severity level passed to `log_message`.
 */
typedef int32_t GridLockLogLevel;

/**
 * @enum GridLockLogLevelCode
 * @brief Severity constants for `GridLockHostApi::log_message`.
 */
enum GridLockLogLevelCode {
  GRIDLOCK_LOG_TRACE = 0, /**< Verbose protocol/diagnostic trace message. */
  GRIDLOCK_LOG_DEBUG = 1, /**< Developer debug message. */
  GRIDLOCK_LOG_INFO = 2,  /**< Normal operational status message. */
  GRIDLOCK_LOG_WARN = 3,  /**< Non-fatal warning condition. */
  GRIDLOCK_LOG_ERROR = 4  /**< Error requiring user or operator attention. */
};

/* Forward declarations of ABI structs. */
typedef struct GridLockHostApi GridLockHostApi;
typedef struct GridLockPluginDescriptor GridLockPluginDescriptor;
typedef struct GridLockStopInfo GridLockStopInfo;
typedef struct GridLockVariableContext GridLockVariableContext;
typedef struct GridLockCommandSpec GridLockCommandSpec;

/**
 * @struct GridLockStopInfo
 * @brief Snapshot of an MPI rank stop event delivered to `on_rank_stopped`.
 */
struct GridLockStopInfo {
  uint32_t struct_size;      /**< Must equal `sizeof(GridLockStopInfo)`. */
  uint32_t abi_major;        /**< Producer ABI major version (`GRIDLOCK_PLUGIN_ABI_MAJOR`). */
  uint32_t abi_minor;        /**< Producer ABI minor version (`GRIDLOCK_PLUGIN_ABI_MINOR`). */
  uint32_t reserved_flags;   /**< Bitmask reserved for future flags; must be `0`. */
  int32_t rank;              /**< Zero-based MPI rank index that stopped. */
  int32_t thread_id;         /**< One-based debugger thread ID on `rank`. */
  int32_t line;              /**< One-based source line number, or `-1` if unknown. */
  int32_t frame_level;       /**< Zero-based stack frame depth (`0` = innermost). */
  uint64_t pc_address;       /**< Instruction pointer virtual address. */
  const char* reason_utf8;   /**< UTF-8 stop reason (e.g., `"breakpoint-hit"`). */
  const char* file_utf8;     /**< UTF-8 resolved source path, or `NULL` if unavailable. */
  const char* function_utf8; /**< UTF-8 active function symbol, or `NULL` if unavailable. */
};

/**
 * @struct GridLockVariableContext
 * @brief Target variable context passed to registered variable-action callbacks.
 */
struct GridLockVariableContext {
  uint32_t struct_size;       /**< Must equal `sizeof(GridLockVariableContext)`. */
  uint32_t abi_major;         /**< Producer ABI major version (`GRIDLOCK_PLUGIN_ABI_MAJOR`). */
  uint32_t abi_minor;         /**< Producer ABI minor version (`GRIDLOCK_PLUGIN_ABI_MINOR`). */
  uint32_t reserved_flags;    /**< Bitmask reserved for future flags; must be `0`. */
  int32_t rank;               /**< Zero-based MPI rank owning the inspected variable. */
  int32_t frame_level;        /**< Zero-based stack frame depth where evaluated. */
  uint64_t address;           /**< Target address of variable/pointer, or `0` if unknown. */
  size_t byte_size;           /**< Static byte size of variable, or `0` if unknown. */
  const char* action_id_utf8; /**< Invoked `action_id` registered by the plugin. */
  const char* name_utf8;      /**< Short variable identifier (e.g., `"mesh_view"`). */
  const char* expression_utf8;/**< Full evaluable C/C++ expression path. */
  const char* type_utf8;      /**< Debugger type string matched by `type_regex`. */
  const char* value_utf8;     /**< Formatted debugger value summary string. */
};

/**
 * @struct GridLockCommandSpec
 * @brief Command metadata and execution context passed to command callbacks.
 */
struct GridLockCommandSpec {
  uint32_t struct_size;          /**< Must equal `sizeof(GridLockCommandSpec)`. */
  uint32_t abi_major;            /**< Producer ABI major version (`GRIDLOCK_PLUGIN_ABI_MAJOR`). */
  uint32_t abi_minor;            /**< Producer ABI minor version (`GRIDLOCK_PLUGIN_ABI_MINOR`). */
  uint32_t reserved_flags;       /**< Bitmask reserved for future flags; must be `0`. */
  int32_t active_rank;           /**< Focused zero-based MPI rank, or `-1` when none. */
  int32_t rank_count;            /**< Total MPI ranks in session, or `0` if inactive. */
  const char* command_id_utf8;   /**< Unique command identifier registered by plugin. */
  const char* menu_label_utf8;   /**< Display label registered for command menus. */
  const char* shortcut_hint_utf8;/**< Optional portable shortcut hint, or `NULL`. */
  const char* args_utf8;         /**< Optional UTF-8 invocation arguments/JSON, or `NULL`. */
};

/** @brief Callback invoked when a user or automation triggers a variable action. */
typedef GridLockStatus (*GridLockVariableActionCallback)(
    void* plugin_ctx,
    const GridLockHostApi* host,
    const GridLockVariableContext* var_ctx);

/** @brief Callback invoked when a user or automation triggers a plugin command. */
typedef GridLockStatus (*GridLockCommandActionCallback)(
    void* plugin_ctx,
    const GridLockHostApi* host,
    const GridLockCommandSpec* cmd_spec);

/** @brief Host heap allocation helper paired symmetrically with `GridLockFreeBytesFn`. */
typedef void* (*GridLockAllocBytesFn)(void* host_ctx, size_t num_bytes);

/** @brief Host heap deallocation helper releasing memory from `GridLockAllocBytesFn`. */
typedef void (*GridLockFreeBytesFn)(void* host_ctx, void* ptr);

/** @brief Writes a UTF-8 diagnostic message to GridLock's structured log sink. */
typedef GridLockStatus (*GridLockLogMessageFn)(
    void* host_ctx,
    GridLockLogLevel level,
    const char* utf8_msg);

/** @brief Queries the number of MPI ranks in the active debug session. */
typedef GridLockStatus (*GridLockGetRankCountFn)(
    void* host_ctx,
    int32_t* out_ranks);

/** @brief Queries the currently selected zero-based MPI rank index. */
typedef GridLockStatus (*GridLockGetActiveRankFn)(
    void* host_ctx,
    int32_t* out_rank);

/**
 * @brief Evaluates `expr_utf8` on `rank` into caller-owned `out_utf8` buffer.
 *
 * Writes a NUL-terminated UTF-8 string when `cap` exceeds the result byte length
 * and stores the payload byte count (excluding trailing NUL) in `*out_len`.
 * Returns `GRIDLOCK_ERR_BUFFER_TOO_SMALL` and writes the required payload length
 * (excluding NUL) to `*out_len` when `cap` is insufficient.
 */
typedef GridLockStatus (*GridLockEvalExprFn)(
    void* host_ctx,
    int32_t rank,
    const char* expr_utf8,
    char* out_utf8,
    size_t cap,
    size_t* out_len);

/** @brief Reads `num_bytes` from `address` in `rank`'s address space into `out_bytes`. */
typedef GridLockStatus (*GridLockReadMemoryFn)(
    void* host_ctx,
    int32_t rank,
    uint64_t address,
    void* out_bytes,
    size_t num_bytes,
    size_t* out_read);

/** @brief Writes `num_bytes` from `in_bytes` into `address` in `rank`'s address space. */
typedef GridLockStatus (*GridLockWriteMemoryFn)(
    void* host_ctx,
    int32_t rank,
    uint64_t address,
    const void* in_bytes,
    size_t num_bytes,
    size_t* out_written);

/** @brief Installs a source breakpoint at `file_utf8:line` with optional `condition_utf8`. */
typedef GridLockStatus (*GridLockSetBreakpointFn)(
    void* host_ctx,
    const char* file_utf8,
    int32_t line,
    const char* condition_utf8,
    int32_t* out_bp_id);

/** @brief Removes a breakpoint previously created via `set_breakpoint`. */
typedef GridLockStatus (*GridLockRemoveBreakpointFn)(
    void* host_ctx,
    int32_t bp_id);

/** @brief Registers a context action matched against variable types via `type_regex`. */
typedef GridLockStatus (*GridLockRegisterVariableActionFn)(
    void* host_ctx,
    const char* action_id,
    const char* menu_label,
    const char* type_regex,
    GridLockVariableActionCallback cb,
    void* plugin_ctx);

/** @brief Registers a named command action with an optional `shortcut_hint`. */
typedef GridLockStatus (*GridLockRegisterCommandActionFn)(
    void* host_ctx,
    const char* command_id,
    const char* menu_label,
    const char* shortcut_hint,
    GridLockCommandActionCallback cb,
    void* plugin_ctx);

/**
 * @brief Emits a structured JSON event on `channel_utf8` to external listeners/sinks.
 *
 * Enables domain plugins to publish telemetry, mesh metadata, or array snapshots
 * to external tools, sockets, or trace files without coupling to GridLock's UI.
 */
typedef GridLockStatus (*GridLockEmitStructuredEventFn)(
    void* host_ctx,
    const char* channel_utf8,
    const char* json_payload_utf8);

/**
 * @struct GridLockHostApi
 * @brief Host service table passed by pointer to plugins at initialization and callbacks.
 */
struct GridLockHostApi {
  uint32_t struct_size;                                  /**< `sizeof(GridLockHostApi)`. */
  uint32_t abi_major;                                    /**< `GRIDLOCK_PLUGIN_ABI_MAJOR`. */
  uint32_t abi_minor;                                    /**< `GRIDLOCK_PLUGIN_ABI_MINOR`. */
  uint32_t reserved_flags;                               /**< Reserved flags; must be `0`. */
  void* host_ctx;                                        /**< Opaque host state pointer. */
  GridLockAllocBytesFn alloc_bytes;                      /**< Host allocator callback. */
  GridLockFreeBytesFn free_bytes;                        /**< Host deallocator callback. */
  GridLockLogMessageFn log_message;                      /**< Structured logger callback. */
  GridLockGetRankCountFn get_rank_count;                 /**< Total rank query callback. */
  GridLockGetActiveRankFn get_active_rank;               /**< Active rank query callback. */
  GridLockEvalExprFn eval_expr;                          /**< Expression evaluation callback. */
  GridLockReadMemoryFn read_memory;                      /**< Target memory read callback. */
  GridLockWriteMemoryFn write_memory;                    /**< Target memory write callback. */
  GridLockSetBreakpointFn set_breakpoint;                /**< Breakpoint insertion callback. */
  GridLockRemoveBreakpointFn remove_breakpoint;          /**< Breakpoint removal callback. */
  GridLockRegisterVariableActionFn register_variable_action; /**< Variable action hook. */
  GridLockRegisterCommandActionFn register_command_action;   /**< Command action hook. */
  GridLockEmitStructuredEventFn emit_structured_event;   /**< Structured telemetry emitter. */
  void* reserved[8];                                     /**< Future minor-ABI expansion slots. */
};

/** @brief Optional plugin lifecycle hook called when a parallel debug session starts. */
typedef GridLockStatus (*GridLockSessionStartCallback)(
    void* plugin_ctx,
    const GridLockHostApi* host,
    int32_t rank_count);

/** @brief Optional plugin lifecycle hook called when an MPI rank stops. */
typedef GridLockStatus (*GridLockRankStoppedCallback)(
    void* plugin_ctx,
    const GridLockHostApi* host,
    const GridLockStopInfo* stop_info);

/** @brief Optional plugin lifecycle hook called when the active debug session ends. */
typedef GridLockStatus (*GridLockSessionEndCallback)(
    void* plugin_ctx,
    const GridLockHostApi* host);

/** @brief Optional plugin teardown hook called immediately before unloading the plugin. */
typedef void (*GridLockPluginShutdownCallback)(
    void* plugin_ctx,
    const GridLockHostApi* host);

/**
 * @struct GridLockPluginDescriptor
 * @brief Plugin metadata and lifecycle dispatch table populated by `gridlock_plugin_init_v1`.
 */
struct GridLockPluginDescriptor {
  uint32_t struct_size;                        /**< `sizeof(GridLockPluginDescriptor)`. */
  uint32_t abi_major;                          /**< `GRIDLOCK_PLUGIN_ABI_MAJOR`. */
  uint32_t abi_minor;                          /**< `GRIDLOCK_PLUGIN_ABI_MINOR`. */
  uint32_t reserved_flags;                     /**< Reserved flags; must be `0`. */
  const char* plugin_id_utf8;                  /**< Unique stable plugin identifier. */
  const char* plugin_name_utf8;                /**< Human-readable plugin title. */
  const char* plugin_version_utf8;             /**< Semantic version string (`"X.Y.Z"`). */
  const char* description_utf8;                /**< Brief capability summary. */
  void* plugin_ctx;                            /**< Plugin-owned instance state pointer. */
  GridLockSessionStartCallback on_session_start; /**< Optional session-start hook. */
  GridLockRankStoppedCallback on_rank_stopped; /**< Optional rank-stopped hook. */
  GridLockSessionEndCallback on_session_end;   /**< Optional session-end hook. */
  GridLockPluginShutdownCallback shutdown;     /**< Optional unload cleanup hook. */
  void* reserved[8];                           /**< Future minor-ABI expansion slots. */
};

/** @brief Function pointer type for the single exported v1 plugin entry point. */
typedef GridLockStatus (*GridLockPluginInitV1Fn)(
    const GridLockHostApi* host,
    GridLockPluginDescriptor* out_desc);

/**
 * @brief Single versioned entry point exported by every GridLock v1.x native plugin.
 * @param host Non-null pointer to the host API table valid for the plugin's lifetime.
 * @param out_desc Non-null descriptor initialized by host and populated by the plugin.
 * @return `GRIDLOCK_OK` on success, or a negative `GridLockStatus` error code.
 */
GRIDLOCK_PLUGIN_EXPORT GridLockStatus gridlock_plugin_init_v1(
    const GridLockHostApi* host,
    GridLockPluginDescriptor* out_desc);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GRIDLOCK_PLUGIN_API_H */
