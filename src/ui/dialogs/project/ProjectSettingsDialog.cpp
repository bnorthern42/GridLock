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
#include "../calculator/ProgrammerCalculatorDialog.hpp"
#include "../../utils/FileDialogHelper.hpp"
#include "../../../core/managers/ConfigManager.hpp"

#include <QAbstractItemView>
#include <QColor>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
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
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
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

namespace {

/**
 * @brief Checks whether @p filePath is a runnable user ELF executable.
 * @param filePath Candidate file path on disk.
 * @return True if executable non-shared ELF with valid magic bytes.
 */
bool isRunnableElfCandidate(const QString& filePath) {
    // Reject empty strings before inspecting filesystem metadata.
    if (filePath.isEmpty()) return false;
    const QString lower = filePath.toLower();
    // Skip CMake/Meson internal probe binaries, shared libraries, objects, and split debuginfo files.
    if (lower.contains(QStringLiteral("/meson-private/")) || lower.contains(QStringLiteral("/cmakefiles/")) ||
        lower.contains(QStringLiteral("sanitycheck")) || lower.endsWith(QStringLiteral(".o")) ||
        lower.endsWith(QStringLiteral(".a")) || lower.endsWith(QStringLiteral(".so")) ||
        lower.contains(QStringLiteral(".so.")) || lower.endsWith(QStringLiteral(".debug"))) {
        return false;
    }
    // Require a regular executable file of at least 4 bytes so the ELF magic header can be read.
    const QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile() || !fi.isExecutable() || fi.size() < 4) return false;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    // Verify the standard 4-byte `\x7fELF` header signature.
    const QByteArray magic = f.read(4);
    return magic.size() == 4 && magic[0] == '\x7f' && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}

/**
 * @brief Collects deduplicated ELF launch targets from toolbar state and workspace build folders.
 * @param wsRoot Active workspace root directory path.
 * @param toolbarTargetCombo Optional MainWindow `#toolbarTargetSelector` combo box.
 * @return Ordered list of absolute paths to discovered runnable executables.
 */
QStringList discoverProjectBinaries(const QString& wsRoot, QComboBox* toolbarTargetCombo) {
    QStringList ordered;
    QSet<QString> seen;
    // Deduplicate candidate paths using canonical path keys so symlinks map to one entry.
    auto addCandidate = [&](const QString& path) {
        if (!isRunnableElfCandidate(path)) return;
        const QString absPath = QFileInfo(path).absoluteFilePath();
        const QString canon = QFileInfo(absPath).canonicalFilePath();
        const QString key = canon.isEmpty() ? absPath : canon;
        if (seen.contains(key)) return;
        seen.insert(key);
        ordered << absPath;
    };
    // Preserve active toolbar selector target first so opening Project Settings keeps current selection.
    if (toolbarTargetCombo) {
        const int cur = toolbarTargetCombo->currentIndex();
        if (cur >= 0) addCandidate(toolbarTargetCombo->itemData(cur).toString());
        // Include remaining toolbar executables ahead of filesystem discovery results.
        for (int i = 0; i < toolbarTargetCombo->count(); ++i) {
            const QString data = toolbarTargetCombo->itemData(i).toString();
            if (data != QStringLiteral("__open_binary__")) addCandidate(data);
        }
    }
    // Stop early when no valid workspace root directory is active.
    if (wsRoot.isEmpty() || !QFileInfo::exists(wsRoot)) return ordered;
    const QDir rootDir(wsRoot);
    // Scan top-level workspace executables directly before descending into build folders.
    for (const QFileInfo& fi : rootDir.entryInfoList(QDir::Files | QDir::Executable | QDir::NoDotAndDotDot)) {
        addCandidate(fi.absoluteFilePath());
    }
    // Search standard Meson, CMake, and custom output directories recursively.
    const QStringList subDirs = {QStringLiteral("build_meson"), QStringLiteral("build_cmake"),
                                 QStringLiteral("builddir"), QStringLiteral("build"),
                                 QStringLiteral("bin"), QStringLiteral("src")};
    QStringList discovered;
    for (const QString& sub : subDirs) {
        const QString subPath = rootDir.filePath(sub);
        if (!QFileInfo(subPath).isDir()) continue;
        // Walk each build directory subtree for compiled ELF binaries.
        QDirIterator it(subPath, QDir::Files | QDir::Executable | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString candidate = it.next();
            if (isRunnableElfCandidate(candidate)) discovered << QFileInfo(candidate).absoluteFilePath();
        }
    }
    // Sort discovered workspace executables case-insensitively before appending.
    discovered.sort(Qt::CaseInsensitive);
    for (const QString& bin : discovered) addCandidate(bin);
    return ordered;
}

} // namespace

/**
 * @brief Constructs and presents the project settings configuration dialog.
 * @param parent Optional parent widget.
 */
ProjectSettingsDialog::ProjectSettingsDialog(QWidget* parent) : QDialog(parent) {
    // Configure window title, dark-slate chrome, and spacious workstation modal geometry.
    setWindowTitle(QStringLiteral("Project Settings"));
    setMinimumSize(540, 290);
    resize(600, 310);
    setStyleSheet(workstationDialogStyleSheet());

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);

    // GroupBox card wrapping target binary, CLI args, working directory, and MPI execution settings.
    auto* card = new QGroupBox(QStringLiteral("Target Execution Configuration"), this);
    auto* form = new QFormLayout(card);
    form->setContentsMargins(10, 12, 10, 10);
    form->setSpacing(8);

    // Target executable editable combo box paired with workspace-scoped Browse button.
    m_targetBinaryCombo = new QComboBox(card);
    m_targetBinaryCombo->setObjectName(QStringLiteral("targetBinaryCombo"));
    m_targetBinaryCombo->setEditable(true);
    m_targetBinaryCombo->setMinimumWidth(260);
    m_targetBinaryCombo->setFixedHeight(24);
    // Name the inner QLineEdit for direct UI testing and theme targeting.
    if (m_targetBinaryCombo->lineEdit()) {
        m_targetBinaryCombo->lineEdit()->setObjectName(QStringLiteral("targetBinaryEdit"));
    }
    // Browse button launching a workspace-rooted executable picker dialog.
    m_targetBrowseBtn = new QPushButton(QStringLiteral("Browse..."), card);
    m_targetBrowseBtn->setObjectName(QStringLiteral("targetBrowseBtn"));
    m_targetBrowseBtn->setFixedSize(88, 24);
    // Horizontal row grouping the editable target combo and its Browse trigger.
    auto* targetRow = new QHBoxLayout();
    targetRow->setContentsMargins(0, 0, 0, 0);
    targetRow->setSpacing(6);
    targetRow->addWidget(m_targetBinaryCombo, 1);
    targetRow->addWidget(m_targetBrowseBtn);

    // Command-line arguments forwarded to the inferior process upon launch.
    m_binaryArguments = new QLineEdit(card);
    m_binaryArguments->setObjectName(QStringLiteral("targetArgsEdit"));
    m_binaryArguments->setPlaceholderText(QStringLiteral("e.g., config.toml --threads 4"));
    m_binaryArguments->setFixedHeight(24);

    // Inferior working directory input paired with folder Browse button.
    m_workingDirectory = new QLineEdit(card);
    m_workingDirectory->setObjectName(QStringLiteral("workingDirEdit"));
    m_workingDirectory->setFixedHeight(24);
    // Folder chooser button for selecting the execution working directory.
    m_workingDirBrowseBtn = new QPushButton(QStringLiteral("Browse..."), card);
    m_workingDirBrowseBtn->setObjectName(QStringLiteral("workingDirBrowseBtn"));
    m_workingDirBrowseBtn->setFixedSize(88, 24);
    // Horizontal row pairing working directory input and folder chooser trigger.
    auto* wdRow = new QHBoxLayout();
    wdRow->setContentsMargins(0, 0, 0, 0);
    wdRow->setSpacing(6);
    wdRow->addWidget(m_workingDirectory, 1);
    wdRow->addWidget(m_workingDirBrowseBtn);

    // Per-project MPI rank count (-np) and launcher flags row.
    m_mpiRanksSpin = new QSpinBox(card);
    m_mpiRanksSpin->setObjectName(QStringLiteral("mpiRanksSpin"));
    m_mpiRanksSpin->setRange(1, 256);
    m_mpiRanksSpin->setFixedHeight(24);
    // Additional flags forwarded directly to mpirun/mpiexec (e.g., oversubscribe/binding).
    m_mpiArgsEdit = new QLineEdit(card);
    m_mpiArgsEdit->setObjectName(QStringLiteral("mpiArgsEdit"));
    m_mpiArgsEdit->setPlaceholderText(QStringLiteral("e.g., --oversubscribe --bind-to core"));
    m_mpiArgsEdit->setFixedHeight(24);
    // Inline row aligning MPI rank spinbox alongside optional mpirun flag input.
    auto* mpiRow = new QHBoxLayout();
    mpiRow->setContentsMargins(0, 0, 0, 0);
    mpiRow->setSpacing(6);
    mpiRow->addWidget(m_mpiRanksSpin);
    auto* mpiArgsLbl = new QLabel(QStringLiteral("MPI Args:"), card);
    mpiRow->addWidget(mpiArgsLbl);
    mpiRow->addWidget(m_mpiArgsEdit, 1);

    // Mount labelled form rows for each configurable execution parameter.
    form->addRow(QStringLiteral("Target Binary:"), targetRow);
    form->addRow(QStringLiteral("Program Arguments:"), m_binaryArguments);
    form->addRow(QStringLiteral("Working Directory:"), wdRow);
    form->addRow(QStringLiteral("MPI Ranks (-np):"), mpiRow);
    rootLayout->addWidget(card, 1);

    // Programmer's Calculator default configuration card (width, signed mode, and inline launcher).
    auto* calcCard = new QGroupBox(QStringLiteral("Programmer's Calculator"), this);
    auto* calcForm = new QFormLayout(calcCard);
    calcForm->setContentsMargins(10, 12, 10, 10);
    calcForm->setSpacing(6);
    m_calcWidthCombo = new QComboBox(calcCard);
    m_calcWidthCombo->setObjectName(QStringLiteral("calcDefaultWidthCombo"));
    m_calcWidthCombo->setFixedHeight(24);
    m_calcWidthCombo->addItem(QStringLiteral("64-bit (QWORD)"), 64);
    m_calcWidthCombo->addItem(QStringLiteral("32-bit (DWORD)"), 32);
    m_calcWidthCombo->addItem(QStringLiteral("16-bit (WORD)"), 16);
    m_calcWidthCombo->addItem(QStringLiteral("8-bit (BYTE)"), 8);
    m_calcSignedCheck = new QCheckBox(QStringLiteral("Default Signed Decimal Display"), calcCard);
    m_calcSignedCheck->setObjectName(QStringLiteral("calcDefaultSignedCheck"));
    m_openCalcBtn = new QPushButton(QStringLiteral("Open Programmer's Calculator..."), calcCard);
    m_openCalcBtn->setObjectName(QStringLiteral("openProgrammerCalculatorBtn"));
    m_openCalcBtn->setFixedHeight(24);
    QSettings calcSettings(QStringLiteral("gridlock"), QStringLiteral("debugger"));
    const int savedCalcWidth = calcSettings.value(QStringLiteral("tools/calcDefaultWidth"), 64).toInt();
    const int widthIdx = m_calcWidthCombo->findData(savedCalcWidth);
    if (widthIdx >= 0) m_calcWidthCombo->setCurrentIndex(widthIdx);
    m_calcSignedCheck->setChecked(calcSettings.value(QStringLiteral("tools/calcDefaultSigned"), false).toBool());
    connect(m_openCalcBtn, &QPushButton::clicked, this, [this]() {
        if (QWidget* pw = parentWidget()) {
            if (QMetaObject::invokeMethod(pw, "openProgrammerCalculator")) return;
        }
        auto* dlg = new ProgrammerCalculatorDialog(parentWidget());
        dlg->setAttribute(Qt::WA_DeleteOnClose, true);
        dlg->show();
    });
    auto* calcRow = new QHBoxLayout();
    calcRow->setContentsMargins(0, 0, 0, 0);
    calcRow->setSpacing(8);
    calcRow->addWidget(m_calcWidthCombo);
    calcRow->addWidget(m_calcSignedCheck, 1);
    calcRow->addWidget(m_openCalcBtn);
    calcForm->addRow(QStringLiteral("Default Width:"), calcRow);
    rootLayout->addWidget(calcCard);

    // Construct 24px-high Ok/Cancel dialog button strip.
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    for (QAbstractButton* btn : buttons->buttons()) {
        btn->setFixedHeight(24);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);

    // Resolve active workspace root, discovered ELFs, and persisted ConfigManager project settings.
    auto& cfg = gridlock::core::ConfigManager::instance();
    const auto ps = cfg.loadProjectSettings();
    const QString wsRoot = FileDialogHelper::resolveDefaultDirectory(cfg.workspacePath());
    const QString wsCanon = QFileInfo(wsRoot).canonicalFilePath();
    auto* parentTargetCombo = parent ? parent->findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector")) : nullptr;
    auto* parentRankSpin = parent ? parent->findChild<QSpinBox*>(QStringLiteral("toolbarRankSpin")) : nullptr;
    const QStringList discovered = discoverProjectBinaries(wsRoot, parentTargetCombo);
    for (const QString& bin : discovered) m_targetBinaryCombo->addItem(bin, bin);

    // Pre-select persisted workspace target or auto-fill with active toolbar / discovered ELF target.
    const QString savedTarget = QString::fromStdString(ps.targetBinary).trimmed();
    const QString savedCanon = QFileInfo(savedTarget).canonicalFilePath();
    const bool savedInWs = !savedCanon.isEmpty() && !wsCanon.isEmpty() &&
                           (savedCanon == wsCanon || savedCanon.startsWith(wsCanon + QLatin1Char('/')));
    // Honor saved target only if it exists and belongs to the active workspace (or no workspace discovery exists).
    if (!savedTarget.isEmpty() && QFileInfo::exists(savedTarget) &&
        (wsCanon.isEmpty() || savedInWs || m_targetBinaryCombo->count() == 0)) {
        int matchIdx = -1;
        // Locate matching candidate inside the pre-populated combo box by canonical path.
        for (int i = 0; i < m_targetBinaryCombo->count(); ++i) {
            if (QFileInfo(m_targetBinaryCombo->itemData(i).toString()).canonicalFilePath() == savedCanon) {
                matchIdx = i;
                break;
            }
        }
        // Prepend explicit saved path if it lies outside the auto-scanned build subdirectories.
        if (matchIdx < 0) {
            m_targetBinaryCombo->insertItem(0, savedTarget, savedTarget);
            matchIdx = 0;
        }
        m_targetBinaryCombo->setCurrentIndex(matchIdx);
    } else if (m_targetBinaryCombo->count() > 0) {
        // Default to active toolbar target or first discovered workspace executable.
        m_targetBinaryCombo->setCurrentIndex(0);
    } else {
        // Fall back to editable text when no runnable workspace binary is present yet.
        m_targetBinaryCombo->setCurrentText(savedTarget);
    }

    // Populate per-target program arguments, working directory (defaulting to workspace root), and MPI fields.
    auto resolveSelectedTarget = [this]() -> QString {
        const QString typed = m_targetBinaryCombo->currentText().trimmed();
        const int idx = m_targetBinaryCombo->currentIndex();
        if (idx >= 0 && m_targetBinaryCombo->itemText(idx) == typed) {
            const QString dataPath = m_targetBinaryCombo->itemData(idx).toString().trimmed();
            if (!dataPath.isEmpty()) return dataPath;
        }
        return typed;
    };
    const QString initTarget = resolveSelectedTarget();
    const QString savedArgs = !initTarget.isEmpty() ? cfg.programArgs(initTarget)
                                                    : (!ps.targetArguments.isEmpty() ? ps.targetArguments : QString::fromStdString(ps.programArguments));
    m_binaryArguments->setText(savedArgs);
    connect(m_targetBinaryCombo, &QComboBox::currentIndexChanged, this, [this, resolveSelectedTarget](int) {
        const QString selected = resolveSelectedTarget();
        if (!selected.isEmpty()) m_binaryArguments->setText(gridlock::core::ConfigManager::instance().programArgs(selected));
    });
    const QString savedWd = QString::fromStdString(ps.workingDirectory).trimmed();
    m_workingDirectory->setText((!savedWd.isEmpty() && QFileInfo(savedWd).isDir()) ? savedWd : wsRoot);
    const int initRanks = ps.mpiRanks > 0 ? ps.mpiRanks : (parentRankSpin ? parentRankSpin->value() : cfg.getDefaultRanks());
    m_mpiRanksSpin->setValue(qMax(1, initRanks));
    m_mpiArgsEdit->setText(ps.mpiArgs);

    // Wire workspace-aware executable picker to target combo box.
    connect(m_targetBrowseBtn, &QPushButton::clicked, this, [this, wsRoot]() {
        const QString wd = m_workingDirectory->text().trimmed();
        const QString seed = (!wd.isEmpty() && QFileInfo(wd).isDir()) ? wd : wsRoot;
        const QString picked = FileDialogHelper::getOpenFileName(
            this, QStringLiteral("Select Target Executable"), seed, QStringLiteral("Executables (*);;All Files (*)"));
        if (!picked.isEmpty()) {
            m_targetBinaryCombo->setCurrentText(picked);
            m_binaryArguments->setText(gridlock::core::ConfigManager::instance().programArgs(picked));
        }
    });
    // Wire workspace-aware folder picker to working directory input.
    connect(m_workingDirBrowseBtn, &QPushButton::clicked, this, [this, wsRoot]() {
        const QString wd = m_workingDirectory->text().trimmed();
        const QString seed = (!wd.isEmpty() && QFileInfo(wd).isDir()) ? wd : wsRoot;
        const QString picked = FileDialogHelper::getExistingDirectory(
            this, QStringLiteral("Select Working Directory"), seed);
        if (!picked.isEmpty()) m_workingDirectory->setText(picked);
    });
}

/**
 * @brief Serializes form inputs back to ConfigManager and synchronizes MainWindow toolbar controls.
 */
void ProjectSettingsDialog::accept() {
    auto& cfg = gridlock::core::ConfigManager::instance();
    auto ps = cfg.loadProjectSettings();
    // Resolve typed text vs combo user data, normalizing relative binary paths against working directory.
    const QString typedTarget = m_targetBinaryCombo->currentText().trimmed();
    QString chosenTarget = typedTarget;
    const int curIdx = m_targetBinaryCombo->currentIndex();
    if (curIdx >= 0 && m_targetBinaryCombo->itemText(curIdx) == typedTarget) {
        const QString dataPath = m_targetBinaryCombo->itemData(curIdx).toString().trimmed();
        if (!dataPath.isEmpty()) chosenTarget = dataPath;
    }
    // Fall back to workspace root when the user leaves Working Directory empty.
    QString wd = m_workingDirectory->text().trimmed();
    if (wd.isEmpty()) wd = FileDialogHelper::resolveDefaultDirectory(cfg.workspacePath());
    if (!chosenTarget.isEmpty() && !QDir::isAbsolutePath(chosenTarget) && !wd.isEmpty()) {
        const QString candidate = QDir(wd).filePath(chosenTarget);
        if (QFileInfo::exists(candidate)) chosenTarget = QFileInfo(candidate).absoluteFilePath();
    }

    // Persist updated project execution parameters into `.gridlock/settings.toml` (target-scoped args).
    ps.targetBinary = chosenTarget.toStdString();
    ps.targetArguments = m_binaryArguments->text().trimmed();
    ps.programArguments = ps.targetArguments.toStdString();
    ps.workingDirectory = wd.toStdString();
    ps.mpiRanks = m_mpiRanksSpin->value();
    ps.mpiArgs = m_mpiArgsEdit->text().trimmed();
    cfg.saveProjectSettings(ps);
    if (!chosenTarget.isEmpty()) cfg.setProgramArgs(chosenTarget, ps.targetArguments);

    // Persist Programmer's Calculator default bit-width and signed display settings.
    QSettings calcSettings(QStringLiteral("gridlock"), QStringLiteral("debugger"));
    if (m_calcWidthCombo) calcSettings.setValue(QStringLiteral("tools/calcDefaultWidth"), m_calcWidthCombo->currentData().toInt());
    if (m_calcSignedCheck) calcSettings.setValue(QStringLiteral("tools/calcDefaultSigned"), m_calcSignedCheck->isChecked());

    // Mirror updated MPI rank count and flags into global DebuggerSettings defaults.
    auto ds = cfg.getDebuggerSettings();
    ds.defaultRanks = ps.mpiRanks;
    ds.mpiArgs = ps.mpiArgs;
    cfg.saveDebuggerSettings(ds);

    // Synchronize parent MainWindow toolbar rank spinbox and TARGET selector immediately.
    if (QWidget* parentWin = parentWidget()) {
        // Update main toolbar RANKS (-np) spinbox if present on the parent window.
        if (auto* rankSpin = parentWin->findChild<QSpinBox*>(QStringLiteral("toolbarRankSpin"))) {
            rankSpin->setValue(ps.mpiRanks);
        }
        // Update main toolbar TARGET selector combo box and trigger binary/DWARF loading on switch.
        if (auto* targetCombo = parentWin->findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector"))) {
            if (!chosenTarget.isEmpty()) {
                const QString chosenCanon = QFileInfo(chosenTarget).canonicalFilePath();
                int matchIdx = -1;
                int openBinIdx = -1;
                // Search existing toolbar entries while tracking the sentinel Open Binary action index.
                for (int i = 0; i < targetCombo->count(); ++i) {
                    const QString data = targetCombo->itemData(i).toString();
                    if (data == QStringLiteral("__open_binary__")) { openBinIdx = i; continue; }
                    if (data == chosenTarget || (!chosenCanon.isEmpty() && QFileInfo(data).canonicalFilePath() == chosenCanon)) {
                        matchIdx = i;
                        break;
                    }
                }
                // Insert custom binary right before `Open ELF / Binary...` when not already listed.
                if (matchIdx < 0) {
                    const int insertAt = (openBinIdx >= 0) ? openBinIdx : targetCombo->count();
                    targetCombo->insertItem(insertAt, QFileInfo(chosenTarget).fileName(), chosenTarget);
                    matchIdx = insertAt;
                }
                // Activating the combo entry invokes MainWindow's connected selectTargetByName handler.
                targetCombo->setCurrentIndex(matchIdx);
            }
        }
    }
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
