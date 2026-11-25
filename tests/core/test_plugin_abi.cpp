/**
 * @file test_plugin_abi.cpp
 * @brief Compile-time C99 plugin ABI v1.0 layout gate and C++23 helper unit tests.
 *
 * Freezes exact `sizeof(...)` and `offsetof(...)` invariants for every field in
 * `GridLockHostApi`, `GridLockPluginDescriptor`, `GridLockStopInfo`,
 * `GridLockVariableContext`, and `GridLockCommandSpec`, and exercises the
 * header-only C++23 wrappers in `gridlock/plugin_cpp_helpers.hpp`.
 */

#include "gridlock/plugin_api.h"
#include "gridlock/plugin_cpp_helpers.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include <QObject>
#include <QtTest>

// ============================================================================
// 1. Compile-Time ABI Version, Status Code, and Standard-Layout Gates
// ============================================================================

static_assert(GRIDLOCK_PLUGIN_ABI_MAJOR == 1u, "Frozen ABI major version must be 1");
static_assert(GRIDLOCK_PLUGIN_ABI_MINOR == 0u, "Frozen ABI minor version must be 0");
static_assert(sizeof(GridLockStatus) == 4u, "GridLockStatus must be fixed 32-bit signed");
static_assert(sizeof(GridLockLogLevel) == 4u, "GridLockLogLevel must be fixed 32-bit signed");

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

static_assert(std::is_standard_layout_v<GridLockStopInfo>);
static_assert(std::is_trivially_copyable_v<GridLockStopInfo>);
static_assert(std::is_standard_layout_v<GridLockVariableContext>);
static_assert(std::is_trivially_copyable_v<GridLockVariableContext>);
static_assert(std::is_standard_layout_v<GridLockCommandSpec>);
static_assert(std::is_trivially_copyable_v<GridLockCommandSpec>);
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

// GridLockHostApi (192 bytes, including void* reserved[8] tail padding)
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
static_assert(offsetof(GridLockHostApi, reserved) == 128u);
static_assert(sizeof(std::declval<GridLockHostApi>().reserved) == 64u);
static_assert(sizeof(GridLockHostApi) == 192u);

// GridLockPluginDescriptor (152 bytes, including void* reserved[8] tail padding)
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
static_assert(offsetof(GridLockPluginDescriptor, reserved) == 88u);
static_assert(sizeof(std::declval<GridLockPluginDescriptor>().reserved) == 64u);
static_assert(sizeof(GridLockPluginDescriptor) == 152u);

namespace {

/** @brief Fake host backing store exercising every callback slot in `GridLockHostApi`. */
struct MockHostState {
  int32_t rankCount = 4;
  int32_t activeRank = 2;
  int32_t liveAllocs = 0;
  int32_t evalCallCount = 0;
  int32_t nextBreakpointId = 10;
  size_t simulatedShortReadLimit = 0;
  size_t simulatedOversizedEvalLen = 0;
  std::string evalPayload = "42";
  std::string lastChannel;
  std::string lastJsonPayload;
  std::string lastLog;
  std::vector<std::byte> targetMemory;
  GridLockVariableActionCallback varCb = nullptr;
  void* varPluginCtx = nullptr;
  GridLockCommandActionCallback cmdCb = nullptr;
  void* cmdPluginCtx = nullptr;
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
  if (hostCtx == nullptr || msg == nullptr || level < GRIDLOCK_LOG_TRACE) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
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
  if (state == nullptr || outBytes == nullptr || outRead == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  if (rank < 0 || rank >= state->rankCount) return GRIDLOCK_ERR_INVALID_RANK;
  if (addr + numBytes > state->targetMemory.size()) return GRIDLOCK_ERR_MEMORY_READ_FAILED;
  const size_t toCopy = (state->simulatedShortReadLimit > 0u &&
                         state->simulatedShortReadLimit < numBytes)
                            ? state->simulatedShortReadLimit
                            : numBytes;
  std::memcpy(outBytes, state->targetMemory.data() + addr, toCopy);
  *outRead = toCopy;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockWriteMemory(
    void* hostCtx, int32_t rank, uint64_t addr, const void* inBytes, size_t numBytes,
    size_t* outWritten) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (state == nullptr || inBytes == nullptr || outWritten == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  if (rank < 0 || rank >= state->rankCount || addr + numBytes > state->targetMemory.size()) {
    return GRIDLOCK_ERR_MEMORY_READ_FAILED;
  }
  std::memcpy(state->targetMemory.data() + addr, inBytes, numBytes);
  *outWritten = numBytes;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockSetBreakpoint(
    void* hostCtx, const char* file, int32_t line, const char* cond, int32_t* outBpId) {
  (void)cond;
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (state == nullptr || file == nullptr || line <= 0 || outBpId == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
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
  if (state == nullptr || actionId == nullptr || label == nullptr || regex == nullptr ||
      cb == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  state->varCb = cb;
  state->varPluginCtx = pluginCtx;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockRegisterCmdAction(
    void* hostCtx, const char* cmdId, const char* label, const char* shortcut,
    GridLockCommandActionCallback cb, void* pluginCtx) {
  (void)shortcut;
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (state == nullptr || cmdId == nullptr || label == nullptr || cb == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  state->cmdCb = cb;
  state->cmdPluginCtx = pluginCtx;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL mockEmitEvent(
    void* hostCtx, const char* channel, const char* jsonPayload) {
  auto* state = static_cast<MockHostState*>(hostCtx);
  if (state == nullptr || channel == nullptr || jsonPayload == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  state->lastChannel = channel;
  state->lastJsonPayload = jsonPayload;
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
  /** @brief Verifies ABI compatibility checks, forward minor versions, and descriptor header init. */
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

    // Hypothetical v1.1 host struct with trailing expansion slots must be accepted.
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

  /** @brief Verifies `evalExpr` across fast stack buffers, dynamic retry, overflow bounds, and errors. */
  void testEvalExprStackRetryAndErrors() {
    MockHostState state;
    const GridLockHostApi host = makeMockHostApi(&state);

    const auto fastRes = gridlock::plugin::evalExpr(&host, 1, "local_rank");
    QVERIFY(fastRes.has_value());
    QCOMPARE(*fastRes, std::string("42"));

    // Exceed the 256-byte stack buffer to exercise GRIDLOCK_ERR_BUFFER_TOO_SMALL retry.
    state.evalPayload = std::string(1024u, 'x');
    const auto largeRes = gridlock::plugin::evalExpr(&host, 0, "big_tensor_summary");
    QVERIFY(largeRes.has_value());
    QCOMPARE(largeRes->size(), std::size_t{1024u});
    QCOMPARE(*largeRes, state.evalPayload);

    // Reject outLen == SIZE_MAX and kMaxEvalExprBytes without wrapping outLen + 1u or retrying.
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

  /** @brief Verifies `readBytes`, `readPod<T>`, RAII `allocHostBytes`, and callback dispatch. */
  void testReadPodRaiiAndHostCallbacks() {
    MockHostState state;
    state.targetMemory.resize(64u, std::byte{0});
    const GridLockHostApi host = makeMockHostApi(&state);

    const SamplePodHeader expected{0x7ffd00102000ULL, 128u, 64u};
    size_t written = 0u;
    QCOMPARE(host.write_memory(host.host_ctx, 2, 8u, &expected, sizeof(expected), &written),
             GRIDLOCK_OK);
    QCOMPARE(written, sizeof(expected));

    const auto podRes = gridlock::plugin::readPod<SamplePodHeader>(&host, 2, 8u);
    QVERIFY(podRes.has_value());
    QCOMPARE(podRes->dataPtr, expected.dataPtr);
    QCOMPARE(podRes->extentX, expected.extentX);
    QCOMPARE(podRes->extentY, expected.extentY);

    // Simulate a truncated read to ensure readPod rejects partial structures.
    state.simulatedShortReadLimit = 4u;
    const auto shortPod = gridlock::plugin::readPod<SamplePodHeader>(&host, 2, 8u);
    QVERIFY(!shortPod.has_value());
    QCOMPARE(shortPod.error(), GRIDLOCK_ERR_MEMORY_READ_FAILED);
    state.simulatedShortReadLimit = 0u;

    // Verify symmetric RAII host allocation / deallocation.
    {
      auto bufRes = gridlock::plugin::allocHostBytes(&host, 128u);
      QVERIFY(bufRes.has_value());
      QCOMPARE(state.liveAllocs, 1);
    }
    QCOMPARE(state.liveAllocs, 0);

    // Exercise remaining host callbacks (rank queries, breakpoints, structured events).
    int32_t ranks = 0;
    int32_t active = -1;
    int32_t bpId = -1;
    QCOMPARE(host.get_rank_count(host.host_ctx, &ranks), GRIDLOCK_OK);
    QCOMPARE(host.get_active_rank(host.host_ctx, &active), GRIDLOCK_OK);
    QCOMPARE(ranks, 4);
    QCOMPARE(active, 2);
    QCOMPARE(host.set_breakpoint(host.host_ctx, "solver.cpp", 42, "iter > 5", &bpId), GRIDLOCK_OK);
    QCOMPARE(bpId, 10);
    QCOMPARE(host.remove_breakpoint(host.host_ctx, bpId), GRIDLOCK_OK);
    QCOMPARE(host.emit_structured_event(host.host_ctx, "mesh.snapshot", "{\"nx\":128}"),
             GRIDLOCK_OK);
    QCOMPARE(state.lastChannel, std::string("mesh.snapshot"));
    QCOMPARE(state.lastJsonPayload, std::string("{\"nx\":128}"));
  }
};

#ifdef GRIDLOCK_PLUGIN_ABI_STANDALONE_MAIN
QTEST_MAIN(TestPluginAbi)
#endif
#include "test_plugin_abi.moc"
