/**
 * @file PluginVerifier.hpp
 * @brief Sandboxed post-build and CLI conformance verifier for GridLock C99 ABI v1.x plugins.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <QString>
#include <QStringList>

namespace gridlock::core::plugins {

/**
 * @struct PluginVerifyReport
 * @brief Structured diagnostic summary returned by `PluginVerifier::verifyBinary`.
 */
struct PluginVerifyReport {
  bool passed = false;               ///< True when zero conformance errors were encountered.
  QString libraryPath;               ///< Canonical or absolute path to the inspected plugin binary.
  QString pluginId;                  ///< Descriptor `plugin_id_utf8` if valid.
  QString pluginName;                ///< Descriptor `plugin_name_utf8` if valid.
  QString pluginVersion;             ///< Descriptor `plugin_version_utf8` if valid.
  uint32_t abiMajor = 0u;            ///< Descriptor `abi_major`.
  uint32_t abiMinor = 0u;            ///< Descriptor `abi_minor`.
  int32_t outstandingAllocCount = 0; ///< Unfreed `host->alloc_bytes` block count after `shutdown()`.
  size_t outstandingBytes = 0u;      ///< Unfreed `host->alloc_bytes` byte total after `shutdown()`.
  int registeredActionCount = 0;     ///< Valid staged actions committed during `gridlock_plugin_init_v1`.
  QStringList warnings;              ///< Non-fatal symbol visibility / `STB_GNU_UNIQUE` warnings.
  QStringList errors;                ///< Fatal ABI, missing-symbol, registration, lifecycle, or leak errors.

  /** @brief Returns the number of unfreed bytes still live on the host heap after `shutdown()`. */
  size_t outstandingAllocBytes() const noexcept { return outstandingBytes; }

  /** @brief Formats a human-readable pass/warning/error report suitable for CLI output. */
  QString formatSummary() const;
};

/**
 * @class PluginVerifier
 * @brief Runs isolated symbol-hygiene, ABI-header, regex/menu, lifecycle, and leak checks on a plugin `.so`.
 */
class PluginVerifier {
 public:
  /**
   * @brief Verifies @p soPath inside an isolated `PluginManager` + `PluginHostBridge` sandbox.
   * @param soPath Path to the compiled `.so` / `.dylib` / `.dll` shared library.
   * @param outReport Optional structured report receiving metadata, warnings, and errors.
   * @return True when @p soPath passed all conformance checks (`errors.isEmpty()`).
   */
  static bool verifyBinary(const QString& soPath, PluginVerifyReport* outReport = nullptr);
};

}  // namespace gridlock::core::plugins
