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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <pwd.h>
#include <unistd.h>

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
    resize(640, 360);
    setStyleSheet(workstationDialogStyleSheet());

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);

    // Top filter strip with live substring filter box and 24px Refresh button.
    auto* filterRow = new QHBoxLayout();
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->setSpacing(6);
    auto* filterLabel = new QLabel(QStringLiteral("Filter Processes:"), this);
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setObjectName(QStringLiteral("attachProcessFilterEdit"));
    m_filterEdit->setPlaceholderText(QStringLiteral("Filter by PID, process name, user, or command line..."));
    m_filterEdit->setFixedHeight(24);
    m_refreshBtn = new QPushButton(QStringLiteral("Refresh"), this);
    m_refreshBtn->setObjectName(QStringLiteral("attachProcessRefreshBtn"));
    m_refreshBtn->setFixedHeight(24);
    filterRow->addWidget(filterLabel);
    filterRow->addWidget(m_filterEdit, 1);
    filterRow->addWidget(m_refreshBtn);
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
        QStringLiteral("PROCESS"),
        QStringLiteral("USER"),
        QStringLiteral("COMMAND")
    });
    m_processTable->horizontalHeader()->setVisible(true);
    m_processTable->horizontalHeader()->setMinimumSectionSize(48);
    m_processTable->horizontalHeader()->setStretchLastSection(true);
    m_processTable->setColumnWidth(0, 76);
    m_processTable->setColumnWidth(1, 130);
    m_processTable->setColumnWidth(2, 96);
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
    connect(m_refreshBtn, &QPushButton::clicked, this, &AttachProcessDialog::refreshProcesses);
    connect(m_processTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem*) { accept(); });
    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        filterProcesses(text);
    });

    // Populate initial table rows from live Linux `/proc` process descriptors.
    refreshProcesses();
    rootLayout->addWidget(buttons);
}

/**
 * @brief Scans `/proc/<pid>/status` and `/proc/<pid>/cmdline` for user-owned live OS processes.
 *
 * Uses `QFile::readAll()` because Linux procfs pseudo-files report `st_size == 0` in `stat()`,
 * which causes `QIODevice::atEnd()` to evaluate true before reading content. Filters processes
 * to those matching `geteuid()`, excludes GridLock's own GUI/test PID, and prioritizes MPI ranks.
 */
void AttachProcessDialog::refreshProcesses() {
    if (!m_processTable) return;
    // Query effective POSIX user ID, current application PID, and human-readable login name.
    const uid_t currentUid = ::geteuid();
    const qint64 selfPid = QCoreApplication::applicationPid();
    const struct passwd* pw = ::getpwuid(currentUid);
    const QString userName = (pw && pw->pw_name) ? QString::fromLocal8Bit(pw->pw_name)
                                                 : qEnvironmentVariable("USER", QString::number(currentUid));
    // Extract configured target binary basename so active project executables sort at the top.
    const QString targetBase = QFileInfo(QString::fromStdString(
        gridlock::core::ConfigManager::instance().loadProjectSettings().targetBinary)).fileName();

    // Lightweight value record representing one user-owned OS process discovered in `/proc`.
    struct ProcRow {
        qint64 pid{0};
        QString name;
        QString cmdline;
        bool priority{false};
    };
    QList<ProcRow> rows;

    // Enumerate numeric `/proc/<pid>` directories and inspect status + command-line metadata.
    const QDir procDir(QStringLiteral("/proc"));
    for (const QString& entry : procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool pidOk = false;
        const qint64 pid = entry.toLongLong(&pidOk);
        // Skip non-numeric procfs subdirectories (`net`, `sys`, `irq`) and our own process ID.
        if (!pidOk || pid <= 0 || pid == selfPid) continue;

        // Read `/proc/<pid>/status` in full via `readAll()` to extract `Name:` and `Uid:` fields.
        QFile statusFile(QStringLiteral("/proc/%1/status").arg(pid));
        if (!statusFile.open(QIODevice::ReadOnly)) continue;
        const QByteArray statusBytes = statusFile.readAll();
        if (statusBytes.isEmpty()) continue;

        QString procName;
        bool uidMatched = false;
        // Parse newline-delimited procfs status headers until both `Name:` and `Uid:` are resolved.
        for (const QByteArray& rawLine : statusBytes.split('\n')) {
            const QString line = QString::fromUtf8(rawLine).trimmed();
            if (line.startsWith(QStringLiteral("Name:"))) {
                procName = line.mid(5).trimmed();
            } else if (line.startsWith(QStringLiteral("Uid:"))) {
                // `Uid:` lists Real, Effective, Saved-set, and Filesystem UIDs separated by tabs.
                const QStringList parts = line.mid(4).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                if (!parts.isEmpty()) {
                    const uid_t realUid = static_cast<uid_t>(parts[0].toUInt());
                    const uid_t effUid = parts.size() > 1 ? static_cast<uid_t>(parts[1].toUInt()) : realUid;
                    uidMatched = (realUid == currentUid || effUid == currentUid);
                }
                break;
            }
        }
        // Exclude processes owned by other system accounts as well as any running `gridlock` binary.
        if (!uidMatched || procName.isEmpty() || procName.compare(QStringLiteral("gridlock"), Qt::CaseInsensitive) == 0) {
            continue;
        }

        // Convert NUL-delimited `/proc/<pid>/cmdline` argv tokens into a space-delimited command string.
        QString cmdText;
        QFile cmdFile(QStringLiteral("/proc/%1/cmdline").arg(pid));
        if (cmdFile.open(QIODevice::ReadOnly)) {
            QByteArray raw = cmdFile.readAll();
            for (char& ch : raw) { if (ch == '\0') ch = ' '; }
            cmdText = QString::fromUtf8(raw).trimmed();
        }
        // Kernel threads or zombies without user-space argv fall back to bracketed `[Name]`.
        if (cmdText.isEmpty()) cmdText = QStringLiteral("[%1]").arg(procName);

        // Flag MPI job launchers, OpenMPI/PRTE daemons, SLURM steps, and local target binaries.
        const bool isPriority =
            procName.contains(QStringLiteral("mpi"), Qt::CaseInsensitive) ||
            procName.contains(QStringLiteral("orted"), Qt::CaseInsensitive) ||
            procName.contains(QStringLiteral("prte"), Qt::CaseInsensitive) ||
            procName.contains(QStringLiteral("srun"), Qt::CaseInsensitive) ||
            cmdText.contains(QStringLiteral("mpirun"), Qt::CaseInsensitive) ||
            cmdText.contains(QStringLiteral("mpiexec"), Qt::CaseInsensitive) ||
            cmdText.contains(QStringLiteral("tutorial/"), Qt::CaseInsensitive) ||
            (!targetBase.isEmpty() && procName == targetBase);
        rows.push_back(ProcRow{pid, procName, cmdText, isPriority});
    }

    // Order MPI / target executables ahead of general desktop processes, then newest PIDs first.
    std::sort(rows.begin(), rows.end(), [](const ProcRow& a, const ProcRow& b) {
        return (a.priority != b.priority) ? (a.priority > b.priority) : (a.pid > b.pid);
    });

    // Populate the 4-column QTableWidget (`PID`, `PROCESS`, `USER`, `COMMAND`) and highlight priority rows.
    m_processTable->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        m_processTable->setItem(i, 0, new QTableWidgetItem(QString::number(rows[i].pid)));
        auto* nameItem = new QTableWidgetItem(rows[i].name);
        if (rows[i].priority) nameItem->setForeground(QColor(QStringLiteral("#61afef")));
        m_processTable->setItem(i, 1, nameItem);
        m_processTable->setItem(i, 2, new QTableWidgetItem(userName));
        m_processTable->setItem(i, 3, new QTableWidgetItem(rows[i].cmdline));
    }
    // Re-apply active substring filter when refreshing while a user search query is present.
    if (m_filterEdit && !m_filterEdit->text().isEmpty()) filterProcesses(m_filterEdit->text());
    // Default-select the first visible process row so clicking Attach immediately targets a valid PID.
    for (int r = 0; r < m_processTable->rowCount(); ++r) {
        if (!m_processTable->isRowHidden(r)) { m_processTable->selectRow(r); break; }
    }
}

/**
 * @brief Returns the currently selected OS process ID, or -1 if none is selected.
 */
qint64 AttachProcessDialog::selectedPid() const {
    if (!m_processTable || m_processTable->currentRow() < 0) {
        return -1;
    }
    if (const QTableWidgetItem* pidItem = m_processTable->item(m_processTable->currentRow(), 0)) {
        bool ok = false;
        const qint64 pid = pidItem->text().toLongLong(&ok);
        return ok ? pid : -1;
    }
    return -1;
}

/**
 * @brief Filters visible process rows matching the query string.
 * @param filterText Substring matched against PID, process name, user, or command line.
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
