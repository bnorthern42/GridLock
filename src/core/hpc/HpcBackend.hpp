/**
 * @file HpcBackend.hpp
 * @brief Abstract interface for remote HPC cluster operations (Spack package discovery and SLURM job control).
 *
 * Decouples UI dialogs (`SpackEnvDialog`, `SlurmDialog`) from SSH transport details
 * so offline development and CI tests can substitute `MockHpcBackend`.
 */

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace gridlock::core {

/**
 * @struct HpcResult
 * @brief Standard result envelope returned by asynchronous `HpcBackend` operations.
 */
struct HpcResult {
  bool success = false; ///< `true` if the remote command exited with status 0.
  QString output;       ///< Captured standard output from the remote command.
  QString errorOutput;  ///< Captured standard error or transport diagnostic message.
};

/**
 * @class HpcBackend
 * @brief Pure-virtual interface for querying Spack environments and managing SLURM jobs.
 */
class HpcBackend : public QObject {
  Q_OBJECT

 public:
  /**
   * @brief Constructs the HPC backend with an optional parent QObject.
   */
  explicit HpcBackend(QObject* parent = nullptr) : QObject(parent) {}

  /**
   * @brief Virtual destructor for safe subclass cleanup.
   */
  ~HpcBackend() override = default;

  /**
   * @brief Queries `spack find` on the cluster and emits `packagesReady()`.
   */
  virtual void fetchPackages() = 0;

  /**
   * @brief Submits a rendered `sbatch` script and emits `slurmJobSubmitted()`.
   * @param script Complete SLURM batch script contents.
   */
  virtual void submitSlurmJob(const QString& script) = 0;

  /**
   * @brief Cancels a running SLURM batch job by numerical job ID via `scancel`.
   */
  virtual void cancelSlurmJob(int jobId) = 0;

 signals:
  /** @brief Emitted when `fetchPackages()` finishes. */
  void packagesReady(const HpcResult& result);

  /** @brief Emitted when `submitSlurmJob()` finishes. */
  void slurmJobSubmitted(const HpcResult& result);

  /** @brief Emitted when a generic remote command (such as `scancel`) finishes. */
  void commandFinished(const QString& tag, const HpcResult& result);
};

}  // namespace gridlock::core
