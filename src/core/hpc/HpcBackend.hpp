#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

namespace gridlock::core {

/// Result returned by any HpcBackend async operation.
struct HpcResult {
    bool    success  = false;
    QString output;          ///< stdout from the remote command
    QString errorOutput;     ///< stderr or error message
};

///
/// HpcBackend — pure-virtual interface for remote HPC operations.
///
/// In production a SshHpcBackend implements this via QProcess + ssh(1).
/// In tests (and CI) a MockHpcBackend returns pre-canned responses, making
/// the feature set fully testable without a live cluster.
///
class HpcBackend : public QObject {
    Q_OBJECT
public:
    explicit HpcBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~HpcBackend() override = default;

    /// Run `spack find` on the remote host and emit packagesReady().
    virtual void fetchPackages() = 0;

    /// Submit a SLURM job script over SSH and emit slurmJobSubmitted().
    /// @param script  The rendered sbatch script content.
    virtual void submitSlurmJob(const QString &script) = 0;

    /// Cancel a running SLURM job by ID.
    virtual void cancelSlurmJob(int jobId) = 0;

signals:
    /// Emitted when `spack find` output is ready (success or error).
    void packagesReady(const HpcResult &result);

    /// Emitted after an sbatch submission attempt.
    void slurmJobSubmitted(const HpcResult &result);

    /// Emitted when any generic remote command completes.
    void commandFinished(const QString &tag, const HpcResult &result);
};

} // namespace gridlock::core
