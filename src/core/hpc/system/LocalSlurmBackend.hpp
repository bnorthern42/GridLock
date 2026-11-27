/**
 * @file LocalSlurmBackend.hpp
 * @brief Real Slurm job submission, queue polling, node/port discovery, and gdbserver attach backend.
 */

#pragma once

#include "../HpcBackend.hpp"
#include "ProcessScanner.hpp"

#include <QList>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <memory>

namespace gridlock::core {

/**
 * @class LocalSlurmBackend
 * @brief Concrete HpcBackend orchestrating sbatch submission, squeue polling, scontrol node expansion, and scancel cleanup.
 */
class LocalSlurmBackend : public HpcBackend {
  Q_OBJECT

 public:
  explicit LocalSlurmBackend(QObject* parent = nullptr);
  ~LocalSlurmBackend() override;

  void fetchPackages() override;
  void submitSlurmJob(const QString& script) override;
  void cancelSlurmJob(int jobId) override;

  /**
   * @brief Stages a per-rank `gdbserver --multi` wrapper in a shared directory, submits via `sbatch --parsable`, and polls until RUNNING.
   */
  void submitDebugJob(const QString& scriptTemplate,
                      const QString& targetBinary,
                      const QString& targetArgs = QString(),
                      const QString& workingDir = QString());

  /** @brief Cancels the currently active or queued Slurm job (`scancel`) and tears down any SSH forward tunnel. */
  void cancelActiveJob();

  /** @brief Configures the `squeue` poll interval in milliseconds (default `1000`, clamped `>= 10`). */
  void setPollIntervalMs(int intervalMs);
  /** @brief Configures the starting TCP port (`base_port + rank`, default `2345`) for per-rank `gdbserver` listeners. */
  void setBasePort(int basePort);
  /** @brief Overrides the shared staging directory where wrapper and `.sbatch` scripts are written. */
  void setStagingDirectory(const QString& dirPath);
  /** @brief Stages the target binary path used if `submitSlurmJob()` is invoked directly through `HpcBackend`. */
  void setTargetBinary(const QString& binaryPath) { m_stagedTargetBinary = binaryPath; }

  int activeJobId() const { return m_activeJobId; }
  bool isJobActive() const { return m_activeJobId > 0; }
  int basePort() const { return m_basePort; }
  QString lastStagedScriptPath() const { return m_lastStagedScriptPath; }
  QString lastWrapperScriptPath() const { return m_lastWrapperScriptPath; }
  QStringList resolvedComputeEndpoints() const { return m_resolvedComputeEndpoints; }

  /** @brief Resolves a Slurm CLI executable from explicit path, sibling tool directory, or `PATH`. */
  static QString resolveSlurmExecutable(const QString& configuredPath,
                                        const QString& defaultName,
                                        const QString& siblingDirHint = QString());

  /** @brief Computes total MPI rank count from `#SBATCH` directives (`--nodes`, `--ntasks`, `--ntasks-per-node`) or fallback settings. */
  static int inferTotalRanksFromScript(const QString& scriptText, int fallbackNodes, int fallbackTasksPerNode);

  /** @brief Maps expanded compute hostnames (`scontrol show hostnames`) to per-rank `host:(basePort + rank)` endpoints. */
  static QStringList buildComputeRankEndpoints(const QStringList& hostnames, int totalRanks, int basePort);

 signals:
  /** @brief Emitted for live progress/error lines intended for the HPC console and status bar. */
  void statusMessage(const QString& message, bool isError);
  /** @brief Emitted when `squeue` reports a Slurm state transition (`SUBMITTED`, `PENDING`, `CONFIGURING`, `RUNNING`, etc.). */
  void jobStateChanged(int jobId, const QString& slurmState);
  /** @brief Emitted once the job reaches `RUNNING` and per-rank `gdbserver` endpoints (plus optional SSH tunnels) are ready. */
  void attachTargetsReady(int jobId,
                          const QList<gridlock::RemoteTargetSpec>& targets,
                          const QStringList& computeEndpoints);
  /** @brief Emitted if `sbatch`, `squeue`, `scontrol`, or SSH port forwarding fails before attach. */
  void jobFailed(int jobId, const QString& errorReason);

 private:
  void pollActiveJobState();
  void resolveRunningJobNodes(int jobId);
  void runScontrolShowHostnames(int jobId, const QString& nodelistExpr);
  void finalizeAttachTargets(int jobId, const QStringList& hostnames);
  void stopSshTunnel();
  QString resolveSharedStagingDirectory(const QString& workingDir, const QString& targetBinary) const;
  QString buildStagedBatchScript(const QString& rawScript,
                                 const QString& wrapperPath,
                                 const QString& targetBinary,
                                 const QString& targetArgs,
                                 const QString& stagingDir) const;

  QTimer* m_pollTimer = nullptr;
  int m_pollIntervalMs = 1000;
  int m_basePort = 2345;
  int m_activeJobId = -1;
  int m_expectedRanks = 1;
  int m_emptyPollRetries = 0;
  bool m_pollInFlight = false;
  QString m_lastReportedState;
  QString m_stagingDirOverride;
  QString m_activeStagingDir;
  QString m_stagedTargetBinary;
  QString m_activeTargetBinary;
  QString m_activeSiblingBinDir;
  QString m_lastStagedScriptPath;
  QString m_lastWrapperScriptPath;
  QStringList m_resolvedComputeEndpoints;
  std::unique_ptr<QProcess> m_sshTunnelProcess;
};

}  // namespace gridlock::core
