/**
 * @file MockHpcBackend.cpp
 * @brief Implementation of simulated Spack and SLURM operations for `MockHpcBackend`.
 */

#include "MockHpcBackend.hpp"

#include <QTimer>

namespace gridlock::core {

// Default list of realistic HPC Spack specs returned when no test override is set.
static const QStringList kDefaultFakePackages = {
    "openmpi@4.1.5%gcc@12.3.0",
    "hdf5@1.14.0%gcc@12.3.0+mpi",
    "fftw@3.3.10%gcc@12.3.0+mpi+openmp",
    "petsc@3.19.3%gcc@12.3.0+mpi+hypre",
    "hypre@2.28.0%gcc@12.3.0+mpi",
    "boost@1.82.0%gcc@12.3.0+mpi",
    "cmake@3.26.4%gcc@12.3.0",
    "python@3.11.3%gcc@12.3.0",
    "py-numpy@1.25.0%gcc@12.3.0",
    "py-scipy@1.11.1%gcc@12.3.0",
    "gdb@13.1%gcc@12.3.0",
    "valgrind@3.21.0%gcc@12.3.0+mpi",
};

/**
 * @brief Initializes `MockHpcBackend` with the default 12-package HPC Spack catalog.
 */
MockHpcBackend::MockHpcBackend(QObject* parent)
    : HpcBackend(parent), m_fakePackages(kDefaultFakePackages) {}

/**
 * @brief Replaces the simulated Spack package list with @p packages.
 */
void MockHpcBackend::setFakePackages(const QStringList& packages) {
  m_fakePackages = packages;
}

/**
 * @brief Configures whether the next `submitSlurmJob()` call simulates an `sbatch` failure.
 */
void MockHpcBackend::setNextSubmitFails(bool fail) {
  m_nextSubmitFails = fail;
}

/**
 * @brief Simulates `spack find` with a 200 ms network delay before emitting `packagesReady()`.
 */
void MockHpcBackend::fetchPackages() {
  // Simulate ~200 ms SSH round-trip latency before delivering the package list.
  QTimer::singleShot(200, this, [this]() {
    HpcResult r;
    r.success = true;
    r.output = m_fakePackages.join('\n') +
               "\n\n[MOCK] 12 packages in environment / +dependencies";
    emit packagesReady(r);
  });
}

/**
 * @brief Simulates `sbatch` submission, parsing `#SBATCH --job-name` from @p script.
 */
void MockHpcBackend::submitSlurmJob(const QString& script) {
  const bool fail = m_nextSubmitFails;
  // Reset one-shot fault injection flag so subsequent submissions succeed normally.
  m_nextSubmitFails = false;

  QTimer::singleShot(300, this, [this, fail, script]() {
    HpcResult r;
    if (fail) {
      r.success = false;
      r.errorOutput =
          "[MOCK] sbatch: error: Unable to connect to SLURM daemon\n";
    } else {
      // Extract `#SBATCH --job-name=<name>` if present; otherwise default to `gridlock_job`.
      QString jobName = "gridlock_job";
      const QStringList lines = script.split('\n');
      for (const QString& ln : lines) {
        if (ln.startsWith("#SBATCH --job-name")) {
          jobName = ln.section('=', 1).trimmed();
          break;
        }
      }
      static int s_jobCounter = 10000;
      r.success = true;
      r.output = QString("[MOCK] Submitted batch job %1  (name: %2)\n")
                     .arg(++s_jobCounter)
                     .arg(jobName);
    }
    emit slurmJobSubmitted(r);
  });
}

/**
 * @brief Simulates `scancel <jobId>` with a 150 ms network delay.
 */
void MockHpcBackend::cancelSlurmJob(int jobId) {
  QTimer::singleShot(150, this, [this, jobId]() {
    HpcResult r;
    r.success = true;
    r.output = QString("[MOCK] scancel %1: job cancelled\n").arg(jobId);
    emit commandFinished("scancel", r);
  });
}

}  // namespace gridlock::core
