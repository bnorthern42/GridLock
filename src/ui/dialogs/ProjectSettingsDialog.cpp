/**
 * @file ProjectSettingsDialog.cpp
 * @brief Implementation of project configuration, process attach, new project, and about modal dialogs.
 *
 * Implements dark-slate workstation modal dialogs (ProjectSettingsDialog,
 * AttachProcessDialog, NewProjectDialog, and AboutDialog) with #16181d dialog
 * chrome, #1b1e24 bordered content cards, 24px-high action buttons and inputs,
 * and borderless QFrame::NoFrame process tables.
 */

#include "ProjectSettingsDialog.hpp"
#include "../../core/managers/ConfigManager.hpp"

#include <QAbstractItemView>
#include <QColor>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace gridlock::ui {

namespace {

/**
 * @brief Returns the shared dark-slate workstation stylesheet for modal dialogs.
 * @return QSS stylesheet string enforcing #16181d background, #1b1e24 cards, and 24px controls.
 */
QString workstationDialogStyleSheet() {
    return QStringLiteral(
        "QDialog { background: #16181d; color: #d7dae0; }"
        "QGroupBox { background: #1b1e24; border: 1px solid #282c34; border-radius: 3px; "
        "margin-top: 8px; padding: 10px 8px 8px 8px; color: #d7dae0; font-weight: 600; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; "
        "padding: 0px 6px; color: #61afef; }"
        "QLabel { color: #abb2bf; font-size: 11px; }"
        "QLineEdit, QComboBox { background: #181a1f; color: #d7dae0; border: 1px solid #323844; "
        "border-radius: 2px; padding: 1px 6px; min-height: 20px; max-height: 24px; "
        "font-family: 'JetBrains Mono', monospace; font-size: 11px; }"
        "QPushButton { background: #282c34; color: #d7dae0; border: 1px solid #3b4048; "
        "border-radius: 2px; padding: 2px 10px; min-height: 22px; max-height: 24px; font-size: 11px; }"
        "QPushButton:hover { background: #323842; color: #ffffff; border: 1px solid #528bff; }");
}

}  // namespace

// ============================================================================
// ProjectSettingsDialog Implementation
// ============================================================================

/**
 * @brief Constructs and presents the project settings configuration dialog.
 * @param parent Optional parent widget.
 */
ProjectSettingsDialog::ProjectSettingsDialog(QWidget* parent) : QDialog(parent) {
    // Configure window title, dark-slate chrome, and workstation modal geometry.
    setWindowTitle(QStringLiteral("Project Settings"));
    setMinimumSize(460, 250);
    resize(480, 260);
    setStyleSheet(workstationDialogStyleSheet());

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);

    // GroupBox card wrapping target binary, arguments, and working directory fields.
    auto* card = new QGroupBox(QStringLiteral("Target Execution Configuration"), this);
    auto* form = new QFormLayout(card);
    form->setContentsMargins(10, 12, 10, 10);
    form->setSpacing(8);

    m_targetBinary = new QLineEdit(card);
    m_targetBinary->setFixedHeight(24);
    m_binaryArguments = new QLineEdit(card);
    m_binaryArguments->setFixedHeight(24);
    m_workingDirectory = new QLineEdit(card);
    m_workingDirectory->setFixedHeight(24);

    // Mount labelled form rows for each configurable parameter.
    form->addRow(QStringLiteral("Target Binary:"), m_targetBinary);
    form->addRow(QStringLiteral("Binary Arguments:"), m_binaryArguments);
    form->addRow(QStringLiteral("Working Directory:"), m_workingDirectory);
    rootLayout->addWidget(card, 1);

    // Construct 24px-high Ok/Cancel dialog button strip.
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    for (QAbstractButton* btn : buttons->buttons()) {
        btn->setFixedHeight(24);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);

    // Pre-populate fields from active ConfigManager project settings state.
    const auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
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

// ============================================================================
// AttachProcessDialog Implementation
// ============================================================================

/**
 * @brief Constructs the attach process dialog with live filter bar and MPI rank table.
 * @param parent Optional parent widget.
 */
AttachProcessDialog::AttachProcessDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Attach to MPI / Local Process"));
    setMinimumSize(560, 320);
    resize(620, 360);
    setStyleSheet(workstationDialogStyleSheet());

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);

    // Top filter strip inside a compact dark-slate header bar.
    auto* filterRow = new QHBoxLayout();
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->setSpacing(6);
    auto* filterLabel = new QLabel(QStringLiteral("Filter Processes:"), this);
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setObjectName(QStringLiteral("attachProcessFilterEdit"));
    m_filterEdit->setPlaceholderText(QStringLiteral("Filter by PID, MPI rank, state, or binary command..."));
    m_filterEdit->setFixedHeight(24);
    filterRow->addWidget(filterLabel);
    filterRow->addWidget(m_filterEdit, 1);
    rootLayout->addLayout(filterRow);

    // 4-column borderless process table with alternating row colors and >= 48px section sizes.
    m_processTable = new QTableWidget(0, 4, this);
    m_processTable->setObjectName(QStringLiteral("attachProcessTable"));
    m_processTable->setFrameShape(QFrame::NoFrame);
    m_processTable->setAlternatingRowColors(true);
    m_processTable->verticalHeader()->setVisible(false);
    m_processTable->verticalHeader()->setDefaultSectionSize(22);
    m_processTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_processTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_processTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_processTable->setHorizontalHeaderLabels({
        QStringLiteral("PID"),
        QStringLiteral("RANK"),
        QStringLiteral("STATE"),
        QStringLiteral("COMMAND")
    });
    m_processTable->horizontalHeader()->setVisible(true);
    m_processTable->horizontalHeader()->setMinimumSectionSize(48);
    m_processTable->horizontalHeader()->setStretchLastSection(true);
    m_processTable->setColumnWidth(0, 72);
    m_processTable->setColumnWidth(1, 72);
    m_processTable->setColumnWidth(2, 110);

    // Seed structured MPI cluster process rows so the table never renders a blank void.
    struct ProcessEntry {
        const char* pid;
        const char* rank;
        const char* state;
        const char* cmd;
    };
    const ProcessEntry seedRows[] = {
        {"4102", "Rank 0", "RUNNING (MPI_Send)", "mpirun -np 4 ./mpi_navier_stokes --grid 512"},
        {"4103", "Rank 1", "STOPPED (MPI_Recv)", "./mpi_navier_stokes --grid 512"},
        {"4104", "Rank 2", "WAITING (MPI_Barrier)", "./mpi_navier_stokes --grid 512"},
        {"4105", "Rank 3", "RUNNING (MPI_Allreduce)", "./mpi_navier_stokes --grid 512"},
    };
    m_processTable->setRowCount(4);
    for (int i = 0; i < 4; ++i) {
        m_processTable->setItem(i, 0, new QTableWidgetItem(QString::fromLatin1(seedRows[i].pid)));
        m_processTable->setItem(i, 1, new QTableWidgetItem(QString::fromLatin1(seedRows[i].rank)));
        auto* stateItem = new QTableWidgetItem(QString::fromLatin1(seedRows[i].state));
        stateItem->setForeground(QColor(QStringLiteral("#61afef")));
        m_processTable->setItem(i, 2, stateItem);
        m_processTable->setItem(i, 3, new QTableWidgetItem(QString::fromLatin1(seedRows[i].cmd)));
    }
    m_processTable->selectRow(0);
    rootLayout->addWidget(m_processTable, 1);

    // Footer action buttons (Attach / Cancel) capped at 24px height.
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (QPushButton* okBtn = buttons->button(QDialogButtonBox::Ok)) {
        okBtn->setText(QStringLiteral("Attach"));
    }
    for (QAbstractButton* btn : buttons->buttons()) {
        btn->setFixedHeight(24);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        filterProcesses(text);
    });
    rootLayout->addWidget(buttons);
}

/**
 * @brief Returns the currently selected process ID, or -1 if none is selected.
 */
int AttachProcessDialog::selectedPid() const {
    if (!m_processTable || m_processTable->currentRow() < 0) {
        return -1;
    }
    if (const QTableWidgetItem* pidItem = m_processTable->item(m_processTable->currentRow(), 0)) {
        return pidItem->text().toInt();
    }
    return -1;
}

/**
 * @brief Filters visible process rows matching the query string.
 * @param filterText Substring matched against PID, rank, state, or command.
 */
void AttachProcessDialog::filterProcesses(const QString& filterText) {
    if (!m_processTable) {
        return;
    }
    const QString query = filterText.trimmed();
    for (int row = 0; row < m_processTable->rowCount(); ++row) {
        bool match = query.isEmpty();
        for (int col = 0; col < m_processTable->columnCount() && !match; ++col) {
            if (const QTableWidgetItem* it = m_processTable->item(row, col)) {
                if (it->text().contains(query, Qt::CaseInsensitive)) {
                    match = true;
                }
            }
        }
        m_processTable->setRowHidden(row, !match);
    }
}

// ============================================================================
// NewProjectDialog Implementation
// ============================================================================

/**
 * @brief Constructs the streamlined dark-slate New Project modal dialog.
 * @param parent Optional parent widget.
 */
NewProjectDialog::NewProjectDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("New MPI Project"));
    setMinimumSize(460, 260);
    resize(500, 280);
    setStyleSheet(workstationDialogStyleSheet());

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);

    // Card container for project workspace and MPI launcher settings.
    auto* card = new QGroupBox(QStringLiteral("Workspace & MPI Target Configuration"), this);
    auto* form = new QFormLayout(card);
    form->setContentsMargins(10, 12, 10, 10);
    form->setSpacing(8);

    m_projectNameEdit = new QLineEdit(QStringLiteral("mpi_cluster_target"), card);
    m_projectNameEdit->setFixedHeight(24);
    m_workspaceDirEdit = new QLineEdit(QStringLiteral("/tmp/gridlock_workspace"), card);
    m_workspaceDirEdit->setFixedHeight(24);
    m_targetBinaryEdit = new QLineEdit(QStringLiteral("./build/mpi_target"), card);
    m_targetBinaryEdit->setFixedHeight(24);

    m_launcherCombo = new QComboBox(card);
    m_launcherCombo->addItems({QStringLiteral("mpirun"), QStringLiteral("mpiexec"), QStringLiteral("srun")});
    m_launcherCombo->setFixedHeight(24);

    form->addRow(QStringLiteral("Project Name:"), m_projectNameEdit);
    form->addRow(QStringLiteral("Workspace Path:"), m_workspaceDirEdit);
    form->addRow(QStringLiteral("Target Binary:"), m_targetBinaryEdit);
    form->addRow(QStringLiteral("MPI Launcher:"), m_launcherCombo);
    rootLayout->addWidget(card, 1);

    // Footer buttons capped at 24px height.
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (QPushButton* okBtn = buttons->button(QDialogButtonBox::Ok)) {
        okBtn->setText(QStringLiteral("Create Project"));
    }
    for (QAbstractButton* btn : buttons->buttons()) {
        btn->setFixedHeight(24);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);
}

QString NewProjectDialog::projectName() const {
    return m_projectNameEdit ? m_projectNameEdit->text().trimmed() : QString();
}

QString NewProjectDialog::workspaceDirectory() const {
    return m_workspaceDirEdit ? m_workspaceDirEdit->text().trimmed() : QString();
}

QString NewProjectDialog::targetExecutable() const {
    return m_targetBinaryEdit ? m_targetBinaryEdit->text().trimmed() : QString();
}

// ============================================================================
// AboutDialog Implementation
// ============================================================================

/**
 * @brief Constructs the dark-slate About GridLock workstation dialog.
 * @param parent Optional parent widget.
 */
AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("About GridLock Workstation"));
    setMinimumSize(440, 240);
    resize(460, 260);
    setStyleSheet(workstationDialogStyleSheet());

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);

    // Dark-slate metadata card summarizing workstation architecture.
    auto* card = new QGroupBox(QStringLiteral("GridLock Multi-Rank HPC & Binary Workstation"), this);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(10, 12, 10, 10);
    cardLayout->setSpacing(6);

    const QString appVer = QCoreApplication::applicationVersion().isEmpty()
                               ? QStringLiteral("0.6.2")
                               : QCoreApplication::applicationVersion();
    auto* versionLabel = new QLabel(
        QStringLiteral("<b>Version:</b> %1 &nbsp;|&nbsp; <b>ABI:</b> x86_64 ELF64 / System V")
            .arg(appVer),
        card);
    auto* summaryLabel = new QLabel(
        QStringLiteral("Synchronized multi-rank GDB/MI & DAP parallel debugger, static ELF64 binary "
                       "inspector, interactive CFG basic-block graph, and Wait-For Graph deadlock engine."),
        card);
    summaryLabel->setWordWrap(true);

    cardLayout->addWidget(versionLabel);
    cardLayout->addWidget(summaryLabel);
    cardLayout->addStretch(1);
    rootLayout->addWidget(card, 1);

    // 24px Close button strip.
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    for (QAbstractButton* btn : buttons->buttons()) {
        btn->setFixedHeight(24);
    }
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    rootLayout->addWidget(buttons);
}

}  // namespace gridlock::ui
