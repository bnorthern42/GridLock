/**
 * @file plugin_cpp_helpers.hpp
 * @brief Optional header-only C++23 convenience wrappers over GridLock's C99 plugin ABI.
 *
 * Provides zero-overhead RAII memory guards, `std::span` byte readers, typed POD
 * memory readers, `std::expected` expression evaluation, interactive user input
 * prompts, and theme token queries built strictly on top of `gridlock/plugin_api.h`
 * without altering the underlying C ABI layout or introducing any UI/Qt dependencies.
 */

#ifndef GRIDLOCK_PLUGIN_CPP_HELPERS_HPP
#define GRIDLOCK_PLUGIN_CPP_HELPERS_HPP

#include "gridlock/plugin_api.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace gridlock::plugin {

/** @brief Upper bound (16 MiB) for dynamic string buffers to prevent overflow and OOM. */
inline constexpr size_t kMaxEvalExprBytes = 16u * 1024u * 1024u;

/**
 * @struct HostDeleter
 * @brief Symmetric RAII deleter releasing host-allocated buffers via `free_bytes`.
 */
struct HostDeleter {
  const GridLockHostApi* host = nullptr; /**< Host API table owning the allocation. */

  /**
   * @brief Releases `ptr` back to `host->free_bytes` if non-null.
   * @param ptr Buffer previously returned by `host->alloc_bytes`.
   */
  void operator()(void* ptr) const noexcept {
    if (ptr != nullptr && host != nullptr && host->free_bytes != nullptr) {
      host->free_bytes(host->host_ctx, ptr);
    }
  }
};

/** @brief Unique byte-buffer handle backed by `GridLockHostApi::alloc_bytes` / `free_bytes`. */
using HostByteBuffer = std::unique_ptr<std::byte[], HostDeleter>;

/**
 * @brief Validates that a `GridLockHostApi` table matches major ABI v1 and v1.0 core fields.
 *
 * Accepts forward-compatible v1.x host tables whose `struct_size` is at least
 * `offsetof(GridLockHostApi, reserved)` (including tables larger than v1.0).
 *
 * @param host Candidate host API table pointer passed to `gridlock_plugin_init_v1`.
 * @return `GRIDLOCK_OK` when compatible, or `GRIDLOCK_ERR_INVALID_ARG` / `GRIDLOCK_ERR_ABI_MISMATCH`.
 */
[[nodiscard]] constexpr GridLockStatus validateHostAbi(const GridLockHostApi* host) noexcept {
  if (host == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  if (host->abi_major != GRIDLOCK_PLUGIN_ABI_MAJOR ||
      host->struct_size < offsetof(GridLockHostApi, reserved)) {
    return GRIDLOCK_ERR_ABI_MISMATCH;
  }
  return GRIDLOCK_OK;
}

/**
 * @brief Initializes the 16-byte ABI header and zeroes reserved fields on `outDesc`.
 * @param outDesc Plugin descriptor to stamp with ABI v1.0 header metadata.
 * @return `GRIDLOCK_OK` on success, or `GRIDLOCK_ERR_INVALID_ARG` / `GRIDLOCK_ERR_ABI_MISMATCH`.
 */
[[nodiscard]] constexpr GridLockStatus initPluginDescriptorHeader(
    GridLockPluginDescriptor* outDesc) noexcept {
  if (outDesc == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  if (outDesc->struct_size != 0u && outDesc->struct_size < sizeof(GridLockPluginDescriptor)) {
    return GRIDLOCK_ERR_ABI_MISMATCH;
  }
  *outDesc = GridLockPluginDescriptor{};
  outDesc->struct_size = static_cast<uint32_t>(sizeof(GridLockPluginDescriptor));
  outDesc->abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
  outDesc->abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
  outDesc->reserved_flags = 0u;
  return GRIDLOCK_OK;
}

/**
 * @brief Allocates a host-managed byte buffer wrapped in an RAII `HostByteBuffer`.
 * @param host Valid host API pointer providing `alloc_bytes` and `free_bytes`.
 * @param numBytes Number of bytes to request from the host allocator.
 * @return `HostByteBuffer` on success, or a negative `GridLockStatus` error code.
 */
[[nodiscard]] inline std::expected<HostByteBuffer, GridLockStatus> allocHostBytes(
    const GridLockHostApi* host,
    std::size_t numBytes) noexcept {
  if (host == nullptr || host->alloc_bytes == nullptr || host->free_bytes == nullptr) {
    return std::unexpected(GRIDLOCK_ERR_INVALID_ARG);
  }
  if (numBytes == 0u) {
    return HostByteBuffer(nullptr, HostDeleter{host});
  }
  void* raw = host->alloc_bytes(host->host_ctx, numBytes);
  if (raw == nullptr) {
    return std::unexpected(GRIDLOCK_ERR_BUFFER_TOO_SMALL);
  }
  return HostByteBuffer(static_cast<std::byte*>(raw), HostDeleter{host});
}

/**
 * @brief Evaluates a C/C++ debugger expression on `rank` and returns its UTF-8 value.
 *
 * Uses a fast 256-byte stack buffer first and automatically retries once with a
 * bounded heap buffer (`< kMaxEvalExprBytes`) when `eval_expr` reports
 * `GRIDLOCK_ERR_BUFFER_TOO_SMALL`. Never throws across C ABI boundaries.
 *
 * @param host Non-null host API table providing `eval_expr`.
 * @param rank Zero-based MPI rank index to query.
 * @param expr Debugger expression view (need not be NUL-terminated by caller).
 * @return Evaluated `std::string` on `GRIDLOCK_OK`, or `GridLockStatus` on failure.
 */
[[nodiscard]] inline std::expected<std::string, GridLockStatus> evalExpr(
    const GridLockHostApi* host,
    int32_t rank,
    std::string_view expr) noexcept {
  if (host == nullptr || host->eval_expr == nullptr || expr.empty()) {
    return std::unexpected(GRIDLOCK_ERR_INVALID_ARG);
  }

  try {
    const std::string exprNullTerm(expr);
    constexpr std::size_t kStackCapacity = 256u;
    std::array<char, kStackCapacity> stackBuf{};
    std::size_t outLen = 0u;

    GridLockStatus status = host->eval_expr(
        host->host_ctx, rank, exprNullTerm.c_str(), stackBuf.data(), stackBuf.size(), &outLen);
    if (status == GRIDLOCK_OK) {
      const std::size_t clampedLen =
          (outLen < stackBuf.size()) ? outLen : (stackBuf.size() - 1u);
      return std::string(stackBuf.data(), clampedLen);
    }
    if (status != GRIDLOCK_ERR_BUFFER_TOO_SMALL) {
      return std::unexpected(status);
    }
    if (outLen >= kMaxEvalExprBytes) {
      return std::unexpected(GRIDLOCK_ERR_BUFFER_TOO_SMALL);
    }

    // Safe against SIZE_MAX wraparound because outLen < kMaxEvalExprBytes.
    const std::size_t dynamicCap =
        (outLen >= stackBuf.size()) ? (outLen + 1u) : (stackBuf.size() * 16u);
    std::vector<char> dynamicBuf(dynamicCap, '\0');
    if (dynamicBuf.empty()) {
      return std::unexpected(GRIDLOCK_ERR_BUFFER_TOO_SMALL);
    }
    std::size_t retryLen = 0u;
    status = host->eval_expr(
        host->host_ctx, rank, exprNullTerm.c_str(), dynamicBuf.data(), dynamicBuf.size(), &retryLen);
    if (status != GRIDLOCK_OK) {
      return std::unexpected(status);
    }
    const std::size_t clampedRetryLen =
        (retryLen < dynamicBuf.size()) ? retryLen : (dynamicBuf.size() - 1u);
    return std::string(dynamicBuf.data(), clampedRetryLen);
  } catch (...) {
    return std::unexpected(GRIDLOCK_ERR_BUFFER_TOO_SMALL);
  }
}

/**
 * @brief Prompts the user for a single-line input string with automatic buffer retry.
 *
 * Uses a fast 256-byte stack buffer first and automatically retries once with a
 * bounded heap buffer (`< kMaxEvalExprBytes`) on `GRIDLOCK_ERR_BUFFER_TOO_SMALL`.
 * Returns `std::unexpected(GRIDLOCK_ERR_CANCELLED)` if the user cancels the dialog.
 *
 * @param host Non-null host API table providing `prompt_input`.
 * @param title Dialog window title text.
 * @param label Field prompt label text.
 * @param defaultVal Optional initial input text seeded into the field.
 * @return User-entered `std::string` on `GRIDLOCK_OK`, or negative `GridLockStatus` on error/cancel.
 */
[[nodiscard]] inline std::expected<std::string, GridLockStatus> promptInput(
    const GridLockHostApi* host,
    std::string_view title,
    std::string_view label,
    std::string_view defaultVal = {}) noexcept {
  if (host == nullptr || host->prompt_input == nullptr) {
    return std::unexpected(GRIDLOCK_ERR_INVALID_ARG);
  }

  try {
    const std::string titleNullTerm(title);
    const std::string labelNullTerm(label);
    const std::string defaultNullTerm(defaultVal);
    constexpr std::size_t kStackCapacity = 256u;
    std::array<char, kStackCapacity> stackBuf{};
    std::size_t outLen = 0u;

    GridLockStatus status = host->prompt_input(
        host->host_ctx, titleNullTerm.c_str(), labelNullTerm.c_str(), defaultNullTerm.c_str(),
        stackBuf.data(), stackBuf.size(), &outLen);
    if (status == GRIDLOCK_OK) {
      const std::size_t clampedLen =
          (outLen < stackBuf.size()) ? outLen : (stackBuf.size() - 1u);
      return std::string(stackBuf.data(), clampedLen);
    }
    if (status != GRIDLOCK_ERR_BUFFER_TOO_SMALL) {
      return std::unexpected(status);
    }
    if (outLen >= kMaxEvalExprBytes) {
      return std::unexpected(GRIDLOCK_ERR_BUFFER_TOO_SMALL);
    }

    const std::size_t dynamicCap =
        (outLen >= stackBuf.size()) ? (outLen + 1u) : (stackBuf.size() * 16u);
    std::vector<char> dynamicBuf(dynamicCap, '\0');
    if (dynamicBuf.empty()) {
      return std::unexpected(GRIDLOCK_ERR_BUFFER_TOO_SMALL);
    }
    std::size_t retryLen = 0u;
    status = host->prompt_input(
        host->host_ctx, titleNullTerm.c_str(), labelNullTerm.c_str(), defaultNullTerm.c_str(),
        dynamicBuf.data(), dynamicBuf.size(), &retryLen);
    if (status != GRIDLOCK_OK) {
      return std::unexpected(status);
    }
    const std::size_t clampedRetryLen =
        (retryLen < dynamicBuf.size()) ? retryLen : (dynamicBuf.size() - 1u);
    return std::string(dynamicBuf.data(), clampedRetryLen);
  } catch (...) {
    return std::unexpected(GRIDLOCK_ERR_BUFFER_TOO_SMALL);
  }
}

/**
 * @brief Queries the active workstation `GridLockThemeInfo` token palette.
 * @param host Non-null host API table providing `get_theme_info`.
 * @return Populated `GridLockThemeInfo` on `GRIDLOCK_OK`, or negative `GridLockStatus` on error.
 */
[[nodiscard]] inline std::expected<GridLockThemeInfo, GridLockStatus> getThemeInfo(
    const GridLockHostApi* host) noexcept {
  if (host == nullptr || host->get_theme_info == nullptr) {
    return std::unexpected(GRIDLOCK_ERR_INVALID_ARG);
  }
  GridLockThemeInfo info{};
  info.struct_size = static_cast<uint32_t>(sizeof(GridLockThemeInfo));
  info.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
  info.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
  info.reserved_flags = 0u;
  const GridLockStatus status = host->get_theme_info(host->host_ctx, &info);
  if (status != GRIDLOCK_OK) {
    return std::unexpected(status);
  }
  return info;
}

/**
 * @brief Queries DWARF type metadata (`byte_size`, `is_pointer`, `is_contiguous_container`, `type_name_utf8`) on `rank`.
 * @param host Non-null host API table providing `query_type_info`.
 * @param rank Zero-based stopped MPI rank index to inspect.
 * @param expr C/C++ debugger expression view (need not be NUL-terminated by caller).
 * @return Populated `GridLockTypeInfo` on `GRIDLOCK_OK`, or negative `GridLockStatus` on error.
 */
[[nodiscard]] inline std::expected<GridLockTypeInfo, GridLockStatus> queryTypeInfo(
    const GridLockHostApi* host,
    int32_t rank,
    std::string_view expr) noexcept {
  if (host == nullptr || host->query_type_info == nullptr || expr.empty()) {
    return std::unexpected(GRIDLOCK_ERR_INVALID_ARG);
  }
  try {
    const std::string exprNullTerm(expr);
    GridLockTypeInfo info{};
    info.struct_size = static_cast<uint32_t>(sizeof(GridLockTypeInfo));
    info.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
    info.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
    info.reserved_flags = 0u;
    const GridLockStatus status = host->query_type_info(host->host_ctx, rank, exprNullTerm.c_str(), &info);
    if (status != GRIDLOCK_OK) {
      return std::unexpected(status);
    }
    return info;
  } catch (...) {
    return std::unexpected(GRIDLOCK_ERR_BUFFER_TOO_SMALL);
  }
}

/**
 * @brief Resolves contiguous container/array layout (`data_address`, `element_count`, `element_size`, types) on `rank`.
 * @param host Non-null host API table providing `resolve_contiguous_range`.
 * @param rank Zero-based stopped MPI rank index to inspect.
 * @param expr C/C++ contiguous container or fixed-array expression view.
 * @return Populated `GridLockContiguousRange` on `GRIDLOCK_OK`, or negative `GridLockStatus` on error.
 */
[[nodiscard]] inline std::expected<GridLockContiguousRange, GridLockStatus> resolveContiguousRange(
    const GridLockHostApi* host,
    int32_t rank,
    std::string_view expr) noexcept {
  if (host == nullptr || host->resolve_contiguous_range == nullptr || expr.empty()) {
    return std::unexpected(GRIDLOCK_ERR_INVALID_ARG);
  }
  try {
    const std::string exprNullTerm(expr);
    GridLockContiguousRange range{};
    range.struct_size = static_cast<uint32_t>(sizeof(GridLockContiguousRange));
    range.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
    range.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
    range.reserved_flags = 0u;
    const GridLockStatus status =
        host->resolve_contiguous_range(host->host_ctx, rank, exprNullTerm.c_str(), &range);
    if (status != GRIDLOCK_OK) {
      return std::unexpected(status);
    }
    return range;
  } catch (...) {
    return std::unexpected(GRIDLOCK_ERR_BUFFER_TOO_SMALL);
  }
}

/**
 * @brief Reads raw target memory from `rank` at `addr` into caller-supplied `out` span.
 * @param host Non-null host API table providing `read_memory`.
 * @param rank Zero-based MPI rank index to read from.
 * @param addr Virtual address in the target rank's address space.
 * @param out Destination byte span to populate.
 * @return Number of bytes read on `GRIDLOCK_OK`, or `GridLockStatus` on error.
 */
[[nodiscard]] inline std::expected<std::size_t, GridLockStatus> readBytes(
    const GridLockHostApi* host,
    int32_t rank,
    uint64_t addr,
    std::span<std::byte> out) noexcept {
  if (host == nullptr || host->read_memory == nullptr) {
    return std::unexpected(GRIDLOCK_ERR_INVALID_ARG);
  }
  if (out.empty()) {
    return std::size_t{0u};
  }
  std::size_t bytesRead = 0u;
  const GridLockStatus status = host->read_memory(
      host->host_ctx, rank, addr, out.data(), out.size(), &bytesRead);
  if (status != GRIDLOCK_OK) {
    return std::unexpected(status);
  }
  return bytesRead;
}

/**
 * @brief Reads a trivially copyable non-bool, non-pointer POD value `T` from `rank` at `addr`.
 *
 * Rejects partial reads (`bytesRead != sizeof(T)`) with `GRIDLOCK_ERR_MEMORY_READ_FAILED`
 * and reconstructs `T` via `std::bit_cast` without strict-aliasing undefined behavior.
 * Disallows `bool` and pointer types so trap bit patterns or target addresses are never
 * materialized as host `bool` or host pointers.
 *
 * @tparam T Non-const, non-bool, non-pointer trivially copyable type with `sizeof(T) > 0`.
 * @param host Non-null host API table providing `read_memory`.
 * @param rank Zero-based MPI rank index to read from.
 * @param addr Virtual address of `T` in the target rank's address space.
 * @return Decoded value `T` on full read, or `GridLockStatus` on error.
 */
template <typename T>
  requires(std::is_trivially_copyable_v<T> && !std::is_const_v<T> &&
           !std::is_same_v<std::remove_cv_t<T>, bool> && !std::is_pointer_v<T> &&
           !std::is_member_pointer_v<T> && sizeof(T) > 0u)
[[nodiscard]] inline std::expected<T, GridLockStatus> readPod(
    const GridLockHostApi* host,
    int32_t rank,
    uint64_t addr) noexcept {
  std::array<std::byte, sizeof(T)> raw{};
  const auto bytesRes = readBytes(host, rank, addr, std::span<std::byte>(raw));
  if (!bytesRes.has_value()) {
    return std::unexpected(bytesRes.error());
  }
  if (*bytesRes != sizeof(T)) {
    return std::unexpected(GRIDLOCK_ERR_MEMORY_READ_FAILED);
  }
  return std::bit_cast<T>(raw);
}

}  // namespace gridlock::plugin

#endif  // GRIDLOCK_PLUGIN_CPP_HELPERS_HPP
