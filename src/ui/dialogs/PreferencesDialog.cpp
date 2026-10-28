// PreferencesDialog.cpp
// Implements a side-bar preferences dialog.
// MPI / GDB settings are owned exclusively by DebuggerSettingsPage and
// persisted through ConfigManager.

#include "PreferencesDialog.hpp"
#include "../../core/managers/ConfigManager.hpp"
#include "../../core/managers/DocsetManager.hpp"
#include "../../core/managers/ThemeManager.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWindow>

namespace gridlock::ui {

namespace {

QFormLayout *createStandardPageForm(QWidget *parent, const QString &titleHtml,
                                    int spacing = 14) {
  auto *form = new QFormLayout(parent);
  form->setContentsMargins(24, 24, 24, 24);
  form->setSpacing(spacing);
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

  auto *heading = new QLabel(titleHtml, parent);
  heading->setObjectName("heading");
  form->addRow(heading);

  auto *separator = new QLabel(parent);
  separator->setFixedHeight(1);
  separator->setObjectName("separator");
  form->addRow(separator);
  return form;
}

QLineEdit *addBrowsePathRow(QWidget *parent, QFormLayout *form,
                            const QString &rowLabel,
                            const QString &placeholder,
                            const QString &toolTip,
                            const QString &dialogTitle,
                            const QString &defaultDir = QString()) {
  auto *row = new QHBoxLayout();
  auto *edit = new QLineEdit(parent);
  edit->setPlaceholderText(placeholder);
  if (!toolTip.isEmpty()) {
    edit->setToolTip(toolTip);
  }
  auto *browseBtn = new QPushButton(QObject::tr("Browse…"), parent);
  browseBtn->setFixedWidth(72);
  QObject::connect(browseBtn, &QPushButton::clicked, parent,
                   [parent, edit, dialogTitle, defaultDir]() {
                     QString path = QFileDialog::getOpenFileName(
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

void addFormNote(QWidget *parent, QFormLayout *form, const QString &html) {
  auto *note = new QLabel(html, parent);
  note->setWordWrap(true);
  form->addRow(QString(), note);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
//  AppearanceSettingsPage
// ═══════════════════════════════════════════════════════════════════════════

AppearanceSettingsPage::AppearanceSettingsPage(QWidget *parent)
    : QWidget(parent) {
  auto *form = createStandardPageForm(this, tr("<b>Appearance</b>"));

  m_themeCombo = new QComboBox(this);
  m_themeCombo->addItems(
      gridlock::core::managers::ThemeManager::instance().getAvailableThemes());
  m_themeCombo->setToolTip(
      tr("Select the application color theme. Changes are applied immediately."));
  form->addRow(tr("Color Theme:"), m_themeCombo);

  m_darkModeCheck = new QCheckBox(tr("Enable Dark Mode"), this);
  form->addRow(QString(), m_darkModeCheck);

  connect(m_themeCombo, &QComboBox::currentTextChanged, this,
          [this](const QString &theme) {
            gridlock::core::managers::ThemeManager::instance().setTheme(
                theme, m_darkModeCheck->isChecked());
          });
  connect(m_darkModeCheck, &QCheckBox::toggled, this, [](bool) {});

  m_fileTreeStyleCombo = new QComboBox(this);
  m_fileTreeStyleCombo->addItems({tr("Compact"), tr("Comfortable"), tr("Large")});
  m_fileTreeStyleCombo->setToolTip(
      tr("Density of items in the Project Explorer tree."));
  form->addRow(tr("File Tree Style:"), m_fileTreeStyleCombo);

  m_colorizeIconsCheck = new QCheckBox(tr("Enable Colorful File Icons"), this);
  m_colorizeIconsCheck->setToolTip(
      tr("Render file tree icons using vibrant colors based on file type."));
  form->addRow(QString(), m_colorizeIconsCheck);

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

  loadFromSettings();
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

void AppearanceSettingsPage::loadFromSettings() {
  QSettings s("gridlock", "debugger");
  const QString theme = s.value("appearance/theme", "dark_teal").toString();
  bool isDark = s.value("appearance/dark_mode", true).toBool();

  m_darkModeCheck->blockSignals(true);
  m_darkModeCheck->setChecked(isDark);
  m_darkModeCheck->blockSignals(false);

  int idx = m_themeCombo->findText(theme);
  if (idx >= 0) {
    m_themeCombo->setCurrentIndex(idx);
  }
  int ftsIdx = m_fileTreeStyleCombo->findText(
      s.value("appearance/file_tree_style", "Comfortable").toString());
  if (ftsIdx >= 0) {
    m_fileTreeStyleCombo->setCurrentIndex(ftsIdx);
  }
  m_colorizeIconsCheck->setChecked(
      s.value("appearance/colorize_icons", true).toBool());
  m_uiFontSizeBox->setValue(s.value("appearance/ui_font_size", 11).toInt());
  m_codeFontSizeBox->setValue(s.value("appearance/code_font_size", 11).toInt());
}

// ═══════════════════════════════════════════════════════════════════════════
//  EditingSettingsPage
// ═══════════════════════════════════════════════════════════════════════════

EditingSettingsPage::EditingSettingsPage(QWidget *parent) : QWidget(parent) {
  auto *form = createStandardPageForm(this, tr("<b>Editing</b>"));

  m_tabWidthBox = new QSpinBox(this);
  m_tabWidthBox->setRange(1, 16);
  m_tabWidthBox->setValue(4);
  m_tabWidthBox->setToolTip(
      tr("Number of spaces that represent one tab stop in the source view."));
  form->addRow(tr("Tab Width:"), m_tabWidthBox);

  m_indentModeCombo = new QComboBox(this);
  m_indentModeCombo->addItems({tr("Spaces"), tr("Tabs")});
  m_indentModeCombo->setToolTip(
      tr("Whether to insert spaces or a real tab character when Tab is pressed."));
  form->addRow(tr("Indent With:"), m_indentModeCombo);

  m_whitespaceCombo = new QComboBox(this);
  m_whitespaceCombo->addItems({tr("Hidden"), tr("Visible")});
  m_whitespaceCombo->setToolTip(
      tr("Show invisible whitespace characters in the editor."));
  form->addRow(tr("Whitespace:"), m_whitespaceCombo);

  m_wordWrapCheck = new QCheckBox(tr("Enable Word Wrap"), this);
  m_wordWrapCheck->setToolTip(tr("Wrap long lines at the edge of the editor view."));
  form->addRow(QString(), m_wordWrapCheck);

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

// ═══════════════════════════════════════════════════════════════════════════
//  BehaviorSettingsPage
// ═══════════════════════════════════════════════════════════════════════════

BehaviorSettingsPage::BehaviorSettingsPage(QWidget *parent) : QWidget(parent) {
  auto *form = createStandardPageForm(this, tr("<b>Behavior</b>"));

  m_restoreBreakpointsCheck = new QComboBox(this);
  m_restoreBreakpointsCheck->addItems({tr("Always"), tr("Ask"), tr("Never")});
  m_restoreBreakpointsCheck->setToolTip(
      tr("Whether to reload saved breakpoints when a source file is opened."));
  form->addRow(tr("Restore Breakpoints:"), m_restoreBreakpointsCheck);

  m_confirmQuitCheck = new QComboBox(this);
  m_confirmQuitCheck->addItems({tr("Yes"), tr("No")});
  m_confirmQuitCheck->setToolTip(
      tr("Ask for confirmation before terminating an active debug session."));
  form->addRow(tr("Confirm Quit:"), m_confirmQuitCheck);

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

void BehaviorSettingsPage::loadFromSettings() {
  QSettings s("gridlock", "debugger");
  int rbIdx = m_restoreBreakpointsCheck->findText(
      s.value("behavior/restore_breakpoints", "Always").toString());
  if (rbIdx >= 0) {
    m_restoreBreakpointsCheck->setCurrentIndex(rbIdx);
  }
  m_confirmQuitCheck->setCurrentIndex(
      s.value("behavior/confirm_quit", true).toBool() ? 0 : 1);
  m_focusOnStopCheck->setCurrentIndex(
      s.value("behavior/focus_on_stop", true).toBool() ? 0 : 1);
}

// ═══════════════════════════════════════════════════════════════════════════
//  DebuggerSettingsPage
// ═══════════════════════════════════════════════════════════════════════════

DebuggerSettingsPage::DebuggerSettingsPage(QWidget *parent) : QWidget(parent) {
  auto *form = createStandardPageForm(this, tr("<b>Debugger</b>"));

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

  m_mpiExecEdit = new QLineEdit(this);
  m_mpiExecEdit->setPlaceholderText("mpiexec");
  m_mpiExecEdit->setToolTip(
      tr("MPI launcher used when attaching the debugger to MPI ranks."));
  form->addRow(tr("MPI Executable:"), m_mpiExecEdit);

  m_mpiArgsEdit = new QLineEdit(this);
  m_mpiArgsEdit->setPlaceholderText("--oversubscribe");
  m_mpiArgsEdit->setToolTip(
      tr("Extra flags forwarded to the MPI launcher on every debug session."));
  form->addRow(tr("MPI Args:"), m_mpiArgsEdit);

  m_rankBox = new QSpinBox(this);
  m_rankBox->setRange(1, 256);
  m_rankBox->setToolTip(
      tr("Default number of MPI ranks for debugger sessions (minimum 1)."));
  form->addRow(tr("Default Rank Count:"), m_rankBox);

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

// ═════════════════════════════════════════════════════════════════════════
//  HpcSettingsPage
// ═════════════════════════════════════════════════════════════════════════

HpcSettingsPage::HpcSettingsPage(QWidget *parent) : QWidget(parent) {
  auto *form = createStandardPageForm(this, tr("<b>HPC / Cluster</b>"));

  auto *hostsRow = new QHBoxLayout();
  m_hostsFileEdit = new QLineEdit(this);
  m_hostsFileEdit->setPlaceholderText(tr("(not set — use default hosts)"));
  m_hostsFileEdit->setToolTip(
      tr("Path to an MPI hostfile. When set, --hostfile <path> is passed to the\n"
         "MPI launcher. Leave empty to let the launcher choose nodes automatically."));
  auto *browseBtn = new QPushButton(tr("Browse…"), this);
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

  m_envVarsEdit = new QTextEdit(this);
  m_envVarsEdit->setFixedHeight(90);
  m_envVarsEdit->setPlaceholderText(
      tr("One key=value pair per line\ne.g.  OMPI_MCA_btl=tcp,self\n      UCX_NET_DEVICES=eth0"));
  m_envVarsEdit->setToolTip(
      tr("MPI environment variables injected via -x KEY=VALUE flags.\nOne entry per line: KEY=VALUE"));
  m_envVarsEdit->setAcceptRichText(false);
  form->addRow(tr("MPI Env Vars:"), m_envVarsEdit);

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

void HpcSettingsPage::loadFromSettings() {
  const auto hs = gridlock::core::ConfigManager::instance().getHpcSettings();
  m_hostsFileEdit->setText(hs.hostsFile);
  m_envVarsEdit->setPlainText(hs.envVars);
  m_strictAffinityBox->setChecked(hs.strictAffinity);
}

// ═════════════════════════════════════════════════════════════════════════
//  HpcIntegrationSettingsPage
// ═════════════════════════════════════════════════════════════════════════

HpcIntegrationSettingsPage::HpcIntegrationSettingsPage(QWidget *parent)
    : QWidget(parent) {
  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->addWidget(scroll);

  auto *container = new QWidget();
  scroll->setWidget(container);
  auto *form = new QFormLayout(container);
  form->setContentsMargins(24, 24, 24, 24);
  form->setSpacing(12);
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

  auto addSectionHeading = [&](const QString &title) {
    auto *lbl = new QLabel(title, container);
    lbl->setObjectName("heading");
    form->addRow(lbl);
  };

  addSectionHeading(tr("<b>Remote SSH Connection</b>"));

  m_sshHostEdit = new QLineEdit(container);
  m_sshHostEdit->setPlaceholderText(tr("e.g. login.hpc.university.edu"));
  m_sshHostEdit->setToolTip(tr("Hostname or IP of the HPC login node."));
  form->addRow(tr("SSH Host:"), m_sshHostEdit);

  m_sshUserEdit = new QLineEdit(container);
  m_sshUserEdit->setPlaceholderText(tr("your_hpc_username"));
  form->addRow(tr("SSH User:"), m_sshUserEdit);

  auto *keyRow = new QHBoxLayout();
  m_sshKeyEdit = new QLineEdit(container);
  m_sshKeyEdit->setPlaceholderText(tr("~/.ssh/id_rsa"));
  m_sshKeyEdit->setToolTip(
      tr("Path to the private SSH key file used for authentication."));
  auto *keyBrowse = new QPushButton(tr("Browse…"), container);
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

  auto *gpuRow = new QHBoxLayout();
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

  auto *tplLabel = new QLabel(
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

void HpcIntegrationSettingsPage::loadFromSettings() {
  auto &mgr = gridlock::core::ConfigManager::instance();
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

// ═══════════════════════════════════════════════════════════════════════════
//  DocsetSettingsPage
// ═══════════════════════════════════════════════════════════════════════════

DocsetSettingsPage::DocsetSettingsPage(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 24, 24, 24);
  layout->setSpacing(14);

  auto *heading = new QLabel(tr("<b>Offline Docsets</b>"), this);
  heading->setObjectName("heading");
  layout->addWidget(heading);

  auto *separator = new QLabel(this);
  separator->setFixedHeight(1);
  separator->setObjectName("separator");
  layout->addWidget(separator);

  auto *dirLayout = new QHBoxLayout();
  dirLayout->addWidget(new QLabel("Docset Directory:", this));
  m_dirEdit = new QLineEdit(this);
  dirLayout->addWidget(m_dirEdit);
  auto *browseBtn = new QPushButton("Browse...", this);
  connect(browseBtn, &QPushButton::clicked, this, [this]() {
    QString dir = QFileDialog::getExistingDirectory(
        this, "Select Docset Directory", m_dirEdit->text());
    if (!dir.isEmpty()) {
      m_dirEdit->setText(dir);
    }
  });
  dirLayout->addWidget(browseBtn);
  layout->addLayout(dirLayout);

  m_autoDetectBtn = new QPushButton("Auto-Detect Needs", this);
  connect(m_autoDetectBtn, &QPushButton::clicked, this,
          &DocsetSettingsPage::onAutoDetect);
  layout->addWidget(m_autoDetectBtn, 0, Qt::AlignLeft);

  m_table = new QTableWidget(0, 1, this);
  m_table->setHorizontalHeaderLabels({"Installed Docsets"});
  m_table->horizontalHeader()->setStretchLastSection(true);
  layout->addWidget(m_table);

  loadFromSettings();
}

QString DocsetSettingsPage::docsetDirectory() const { return m_dirEdit->text(); }

void DocsetSettingsPage::loadFromSettings() {
  m_dirEdit->setText(
      gridlock::core::ConfigManager::instance().getDocsetDirectory());
  if (m_dirEdit->text().isEmpty()) {
    m_dirEdit->setText(QDir::homePath() + "/.local/share/Zeal/Zeal/docsets");
  }
  refreshTable();
}

void DocsetSettingsPage::refreshTable() {
  m_table->setRowCount(0);
  auto paths = gridlock::core::DocsetManager::instance().getActiveDocsetPaths();
  for (int i = 0; i < paths.size(); ++i) {
    m_table->insertRow(i);
    m_table->setItem(i, 0, new QTableWidgetItem(paths[i]));
  }
}

void DocsetSettingsPage::onAutoDetect() {
  m_autoDetectBtn->setText("Detecting...");
  m_autoDetectBtn->setEnabled(false);

  auto *watcher = new QFutureWatcher<QStringList>(this);
  connect(watcher, &QFutureWatcher<QStringList>::finished, this,
          [this, watcher]() {
            QStringList suggestions = watcher->result();
            m_autoDetectBtn->setText(
                suggestions.isEmpty() ? "No extra docsets needed"
                                      : "Suggested: " + suggestions.join(", "));
            m_autoDetectBtn->setEnabled(true);
            watcher->deleteLater();
          });

  watcher->setFuture(
      gridlock::core::DocsetManager::instance().suggestDocsets(QDir::currentPath()));
}

// ═══════════════════════════════════════════════════════════════════════════
//  TerminalSettingsPage
// ═══════════════════════════════════════════════════════════════════════════

TerminalSettingsPage::TerminalSettingsPage(QWidget *parent) : QWidget(parent) {
  auto *form = createStandardPageForm(this, tr("<b>Terminal</b>"));

  m_shellPathEdit = addBrowsePathRow(
      this, form, tr("Shell Path:"), tr("/bin/bash"),
      tr("Path to the shell executable (e.g. /bin/bash, /bin/zsh). Requires terminal restart."),
      tr("Select Shell Executable"), QStringLiteral("/bin"));

  m_fontFamilyCombo = new QComboBox(this);
  m_fontFamilyCombo->addItems({"FiraCode Nerd Font", "Hack", "Monospace"});
  m_fontFamilyCombo->setToolTip(tr("Monospace font for the embedded terminal."));
  form->addRow(tr("Font Family:"), m_fontFamilyCombo);

  m_fontSizeBox = new QSpinBox(this);
  m_fontSizeBox->setRange(6, 72);
  m_fontSizeBox->setToolTip(tr("Font size in points for the terminal."));
  form->addRow(tr("Font Size:"), m_fontSizeBox);

  loadFromSettings();
}

QString TerminalSettingsPage::shellPath() const { return m_shellPathEdit->text().trimmed(); }
QString TerminalSettingsPage::fontFamily() const { return m_fontFamilyCombo->currentText(); }
int TerminalSettingsPage::fontSize() const { return m_fontSizeBox->value(); }

void TerminalSettingsPage::loadFromSettings() {
  const auto ts = gridlock::core::ConfigManager::instance().getTerminalSettings();
  m_shellPathEdit->setText(ts.shellPath);
  int fIdx = m_fontFamilyCombo->findText(ts.fontFamily);
  if (fIdx >= 0) {
    m_fontFamilyCombo->setCurrentIndex(fIdx);
  } else {
    m_fontFamilyCombo->setCurrentText(ts.fontFamily);
  }
  m_fontSizeBox->setValue(ts.fontSize);
}

// ═══════════════════════════════════════════════════════════════════════════
//  PreferencesDialog
// ═══════════════════════════════════════════════════════════════════════════

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Preferences — GridLock"));
  setMinimumSize(780, 540);
  resize(860, 580);

  auto *rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  auto *splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setHandleWidth(1);
  splitter->setChildrenCollapsible(false);

  setupSidebar();
  setupPages();

  splitter->addWidget(m_sidebar);
  splitter->addWidget(m_stack);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({190, 670});

  rootLayout->addWidget(splitter, 1);

  auto *line = new QLabel(this);
  line->setFixedHeight(1);
  line->setObjectName("separator");
  rootLayout->addWidget(line);

  setupButtonBox();
  rootLayout->addWidget(m_buttonBox);
}

void PreferencesDialog::setupSidebar() {
  m_sidebar = new QListWidget(this);
  m_sidebar->setFixedWidth(190);
  m_sidebar->setIconSize(QSize(20, 20));
  m_sidebar->setSpacing(0);
  m_sidebar->setFrameShape(QFrame::NoFrame);
  m_sidebar->setProperty("role", "sidebar");

  struct Entry {
    QString label;
    QString iconName;
  };
  const QList<Entry> entries = {
      {tr("Appearance"), "\uf1fc"},      {tr("Editing"), "\uf044"},
      {tr("Behavior"), "\uf085"},        {tr("Debugger"), "\uf188"},
      {tr("HPC / Cluster"), "\uf233"},   {tr("HPC Integration"), "\uf1c0"},
      {tr("Terminal"), "\uf120"},        {tr("Docsets"), "\uf02d"},
  };

  for (const auto &e : entries) {
    auto *item = new QListWidgetItem(m_sidebar);
    item->setText(e.label);

    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(QFont("Symbols Nerd Font", 14));
    painter.setPen(QColor("#cdd6f4"));
    painter.drawText(0, 0, 20, 20, Qt::AlignCenter, e.iconName);
    item->setIcon(QIcon(pixmap));

    item->setSizeHint(QSize(190, 44));
    m_sidebar->addItem(item);
  }

  m_sidebar->setCurrentRow(0);
  connect(m_sidebar, &QListWidget::currentRowChanged, this, [this](int row) {
    if (m_stack) {
      m_stack->setCurrentIndex(row);
    }
  });
}

void PreferencesDialog::setupPages() {
  m_stack = new QStackedWidget(this);

  m_appearancePage = new AppearanceSettingsPage(m_stack);
  m_editingPage = new EditingSettingsPage(m_stack);
  m_behaviorPage = new BehaviorSettingsPage(m_stack);
  m_debuggerPage = new DebuggerSettingsPage(m_stack);
  m_hpcPage = new HpcSettingsPage(m_stack);
  m_hpcIntegrationPage = new HpcIntegrationSettingsPage(m_stack);
  m_terminalPage = new TerminalSettingsPage(m_stack);
  m_docsetPage = new DocsetSettingsPage(m_stack);

  for (QWidget *page : {static_cast<QWidget *>(m_appearancePage),
                        static_cast<QWidget *>(m_editingPage),
                        static_cast<QWidget *>(m_behaviorPage),
                        static_cast<QWidget *>(m_debuggerPage),
                        static_cast<QWidget *>(m_hpcPage),
                        static_cast<QWidget *>(m_hpcIntegrationPage),
                        static_cast<QWidget *>(m_terminalPage),
                        static_cast<QWidget *>(m_docsetPage)}) {
    m_stack->addWidget(page);
  }
  m_stack->setCurrentIndex(0);
}

void PreferencesDialog::setupButtonBox() {
  m_buttonBox = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel,
      Qt::Horizontal, this);
  m_buttonBox->setContentsMargins(12, 10, 12, 10);

  connect(m_buttonBox, &QDialogButtonBox::accepted, this,
          &PreferencesDialog::acceptAndApply);
  connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
          this, &PreferencesDialog::apply);
}

void PreferencesDialog::apply() {
  QSettings s("gridlock", "debugger");

  s.setValue("appearance/theme", m_appearancePage->selectedTheme());
  s.setValue("appearance/dark_mode", m_appearancePage->isDarkMode());
  s.setValue("appearance/file_tree_style", m_appearancePage->fileTreeStyle());
  s.setValue("appearance/colorize_icons", m_appearancePage->colorizeIcons());
  s.setValue("appearance/ui_font_size", m_appearancePage->uiFontSize());
  s.setValue("appearance/code_font_size", m_appearancePage->codeFontSize());

  s.setValue("editing/tab_width", m_editingPage->tabWidth());
  s.setValue("editing/insert_spaces", m_editingPage->insertSpaces());
  s.setValue("editing/show_whitespace", m_editingPage->showWhitespace());
  s.setValue("editing/word_wrap", m_editingPage->wordWrap());
  s.setValue("editing/show_edge_column", m_editingPage->showEdgeColumn());
  s.setValue("editing/edge_column", m_editingPage->edgeColumn());

  s.setValue("behavior/restore_breakpoints",
             m_behaviorPage->restoreBreakpoints());
  s.setValue("behavior/confirm_quit", m_behaviorPage->confirmQuit());
  s.setValue("behavior/focus_on_stop", m_behaviorPage->focusOnStop());

  auto &cfg = gridlock::core::ConfigManager::instance();
  cfg.setGlobalClangdPath(m_debuggerPage->clangdPath());
  cfg.setGlobalDapAdapterPath(m_debuggerPage->dapAdapterPath());

  gridlock::core::DebuggerSettings ds;
  ds.gdbPath = m_debuggerPage->gdbPath();
  ds.mpiExecutable = m_debuggerPage->mpiExecutable();
  ds.mpiArgs = m_debuggerPage->mpiArgs();
  ds.defaultRanks = m_debuggerPage->defaultRanks();
  ds.trapFpe = m_debuggerPage->trapFpe();
  cfg.saveDebuggerSettings(ds);

  auto ps = cfg.loadProjectSettings();
  ps.customGdbPath = m_debuggerPage->gdbPath().toStdString();
  cfg.saveProjectSettings(ps);

  gridlock::core::HpcSettings hs;
  hs.hostsFile = m_hpcPage->hostsFile();
  hs.envVars = m_hpcPage->envVars();
  hs.strictAffinity = m_hpcPage->strictAffinity();
  cfg.saveHpcSettings(hs);

  gridlock::core::SshSettings ssh;
  ssh.host = m_hpcIntegrationPage->sshHost();
  ssh.user = m_hpcIntegrationPage->sshUser();
  ssh.keyPath = m_hpcIntegrationPage->sshKeyPath();
  cfg.saveSshSettings(ssh);

  gridlock::core::SlurmSettings slurm;
  slurm.scriptTemplate = m_hpcIntegrationPage->slurmTemplate();
  slurm.partition = m_hpcIntegrationPage->slurmPartition();
  slurm.nodes = m_hpcIntegrationPage->slurmNodes();
  slurm.tasksPerNode = m_hpcIntegrationPage->slurmTasks();
  slurm.requestGpus = m_hpcIntegrationPage->requestGpus();
  slurm.gpusPerNode = m_hpcIntegrationPage->gpusPerNode();
  slurm.spackRoot = m_hpcIntegrationPage->spackRoot();
  cfg.saveSlurmSettings(slurm);

  auto &docMgr = gridlock::core::DocsetManager::instance();
  if (docMgr.getDocsetDirectory() != m_docsetPage->docsetDirectory()) {
    docMgr.setDocsetDirectory(m_docsetPage->docsetDirectory());
    m_docsetPage->refreshTable();
  }

  gridlock::core::TerminalSettings ts;
  ts.shellPath = m_terminalPage->shellPath();
  ts.fontFamily = m_terminalPage->fontFamily();
  ts.fontSize = m_terminalPage->fontSize();
  cfg.saveTerminalSettings(ts);

  s.sync();
  emit preferencesChanged();
}

void PreferencesDialog::acceptAndApply() {
  apply();
  accept();
}

void PreferencesDialog::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && window()->windowHandle()) {
    window()->windowHandle()->startSystemMove();
  } else {
    QDialog::mousePressEvent(event);
  }
}

void PreferencesDialog::mouseMoveEvent(QMouseEvent *event) {
  QDialog::mouseMoveEvent(event);
}

} // namespace gridlock::ui