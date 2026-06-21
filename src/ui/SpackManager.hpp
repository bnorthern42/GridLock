#pragma once
#include "../core/HpcBackend.hpp"
#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace gridlock::ui {

///
/// SpackManager — "HPC Console" bottom-tab widget.
///
/// Wraps an HpcBackend (real SSH or mock) to:
///   • Browse and search the remote Spack package environment
///   • Display sbatch submission output
///   • Show all HPC backend messages in a scrollable console
///
/// The backend is injected so tests can pass a MockHpcBackend.
///
class SpackManager : public QWidget {
    Q_OBJECT
public:
    /// @param backend  Ownership is NOT transferred; caller manages lifetime.
    explicit SpackManager(gridlock::core::HpcBackend *backend,
                          QWidget *parent = nullptr);

    /// Replace the active backend (e.g. switch from mock to SSH at runtime).
    void setBackend(gridlock::core::HpcBackend *backend);

    /// Log an arbitrary message into the HPC console (e.g. from sbatch).
    void appendMessage(const QString &msg, bool isError = false);

public slots:
    void onFetchPackages();

signals:
    /// Forwarded from the backend after a job is submitted.
    void jobSubmitted(const QString &output);

private slots:
    void onPackagesReady(const gridlock::core::HpcResult &result);
    void onJobSubmitted(const gridlock::core::HpcResult &result);
    void onFilterChanged(const QString &text);
    void onClearClicked();

private:
    void applyFilter();
    void reconnectBackend();

    gridlock::core::HpcBackend *m_backend = nullptr;

    // ── Search / toolbar row ──────────────────────────────────────────────
    QLineEdit   *m_filterEdit   = nullptr;
    QPushButton *m_fetchButton  = nullptr;
    QPushButton *m_clearButton  = nullptr;
    QLabel      *m_statusLabel  = nullptr;

    // ── Main console output ───────────────────────────────────────────────
    QPlainTextEdit *m_console   = nullptr;

    // Raw package lines from the last `spack find` (for local filter)
    QStringList m_packageLines;
};

} // namespace gridlock::ui
