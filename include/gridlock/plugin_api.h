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

/** @brief Explicit C calling convention on Windows to prevent `/Gz` (`__stdcall`) stack corruption. */
#ifndef GRIDLOCK_CALL
#if defined(_WIN32)
#define GRIDLOCK_CALL __cdecl
#else
#define GRIDLOCK_CALL
#endif
#endif

/** @brief Shared-library symbol export attribute for `gridlock_plugin_init_v1`. */
#ifndef GRIDLOCK_PLUGIN_EXPORT
#if defined(_WIN32) && !defined(GRIDLOCK_BUILDING_HOST)
#define GRIDLOCK_PLUGIN_EXPORT __declspec(dllexport)
#elif !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#define GRIDLOCK_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define GRIDLOCK_PLUGIN_EXPORT
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
  GRIDLOCK_ERR_TIMEOUT = -9,            /**< Backend request timed out before completion. */
  GRIDLOCK_ERR_CANCELLED = -10          /**< Interactive dialog or input prompt cancelled by user. */
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

/**
 * @typedef GridLockDialogSeverity
 * @brief Fixed-width 32-bit signed severity icon/style for modal dialogs and non-modal toasts.
 */
typedef int32_t GridLockDialogSeverity;

/**
 * @enum GridLockDialogSeverityCode
 * @brief Canonical severity constants for `show_message_box` and `show_toast`.
 */
enum GridLockDialogSeverityCode {
  GRIDLOCK_DIALOG_INFO = 0,     /**< Informational message styling. */
  GRIDLOCK_DIALOG_WARNING = 1,  /**< Warning message styling. */
  GRIDLOCK_DIALOG_ERROR = 2,    /**< Error alert styling. */
  GRIDLOCK_DIALOG_QUESTION = 3  /**< Confirmation question styling. */
};

/**
 * @typedef GridLockDialogButtons
 * @brief Fixed-width 32-bit signed button preset for `show_message_box`.
 */
typedef int32_t GridLockDialogButtons;

/**
 * @enum GridLockDialogButtonsCode
 * @brief Button layout constants for `GridLockHostApi::show_message_box`.
 */
enum GridLockDialogButtonsCode {
  GRIDLOCK_BUTTONS_OK = 1,        /**< Single **OK** acknowledgement button. */
  GRIDLOCK_BUTTONS_OK_CANCEL = 2, /**< **OK** and **Cancel** buttons. */
  GRIDLOCK_BUTTONS_YES_NO = 3     /**< **Yes** and **No** confirmation buttons. */
};

/**
 * @typedef GridLockDialogResult
 * @brief Fixed-width 32-bit signed button selection returned by `show_message_box`.
 */
typedef int32_t GridLockDialogResult;

/**
 * @enum GridLockDialogResultCode
 * @brief Result constants written to `*out_result` by `GridLockHostApi::show_message_box`.
 */
enum GridLockDialogResultCode {
  GRIDLOCK_RESULT_OK = 1,     /**< User accepted with **OK**. */
  GRIDLOCK_RESULT_CANCEL = 2, /**< User dismissed or clicked **Cancel**. */
  GRIDLOCK_RESULT_YES = 3,    /**< User confirmed with **Yes**. */
  GRIDLOCK_RESULT_NO = 4      /**< User declined with **No**. */
};

/**
 * @typedef GridLockContextSurface
 * @brief Fixed-width 32-bit signed workstation UI surface identifier for context-menu actions.
 */
typedef int32_t GridLockContextSurface;

/**
 * @enum GridLockContextSurfaceCode
 * @brief Target UI surfaces supported by `GridLockHostApi::register_context_action`.
 */
enum GridLockContextSurfaceCode {
  GRIDLOCK_SURFACE_VARIABLES = 1,   /**< Variables / Watch tree rows. */
  GRIDLOCK_SURFACE_CODE_EDITOR = 2, /**< Source code editor viewport and selection. */
  GRIDLOCK_SURFACE_CALL_STACK = 3,  /**< Call-stack frame table rows. */
  GRIDLOCK_SURFACE_BREAKPOINTS = 4, /**< Breakpoint gutter and breakpoint list entries. */
  GRIDLOCK_SURFACE_RANK_LIST = 5    /**< MPI server-rack / rank diagnostic entries. */
};

/**
 * @typedef GridLockReportFormat
 * @brief Fixed-width 32-bit signed declarative content format for `publish_report_view`.
 */
typedef int32_t GridLockReportFormat;

/**
 * @enum GridLockReportFormatCode
 * @brief Rendering formats supported by `GridLockHostApi::publish_report_view`.
 */
enum GridLockReportFormatCode {
  GRIDLOCK_REPORT_MARKDOWN = 1,        /**< Rich Markdown text document. */
  GRIDLOCK_REPORT_KEY_VALUE_TABLE = 2, /**< Two-column key/value JSON object or `key=value` lines. */
  GRIDLOCK_REPORT_JSON_TREE = 3        /**< Expandable hierarchical JSON document tree. */
};

/* Forward declarations of ABI structs. */
typedef struct GridLockHostApi GridLockHostApi;
typedef struct GridLockPluginDescriptor GridLockPluginDescriptor;
typedef struct GridLockStopInfo GridLockStopInfo;
typedef struct GridLockVariableContext GridLockVariableContext;
typedef struct GridLockCommandSpec GridLockCommandSpec;
typedef struct GridLockSurfaceContext GridLockSurfaceContext;
typedef struct GridLockThemeInfo GridLockThemeInfo;
typedef struct GridLockTypeInfo GridLockTypeInfo;
typedef struct GridLockContiguousRange GridLockContiguousRange;

/**
 * @struct GridLockStopInfo
 * @brief Snapshot of an MPI rank stop event delivered to `on_rank_stopped`.
 *
 * Pointer Lifetime: `reason_utf8`, `file_utf8`, and `function_utf8` are host-owned
 * borrows valid strictly for the synchronous duration of the `on_rank_stopped` call.
 * Plugins must copy any string data they wish to retain after returning.
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
 *
 * Pointer Lifetime: `action_id_utf8`, `name_utf8`, `expression_utf8`, `type_utf8`,
 * and `value_utf8` are host-owned borrows valid strictly for the synchronous duration
 * of the `GridLockVariableActionCallback` invocation.
 */
struct GridLockVariableContext {
  uint32_t struct_size;        /**< Must equal `sizeof(GridLockVariableContext)`. */
  uint32_t abi_major;          /**< Producer ABI major version (`GRIDLOCK_PLUGIN_ABI_MAJOR`). */
  uint32_t abi_minor;          /**< Producer ABI minor version (`GRIDLOCK_PLUGIN_ABI_MINOR`). */
  uint32_t reserved_flags;     /**< Bitmask reserved for future flags; must be `0`. */
  int32_t rank;                /**< Zero-based MPI rank owning the inspected variable. */
  int32_t frame_level;         /**< Zero-based stack frame depth where evaluated. */
  uint64_t address;            /**< Target address of variable/pointer, or `0` if unknown. */
  size_t byte_size;            /**< Static byte size of variable, or `0` if unknown. */
  const char* action_id_utf8;  /**< Invoked `action_id` registered by the plugin. */
  const char* name_utf8;       /**< Short variable identifier (e.g., `"mesh_view"`). */
  const char* expression_utf8; /**< Full evaluable C/C++ expression path. */
  const char* type_utf8;       /**< Debugger type string matched by `type_regex`. */
  const char* value_utf8;      /**< Formatted debugger value summary string. */
};

/**
 * @struct GridLockCommandSpec
 * @brief Command metadata and execution context passed to command callbacks.
 *
 * Pointer Lifetime: `command_id_utf8`, `menu_label_utf8`, `shortcut_hint_utf8`, and
 * `args_utf8` are host-owned borrows valid strictly for the synchronous duration of
 * the `GridLockCommandActionCallback` invocation.
 */
struct GridLockCommandSpec {
  uint32_t struct_size;           /**< Must equal `sizeof(GridLockCommandSpec)`. */
  uint32_t abi_major;             /**< Producer ABI major version (`GRIDLOCK_PLUGIN_ABI_MAJOR`). */
  uint32_t abi_minor;             /**< Producer ABI minor version (`GRIDLOCK_PLUGIN_ABI_MINOR`). */
  uint32_t reserved_flags;        /**< Bitmask reserved for future flags; must be `0`. */
  int32_t active_rank;            /**< Focused zero-based MPI rank, or `-1` when none. */
  int32_t rank_count;             /**< Total MPI ranks in session, or `0` if inactive. */
  const char* command_id_utf8;    /**< Unique command identifier registered by plugin. */
  const char* menu_label_utf8;    /**< Display label or menu path registered for command. */
  const char* shortcut_hint_utf8; /**< Optional portable shortcut hint, or `NULL`. */
  const char* args_utf8;          /**< Optional UTF-8 invocation arguments/JSON, or `NULL`. */
};

/**
 * @struct GridLockSurfaceContext
 * @brief Multi-surface context snapshot delivered to `GridLockContextActionFn`.
 *
 * Pointer Lifetime: `symbol_utf8`, `type_utf8`, `file_utf8`, and `selected_text_utf8`
 * are host-owned borrows valid strictly for the synchronous duration of the callback.
 */
struct GridLockSurfaceContext {
  uint32_t struct_size;            /**< Must equal `sizeof(GridLockSurfaceContext)`. */
  uint32_t abi_major;              /**< Producer ABI major version (`GRIDLOCK_PLUGIN_ABI_MAJOR`). */
  uint32_t abi_minor;              /**< Producer ABI minor version (`GRIDLOCK_PLUGIN_ABI_MINOR`). */
  uint32_t reserved_flags;         /**< Bitmask reserved for future flags; must be `0`. */
  GridLockContextSurface surface;  /**< Surface where the context menu was invoked. */
  int32_t rank;                    /**< Target zero-based MPI rank, or `-1` when none. */
  int32_t frame_level;             /**< Zero-based stack frame depth, or `0`. */
  int32_t line;                    /**< One-based source line, or `-1` if not applicable. */
  int32_t column;                  /**< One-based source column, or `-1` if not applicable. */
  int32_t bp_id;                   /**< Breakpoint identifier, or `-1` if not applicable. */
  uint64_t address;                /**< Associated target virtual address, or `0`. */
  const char* symbol_utf8;         /**< Variable, function, or expression symbol, or `NULL`. */
  const char* type_utf8;           /**< Debugger type name (Variables surface), or `NULL`. */
  const char* file_utf8;           /**< Resolved source file path, or `NULL`. */
  const char* selected_text_utf8;  /**< Highlighted editor text, or `NULL`. */
};

/**
 * @struct GridLockThemeInfo
 * @brief Fixed-size POD snapshot of active workstation theme tokens and hex colors.
 *
 * All character arrays are NUL-terminated ASCII/UTF-8 strings (e.g., `"#1e1e2e"`).
 */
struct GridLockThemeInfo {
  uint32_t struct_size;       /**< Must equal `sizeof(GridLockThemeInfo)`. */
  uint32_t abi_major;         /**< Producer ABI major version (`GRIDLOCK_PLUGIN_ABI_MAJOR`). */
  uint32_t abi_minor;         /**< Producer ABI minor version (`GRIDLOCK_PLUGIN_ABI_MINOR`). */
  uint32_t reserved_flags;    /**< Bitmask reserved for future flags; must be `0`. */
  int32_t is_dark;            /**< `1` when active theme is dark, `0` when light. */
  char theme_id_utf8[64];     /**< NUL-terminated active theme identifier. */
  char bg_primary_hex[16];    /**< Primary canvas/editor background `#RRGGBB`. */
  char bg_secondary_hex[16];  /**< Secondary sidebar/header background `#RRGGBB`. */
  char fg_primary_hex[16];    /**< Primary foreground text `#RRGGBB`. */
  char fg_muted_hex[16];      /**< Muted/secondary foreground text `#RRGGBB`. */
  char accent_hex[16];        /**< Primary brand/focus accent `#RRGGBB`. */
  char selection_hex[16];     /**< Active row/text selection background `#RRGGBB`. */
  char warning_hex[16];       /**< Semantic warning token `#RRGGBB`. */
  char error_hex[16];         /**< Semantic error/fault token `#RRGGBB`. */
};

/**
 * @struct GridLockTypeInfo
 * @brief Fixed-size POD snapshot of DWARF type metadata resolved without inferior calls.
 */
struct GridLockTypeInfo {
  uint32_t struct_size;             /**< Must equal `sizeof(GridLockTypeInfo)`. */
  uint32_t abi_major;               /**< Producer ABI major version (`GRIDLOCK_PLUGIN_ABI_MAJOR`). */
  uint32_t abi_minor;               /**< Producer ABI minor version (`GRIDLOCK_PLUGIN_ABI_MINOR`). */
  uint32_t reserved_flags;          /**< Bitmask reserved for future flags; must be `0`. */
  uint64_t byte_size;               /**< Static `sizeof(expr)` in bytes on `rank`. */
  int32_t is_pointer;               /**< `1` when resolved DWARF type is a pointer, else `0`. */
  int32_t is_contiguous_container;  /**< `1` for C arrays, `std::array`, `std::vector`, `std::span`, `std::valarray`. */
  char type_name_utf8[256];         /**< NUL-terminated resolved DWARF type name. */
};

/**
 * @struct GridLockContiguousRange
 * @brief Contiguous element buffer descriptor resolved via DWARF without inferior calls.
 */
struct GridLockContiguousRange {
  uint32_t struct_size;          /**< Must equal `sizeof(GridLockContiguousRange)`. */
  uint32_t abi_major;            /**< Producer ABI major version (`GRIDLOCK_PLUGIN_ABI_MAJOR`). */
  uint32_t abi_minor;            /**< Producer ABI minor version (`GRIDLOCK_PLUGIN_ABI_MINOR`). */
  uint32_t reserved_flags;       /**< Bitmask reserved for future flags; must be `0`. */
  uint64_t data_address;         /**< Virtual address of element `0` (or `0` when empty). */
  uint64_t element_count;        /**< Number of contiguous elements in `[0, element_count)`. */
  uint64_t element_size;         /**< Byte size of a single element (`sizeof(T)`). */
  char element_type_utf8[128];   /**< NUL-terminated element DWARF type name (`T`). */
  char container_type_utf8[256]; /**< NUL-terminated outer container/array DWARF type name. */
};

/** @brief Callback invoked when a user or automation triggers a variable action. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockVariableActionCallback)(
    void* plugin_ctx,
    const GridLockHostApi* host,
    const GridLockVariableContext* var_ctx);

/** @brief Callback invoked when a user or automation triggers a plugin command or menu item. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockCommandActionCallback)(
    void* plugin_ctx,
    const GridLockHostApi* host,
    const GridLockCommandSpec* cmd_spec);

/** @brief Callback invoked when a user clicks a plugin status-bar item. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockStatusItemCallback)(
    void* plugin_ctx,
    const GridLockHostApi* host,
    const char* item_id_utf8);

/** @brief Callback invoked when a user triggers a multi-surface context-menu action. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockContextActionFn)(
    void* plugin_ctx,
    const GridLockHostApi* host,
    const GridLockSurfaceContext* surface_ctx);

/** @brief Host heap allocation helper paired symmetrically with `GridLockFreeBytesFn`. */
typedef void* (GRIDLOCK_CALL *GridLockAllocBytesFn)(void* host_ctx, size_t num_bytes);

/** @brief Host heap deallocation helper releasing memory from `GridLockAllocBytesFn`. */
typedef void (GRIDLOCK_CALL *GridLockFreeBytesFn)(void* host_ctx, void* ptr);

/** @brief Writes a UTF-8 diagnostic message to GridLock's structured log sink. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockLogMessageFn)(
    void* host_ctx,
    GridLockLogLevel level,
    const char* utf8_msg);

/** @brief Queries the number of MPI ranks in the active debug session. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockGetRankCountFn)(
    void* host_ctx,
    int32_t* out_ranks);

/** @brief Queries the currently selected zero-based MPI rank index. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockGetActiveRankFn)(
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
typedef GridLockStatus (GRIDLOCK_CALL *GridLockEvalExprFn)(
    void* host_ctx,
    int32_t rank,
    const char* expr_utf8,
    char* out_utf8,
    size_t cap,
    size_t* out_len);

/** @brief Reads `num_bytes` from `address` in `rank`'s address space into `out_bytes`. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockReadMemoryFn)(
    void* host_ctx,
    int32_t rank,
    uint64_t address,
    void* out_bytes,
    size_t num_bytes,
    size_t* out_read);

/** @brief Writes `num_bytes` from `in_bytes` into `address` in `rank`'s address space. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockWriteMemoryFn)(
    void* host_ctx,
    int32_t rank,
    uint64_t address,
    const void* in_bytes,
    size_t num_bytes,
    size_t* out_written);

/** @brief Installs a source breakpoint at `file_utf8:line` with optional `condition_utf8`. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockSetBreakpointFn)(
    void* host_ctx,
    const char* file_utf8,
    int32_t line,
    const char* condition_utf8,
    int32_t* out_bp_id);

/** @brief Removes a breakpoint previously created via `set_breakpoint`. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockRemoveBreakpointFn)(
    void* host_ctx,
    int32_t bp_id);

/** @brief Registers a context action matched against variable types via `type_regex`. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockRegisterVariableActionFn)(
    void* host_ctx,
    const char* action_id,
    const char* menu_label,
    const char* type_regex,
    GridLockVariableActionCallback cb,
    void* plugin_ctx);

/** @brief Registers a named command action with an optional `shortcut_hint`. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockRegisterCommandActionFn)(
    void* host_ctx,
    const char* command_id,
    const char* menu_label,
    const char* shortcut_hint,
    GridLockCommandActionCallback cb,
    void* plugin_ctx);

/** @brief Emits a structured JSON event on `channel_utf8` to external listeners/sinks. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockEmitStructuredEventFn)(
    void* host_ctx,
    const char* channel_utf8,
    const char* json_payload_utf8);

/** @brief Displays a modal message dialog and writes a `GridLockDialogResult` to `*out_result`. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockShowMessageBoxFn)(
    void* host_ctx,
    GridLockDialogSeverity severity,
    GridLockDialogButtons buttons,
    const char* title_utf8,
    const char* body_utf8,
    int32_t* out_result);

/**
 * @brief Prompts the user for a single-line UTF-8 string into caller-owned `out_utf8`.
 *
 * Uses the same `(out_utf8, cap, out_len)` buffer-sizing protocol as `eval_expr` and
 * returns `GRIDLOCK_ERR_CANCELLED` (`-10`) when the user cancels the dialog.
 */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockPromptInputFn)(
    void* host_ctx,
    const char* title_utf8,
    const char* label_utf8,
    const char* default_utf8,
    char* out_utf8,
    size_t cap,
    size_t* out_len);

/** @brief Displays a non-blocking toast notification for `duration_ms` milliseconds. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockShowToastFn)(
    void* host_ctx,
    GridLockDialogSeverity severity,
    const char* title_utf8,
    const char* body_utf8,
    int32_t duration_ms);

/** @brief Registers a `"/"`-delimited nested menu action (e.g., `"Mesh Tools/Refinement/Export"`). */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockRegisterMenuActionFn)(
    void* host_ctx,
    const char* action_id,
    const char* menu_path_utf8,
    const char* shortcut_hint,
    GridLockCommandActionCallback cb,
    void* plugin_ctx);

/** @brief Creates or updates a clickable status-bar item styled with optional `color_token_utf8`. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockSetStatusItemFn)(
    void* host_ctx,
    const char* item_id,
    const char* text_utf8,
    const char* tooltip_utf8,
    const char* color_token_utf8,
    GridLockStatusItemCallback cb,
    void* plugin_ctx);

/** @brief Removes a status-bar item previously created by the calling plugin. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockRemoveStatusItemFn)(
    void* host_ctx,
    const char* item_id);

/** @brief Registers a context-menu action on `surface` filtered by optional `filter_regex`. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockRegisterContextActionFn)(
    void* host_ctx,
    GridLockContextSurface surface,
    const char* action_id,
    const char* menu_label,
    const char* filter_regex,
    GridLockContextActionFn cb,
    void* plugin_ctx);

/** @brief Opens `file_utf8` in the source editor and positions the cursor at `line:column`. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockNavigateToLocationFn)(
    void* host_ctx,
    const char* file_utf8,
    int32_t line,
    int32_t column);

/** @brief Attaches or updates an inline editor badge/detail annotation on `file_utf8:line`. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockSetEditorAnnotationFn)(
    void* host_ctx,
    const char* annotation_id,
    const char* file_utf8,
    int32_t line,
    const char* badge_utf8,
    const char* detail_utf8,
    const char* color_token_utf8);

/** @brief Clears plugin editor annotations matching `id_prefix_utf8` (`NULL` or `""` clears all owned). */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockClearEditorAnnotationsFn)(
    void* host_ctx,
    const char* id_prefix_utf8);

/** @brief Populates `*out_theme` with the active workstation theme palette tokens. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockGetThemeInfoFn)(
    void* host_ctx,
    GridLockThemeInfo* out_theme);

/** @brief Opens or updates a declarative report panel (`MARKDOWN`, `KEY_VALUE_TABLE`, or `JSON_TREE`). */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockPublishReportViewFn)(
    void* host_ctx,
    const char* panel_id,
    const char* title_utf8,
    GridLockReportFormat format,
    const char* content_utf8);

/** @brief Closes a declarative report panel previously published by `panel_id`. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockCloseReportViewFn)(
    void* host_ctx,
    const char* panel_id);

/** @brief Queries DWARF type name, `sizeof`, pointer, and contiguous-container flags without inferior calls. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockQueryTypeInfoFn)(
    void* host_ctx,
    int32_t rank,
    const char* expr_utf8,
    GridLockTypeInfo* out_info);

/** @brief Resolves base address, element count, element size, and types of a contiguous container/array without inferior calls. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockResolveContiguousRangeFn)(
    void* host_ctx,
    int32_t rank,
    const char* expr_utf8,
    GridLockContiguousRange* out_range);

/**
 * @struct GridLockHostApi
 * @brief Host service table passed by pointer to plugins at initialization and callbacks.
 */
struct GridLockHostApi {
  uint32_t struct_size;                                       /**< `sizeof(GridLockHostApi)`. */
  uint32_t abi_major;                                         /**< `GRIDLOCK_PLUGIN_ABI_MAJOR`. */
  uint32_t abi_minor;                                         /**< `GRIDLOCK_PLUGIN_ABI_MINOR`. */
  uint32_t reserved_flags;                                    /**< Reserved flags; must be `0`. */
  void* host_ctx;                                             /**< Opaque host state pointer. */
  GridLockAllocBytesFn alloc_bytes;                           /**< Host allocator callback. */
  GridLockFreeBytesFn free_bytes;                             /**< Host deallocator callback. */
  GridLockLogMessageFn log_message;                           /**< Structured logger callback. */
  GridLockGetRankCountFn get_rank_count;                      /**< Total rank query callback. */
  GridLockGetActiveRankFn get_active_rank;                    /**< Active rank query callback. */
  GridLockEvalExprFn eval_expr;                               /**< Expression evaluation callback. */
  GridLockReadMemoryFn read_memory;                           /**< Target memory read callback. */
  GridLockWriteMemoryFn write_memory;                         /**< Target memory write callback. */
  GridLockSetBreakpointFn set_breakpoint;                     /**< Breakpoint insertion callback. */
  GridLockRemoveBreakpointFn remove_breakpoint;               /**< Breakpoint removal callback. */
  GridLockRegisterVariableActionFn register_variable_action;  /**< Variable action hook. */
  GridLockRegisterCommandActionFn register_command_action;    /**< Command action hook. */
  GridLockEmitStructuredEventFn emit_structured_event;        /**< Structured telemetry emitter. */
  GridLockShowMessageBoxFn show_message_box;                  /**< Modal message dialog callback. */
  GridLockPromptInputFn prompt_input;                         /**< Modal text-input prompt callback. */
  GridLockShowToastFn show_toast;                             /**< Non-blocking toast notification. */
  GridLockRegisterMenuActionFn register_menu_action;          /**< Nested slash-path menu hook. */
  GridLockSetStatusItemFn set_status_item;                    /**< Status-bar item upsert callback. */
  GridLockRemoveStatusItemFn remove_status_item;              /**< Status-bar item removal callback. */
  GridLockRegisterContextActionFn register_context_action;    /**< Multi-surface context menu hook. */
  GridLockNavigateToLocationFn navigate_to_location;          /**< Source editor jump callback. */
  GridLockSetEditorAnnotationFn set_editor_annotation;        /**< Source line annotation callback. */
  GridLockClearEditorAnnotationsFn clear_editor_annotations;  /**< Source annotation clear callback. */
  GridLockGetThemeInfoFn get_theme_info;                      /**< Active theme token query callback. */
  GridLockPublishReportViewFn publish_report_view;            /**< Declarative report view publisher. */
  GridLockCloseReportViewFn close_report_view;                /**< Declarative report view closer. */
  GridLockQueryTypeInfoFn query_type_info;                    /**< Zero-call DWARF type query callback. */
  GridLockResolveContiguousRangeFn resolve_contiguous_range;  /**< Zero-call contiguous range resolver. */
  void* reserved[8];                                          /**< Future minor-ABI expansion slots. */
};

/** @brief Optional plugin lifecycle hook called when a parallel debug session starts. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockSessionStartCallback)(
    void* plugin_ctx,
    const GridLockHostApi* host,
    int32_t rank_count);

/** @brief Optional plugin lifecycle hook called when an MPI rank stops. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockRankStoppedCallback)(
    void* plugin_ctx,
    const GridLockHostApi* host,
    const GridLockStopInfo* stop_info);

/** @brief Optional plugin lifecycle hook called when the active debug session ends. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockSessionEndCallback)(
    void* plugin_ctx,
    const GridLockHostApi* host);

/** @brief Optional plugin teardown hook called immediately before unloading the plugin. */
typedef void (GRIDLOCK_CALL *GridLockPluginShutdownCallback)(
    void* plugin_ctx,
    const GridLockHostApi* host);

/** @brief Optional plugin notification hook called whenever the workstation theme changes. */
typedef void (GRIDLOCK_CALL *GridLockThemeChangedCallback)(
    void* plugin_ctx,
    const GridLockThemeInfo* theme);

/**
 * @struct GridLockPluginDescriptor
 * @brief Plugin metadata and lifecycle dispatch table populated by `gridlock_plugin_init_v1`.
 *
 * Pointer Lifetime: `plugin_id_utf8`, `plugin_name_utf8`, `plugin_version_utf8`, and
 * `description_utf8` are plugin-owned strings (typically static `.rodata` literals or
 * buffers freed at the end of `shutdown`) and MUST remain valid and immutable until
 * `shutdown` returns (or until the plugin shared library is unloaded if `shutdown` is `NULL`).
 */
struct GridLockPluginDescriptor {
  uint32_t struct_size;                          /**< `sizeof(GridLockPluginDescriptor)`. */
  uint32_t abi_major;                            /**< `GRIDLOCK_PLUGIN_ABI_MAJOR`. */
  uint32_t abi_minor;                            /**< `GRIDLOCK_PLUGIN_ABI_MINOR`. */
  uint32_t reserved_flags;                       /**< Reserved flags; must be `0`. */
  const char* plugin_id_utf8;                    /**< Unique stable plugin identifier. */
  const char* plugin_name_utf8;                  /**< Human-readable plugin title. */
  const char* plugin_version_utf8;               /**< Semantic version string (`"X.Y.Z"`). */
  const char* description_utf8;                  /**< Brief capability summary. */
  void* plugin_ctx;                              /**< Plugin-owned instance state pointer. */
  GridLockSessionStartCallback on_session_start; /**< Optional session-start hook. */
  GridLockRankStoppedCallback on_rank_stopped;   /**< Optional rank-stopped hook. */
  GridLockSessionEndCallback on_session_end;     /**< Optional session-end hook. */
  GridLockPluginShutdownCallback shutdown;       /**< Optional unload cleanup hook. */
  GridLockThemeChangedCallback on_theme_changed; /**< Optional live theme-change hook. */
  void* reserved[8];                             /**< Future minor-ABI expansion slots. */
};

/** @brief Function pointer type for the single exported v1 plugin entry point. */
typedef GridLockStatus (GRIDLOCK_CALL *GridLockPluginInitV1Fn)(
    const GridLockHostApi* host,
    GridLockPluginDescriptor* out_desc);

/**
 * @brief Single versioned entry point exported by every GridLock v1.x native plugin.
 * @param host Non-null pointer to the host API table valid for the plugin's lifetime.
 * @param out_desc Non-null descriptor initialized by host and populated by the plugin.
 * @return `GRIDLOCK_OK` on success, or a negative `GridLockStatus` error code.
 */
GRIDLOCK_PLUGIN_EXPORT GridLockStatus GRIDLOCK_CALL gridlock_plugin_init_v1(
    const GridLockHostApi* host,
    GridLockPluginDescriptor* out_desc);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GRIDLOCK_PLUGIN_API_H */
