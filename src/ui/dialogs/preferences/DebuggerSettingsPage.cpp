/**
 * @file DebuggerSettingsPage.cpp
 * @brief Implementation of the Debugger settings page widget.
 */

#include "DebuggerSettingsPage.hpp"
#include "PreferencesFormHelpers.hpp"
#include "../../../core/managers/ConfigManager.hpp"

#include <QComboBox>
#include <QFormLayout>

namespace gridlock::ui {

using prefs_detail::addBrowsePathRow;
using prefs_detail::addFormNote;
using prefs_detail::createStandardPageForm;

/**
 * @brief Constructs the debugging backend and MPI execution configuration page.
 * @param parent Optional parent widget.
 */
DebuggerSettingsPage::DebuggerSettingsPage(QWidget* parent)
    : QWidget(parent) {
    auto* form = createStandardPageForm(this, tr("<b>Debugger</b>"));

    // Local debugger backend engine selector (GDB, LLDB-DAP, CUDA-GDB, ROCm GDB).
    auto* engineCombo = new QComboBox(this);
    engineCombo->setObjectName(QStringLiteral("debuggerEngineCombo"));
    engineCombo->addItem(tr("GNU GDB (gdb)"), QStringLiteral("gdb"));
    engineCombo->addItem(tr("LLVM LLDB-DAP (lldb-dap)"), QStringLiteral("lldb-dap"));
    engineCombo->addItem(tr("NVIDIA CUDA-GDB (cuda-gdb)"), QStringLiteral("cuda-gdb"));
    engineCombo->addItem(tr("AMD ROCm GDB (rocgdb)"), QStringLiteral("rocgdb"));
    engineCombo->setToolTip(
        tr("Select the local debugger backend engine used for target attachment and MI/DAP sessions."));
    form->addRow(tr("Debugger Engine:"), engineCombo);

    // Default disassembly presentation view mode selector (CFG Map, Linear Assembly, Split CFG + Decompiler).
    auto* defModeCombo = new QComboBox(this);
    defModeCombo->setObjectName(QStringLiteral("defaultDisassemblyModeCombo"));
    defModeCombo->addItem(tr("Control Flow Graph (Map)"), QStringLiteral("cfg"));
    defModeCombo->addItem(tr("Linear Assembly List"), QStringLiteral("linear"));
    defModeCombo->addItem(tr("Split CFG + Decompiler"), QStringLiteral("split"));
    defModeCombo->setToolTip(
        tr("Default presentation mode for DisassemblyView across debugging sessions."));
    form->addRow(tr("Default Disassembly View:"), defModeCombo);

    // Language server and adapter executable path browse inputs.
    m_clangdPathEdit = addBrowsePathRow(
        this, form, tr("Clangd Path:"), QStringLiteral("clangd"),
        tr("Path to the clangd language server executable."),
        tr("Select Clangd Executable"));
    m_dapAdapterEdit = addBrowsePathRow(
        this, form, tr("DAP Adapter Path:"), QStringLiteral("lldb-dap"),
        tr("Path to the DAP server (e.g. lldb-dap)."),
        tr("Select DAP Adapter"));
    m_gdbPathEdit = addBrowsePathRow(
        this, form, tr("GDB Path:"), QStringLiteral("gdb"),
        tr("Full path to the GDB binary (e.g. gdb, rocgdb, cuda-gdb, or a custom build)."),
        tr("Select GDB Path"));

    connect(engineCombo, &QComboBox::currentIndexChanged, this, [this, engineCombo](int) {
        if (!m_gdbPathEdit) return;
        const QString eng = engineCombo->currentData().toString().trimmed().toLower();
        if (eng != QStringLiteral("gdb") && eng != QStringLiteral("cuda-gdb") &&
            eng != QStringLiteral("rocgdb")) {
            return;
        }
        m_gdbPathEdit->setPlaceholderText(eng);
        const QString cur = m_gdbPathEdit->text().trimmed();
        if (cur.isEmpty() || cur == QStringLiteral("gdb") ||
            cur == QStringLiteral("cuda-gdb") || cur == QStringLiteral("rocgdb")) {
            m_gdbPathEdit->setText(eng);
        }
    });

    // MPI launcher executable command.
    m_mpiExecEdit = new QLineEdit(this);
    m_mpiExecEdit->setPlaceholderText(QStringLiteral("mpiexec"));
    m_mpiExecEdit->setToolTip(
        tr("MPI launcher used when attaching the debugger to MPI ranks."));
    form->addRow(tr("MPI Executable:"), m_mpiExecEdit);

    // Forwarded MPI command line arguments.
    m_mpiArgsEdit = new QLineEdit(this);
    m_mpiArgsEdit->setPlaceholderText(QStringLiteral("--oversubscribe"));
    m_mpiArgsEdit->setToolTip(
        tr("Extra flags forwarded to the MPI launcher on every debug session."));
    form->addRow(tr("MPI Args:"), m_mpiArgsEdit);

    // Initial rank allocation count.
    m_rankBox = new QSpinBox(this);
    m_rankBox->setRange(1, 256);
    m_rankBox->setToolTip(
        tr("Default number of MPI ranks for debugger sessions (minimum 1)."));
    form->addRow(tr("Default Rank Count:"), m_rankBox);

    // Floating point exception trapping checkbox.
    m_trapFpeCheck = new QCheckBox(tr("Trap FPE (NaN/Overflow)"), this);
    m_trapFpeCheck->setToolTip(
        tr("Automatically set GDB to catch floating-point traps."));
    form->addRow(QString(), m_trapFpeCheck);

    // Toggle for displaying the Pseudo-C Decompiler pane when the C++ Source Editor is also open.
    auto* showDecompCheck = new QCheckBox(tr("Show Pseudo-C Decompiler when Source Editor is visible"), this);
    showDecompCheck->setObjectName(QStringLiteral("showDecompilerWithSourceCheck"));
    showDecompCheck->setToolTip(
        tr("When unchecked, Disassembly displays pure assembly in CODE and MPI perspectives."));
    form->addRow(QString(), showDecompCheck);

    addFormNote(this, form,
                tr("<small style='color:#888;'>GDB must support the MI2 protocol "
                   "(<code>--interpreter=mi2</code>).<br>Changes take effect on "
                   "the next debug session launch.</small>"));

    loadFromSettings();
}

QString DebuggerSettingsPage::clangdPath() const { return m_clangdPathEdit->text(); }
QString DebuggerSettingsPage::dapAdapterPath() const { return m_dapAdapterEdit->text(); }
QString DebuggerSettingsPage::gdbPath() const { return m_gdbPathEdit->text(); }
QString DebuggerSettingsPage::mpiExecutable() const { return m_mpiExecEdit->text(); }
QString DebuggerSettingsPage::mpiArgs() const { return m_mpiArgsEdit->text(); }
int DebuggerSettingsPage::defaultRanks() const { return m_rankBox->value(); }
bool DebuggerSettingsPage::trapFpe() const { return m_trapFpeCheck->isChecked(); }

/**
 * @brief Restores debugger settings from global ConfigManager singleton.
 */
void DebuggerSettingsPage::loadFromSettings() {
    auto& cfg = gridlock::core::ConfigManager::instance();
    const auto ds = cfg.getDebuggerSettings();
    (void)cfg.loadProjectSettings();
    if (auto* engineCombo = findChild<QComboBox*>(QStringLiteral("debuggerEngineCombo"))) {
        const int idx = engineCombo->findData(cfg.debuggerEngine());
        if (idx >= 0) engineCombo->setCurrentIndex(idx);
    }
    if (auto* defModeCombo = findChild<QComboBox*>(QStringLiteral("defaultDisassemblyModeCombo"))) {
        const int idx = defModeCombo->findData(cfg.defaultDisassemblyMode());
        if (idx >= 0) defModeCombo->setCurrentIndex(idx);
    }
    if (auto* showDecompCheck = findChild<QCheckBox*>(QStringLiteral("showDecompilerWithSourceCheck"))) {
        showDecompCheck->setChecked(cfg.showDecompilerWithSource());
    }
    m_clangdPathEdit->setText(cfg.getGlobalClangdPath());
    m_dapAdapterEdit->setText(cfg.getGlobalDapAdapterPath());
    const QString eng = cfg.debuggerEngine().trimmed().toLower();
    QString effectiveGdb = ds.gdbPath.trimmed();
    if ((eng == QStringLiteral("rocgdb") || eng == QStringLiteral("cuda-gdb")) &&
        (effectiveGdb.isEmpty() || effectiveGdb == QStringLiteral("gdb"))) {
        effectiveGdb = eng;
    }
    m_gdbPathEdit->setText(effectiveGdb.isEmpty() ? ds.gdbPath : effectiveGdb);
    m_mpiExecEdit->setText(ds.mpiExecutable);
    m_mpiArgsEdit->setText(ds.mpiArgs);
    m_rankBox->setValue(ds.defaultRanks);
    m_trapFpeCheck->setChecked(ds.trapFpe);
}

}  // namespace gridlock::ui
