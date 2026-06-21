#pragma once
#include "HpcBackend.hpp"
#include <QStringList>

namespace gridlock::core {

///
/// MockHpcBackend — deterministic fake responses for offline development.
///
/// Emits signals via QTimer::singleShot so callers see realistic async
/// behaviour (signal arrives after the call returns) without real SSH.
///
class MockHpcBackend : public HpcBackend {
    Q_OBJECT
public:
    explicit MockHpcBackend(QObject *parent = nullptr);

    void fetchPackages()                       override;
    void submitSlurmJob(const QString &script) override;
    void cancelSlurmJob(int jobId)             override;

    /// Inject a custom package list for test scenarios.
    void setFakePackages(const QStringList &packages);
    /// Force the next submitSlurmJob() to simulate a failure.
    void setNextSubmitFails(bool fail);

private:
    QStringList m_fakePackages;
    bool        m_nextSubmitFails = false;
};

} // namespace gridlock::core
