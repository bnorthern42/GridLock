/**
 * @file test_plugin_abi.cpp
 * @brief Compile-time C99 plugin ABI v1.0 layout gate and C++23 helper unit tests.
 *
 * Freezes exact `sizeof(...)` and `offsetof(...)` invariants for every field in
 * `GridLockHostApi`, `GridLockPluginDescriptor`, `GridLockStopInfo`,
 * `GridLockVariableContext`, `GridLockCommandSpec`, `GridLockSurfaceContext`, and
 * `GridLockThemeInfo`, and exercises the header-only C++23 wrappers in
 * `gridlock/plugin_cpp_helpers.hpp`.
 */

#include "gridlock/plugin_api.h"
#include "gridlock/plugin_cpp_helpers.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include <QObject>
#include <QtTest>

// ============================================================================
// 1. Compile-Time ABI Version, Enum Constants, and Standard-Layout Gates
// ============================================================================

static_assert(GRIDLOCK_PLUGIN_ABI_MAJOR == 1u, "Frozen ABI major version must be 1");
static_assert(GRIDLOCK_PLUGIN_ABI_MINOR == 0u, "Frozen ABI minor version must be 0");
static_assert(sizeof(GridLockStatus) == 4u, "GridLockStatus must be fixed 32-bit signed");
static_assert(sizeof(GridLockLogLevel) == 4u, "GridLockLogLevel must be fixed 32-bit signed");
static_assert(sizeof(GridLockDialogSeverity) == 4u, "GridLockDialogSeverity must be 32-bit signed");
static_assert(sizeof(GridLockDialogButtons) == 4u, "GridLockDialogButtons must be 32-bit signed");
static_assert(sizeof(GridLockDialogResult) == 4u, "GridLockDialogResult must be 32-bit signed");
static_assert(sizeof(GridLockContextSurface) == 4u, "GridLockContextSurface must be 32-bit signed");
static_assert(sizeof(GridLockReportFormat) == 4u, "GridLockReportFormat must be 32-bit signed");

static_assert(GRIDLOCK_OK == 0);
static_assert(GRIDLOCK_ERR_INVALID_ARG == -1);
static_assert(GRIDLOCK_ERR_ABI_MISMATCH == -2);
static_assert(GRIDLOCK_ERR_BUFFER_TOO_SMALL == -3);
static_assert(GRIDLOCK_ERR_NOT_STOPPED == -4);
static_assert(GRIDLOCK_ERR_INVALID_RANK == -5);
static_assert(GRIDLOCK_ERR_EVAL_FAILED == -6);
static_assert(GRIDLOCK_ERR_MEMORY_READ_FAILED == -7);
static_assert(GRIDLOCK_ERR_UNSUPPORTED == -8);
static_assert(GRIDLOCK_ERR_TIMEOUT == -9);
static_assert(GRIDLOCK_ERR_CANCELLED == -10);

static_assert(GRIDLOCK_DIALOG_INFO == 0);
static_assert(GRIDLOCK_DIALOG_WARNING == 1);
static_assert(GRIDLOCK_DIALOG_ERROR == 2);
static_assert(GRIDLOCK_DIALOG_QUESTION == 3);
static_assert(GRIDLOCK_BUTTONS_OK == 1);
static_assert(GRIDLOCK_BUTTONS_OK_CANCEL == 2);
static_assert(GRIDLOCK_BUTTONS_YES_NO == 3);
static_assert(GRIDLOCK_RESULT_OK == 1);
static_assert(GRIDLOCK_RESULT_CANCEL == 2);
static_assert(GRIDLOCK_RESULT_YES == 3);
static_assert(GRIDLOCK_RESULT_NO == 4);

static_assert(GRIDLOCK_SURFACE_VARIABLES == 1);
static_assert(GRIDLOCK_SURFACE_CODE_EDITOR == 2);
static_assert(GRIDLOCK_SURFACE_CALL_STACK == 3);
static_assert(GRIDLOCK_SURFACE_BREAKPOINTS == 4);
static_assert(GRIDLOCK_SURFACE_RANK_LIST == 5);

static_assert(GRIDLOCK_REPORT_MARKDOWN == 1);
static_assert(GRIDLOCK_REPORT_KEY_VALUE_TABLE == 2);
static_assert(GRIDLOCK_REPORT_JSON_TREE == 3);

static_assert(std::is_standard_layout_v<GridLockStopInfo>);
static_assert(std::is_trivially_copyable_v<GridLockStopInfo>);
static_assert(std::is_standard_layout_v<GridLockVariableContext>);
static_assert(std::is_trivially_copyable_v<GridLockVariableContext>);
static_assert(std::is_standard_layout_v<GridLockCommandSpec>);
static_assert(std::is_trivially_copyable_v<GridLockCommandSpec>);
static_assert(std::is_standard_layout_v<GridLockSurfaceContext>);
static_assert(std::is_trivially_copyable_v<GridLockSurfaceContext>);
static_assert(std::is_standard_layout_v<GridLockThemeInfo>);
static_assert(std::is_trivially_copyable_v<GridLockThemeInfo>);
static_assert(std::is_standard_layout_v<GridLockHostApi>);
static_assert(std::is_trivially_copyable_v<GridLockHostApi>);
static_assert(std::is_standard_layout_v<GridLockPluginDescriptor>);
static_assert(std::is_trivially_copyable_v<GridLockPluginDescriptor>);

static_assert(sizeof(void*) == 8u && sizeof(size_t) == 8u,
              "64-bit ABI layout gate expects 8-byte pointers and size_t");

// ============================================================================
// 2. Exact Field Offset & Struct Size Gates (64-bit LP64 / LLP64)
// ============================================================================

// GridLockStopInfo (64 bytes, zero implicit padding)
static_assert(offsetof(GridLockStopInfo, struct_size) == 0u);
static_assert(offsetof(GridLockStopInfo, abi_major) == 4u);
static_assert(offsetof(GridLockStopInfo, abi_minor) == 8u);
static_assert(offsetof(GridLockStopInfo, reserved_flags) == 12u);
static_assert(offsetof(GridLockStopInfo, rank) == 16u);
static_assert(offsetof(GridLockStopInfo, thread_id) == 20u);
static_assert(offsetof(GridLockStopInfo, line) == 24u);
static_assert(offsetof(GridLockStopInfo, frame_level) == 28u);
static_assert(offsetof(GridLockStopInfo, pc_address) == 32u);
static_assert(offsetof(GridLockStopInfo, reason_utf8) == 40u);
static_assert(offsetof(GridLockStopInfo, file_utf8) == 48u);
static_assert(offsetof(GridLockStopInfo, function_utf8) == 56u);
static_assert(sizeof(GridLockStopInfo) == 64u);

// GridLockVariableContext (80 bytes, zero implicit padding)
static_assert(offsetof(GridLockVariableContext, struct_size) == 0u);
static_assert(offsetof(GridLockVariableContext, abi_major) == 4u);
static_assert(offsetof(GridLockVariableContext, abi_minor) == 8u);
static_assert(offsetof(GridLockVariableContext, reserved_flags) == 12u);
static_assert(offsetof(GridLockVariableContext, rank) == 16u);
static_assert(offsetof(GridLockVariableContext, frame_level) == 20u);
static_assert(offsetof(GridLockVariableContext, address) == 24u);
static_assert(offsetof(GridLockVariableContext, byte_size) == 32u);
static_assert(offsetof(GridLockVariableContext, action_id_utf8) == 40u);
static_assert(offsetof(GridLockVariableContext, name_utf8) == 48u);
static_assert(offsetof(GridLockVariableContext, expression_utf8) == 56u);
static_assert(offsetof(GridLockVariableContext, type_utf8) == 64u);
static_assert(offsetof(GridLockVariableContext, value_utf8) == 72u);
static_assert(sizeof(GridLockVariableContext) == 80u);

// GridLockCommandSpec (56 bytes, zero implicit padding)
static_assert(offsetof(GridLockCommandSpec, struct_size) == 0u);
static_assert(offsetof(GridLockCommandSpec, abi_major) == 4u);
static_assert(offsetof(GridLockCommandSpec, abi_minor) == 8u);
static_assert(offsetof(GridLockCommandSpec, reserved_flags) == 12u);
static_assert(offsetof(GridLockCommandSpec, active_rank) == 16u);
static_assert(offsetof(GridLockCommandSpec, rank_count) == 20u);
static_assert(offsetof(GridLockCommandSpec, command_id_utf8) == 24u);
static_assert(offsetof(GridLockCommandSpec, menu_label_utf8) == 32u);
static_assert(offsetof(GridLockCommandSpec, shortcut_hint_utf8) == 40u);
static_assert(offsetof(GridLockCommandSpec, args_utf8) == 48u);
static_assert(sizeof(GridLockCommandSpec) == 56u);

// GridLockSurfaceContext (80 bytes, zero implicit padding)
static_assert(offsetof(GridLockSurfaceContext, struct_size) == 0u);
static_assert(offsetof(GridLockSurfaceContext, abi_major) == 4u);
static_assert(offsetof(GridLockSurfaceContext, abi_minor) == 8u);
static_assert(offsetof(GridLockSurfaceContext, reserved_flags) == 12u);
static_assert(offsetof(GridLockSurfaceContext, surface) == 16u);
static_assert(offsetof(GridLockSurfaceContext, rank) == 20u);
static_assert(offsetof(GridLockSurfaceContext, frame_level) == 24u);
static_assert(offsetof(GridLockSurfaceContext, line) == 28u);
static_assert(offsetof(GridLockSurfaceContext, column) == 32u);
static_assert(offsetof(GridLockSurfaceContext, bp_id) == 36u);
static_assert(offsetof(GridLockSurfaceContext, address) == 40u);
static_assert(offsetof(GridLockSurfaceContext, symbol_utf8) == 48u);
static_assert(offsetof(GridLockSurfaceContext, type_utf8) == 56u);
static_assert(offsetof(GridLockSurfaceContext, file_utf8) == 64u);
static_assert(offsetof(GridLockSurfaceContext, selected_text_utf8) == 72u);
static_assert(sizeof(GridLockSurfaceContext) == 80u);

// GridLockThemeInfo (212 bytes, zero implicit padding)
static_assert(offsetof(GridLockThemeInfo, struct_size) == 0u);
static_assert(offsetof(GridLockThemeInfo, abi_major) == 4u);
static_assert(offsetof(GridLockThemeInfo, abi_minor) == 8u);
static_assert(offsetof(GridLockThemeInfo, reserved_flags) == 12u);
static_assert(offsetof(GridLockThemeInfo, is_dark) == 16u);
static_assert(offsetof(GridLockThemeInfo, theme_id_utf8) == 20u);
static_assert(offsetof(GridLockThemeInfo, bg_primary_hex) == 84u);
static_assert(offsetof(GridLockThemeInfo, bg_secondary_hex) == 100u);
static_assert(offsetof(GridLockThemeInfo, fg_primary_hex) == 116u);
static_assert(offsetof(GridLockThemeInfo, fg_muted_hex) == 132u);
static_assert(offsetof(GridLockThemeInfo, accent_hex) == 148u);
static_assert(offsetof(GridLockThemeInfo, selection_hex) == 164u);
static_assert(offsetof(GridLockThemeInfo, warning_hex) == 180u);
static_assert(offsetof(GridLockThemeInfo, error_hex) == 196u);
static_assert(sizeof(GridLockThemeInfo) == 212u);

// GridLockHostApi (296 bytes, including void* reserved[8] tail padding)
static_assert(offsetof(GridLockHostApi, struct_size) == 0u);
static_assert(offsetof(GridLockHostApi, abi_major) == 4u);
static_assert(offsetof(GridLockHostApi, abi_minor) == 8u);
static_assert(offsetof(GridLockHostApi, reserved_flags) == 12u);
static_assert(offsetof(GridLockHostApi, host_ctx) == 16u);
static_assert(offsetof(GridLockHostApi, alloc_bytes) == 24u);
static_assert(offsetof(GridLockHostApi, free_bytes) == 32u);
static_assert(offsetof(GridLockHostApi, log_message) == 40u);
static_assert(offsetof(GridLockHostApi, get_rank_count) == 48u);
static_assert(offsetof(GridLockHostApi, get_active_rank) == 56u);
static_assert(offsetof(GridLockHostApi, eval_expr) == 64u);
static_assert(offsetof(GridLockHostApi, read_memory) == 72u);
static_assert(offsetof(GridLockHostApi, write_memory) == 80u);
static_assert(offsetof(GridLockHostApi, set_breakpoint) == 88u);
static_assert(offsetof(GridLockHostApi, remove_breakpoint) == 96u);
static_assert(offsetof(GridLockHostApi, register_variable_action) == 104u);
static_assert(offsetof(GridLockHostApi, register_command_action) == 112u);
static_assert(offsetof(GridLockHostApi, emit_structured_event) == 120u);
static_assert(offsetof(GridLockHostApi, show_message_box) == 128u);
static_assert(offsetof(GridLockHostApi, prompt_input) == 136u);
static_assert(offsetof(GridLockHostApi, show_toast) == 144u);
static_assert(offsetof(GridLockHostApi, register_menu_action) == 152u);
static_assert(offsetof(GridLockHostApi, set_status_item) == 160u);
static_assert(offsetof(GridLockHostApi, remove_status_item) == 168u);
static_assert(offsetof(GridLockHostApi, register_context_action) == 176u);
static_assert(offsetof(GridLockHostApi, navigate_to_location) == 184u);
static_assert(offsetof(GridLockHostApi, set_editor_annotation) == 192u);
static_assert(offsetof(GridLockHostApi, clear_editor_annotations) == 200u);
static_assert(offsetof(GridLockHostApi, get_theme_info) == 208u);
static_assert(offsetof(GridLockHostApi, publish_report_view) == 216u);
static_assert(offsetof(GridLockHostApi, close_report_view) == 224u);
static_assert(offsetof(GridLockHostApi, reserved) == 232u);
static_assert(sizeof(std::declval<GridLockHostApi>().reserved) == 64u);
static_assert(sizeof(GridLockHostApi) == 296u);

// GridLockPluginDescriptor (160 bytes, including void* reserved[8] tail padding)
static_assert(offsetof(GridLockPluginDescriptor, struct_size) == 0u);
static_assert(offsetof(GridLockPluginDescriptor, abi_major) == 4u);
static_assert(offsetof(GridLockPluginDescriptor, abi_minor) == 8u);
static_assert(offsetof(GridLockPluginDescriptor, reserved_flags) == 12u);
static_assert(offsetof(GridLockPluginDescriptor, plugin_id_utf8) == 16u);
static_assert(offsetof(GridLockPluginDescriptor, plugin_name_utf8) == 24u);
static_assert(offsetof(GridLockPluginDescriptor, plugin_version_utf8) == 32u);
static_assert(offsetof(GridLockPluginDescriptor, description_utf8) == 40u);
static_assert(offsetof(GridLockPluginDescriptor, plugin_ctx) == 48u);
static_assert(offsetof(GridLockPluginDescriptor, on_session_start) == 56u);
static_assert(offsetof(GridLockPluginDescriptor, on_rank_stopped) == 64u);
static_assert(offsetof(GridLockPluginDescriptor, on_session_end) == 72u);
static_assert(offsetof(GridLockPluginDescriptor, shutdown) == 80u);
static_assert(offsetof(GridLockPluginDescriptor, on_theme_changed) == 88u);
static_assert(offsetof(GridLockPluginDescriptor, reserved) == 96u);
static_assert(sizeof(std::declval<GridLockPluginDescriptor>().reserved) == 64u);
static_assert(sizeof(GridLockPluginDescriptor) == 160u);

namespace {

/** @brief Fake host backing store exercising every callback slot in `GridLockHostApi`. */
struct MockHostState {
  int32_t rankCount = 4;
  int32_t activeRank = 2;
  int32_t liveAllocs = 0;
  int32_t evalCallCount = 0;
  int32_t promptCallCount = 0;
  int32_t nextBreakpointId = 10;
  int32_t nextDialogResult = GRIDLOCK_RESULT_YES;
  int32_t lastNavLine = -1;
  int32_t lastNavCol = -1;
  bool promptCancelled = false;
  size_t simulatedShortReadLimit = 0;
  size_t simulatedOversizedEvalLen = 0;
  size_t simulatedOversizedPromptLen = 0;
  std::string evalPayload = "42";
  std::string promptPayload = "solver_field";
  std::string lastChannel;
  std::string lastJsonPayload;
  std::string lastLog;
  std::string lastToastTitle;
  std::string lastMenuPath;
  std::string lastStatusItemId;
  std::string lastStatusText;
  std::string lastAnnotationId;
  std::string lastClearedPrefix;
  std::string lastReportPanelId;
  std::string lastReportContent;
  GridLockReportFormat lastReportFormat = GRIDLOCK_REPORT_MARKDOWN;
  std::vector<std::byte> targetMemory;
  GridLockVariableActionCallback varCb = nullptr;
  void* varPluginCtx = nullptr;
  GridLockCommandActionCallback cmdCb = nullptr;
  void* cmdPluginCtx = nullptr;
  GridLockStatusItemCallback statusCb = nullptr;
  void* statusPluginCtx = nullptr;
  GridLockContextActionFn surfaceCb = nullptr;
  void* surfacePluginCtx = nullptr;
  GridLockContextSurface registeredSurface = GRIDLOCK_SURFACE_VARIABLES;
};

void* GRIDLOCK_CALL mockAllocBytes(void* hostCtx, size_t numBytes) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (state == nullptr || numBytes == 0u) return nullptr;
  void* mem = std::malloc(numBytes);
  if (mem != nullptr) ++state->liveAllocs;
  return mem;
}

void GRIDLOCK_CALL mockFreeBytes(void* hostCtx, void* ptr) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (state != nullptr && ptr != nullptr) {
    --state->liveAllocs;
    std::free(ptr);
  }
}

GridLockStatus GRIDLOCK_CALL mockLogMessage(void* hostCtx, GridLockLogLevel level, const char* msg) {
  if (hostCtx == nullptr || msg == nullptr || level < GRIDLOCK_LOG_TRACE) return GRIDLOCK_ERR_INVALID_ARG;
  static_cast<MockHostState*>(hostCtx)->lastLog = msg;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockGetRankCount(void* hostCtx, int32_t* outRanks) {
  if (hostCtx == nullptr || outRanks == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
  *outRanks = static_cast<MockHostState*>(hostCtx)->rankCount;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockGetActiveRank(void* hostCtx, int32_t* outRank) {
  if (hostCtx == nullptr || outRank == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
  *outRank = static_cast<MockHostState*>(hostCtx)->activeRank;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockEvalExpr(
    void* hostCtx, int32_t rank, const char* expr, char* outBuf, size_t cap, size_t* outLen) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (state == nullptr || expr == nullptr || outLen == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
  ++state->evalCallCount;
  if (rank < 0 || rank >= state->rankCount) return GRIDLOCK_ERR_INVALID_RANK;
  if (std::strcmp(expr, "bad_expr") == 0) return GRIDLOCK_ERR_EVAL_FAILED;
  if (state->simulatedOversizedEvalLen > 0u) {
    *outLen = state->simulatedOversizedEvalLen;
    return GRIDLOCK_ERR_BUFFER_TOO_SMALL;
  }
  *outLen = state->evalPayload.size();
  if (outBuf == nullptr || cap <= state->evalPayload.size()) return GRIDLOCK_ERR_BUFFER_TOO_SMALL;
  std::memcpy(outBuf, state->evalPayload.data(), state->evalPayload.size());
  outBuf[state->evalPayload.size()] = '\0';
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockReadMemory(
    void* hostCtx, int32_t rank, uint64_t addr, void* outBytes, size_t numBytes, size_t* outRead) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (state == nullptr || outBytes == nullptr || outRead == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
  if (rank < 0 || rank >= state->rankCount) return GRIDLOCK_ERR_INVALID_RANK;
  if (addr + numBytes > state->targetMemory.size()) return GRIDLOCK_ERR_MEMORY_READ_FAILED;
  const size_t toCopy = (state->simulatedShortReadLimit > 0u && state->simulatedShortReadLimit < numBytes)
                            ? state->simulatedShortReadLimit
                            : numBytes;
  std::memcpy(outBytes, state->targetMemory.data() + addr, toCopy);
  *outRead = toCopy;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockWriteMemory(
    void* hostCtx, int32_t rank, uint64_t addr, const void* inBytes, size_t numBytes, size_t* outWritten) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (state == nullptr || inBytes == nullptr || outWritten == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
  if (rank < 0 || rank >= state->rankCount || addr + numBytes > state->targetMemory.size()) {
    return GRIDLOCK_ERR_MEMORY_READ_FAILED;
  }
  std::memcpy(state->targetMemory.data() + addr, inBytes, numBytes);
  *outWritten = numBytes;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockSetBreakpoint(
    void* hostCtx, const char* file, int32_t line, const char*, int32_t* outBpId) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (state == nullptr || file == nullptr || line <= 0 || outBpId == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
  *outBpId = state->nextBreakpointId++;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockRemoveBreakpoint(void* hostCtx, int32_t bpId) {
  return (hostCtx != nullptr && bpId > 0) ? GRIDLOCK_OK : GRIDLOCK_ERR_INVALID_ARG;
}

GridLockStatus GRIDLOCK_CALL mockRegisterVarAction(
    void* hostCtx, const char* actionId, const char* label, const char* regex,
    GridLockVariableActionCallback cb, void* pluginCtx) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (!state || !actionId || !label || !regex || !cb) return GRIDLOCK_ERR_INVALID_ARG;
  state->varCb = cb; state->varPluginCtx = pluginCtx;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockRegisterCmdAction(
    void* hostCtx, const char* cmdId, const char* label, const char*,
    GridLockCommandActionCallback cb, void* pluginCtx) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (!state || !cmdId || !label || !cb) return GRIDLOCK_ERR_INVALID_ARG;
  state->cmdCb = cb; state->cmdPluginCtx = pluginCtx;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockEmitEvent(void* hostCtx, const char* channel, const char* jsonPayload) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (!state || !channel || !jsonPayload) return GRIDLOCK_ERR_INVALID_ARG;
  state->lastChannel = channel; state->lastJsonPayload = jsonPayload;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockShowMessageBox(
    void* hostCtx, GridLockDialogSeverity, GridLockDialogButtons,
    const char* title, const char* body, int32_t* outResult) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (!state || !title || !body || !outResult) return GRIDLOCK_ERR_INVALID_ARG;
  *outResult = state->nextDialogResult;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockPromptInput(
    void* hostCtx, const char* title, const char* label, const char*,
    char* outBuf, size_t cap, size_t* outLen) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (!state || !title || !label || !outLen) return GRIDLOCK_ERR_INVALID_ARG;
  ++state->promptCallCount;
  if (state->promptCancelled) return GRIDLOCK_ERR_CANCELLED;
  if (state->simulatedOversizedPromptLen > 0u) {
    *outLen = state->simulatedOversizedPromptLen;
    return GRIDLOCK_ERR_BUFFER_TOO_SMALL;
  }
  *outLen = state->promptPayload.size();
  if (outBuf == nullptr || cap <= state->promptPayload.size()) return GRIDLOCK_ERR_BUFFER_TOO_SMALL;
  std::memcpy(outBuf, state->promptPayload.data(), state->promptPayload.size());
  outBuf[state->promptPayload.size()] = '\0';
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockShowToast(
    void* hostCtx, GridLockDialogSeverity, const char* title, const char* body, int32_t) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (!state || !title || !body) return GRIDLOCK_ERR_INVALID_ARG;
  state->lastToastTitle = title;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockRegisterMenuAction(
    void* hostCtx, const char* actionId, const char* menuPath, const char*,
    GridLockCommandActionCallback cb, void* pluginCtx) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (!state || !actionId || !menuPath || !cb) return GRIDLOCK_ERR_INVALID_ARG;
  state->lastMenuPath = menuPath; state->cmdCb = cb; state->cmdPluginCtx = pluginCtx;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockSetStatusItem(
    void* hostCtx, const char* itemId, const char* text, const char*,
    const char*, GridLockStatusItemCallback cb, void* pluginCtx) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (!state || !itemId || !text) return GRIDLOCK_ERR_INVALID_ARG;
  state->lastStatusItemId = itemId; state->lastStatusText = text;
  state->statusCb = cb; state->statusPluginCtx = pluginCtx;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockRemoveStatusItem(void* hostCtx, const char* itemId) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (!state || !itemId) return GRIDLOCK_ERR_INVALID_ARG;
  if (state->lastStatusItemId == itemId) state->lastStatusItemId.clear();
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockRegisterContextAction(
    void* hostCtx, GridLockContextSurface surface, const char* actionId, const char* label,
    const char*, GridLockContextActionFn cb, void* pluginCtx) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (!state || !actionId || !label || !cb || surface < GRIDLOCK_SURFACE_VARIABLES) return GRIDLOCK_ERR_INVALID_ARG;
  state->registeredSurface = surface; state->surfaceCb = cb; state->surfacePluginCtx = pluginCtx;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockNavigateToLocation(void* hostCtx, const char* file, int32_t line, int32_t col) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (!state || !file || line <= 0) return GRIDLOCK_ERR_INVALID_ARG;
  state->lastNavLine = line; state->lastNavCol = col;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockSetEditorAnnotation(
    void* hostCtx, const char* annId, const char* file, int32_t line,
    const char* badge, const char*, const char*) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (!state || !annId || !file || !badge || line <= 0) return GRIDLOCK_ERR_INVALID_ARG;
  state->lastAnnotationId = annId;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockClearEditorAnnotations(void* hostCtx, const char* prefix) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (!state) return GRIDLOCK_ERR_INVALID_ARG;
  state->lastClearedPrefix = prefix ? prefix : "";
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockGetThemeInfo(void* hostCtx, GridLockThemeInfo* outTheme) {
  if (!hostCtx || !outTheme || outTheme->struct_size < sizeof(GridLockThemeInfo)) return GRIDLOCK_ERR_INVALID_ARG;
  outTheme->is_dark = 1;
  std::snprintf(outTheme->theme_id_utf8, sizeof(outTheme->theme_id_utf8), "%s", "midnight_slate");
  std::snprintf(outTheme->bg_primary_hex, sizeof(outTheme->bg_primary_hex), "%s", "#14171f");
  std::snprintf(outTheme->bg_secondary_hex, sizeof(outTheme->bg_secondary_hex), "%s", "#1b1f2a");
  std::snprintf(outTheme->fg_primary_hex, sizeof(outTheme->fg_primary_hex), "%s", "#e5e9f0");
  std::snprintf(outTheme->fg_muted_hex, sizeof(outTheme->fg_muted_hex), "%s", "#8c96a8");
  std::snprintf(outTheme->accent_hex, sizeof(outTheme->accent_hex), "%s", "#38bdf8");
  std::snprintf(outTheme->selection_hex, sizeof(outTheme->selection_hex), "%s", "#2b3a55");
  std::snprintf(outTheme->warning_hex, sizeof(outTheme->warning_hex), "%s", "#f59e0b");
  std::snprintf(outTheme->error_hex, sizeof(outTheme->error_hex), "%s", "#ef4444");
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockPublishReportView(
    void* hostCtx, const char* panelId, const char* title, GridLockReportFormat format, const char* content) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (!state || !panelId || !title || !content) return GRIDLOCK_ERR_INVALID_ARG;
  state->lastReportPanelId = panelId; state->lastReportFormat = format; state->lastReportContent = content;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockCloseReportView(void* hostCtx, const char* panelId) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (!state || !panelId) return GRIDLOCK_ERR_INVALID_ARG;
  if (state->lastReportPanelId == panelId) state->lastReportPanelId.clear();
  return GRIDLOCK_OK;
}

GridLockHostApi makeMockHostApi(MockHostState* state) {
  GridLockHostApi api{};
  api.struct_size = static_cast<uint32_t>(sizeof(GridLockHostApi));
  api.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
  api.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
  api.reserved_flags = 0u;
  api.host_ctx = state;
  api.alloc_bytes = &mockAllocBytes;
  api.free_bytes = &mockFreeBytes;
  api.log_message = &mockLogMessage;
  api.get_rank_count = &mockGetRankCount;
  api.get_active_rank = &mockGetActiveRank;
  api.eval_expr = &mockEvalExpr;
  api.read_memory = &mockReadMemory;
  api.write_memory = &mockWriteMemory;
  api.set_breakpoint = &mockSetBreakpoint;
  api.remove_breakpoint = &mockRemoveBreakpoint;
  api.register_variable_action = &mockRegisterVarAction;
  api.register_command_action = &mockRegisterCmdAction;
  api.emit_structured_event = &mockEmitEvent;
  api.show_message_box = &mockShowMessageBox;
  api.prompt_input = &mockPromptInput;
  api.show_toast = &mockShowToast;
  api.register_menu_action = &mockRegisterMenuAction;
  api.set_status_item = &mockSetStatusItem;
  api.remove_status_item = &mockRemoveStatusItem;
  api.register_context_action = &mockRegisterContextAction;
  api.navigate_to_location = &mockNavigateToLocation;
  api.set_editor_annotation = &mockSetEditorAnnotation;
  api.clear_editor_annotations = &mockClearEditorAnnotations;
  api.get_theme_info = &mockGetThemeInfo;
  api.publish_report_view = &mockPublishReportView;
  api.close_report_view = &mockCloseReportView;
  return api;
}

struct SamplePodHeader {
  uint64_t dataPtr;
  uint32_t extentX;
  uint32_t extentY;
};

struct ExtendedHostApiMinorV1 {
  GridLockHostApi v1;
  void* extraMinorCallbacks[4];
};

template <typename T>
concept CanReadPod = requires(const GridLockHostApi* h) {
  gridlock::plugin::readPod<T>(h, 0, 0u);
};

static_assert(noexcept(gridlock::plugin::evalExpr(nullptr, 0, "x")));
static_assert(noexcept(gridlock::plugin::promptInput(nullptr, "t", "l", "d")));
static_assert(noexcept(gridlock::plugin::getThemeInfo(nullptr)));
static_assert(CanReadPod<SamplePodHeader>);
static_assert(CanReadPod<uint32_t>);
static_assert(!CanReadPod<bool>);
static_assert(!CanReadPod<void*>);
static_assert(!CanReadPod<const double*>);
static_assert(!CanReadPod<uint32_t SamplePodHeader::*>);

}  // namespace

/**
 * @class TestPluginAbi
 * @brief QtTest suite verifying runtime ABI contract and C++23 wrapper helpers.
 */
class TestPluginAbi : public QObject {
  Q_OBJECT

 private slots:
  void testAbiValidationAndDescriptorInit() {
    MockHostState state;
    GridLockHostApi host = makeMockHostApi(&state);
    QCOMPARE(gridlock::plugin::validateHostAbi(nullptr), GRIDLOCK_ERR_INVALID_ARG);
    QCOMPARE(gridlock::plugin::validateHostAbi(&host), GRIDLOCK_OK);

    host.abi_major = 2u;
    QCOMPARE(gridlock::plugin::validateHostAbi(&host), GRIDLOCK_ERR_ABI_MISMATCH);
    host.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
    host.struct_size = static_cast<uint32_t>(offsetof(GridLockHostApi, reserved) - 1u);
    QCOMPARE(gridlock::plugin::validateHostAbi(&host), GRIDLOCK_ERR_ABI_MISMATCH);
    host.struct_size = static_cast<uint32_t>(offsetof(GridLockHostApi, reserved));
    QCOMPARE(gridlock::plugin::validateHostAbi(&host), GRIDLOCK_OK);

    ExtendedHostApiMinorV1 hostMinorV1{};
    hostMinorV1.v1 = makeMockHostApi(&state);
    hostMinorV1.v1.abi_minor = 1u;
    hostMinorV1.v1.struct_size = static_cast<uint32_t>(sizeof(ExtendedHostApiMinorV1));
    QCOMPARE(gridlock::plugin::validateHostAbi(&hostMinorV1.v1), GRIDLOCK_OK);

    GridLockPluginDescriptor desc{};
    QCOMPARE(gridlock::plugin::initPluginDescriptorHeader(&desc), GRIDLOCK_OK);
    QCOMPARE(desc.struct_size, static_cast<uint32_t>(sizeof(GridLockPluginDescriptor)));
    QCOMPARE(desc.abi_major, GRIDLOCK_PLUGIN_ABI_MAJOR);
    QCOMPARE(desc.abi_minor, GRIDLOCK_PLUGIN_ABI_MINOR);
  }

  void testEvalExprStackRetryAndErrors() {
    MockHostState state;
    const GridLockHostApi host = makeMockHostApi(&state);

    const auto fastRes = gridlock::plugin::evalExpr(&host, 1, "local_rank");
    QVERIFY(fastRes.has_value());
    QCOMPARE(*fastRes, std::string("42"));

    state.evalPayload = std::string(1024u, 'x');
    const auto largeRes = gridlock::plugin::evalExpr(&host, 0, "big_tensor_summary");
    QVERIFY(largeRes.has_value());
    QCOMPARE(largeRes->size(), std::size_t{1024u});
    QCOMPARE(*largeRes, state.evalPayload);

    state.evalCallCount = 0;
    state.simulatedOversizedEvalLen = std::numeric_limits<size_t>::max();
    const auto maxOverflowRes = gridlock::plugin::evalExpr(&host, 0, "huge_expr");
    QVERIFY(!maxOverflowRes.has_value());
    QCOMPARE(maxOverflowRes.error(), GRIDLOCK_ERR_BUFFER_TOO_SMALL);
    QCOMPARE(state.evalCallCount, 1);

    state.evalCallCount = 0;
    state.simulatedOversizedEvalLen = gridlock::plugin::kMaxEvalExprBytes;
    const auto capBoundaryRes = gridlock::plugin::evalExpr(&host, 0, "huge_expr");
    QVERIFY(!capBoundaryRes.has_value());
    QCOMPARE(capBoundaryRes.error(), GRIDLOCK_ERR_BUFFER_TOO_SMALL);
    QCOMPARE(state.evalCallCount, 1);
    state.simulatedOversizedEvalLen = 0u;

    const auto badRank = gridlock::plugin::evalExpr(&host, 99, "x");
    QVERIFY(!badRank.has_value());
    QCOMPARE(badRank.error(), GRIDLOCK_ERR_INVALID_RANK);

    const auto badExpr = gridlock::plugin::evalExpr(&host, 0, "bad_expr");
    QVERIFY(!badExpr.has_value());
    QCOMPARE(badExpr.error(), GRIDLOCK_ERR_EVAL_FAILED);
  }

  void testReadPodRaiiAndHostCallbacks() {
    MockHostState state;
    state.targetMemory.resize(64u, std::byte{0});
    const GridLockHostApi host = makeMockHostApi(&state);

    const SamplePodHeader expected{0x7ffd00102000ULL, 128u, 64u};
    size_t written = 0u;
    QCOMPARE(host.write_memory(host.host_ctx, 2, 8u, &expected, sizeof(expected), &written), GRIDLOCK_OK);
    QCOMPARE(written, sizeof(expected));

    const auto podRes = gridlock::plugin::readPod<SamplePodHeader>(&host, 2, 8u);
    QVERIFY(podRes.has_value());
    QCOMPARE(podRes->dataPtr, expected.dataPtr);
    QCOMPARE(podRes->extentX, expected.extentX);
    QCOMPARE(podRes->extentY, expected.extentY);

    state.simulatedShortReadLimit = 4u;
    const auto shortPod = gridlock::plugin::readPod<SamplePodHeader>(&host, 2, 8u);
    QVERIFY(!shortPod.has_value());
    QCOMPARE(shortPod.error(), GRIDLOCK_ERR_MEMORY_READ_FAILED);
    state.simulatedShortReadLimit = 0u;

    {
      auto bufRes = gridlock::plugin::allocHostBytes(&host, 128u);
      QVERIFY(bufRes.has_value());
      QCOMPARE(state.liveAllocs, 1);
    }
    QCOMPARE(state.liveAllocs, 0);

    int32_t ranks = 0, active = -1, bpId = -1;
    QCOMPARE(host.get_rank_count(host.host_ctx, &ranks), GRIDLOCK_OK);
    QCOMPARE(host.get_active_rank(host.host_ctx, &active), GRIDLOCK_OK);
    QCOMPARE(ranks, 4);
    QCOMPARE(active, 2);
    QCOMPARE(host.set_breakpoint(host.host_ctx, "solver.cpp", 42, "iter > 5", &bpId), GRIDLOCK_OK);
    QCOMPARE(bpId, 10);
    QCOMPARE(host.remove_breakpoint(host.host_ctx, bpId), GRIDLOCK_OK);
    QCOMPARE(host.emit_structured_event(host.host_ctx, "mesh.snapshot", "{\"nx\":128}"), GRIDLOCK_OK);
    QCOMPARE(state.lastChannel, std::string("mesh.snapshot"));
    QCOMPARE(state.lastJsonPayload, std::string("{\"nx\":128}"));
  }

  void testExpandedDialogsMenusSurfacesThemeAndReports() {
    MockHostState state;
    const GridLockHostApi host = makeMockHostApi(&state);

    // 1. Dialogs, promptInput (stack, dynamic retry, cancel, SIZE_MAX overflow), and toasts.
    int32_t dlgRes = 0;
    QCOMPARE(host.show_message_box(host.host_ctx, GRIDLOCK_DIALOG_QUESTION, GRIDLOCK_BUTTONS_YES_NO,
                                   "Export Mesh", "Write VTK snapshot?", &dlgRes),
             GRIDLOCK_OK);
    QCOMPARE(dlgRes, GRIDLOCK_RESULT_YES);

    const auto fastPrompt = gridlock::plugin::promptInput(&host, "Mesh Probe", "Field Expression:", "u_vel");
    QVERIFY(fastPrompt.has_value());
    QCOMPARE(*fastPrompt, std::string("solver_field"));

    state.promptPayload = std::string(600u, 'q');
    const auto longPrompt = gridlock::plugin::promptInput(&host, "Mesh Probe", "Long Expression:");
    QVERIFY(longPrompt.has_value());
    QCOMPARE(longPrompt->size(), std::size_t{600u});

    state.promptCancelled = true;
    const auto cancelledPrompt = gridlock::plugin::promptInput(&host, "Mesh Probe", "Cancel Test");
    QVERIFY(!cancelledPrompt.has_value());
    QCOMPARE(cancelledPrompt.error(), GRIDLOCK_ERR_CANCELLED);
    state.promptCancelled = false;

    state.promptCallCount = 0;
    state.simulatedOversizedPromptLen = std::numeric_limits<size_t>::max();
    const auto overflowPrompt = gridlock::plugin::promptInput(&host, "Mesh Probe", "Overflow Test");
    QVERIFY(!overflowPrompt.has_value());
    QCOMPARE(overflowPrompt.error(), GRIDLOCK_ERR_BUFFER_TOO_SMALL);
    QCOMPARE(state.promptCallCount, 1);
    state.simulatedOversizedPromptLen = 0u;

    QCOMPARE(host.show_toast(host.host_ctx, GRIDLOCK_DIALOG_INFO, "Mesh Exported", "Saved step 4", 3000),
             GRIDLOCK_OK);
    QCOMPARE(state.lastToastTitle, std::string("Mesh Exported"));

    // 2. Nested menus, clickable status-bar items, multi-surface context menus, editor navigation & annotations.
    auto dummyCmd = +[](void*, const GridLockHostApi*, const GridLockCommandSpec*) -> GridLockStatus {
      return GRIDLOCK_OK;
    };
    QCOMPARE(host.register_menu_action(host.host_ctx, "mesh.export", "Mesh Tools/Refinement/Export VTK",
                                       "Ctrl+Shift+M", dummyCmd, nullptr),
             GRIDLOCK_OK);
    QCOMPARE(state.lastMenuPath, std::string("Mesh Tools/Refinement/Export VTK"));

    auto dummyStatusCb = +[](void*, const GridLockHostApi*, const char*) -> GridLockStatus {
      return GRIDLOCK_OK;
    };
    QCOMPARE(host.set_status_item(host.host_ctx, "mesh.status", "AMR: L3", "Refinement Level 3",
                                  "accent", dummyStatusCb, nullptr),
             GRIDLOCK_OK);
    QCOMPARE(state.lastStatusText, std::string("AMR: L3"));
    QCOMPARE(host.remove_status_item(host.host_ctx, "mesh.status"), GRIDLOCK_OK);
    QVERIFY(state.lastStatusItemId.empty());

    auto dummySurfaceCb = +[](void*, const GridLockHostApi*, const GridLockSurfaceContext* ctx) -> GridLockStatus {
      return (ctx != nullptr && ctx->surface == GRIDLOCK_SURFACE_CALL_STACK) ? GRIDLOCK_OK
                                                                             : GRIDLOCK_ERR_INVALID_ARG;
    };
    QCOMPARE(host.register_context_action(host.host_ctx, GRIDLOCK_SURFACE_CALL_STACK, "mesh.frame_inspect",
                                          "Inspect Frame Domain", "solve.*", dummySurfaceCb, nullptr),
             GRIDLOCK_OK);
    GridLockSurfaceContext surfCtx{};
    surfCtx.struct_size = static_cast<uint32_t>(sizeof(GridLockSurfaceContext));
    surfCtx.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
    surfCtx.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
    surfCtx.surface = GRIDLOCK_SURFACE_CALL_STACK;
    surfCtx.rank = 1;
    surfCtx.frame_level = 2;
    surfCtx.line = 95;
    surfCtx.column = 4;
    surfCtx.bp_id = -1;
    surfCtx.address = 0x401280ULL;
    surfCtx.symbol_utf8 = "solveStep";
    QCOMPARE(state.surfaceCb(nullptr, &host, &surfCtx), GRIDLOCK_OK);

    QCOMPARE(host.navigate_to_location(host.host_ctx, "src/solver.cpp", 128, 12), GRIDLOCK_OK);
    QCOMPARE(state.lastNavLine, 128);
    QCOMPARE(state.lastNavCol, 12);
    QCOMPARE(host.set_editor_annotation(host.host_ctx, "mesh.ann.1", "src/solver.cpp", 128,
                                        "L2 Norm: 1.4e-6", "Converged", "warning"),
             GRIDLOCK_OK);
    QCOMPARE(state.lastAnnotationId, std::string("mesh.ann.1"));
    QCOMPARE(host.clear_editor_annotations(host.host_ctx, "mesh.ann."), GRIDLOCK_OK);
    QCOMPARE(state.lastClearedPrefix, std::string("mesh.ann."));

    // 3. Theme token query & declarative report views.
    const auto themeRes = gridlock::plugin::getThemeInfo(&host);
    QVERIFY(themeRes.has_value());
    QCOMPARE(themeRes->is_dark, 1);
    QCOMPARE(std::string(themeRes->theme_id_utf8), std::string("midnight_slate"));
    QCOMPARE(std::string(themeRes->accent_hex), std::string("#38bdf8"));

    QCOMPARE(host.publish_report_view(host.host_ctx, "mesh.report", "AMR Residual Summary",
                                      GRIDLOCK_REPORT_KEY_VALUE_TABLE, "{\"cells\":4096}"),
             GRIDLOCK_OK);
    QCOMPARE(state.lastReportFormat, GRIDLOCK_REPORT_KEY_VALUE_TABLE);
    QCOMPARE(host.close_report_view(host.host_ctx, "mesh.report"), GRIDLOCK_OK);
    QVERIFY(state.lastReportPanelId.empty());
  }
};

#ifdef GRIDLOCK_PLUGIN_ABI_STANDALONE_MAIN
QTEST_MAIN(TestPluginAbi)
#endif
#include "test_plugin_abi.moc"
