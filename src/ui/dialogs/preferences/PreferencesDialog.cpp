/**
 * @file PreferencesDialog.cpp
 * @brief Implementation of the central PreferencesDialog shell, sidebar, page stack, and persistence flush.
 */

#include "PreferencesDialog.hpp"
#include "../../../core/managers/ConfigManager.hpp"
#include "../../../core/managers/DocsetManager.hpp"
#include "../../../core/managers/ThemeManager.hpp"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QVBoxLayout>

namespace gridlock::ui {

/**
 * @brief Constructs the top-level preferences dialog.
 * @param parent Optional parent widget.
 */
PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("PreferencesDialog"));
    setWindowTitle(tr("Preferences — GridLock"));
    setMinimumSize(880, 620);
    resize(960, 680);

    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);

    setupSidebar();
    setupPages();

    splitter->addWidget(m_sidebar);
    splitter->addWidget(m_stack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({195, 765});

    rootLayout->addWidget(splitter, 1);

    auto* line = new QLabel(this);
    line->setFixedHeight(1);
    line->setObjectName(QStringLiteral("separator"));
    rootLayout->addWidget(line);

    setupButtonBox();
    rootLayout->addWidget(m_buttonBox);

    // Harmonize 26px control heights and attach descriptive per-item tooltips to all dropdowns.
    for (QPushButton* btn : findChildren<QPushButton*>()) {
        if (m_buttonBox && m_buttonBox->buttons().contains(btn)) continue;
        btn->setFixedHeight(26);
        if (btn->text().contains(QStringLiteral("Browse"), Qt::CaseInsensitive)) btn->setMinimumWidth(96);
    }
    for (QLineEdit* edit : findChildren<QLineEdit*>()) {
        edit->setFixedHeight(26);
    }
    for (QComboBox* combo : findChildren<QComboBox*>()) {
        combo->setFixedHeight(26);
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemData(i, Qt::ToolTipRole).toString().isEmpty()) {
                const QString val = combo->itemData(i).toString();
                const QString desc = val.isEmpty() ? combo->itemText(i)
                                                   : QStringLiteral("%1 (%2)").arg(combo->itemText(i), val);
                combo->setItemData(i, desc, Qt::ToolTipRole);
            }
        }
        if (combo->toolTip().isEmpty() && combo->currentIndex() >= 0) {
            combo->setToolTip(combo->itemData(combo->currentIndex(), Qt::ToolTipRole).toString());
        }
        connect(combo, &QComboBox::currentIndexChanged, this, [combo](int idx) {
            if (idx >= 0) combo->setToolTip(combo->itemData(idx, Qt::ToolTipRole).toString());
        });
    }

    applyDialogThemeChrome();
    connect(&gridlock::core::managers::ThemeManager::instance(),
            &gridlock::core::managers::ThemeManager::themeModeChanged,
            this, [this](bool) { applyDialogThemeChrome(); });
}

/**
 * @brief Builds the vertical category navigation list with icons and opaque surface attributes.
 */
void PreferencesDialog::setupSidebar() {
    m_sidebar = new QListWidget(this);
    m_sidebar->setObjectName(QStringLiteral("preferencesCategoryList"));
    m_sidebar->setMinimumWidth(190);
    m_sidebar->setFixedWidth(195);
    m_sidebar->setIconSize(QSize(16, 16));
    m_sidebar->setSpacing(0);
    m_sidebar->setFrameShape(QFrame::NoFrame);
    m_sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebar->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_sidebar->setProperty("role", "sidebar");
    m_sidebar->setAttribute(Qt::WA_StyledBackground, true);
    m_sidebar->setAutoFillBackground(true);
    if (m_sidebar->viewport()) m_sidebar->viewport()->setAutoFillBackground(true);
    QFont sideFont = m_sidebar->font();
    sideFont.setPixelSize(13);
    m_sidebar->setFont(sideFont);

    struct Entry { QString label; QString iconName; };
    const QList<Entry> entries = {
        {tr("Appearance"), QStringLiteral("\uf1fc")},      {tr("Editing"), QStringLiteral("\uf044")},
        {tr("Behavior"), QStringLiteral("\uf085")},        {tr("Keybindings"), QStringLiteral("\uf11c")},
        {tr("Debugger"), QStringLiteral("\uf188")},        {tr("HPC / Cluster"), QStringLiteral("\uf233")},
        {tr("HPC Integration"), QStringLiteral("\uf1c0")}, {tr("Terminal"), QStringLiteral("\uf120")},
        {tr("Docsets"), QStringLiteral("\uf02d")},
    };
    for (const auto& e : entries) {
        auto* item = new QListWidgetItem(m_sidebar);
        item->setText(e.label);
        item->setData(Qt::UserRole, e.iconName);
        item->setSizeHint(QSize(190, 36));
        m_sidebar->addItem(item);
    }
    m_sidebar->setCurrentRow(0);
    refreshSidebarIcons();
    connect(m_sidebar, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_stack) m_stack->setCurrentIndex(row);
        refreshSidebarIcons();
    });
}

/**
 * @brief Instantiates and registers all individual settings pages in the opaque stack.
 */
void PreferencesDialog::setupPages() {
    m_stack = new QStackedWidget(this);
    m_stack->setAttribute(Qt::WA_StyledBackground, true);
    m_stack->setAutoFillBackground(true);

    m_appearancePage = new AppearanceSettingsPage(m_stack);
    m_editingPage = new EditingSettingsPage(m_stack);
    m_behaviorPage = new BehaviorSettingsPage(m_stack);
    m_keybindingsPage = new KeybindingsSettingsPage(m_stack);
    m_debuggerPage = new DebuggerSettingsPage(m_stack);
    m_hpcPage = new HpcSettingsPage(m_stack);
    m_hpcIntegrationPage = new HpcIntegrationSettingsPage(m_stack);
    m_terminalPage = new TerminalSettingsPage(m_stack);
    m_docsetPage = new DocsetSettingsPage(m_stack);

    for (QWidget* page : {static_cast<QWidget*>(m_appearancePage),
                          static_cast<QWidget*>(m_editingPage),
                          static_cast<QWidget*>(m_behaviorPage),
                          static_cast<QWidget*>(m_keybindingsPage),
                          static_cast<QWidget*>(m_debuggerPage),
                          static_cast<QWidget*>(m_hpcPage),
                          static_cast<QWidget*>(m_hpcIntegrationPage),
                          static_cast<QWidget*>(m_terminalPage),
                          static_cast<QWidget*>(m_docsetPage)}) {
        page->setAttribute(Qt::WA_StyledBackground, true);
        page->setAutoFillBackground(true);
        auto* scroll = new QScrollArea(m_stack);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setAttribute(Qt::WA_StyledBackground, true);
        scroll->setAutoFillBackground(true);
        scroll->setWidget(page);
        m_stack->addWidget(scroll);
    }
    m_stack->setCurrentIndex(0);
}

/**
 * @brief Constructs modern icon-free Ok, Apply, and Cancel dialog action buttons.
 */
void PreferencesDialog::setupButtonBox() {
    class IconStripFilter final : public QObject {
    public:
        explicit IconStripFilter(QDialogButtonBox* box) : QObject(box), m_box(box) {}
        bool eventFilter(QObject* w, QEvent* ev) override {
            if (m_box && (ev->type() == QEvent::StyleChange || ev->type() == QEvent::Polish || ev->type() == QEvent::Show)) {
                for (QAbstractButton* b : m_box->buttons())
                    if (b && !b->icon().isNull()) { b->setIcon(QIcon()); b->setIconSize(QSize(0, 0)); }
            }
            return QObject::eventFilter(w, ev);
        }
    private:
        QDialogButtonBox* m_box = nullptr;
    };

    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel,
        Qt::Horizontal, this);
    m_buttonBox->setContentsMargins(12, 8, 12, 8);
    for (QAbstractButton* btn : m_buttonBox->buttons()) {
        if (btn) { btn->setIcon(QIcon()); btn->setIconSize(QSize(0, 0)); }
    }
    m_buttonBox->installEventFilter(new IconStripFilter(m_buttonBox));

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &PreferencesDialog::acceptAndApply);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &PreferencesDialog::apply);
}

/**
 * @brief Serializes all modified page settings to persistent storage and emits preferencesChanged().
 */
void PreferencesDialog::apply() {
    QSettings s("gridlock", "debugger");

    // 1. Appearance settings
    s.setValue("appearance/theme", m_appearancePage->selectedTheme());
    s.setValue("appearance/dark_mode", m_appearancePage->isDarkMode());
    s.setValue("appearance/file_tree_style", m_appearancePage->fileTreeStyle());
    s.setValue("appearance/colorize_icons", m_appearancePage->colorizeIcons());
    s.setValue("appearance/ui_font_family", m_appearancePage->uiFontFamily());
    s.setValue("appearance/ui_font_size", m_appearancePage->uiFontSize());
    s.setValue("appearance/code_font_family", m_appearancePage->codeFontFamily());
    s.setValue("appearance/code_font_size", m_appearancePage->codeFontSize());
    s.sync();
    gridlock::core::ConfigManager::instance().setMemoryMapColorScheme(m_appearancePage->memoryMapColorScheme());
    gridlock::core::managers::ThemeManager::instance().applyThemeAndFonts();

    // 2. Editor options
    s.setValue("editing/tab_width", m_editingPage->tabWidth());
    s.setValue("editing/insert_spaces", m_editingPage->insertSpaces());
    s.setValue("editing/show_whitespace", m_editingPage->showWhitespace());
    s.setValue("editing/word_wrap", m_editingPage->wordWrap());
    s.setValue("editing/show_edge_column", m_editingPage->showEdgeColumn());
    s.setValue("editing/edge_column", m_editingPage->edgeColumn());
    s.setValue("editing/rainbow_brackets", m_editingPage->rainbowBrackets());
    s.setValue("editing/data_doc_highlighting", m_editingPage->dataDocHighlighting());
    s.setValue("editor/inline_variable_values", m_editingPage->inlineVariableValues());

    // 3. User interaction behavior
    s.setValue("behavior/restore_breakpoints", m_behaviorPage->restoreBreakpoints());
    s.setValue("behavior/confirm_quit", m_behaviorPage->confirmQuit());
    s.setValue("behavior/focus_on_stop", m_behaviorPage->focusOnStop());

    // 4. Debugger engine & launcher configurations
    auto& cfg = gridlock::core::ConfigManager::instance();
    cfg.setRainbowBracketsEnabled(m_editingPage->rainbowBrackets());
    cfg.setDataDocHighlightingEnabled(m_editingPage->dataDocHighlighting());
    cfg.setInlineVariableValuesEnabled(m_editingPage->inlineVariableValues());
    if (auto* telemetryCombo = m_behaviorPage->findChild<QComboBox*>(QStringLiteral("telemetryFormatCombo"))) {
        cfg.setTelemetryDisplayFormat(telemetryCombo->currentData().toString());
    }
    if (auto* engineCombo = m_debuggerPage->findChild<QComboBox*>(QStringLiteral("debuggerEngineCombo"))) {
        cfg.setDebuggerEngine(engineCombo->currentData().toString());
    }
    if (auto* defModeCombo = m_debuggerPage->findChild<QComboBox*>(QStringLiteral("defaultDisassemblyModeCombo"))) {
        cfg.setDefaultDisassemblyMode(defModeCombo->currentData().toString());
    }
    if (auto* showDecompCheck = m_debuggerPage->findChild<QCheckBox*>(QStringLiteral("showDecompilerWithSourceCheck"))) {
        cfg.setShowDecompilerWithSource(showDecompCheck->isChecked());
    }
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

    // 6. Docsets, terminal, and keybinding settings
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
    if (m_keybindingsPage) m_keybindingsPage->applyPendingChanges();

    s.sync();
    if (qApp) qApp->setProperty("gridlock_disasm_prefs_rev", qApp->property("gridlock_disasm_prefs_rev").toInt() + 1);
    emit preferencesChanged();
}

void PreferencesDialog::acceptAndApply() { apply(); accept(); }

bool PreferencesDialog::parseSlurmBatchScript(const QString& scriptText) {
    return m_hpcIntegrationPage ? m_hpcIntegrationPage->parseSlurmBatchScript(scriptText) : false;
}
bool PreferencesDialog::loadSlurmBatchScriptFile(const QString& filePath) {
    return m_hpcIntegrationPage ? m_hpcIntegrationPage->loadSlurmBatchScriptFile(filePath) : false;
}
QString PreferencesDialog::generateSlurmBatchScript() const {
    return m_hpcIntegrationPage ? m_hpcIntegrationPage->generateSlurmBatchScript() : QString();
}
QString PreferencesDialog::slurmFieldValue(const QString& key) const {
    return m_hpcIntegrationPage ? m_hpcIntegrationPage->slurmFieldValue(key) : QString();
}

}  // namespace gridlock::ui
