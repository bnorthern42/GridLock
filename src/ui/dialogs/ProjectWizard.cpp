#include "ProjectWizard.hpp"
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStringList>

namespace gridlock::ui::dialogs {

ProjectWizard::ProjectWizard(QWidget *parent) : QWizard(parent) {
  setWindowTitle("New Project Wizard");
  addPage(createWorkspacePage());
  addPage(createMpiPage());
}

QWizardPage *ProjectWizard::createWorkspacePage() {
  auto *page = new QWizardPage();
  page->setTitle("Workspace Settings");

  auto *layout = new QFormLayout(page);

  m_projectNameEdit = new QLineEdit();
  layout->addRow("Project Name:", m_projectNameEdit);

  m_workspaceDirEdit = new QLineEdit();
  auto *dirBtn = new QPushButton("Browse...");
  connect(dirBtn, &QPushButton::clicked, this, [this]() {
    QString dir =
        QFileDialog::getExistingDirectory(this, "Select Workspace Directory");
    if (!dir.isEmpty())
      m_workspaceDirEdit->setText(dir);
  });
  auto *dirLayout = new QHBoxLayout();
  dirLayout->addWidget(m_workspaceDirEdit);
  dirLayout->addWidget(dirBtn);
  layout->addRow("Workspace Directory:", dirLayout);

  m_targetExecEdit = new QLineEdit();
  auto *execBtn = new QPushButton("Browse...");
  connect(execBtn, &QPushButton::clicked, this, [this]() {
    QString file =
        QFileDialog::getOpenFileName(this, "Select Target Executable");
    if (!file.isEmpty())
      m_targetExecEdit->setText(file);
  });
  auto *execLayout = new QHBoxLayout();
  execLayout->addWidget(m_targetExecEdit);
  execLayout->addWidget(execBtn);
  layout->addRow("Target Executable:", execLayout);

  m_execArgsEdit = new QLineEdit();
  m_execArgsEdit->setPlaceholderText(
      "-v --config=file.txt"); // Optional: helps the user know what goes here
  layout->addRow("Executable Arguments:", m_execArgsEdit);

  return page;
}

QWizardPage *ProjectWizard::createMpiPage() {
  auto *page = new QWizardPage();
  page->setTitle("MPI Settings");
  auto *layout = new QFormLayout(page);

  m_ranksBox = new QSpinBox();
  m_ranksBox->setMinimum(1);
  m_ranksBox->setMaximum(1024);
  m_ranksBox->setValue(4);
  layout->addRow("Number of Ranks (-np):", m_ranksBox);

  m_bindToCoreCheck = new QCheckBox("Bind to Core (--bind-to core)");
  layout->addRow(m_bindToCoreCheck);

  m_mapByNodeCheck = new QCheckBox("Map by Node (--map-by node)");
  layout->addRow(m_mapByNodeCheck);

  m_useHwThreadsCheck =
      new QCheckBox("Use Hardware Threads (--use-hwthread-cpus)");
  layout->addRow(m_useHwThreadsCheck);

  // --- New MPI Options ---
  m_oversubscribeCheck =
      new QCheckBox("Allow Oversubscription (--oversubscribe)");
  m_oversubscribeCheck->setToolTip(
      "Allows running more MPI ranks than available physical CPU slots.");
  layout->addRow(m_oversubscribeCheck);

  m_reportBindingsCheck = new QCheckBox("Report Bindings (--report-bindings)");
  layout->addRow(m_reportBindingsCheck);

  m_displayMapCheck = new QCheckBox("Display Map (--display-map)");
  layout->addRow(m_displayMapCheck);

  m_tagOutputCheck = new QCheckBox("Tag Output (--tag-output)");
  m_tagOutputCheck->setToolTip("Tags each line of output with the rank ID.");
  layout->addRow(m_tagOutputCheck);

  m_timestampOutputCheck =
      new QCheckBox("Timestamp Output (--timestamp-output)");
  layout->addRow(m_timestampOutputCheck);

  m_customMpiArgsEdit = new QLineEdit();
  m_customMpiArgsEdit->setPlaceholderText(
      "e.g., -x LD_LIBRARY_PATH --mca btl tcp,self");
  layout->addRow("Custom MPI Args:", m_customMpiArgsEdit);

  return page;
}
QString ProjectWizard::getMpiLaunchCommand() const {
  QStringList cmd;
  cmd << "mpirun" << "-np" << QString::number(m_ranksBox->value());

  if (m_bindToCoreCheck->isChecked())
    cmd << "--bind-to" << "core";
  if (m_mapByNodeCheck->isChecked())
    cmd << "--map-by" << "node";
  if (m_useHwThreadsCheck->isChecked())
    cmd << "--use-hwthread-cpus";

  // --- New MPI Options ---
  if (m_oversubscribeCheck->isChecked())
    cmd << "--oversubscribe";
  if (m_reportBindingsCheck->isChecked())
    cmd << "--report-bindings";
  if (m_displayMapCheck->isChecked())
    cmd << "--display-map";
  if (m_tagOutputCheck->isChecked())
    cmd << "--tag-output";
  if (m_timestampOutputCheck->isChecked())
    cmd << "--timestamp-output";

  // Capture custom OpenMPI parameters (like -x ENV_VARS or --mca configs)
  QString customMpiArgs = m_customMpiArgsEdit->text().trimmed();
  if (!customMpiArgs.isEmpty()) {
    cmd << customMpiArgs;
  }

  // Add the target binary
  cmd << m_targetExecEdit->text();

  // Append program execution arguments if they exist
  QString execArgs = m_execArgsEdit->text().trimmed();
  if (!execArgs.isEmpty()) {
    cmd << execArgs;
  }

  return cmd.join(" ");
}
QString ProjectWizard::getExecutableArguments() const {
  return m_execArgsEdit->text();
}

QString ProjectWizard::getProjectRoot() const {
  return m_workspaceDirEdit->text();
}
QString ProjectWizard::getTargetBinary() const {
  return m_targetExecEdit->text();
}
int ProjectWizard::getRanks() const { return m_ranksBox->value(); }

} // namespace gridlock::ui::dialogs
