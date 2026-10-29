/**
 * @file PreferencesDialog.cpp
 * @brief Implementation of the central Preferences dialog for GridLock.
 *
 * Implements a split sidebar navigation dialog. The left navigation pane
 * hosts a categorized QListWidget with Nerd Font icons, and the right viewport
 * displays a QStackedWidget containing the respective preference pages.
 * Changes are written to persistent QSettings and ConfigManager state.
 */

#include "PreferencesDialog.hpp"
#include "../../core/managers/ConfigManager.hpp"
#include "../../core/managers/DocsetManager.hpp"
#include "../../core/managers/ThemeManager.hpp"

#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWindow>

namespace gridlock::ui {

/**
 * @brief Constructs the top-level preferences dialog.
 * @param parent Optional parent widget.
 *
 * Assembles the modal window chrome, configures the horizontal splitter between
 * sidebar and page stack, and wires the standard Ok/Apply/Cancel dialog buttons.
 */
PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Preferences — GridLock"));
    setMinimumSize(780, 540);
    resize(860, 580);

    // Apply unified dark-slate workstation dialog chrome and compact control heights.
    setStyleSheet(QStringLiteral(
        "QDialog { background: #16181d; color: #d7dae0; }"
        "QGroupBox { background: #1b1e24; border: 1px solid #282c34; border-radius: 3px; "
        "margin-top: 8px; padding-top: 10px; color: #d7dae0; }"
        "QLineEdit, QComboBox, QSpinBox { background: #181a1f; color: #d7dae0; "
        "border: 1px solid #323844; border-radius: 2px; padding: 1px 6px; min-height: 20px; max-height: 24px; }"
        "QPushButton { background: #282c34; color: #d7dae0; border: 1px solid #3b4048; "
        "border-radius: 2px; padding: 2px 10px; min-height: 22px; max-height: 24px; }"
        "QPushButton:hover { background: #323842; color: #ffffff; }"
        "QLabel#separator { background: #2c313a; }"));

    // Root vertical layout containing the splitter and bottom action button bar.
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // Horizontal splitter separating the sidebar list from the active page stack.
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);

    setupSidebar();
    setupPages();

    splitter->addWidget(m_sidebar);
    splitter->addWidget(m_stack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({170, 690});

    rootLayout->addWidget(splitter, 1);

    // Subtle 1px divider above the button box.
    auto* line = new QLabel(this);
    line->setFixedHeight(1);
    line->setObjectName("separator");
    rootLayout->addWidget(line);

    setupButtonBox();
    rootLayout->addWidget(m_buttonBox);

    // Harmonize control heights across all stacked settings pages and footer buttons.
    for (QPushButton* btn : findChildren<QPushButton*>()) {
        btn->setFixedHeight(24);
    }
    for (QLineEdit* edit : findChildren<QLineEdit*>()) {
        edit->setFixedHeight(24);
    }
    for (QComboBox* combo : findChildren<QComboBox*>()) {
        combo->setFixedHeight(24);
    }
}

/**
 * @brief Builds the vertical category navigation list with icons.
 *
 * Populates a 170px QListWidget with category labels and Nerd Font glyphs rendered
 * dynamically onto transparent QPixmaps. Suppresses horizontal scrollbars and
 * applies a sleek 28px-row navigation style with a 2px #528bff active indicator bar.
 */
void PreferencesDialog::setupSidebar() {
    m_sidebar = new QListWidget(this);
    m_sidebar->setFixedWidth(170);
    m_sidebar->setIconSize(QSize(16, 16));
    m_sidebar->setSpacing(0);
    m_sidebar->setFrameShape(QFrame::NoFrame);
    m_sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebar->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_sidebar->setProperty("role", "sidebar");
    m_sidebar->setStyleSheet(QStringLiteral(
        "QListWidget { background: #1e2128; color: #abb2bf; border: none; "
        "border-right: 1px solid #2c313a; outline: none; font-size: 11px; }"
        "QListWidget::item { height: 28px; padding: 2px 8px; border-left: 2px solid transparent; }"
        "QListWidget::item:hover { background: #252932; color: #ffffff; }"
        "QListWidget::item:selected { background: #262b36; color: #ffffff; "
        "border-left: 2px solid #528bff; font-weight: 600; }"));

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

    // Render each category icon using Symbols Nerd Font into a 16x16 QPixmap.
    for (const auto& e : entries) {
        auto* item = new QListWidgetItem(m_sidebar);
        item->setText(e.label);

        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(QFont("Symbols Nerd Font", 11));
        painter.setPen(QColor("#cdd6f4"));
        painter.drawText(0, 0, 16, 16, Qt::AlignCenter, e.iconName);
        item->setIcon(QIcon(pixmap));

        item->setSizeHint(QSize(168, 28));
        m_sidebar->addItem(item);
    }

    // Default to the first page (Appearance) and connect selection updates.
    m_sidebar->setCurrentRow(0);
    connect(m_sidebar, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_stack) {
            m_stack->setCurrentIndex(row);
        }
    });
}

/**
 * @brief Instantiates and registers all individual settings pages in the stack.
 */
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

    // Register pages in the exact sequential order matching the sidebar entries.
    for (QWidget* page : {static_cast<QWidget*>(m_appearancePage),
                          static_cast<QWidget*>(m_editingPage),
                          static_cast<QWidget*>(m_behaviorPage),
                          static_cast<QWidget*>(m_debuggerPage),
                          static_cast<QWidget*>(m_hpcPage),
                          static_cast<QWidget*>(m_hpcIntegrationPage),
                          static_cast<QWidget*>(m_terminalPage),
                          static_cast<QWidget*>(m_docsetPage)}) {
        m_stack->addWidget(page);
    }
    m_stack->setCurrentIndex(0);
}

/**
 * @brief Constructs standard Ok, Apply, and Cancel dialog buttons with 24px height.
 */
void PreferencesDialog::setupButtonBox() {
    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel,
        Qt::Horizontal, this);
    m_buttonBox->setContentsMargins(12, 8, 12, 8);
    for (QAbstractButton* btn : m_buttonBox->buttons()) {
        btn->setFixedHeight(24);
    }

    connect(m_buttonBox, &QDialogButtonBox::accepted, this,
            &PreferencesDialog::acceptAndApply);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &PreferencesDialog::apply);
}

/**
 * @brief Serializes all modified page settings to persistent storage.
 *
 * Flushes GUI states to QSettings ("gridlock", "debugger") and to ConfigManager
 * for backend engine consumption, then emits preferencesChanged().
 */
void PreferencesDialog::apply() {
    QSettings s("gridlock", "debugger");

    // 1. Appearance settings
    s.setValue("appearance/theme", m_appearancePage->selectedTheme());
    s.setValue("appearance/dark_mode", m_appearancePage->isDarkMode());
    s.setValue("appearance/file_tree_style", m_appearancePage->fileTreeStyle());
    s.setValue("appearance/colorize_icons", m_appearancePage->colorizeIcons());
    s.setValue("appearance/ui_font_size", m_appearancePage->uiFontSize());
    s.setValue("appearance/code_font_size", m_appearancePage->codeFontSize());

    // 2. Editor options
    s.setValue("editing/tab_width", m_editingPage->tabWidth());
    s.setValue("editing/insert_spaces", m_editingPage->insertSpaces());
    s.setValue("editing/show_whitespace", m_editingPage->showWhitespace());
    s.setValue("editing/word_wrap", m_editingPage->wordWrap());
    s.setValue("editing/show_edge_column", m_editingPage->showEdgeColumn());
    s.setValue("editing/edge_column", m_editingPage->edgeColumn());

    // 3. User interaction behavior
    s.setValue("behavior/restore_breakpoints",
               m_behaviorPage->restoreBreakpoints());
    s.setValue("behavior/confirm_quit", m_behaviorPage->confirmQuit());
    s.setValue("behavior/focus_on_stop", m_behaviorPage->focusOnStop());

    // 4. Debugger engine & launcher configurations
    auto& cfg = gridlock::core::ConfigManager::instance();
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

    // 5. Cluster & remote execution configurations
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

    // 6. Docsets and terminal settings
    auto& docMgr = gridlock::core::DocsetManager::instance();
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

/**
 * @brief Applies current settings and closes the dialog with Accepted status.
 */
void PreferencesDialog::acceptAndApply() {
    apply();
    accept();
}

/**
 * @brief Handles custom mouse dragging for client-side decorated dialog window.
 * @param event The mouse press event.
 */
void PreferencesDialog::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
    } else {
        QDialog::mousePressEvent(event);
    }
}

/**
 * @brief Handles mouse movement events during window repositioning.
 * @param event The mouse move event.
 */
void PreferencesDialog::mouseMoveEvent(QMouseEvent* event) {
    QDialog::mouseMoveEvent(event);
}

}  // namespace gridlock::ui