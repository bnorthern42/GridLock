/**
 * @file MockHpcBackend.hpp
 * @brief Deterministic in-memory `HpcBackend` implementation for offline development and testing.
 *
 * Emits simulated Spack and SLURM responses asynchronously via `QTimer::singleShot`
 * so UI callers exercise realistic non-blocking signal delivery without live SSH.
 */

#pragma once

#include <QStringList>

#include "HpcBackend.hpp"

namespace gridlock::core {

/**
 * @class MockHpcBackend
 * @brief Simulates `spack find`, `sbatch`, and `scancel` cluster operations locally.
 */
class MockHpcBackend : public HpcBackend {
  Q_OBJECT

 public:
  /**
   * @brief Constructs the mock backend pre-populated with a realistic HPC Spack environment.
   */
  explicit MockHpcBackend(QObject* parent = nullptr);

  /** @brief Asynchronously emits `packagesReady()` with `m_fakePackages`. */
  void fetchPackages() override;

  /** @brief Asynchronously emits `slurmJobSubmitted()` with a synthetic SLURM job ID. */
  void submitSlurmJob(const QString& script) override;

  /** @brief Asynchronously emits `commandFinished("scancel", ...)` for @p jobId. */
  void cancelSlurmJob(int jobId) override;

  /** @brief Overrides the list of Spack specs returned by `fetchPackages()`. */
  void setFakePackages(const QStringList& packages);

  /** @brief Forces the next `submitSlurmJob()` call to simulate a SLURM daemon connection error. */
  void setNextSubmitFails(bool fail);

 private:
  QStringList m_fakePackages;      ///< Simulated Spack package specs.
  bool m_nextSubmitFails = false;  ///< One-shot fault injection flag for `submitSlurmJob()`.
};

}  // namespace gridlock::core
