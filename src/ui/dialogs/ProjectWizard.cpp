/**
 * @file ProjectWizard.cpp
 * @brief Implementation of new MPI project initialization wizard.
 *
 * Implements the multi-page wizard workflow, handling workspace path browsing,
 * executable selection, MPI rank allocation, and mpirun command composition.
 */

#include "ProjectWizard.hpp"

#include "../../core/managers/ConfigManager.hpp"

#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStringList>

namespace gridlock::ui::dialogs {

/**
 * @brief Constructs wizard pages, applies dark-slate chrome, and sets modal window geometry.
 * @param parent Optional parent widget.
 */
ProjectWizard::ProjectWizard(QWidget *parent) : QWizard(parent) {
  setWindowTitle("New Project Wizard");
  setMinimumSize(540, 380);
  resize(580, 420);
  setStyleSheet(QStringLiteral(
      "QWizard, QWizardPage { background: #16181d; color: #d7dae0; }"
      "QLineEdit, QSpinBox { background: #181a1f; color: #d7dae0; border: 1px solid #323844; "
      "border-radius: 2px; padding: 1px 6px; min-height: 20px; max-height: 24px; }"
      "QPushButton { background: #282c34; color: #d7dae0; border: 1px solid #3b4048; "
      "border-radius: 2px; padding: 2px 10px; min-height: 22px; max-height: 24px; }"));
  addPage(createWorkspacePage());
  addPage(createMpiPage());

  // Enforce 24px control height cap across all wizard inputs and navigation buttons.
  for (QPushButton *btn : findChildren<QPushButton *>()) btn->setFixedHeight(24);
  for (QLineEdit *edit : findChildren<QLineEdit *>()) edit->setFixedHeight(24);

  // Persist inferior executable arguments into ProjectSettings (separate from mpirun flags) on accept.
  connect(this, &QDialog::accepted, this, [this]() {
    auto &cfg = gridlock::core::ConfigManager::instance();
    if (!getProjectRoot().trimmed().isEmpty()) cfg.setWorkspace(getProjectRoot().trimmed());
    auto ps = cfg.loadProjectSettings();
    if (!getTargetBinary().trimmed().isEmpty()) ps.targetBinary = getTargetBinary().trimmed().toStdString();
    if (!getProjectRoot().trimmed().isEmpty()) ps.workingDirectory = getProjectRoot().trimmed().toStdString();
    ps.targetArguments = getExecutableArguments().trimmed();
    ps.programArguments = ps.targetArguments.toStdString();
    ps.mpiRanks = getRanks();
    QString pureMpi = getMpiLaunchCommand();
    pureMpi.remove(QStringLiteral("mpirun ")).remove(QStringLiteral("-np %1").arg(getRanks())).remove(getTargetBinary());
    ps.mpiArgs = pureMpi.simplified();
    cfg.saveProjectSettings(ps);
  });
}

/**
 * @brief Constructs Page 1: Workspace root, project name, target executable, and args.
 * @return Configured wizard page instance.
 */
QWizardPage *ProjectWizard::createWorkspacePage() {
  auto *page = new QWizardPage();
  page->setTitle("Workspace Settings");

  auto *layout = new QFormLayout(page);

  // Project name field.
  m_projectNameEdit = new QLineEdit();
  m_projectNameEdit->setObjectName(QStringLiteral("wizardProjectNameEdit"));
  m_projectNameEdit->setFixedHeight(24);
  layout->addRow("Project Name:", m_projectNameEdit);

  // Workspace folder selector with directory browse button.
  m_workspaceDirEdit = new QLineEdit();
  m_workspaceDirEdit->setObjectName(QStringLiteral("wizardWorkspaceDirEdit"));
  m_workspaceDirEdit->setFixedHeight(24);
  auto *dirBtn = new QPushButton("Browse...");
  dirBtn->setFixedHeight(24);
  connect(dirBtn, &QPushButton::clicked, this, [this]() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Workspace Directory");
    if (!dir.isEmpty()) m_workspaceDirEdit->setText(dir);
  });
  auto *dirLayout = new QHBoxLayout();
  dirLayout->addWidget(m_workspaceDirEdit);
  dirLayout->addWidget(dirBtn);
  layout->addRow("Workspace Directory:", dirLayout);

  // Target executable path with file browse button.
  m_targetExecEdit = new QLineEdit();
  m_targetExecEdit->setObjectName(QStringLiteral("wizardTargetExecEdit"));
  m_targetExecEdit->setFixedHeight(24);
  auto *execBtn = new QPushButton("Browse...");
  execBtn->setFixedHeight(24);
  connect(execBtn, &QPushButton::clicked, this, [this]() {
    QString file = QFileDialog::getOpenFileName(this, "Select Target Executable");
    if (!file.isEmpty()) m_targetExecEdit->setText(file);
  });
  auto *execLayout = new QHBoxLayout();
  execLayout->addWidget(m_targetExecEdit);
  execLayout->addWidget(execBtn);
  layout->addRow("Target Executable:", execLayout);

  // CLI argument string passed to the inferior binary (never injected into mpirun flags).
  m_execArgsEdit = new QLineEdit();
  m_execArgsEdit->setObjectName(QStringLiteral("wizardExecArgsEdit"));
  m_execArgsEdit->setFixedHeight(24);
  m_execArgsEdit->setPlaceholderText("-v --config=file.txt");
  layout->addRow("Executable Arguments:", m_execArgsEdit);

  return page;
}

/**
 * @brief Constructs Page 2: MPI ranks, CPU binding, oversubscription, and MCA flags.
 * @return Configured wizard page instance.
 */
QWizardPage *ProjectWizard::createMpiPage() {
  auto *page = new QWizardPage();
  page->setTitle("MPI Settings");
  auto *layout = new QFormLayout(page);

  // Number of parallel MPI ranks to allocate.
  m_ranksBox = new QSpinBox();
  m_ranksBox->setObjectName(QStringLiteral("wizardRanksSpin"));
  m_ranksBox->setMinimum(1);
  m_ranksBox->setMaximum(1024);
  m_ranksBox->setValue(4);
  layout->addRow("Number of Ranks (-np):", m_ranksBox);

  // Process-to-core binding checkboxes.
  m_bindToCoreCheck = new QCheckBox("Bind to Core (--bind-to core)");
  layout->addRow(m_bindToCoreCheck);

  m_mapByNodeCheck = new QCheckBox("Map by Node (--map-by node)");
  layout->addRow(m_mapByNodeCheck);

  m_useHwThreadsCheck = new QCheckBox("Use Hardware Threads (--use-hwthread-cpus)");
  layout->addRow(m_useHwThreadsCheck);

  // Oversubscription and telemetry output flags.
  m_oversubscribeCheck = new QCheckBox("Allow Oversubscription (--oversubscribe)");
  m_oversubscribeCheck->setToolTip("Allows running more MPI ranks than available physical CPU slots.");
  layout->addRow(m_oversubscribeCheck);

  m_reportBindingsCheck = new QCheckBox("Report Bindings (--report-bindings)");
  layout->addRow(m_reportBindingsCheck);

  m_displayMapCheck = new QCheckBox("Display Map (--display-map)");
  layout->addRow(m_displayMapCheck);

  m_tagOutputCheck = new QCheckBox("Tag Output (--tag-output)");
  m_tagOutputCheck->setToolTip("Tags each line of output with the rank ID.");
  layout->addRow(m_tagOutputCheck);

  m_timestampOutputCheck = new QCheckBox("Timestamp Output (--timestamp-output)");
  layout->addRow(m_timestampOutputCheck);

  // Freeform custom arguments string.
  m_customMpiArgsEdit = new QLineEdit();
  m_customMpiArgsEdit->setObjectName(QStringLiteral("wizardCustomMpiArgsEdit"));
  m_customMpiArgsEdit->setPlaceholderText("e.g., -x LD_LIBRARY_PATH --mca btl tcp,self");
  layout->addRow("Custom MPI Args:", m_customMpiArgsEdit);

  return page;
}

/**
 * @brief Constructs the synthesized `mpirun` command string (pure MPI flags + binary; target `execArgs` are stored in `ProjectSettings::programArguments`).
 * @return Shell command line string.
 */
QString ProjectWizard::getMpiLaunchCommand() const {
  QStringList cmd;
  cmd << "mpirun" << "-np" << QString::number(m_ranksBox->value());

  // Append core binding flags if enabled.
  if (m_bindToCoreCheck->isChecked()) cmd << "--bind-to" << "core";
  if (m_mapByNodeCheck->isChecked()) cmd << "--map-by" << "node";
  if (m_useHwThreadsCheck->isChecked()) cmd << "--use-hwthread-cpus";

  // Append diagnostic and mapping flags.
  if (m_oversubscribeCheck->isChecked()) cmd << "--oversubscribe";
  if (m_reportBindingsCheck->isChecked()) cmd << "--report-bindings";
  if (m_displayMapCheck->isChecked()) cmd << "--display-map";
  if (m_tagOutputCheck->isChecked()) cmd << "--tag-output";
  if (m_timestampOutputCheck->isChecked()) cmd << "--timestamp-output";

  // Capture custom OpenMPI parameters (like -x ENV_VARS or --mca configs).
  const QString customMpiArgs = m_customMpiArgsEdit->text().trimmed();
  if (!customMpiArgs.isEmpty()) cmd << customMpiArgs;

  // Add the target binary path when non-empty (excluding inferior execArgs so mpirun flags remain uncontaminated).
  const QString targetBin = m_targetExecEdit->text().trimmed();
  if (!targetBin.isEmpty()) cmd << targetBin;

  return cmd.join(" ");
}

/**
 * @brief Retrieves user-specified target command-line arguments.
 * @return Argument string.
 */
QString ProjectWizard::getExecutableArguments() const { return m_execArgsEdit->text(); }

/**
 * @brief Retrieves the chosen project root workspace directory.
 * @return Filesystem directory path.
 */
QString ProjectWizard::getProjectRoot() const { return m_workspaceDirEdit->text(); }

/**
 * @brief Retrieves the target executable binary path.
 * @return Absolute binary path.
 */
QString ProjectWizard::getTargetBinary() const { return m_targetExecEdit->text(); }

/**
 * @brief Retrieves the number of MPI ranks configured in the wizard.
 * @return Integer rank count.
 */
int ProjectWizard::getRanks() const { return m_ranksBox->value(); }

}  // namespace gridlock::ui::dialogs
