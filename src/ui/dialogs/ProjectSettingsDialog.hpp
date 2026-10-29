/**
 * @file ProjectSettingsDialog.hpp
 * @brief Modal configuration and workstation inspection dialogs for project settings, process attach, new project, and about info.
 *
 * Exposes structured dark-slate modal dialogs:
 * - ProjectSettingsDialog: Active project target binary, CLI arguments, and working directory.
 * - AttachProcessDialog: Live MPI rank / local process table selector with PID/command filtering.
 * - NewProjectDialog: Streamlined single-card MPI project creation dialog.
 * - AboutDialog: Workstation build, GDB/MI + ELF64 architecture, and telemetry card modal.
 */

#pragma once

#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QTableWidget>

namespace gridlock::ui {

/**
 * @class ProjectSettingsDialog
 * @brief Form dialog for managing active project execution parameters.
 */
class ProjectSettingsDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Constructs the project settings dialog and populates current values.
     * @param parent Optional parent widget.
     */
    explicit ProjectSettingsDialog(QWidget* parent = nullptr);

    /**
     * @brief Persists modified settings to ConfigManager upon user confirmation.
     */
    void accept() override;

private:
    QLineEdit* m_targetBinary{nullptr};       ///< Input field for target executable binary path.
    QLineEdit* m_binaryArguments{nullptr};    ///< Input field for program command-line arguments.
    QLineEdit* m_workingDirectory{nullptr};   ///< Input field for process working directory path.
};

/**
 * @class AttachProcessDialog
 * @brief Modal workstation dialog for inspecting and attaching to running MPI or local processes.
 *
 * Displays a 4-column borderless QTableWidget (PID, RANK, STATE, COMMAND) with alternating
 * row colors, 48px minimum section sizes, and live substring filtering.
 */
class AttachProcessDialog : public QDialog {
public:
    /**
     * @brief Constructs the attach process dialog with live filter bar and process table.
     * @param parent Optional parent widget.
     */
    explicit AttachProcessDialog(QWidget* parent = nullptr);

    /**
     * @brief Returns the currently selected process ID, or -1 if none is selected.
     * @return Selected process PID.
     */
    int selectedPid() const;

private:
    /**
     * @brief Filters visible process rows matching the query string.
     * @param filterText Substring matched against PID, rank, state, or command.
     */
    void filterProcesses(const QString& filterText);

    QLineEdit* m_filterEdit{nullptr};         ///< Substring filter input for process rows.
    QTableWidget* m_processTable{nullptr};    ///< 4-column table displaying active processes.
};

/**
 * @class NewProjectDialog
 * @brief Streamlined dark-slate modal dialog for creating a new MPI debugging project.
 */
class NewProjectDialog : public QDialog {
public:
    /**
     * @brief Constructs the new project configuration dialog.
     * @param parent Optional parent widget.
     */
    explicit NewProjectDialog(QWidget* parent = nullptr);

    /**
     * @brief Returns the configured project name.
     */
    QString projectName() const;

    /**
     * @brief Returns the configured workspace directory path.
     */
    QString workspaceDirectory() const;

    /**
     * @brief Returns the configured target executable binary path.
     */
    QString targetExecutable() const;

private:
    QLineEdit* m_projectNameEdit{nullptr};    ///< Input field for project identifier.
    QLineEdit* m_workspaceDirEdit{nullptr};   ///< Input field for root workspace path.
    QLineEdit* m_targetBinaryEdit{nullptr};   ///< Input field for MPI target binary.
    QComboBox* m_launcherCombo{nullptr};      ///< Dropdown selector for MPI process launcher.
};

/**
 * @class AboutDialog
 * @brief Modal workstation dialog displaying GridLock build metadata and subsystem telemetry capabilities.
 */
class AboutDialog : public QDialog {
public:
    /**
     * @brief Constructs the dark-slate About GridLock dialog.
     * @param parent Optional parent widget.
     */
    explicit AboutDialog(QWidget* parent = nullptr);
};

}  // namespace gridlock::ui
