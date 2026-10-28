/**
 * @file ProjectSettingsDialog.cpp
 * @brief Implementation of project configuration editor dialog.
 *
 * Implements form layout construction, data population from ConfigManager,
 * and persistence of target binary settings upon dialog acceptance.
 */

#include "ProjectSettingsDialog.hpp"
#include "../../core/managers/ConfigManager.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>

namespace gridlock::ui {

/**
 * @brief Constructs and presents the project settings configuration dialog.
 * @param parent Optional parent widget.
 */
ProjectSettingsDialog::ProjectSettingsDialog(QWidget* parent) : QDialog(parent) {
    // Configure window title and default modal dialog geometry.
    setWindowTitle("Project Settings");
    resize(400, 150);

    // Arrange fields using standard two-column form layout.
    auto* form = new QFormLayout(this);
    m_targetBinary = new QLineEdit(this);
    m_binaryArguments = new QLineEdit(this);
    m_workingDirectory = new QLineEdit(this);

    // Mount labelled form rows for each configurable parameter.
    form->addRow("Target Binary:", m_targetBinary);
    form->addRow("Binary Arguments:", m_binaryArguments);
    form->addRow("Working Directory:", m_workingDirectory);

    // Construct Ok/Cancel dialog button strip.
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);

    // Pre-populate fields from active ConfigManager project settings state.
    auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
    m_targetBinary->setText(QString::fromStdString(ps.targetBinary));
    m_binaryArguments->setText(QString::fromStdString(ps.programArguments));
    m_workingDirectory->setText(QString::fromStdString(ps.workingDirectory));
}

/**
 * @brief Serializes form inputs back to ConfigManager on user acceptance.
 */
void ProjectSettingsDialog::accept() {
    // Read current project configuration snapshot.
    auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();

    // Update settings model with edited values from GUI fields.
    ps.targetBinary = m_targetBinary->text().toStdString();
    ps.programArguments = m_binaryArguments->text().toStdString();
    ps.workingDirectory = m_workingDirectory->text().toStdString();

    // Commit changes to persistent storage and close dialog.
    gridlock::core::ConfigManager::instance().saveProjectSettings(ps);
    QDialog::accept();
}

}  // namespace gridlock::ui
