/**
 * @file HpcSettingsPages.cpp
 * @brief Implementations of HpcSettingsPage and HpcIntegrationSettingsPage UI construction and settings sync.
 */

#include "HpcSettingsPages.hpp"
#include "PreferencesFormHelpers.hpp"
#include "../../../core/managers/ConfigManager.hpp"

#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace gridlock::ui {

using prefs_detail::addFormNote;
using prefs_detail::createStandardPageForm;
using prefs_detail::setComboTextValue;

// ============================================================================
// HpcSettingsPage Implementation
// ============================================================================

/**
 * @brief Constructs local cluster topology and environment settings page.
 * @param parent Optional parent widget.
 */
HpcSettingsPage::HpcSettingsPage(QWidget* parent)
    : QWidget(parent) {
    auto* form = createStandardPageForm(this, tr("<b>HPC / Cluster</b>"));

    // Hostfile browse input layout with non-clipped 92px minimum Browse button width.
    auto* hostsRow = new QHBoxLayout();
    m_hostsFileEdit = new QLineEdit(this);
    m_hostsFileEdit->setPlaceholderText(tr("(not set — use default hosts)"));
    m_hostsFileEdit->setToolTip(
        tr("Path to an MPI hostfile. When set, --hostfile <path> is passed to the\n"
           "MPI launcher. Leave empty to let the launcher choose nodes automatically."));
    auto* browseBtn = new QPushButton(tr("Browse…"), this);
    browseBtn->setMinimumWidth(92);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select MPI Hosts File"),
            m_hostsFileEdit->text().isEmpty() ? QDir::homePath() : m_hostsFileEdit->text(),
            tr("All Files (*)"), nullptr, QFileDialog::DontUseNativeDialog);
        if (!path.isEmpty()) m_hostsFileEdit->setText(path);
    });
    hostsRow->addWidget(m_hostsFileEdit);
    hostsRow->addWidget(browseBtn);
    form->addRow(tr("MPI Hosts File:"), hostsRow);

    // Multi-line environment variable key-value definitions for MPI rank processes.
    m_envVarsEdit = new QTextEdit(this);
    m_envVarsEdit->setFixedHeight(90);
    m_envVarsEdit->setPlaceholderText(
        tr("One key=value pair per line\ne.g.  OMPI_MCA_btl=tcp,self\n      UCX_NET_DEVICES=eth0"));
    m_envVarsEdit->setToolTip(
        tr("MPI environment variables injected via -x KEY=VALUE flags.\nOne entry per line: KEY=VALUE"));
    m_envVarsEdit->setAcceptRichText(false);
    form->addRow(tr("MPI Env Vars:"), m_envVarsEdit);

    // Strict node affinity binding option for NUMA-sensitive multi-node runs.
    m_strictAffinityBox = new QCheckBox(tr("Bind ranks to individual nodes (--map-by node)"), this);
    m_strictAffinityBox->setToolTip(
        tr("Passes --map-by node to the MPI launcher so each rank is pinned to\n"
           "a separate physical node. Useful for NUMA-sensitive workloads."));
    form->addRow(tr("Strict Node Affinity:"), m_strictAffinityBox);

    // Opt-in toggle for exposing the experimental Submit Slurm Job action in the File menu.
    auto* slurmOptInLayout = new QVBoxLayout();
    slurmOptInLayout->setContentsMargins(0, 0, 0, 0);
    slurmOptInLayout->setSpacing(3);
    m_enableSlurmMenuBox = new QCheckBox(tr("Show \"Submit Slurm Job...\" in File menu"), this);
    m_enableSlurmMenuBox->setObjectName(QStringLiteral("enableSlurmMenuCheck"));
    m_enableSlurmMenuBox->setChecked(false);
    m_slurmWarningLabel = new QLabel(tr("Experimental, untested, not responsible for breakage"), this);
    m_slurmWarningLabel->setObjectName(QStringLiteral("slurmExperimentalWarningLabel"));
    m_slurmWarningLabel->setStyleSheet(QStringLiteral("color: #ff4d4f; font-weight: bold;"));
    m_slurmWarningLabel->setWordWrap(true);
    slurmOptInLayout->addWidget(m_enableSlurmMenuBox);
    slurmOptInLayout->addWidget(m_slurmWarningLabel);
    form->addRow(tr("Slurm Integration:"), slurmOptInLayout);

    addFormNote(this, form,
                tr("<small style='color:#888;'>Changes are applied on the "
                   "<b>next</b> debug session launch.</small>"));

    loadFromSettings();
}

QString HpcSettingsPage::hostsFile() const { return m_hostsFileEdit->text().trimmed(); }
QString HpcSettingsPage::envVars() const { return m_envVarsEdit->toPlainText().trimmed(); }
bool HpcSettingsPage::strictAffinity() const { return m_strictAffinityBox->isChecked(); }
bool HpcSettingsPage::enableSlurmMenu() const { return m_enableSlurmMenuBox && m_enableSlurmMenuBox->isChecked(); }

/**
 * @brief Restores cluster settings from ConfigManager.
 */
void HpcSettingsPage::loadFromSettings() {
    const auto hs = gridlock::core::ConfigManager::instance().getHpcSettings();
    m_hostsFileEdit->setText(hs.hostsFile);
    m_envVarsEdit->setPlainText(hs.envVars);
    m_strictAffinityBox->setChecked(hs.strictAffinity);
    if (m_enableSlurmMenuBox) m_enableSlurmMenuBox->setChecked(hs.enableSlurmMenu);
}

// ============================================================================
// HpcIntegrationSettingsPage Implementation
// ============================================================================

/**
 * @brief Constructs remote SSH, 16-directive SLURM batch, and Spack integration page.
 * @param parent Optional parent widget.
 */
HpcIntegrationSettingsPage::HpcIntegrationSettingsPage(QWidget* parent)
    : QWidget(parent) {
    auto* form = new QFormLayout(this);
    form->setContentsMargins(20, 16, 20, 24);
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto addSectionHeading = [&](const QString& title) {
        auto* lbl = new QLabel(title, this);
        lbl->setObjectName(QStringLiteral("heading"));
        form->addRow(lbl);
    };

    // 1. SSH Remote node credentials section.
    addSectionHeading(tr("<b>Remote SSH Connection</b>"));

    m_sshHostEdit = new QLineEdit(this);
    m_sshHostEdit->setPlaceholderText(tr("e.g. login.hpc.university.edu"));
    m_sshHostEdit->setToolTip(tr("Hostname or IP of the HPC login node."));
    form->addRow(tr("SSH Host:"), m_sshHostEdit);

    m_sshUserEdit = new QLineEdit(this);
    m_sshUserEdit->setPlaceholderText(tr("your_hpc_username"));
    form->addRow(tr("SSH User:"), m_sshUserEdit);

    auto* keyRow = new QHBoxLayout();
    m_sshKeyEdit = new QLineEdit(this);
    m_sshKeyEdit->setPlaceholderText(tr("~/.ssh/id_rsa"));
    m_sshKeyEdit->setToolTip(tr("Path to the private SSH key file used for authentication."));
    auto* keyBrowse = new QPushButton(tr("Browse…"), this);
    keyBrowse->setMinimumWidth(92);
    connect(keyBrowse, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select SSH Private Key"), QDir::homePath(),
            tr("SSH Keys (id_rsa id_ed25519 *.pem);; All Files (*)"), nullptr,
            QFileDialog::DontUseNativeDialog);
        if (!path.isEmpty()) m_sshKeyEdit->setText(path);
    });
    keyRow->addWidget(m_sshKeyEdit, 1);
    keyRow->addWidget(keyBrowse);
    form->addRow(tr("SSH Key Path:"), keyRow);

    // 2. SLURM Batch Configuration section with responsive script loader and 16 full-width directives.
    addSectionHeading(tr("<b>SLURM Batch Configuration</b>"));

    auto* sbatchContainer = new QVBoxLayout();
    sbatchContainer->setSpacing(6);
    m_sbatchFilePathEdit = new QLineEdit(this);
    m_sbatchFilePathEdit->setPlaceholderText(
        tr("Select an existing .sbatch / .slurm / .sh script to auto-populate fields..."));
    sbatchContainer->addWidget(m_sbatchFilePathEdit);

    auto* sbatchBtnRow = new QHBoxLayout();
    sbatchBtnRow->setSpacing(8);
    auto* loadSbatchBtn = new QPushButton(tr("Load && Auto-Detect .sbatch Script..."), this);
    loadSbatchBtn->setObjectName(QStringLiteral("loadSlurmScriptButton"));
    auto* syncPreviewBtn = new QPushButton(tr("Sync Fields -> Script Preview"), this);
    connect(loadSbatchBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select SLURM Batch Script"), QDir::currentPath(),
            tr("SLURM Batch Scripts (*.sbatch *.slurm *.job *.sh);;All Files (*)"),
            nullptr, QFileDialog::DontUseNativeDialog);
        if (!path.isEmpty()) loadSlurmBatchScriptFile(path);
    });
    connect(syncPreviewBtn, &QPushButton::clicked, this, [this]() {
        m_templateEdit->setPlainText(generateSlurmBatchScript());
    });
    sbatchBtnRow->addWidget(loadSbatchBtn);
    sbatchBtnRow->addWidget(syncPreviewBtn);
    sbatchBtnRow->addStretch(1);
    sbatchContainer->addLayout(sbatchBtnRow);
    form->addRow(tr("Batch Script:"), sbatchContainer);

    m_jobNameEdit = new QLineEdit(this);
    m_jobNameEdit->setPlaceholderText(tr("gridlock_debug"));
    m_jobNameEdit->setToolTip(tr("SLURM job name (--job-name / -J)."));
    form->addRow(tr("Job Name:"), m_jobNameEdit);

    m_accountEdit = new QLineEdit(this);
    m_accountEdit->setPlaceholderText(tr("(optional allocation / project)"));
    m_accountEdit->setToolTip(tr("SLURM allocation or project account (--account / -A)."));
    form->addRow(tr("Account:"), m_accountEdit);

    m_partitionCombo = new QComboBox(this);
    m_partitionCombo->setEditable(true);
    m_partitionCombo->addItems({QStringLiteral("batch"), QStringLiteral("debug"), QStringLiteral("gpu"),
                                QStringLiteral("compute"), QStringLiteral("interactive"), QStringLiteral("highmem"), QStringLiteral("long")});
    m_partitionCombo->setToolTip(tr("Target SLURM queue or partition (--partition / -p)."));
    form->addRow(tr("Partition:"), m_partitionCombo);

    m_qosCombo = new QComboBox(this);
    m_qosCombo->setEditable(true);
    m_qosCombo->addItems({QStringLiteral("(default)"), QStringLiteral("normal"), QStringLiteral("debug"),
                          QStringLiteral("high"), QStringLiteral("long"), QStringLiteral("standby")});
    m_qosCombo->setToolTip(tr("Quality of Service tier (--qos / -q)."));
    form->addRow(tr("QoS:"), m_qosCombo);

    m_timeCombo = new QComboBox(this);
    m_timeCombo->setEditable(true);
    m_timeCombo->addItems({QStringLiteral("00:15:00"), QStringLiteral("00:30:00"), QStringLiteral("01:00:00"), QStringLiteral("02:00:00"),
                           QStringLiteral("04:00:00"), QStringLiteral("08:00:00"), QStringLiteral("12:00:00"), QStringLiteral("24:00:00"),
                           QStringLiteral("48:00:00"), QStringLiteral("72:00:00")});
    m_timeCombo->setCurrentText(QStringLiteral("01:00:00"));
    m_timeCombo->setToolTip(tr("Maximum job wall-clock time (--time / -t)."));
    form->addRow(tr("Wall Time:"), m_timeCombo);

    m_nodesBox = new QSpinBox(this); m_nodesBox->setRange(1, 9999); m_nodesBox->setValue(1);
    m_nodesBox->setToolTip(tr("Number of compute nodes (--nodes / -N)."));
    form->addRow(tr("Nodes:"), m_nodesBox);

    m_tasksBox = new QSpinBox(this); m_tasksBox->setRange(1, 1024); m_tasksBox->setValue(4);
    m_tasksBox->setToolTip(tr("MPI tasks per compute node (--ntasks-per-node / -n)."));
    form->addRow(tr("Tasks / Node:"), m_tasksBox);

    m_cpusPerTaskBox = new QSpinBox(this); m_cpusPerTaskBox->setRange(1, 256); m_cpusPerTaskBox->setValue(1);
    m_cpusPerTaskBox->setToolTip(tr("CPU cores allocated per MPI task (--cpus-per-task)."));
    form->addRow(tr("CPUs / Task:"), m_cpusPerTaskBox);

    m_memCombo = new QComboBox(this);
    m_memCombo->setEditable(true);
    m_memCombo->addItems({QStringLiteral("4G"), QStringLiteral("8G"), QStringLiteral("16G"), QStringLiteral("32G"),
                          QStringLiteral("64G"), QStringLiteral("128G"), QStringLiteral("256G"), QStringLiteral("512G"),
                          QStringLiteral("0 (All Node Memory)")});
    m_memCombo->setCurrentText(QStringLiteral("16G"));
    m_memCombo->setToolTip(tr("Memory allocated per node (--mem)."));
    form->addRow(tr("Memory:"), m_memCombo);

    auto* gpuRow = new QHBoxLayout(); gpuRow->setSpacing(8);
    m_gpuCheck = new QCheckBox(tr("Request GPUs"), this);
    m_gpuTypeEdit = new QLineEdit(this);
    m_gpuTypeEdit->setPlaceholderText(tr("GPU model (e.g. a100, v100)"));
    m_gpuTypeEdit->setToolTip(tr("Optional GPU accelerator type for --gres=gpu:<type>:<count>."));
    m_gpuTypeEdit->setEnabled(false);
    m_gpusBox = new QSpinBox(this); m_gpusBox->setRange(1, 16); m_gpusBox->setMinimumWidth(110);
    m_gpusBox->setToolTip(tr("Number of GPUs per node."));
    m_gpusBox->setEnabled(false);
    connect(m_gpuCheck, &QCheckBox::toggled, m_gpuTypeEdit, &QLineEdit::setEnabled);
    connect(m_gpuCheck, &QCheckBox::toggled, m_gpusBox, &QSpinBox::setEnabled);
    gpuRow->addWidget(m_gpuCheck); gpuRow->addWidget(m_gpuTypeEdit, 1); gpuRow->addWidget(m_gpusBox);
    form->addRow(tr("GRES GPUs:"), gpuRow);

    m_constraintEdit = new QLineEdit(this);
    m_constraintEdit->setPlaceholderText(tr("e.g. rome, hdr, nvlink"));
    m_constraintEdit->setToolTip(tr("Node hardware feature constraint (--constraint / -C)."));
    form->addRow(tr("Constraint:"), m_constraintEdit);

    m_mpiLauncherCombo = new QComboBox(this);
    m_mpiLauncherCombo->setEditable(true);
    m_mpiLauncherCombo->addItems({QStringLiteral("srun"), QStringLiteral("mpirun"), QStringLiteral("mpiexec"), QStringLiteral("srun --mpi=pmix")});
    m_mpiLauncherCombo->setToolTip(tr("MPI job launcher command invoked inside the batch script."));
    form->addRow(tr("MPI Launcher:"), m_mpiLauncherCombo);

    m_modulesEdit = new QLineEdit(this);
    m_modulesEdit->setPlaceholderText(tr("e.g. gcc/13.2 openmpi/5.0 cuda/12.2"));
    m_modulesEdit->setToolTip(tr("Space-separated environment modules loaded via 'module load'."));
    form->addRow(tr("Modules:"), m_modulesEdit);

    m_outputLogEdit = new QLineEdit(this);
    m_outputLogEdit->setText(QStringLiteral("gridlock_%j.log"));
    m_outputLogEdit->setToolTip(tr("SLURM stdout/stderr log file pattern (--output / -o)."));
    form->addRow(tr("Output Log:"), m_outputLogEdit);

    m_mailTypeCombo = new QComboBox(this);
    m_mailTypeCombo->setEditable(true);
    m_mailTypeCombo->addItems({QStringLiteral("NONE"), QStringLiteral("END,FAIL"), QStringLiteral("ALL"), QStringLiteral("BEGIN,END,FAIL"), QStringLiteral("FAIL")});
    m_mailTypeCombo->setToolTip(tr("Email notification triggers (--mail-type)."));
    form->addRow(tr("Mail Type:"), m_mailTypeCombo);

    m_mailUserEdit = new QLineEdit(this);
    m_mailUserEdit->setPlaceholderText(tr("user@university.edu"));
    m_mailUserEdit->setToolTip(tr("Email address for SLURM job notifications (--mail-user)."));
    form->addRow(tr("Mail User:"), m_mailUserEdit);

    auto* tplLabel = new QLabel(
        tr("Script Template (Use <tt>{FILE}</tt> as a placeholder for the target binary):"), this);
    tplLabel->setTextFormat(Qt::RichText);
    tplLabel->setWordWrap(true);
    tplLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    form->addRow(tplLabel);

    m_templateEdit = new QPlainTextEdit(this);
    m_templateEdit->setMinimumHeight(120);
    QFont mono(QStringLiteral("Monospace"), 9);
    mono.setStyleHint(QFont::TypeWriter);
    m_templateEdit->setFont(mono);
    form->addRow(m_templateEdit);

    // 3. Spack package manager environment configuration.
    addSectionHeading(tr("<b>Spack Package Manager</b>"));

    m_spackRootEdit = new QLineEdit(this);
    m_spackRootEdit->setPlaceholderText(tr("/opt/spack"));
    m_spackRootEdit->setToolTip(
        tr("Absolute path to the Spack installation on the remote node.\n"
           "GridLock will source \"<path>/share/spack/setup-env.sh\" before\n"
           "running any spack commands."));
    form->addRow(tr("Spack Install Path:"), m_spackRootEdit);

    loadFromSettings();
}

QString HpcIntegrationSettingsPage::sshHost() const { return m_sshHostEdit->text().trimmed(); }
QString HpcIntegrationSettingsPage::sshUser() const { return m_sshUserEdit->text().trimmed(); }
QString HpcIntegrationSettingsPage::sshKeyPath() const { return m_sshKeyEdit->text().trimmed(); }
QString HpcIntegrationSettingsPage::slurmTemplate() const { return m_templateEdit->toPlainText(); }
QString HpcIntegrationSettingsPage::slurmPartition() const { return m_partitionCombo->currentText().trimmed(); }
int HpcIntegrationSettingsPage::slurmNodes() const { return m_nodesBox->value(); }
int HpcIntegrationSettingsPage::slurmTasks() const { return m_tasksBox->value(); }
bool HpcIntegrationSettingsPage::requestGpus() const { return m_gpuCheck->isChecked(); }
int HpcIntegrationSettingsPage::gpusPerNode() const { return m_gpusBox->value(); }
QString HpcIntegrationSettingsPage::spackRoot() const { return m_spackRootEdit->text().trimmed(); }

void HpcIntegrationSettingsPage::loadFromSettings() {
    auto& mgr = gridlock::core::ConfigManager::instance();
    const auto ssh = mgr.getSshSettings();
    const auto slurm = mgr.getSlurmSettings();

    m_sshHostEdit->setText(ssh.host);
    m_sshUserEdit->setText(ssh.user);
    m_sshKeyEdit->setText(ssh.keyPath);

    m_jobNameEdit->setText(QStringLiteral("gridlock_debug"));
    setComboTextValue(m_partitionCombo, slurm.partition.isEmpty() ? QStringLiteral("batch") : slurm.partition);
    m_nodesBox->setValue(qMax(1, slurm.nodes));
    m_tasksBox->setValue(qMax(1, slurm.tasksPerNode));
    m_gpuCheck->setChecked(slurm.requestGpus);
    m_gpusBox->setValue(qMax(1, slurm.gpusPerNode));
    m_spackRootEdit->setText(slurm.spackRoot);

    if (!slurm.scriptTemplate.trimmed().isEmpty()) {
        parseSlurmBatchScript(slurm.scriptTemplate);
    } else {
        m_templateEdit->setPlainText(generateSlurmBatchScript());
    }
}

}  // namespace gridlock::ui
