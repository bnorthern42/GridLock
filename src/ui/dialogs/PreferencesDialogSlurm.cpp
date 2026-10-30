/**
 * @file PreferencesDialogSlurm.cpp
 * @brief Implementation of HPC Cluster, Remote SSH, Spack, and 16-Directive SLURM settings pages.
 *
 * Implements HpcSettingsPage and HpcIntegrationSettingsPage, including a full
 * 16-directive SLURM `#SBATCH` configuration form, batch script file loader,
 * automatic `#SBATCH` directive and `module load` parser, and bidirectional
 * script template generator.
 */

#include "PreferencesDialog.hpp"
#include "../../core/managers/ConfigManager.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

namespace gridlock::ui {

namespace {

/**
 * @brief Sets an editable or standard QComboBox value, selecting an existing item if matched.
 *
 * Searches the combo box item list for an exact string match first so preset
 * indices stay synchronized; otherwise writes the custom string into the editable
 * line edit of the combo box.
 *
 * @param combo Target editable QComboBox control.
 * @param value Desired directive string value.
 */
void setComboTextValue(QComboBox* combo, const QString& value) {
    if (!combo) {
        return;
    }
    // Check if the requested string matches an existing preset item first.
    const int idx = combo->findText(value, Qt::MatchFixedString);
    if (idx >= 0) {
        // Select the matching preset entry by index.
        combo->setCurrentIndex(idx);
    } else {
        // Populate the custom value into the editable combo text field.
        combo->setEditText(value);
    }
}

}  // namespace

// ============================================================================
// HpcSettingsPage Implementation
// ============================================================================

/**
 * @brief Constructs local cluster topology and environment settings page.
 *
 * Provides controls for selecting an MPI hostfile, specifying multi-line
 * environment variable key-value pairs (`-x KEY=VALUE`), and enabling strict
 * physical node affinity (`--map-by node`).
 *
 * @param parent Optional parent widget.
 */
HpcSettingsPage::HpcSettingsPage(QWidget* parent)
    : QWidget(parent) {
    // Configure top-level form layout with section heading and separator.
    auto* form = new QFormLayout(this);
    form->setContentsMargins(24, 24, 24, 24);
    form->setSpacing(14);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Create bold category title label at the top of the page.
    auto* heading = new QLabel(tr("<b>HPC / Cluster</b>"), this);
    heading->setObjectName("heading");
    form->addRow(heading);

    // Add 1px horizontal divider beneath the category title.
    auto* separator = new QLabel(this);
    separator->setFixedHeight(1);
    separator->setObjectName("separator");
    form->addRow(separator);

    // Hostfile browse input layout with non-clipped 92px minimum Browse button width.
    auto* hostsRow = new QHBoxLayout();
    m_hostsFileEdit = new QLineEdit(this);
    m_hostsFileEdit->setPlaceholderText(tr("(not set — use default hosts)"));
    m_hostsFileEdit->setToolTip(
        tr("Path to an MPI hostfile. When set, --hostfile <path> is passed to the\n"
           "MPI launcher. Leave empty to let the launcher choose nodes automatically."));
    auto* browseBtn = new QPushButton(tr("Browse…"), this);
    // Reserve >= 92px width so the uppercase button label is never clipped.
    browseBtn->setMinimumWidth(92);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        // Open non-native file dialog starting from the current hostfile path or Home.
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select MPI Hosts File"),
            m_hostsFileEdit->text().isEmpty() ? QDir::homePath()
                                              : m_hostsFileEdit->text(),
            tr("All Files (*)"), nullptr, QFileDialog::DontUseNativeDialog);
        if (!path.isEmpty()) {
            m_hostsFileEdit->setText(path);
        }
    });
    hostsRow->addWidget(m_hostsFileEdit);
    hostsRow->addWidget(browseBtn);
    form->addRow(tr("MPI Hosts File:"), hostsRow);

    // Multi-line environment variable key-value definitions for MPI rank processes.
    m_envVarsEdit = new QTextEdit(this);
    m_envVarsEdit->setFixedHeight(90);
    m_envVarsEdit->setPlaceholderText(
        tr("One key=value pair per line\ne.g.  OMPI_MCA_btl=tcp,self\n      "
           "UCX_NET_DEVICES=eth0"));
    m_envVarsEdit->setToolTip(
        tr("MPI environment variables injected via -x KEY=VALUE flags.\nOne "
           "entry per line: KEY=VALUE"));
    m_envVarsEdit->setAcceptRichText(false);
    form->addRow(tr("MPI Env Vars:"), m_envVarsEdit);

    // Strict node affinity binding option for NUMA-sensitive multi-node runs.
    m_strictAffinityBox =
        new QCheckBox(tr("Bind ranks to individual nodes (--map-by node)"), this);
    m_strictAffinityBox->setToolTip(
        tr("Passes --map-by node to the MPI launcher so each rank is pinned to\n"
           "a separate physical node. Useful for NUMA-sensitive workloads."));
    form->addRow(tr("Strict Node Affinity:"), m_strictAffinityBox);

    // Footnote reminding the user when MPI cluster flags take effect.
    auto* note = new QLabel(
        tr("<small style='color:#888;'>Changes are applied on the "
           "<b>next</b> debug session launch.</small>"),
        this);
    note->setWordWrap(true);
    form->addRow(QString(), note);

    // Populate initial form controls from persistent ConfigManager state.
    loadFromSettings();
}

/// Returns the trimmed MPI hostfile path configured by the user.
QString HpcSettingsPage::hostsFile() const { return m_hostsFileEdit->text().trimmed(); }

/// Returns the newline-delimited KEY=VALUE environment variable list.
QString HpcSettingsPage::envVars() const { return m_envVarsEdit->toPlainText().trimmed(); }

/// Returns true when `--map-by node` rank-to-node pinning is enabled.
bool HpcSettingsPage::strictAffinity() const { return m_strictAffinityBox->isChecked(); }

/**
 * @brief Restores cluster settings from ConfigManager.
 */
void HpcSettingsPage::loadFromSettings() {
    // Query stored HpcSettings struct from ConfigManager singleton.
    const auto hs = gridlock::core::ConfigManager::instance().getHpcSettings();
    m_hostsFileEdit->setText(hs.hostsFile);
    m_envVarsEdit->setPlainText(hs.envVars);
    m_strictAffinityBox->setChecked(hs.strictAffinity);
}

// ============================================================================
// HpcIntegrationSettingsPage Implementation
// ============================================================================

/**
 * @brief Constructs remote SSH, 16-directive SLURM batch, and Spack integration page.
 *
 * Builds three logical sections inside a scrollable container:
 * 1. Remote SSH Connection (`SSH Host`, `SSH User`, `SSH Key Path`).
 * 2. SLURM Batch Configuration (`.sbatch` file loader, 2-column grid of all 16
 *    `#SBATCH` directives, and live script template editor).
 * 3. Spack Package Manager (`Spack Install Path`).
 *
 * @param parent Optional parent widget.
 */
HpcIntegrationSettingsPage::HpcIntegrationSettingsPage(QWidget* parent)
    : QWidget(parent) {
    // Wrap the page in a borderless scroll area so all 16 SLURM controls fit comfortably.
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    // Create inner scrollable form container.
    auto* container = new QWidget();
    scroll->setWidget(container);
    auto* form = new QFormLayout(container);
    form->setContentsMargins(24, 20, 24, 20);
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Helper lambda to insert styled bold section headings into the form layout.
    auto addSectionHeading = [&](const QString& title) {
        auto* lbl = new QLabel(title, container);
        lbl->setObjectName("heading");
        form->addRow(lbl);
    };

    // 1. SSH Remote node credentials section.
    addSectionHeading(tr("<b>Remote SSH Connection</b>"));

    // Remote login node hostname or IP address field.
    m_sshHostEdit = new QLineEdit(container);
    m_sshHostEdit->setPlaceholderText(tr("e.g. login.hpc.university.edu"));
    m_sshHostEdit->setToolTip(tr("Hostname or IP of the HPC login node."));
    form->addRow(tr("SSH Host:"), m_sshHostEdit);

    // Remote cluster account username field.
    m_sshUserEdit = new QLineEdit(container);
    m_sshUserEdit->setPlaceholderText(tr("your_hpc_username"));
    form->addRow(tr("SSH User:"), m_sshUserEdit);

    // Private SSH key path row with non-clipped 92px Browse button.
    auto* keyRow = new QHBoxLayout();
    m_sshKeyEdit = new QLineEdit(container);
    m_sshKeyEdit->setPlaceholderText(tr("~/.ssh/id_rsa"));
    m_sshKeyEdit->setToolTip(
        tr("Path to the private SSH key file used for authentication."));
    auto* keyBrowse = new QPushButton(tr("Browse…"), container);
    keyBrowse->setMinimumWidth(92);
    connect(keyBrowse, &QPushButton::clicked, this, [this]() {
        // Prompt user to choose an SSH private key file from ~/.ssh or Home.
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select SSH Private Key"), QDir::homePath(),
            tr("SSH Keys (id_rsa id_ed25519 *.pem);; All Files (*)"), nullptr,
            QFileDialog::DontUseNativeDialog);
        if (!path.isEmpty()) {
            m_sshKeyEdit->setText(path);
        }
    });
    keyRow->addWidget(m_sshKeyEdit);
    keyRow->addWidget(keyBrowse);
    form->addRow(tr("SSH Key Path:"), keyRow);

    // 2. SLURM Batch Configuration section with file auto-detector and 16 directives.
    addSectionHeading(tr("<b>SLURM Batch Configuration</b>"));

    // Top row: Load & Auto-Detect .sbatch Script button, file path display, and Sync button.
    auto* sbatchFileRow = new QHBoxLayout();
    auto* loadSbatchBtn = new QPushButton(tr("Load && Auto-Detect .sbatch Script..."), container);
    loadSbatchBtn->setObjectName(QStringLiteral("loadSlurmScriptButton"));
    loadSbatchBtn->setMinimumWidth(210);
    m_sbatchFilePathEdit = new QLineEdit(container);
    m_sbatchFilePathEdit->setPlaceholderText(tr("Select an existing .sbatch / .slurm / .sh script to auto-populate fields..."));
    auto* syncPreviewBtn = new QPushButton(tr("Sync Fields -> Script Preview"), container);
    syncPreviewBtn->setMinimumWidth(175);
    connect(loadSbatchBtn, &QPushButton::clicked, this, [this]() {
        // Open file chooser for .sbatch / .slurm / .job / .sh scripts and auto-parse directives.
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select SLURM Batch Script"), QDir::currentPath(),
            tr("SLURM Batch Scripts (*.sbatch *.slurm *.job *.sh);;All Files (*)"),
            nullptr, QFileDialog::DontUseNativeDialog);
        if (!path.isEmpty()) {
            loadSlurmBatchScriptFile(path);
        }
    });
    connect(syncPreviewBtn, &QPushButton::clicked, this, [this]() {
        // Regenerate the #SBATCH script preview from the 16 form controls.
        m_templateEdit->setPlainText(generateSlurmBatchScript());
    });
    sbatchFileRow->addWidget(loadSbatchBtn);
    sbatchFileRow->addWidget(m_sbatchFilePathEdit, 1);
    sbatchFileRow->addWidget(syncPreviewBtn);
    form->addRow(tr("Batch Script:"), sbatchFileRow);

    // Build a compact 2-column QGridLayout holding all 16 standard SLURM directives.
    auto* gridWidget = new QWidget(container);
    auto* grid = new QGridLayout(gridWidget);
    grid->setContentsMargins(0, 4, 0, 4);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);

    // Row 0: Directive 1 (Job Name --job-name) and Directive 2 (Account --account).
    m_jobNameEdit = new QLineEdit(gridWidget);
    m_jobNameEdit->setPlaceholderText(tr("gridlock_debug"));
    m_accountEdit = new QLineEdit(gridWidget);
    m_accountEdit->setPlaceholderText(tr("(optional allocation / project)"));
    grid->addWidget(new QLabel(tr("Job Name (--job-name):"), gridWidget), 0, 0, Qt::AlignRight);
    grid->addWidget(m_jobNameEdit, 0, 1);
    grid->addWidget(new QLabel(tr("Account (--account):"), gridWidget), 0, 2, Qt::AlignRight);
    grid->addWidget(m_accountEdit, 0, 3);

    // Row 1: Directive 3 (Partition --partition) and Directive 4 (Quality of Service --qos).
    m_partitionCombo = new QComboBox(gridWidget);
    m_partitionCombo->setEditable(true);
    m_partitionCombo->addItems({QStringLiteral("batch"), QStringLiteral("debug"),
                                QStringLiteral("gpu"), QStringLiteral("compute"),
                                QStringLiteral("interactive"), QStringLiteral("highmem"),
                                QStringLiteral("long")});
    m_qosCombo = new QComboBox(gridWidget);
    m_qosCombo->setEditable(true);
    m_qosCombo->addItems({QStringLiteral("(default)"), QStringLiteral("normal"),
                          QStringLiteral("debug"), QStringLiteral("high"),
                          QStringLiteral("long"), QStringLiteral("standby")});
    grid->addWidget(new QLabel(tr("Partition (--partition):"), gridWidget), 1, 0, Qt::AlignRight);
    grid->addWidget(m_partitionCombo, 1, 1);
    grid->addWidget(new QLabel(tr("QoS (--qos):"), gridWidget), 1, 2, Qt::AlignRight);
    grid->addWidget(m_qosCombo, 1, 3);

    // Row 2: Directive 5 (Wall-Clock Time --time) and Directive 9 (Memory per Node --mem).
    m_timeCombo = new QComboBox(gridWidget);
    m_timeCombo->setEditable(true);
    m_timeCombo->addItems({QStringLiteral("00:15:00"), QStringLiteral("00:30:00"),
                           QStringLiteral("01:00:00"), QStringLiteral("02:00:00"),
                           QStringLiteral("04:00:00"), QStringLiteral("08:00:00"),
                           QStringLiteral("12:00:00"), QStringLiteral("24:00:00"),
                           QStringLiteral("48:00:00"), QStringLiteral("72:00:00")});
    m_timeCombo->setCurrentText(QStringLiteral("01:00:00"));
    m_memCombo = new QComboBox(gridWidget);
    m_memCombo->setEditable(true);
    m_memCombo->addItems({QStringLiteral("4G"), QStringLiteral("8G"), QStringLiteral("16G"),
                          QStringLiteral("32G"), QStringLiteral("64G"), QStringLiteral("128G"),
                          QStringLiteral("256G"), QStringLiteral("512G"),
                          QStringLiteral("0 (All Node Memory)")});
    m_memCombo->setCurrentText(QStringLiteral("16G"));
    grid->addWidget(new QLabel(tr("Wall Time (--time):"), gridWidget), 2, 0, Qt::AlignRight);
    grid->addWidget(m_timeCombo, 2, 1);
    grid->addWidget(new QLabel(tr("Memory (--mem):"), gridWidget), 2, 2, Qt::AlignRight);
    grid->addWidget(m_memCombo, 2, 3);

    // Row 3: Directive 6 (Nodes --nodes) and Directive 7 (Tasks per Node --ntasks-per-node).
    m_nodesBox = new QSpinBox(gridWidget);
    m_nodesBox->setRange(1, 9999);
    m_nodesBox->setValue(1);
    m_tasksBox = new QSpinBox(gridWidget);
    m_tasksBox->setRange(1, 1024);
    m_tasksBox->setValue(4);
    grid->addWidget(new QLabel(tr("Nodes (--nodes):"), gridWidget), 3, 0, Qt::AlignRight);
    grid->addWidget(m_nodesBox, 3, 1);
    grid->addWidget(new QLabel(tr("Tasks/Node (--ntasks-per-node):"), gridWidget), 3, 2, Qt::AlignRight);
    grid->addWidget(m_tasksBox, 3, 3);

    // Row 4: Directive 8 (CPUs per Task --cpus-per-task) and Directive 11 (Constraint --constraint).
    m_cpusPerTaskBox = new QSpinBox(gridWidget);
    m_cpusPerTaskBox->setRange(1, 256);
    m_cpusPerTaskBox->setValue(1);
    m_constraintEdit = new QLineEdit(gridWidget);
    m_constraintEdit->setPlaceholderText(tr("e.g. rome, hdr, nvlink"));
    grid->addWidget(new QLabel(tr("CPUs/Task (--cpus-per-task):"), gridWidget), 4, 0, Qt::AlignRight);
    grid->addWidget(m_cpusPerTaskBox, 4, 1);
    grid->addWidget(new QLabel(tr("Constraint (--constraint):"), gridWidget), 4, 2, Qt::AlignRight);
    grid->addWidget(m_constraintEdit, 4, 3);

    // Row 5: Directive 10 (GPUs / GRES --gres=gpu:<type>:<count>) and Directive 12 (MPI Launcher).
    auto* gpuCell = new QWidget(gridWidget);
    auto* gpuRow = new QHBoxLayout(gpuCell);
    gpuRow->setContentsMargins(0, 0, 0, 0);
    gpuRow->setSpacing(4);
    m_gpuCheck = new QCheckBox(tr("GPUs"), gpuCell);
    m_gpuTypeEdit = new QLineEdit(gpuCell);
    m_gpuTypeEdit->setPlaceholderText(tr("a100 / v100"));
    m_gpuTypeEdit->setEnabled(false);
    m_gpusBox = new QSpinBox(gpuCell);
    m_gpusBox->setRange(1, 16);
    m_gpusBox->setEnabled(false);
    // Enable GPU model and count controls only when Request GPUs is checked.
    connect(m_gpuCheck, &QCheckBox::toggled, m_gpuTypeEdit, &QLineEdit::setEnabled);
    connect(m_gpuCheck, &QCheckBox::toggled, m_gpusBox, &QSpinBox::setEnabled);
    gpuRow->addWidget(m_gpuCheck);
    gpuRow->addWidget(m_gpuTypeEdit, 1);
    gpuRow->addWidget(m_gpusBox);

    m_mpiLauncherCombo = new QComboBox(gridWidget);
    m_mpiLauncherCombo->setEditable(true);
    m_mpiLauncherCombo->addItems({QStringLiteral("srun"), QStringLiteral("mpirun"),
                                  QStringLiteral("mpiexec"), QStringLiteral("srun --mpi=pmix")});
    grid->addWidget(new QLabel(tr("GRES GPUs (--gres):"), gridWidget), 5, 0, Qt::AlignRight);
    grid->addWidget(gpuCell, 5, 1);
    grid->addWidget(new QLabel(tr("MPI Launcher:"), gridWidget), 5, 2, Qt::AlignRight);
    grid->addWidget(m_mpiLauncherCombo, 5, 3);

    // Row 6: Directive 13 (Environment Modules module load) and Directive 14 (Output Log --output).
    m_modulesEdit = new QLineEdit(gridWidget);
    m_modulesEdit->setPlaceholderText(tr("e.g. gcc/13.2 openmpi/5.0 cuda/12.2"));
    m_outputLogEdit = new QLineEdit(gridWidget);
    m_outputLogEdit->setText(QStringLiteral("gridlock_%j.log"));
    grid->addWidget(new QLabel(tr("Modules (module load):"), gridWidget), 6, 0, Qt::AlignRight);
    grid->addWidget(m_modulesEdit, 6, 1);
    grid->addWidget(new QLabel(tr("Output Log (--output):"), gridWidget), 6, 2, Qt::AlignRight);
    grid->addWidget(m_outputLogEdit, 6, 3);

    // Row 7: Directive 15 (Email Notification --mail-type) and Directive 16 (Notification Email --mail-user).
    m_mailTypeCombo = new QComboBox(gridWidget);
    m_mailTypeCombo->setEditable(true);
    m_mailTypeCombo->addItems({QStringLiteral("NONE"), QStringLiteral("END,FAIL"),
                               QStringLiteral("ALL"), QStringLiteral("BEGIN,END,FAIL"),
                               QStringLiteral("FAIL")});
    m_mailUserEdit = new QLineEdit(gridWidget);
    m_mailUserEdit->setPlaceholderText(tr("user@university.edu"));
    grid->addWidget(new QLabel(tr("Mail Type (--mail-type):"), gridWidget), 7, 0, Qt::AlignRight);
    grid->addWidget(m_mailTypeCombo, 7, 1);
    grid->addWidget(new QLabel(tr("Mail User (--mail-user):"), gridWidget), 7, 2, Qt::AlignRight);
    grid->addWidget(m_mailUserEdit, 7, 3);

    form->addRow(gridWidget);

    // Editable script template preview box showing the full batch job script.
    auto* tplLabel = new QLabel(
        tr("Script Template (Use <tt>{FILE}</tt> as a placeholder for the target binary):"),
        container);
    tplLabel->setTextFormat(Qt::RichText);
    form->addRow(tplLabel);

    m_templateEdit = new QPlainTextEdit(container);
    m_templateEdit->setMinimumHeight(120);
    QFont mono(QStringLiteral("Monospace"), 9);
    mono.setStyleHint(QFont::TypeWriter);
    m_templateEdit->setFont(mono);
    form->addRow(m_templateEdit);

    // 3. Spack package manager environment configuration.
    addSectionHeading(tr("<b>Spack Package Manager</b>"));

    m_spackRootEdit = new QLineEdit(container);
    m_spackRootEdit->setPlaceholderText(tr("/opt/spack"));
    m_spackRootEdit->setToolTip(
        tr("Absolute path to the Spack installation on the remote node.\n"
           "GridLock will source \"<path>/share/spack/setup-env.sh\" before\n"
           "running any spack commands."));
    form->addRow(tr("Spack Install Path:"), m_spackRootEdit);

    // Load saved SSH, SLURM, and Spack settings from ConfigManager.
    loadFromSettings();
}

/// Returns the remote SSH login hostname.
QString HpcIntegrationSettingsPage::sshHost() const { return m_sshHostEdit->text().trimmed(); }

/// Returns the remote SSH login username.
QString HpcIntegrationSettingsPage::sshUser() const { return m_sshUserEdit->text().trimmed(); }

/// Returns the local filesystem path to the private SSH key.
QString HpcIntegrationSettingsPage::sshKeyPath() const { return m_sshKeyEdit->text().trimmed(); }

/// Returns the full SLURM batch script text from the template editor.
QString HpcIntegrationSettingsPage::slurmTemplate() const { return m_templateEdit->toPlainText(); }

/// Returns the currently selected or custom SLURM partition name.
QString HpcIntegrationSettingsPage::slurmPartition() const { return m_partitionCombo->currentText().trimmed(); }

/// Returns the requested SLURM node count (`--nodes`).
int HpcIntegrationSettingsPage::slurmNodes() const { return m_nodesBox->value(); }

/// Returns the requested MPI tasks per node (`--ntasks-per-node`).
int HpcIntegrationSettingsPage::slurmTasks() const { return m_tasksBox->value(); }

/// Returns true if GPU GRES allocation (`--gres=gpu:...`) is enabled.
bool HpcIntegrationSettingsPage::requestGpus() const { return m_gpuCheck->isChecked(); }

/// Returns the requested number of GPUs per node.
int HpcIntegrationSettingsPage::gpusPerNode() const { return m_gpusBox->value(); }

/// Returns the remote Spack installation root directory.
QString HpcIntegrationSettingsPage::spackRoot() const { return m_spackRootEdit->text().trimmed(); }

/**
 * @brief Restores remote SSH, SLURM, and Spack configurations from ConfigManager.
 */
void HpcIntegrationSettingsPage::loadFromSettings() {
    auto& mgr = gridlock::core::ConfigManager::instance();
    const auto ssh = mgr.getSshSettings();
    const auto slurm = mgr.getSlurmSettings();

    // Populate SSH host, username, and private key controls.
    m_sshHostEdit->setText(ssh.host);
    m_sshUserEdit->setText(ssh.user);
    m_sshKeyEdit->setText(ssh.keyPath);

    // Populate baseline SLURM fields before parsing the full script template.
    m_jobNameEdit->setText(QStringLiteral("gridlock_debug"));
    setComboTextValue(m_partitionCombo, slurm.partition.isEmpty() ? QStringLiteral("batch") : slurm.partition);
    m_nodesBox->setValue(qMax(1, slurm.nodes));
    m_tasksBox->setValue(qMax(1, slurm.tasksPerNode));
    m_gpuCheck->setChecked(slurm.requestGpus);
    m_gpusBox->setValue(qMax(1, slurm.gpusPerNode));
    m_spackRootEdit->setText(slurm.spackRoot);

    // Parse existing scriptTemplate directives so all 16 form fields stay synchronized.
    if (!slurm.scriptTemplate.trimmed().isEmpty()) {
        parseSlurmBatchScript(slurm.scriptTemplate);
    } else {
        m_templateEdit->setPlainText(generateSlurmBatchScript());
    }
}

/**
 * @brief Loads a SLURM `.sbatch`/`.slurm` file from disk and auto-populates all SLURM fields.
 * @param filePath Path to the batch script on disk.
 * @return True if the file was read and at least one SLURM directive or module was parsed.
 */
bool HpcIntegrationSettingsPage::loadSlurmBatchScriptFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    // Read UTF-8 script text, record the loaded path, and parse all #SBATCH directives.
    const QString content = QString::fromUtf8(file.readAll());
    m_sbatchFilePathEdit->setText(filePath);
    return parseSlurmBatchScript(content);
}

/**
 * @brief Parses `#SBATCH` directives, `module load` statements, and MPI launcher lines.
 *
 * Supports both long-form `#SBATCH --key=value` / `#SBATCH --key value` directives
 * and short-form flags (`-J`, `-A`, `-p`, `-q`, `-N`, `-n`, `-t`, `-C`, `-o`), plus
 * GPU GRES strings (`--gres=gpu:a100:2` or `--gres=gpu:4`), `module load ...` lines,
 * and MPI job launchers (`srun`, `mpirun`, `mpiexec`, `srun --mpi=pmix`).
 *
 * @param scriptText Raw SLURM batch script contents.
 * @return True if at least one SLURM directive, module load, or launcher was detected.
 */
bool HpcIntegrationSettingsPage::parseSlurmBatchScript(const QString& scriptText) {
    if (scriptText.trimmed().isEmpty()) {
        return false;
    }
    // Mirror the full batch script into the template preview box.
    m_templateEdit->setPlainText(scriptText);

    // Match both `#SBATCH --flag=value` / `#SBATCH --flag value` and short flags (`-J`, `-A`, `-p`, `-q`, `-N`, `-t`, `-o`, `-C`).
    static const QRegularExpression sbatchRx(
        QStringLiteral("^\\s*#SBATCH\\s+(?:--([a-zA-Z0-9_-]+)(?:=|\\s+)(\\S+)|-([JApqNntCo])\\s*(\\S+))"));
    static const QRegularExpression moduleRx(
        QStringLiteral("^\\s*module\\s+load\\s+(.+)$"));
    static const QRegularExpression launcherRx(
        QStringLiteral("^\\s*(srun\\s+--mpi=pmix|srun|mpirun|mpiexec)\\b"));

    QStringList loadedModules;
    bool matchedAny = false;

    // Scan each line of the batch script in order.
    const QStringList lines = scriptText.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        const QRegularExpressionMatch sbMatch = sbatchRx.match(line);
        if (sbMatch.hasMatch()) {
            matchedAny = true;
            QString key = sbMatch.captured(1).toLower();
            QString val = sbMatch.captured(2).trimmed();
            // Translate single-letter short SLURM flags into canonical long directive names.
            if (key.isEmpty()) {
                const QString shortFlag = sbMatch.captured(3);
                val = sbMatch.captured(4).trimmed();
                if (shortFlag == QLatin1String("J")) key = QStringLiteral("job-name");
                else if (shortFlag == QLatin1String("A")) key = QStringLiteral("account");
                else if (shortFlag == QLatin1String("p")) key = QStringLiteral("partition");
                else if (shortFlag == QLatin1String("q")) key = QStringLiteral("qos");
                else if (shortFlag == QLatin1String("N")) key = QStringLiteral("nodes");
                else if (shortFlag == QLatin1String("n")) key = QStringLiteral("ntasks-per-node");
                else if (shortFlag == QLatin1String("t")) key = QStringLiteral("time");
                else if (shortFlag == QLatin1String("C")) key = QStringLiteral("constraint");
                else if (shortFlag == QLatin1String("o")) key = QStringLiteral("output");
            }
            // Strip surrounding single or double quotes if present on the directive value.
            if (val.size() >= 2 && ((val.startsWith(QLatin1Char('"')) && val.endsWith(QLatin1Char('"'))) ||
                                    (val.startsWith(QLatin1Char('\'')) && val.endsWith(QLatin1Char('\''))))) {
                val = val.mid(1, val.size() - 2);
            }

            // Dispatch parsed directive value to the corresponding UI control.
            if (key == QLatin1String("job-name")) {
                m_jobNameEdit->setText(val);
            } else if (key == QLatin1String("account")) {
                m_accountEdit->setText(val);
            } else if (key == QLatin1String("partition")) {
                setComboTextValue(m_partitionCombo, val);
            } else if (key == QLatin1String("qos")) {
                setComboTextValue(m_qosCombo, val);
            } else if (key == QLatin1String("time")) {
                setComboTextValue(m_timeCombo, val);
            } else if (key == QLatin1String("nodes")) {
                m_nodesBox->setValue(qMax(1, val.toInt()));
            } else if (key == QLatin1String("ntasks-per-node") || key == QLatin1String("ntasks")) {
                m_tasksBox->setValue(qMax(1, val.toInt()));
            } else if (key == QLatin1String("cpus-per-task")) {
                m_cpusPerTaskBox->setValue(qMax(1, val.toInt()));
            } else if (key == QLatin1String("mem") || key == QLatin1String("mem-per-cpu")) {
                setComboTextValue(m_memCombo, val);
            } else if (key == QLatin1String("gres") || key == QLatin1String("gpus") ||
                       key == QLatin1String("gpus-per-node")) {
                // Parse `gpu:a100:2`, `gpu:4`, or `a100:2` into GPU type and count controls.
                QString gres = val;
                if (gres.startsWith(QStringLiteral("gpu:"), Qt::CaseInsensitive)) {
                    gres = gres.mid(4);
                }
                const QStringList parts = gres.split(QLatin1Char(':'), Qt::SkipEmptyParts);
                m_gpuCheck->setChecked(true);
                if (parts.size() >= 2) {
                    m_gpuTypeEdit->setText(parts[0]);
                    m_gpusBox->setValue(qMax(1, parts[1].toInt()));
                } else if (parts.size() == 1) {
                    bool ok = false;
                    const int count = parts[0].toInt(&ok);
                    if (ok) {
                        m_gpuTypeEdit->clear();
                        m_gpusBox->setValue(qMax(1, count));
                    } else {
                        m_gpuTypeEdit->setText(parts[0]);
                        m_gpusBox->setValue(1);
                    }
                }
            } else if (key == QLatin1String("constraint")) {
                m_constraintEdit->setText(val);
            } else if (key == QLatin1String("output")) {
                m_outputLogEdit->setText(val);
            } else if (key == QLatin1String("mail-type")) {
                setComboTextValue(m_mailTypeCombo, val);
            } else if (key == QLatin1String("mail-user")) {
                m_mailUserEdit->setText(val);
            }
            continue;
        }

        // Extract `module load <pkg1> <pkg2>` statements and accumulate module names.
        const QRegularExpressionMatch modMatch = moduleRx.match(line);
        if (modMatch.hasMatch()) {
            matchedAny = true;
            loadedModules.append(modMatch.captured(1).trimmed());
            continue;
        }

        // Detect MPI launcher command (`srun`, `mpirun`, `mpiexec`, `srun --mpi=pmix`).
        if (!line.startsWith(QLatin1Char('#'))) {
            const QRegularExpressionMatch launchMatch = launcherRx.match(line);
            if (launchMatch.hasMatch()) {
                matchedAny = true;
                setComboTextValue(m_mpiLauncherCombo, launchMatch.captured(1).trimmed());
            }
        }
    }

    // Join all discovered module load arguments into the Modules field.
    if (!loadedModules.isEmpty()) {
        m_modulesEdit->setText(loadedModules.join(QLatin1Char(' ')));
    }
    return matchedAny;
}

/**
 * @brief Generates a complete SLURM batch script from the current 16 form fields.
 * @return Formatted `#SBATCH` batch script string ready for job submission.
 */
QString HpcIntegrationSettingsPage::generateSlurmBatchScript() const {
    QStringList out;
    out << QStringLiteral("#!/bin/bash");
    // Emit job name and account directives when non-empty.
    if (!m_jobNameEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --job-name=%1").arg(m_jobNameEdit->text().trimmed());
    }
    if (!m_accountEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --account=%1").arg(m_accountEdit->text().trimmed());
    }
    // Emit partition, QoS, and wall-clock time limit directives.
    if (!m_partitionCombo->currentText().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --partition=%1").arg(m_partitionCombo->currentText().trimmed());
    }
    const QString qos = m_qosCombo->currentText().trimmed();
    if (!qos.isEmpty() && qos != QStringLiteral("(default)")) {
        out << QStringLiteral("#SBATCH --qos=%1").arg(qos);
    }
    if (!m_timeCombo->currentText().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --time=%1").arg(m_timeCombo->currentText().trimmed());
    }
    // Emit node, MPI task, CPU thread, and memory allocation directives.
    out << QStringLiteral("#SBATCH --nodes=%1").arg(m_nodesBox->value());
    out << QStringLiteral("#SBATCH --ntasks-per-node=%1").arg(m_tasksBox->value());
    out << QStringLiteral("#SBATCH --cpus-per-task=%1").arg(m_cpusPerTaskBox->value());
    if (!m_memCombo->currentText().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --mem=%1").arg(m_memCombo->currentText().trimmed().section(QLatin1Char(' '), 0, 0));
    }
    // Emit GPU GRES and node hardware constraint directives when requested.
    if (m_gpuCheck->isChecked()) {
        const QString gType = m_gpuTypeEdit->text().trimmed();
        out << (gType.isEmpty()
                    ? QStringLiteral("#SBATCH --gres=gpu:%1").arg(m_gpusBox->value())
                    : QStringLiteral("#SBATCH --gres=gpu:%1:%2").arg(gType).arg(m_gpusBox->value()));
    }
    if (!m_constraintEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --constraint=%1").arg(m_constraintEdit->text().trimmed());
    }
    // Emit log file output and email notification directives.
    if (!m_outputLogEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --output=%1").arg(m_outputLogEdit->text().trimmed());
    }
    const QString mailType = m_mailTypeCombo->currentText().trimmed();
    if (!mailType.isEmpty() && mailType != QStringLiteral("NONE")) {
        out << QStringLiteral("#SBATCH --mail-type=%1").arg(mailType);
    }
    if (!m_mailUserEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --mail-user=%1").arg(m_mailUserEdit->text().trimmed());
    }
    out << QString();
    // Emit environment module loading and MPI job launch line.
    if (!m_modulesEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("module load %1").arg(m_modulesEdit->text().trimmed());
    }
    const QString launcher = m_mpiLauncherCombo->currentText().trimmed().isEmpty()
                                 ? QStringLiteral("srun")
                                 : m_mpiLauncherCombo->currentText().trimmed();
    out << QStringLiteral("%1 {FILE}").arg(launcher);
    return out.join(QLatin1Char('\n'));
}

/**
 * @brief Returns the string value of any of the 16 SLURM fields by key.
 * @param key Canonical directive identifier (e.g., `"job_name"`, `"gres_gpu"`).
 * @return Trimmed string representation of the field value.
 */
QString HpcIntegrationSettingsPage::slurmFieldValue(const QString& key) const {
    const QString k = key.trimmed().toLower();
    // Lookup string and numeric directive controls by normalized key.
    if (k == QLatin1String("job_name")) return m_jobNameEdit->text().trimmed();
    if (k == QLatin1String("account")) return m_accountEdit->text().trimmed();
    if (k == QLatin1String("partition")) return m_partitionCombo->currentText().trimmed();
    if (k == QLatin1String("qos")) return m_qosCombo->currentText().trimmed();
    if (k == QLatin1String("time")) return m_timeCombo->currentText().trimmed();
    if (k == QLatin1String("nodes")) return QString::number(m_nodesBox->value());
    if (k == QLatin1String("ntasks_per_node")) return QString::number(m_tasksBox->value());
    if (k == QLatin1String("cpus_per_task")) return QString::number(m_cpusPerTaskBox->value());
    if (k == QLatin1String("mem")) return m_memCombo->currentText().trimmed();
    if (k == QLatin1String("gres_gpu")) {
        // Format GPU GRES as `<type>:<count>` when a GPU model is set, else `<count>`.
        if (!m_gpuCheck->isChecked()) return QString();
        const QString gType = m_gpuTypeEdit->text().trimmed();
        return gType.isEmpty() ? QString::number(m_gpusBox->value())
                               : QStringLiteral("%1:%2").arg(gType).arg(m_gpusBox->value());
    }
    if (k == QLatin1String("constraint")) return m_constraintEdit->text().trimmed();
    if (k == QLatin1String("mpi_launcher")) return m_mpiLauncherCombo->currentText().trimmed();
    if (k == QLatin1String("modules")) return m_modulesEdit->text().trimmed();
    if (k == QLatin1String("output")) return m_outputLogEdit->text().trimmed();
    if (k == QLatin1String("mail_type")) return m_mailTypeCombo->currentText().trimmed();
    if (k == QLatin1String("mail_user")) return m_mailUserEdit->text().trimmed();
    return QString();
}

}  // namespace gridlock::ui
