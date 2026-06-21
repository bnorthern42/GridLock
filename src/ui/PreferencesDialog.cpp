// PreferencesDialog.cpp
// Implements a Kate/KDevelop-style side-bar preferences dialog.
// MPI / GDB settings are owned exclusively by DebuggerSettingsPage and
// persisted through ConfigManager::saveDebuggerSettings() — the single
// source of truth for those values.

#include "PreferencesDialog.hpp"
#include "../core/ConfigManager.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace gridlock::ui {

// ═══════════════════════════════════════════════════════════════════════════
//  AppearanceSettingsPage
// ═══════════════════════════════════════════════════════════════════════════

AppearanceSettingsPage::AppearanceSettingsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *form = new QFormLayout(this);
    form->setContentsMargins(24, 24, 24, 24);
    form->setSpacing(14);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *heading = new QLabel(tr("<b>Appearance</b>"), this);
    heading->setStyleSheet("font-size: 15px; color: #8ab4f8; padding-bottom: 4px;");
    form->addRow(heading);

    auto *separator = new QLabel(this);
    separator->setFixedHeight(1);
    separator->setStyleSheet("background: rgba(255,255,255,0.08); margin-bottom: 8px;");
    form->addRow(separator);

    m_themeCombo = new QComboBox(this);
    m_themeCombo->addItems({tr("System Default"), tr("Breeze Dark"), tr("Breeze Light")});
    m_themeCombo->setToolTip(tr("Select the application color theme. Changes are applied immediately."));
    form->addRow(tr("Color Theme:"), m_themeCombo);

    auto *fontNote = new QLabel(
        tr("<small style='color:#888;'>Font settings follow the system monospace font.<br>"
           "Restart may be required for full theme reload.</small>"),
        this);
    fontNote->setWordWrap(true);
    form->addRow(QString(), fontNote);

    loadFromSettings();
}

QString AppearanceSettingsPage::selectedTheme() const
{
    return m_themeCombo->currentText();
}

void AppearanceSettingsPage::loadFromSettings()
{
    QSettings s("GridLock", "Debugger");
    const QString theme = s.value("appearance/theme", "System Default").toString();
    int idx = m_themeCombo->findText(theme);
    if (idx >= 0) m_themeCombo->setCurrentIndex(idx);
}

// ═══════════════════════════════════════════════════════════════════════════
//  EditingSettingsPage
// ═══════════════════════════════════════════════════════════════════════════

EditingSettingsPage::EditingSettingsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *form = new QFormLayout(this);
    form->setContentsMargins(24, 24, 24, 24);
    form->setSpacing(14);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *heading = new QLabel(tr("<b>Editing</b>"), this);
    heading->setStyleSheet("font-size: 15px; color: #8ab4f8; padding-bottom: 4px;");
    form->addRow(heading);

    auto *separator = new QLabel(this);
    separator->setFixedHeight(1);
    separator->setStyleSheet("background: rgba(255,255,255,0.08); margin-bottom: 8px;");
    form->addRow(separator);

    m_tabWidthBox = new QSpinBox(this);
    m_tabWidthBox->setRange(1, 16);
    m_tabWidthBox->setValue(4);
    m_tabWidthBox->setToolTip(tr("Number of spaces that represent one tab stop in the source view."));
    form->addRow(tr("Tab Width:"), m_tabWidthBox);

    m_indentModeCombo = new QComboBox(this);
    m_indentModeCombo->addItems({tr("Spaces"), tr("Tabs")});
    m_indentModeCombo->setToolTip(tr("Whether to insert spaces or a real tab character when Tab is pressed."));
    form->addRow(tr("Indent With:"), m_indentModeCombo);

    m_whitespaceCombo = new QComboBox(this);
    m_whitespaceCombo->addItems({tr("Hidden"), tr("Visible")});
    m_whitespaceCombo->setToolTip(tr("Show invisible whitespace characters in the editor."));
    form->addRow(tr("Whitespace:"), m_whitespaceCombo);

    loadFromSettings();
}

int  EditingSettingsPage::tabWidth()       const { return m_tabWidthBox->value(); }
bool EditingSettingsPage::insertSpaces()   const { return m_indentModeCombo->currentIndex() == 0; }
bool EditingSettingsPage::showWhitespace() const { return m_whitespaceCombo->currentIndex() == 1; }

void EditingSettingsPage::loadFromSettings()
{
    QSettings s("GridLock", "Debugger");
    m_tabWidthBox->setValue(s.value("editing/tab_width", 4).toInt());
    m_indentModeCombo->setCurrentIndex(s.value("editing/insert_spaces", true).toBool() ? 0 : 1);
    m_whitespaceCombo->setCurrentIndex(s.value("editing/show_whitespace", false).toBool() ? 1 : 0);
}

// ═══════════════════════════════════════════════════════════════════════════
//  BehaviorSettingsPage
//  MPI / rank fields removed — those are now exclusively in DebuggerSettingsPage.
//  This page owns session-level UX preferences only.
// ═══════════════════════════════════════════════════════════════════════════

BehaviorSettingsPage::BehaviorSettingsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *form = new QFormLayout(this);
    form->setContentsMargins(24, 24, 24, 24);
    form->setSpacing(14);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *heading = new QLabel(tr("<b>Behavior</b>"), this);
    heading->setStyleSheet("font-size: 15px; color: #8ab4f8; padding-bottom: 4px;");
    form->addRow(heading);

    auto *separator = new QLabel(this);
    separator->setFixedHeight(1);
    separator->setStyleSheet("background: rgba(255,255,255,0.08); margin-bottom: 8px;");
    form->addRow(separator);

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

    auto *note = new QLabel(
        tr("<small style='color:#888;'>MPI executor and rank settings are in the "
           "<b>Debugger</b> category.</small>"),
        this);
    note->setWordWrap(true);
    form->addRow(QString(), note);

    loadFromSettings();
}

QString BehaviorSettingsPage::restoreBreakpoints() const { return m_restoreBreakpointsCheck->currentText(); }
bool    BehaviorSettingsPage::confirmQuit()        const { return m_confirmQuitCheck->currentIndex() == 0; }
bool    BehaviorSettingsPage::focusOnStop()        const { return m_focusOnStopCheck->currentIndex() == 0; }

void BehaviorSettingsPage::loadFromSettings()
{
    QSettings s("GridLock", "Debugger");
    int rbIdx = m_restoreBreakpointsCheck->findText(
        s.value("behavior/restore_breakpoints", "Always").toString());
    if (rbIdx >= 0) m_restoreBreakpointsCheck->setCurrentIndex(rbIdx);
    m_confirmQuitCheck->setCurrentIndex(s.value("behavior/confirm_quit", true).toBool() ? 0 : 1);
    m_focusOnStopCheck->setCurrentIndex(s.value("behavior/focus_on_stop", true).toBool() ? 0 : 1);
}

// ═══════════════════════════════════════════════════════════════════════════
//  DebuggerSettingsPage
//  Sole owner of GDB path, MPI executable, MPI args, and default rank count.
//  Reads/writes through ConfigManager::getDebuggerSettings / saveDebuggerSettings.
// ═══════════════════════════════════════════════════════════════════════════

DebuggerSettingsPage::DebuggerSettingsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *form = new QFormLayout(this);
    form->setContentsMargins(24, 24, 24, 24);
    form->setSpacing(14);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *heading = new QLabel(tr("<b>Debugger</b>"), this);
    heading->setStyleSheet("font-size: 15px; color: #8ab4f8; padding-bottom: 4px;");
    form->addRow(heading);

    auto *separator = new QLabel(this);
    separator->setFixedHeight(1);
    separator->setStyleSheet("background: rgba(255,255,255,0.08); margin-bottom: 8px;");
    form->addRow(separator);

    m_gdbPathEdit = new QLineEdit(this);
    m_gdbPathEdit->setPlaceholderText("gdb");
    m_gdbPathEdit->setToolTip(tr("Full path to the GDB binary, e.g. /usr/bin/gdb or a custom build."));
    form->addRow(tr("GDB Path:"), m_gdbPathEdit);

    m_mpiExecEdit = new QLineEdit(this);
    m_mpiExecEdit->setPlaceholderText("mpiexec");
    m_mpiExecEdit->setToolTip(tr("MPI launcher used when attaching the debugger to MPI ranks."));
    form->addRow(tr("MPI Executable:"), m_mpiExecEdit);

    m_mpiArgsEdit = new QLineEdit(this);
    m_mpiArgsEdit->setPlaceholderText("--oversubscribe");
    m_mpiArgsEdit->setToolTip(tr("Extra flags forwarded to the MPI launcher on every debug session."));
    form->addRow(tr("MPI Args:"), m_mpiArgsEdit);

    m_rankBox = new QSpinBox(this);
    m_rankBox->setRange(1, 256);
    m_rankBox->setToolTip(tr("Default number of MPI ranks for debugger sessions (minimum 1)."));
    form->addRow(tr("Default Rank Count:"), m_rankBox);

    auto *note = new QLabel(
        tr("<small style='color:#888;'>GDB must support the MI2 protocol (<code>--interpreter=mi2</code>).<br>"
           "Changes take effect on the next debug session launch.</small>"),
        this);
    note->setWordWrap(true);
    form->addRow(QString(), note);

    loadFromSettings();
}

QString DebuggerSettingsPage::gdbPath()       const { return m_gdbPathEdit->text(); }
QString DebuggerSettingsPage::mpiExecutable() const { return m_mpiExecEdit->text(); }
QString DebuggerSettingsPage::mpiArgs()       const { return m_mpiArgsEdit->text(); }
int     DebuggerSettingsPage::defaultRanks()  const { return m_rankBox->value(); }

void DebuggerSettingsPage::loadFromSettings()
{
    const auto ds = gridlock::core::ConfigManager::instance().getDebuggerSettings();
    m_gdbPathEdit->setText(ds.gdbPath);
    m_mpiExecEdit->setText(ds.mpiExecutable);
    m_mpiArgsEdit->setText(ds.mpiArgs);
    m_rankBox->setValue(ds.defaultRanks);
}

// ═════════════════════════════════════════════════════════════════════════
//  HpcSettingsPage
// ═════════════════════════════════════════════════════════════════════════

HpcSettingsPage::HpcSettingsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *form = new QFormLayout(this);
    form->setContentsMargins(24, 24, 24, 24);
    form->setSpacing(14);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *heading = new QLabel(tr("<b>HPC / Cluster</b>"), this);
    heading->setStyleSheet("font-size: 15px; color: #8ab4f8; padding-bottom: 4px;");
    form->addRow(heading);

    auto *separator = new QLabel(this);
    separator->setFixedHeight(1);
    separator->setStyleSheet("background: rgba(255,255,255,0.08); margin-bottom: 8px;");
    form->addRow(separator);

    // ── Hosts file ──────────────────────────────────────────────────────────
    auto *hostsRow = new QHBoxLayout();
    m_hostsFileEdit = new QLineEdit(this);
    m_hostsFileEdit->setPlaceholderText(tr("(not set — use default hosts)"));
    m_hostsFileEdit->setToolTip(tr(
        "Path to an MPI hostfile. When set, --hostfile <path> is passed to the\n"
        "MPI launcher. Leave empty to let the launcher choose nodes automatically."));
    auto *browseBtn = new QPushButton(tr("Browse…"), this);
    browseBtn->setFixedWidth(72);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select MPI Hosts File"),
            m_hostsFileEdit->text().isEmpty()
                ? QDir::homePath() : m_hostsFileEdit->text(),
            tr("All Files (*)"),
            nullptr,
            QFileDialog::DontUseNativeDialog);
        if (!path.isEmpty())
            m_hostsFileEdit->setText(path);
    });
    hostsRow->addWidget(m_hostsFileEdit);
    hostsRow->addWidget(browseBtn);
    form->addRow(tr("MPI Hosts File:"), hostsRow);

    // ── Environment variables ────────────────────────────────────────────────
    m_envVarsEdit = new QTextEdit(this);
    m_envVarsEdit->setFixedHeight(90);
    m_envVarsEdit->setPlaceholderText(tr(
        "One key=value pair per line\n"
        "e.g.  OMPI_MCA_btl=tcp,self\n"
        "      UCX_NET_DEVICES=eth0"));
    m_envVarsEdit->setToolTip(tr(
        "MPI environment variables injected via -x KEY=VALUE flags.\n"
        "One entry per line: KEY=VALUE"));
    m_envVarsEdit->setAcceptRichText(false);
    form->addRow(tr("MPI Env Vars:"), m_envVarsEdit);

    // ── Strict affinity ─────────────────────────────────────────────────────
    m_strictAffinityBox = new QCheckBox(tr("Bind ranks to individual nodes (--map-by node)"), this);
    m_strictAffinityBox->setToolTip(tr(
        "Passes --map-by node to the MPI launcher so each rank is pinned to\n"
        "a separate physical node. Useful for NUMA-sensitive workloads."));
    form->addRow(tr("Strict Node Affinity:"), m_strictAffinityBox);

    auto *note = new QLabel(
        tr("<small style='color:#888;'>Changes are applied on the <b>next</b> debug session launch.</small>"),
        this);
    note->setWordWrap(true);
    form->addRow(QString(), note);

    loadFromSettings();
}

QString HpcSettingsPage::hostsFile()      const { return m_hostsFileEdit->text().trimmed(); }
QString HpcSettingsPage::envVars()        const { return m_envVarsEdit->toPlainText().trimmed(); }
bool    HpcSettingsPage::strictAffinity() const { return m_strictAffinityBox->isChecked(); }

void HpcSettingsPage::loadFromSettings()
{
    const auto hs = gridlock::core::ConfigManager::instance().getHpcSettings();
    m_hostsFileEdit->setText(hs.hostsFile);
    m_envVarsEdit->setPlainText(hs.envVars);
    m_strictAffinityBox->setChecked(hs.strictAffinity);
}

// ═══════════════════════════════════════════════════════════════════════════
//  PreferencesDialog
// ═══════════════════════════════════════════════════════════════════════════

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences — GridLock"));
    setMinimumSize(780, 540);
    resize(860, 580);

    // ── Dark-panel stylesheet ────────────────────────────────────────────
    setStyleSheet(R"(
        QDialog {
            background-color: #1e1e2e;
            color: #cdd6f4;
        }
        QListWidget {
            background-color: #181825;
            border: none;
            border-right: 1px solid rgba(255,255,255,0.07);
            font-size: 13px;
            color: #cdd6f4;
            outline: 0;
        }
        QListWidget::item {
            padding: 10px 18px;
            border-radius: 0px;
        }
        QListWidget::item:selected {
            background-color: #313244;
            color: #cba6f7;
            border-left: 3px solid #cba6f7;
        }
        QListWidget::item:hover:!selected {
            background-color: #252535;
        }
        QStackedWidget {
            background-color: #1e1e2e;
        }
        QWidget {
            background-color: #1e1e2e;
            color: #cdd6f4;
        }
        QLabel {
            color: #cdd6f4;
        }
        QLineEdit, QSpinBox, QComboBox {
            background-color: #313244;
            color: #cdd6f4;
            border: 1px solid rgba(255,255,255,0.1);
            border-radius: 5px;
            padding: 5px 8px;
            selection-background-color: #cba6f7;
            selection-color: #1e1e2e;
            min-height: 26px;
        }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
            border-color: #cba6f7;
        }
        QComboBox::drop-down {
            border: none;
            padding-right: 6px;
        }
        QComboBox QAbstractItemView {
            background-color: #313244;
            color: #cdd6f4;
            selection-background-color: #cba6f7;
            selection-color: #1e1e2e;
            border: 1px solid rgba(255,255,255,0.1);
        }
        QDialogButtonBox QPushButton {
            background-color: #45475a;
            color: #cdd6f4;
            border: 1px solid rgba(255,255,255,0.1);
            border-radius: 5px;
            padding: 6px 18px;
            min-width: 72px;
        }
        QDialogButtonBox QPushButton:hover {
            background-color: #585b70;
        }
        QDialogButtonBox QPushButton[text="OK"],
        QDialogButtonBox QPushButton[text="Apply"] {
            background-color: #cba6f7;
            color: #1e1e2e;
            font-weight: 600;
            border: none;
        }
        QDialogButtonBox QPushButton[text="OK"]:hover,
        QDialogButtonBox QPushButton[text="Apply"]:hover {
            background-color: #d4b9ff;
        }
        QFormLayout QLabel {
            color: #a6adc8;
        }
    )");

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Splitter: sidebar | page stack ──────────────────────────────────
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

    // ── Separator line above button box ──────────────────────────────────
    auto *line = new QLabel(this);
    line->setFixedHeight(1);
    line->setStyleSheet("background: rgba(255,255,255,0.08);");
    rootLayout->addWidget(line);

    setupButtonBox();
    rootLayout->addWidget(m_buttonBox);
}

void PreferencesDialog::setupSidebar()
{
    m_sidebar = new QListWidget(this);
    m_sidebar->setFixedWidth(190);
    m_sidebar->setIconSize(QSize(20, 20));
    m_sidebar->setSpacing(0);
    m_sidebar->setFrameShape(QFrame::NoFrame);

    struct Entry { QString label; QString iconName; };
    const QList<Entry> entries = {
        { tr("Appearance"), "preferences-desktop-theme" },
        { tr("Editing"),    "document-edit"             },
        { tr("Behavior"),   "configure"                 },
        { tr("Debugger"),   "debug-run"                 },
        { tr("HPC / Cluster"), "network-server"         },
    };

    for (const auto &e : entries) {
        auto *item = new QListWidgetItem(m_sidebar);
        item->setText(e.label);
        QIcon icon = QIcon::fromTheme(e.iconName);
        if (!icon.isNull()) item->setIcon(icon);
        item->setSizeHint(QSize(190, 44));
        m_sidebar->addItem(item);
    }

    m_sidebar->setCurrentRow(0);

    connect(m_sidebar, &QListWidget::currentRowChanged,
            this, [this](int row) {
        if (m_stack) m_stack->setCurrentIndex(row);
    });
}

void PreferencesDialog::setupPages()
{
    m_stack = new QStackedWidget(this);

    m_appearancePage = new AppearanceSettingsPage(m_stack);
    m_editingPage    = new EditingSettingsPage(m_stack);
    m_behaviorPage   = new BehaviorSettingsPage(m_stack);
    m_debuggerPage   = new DebuggerSettingsPage(m_stack);
    m_hpcPage        = new HpcSettingsPage(m_stack);

    m_stack->addWidget(m_appearancePage);
    m_stack->addWidget(m_editingPage);
    m_stack->addWidget(m_behaviorPage);
    m_stack->addWidget(m_debuggerPage);
    m_stack->addWidget(m_hpcPage);

    m_stack->setCurrentIndex(0);
}

void PreferencesDialog::setupButtonBox()
{
    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel,
        Qt::Horizontal, this);
    m_buttonBox->setContentsMargins(12, 10, 12, 10);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &PreferencesDialog::acceptAndApply);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttonBox->button(QDialogButtonBox::Apply),
            &QPushButton::clicked, this, &PreferencesDialog::apply);
}

// ── Private helpers ──────────────────────────────────────────────────────

void PreferencesDialog::apply()
{
    QSettings s("GridLock", "Debugger");

    // ── Appearance ──────────────────────────────────────────────────────
    s.setValue("appearance/theme", m_appearancePage->selectedTheme());

    // ── Editing ─────────────────────────────────────────────────────────
    s.setValue("editing/tab_width",       m_editingPage->tabWidth());
    s.setValue("editing/insert_spaces",   m_editingPage->insertSpaces());
    s.setValue("editing/show_whitespace", m_editingPage->showWhitespace());

    // ── Behavior (session UX prefs only — no MPI/rank here) ─────────────
    s.setValue("behavior/restore_breakpoints", m_behaviorPage->restoreBreakpoints());
    s.setValue("behavior/confirm_quit",        m_behaviorPage->confirmQuit());
    s.setValue("behavior/focus_on_stop",       m_behaviorPage->focusOnStop());

    // ── Debugger — single source of truth via ConfigManager ────────────────
    gridlock::core::DebuggerSettings ds;
    ds.gdbPath       = m_debuggerPage->gdbPath();
    ds.mpiExecutable = m_debuggerPage->mpiExecutable();
    ds.mpiArgs       = m_debuggerPage->mpiArgs();
    ds.defaultRanks  = m_debuggerPage->defaultRanks();
    gridlock::core::ConfigManager::instance().saveDebuggerSettings(ds);

    // ── HPC / Cluster — via ConfigManager ───────────────────────────────
    gridlock::core::HpcSettings hs;
    hs.hostsFile      = m_hpcPage->hostsFile();
    hs.envVars        = m_hpcPage->envVars();
    hs.strictAffinity = m_hpcPage->strictAffinity();
    gridlock::core::ConfigManager::instance().saveHpcSettings(hs);

    s.sync();

    emit preferencesChanged();
}

void PreferencesDialog::acceptAndApply()
{
    apply();
    accept();
}

} // namespace gridlock::ui
