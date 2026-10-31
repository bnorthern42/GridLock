/**
 * @file HpcConsoleWidget.hpp
 * @brief Multi-distro HPC and System Package Manager workstation widget.
 *
 * Extends SpackManager into a full-featured Package Manager workstation supporting
 * automatic backend discovery (`spack`, `pacman`, `dnf`, `apt`, `zypper`, `rpm`,
 * `dpkg-query`, `eb`), structured CLI output parsing into `PackageRecord` rows,
 * and three switchable display views (`Table View`, `List View`, and `Raw View`).
 */

#pragma once

#include "../../core/managers/SpackManager.hpp"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVector>

namespace gridlock::ui {

/**
 * @struct PackageRecord
 * @brief Structured metadata for an installed or available software package.
 */
struct PackageRecord {
    QString name;         ///< Package identifier (e.g., "openmpi", "hypre", "gdb").
    QString version;      ///< Version and release string (e.g., "5.0.5-1").
    QString arch;         ///< Target CPU architecture (e.g., "x86_64", "noarch").
    QString repository;   ///< Source repository or package manager (e.g., "extra", "fedora", "spack").
    QString status;       ///< Installation status ("Installed" or "Available").
    QString description;  ///< One-line package summary or description.
};

/**
 * @class HpcConsoleWidget
 * @brief Multi-distro HPC and Linux System Package Manager search and inspection widget.
 *
 * Automatically detects available package managers on the host system when Spack
 * is absent, parses native CLI outputs from `pacman`, `dnf`, `apt`, `zypper`, `rpm`,
 * `dpkg`, and `spack`, and renders results across switchable Table, List, and Raw views.
 */
class HpcConsoleWidget : public SpackManager {
public:
    /**
     * @brief Constructs the multi-backend Package Manager workstation widget.
     * @param backend Optional non-owning pointer to the active HpcBackend.
     * @param parent Optional parent widget.
     */
    explicit HpcConsoleWidget(gridlock::core::HpcBackend* backend = nullptr,
                              QWidget* parent = nullptr);
    ~HpcConsoleWidget() override;

    /**
     * @brief Detects all installed HPC and system package managers in priority order.
     * @return Ordered list of available backend identifiers (e.g., "pacman", "dnf", "apt").
     */
    static QStringList detectAvailablePackageManagers();

    /**
     * @brief Returns the currently active resolved package manager backend identifier.
     * @return Active backend string (e.g., "pacman", "dnf", "apt", "zypper", "rpm", "spack").
     */
    QString activePackageManagerBackend() const;

    /**
     * @brief Selects a specific package manager backend (or "Auto") and refreshes results.
     * @param backend Backend identifier string.
     */
    void setPackageManagerBackend(const QString& backend);

    /**
     * @brief Parses native CLI output from a supported package manager into PackageRecord items.
     * @param backend Package manager identifier ("pacman", "dnf", "apt", "zypper", "rpm", "spack").
     * @param rawOutput Raw stdout string captured from the package manager CLI.
     * @return Vector of parsed PackageRecord items.
     */
    QVector<PackageRecord> parsePackageManagerOutput(const QString& backend,
                                                     const QString& rawOutput) const;

    /**
     * @brief Populates the Table, List, and Raw views from a list of PackageRecord items.
     * @param records Structured package records to display.
     * @param rawOutput Raw CLI output string for the Raw View tab.
     */
    void displayPackageRecords(const QVector<PackageRecord>& records,
                               const QString& rawOutput = QString());

    /**
     * @brief Switches the active presentation mode between "Table", "List", and "Raw".
     * @param mode Target view mode ("Table", "List", or "Raw").
     */
    void setViewMode(const QString& mode);

    /**
     * @brief Returns the active presentation mode identifier ("Table", "List", or "Raw").
     * @return Current view mode string.
     */
    QString currentViewMode() const { return m_currentViewMode; }

    /**
     * @brief Returns the number of visible rows in the structured package table view.
     * @return Row count of m_packageTable.
     */
    int packageTableRowCount() const;

    /**
     * @brief Returns the current visible package count displayed in the table and count badge.
     * @return Number of currently visible package rows.
     */
    int visiblePackageCount() const;

    /**
     * @brief Queries the local installed package database (`rpm -qa`, `dpkg-query -W`, `pacman -Qs`).
     * @param backend Active package manager backend identifier.
     * @param filterQuery Case-insensitive substring filter applied across name and summary.
     * @param outRawLog Optional output string receiving the raw CLI command and matched lines.
     * @return Vector of installed PackageRecord items from the host operating system.
     */
    QVector<PackageRecord> queryLocalInstalledPackages(const QString& backend,
                                                       const QString& filterQuery,
                                                       QString* outRawLog = nullptr) const;

    /**
     * @brief Synchronously queries local installed and remote repository packages for query.
     * @param query Case-insensitive search query string (e.g., "fd", "mpi").
     * @param timeoutMs Maximum wait time in milliseconds for the repository CLI subprocess.
     * @return Total number of visible package rows populated into the table.
     */
    int searchPackagesSynchronously(const QString& query, int timeoutMs = 3000);

    /**
     * @brief Executes an asynchronous two-stage package query against the active host backend.
     * @param query Optional search query substring (defaults to current search bar text).
     */
    void executePackageSearch(const QString& query = QString());

    /**
     * @brief Applies modern dark/light themed QSS to package filter combo boxes.
     */
    Q_INVOKABLE void reloadStyle();

private:
    /**
     * @brief Re-renders m_packageTable and m_listBrowser applying the active status filter.
     */
    void refreshFilteredViews();

    /**
     * @brief Merges local installed records with repository records, deduplicating by name + arch.
     * @param installed Local installed package records.
     * @param repoRecords Remote repository search records.
     * @return Deduplicated merged vector preserving Installed status and version metadata.
     */
    static QVector<PackageRecord> mergePackageRecords(const QVector<PackageRecord>& installed,
                                                      const QVector<PackageRecord>& repoRecords);

    QComboBox* m_backendCombo = nullptr;
    QComboBox* m_statusFilterCombo = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_tableModeBtn = nullptr;
    QPushButton* m_listModeBtn = nullptr;
    QPushButton* m_rawModeBtn = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QLabel* m_countLabel = nullptr;

    QStackedWidget* m_viewStack = nullptr;
    QTableWidget* m_packageTable = nullptr;
    QTextBrowser* m_listBrowser = nullptr;
    QPlainTextEdit* m_outputArea = nullptr;
    QTimer* m_searchDebounceTimer = nullptr;
    QProcess* m_searchProcess = nullptr;

    QString m_detectedBackend;
    QString m_currentViewMode = QStringLiteral("Table");
    QVector<PackageRecord> m_allRecords;
    QString m_lastRawOutput;
    int m_visibleCount = 0;
};

}  // namespace gridlock::ui
