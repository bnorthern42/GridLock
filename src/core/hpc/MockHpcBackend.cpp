#include "MockHpcBackend.hpp"
#include <QTimer>

namespace gridlock::core {

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

MockHpcBackend::MockHpcBackend(QObject *parent)
    : HpcBackend(parent)
    , m_fakePackages(kDefaultFakePackages)
{}

void MockHpcBackend::setFakePackages(const QStringList &packages)
{
    m_fakePackages = packages;
}

void MockHpcBackend::setNextSubmitFails(bool fail)
{
    m_nextSubmitFails = fail;
}

void MockHpcBackend::fetchPackages()
{
    // Simulate ~200 ms network latency then emit.
    QTimer::singleShot(200, this, [this]() {
        HpcResult r;
        r.success = true;
        r.output  = m_fakePackages.join('\n') +
                    "\n\n[MOCK] 12 packages in environment / +dependencies";
        emit packagesReady(r);
    });
}

void MockHpcBackend::submitSlurmJob(const QString &script)
{
    const bool fail = m_nextSubmitFails;
    m_nextSubmitFails = false;   // reset so next call succeeds

    QTimer::singleShot(300, this, [this, fail, script]() {
        HpcResult r;
        if (fail) {
            r.success     = false;
            r.errorOutput = "[MOCK] sbatch: error: Unable to connect to SLURM daemon\n";
        } else {
            // Extract #SBATCH --job-name if present, else use "gridlock_job"
            QString jobName = "gridlock_job";
            const QStringList lines = script.split('\n');
            for (const QString &ln : lines) {
                if (ln.startsWith("#SBATCH --job-name")) {
                    jobName = ln.section('=', 1).trimmed();
                    break;
                }
            }
            static int s_jobCounter = 10000;
            r.success = true;
            r.output  = QString("[MOCK] Submitted batch job %1  (name: %2)\n")
                            .arg(++s_jobCounter).arg(jobName);
        }
        emit slurmJobSubmitted(r);
    });
}

void MockHpcBackend::cancelSlurmJob(int jobId)
{
    QTimer::singleShot(150, this, [this, jobId]() {
        HpcResult r;
        r.success = true;
        r.output  = QString("[MOCK] scancel %1: job cancelled\n").arg(jobId);
        emit commandFinished("scancel", r);
    });
}

} // namespace gridlock::core
