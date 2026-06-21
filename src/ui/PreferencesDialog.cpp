// PreferencesDialog.cpp
// Implements a Kate/KDevelop-style side-bar preferences dialog.

#include "PreferencesDialog.hpp"
#include "../core/ConfigManager.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
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

    m_mpiExecEdit = new QLineEdit(this);
    m_mpiExecEdit->setPlaceholderText("mpiexec");
    m_mpiExecEdit->setToolTip(tr("Path or name of the MPI launcher executable (e.g. mpiexec, mpirun)."));
    form->addRow(tr("MPI Executable:"), m_mpiExecEdit);

    m_extraArgEdit = new QLineEdit(this);
    m_extraArgEdit->setPlaceholderText("--oversubscribe");
    m_extraArgEdit->setToolTip(tr("Extra flags forwarded to the MPI launcher on every run."));
    form->addRow(tr("Extra MPI Args:"), m_extraArgEdit);

    m_rankBox = new QSpinBox(this);
    m_rankBox->setRange(1, 256);
    m_rankBox->setToolTip(tr("Default number of MPI ranks to spawn when launching a new session."));
    form->addRow(tr("Default Rank Count:"), m_rankBox);

    loadFromSettings();
}

QString BehaviorSettingsPage::mpiExecutable() const { return m_mpiExecEdit->text(); }
QString BehaviorSettingsPage::extraArgs()     const { return m_extraArgEdit->text(); }
int     BehaviorSettingsPage::defaultRanks()  const { return m_rankBox->value(); }

void BehaviorSettingsPage::loadFromSettings()
{
    QSettings s("GridLock", "Debugger");
    m_mpiExecEdit->setText(s.value("mpi_executable", "mpiexec").toString());
    m_extraArgEdit->setText(s.value("extra_args", "--oversubscribe").toString());
    m_rankBox->setValue(
        s.value("rank_count",
                gridlock::core::ConfigManager::instance().getDefaultRanks()).toInt());
}

// ═══════════════════════════════════════════════════════════════════════════
//  DebuggerSettingsPage
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

    m_rankBox = new QSpinBox(this);
    m_rankBox->setRange(1, 256);
    m_rankBox->setToolTip(tr("Default number of MPI ranks for debugger sessions."));
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
int     DebuggerSettingsPage::defaultRanks()  const { return m_rankBox->value(); }

void DebuggerSettingsPage::loadFromSettings()
{
    QSettings s("GridLock", "Debugger");
    m_gdbPathEdit->setText(s.value("debugger/gdb_path", "gdb").toString());
    m_mpiExecEdit->setText(s.value("mpi_executable", "mpiexec").toString());
    m_rankBox->setValue(
        s.value("rank_count",
                gridlock::core::ConfigManager::instance().getDefaultRanks()).toInt());
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
    splitter->setSizes({180, 680});

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

    // Category entries
    struct Entry { QString label; QString iconName; };
    const QList<Entry> entries = {
        { tr("Appearance"), "preferences-desktop-theme"   },
        { tr("Editing"),    "document-edit"               },
        { tr("Behavior"),   "configure"                   },
        { tr("Debugger"),   "debug-run"                   },
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

    m_stack->addWidget(m_appearancePage);
    m_stack->addWidget(m_editingPage);
    m_stack->addWidget(m_behaviorPage);
    m_stack->addWidget(m_debuggerPage);

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

    // Appearance
    s.setValue("appearance/theme", m_appearancePage->selectedTheme());

    // Editing
    s.setValue("editing/tab_width",       m_editingPage->tabWidth());
    s.setValue("editing/insert_spaces",   m_editingPage->insertSpaces());
    s.setValue("editing/show_whitespace", m_editingPage->showWhitespace());

    // Behavior (shared MPI settings used by New-Session dialog)
    s.setValue("mpi_executable", m_behaviorPage->mpiExecutable());
    s.setValue("extra_args",     m_behaviorPage->extraArgs());
    s.setValue("rank_count",     m_behaviorPage->defaultRanks());

    // Debugger
    s.setValue("debugger/gdb_path",   m_debuggerPage->gdbPath());
    // Debugger page MPI fields shadow the Behavior-page ones if the user
    // changed them; the Behavior page value is the canonical shared key.
    if (!m_debuggerPage->mpiExecutable().isEmpty())
        s.setValue("mpi_executable", m_debuggerPage->mpiExecutable());
    if (m_debuggerPage->defaultRanks() > 0)
        s.setValue("rank_count", m_debuggerPage->defaultRanks());

    s.sync();

    emit preferencesChanged();
}

void PreferencesDialog::acceptAndApply()
{
    apply();
    accept();
}

} // namespace gridlock::ui
