#include "ProjectSettingsDialog.hpp"
#include "../../core/managers/ConfigManager.hpp"
#include <QFormLayout>
#include <QDialogButtonBox>

namespace gridlock::ui {

ProjectSettingsDialog::ProjectSettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Project Settings");
    resize(400, 150);
    
    auto* form = new QFormLayout(this);
    m_targetBinary = new QLineEdit(this);
    m_binaryArguments = new QLineEdit(this);
    m_workingDirectory = new QLineEdit(this);
    
    form->addRow("Target Binary:", m_targetBinary);
    form->addRow("Binary Arguments:", m_binaryArguments);
    form->addRow("Working Directory:", m_workingDirectory);
    
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
    
    auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
    m_targetBinary->setText(QString::fromStdString(ps.targetBinary));
    m_binaryArguments->setText(QString::fromStdString(ps.programArguments));
    m_workingDirectory->setText(QString::fromStdString(ps.workingDirectory));
}

void ProjectSettingsDialog::accept() {
    auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
    ps.targetBinary = m_targetBinary->text().toStdString();
    ps.programArguments = m_binaryArguments->text().toStdString();
    ps.workingDirectory = m_workingDirectory->text().toStdString();
    gridlock::core::ConfigManager::instance().saveProjectSettings(ps);
    QDialog::accept();
}

} // namespace gridlock::ui
