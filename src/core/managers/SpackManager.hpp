/**
 * @file SpackManager.hpp
 * @brief HPC Console widget for querying remote Spack packages and SLURM batch jobs.
 *
 * Wraps an injected HpcBackend (either SSH-backed or MockHpcBackend) to browse
 * and filter installed Spack modules (`spack find`), log `sbatch` submission
 * results, and surface remote cluster diagnostics in the bottom dock tab.
 */

#pragma once

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "../hpc/HpcBackend.hpp"

namespace gridlock::ui {

/**
 * @brief Interactive "HPC Console" bottom-tab widget for Spack and SLURM telemetry.
 */
class SpackManager : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the SpackManager console bound to the given HpcBackend.
     * @param backend Non-owning pointer to the active HpcBackend (caller manages lifetime).
     * @param parent Optional parent widget.
     */
    explicit SpackManager(gridlock::core::HpcBackend* backend,
                          QWidget* parent = nullptr);

    /**
     * @brief Replaces the active HpcBackend at runtime and reconnects completion signals.
     * @param backend New HpcBackend instance (e.g., switching from mock to SSH).
     */
    void setBackend(gridlock::core::HpcBackend* backend);

    /**
     * @brief Appends a formatted status or error message line to the HPC console log.
     * @param msg Message text to append.
     * @param isError True to prefix the line with `[ERR]`; false for `[HPC]`.
     */
    void appendMessage(const QString& msg, bool isError = false);

public slots:
    /** @brief Triggers an asynchronous `spack find` query on the active HpcBackend. */
    void onFetchPackages();

signals:
    /** @brief Emitted when a SLURM job submission completes successfully on the backend. */
    void jobSubmitted(const QString& output);

private slots:
    /** @brief Handles `spack find` completion and refreshes the filtered console listing. */
    void onPackagesReady(const gridlock::core::HpcResult& result);

    /** @brief Handles SLURM `sbatch` completion and logs output or error text. */
    void onJobSubmitted(const gridlock::core::HpcResult& result);

    /** @brief Re-filters the cached package lines when the search input text changes. */
    void onFilterChanged(const QString& text);

    /** @brief Clears the console log and cached package list, resetting status to "Idle". */
    void onClearClicked();

private:
    /** @brief Filters m_packageLines against m_filterEdit without a remote round-trip. */
    void applyFilter();

    /** @brief Connects packagesReady and slurmJobSubmitted signals from m_backend. */
    void reconnectBackend();

    gridlock::core::HpcBackend* m_backend = nullptr;

    // Search and action toolbar controls.
    QLineEdit* m_filterEdit = nullptr;
    QPushButton* m_fetchButton = nullptr;
    QPushButton* m_clearButton = nullptr;
    QLabel* m_statusLabel = nullptr;

    // Read-only monospace output log.
    QPlainTextEdit* m_console = nullptr;

    // Cached lines from the most recent `spack find` invocation for instant local filtering.
    QStringList m_packageLines;
};

} // namespace gridlock::ui
