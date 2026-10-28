/**
 * @file ProjectSettingsDialog.hpp
 * @brief Modal configuration dialog for active project binary, CLI arguments, and directory.
 *
 * Exposes a structured form interface allowing users to adjust target executable
 * paths, runtime command-line arguments, and process working directory locations
 * synchronized with ConfigManager.
 */

#pragma once

#include <QDialog>
#include <QLineEdit>

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

}  // namespace gridlock::ui
