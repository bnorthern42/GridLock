/**
 * @file PreferencesDialogPages.cpp
 * @brief Implementations of category-specific preference page widgets.
 *
 * Implements the distinct settings pages for Appearance, Editing, Behavior,
 * Debugger, HPC Cluster, HPC Integration, Docsets, and Terminal. Each page
 * initializes form controls with intuitive tooltips, syncs with ConfigManager
 * and QSettings, and exposes accessors for PreferencesDialog.
 */

#include "PreferencesDialog.hpp"
#include "../../core/managers/ConfigManager.hpp"
#include "../../core/managers/DocsetManager.hpp"
#include "../../core/managers/ThemeManager.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

namespace gridlock::ui {

namespace {

/**
 * @brief Builds a standardized form layout container for a preference page.
 * @param parent The hosting page widget.
 * @param titleHtml Rich text or bold heading title string.
 * @param spacing Vertical spacing in pixels between form rows.
 * @return Configured QFormLayout attached to parent.
 */
QFormLayout* createStandardPageForm(QWidget* parent, const QString& titleHtml,
                                    int spacing = 14) {
    auto* form = new QFormLayout(parent);
    form->setContentsMargins(24, 24, 24, 24);
    form->setSpacing(spacing);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Section title heading label with custom object name for styling.
    auto* heading = new QLabel(titleHtml, parent);
    heading->setObjectName("heading");
    form->addRow(heading);

    // Subtle 1px horizontal separator line.
    auto* separator = new QLabel(parent);
    separator->setFixedHeight(1);
    separator->setObjectName("separator");
    form->addRow(separator);
    return form;
}

/**
 * @brief Adds a file or directory browser input row to a form layout.
 * @param parent The parent widget context.
 * @param form Target form layout.
 * @param rowLabel Descriptive field label text.
 * @param placeholder Placeholder hint text for the text field.
 * @param toolTip Informative hover tooltip description.
 * @param dialogTitle Modal file dialog caption.
 * @param defaultDir Fallback directory path if input is empty.
 * @return Pointer to the newly constructed QLineEdit.
 */
QLineEdit* addBrowsePathRow(QWidget* parent, QFormLayout* form,
                            const QString& rowLabel,
                            const QString& placeholder,
                            const QString& toolTip,
                            const QString& dialogTitle,
                            const QString& defaultDir = QString()) {
    auto* row = new QHBoxLayout();
    auto* edit = new QLineEdit(parent);
    edit->setPlaceholderText(placeholder);
    if (!toolTip.isEmpty()) {
        edit->setToolTip(toolTip);
    }
    auto* browseBtn = new QPushButton(QObject::tr("Browse…"), parent);
    browseBtn->setFixedWidth(72);
    QObject::connect(browseBtn, &QPushButton::clicked, parent,
                     [parent, edit, dialogTitle, defaultDir]() {
                         const QString path = QFileDialog::getOpenFileName(
                             parent, dialogTitle, defaultDir);
                         if (!path.isEmpty()) {
                             edit->setText(path);
                         }
                     });
    row->addWidget(edit);
    row->addWidget(browseBtn);
    form->addRow(rowLabel, row);
    return edit;
}

/**
 * @brief Adds an explanatory footnote label to a form layout.
 * @param parent Parent widget context.
 * @param form Target form layout.
 * @param html Formatted HTML note text.
 */
void addFormNote(QWidget* parent, QFormLayout* form, const QString& html) {
    auto* note = new QLabel(html, parent);
    note->setWordWrap(true);
    form->addRow(QString(), note);
}

}  // namespace

// ============================================================================
// AppearanceSettingsPage Implementation
// ============================================================================

/**
 * @brief Constructs the visual appearance settings page.
 * @param parent Optional parent widget.
 */
AppearanceSettingsPage::AppearanceSettingsPage(QWidget* parent)
    : QWidget(parent) {
    auto* form = createStandardPageForm(this, tr("<b>Appearance</b>"));

    // Theme selector dropdown populated dynamically from ThemeManager.
    m_themeCombo = new QComboBox(this);
    m_themeCombo->addItems(
        gridlock::core::managers::ThemeManager::instance().getAvailableThemes());
    m_themeCombo->setToolTip(
        tr("Select the application color theme. Changes are applied immediately."));
    form->addRow(tr("Color Theme:"), m_themeCombo);

    // Dark mode toggle checkbox.
    m_darkModeCheck = new QCheckBox(tr("Enable Dark Mode"), this);
    form->addRow(QString(), m_darkModeCheck);

    // File tree presentation density.
    m_fileTreeStyleCombo = new QComboBox(this);
    m_fileTreeStyleCombo->addItems({tr("Compact"), tr("Comfortable"), tr("Large")});
    m_fileTreeStyleCombo->setToolTip(
        tr("Density of items in the Project Explorer tree."));
    form->addRow(tr("File Tree Style:"), m_fileTreeStyleCombo);

    // File icon colorization toggle.
    m_colorizeIconsCheck = new QCheckBox(tr("Enable Colorful File Icons"), this);
    m_colorizeIconsCheck->setToolTip(
        tr("Render file tree icons using vibrant colors based on file type."));
    form->addRow(QString(), m_colorizeIconsCheck);

    // UI and code editor font sizing spinners.
    m_uiFontSizeBox = new QSpinBox(this);
    m_uiFontSizeBox->setRange(6, 36);
    m_uiFontSizeBox->setToolTip(tr("Global font size for the IDE interface."));
    form->addRow(tr("UI Font Size:"), m_uiFontSizeBox);

    m_codeFontSizeBox = new QSpinBox(this);
    m_codeFontSizeBox->setRange(6, 72);
    m_codeFontSizeBox->setToolTip(tr("Font size for the source code editor."));
    form->addRow(tr("Code Font Size:"), m_codeFontSizeBox);

    addFormNote(this, form,
                tr("<small style='color:#888;'>Font settings follow the system "
                   "monospace font.<br>Restart may be required for full theme "
                   "reload.</small>"));

    // Populate initial state with signals blocked before connecting live preview slots
    // so opening PreferencesDialog never mutates global QSS or terminal fonts.
    loadFromSettings();

    connect(m_themeCombo, &QComboBox::currentTextChanged, this,
            [this](const QString& theme) {
                gridlock::core::managers::ThemeManager::instance().setTheme(
                    theme, m_darkModeCheck->isChecked());
            });
    connect(m_darkModeCheck, &QCheckBox::toggled, this, [](bool checked) {
        gridlock::core::managers::ThemeManager::instance().setDarkMode(checked);
    });
}

QString AppearanceSettingsPage::selectedTheme() const {
    return m_themeCombo->currentText();
}
bool AppearanceSettingsPage::isDarkMode() const {
    return m_darkModeCheck->isChecked();
}
QString AppearanceSettingsPage::fileTreeStyle() const {
    return m_fileTreeStyleCombo->currentText();
}
bool AppearanceSettingsPage::colorizeIcons() const {
    return m_colorizeIconsCheck->isChecked();
}
int AppearanceSettingsPage::uiFontSize() const { return m_uiFontSizeBox->value(); }
int AppearanceSettingsPage::codeFontSize() const { return m_codeFontSizeBox->value(); }

/**
 * @brief Loads persisted appearance preferences from user settings with signals blocked.
 */
void AppearanceSettingsPage::loadFromSettings() {
    QSettings s("gridlock", "debugger");
    const QString theme = s.value("appearance/theme", "dark_teal").toString();
    const bool isDark = s.value("appearance/dark_mode", true).toBool();

    m_darkModeCheck->blockSignals(true);
    m_themeCombo->blockSignals(true);
    m_fileTreeStyleCombo->blockSignals(true);
    m_colorizeIconsCheck->blockSignals(true);
    m_uiFontSizeBox->blockSignals(true);
    m_codeFontSizeBox->blockSignals(true);

    m_darkModeCheck->setChecked(isDark);
    const int idx = m_themeCombo->findText(theme);
    if (idx >= 0) {
        m_themeCombo->setCurrentIndex(idx);
    }
    const int ftsIdx = m_fileTreeStyleCombo->findText(
        s.value("appearance/file_tree_style", "Comfortable").toString());
    if (ftsIdx >= 0) {
        m_fileTreeStyleCombo->setCurrentIndex(ftsIdx);
    }
    m_colorizeIconsCheck->setChecked(
        s.value("appearance/colorize_icons", true).toBool());
    m_uiFontSizeBox->setValue(s.value("appearance/ui_font_size", 11).toInt());
    m_codeFontSizeBox->setValue(s.value("appearance/code_font_size", 11).toInt());

    m_codeFontSizeBox->blockSignals(false);
    m_uiFontSizeBox->blockSignals(false);
    m_colorizeIconsCheck->blockSignals(false);
    m_fileTreeStyleCombo->blockSignals(false);
    m_themeCombo->blockSignals(false);
    m_darkModeCheck->blockSignals(false);
}

// ============================================================================
// EditingSettingsPage Implementation
// ============================================================================

/**
 * @brief Constructs the source editor configuration page.
 * @param parent Optional parent widget.
 */
EditingSettingsPage::EditingSettingsPage(QWidget* parent)
    : QWidget(parent) {
    auto* form = createStandardPageForm(this, tr("<b>Editing</b>"));

    // Tab stop width spinner.
    m_tabWidthBox = new QSpinBox(this);
    m_tabWidthBox->setRange(1, 16);
    m_tabWidthBox->setValue(4);
    m_tabWidthBox->setToolTip(
        tr("Number of spaces that represent one tab stop in the source view."));
    form->addRow(tr("Tab Width:"), m_tabWidthBox);

    // Indentation character mode (spaces vs hard tabs).
    m_indentModeCombo = new QComboBox(this);
    m_indentModeCombo->addItems({tr("Spaces"), tr("Tabs")});
    m_indentModeCombo->setToolTip(
        tr("Whether to insert spaces or a real tab character when Tab is pressed."));
    form->addRow(tr("Indent With:"), m_indentModeCombo);

    // Whitespace glyph visibility.
    m_whitespaceCombo = new QComboBox(this);
    m_whitespaceCombo->addItems({tr("Hidden"), tr("Visible")});
    m_whitespaceCombo->setToolTip(
        tr("Show invisible whitespace characters in the editor."));
    form->addRow(tr("Whitespace:"), m_whitespaceCombo);

    // Word wrap mode toggle.
    m_wordWrapCheck = new QCheckBox(tr("Enable Word Wrap"), this);
    m_wordWrapCheck->setToolTip(tr("Wrap long lines at the edge of the editor view."));
    form->addRow(QString(), m_wordWrapCheck);

    // Edge column guide marker controls.
    m_showEdgeColumnCheck = new QCheckBox(tr("Show Edge Column Line"), this);
    m_showEdgeColumnCheck->setToolTip(
        tr("Display a vertical line at the specified edge column."));
    form->addRow(QString(), m_showEdgeColumnCheck);

    m_edgeColumnBox = new QSpinBox(this);
    m_edgeColumnBox->setRange(1, 300);
    m_edgeColumnBox->setValue(80);
    m_edgeColumnBox->setToolTip(
        tr("The column index at which the edge line is drawn."));
    form->addRow(tr("Edge Column:"), m_edgeColumnBox);

    loadFromSettings();
}

int EditingSettingsPage::tabWidth() const { return m_tabWidthBox->value(); }
bool EditingSettingsPage::insertSpaces() const {
    return m_indentModeCombo->currentIndex() == 0;
}
bool EditingSettingsPage::showWhitespace() const {
    return m_whitespaceCombo->currentIndex() == 1;
}
bool EditingSettingsPage::wordWrap() const { return m_wordWrapCheck->isChecked(); }
bool EditingSettingsPage::showEdgeColumn() const {
    return m_showEdgeColumnCheck->isChecked();
}
int EditingSettingsPage::edgeColumn() const { return m_edgeColumnBox->value(); }

/**
 * @brief Restores editing preferences from user settings storage.
 */
void EditingSettingsPage::loadFromSettings() {
    QSettings s("gridlock", "debugger");
    m_tabWidthBox->setValue(s.value("editing/tab_width", 4).toInt());
    m_indentModeCombo->setCurrentIndex(
        s.value("editing/insert_spaces", true).toBool() ? 0 : 1);
    m_whitespaceCombo->setCurrentIndex(
        s.value("editing/show_whitespace", false).toBool() ? 1 : 0);
    m_wordWrapCheck->setChecked(s.value("editing/word_wrap", false).toBool());
    m_showEdgeColumnCheck->setChecked(
        s.value("editing/show_edge_column", true).toBool());
    m_edgeColumnBox->setValue(s.value("editing/edge_column", 80).toInt());
}

// ============================================================================
// BehaviorSettingsPage Implementation
// ============================================================================

/**
 * @brief Constructs session-level user interaction preference page.
 * @param parent Optional parent widget.
 */
BehaviorSettingsPage::BehaviorSettingsPage(QWidget* parent)
    : QWidget(parent) {
    auto* form = createStandardPageForm(this, tr("<b>Behavior</b>"));

    // Breakpoint persistence policy on workspace load.
    m_restoreBreakpointsCheck = new QComboBox(this);
    m_restoreBreakpointsCheck->addItems({tr("Always"), tr("Ask"), tr("Never")});
    m_restoreBreakpointsCheck->setToolTip(
        tr("Whether to reload saved breakpoints when a source file is opened."));
    form->addRow(tr("Restore Breakpoints:"), m_restoreBreakpointsCheck);

    // Confirmation prompt on session exit.
    m_confirmQuitCheck = new QComboBox(this);
    m_confirmQuitCheck->addItems({tr("Yes"), tr("No")});
    m_confirmQuitCheck->setToolTip(
        tr("Ask for confirmation before terminating an active debug session."));
    form->addRow(tr("Confirm Quit:"), m_confirmQuitCheck);

    // Auto-focus behavior when an MPI rank hits a breakpoint or exception.
    m_focusOnStopCheck = new QComboBox(this);
    m_focusOnStopCheck->addItems({tr("Yes"), tr("No")});
    m_focusOnStopCheck->setToolTip(
        tr("Automatically raise the GridLock window when a rank hits a breakpoint."));
    form->addRow(tr("Focus on Stop:"), m_focusOnStopCheck);

    addFormNote(this, form,
                tr("<small style='color:#888;'>MPI executor and rank settings are "
                   "in the <b>Debugger</b> category.</small>"));

    loadFromSettings();
}

QString BehaviorSettingsPage::restoreBreakpoints() const {
    return m_restoreBreakpointsCheck->currentText();
}
bool BehaviorSettingsPage::confirmQuit() const {
    return m_confirmQuitCheck->currentIndex() == 0;
}
bool BehaviorSettingsPage::focusOnStop() const {
    return m_focusOnStopCheck->currentIndex() == 0;
}

/**
 * @brief Syncs behavior settings from persistent application state.
 */
void BehaviorSettingsPage::loadFromSettings() {
    QSettings s("gridlock", "debugger");
    const int rbIdx = m_restoreBreakpointsCheck->findText(
        s.value("behavior/restore_breakpoints", "Always").toString());
    if (rbIdx >= 0) {
        m_restoreBreakpointsCheck->setCurrentIndex(rbIdx);
    }
    m_confirmQuitCheck->setCurrentIndex(
        s.value("behavior/confirm_quit", true).toBool() ? 0 : 1);
    m_focusOnStopCheck->setCurrentIndex(
        s.value("behavior/focus_on_stop", true).toBool() ? 0 : 1);
}

// ============================================================================
// DebuggerSettingsPage Implementation
// ============================================================================

/**
 * @brief Constructs the debugging backend and MPI execution configuration page.
 * @param parent Optional parent widget.
 */
DebuggerSettingsPage::DebuggerSettingsPage(QWidget* parent)
    : QWidget(parent) {
    auto* form = createStandardPageForm(this, tr("<b>Debugger</b>"));

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
        tr("Full path to the GDB binary, e.g. /usr/bin/gdb or a custom build."),
        tr("Select GDB Path"));

    // MPI launcher executable command.
    m_mpiExecEdit = new QLineEdit(this);
    m_mpiExecEdit->setPlaceholderText("mpiexec");
    m_mpiExecEdit->setToolTip(
        tr("MPI launcher used when attaching the debugger to MPI ranks."));
    form->addRow(tr("MPI Executable:"), m_mpiExecEdit);

    // Forwarded MPI command line arguments.
    m_mpiArgsEdit = new QLineEdit(this);
    m_mpiArgsEdit->setPlaceholderText("--oversubscribe");
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
    const auto ds = gridlock::core::ConfigManager::instance().getDebuggerSettings();
    (void)gridlock::core::ConfigManager::instance().loadProjectSettings();
    m_clangdPathEdit->setText(
        gridlock::core::ConfigManager::instance().getGlobalClangdPath());
    m_dapAdapterEdit->setText(
        gridlock::core::ConfigManager::instance().getGlobalDapAdapterPath());
    m_gdbPathEdit->setText(ds.gdbPath);
    m_mpiExecEdit->setText(ds.mpiExecutable);
    m_mpiArgsEdit->setText(ds.mpiArgs);
    m_rankBox->setValue(ds.defaultRanks);
    m_trapFpeCheck->setChecked(ds.trapFpe);
}

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

    // Hostfile browse input layout.
    auto* hostsRow = new QHBoxLayout();
    m_hostsFileEdit = new QLineEdit(this);
    m_hostsFileEdit->setPlaceholderText(tr("(not set — use default hosts)"));
    m_hostsFileEdit->setToolTip(
        tr("Path to an MPI hostfile. When set, --hostfile <path> is passed to the\n"
           "MPI launcher. Leave empty to let the launcher choose nodes automatically."));
    auto* browseBtn = new QPushButton(tr("Browse…"), this);
    browseBtn->setFixedWidth(72);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
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

    // Multi-line environment variable key-value definitions.
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

    // Strict node affinity binding option.
    m_strictAffinityBox =
        new QCheckBox(tr("Bind ranks to individual nodes (--map-by node)"), this);
    m_strictAffinityBox->setToolTip(
        tr("Passes --map-by node to the MPI launcher so each rank is pinned to\n"
           "a separate physical node. Useful for NUMA-sensitive workloads."));
    form->addRow(tr("Strict Node Affinity:"), m_strictAffinityBox);

    addFormNote(this, form,
                tr("<small style='color:#888;'>Changes are applied on the "
                   "<b>next</b> debug session launch.</small>"));

    loadFromSettings();
}

QString HpcSettingsPage::hostsFile() const { return m_hostsFileEdit->text().trimmed(); }
QString HpcSettingsPage::envVars() const { return m_envVarsEdit->toPlainText().trimmed(); }
bool HpcSettingsPage::strictAffinity() const { return m_strictAffinityBox->isChecked(); }

/**
 * @brief Restores cluster settings from ConfigManager.
 */
void HpcSettingsPage::loadFromSettings() {
    const auto hs = gridlock::core::ConfigManager::instance().getHpcSettings();
    m_hostsFileEdit->setText(hs.hostsFile);
    m_envVarsEdit->setPlainText(hs.envVars);
    m_strictAffinityBox->setChecked(hs.strictAffinity);
}

// ============================================================================
// HpcIntegrationSettingsPage Implementation
// ============================================================================

/**
 * @brief Constructs remote SSH, SLURM batch, and Spack integration page.
 * @param parent Optional parent widget.
 */
HpcIntegrationSettingsPage::HpcIntegrationSettingsPage(QWidget* parent)
    : QWidget(parent) {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    auto* container = new QWidget();
    scroll->setWidget(container);
    auto* form = new QFormLayout(container);
    form->setContentsMargins(24, 24, 24, 24);
    form->setSpacing(12);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto addSectionHeading = [&](const QString& title) {
        auto* lbl = new QLabel(title, container);
        lbl->setObjectName("heading");
        form->addRow(lbl);
    };

    // SSH Remote node credentials.
    addSectionHeading(tr("<b>Remote SSH Connection</b>"));

    m_sshHostEdit = new QLineEdit(container);
    m_sshHostEdit->setPlaceholderText(tr("e.g. login.hpc.university.edu"));
    m_sshHostEdit->setToolTip(tr("Hostname or IP of the HPC login node."));
    form->addRow(tr("SSH Host:"), m_sshHostEdit);

    m_sshUserEdit = new QLineEdit(container);
    m_sshUserEdit->setPlaceholderText(tr("your_hpc_username"));
    form->addRow(tr("SSH User:"), m_sshUserEdit);

    auto* keyRow = new QHBoxLayout();
    m_sshKeyEdit = new QLineEdit(container);
    m_sshKeyEdit->setPlaceholderText(tr("~/.ssh/id_rsa"));
    m_sshKeyEdit->setToolTip(
        tr("Path to the private SSH key file used for authentication."));
    auto* keyBrowse = new QPushButton(tr("Browse…"), container);
    keyBrowse->setFixedWidth(72);
    connect(keyBrowse, &QPushButton::clicked, this, [this]() {
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

    // SLURM batch job parameter configuration.
    addSectionHeading(tr("<b>SLURM Batch Configuration</b>"));

    m_partitionEdit = new QLineEdit(container);
    m_partitionEdit->setPlaceholderText(tr("batch"));
    m_partitionEdit->setToolTip(tr("SLURM partition / queue name."));
    form->addRow(tr("Partition:"), m_partitionEdit);

    m_nodesBox = new QSpinBox(container);
    m_nodesBox->setRange(1, 9999);
    m_nodesBox->setSuffix(tr(" node(s)"));
    form->addRow(tr("Nodes:"), m_nodesBox);

    m_tasksBox = new QSpinBox(container);
    m_tasksBox->setRange(1, 9999);
    m_tasksBox->setSuffix(tr(" task(s)/node"));
    form->addRow(tr("Tasks per Node:"), m_tasksBox);

    auto* gpuRow = new QHBoxLayout();
    m_gpuCheck = new QCheckBox(tr("Request GPUs"), container);
    m_gpusBox = new QSpinBox(container);
    m_gpusBox->setRange(1, 16);
    m_gpusBox->setSuffix(tr(" GPU(s)/node"));
    m_gpusBox->setEnabled(false);
    connect(m_gpuCheck, &QCheckBox::toggled, m_gpusBox, &QSpinBox::setEnabled);
    gpuRow->addWidget(m_gpuCheck);
    gpuRow->addWidget(m_gpusBox);
    gpuRow->addStretch();
    form->addRow(tr("GPUs:"), gpuRow);

    auto* tplLabel = new QLabel(
        tr("Script Template (Use <tt>{FILE}</tt> as a placeholder for the target source code):"),
        container);
    tplLabel->setTextFormat(Qt::RichText);
    form->addRow(tplLabel);

    m_templateEdit = new QPlainTextEdit(container);
    m_templateEdit->setMinimumHeight(130);
    QFont mono("Monospace");
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPointSize(9);
    m_templateEdit->setFont(mono);
    m_templateEdit->setPlaceholderText("#!/bin/bash\n#SBATCH --job-name=gridlock_job\n"
                                       "#SBATCH --output=gridlock_%j.log\n#SBATCH --partition=batch\n"
                                       "#SBATCH --nodes=1\n#SBATCH --ntasks-per-node=4\n\n"
                                       "module load openmpi\nmpirun -np 4 {FILE}");
    form->addRow(m_templateEdit);

    // Spack package manager environment configuration.
    addSectionHeading(tr("<b>Spack Package Manager</b>"));

    m_spackRootEdit = new QLineEdit(container);
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
QString HpcIntegrationSettingsPage::slurmPartition() const { return m_partitionEdit->text().trimmed(); }
int HpcIntegrationSettingsPage::slurmNodes() const { return m_nodesBox->value(); }
int HpcIntegrationSettingsPage::slurmTasks() const { return m_tasksBox->value(); }
bool HpcIntegrationSettingsPage::requestGpus() const { return m_gpuCheck->isChecked(); }
int HpcIntegrationSettingsPage::gpusPerNode() const { return m_gpusBox->value(); }
QString HpcIntegrationSettingsPage::spackRoot() const { return m_spackRootEdit->text().trimmed(); }

/**
 * @brief Restores remote SSH, SLURM, and Spack configurations from ConfigManager.
 */
void HpcIntegrationSettingsPage::loadFromSettings() {
    auto& mgr = gridlock::core::ConfigManager::instance();
    const auto ssh = mgr.getSshSettings();
    const auto slurm = mgr.getSlurmSettings();

    m_sshHostEdit->setText(ssh.host);
    m_sshUserEdit->setText(ssh.user);
    m_sshKeyEdit->setText(ssh.keyPath);

    m_templateEdit->setPlainText(slurm.scriptTemplate);
    m_partitionEdit->setText(slurm.partition);
    m_nodesBox->setValue(slurm.nodes);
    m_tasksBox->setValue(slurm.tasksPerNode);
    m_gpuCheck->setChecked(slurm.requestGpus);
    m_gpusBox->setValue(slurm.gpusPerNode);
    m_spackRootEdit->setText(slurm.spackRoot);
}

// ============================================================================
// DocsetSettingsPage Implementation
// ============================================================================

/**
 * @brief Constructs offline documentation docset management page.
 * @param parent Optional parent widget.
 */
DocsetSettingsPage::DocsetSettingsPage(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(14);

    auto* heading = new QLabel(tr("<b>Offline Docsets</b>"), this);
    heading->setObjectName("heading");
    layout->addWidget(heading);

    auto* separator = new QLabel(this);
    separator->setFixedHeight(1);
    separator->setObjectName("separator");
    layout->addWidget(separator);

    // Directory selection row with Browse dialog and 1-click Zeal auto-detection.
    auto* dirLayout = new QHBoxLayout();
    dirLayout->addWidget(new QLabel("Docset Directory:", this));
    m_dirEdit = new QLineEdit(this);
    dirLayout->addWidget(m_dirEdit);
    auto* browseBtn = new QPushButton("Browse...", this);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        // Resolve default Zeal docsets directory under XDG user data or fallback to Home.
        const QString zealDir = QDir::homePath() + QStringLiteral("/.local/share/Zeal/Zeal/docsets");
        QString startDir = m_dirEdit->text().trimmed();
        if (startDir.isEmpty() || startDir == QStringLiteral("/") || !QDir(startDir).exists()) {
            startDir = QDir(zealDir).exists() ? zealDir : QDir::homePath();
        }
        // Configure non-native directory chooser with quick-jump sidebar URLs.
        QFileDialog dlg(this, tr("Select Docset Directory"), startDir);
        dlg.setFileMode(QFileDialog::Directory);
        dlg.setOption(QFileDialog::ShowDirsOnly, true);
        dlg.setOption(QFileDialog::DontUseNativeDialog, true);
        // Populate left sidebar with Home, Zeal Docsets, Documents, and workspace root.
        dlg.setSidebarUrls({
            QUrl::fromLocalFile(QDir::homePath()),
            QUrl::fromLocalFile(zealDir),
            QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)),
            QUrl::fromLocalFile(QDir::currentPath())
        });
        // Persist selected directory path and immediately reload docset indices.
        if (dlg.exec() == QDialog::Accepted && !dlg.selectedFiles().isEmpty()) {
            const QString chosen = dlg.selectedFiles().first();
            m_dirEdit->setText(chosen);
            gridlock::core::DocsetManager::instance().setDocsetDirectory(chosen);
            refreshTable();
        }
    });
    dirLayout->addWidget(browseBtn);

    // One-click button to discover and attach installed Zeal docsets from standard paths.
    auto* detectZealBtn = new QPushButton("Auto-Detect Zeal Docsets", this);
    connect(detectZealBtn, &QPushButton::clicked, this, [this]() {
        // Prefer ~/.local/share/Zeal/Zeal/docsets when present on the host system.
        const QString zealDir = QDir::homePath() + QStringLiteral("/.local/share/Zeal/Zeal/docsets");
        if (QDir(zealDir).exists()) {
            m_dirEdit->setText(zealDir);
        }
        // Scan all system and user Zeal paths and repopulate the installed docsets table.
        gridlock::core::DocsetManager::instance().discoverAndLoadSystemDocsets();
        refreshTable();
    });
    dirLayout->addWidget(detectZealBtn);
    layout->addLayout(dirLayout);

    // Heuristic source-tree scanner button recommending relevant HPC/C++ docsets.
    m_autoDetectBtn = new QPushButton("Auto-Detect Needs", this);
    connect(m_autoDetectBtn, &QPushButton::clicked, this,
            &DocsetSettingsPage::onAutoDetect);
    layout->addWidget(m_autoDetectBtn, 0, Qt::AlignLeft);

    // Configure borderless docsets table with alternating row colors and 48px minimum section size.
    m_table = new QTableWidget(0, 1, this);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setHorizontalHeaderLabels({"Installed Docsets"});
    m_table->horizontalHeader()->setMinimumSectionSize(48);
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table);

    loadFromSettings();
}

QString DocsetSettingsPage::docsetDirectory() const { return m_dirEdit->text(); }

/**
 * @brief Restores active docset directory path from settings or defaults to ~/.local/share/Zeal.
 */
void DocsetSettingsPage::loadFromSettings() {
    // Avoid defaulting to root `/` when no custom docset directory has been saved yet.
    QString configuredDir = gridlock::core::ConfigManager::instance().getDocsetDirectory().trimmed();
    const QString zealDir = QDir::homePath() + QStringLiteral("/.local/share/Zeal/Zeal/docsets");
    if (configuredDir.isEmpty() || configuredDir == QStringLiteral("/")) {
        configuredDir = QDir(zealDir).exists() ? zealDir : QDir::homePath();
    }
    m_dirEdit->setText(configuredDir);
    refreshTable();
}

/**
 * @brief Refreshes the table view with paths of all discovered docset bundles.
 */
void DocsetSettingsPage::refreshTable() {
    m_table->setRowCount(0);
    const auto paths = gridlock::core::DocsetManager::instance().getActiveDocsetPaths();
    for (int i = 0; i < paths.size(); ++i) {
        m_table->insertRow(i);
        m_table->setItem(i, 0, new QTableWidgetItem(paths[i]));
    }
}

/**
 * @brief Runs background heuristic detection of project languages to recommend docsets.
 */
void DocsetSettingsPage::onAutoDetect() {
    m_autoDetectBtn->setText("Detecting...");
    m_autoDetectBtn->setEnabled(false);

    auto* watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this,
            [this, watcher]() {
                const QStringList suggestions = watcher->result();
                m_autoDetectBtn->setText(
                    suggestions.isEmpty() ? "No extra docsets needed"
                                          : "Suggested: " + suggestions.join(", "));
                m_autoDetectBtn->setEnabled(true);
                watcher->deleteLater();
            });

    watcher->setFuture(
        gridlock::core::DocsetManager::instance().suggestDocsets(QDir::currentPath()));
}

// ============================================================================
// TerminalSettingsPage Implementation
// ============================================================================

/**
 * @brief Constructs embedded terminal emulator configuration page.
 * @param parent Optional parent widget.
 */
TerminalSettingsPage::TerminalSettingsPage(QWidget* parent)
    : QWidget(parent) {
    auto* form = createStandardPageForm(this, tr("<b>Terminal</b>"));

    // Shell executable path browse row.
    m_shellPathEdit = addBrowsePathRow(
        this, form, tr("Shell Path:"), tr("/bin/bash"),
        tr("Path to the shell executable (e.g. /bin/bash, /bin/zsh). Requires terminal restart."),
        tr("Select Shell Executable"), QStringLiteral("/bin"));

    // Terminal monospace font family combo.
    m_fontFamilyCombo = new QComboBox(this);
    m_fontFamilyCombo->addItems({"FiraCode Nerd Font", "Hack", "Monospace"});
    m_fontFamilyCombo->setToolTip(tr("Monospace font for the embedded terminal."));
    form->addRow(tr("Font Family:"), m_fontFamilyCombo);

    // Terminal font size spinner.
    m_fontSizeBox = new QSpinBox(this);
    m_fontSizeBox->setRange(6, 72);
    m_fontSizeBox->setToolTip(tr("Font size in points for the terminal."));
    form->addRow(tr("Font Size:"), m_fontSizeBox);

    loadFromSettings();
}

QString TerminalSettingsPage::shellPath() const { return m_shellPathEdit->text().trimmed(); }
QString TerminalSettingsPage::fontFamily() const { return m_fontFamilyCombo->currentText(); }
int TerminalSettingsPage::fontSize() const { return m_fontSizeBox->value(); }

/**
 * @brief Loads terminal font and shell preferences from ConfigManager.
 */
void TerminalSettingsPage::loadFromSettings() {
    const auto ts = gridlock::core::ConfigManager::instance().getTerminalSettings();
    m_shellPathEdit->setText(ts.shellPath);
    const int fIdx = m_fontFamilyCombo->findText(ts.fontFamily);
    if (fIdx >= 0) {
        m_fontFamilyCombo->setCurrentIndex(fIdx);
    } else {
        m_fontFamilyCombo->setCurrentText(ts.fontFamily);
    }
    m_fontSizeBox->setValue(ts.fontSize);
}

}  // namespace gridlock::ui
