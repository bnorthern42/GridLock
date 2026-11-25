/**
 * @file PreferencesDialog.cpp
 * @brief Implementation of the central PreferencesDialog shell, sidebar, page stack, and persistence flush.
 */

#include "PreferencesDialog.hpp"
#include "../../MainWindow.hpp"
#include "../../../core/managers/ConfigManager.hpp"
#include "../../../core/managers/DocsetManager.hpp"
#include "../../../core/managers/ThemeManager.hpp"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace gridlock::ui {

PreferencesDialog::PreferencesDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("PreferencesDialog"));
    setWindowTitle(tr("Preferences — GridLock"));
    setMinimumSize(880, 620); resize(960, 680);
    setAttribute(Qt::WA_StyledBackground, true); setAutoFillBackground(true);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0); rootLayout->setSpacing(0);
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1); splitter->setChildrenCollapsible(false);

    setupSidebar();
    setupPages();
    m_sidebar = new QListWidget(this);
    m_sidebar->setObjectName(QStringLiteral("preferencesCategoryList"));
    m_sidebar->setMinimumWidth(195); m_sidebar->setIconSize(QSize(16, 16));
    m_sidebar->resize(200, 480); m_sidebar->hide();
    std::function<void(QTreeWidgetItem*)> populateMirror = [&](QTreeWidgetItem* node) {
        if (!node) return;
        auto* li = new QListWidgetItem(node->text(0), m_sidebar);
        li->setData(Qt::UserRole, node->data(0, Qt::UserRole + 1));
        li->setData(Qt::UserRole + 1, node->data(0, Qt::UserRole));
        li->setSizeHint(QSize(185, 36));
        for (int c = 0; c < node->childCount(); ++c) populateMirror(node->child(c));
    };
    for (int i = 0; i < m_categoryTree->topLevelItemCount(); ++i) populateMirror(m_categoryTree->topLevelItem(i));
    m_sidebar->setCurrentRow(0);
    connect(m_sidebar, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_sidebar || row < 0 || row >= m_sidebar->count() || !m_categoryTree) return;
        const int targetPage = m_sidebar->item(row)->data(Qt::UserRole + 1).toInt();
        std::function<QTreeWidgetItem*(QTreeWidgetItem*)> findByPage = [&](QTreeWidgetItem* n) -> QTreeWidgetItem* {
            if (!n) return nullptr;
            if (n->data(0, Qt::UserRole).toInt() == targetPage) return n;
            for (int c = 0; c < n->childCount(); ++c) if (auto* hit = findByPage(n->child(c))) return hit;
            return nullptr;
        };
        for (int i = 0; i < m_categoryTree->topLevelItemCount(); ++i)
            if (auto* hit = findByPage(m_categoryTree->topLevelItem(i))) { m_categoryTree->setCurrentItem(hit); break; }
    });

    splitter->addWidget(m_categoryTree); splitter->addWidget(m_stack);
    splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1); splitter->setSizes({200, 760});
    rootLayout->addWidget(splitter, 1);

    auto* line = new QLabel(this); line->setFixedHeight(1); line->setObjectName(QStringLiteral("separator"));
    rootLayout->addWidget(line);
    setupButtonBox();
    rootLayout->addWidget(m_buttonBox);

    for (QPushButton* btn : findChildren<QPushButton*>()) {
        if (m_buttonBox && m_buttonBox->buttons().contains(btn)) continue;
        btn->setFixedHeight(26);
        if (btn->text().contains(QStringLiteral("Browse"), Qt::CaseInsensitive)) btn->setMinimumWidth(96);
    }
    for (QLineEdit* edit : findChildren<QLineEdit*>()) edit->setFixedHeight(26);
    for (QComboBox* combo : findChildren<QComboBox*>()) {
        combo->setFixedHeight(26);
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemData(i, Qt::ToolTipRole).toString().isEmpty()) {
                const QString val = combo->itemData(i).toString();
                combo->setItemData(i, val.isEmpty() ? combo->itemText(i) : QStringLiteral("%1 (%2)").arg(combo->itemText(i), val), Qt::ToolTipRole);
            }
        }
        if (combo->toolTip().isEmpty() && combo->currentIndex() >= 0)
            combo->setToolTip(combo->itemData(combo->currentIndex(), Qt::ToolTipRole).toString());
        connect(combo, &QComboBox::currentIndexChanged, this, [combo](int idx) {
            if (idx >= 0) combo->setToolTip(combo->itemData(idx, Qt::ToolTipRole).toString());
        });
    }
    applyDialogThemeChrome(); syncCategoryTreeChrome();
    connect(&gridlock::core::managers::ThemeManager::instance(), &gridlock::core::managers::ThemeManager::themeModeChanged,
            this, [this](bool) { applyDialogThemeChrome(); syncCategoryTreeChrome(); });
}

void PreferencesDialog::setupSidebar() {
    m_categoryTree = new QTreeWidget(this);
    m_categoryTree->setObjectName(QStringLiteral("preferencesCategoryTree"));
    m_categoryTree->setHeaderHidden(true); m_categoryTree->setRootIsDecorated(true);
    m_categoryTree->setIndentation(14); m_categoryTree->setMinimumWidth(195); m_categoryTree->setFixedWidth(200);
    m_categoryTree->setIconSize(QSize(16, 16)); m_categoryTree->setFrameShape(QFrame::NoFrame);
    m_categoryTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categoryTree->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_categoryTree->setProperty("role", "sidebar");
    m_categoryTree->setAttribute(Qt::WA_StyledBackground, true); m_categoryTree->setAutoFillBackground(true);
    if (m_categoryTree->viewport()) m_categoryTree->viewport()->setAutoFillBackground(true);
    QFont sideFont = m_categoryTree->font(); sideFont.setPixelSize(13); m_categoryTree->setFont(sideFont);

    auto makeNode = [](const QString& label, int pageIndex, const QString& iconGlyph) {
        auto* item = new QTreeWidgetItem(); item->setText(0, label);
        item->setData(0, Qt::UserRole, pageIndex); item->setData(0, Qt::UserRole + 1, iconGlyph);
        item->setSizeHint(0, QSize(185, 34)); return item;
    };
    auto* appearanceItem = makeNode(tr("Appearance"), 0, QStringLiteral("\uf1fc"));
    auto* toolbarChild = makeNode(tr("Toolbar"), 9, QStringLiteral("\uf0ad"));
    toolbarChild->setSizeHint(0, QSize(170, 30)); appearanceItem->addChild(toolbarChild);
    m_categoryTree->addTopLevelItem(appearanceItem);

    struct TopEntry { QString label; int pageIdx; QString iconGlyph; };
    for (const auto& e : {TopEntry{tr("Editing"), 1, QStringLiteral("\uf044")}, TopEntry{tr("Behavior"), 2, QStringLiteral("\uf085")},
                          TopEntry{tr("Keybindings"), 3, QStringLiteral("\uf11c")}, TopEntry{tr("Debugger"), 4, QStringLiteral("\uf188")},
                          TopEntry{tr("HPC / Cluster"), 5, QStringLiteral("\uf233")}, TopEntry{tr("HPC Integration"), 6, QStringLiteral("\uf1c0")},
                          TopEntry{tr("Terminal"), 7, QStringLiteral("\uf120")}, TopEntry{tr("Docsets"), 8, QStringLiteral("\uf02d")},
                          TopEntry{tr("Plugins"), 10, QStringLiteral("\uf1e6")}}) {
        m_categoryTree->addTopLevelItem(makeNode(e.label, e.pageIdx, e.iconGlyph));
    }
    m_categoryTree->expandAll(); m_categoryTree->setCurrentItem(appearanceItem); syncCategoryTreeChrome();
    connect(m_categoryTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (current && m_stack) {
            const int pageIdx = current->data(0, Qt::UserRole).toInt();
            if (pageIdx >= 0 && pageIdx < m_stack->count()) m_stack->setCurrentIndex(pageIdx);
        }
        syncCategoryTreeChrome();
    });
}

void PreferencesDialog::setupPages() {
    m_stack = new QStackedWidget(this);
    m_stack->setAttribute(Qt::WA_StyledBackground, true); m_stack->setAutoFillBackground(true);

    m_appearancePage = new AppearanceSettingsPage(m_stack);
    if (auto* appForm = qobject_cast<QFormLayout*>(m_appearancePage->layout())) {
        m_searchPopupSizeCombo = new QComboBox(m_appearancePage);
        m_searchPopupSizeCombo->setObjectName(QStringLiteral("searchPopupSizeCombo"));
        for (const auto& p : {std::pair{tr("Small (760 × 440)"), QStringLiteral("small")},
                              std::pair{tr("Medium (860 × 500)"), QStringLiteral("medium")},
                              std::pair{tr("Large (960 × 560)"), QStringLiteral("large")},
                              std::pair{tr("X-Large (1120 × 650)"), QStringLiteral("xlarge")}})
            m_searchPopupSizeCombo->addItem(p.first, p.second);
        const int szIdx = m_searchPopupSizeCombo->findData(gridlock::core::ConfigManager::instance().searchPopupSize());
        m_searchPopupSizeCombo->setCurrentIndex(szIdx >= 0 ? szIdx : 2);
        m_searchPopupSizeCombo->setToolTip(tr("Overlay footprint for Search Everywhere / Command Palette (Double Shift / Ctrl+P)."));
        appForm->addRow(tr("Search Everywhere Popup Size:"), m_searchPopupSizeCombo);

        m_menuBarWindowControlsCheck = new QCheckBox(tr("Show Window Controls in Menu Bar"), m_appearancePage);
        m_menuBarWindowControlsCheck->setObjectName(QStringLiteral("menuBarWindowControlsCheck"));
        m_menuBarWindowControlsCheck->setChecked(gridlock::core::ConfigManager::instance().showMenuBarWindowControls());
        m_menuBarWindowControlsCheck->setToolTip(tr("Show Minimize, Maximize, and Close corner buttons and drag-to-move support on the menu bar."));
        appForm->addRow(tr("Window Chrome:"), m_menuBarWindowControlsCheck);
    }

    gridlock::core::plugins::PluginManager* hostPlugMgr = nullptr;
    if (auto* mw = qobject_cast<MainWindow*>(parentWidget())) hostPlugMgr = mw->pluginManager();

    m_editingPage = new EditingSettingsPage(m_stack); m_behaviorPage = new BehaviorSettingsPage(m_stack);
    m_keybindingsPage = new KeybindingsSettingsPage(m_stack); m_debuggerPage = new DebuggerSettingsPage(m_stack);
    m_hpcPage = new HpcSettingsPage(m_stack); m_hpcIntegrationPage = new HpcIntegrationSettingsPage(m_stack);
    m_terminalPage = new TerminalSettingsPage(m_stack); m_docsetPage = new DocsetSettingsPage(m_stack);
    m_toolbarPage = createToolbarSettingsPage(m_stack); m_pluginPage = new PluginSettingsPage(hostPlugMgr, m_stack);

    for (QWidget* page : {static_cast<QWidget*>(m_appearancePage), static_cast<QWidget*>(m_editingPage),
                          static_cast<QWidget*>(m_behaviorPage), static_cast<QWidget*>(m_keybindingsPage),
                          static_cast<QWidget*>(m_debuggerPage), static_cast<QWidget*>(m_hpcPage),
                          static_cast<QWidget*>(m_hpcIntegrationPage), static_cast<QWidget*>(m_terminalPage),
                          static_cast<QWidget*>(m_docsetPage), m_toolbarPage, static_cast<QWidget*>(m_pluginPage)}) {
        page->setAttribute(Qt::WA_StyledBackground, true); page->setAutoFillBackground(true);
        auto* scroll = new QScrollArea(m_stack);
        scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setAttribute(Qt::WA_StyledBackground, true); scroll->setAutoFillBackground(true);
        scroll->setWidget(page); m_stack->addWidget(scroll);
    }
    m_stack->setCurrentIndex(0);
}

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

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    m_buttonBox->setContentsMargins(12, 8, 12, 8);
    for (QAbstractButton* btn : m_buttonBox->buttons()) if (btn) { btn->setIcon(QIcon()); btn->setIconSize(QSize(0, 0)); }
    m_buttonBox->installEventFilter(new IconStripFilter(m_buttonBox));
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &PreferencesDialog::acceptAndApply);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &PreferencesDialog::apply);
}

void PreferencesDialog::apply() {
    QSettings s("gridlock", "debugger");
    s.setValue("appearance/theme", m_appearancePage->selectedTheme()); s.setValue("appearance/dark_mode", m_appearancePage->isDarkMode());
    s.setValue("appearance/file_tree_style", m_appearancePage->fileTreeStyle()); s.setValue("appearance/colorize_icons", m_appearancePage->colorizeIcons());
    s.setValue("appearance/ui_font_family", m_appearancePage->uiFontFamily()); s.setValue("appearance/ui_font_size", m_appearancePage->uiFontSize());
    s.setValue("appearance/code_font_family", m_appearancePage->codeFontFamily()); s.setValue("appearance/code_font_size", m_appearancePage->codeFontSize());
    s.sync();
    auto& cfg = gridlock::core::ConfigManager::instance();
    cfg.setMemoryMapColorScheme(m_appearancePage->memoryMapColorScheme());
    gridlock::core::managers::ThemeManager::instance().applyThemeAndFonts();

    s.setValue("editing/tab_width", m_editingPage->tabWidth()); s.setValue("editing/insert_spaces", m_editingPage->insertSpaces());
    s.setValue("editing/show_whitespace", m_editingPage->showWhitespace()); s.setValue("editing/word_wrap", m_editingPage->wordWrap());
    s.setValue("editing/show_edge_column", m_editingPage->showEdgeColumn()); s.setValue("editing/edge_column", m_editingPage->edgeColumn());
    s.setValue("editing/rainbow_brackets", m_editingPage->rainbowBrackets()); s.setValue("editing/data_doc_highlighting", m_editingPage->dataDocHighlighting());
    s.setValue("editor/inline_variable_values", m_editingPage->inlineVariableValues());
    s.setValue("behavior/restore_breakpoints", m_behaviorPage->restoreBreakpoints());
    s.setValue("behavior/confirm_quit", m_behaviorPage->confirmQuit()); s.setValue("behavior/focus_on_stop", m_behaviorPage->focusOnStop());

    cfg.setRainbowBracketsEnabled(m_editingPage->rainbowBrackets());
    cfg.setDataDocHighlightingEnabled(m_editingPage->dataDocHighlighting());
    cfg.setInlineVariableValuesEnabled(m_editingPage->inlineVariableValues());
    if (m_searchPopupSizeCombo) cfg.setSearchPopupSize(m_searchPopupSizeCombo->currentData().toString());
    if (m_menuBarWindowControlsCheck) cfg.setShowMenuBarWindowControls(m_menuBarWindowControlsCheck->isChecked());
    if (auto* c = m_behaviorPage->findChild<QComboBox*>(QStringLiteral("telemetryFormatCombo"))) cfg.setTelemetryDisplayFormat(c->currentData().toString());
    if (auto* c = m_debuggerPage->findChild<QComboBox*>(QStringLiteral("debuggerEngineCombo"))) cfg.setDebuggerEngine(c->currentData().toString());
    if (auto* c = m_debuggerPage->findChild<QComboBox*>(QStringLiteral("defaultDisassemblyModeCombo"))) cfg.setDefaultDisassemblyMode(c->currentData().toString());
    if (auto* c = m_debuggerPage->findChild<QCheckBox*>(QStringLiteral("showDecompilerWithSourceCheck"))) cfg.setShowDecompilerWithSource(c->isChecked());
    cfg.setGlobalClangdPath(m_debuggerPage->clangdPath()); cfg.setGlobalDapAdapterPath(m_debuggerPage->dapAdapterPath());

    gridlock::core::DebuggerSettings ds{m_debuggerPage->gdbPath(), m_debuggerPage->mpiExecutable(),
                                        m_debuggerPage->mpiArgs(), m_debuggerPage->defaultRanks(),
                                        m_debuggerPage->trapFpe()};
    cfg.saveDebuggerSettings(ds);
    auto ps = cfg.loadProjectSettings();
    ps.customGdbPath = m_debuggerPage->gdbPath().toStdString();
    cfg.saveProjectSettings(ps);

    cfg.saveHpcSettings({m_hpcPage->hostsFile(), m_hpcPage->envVars(), m_hpcPage->strictAffinity()});
    cfg.saveSshSettings({m_hpcIntegrationPage->sshHost(), m_hpcIntegrationPage->sshUser(), m_hpcIntegrationPage->sshKeyPath()});
    cfg.saveSlurmSettings({m_hpcIntegrationPage->slurmTemplate(), m_hpcIntegrationPage->slurmPartition(),
                           m_hpcIntegrationPage->slurmNodes(), m_hpcIntegrationPage->slurmTasks(),
                           m_hpcIntegrationPage->requestGpus(), m_hpcIntegrationPage->gpusPerNode(),
                           m_hpcIntegrationPage->spackRoot()});

    auto& docMgr = gridlock::core::DocsetManager::instance();
    if (docMgr.getDocsetDirectory() != m_docsetPage->docsetDirectory()) {
        docMgr.setDocsetDirectory(m_docsetPage->docsetDirectory());
        m_docsetPage->refreshTable();
    }
    cfg.saveTerminalSettings({m_terminalPage->shellPath(), m_terminalPage->fontFamily(), m_terminalPage->fontSize()});
    if (m_keybindingsPage) m_keybindingsPage->applyPendingChanges();
    if (m_toolbarLayoutList) cfg.setToolbarLayout(pendingToolbarLayout());
    if (m_pluginPage) (void)m_pluginPage->applyChanges();

    s.sync();
    if (qApp) qApp->setProperty("gridlock_disasm_prefs_rev", qApp->property("gridlock_disasm_prefs_rev").toInt() + 1);
    emit preferencesChanged();
}

void PreferencesDialog::acceptAndApply() { apply(); accept(); }

QWidget* PreferencesDialog::createToolbarSettingsPage(QWidget* parent) {
    static const QList<QPair<QString, QString>> kNamedControls = {
        {QStringLiteral("target_selector"), QStringLiteral("Target Selector")}, {QStringLiteral("config_menu"), QStringLiteral("Launch Config Menu")},
        {QStringLiteral("rank_spin"), QStringLiteral("MPI Rank Count (np:)")}, {QStringLiteral("build"), QStringLiteral("Build / Clean / Rebuild")},
        {QStringLiteral("run"), QStringLiteral("Run Target")}, {QStringLiteral("continue"), QStringLiteral("Continue (Cont)")},
        {QStringLiteral("step_over"), QStringLiteral("Step Over")}, {QStringLiteral("step_into"), QStringLiteral("Step Into")},
        {QStringLiteral("step_out"), QStringLiteral("Step Out")}, {QStringLiteral("pause"), QStringLiteral("Pause")},
        {QStringLiteral("stop"), QStringLiteral("Stop Session")}, {QStringLiteral("record"), QStringLiteral("Time-Travel Record")},
        {QStringLiteral("search_everywhere"), QStringLiteral("Search Everywhere")}, {QStringLiteral("perspectives"), QStringLiteral("Workspace Perspectives")},
    };
    auto labelForToken = [](const QString& tok) -> QString {
        if (tok == QStringLiteral("spacer")) return QStringLiteral("[ Spacer (24px) ]");
        if (tok == QStringLiteral("flex_spacer")) return QStringLiteral("[ Flex Spring ]");
        if (tok == QStringLiteral("separator")) return QStringLiteral("[ Separator ]");
        for (const auto& p : kNamedControls) if (p.first == tok) return p.second;
        return tok;
    };

    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("preferencesToolbarPage"));
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(20, 16, 20, 24);
    root->setSpacing(12);
    auto* heading = new QLabel(QStringLiteral("<b>Main Toolbar Layout &amp; Spacing</b>"), page);
    heading->setObjectName(QStringLiteral("heading"));
    root->addWidget(heading);
    auto* sep = new QLabel(page); sep->setFixedHeight(1); sep->setObjectName(QStringLiteral("separator"));
    root->addWidget(sep);
    auto* desc = new QLabel(tr("Reorder toolbar items, insert fixed 24px spacers, right-aligning flex springs, or vertical separators, or hide optional controls."), page);
    desc->setWordWrap(true);
    root->addWidget(desc);

    auto* midRow = new QHBoxLayout(); midRow->setSpacing(10);
    m_toolbarLayoutList = new QListWidget(page);
    m_toolbarLayoutList->setObjectName(QStringLiteral("toolbarLayoutList"));
    m_toolbarLayoutList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_toolbarLayoutList->setDragDropMode(QAbstractItemView::InternalMove);
    m_toolbarLayoutList->setMinimumSize(220, 440);
    m_toolbarLayoutList->resize(360, 480);
    if (m_toolbarLayoutList->viewport()) m_toolbarLayoutList->viewport()->resize(356, 476);
    midRow->addWidget(m_toolbarLayoutList, 1);

    auto* btnCol = new QVBoxLayout(); btnCol->setSpacing(6);
    auto makeBtn = [&](const QString& text, const QString& obj) {
        auto* b = new QPushButton(text, page); b->setObjectName(obj); b->setMinimumWidth(140); btnCol->addWidget(b); return b;
    };
    auto* moveUpBtn = makeBtn(tr("Move Up"), QStringLiteral("toolbarMoveUpBtn"));
    auto* moveDownBtn = makeBtn(tr("Move Down"), QStringLiteral("toolbarMoveDownBtn"));
    auto* addSpacerBtn = makeBtn(tr("Add Spacer (24px)"), QStringLiteral("toolbarAddSpacerBtn"));
    auto* addFlexBtn = makeBtn(tr("Add Flex Spring"), QStringLiteral("toolbarAddFlexSpacerBtn"));
    auto* addSepBtn = makeBtn(tr("Add Separator"), QStringLiteral("toolbarAddSeparatorBtn"));
    auto* removeBtn = makeBtn(tr("Remove"), QStringLiteral("toolbarRemoveItemBtn"));
    btnCol->addStretch(1);
    auto* resetBtn = makeBtn(tr("Reset Default"), QStringLiteral("toolbarResetDefaultBtn"));
    midRow->addLayout(btnCol);
    root->addLayout(midRow, 1);

    auto* addHiddenRow = new QHBoxLayout(); addHiddenRow->setSpacing(8);
    addHiddenRow->addWidget(new QLabel(tr("Hidden Controls:"), page));
    m_toolbarHiddenCombo = new QComboBox(page); m_toolbarHiddenCombo->setObjectName(QStringLiteral("toolbarHiddenControlsCombo"));
    addHiddenRow->addWidget(m_toolbarHiddenCombo, 1);
    m_toolbarAddHiddenBtn = new QPushButton(tr("Add Control"), page);
    m_toolbarAddHiddenBtn->setObjectName(QStringLiteral("toolbarAddHiddenControlBtn"));
    m_toolbarAddHiddenBtn->setMinimumWidth(140);
    addHiddenRow->addWidget(m_toolbarAddHiddenBtn);
    root->addLayout(addHiddenRow);

    auto refreshHiddenCombo = [this]() {
        if (!m_toolbarHiddenCombo || !m_toolbarLayoutList) return;
        QSet<QString> active;
        for (int i = 0; i < m_toolbarLayoutList->count(); ++i)
            active.insert(m_toolbarLayoutList->item(i)->data(Qt::UserRole).toString());
        m_toolbarHiddenCombo->clear();
        for (const auto& p : kNamedControls) if (!active.contains(p.first)) m_toolbarHiddenCombo->addItem(p.second, p.first);
        const bool hasHidden = m_toolbarHiddenCombo->count() > 0;
        if (!hasHidden) m_toolbarHiddenCombo->addItem(tr("(All controls visible)"), QString());
        m_toolbarHiddenCombo->setEnabled(hasHidden);
        if (m_toolbarAddHiddenBtn) m_toolbarAddHiddenBtn->setEnabled(hasHidden);
    };
    auto populateList = [this, labelForToken, refreshHiddenCombo](const QStringList& tokens) {
        m_toolbarLayoutList->clear();
        for (const QString& tok : tokens) {
            auto* it = new QListWidgetItem(labelForToken(tok), m_toolbarLayoutList);
            it->setData(Qt::UserRole, tok); it->setSizeHint(QSize(220, 22));
        }
        if (m_toolbarLayoutList->count() > 0) m_toolbarLayoutList->setCurrentRow(0);
        refreshHiddenCombo();
    };
    auto insertToken = [this, labelForToken, refreshHiddenCombo](const QString& tok) {
        if (tok.isEmpty() || !m_toolbarLayoutList) return;
        const int row = m_toolbarLayoutList->currentRow(), ins = (row >= 0) ? (row + 1) : m_toolbarLayoutList->count();
        auto* it = new QListWidgetItem(labelForToken(tok));
        it->setData(Qt::UserRole, tok); it->setSizeHint(QSize(220, 22));
        m_toolbarLayoutList->insertItem(ins, it); m_toolbarLayoutList->setCurrentRow(ins); refreshHiddenCombo();
    };

    populateList(gridlock::core::ConfigManager::instance().toolbarLayout());
    connect(moveUpBtn, &QPushButton::clicked, this, [this]() {
        const int r = m_toolbarLayoutList->currentRow();
        if (r > 0) { m_toolbarLayoutList->insertItem(r - 1, m_toolbarLayoutList->takeItem(r)); m_toolbarLayoutList->setCurrentRow(r - 1); }
    });
    connect(moveDownBtn, &QPushButton::clicked, this, [this]() {
        const int r = m_toolbarLayoutList->currentRow();
        if (r >= 0 && r + 1 < m_toolbarLayoutList->count()) { m_toolbarLayoutList->insertItem(r + 1, m_toolbarLayoutList->takeItem(r)); m_toolbarLayoutList->setCurrentRow(r + 1); }
    });
    connect(addSpacerBtn, &QPushButton::clicked, this, [insertToken]() { insertToken(QStringLiteral("spacer")); });
    connect(addFlexBtn, &QPushButton::clicked, this, [insertToken]() { insertToken(QStringLiteral("flex_spacer")); });
    connect(addSepBtn, &QPushButton::clicked, this, [insertToken]() { insertToken(QStringLiteral("separator")); });
    connect(removeBtn, &QPushButton::clicked, this, [this, refreshHiddenCombo]() {
        const int r = m_toolbarLayoutList->currentRow();
        if (r >= 0 && m_toolbarLayoutList->count() > 1) {
            delete m_toolbarLayoutList->takeItem(r);
            m_toolbarLayoutList->setCurrentRow(qMin(r, m_toolbarLayoutList->count() - 1));
            refreshHiddenCombo();
        }
    });
    connect(m_toolbarAddHiddenBtn, &QPushButton::clicked, this, [this, insertToken]() {
        if (m_toolbarHiddenCombo) insertToken(m_toolbarHiddenCombo->currentData().toString());
    });
    connect(resetBtn, &QPushButton::clicked, this, [populateList]() { populateList(gridlock::core::ConfigManager::defaultToolbarLayout()); });
    return page;
}

void PreferencesDialog::syncCategoryTreeChrome() {
    const auto pal = gridlock::core::managers::ThemeManager::paletteForTheme(
        gridlock::core::managers::ThemeManager::instance().activeThemeId());
    const QColor accentCol(pal.accentHex);
    const QString selFg = (accentCol.lightness() < 165) ? QStringLiteral("#ffffff") : QStringLiteral("#16181d");
    const QString hoverBg = pal.isDark ? QStringLiteral("#252932") : pal.canvasHex;
    const QString hoverFg = pal.isDark ? QStringLiteral("#ffffff") : pal.textHex;

    if (m_toolbarLayoutList) {
        m_toolbarLayoutList->setStyleSheet(QStringLiteral(
            "QListWidget#toolbarLayoutList { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 2px; }"
            "QListWidget#toolbarLayoutList::item { padding: 2px 8px; border-radius: 3px; background-color: %1; color: %2; }"
            "QListWidget#toolbarLayoutList::item:selected, QListWidget#toolbarLayoutList::item:selected:!active { background-color: %4; color: %5; font-weight: 600; }")
            .arg(pal.canvasHex, pal.textHex, pal.borderHex, pal.accentHex, selFg));
        m_toolbarLayoutList->ensurePolished();
        QPalette lp = m_toolbarLayoutList->palette();
        for (auto cg : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            lp.setColor(cg, QPalette::Window, QColor(pal.canvasHex)); lp.setColor(cg, QPalette::Base, QColor(pal.canvasHex));
            lp.setColor(cg, QPalette::Text, QColor(pal.textHex)); lp.setColor(cg, QPalette::Highlight, accentCol);
            lp.setColor(cg, QPalette::HighlightedText, QColor(selFg));
        }
        m_toolbarLayoutList->setPalette(lp); m_toolbarLayoutList->setAutoFillBackground(true);
        if (m_toolbarLayoutList->viewport()) { m_toolbarLayoutList->viewport()->setPalette(lp); m_toolbarLayoutList->viewport()->setAutoFillBackground(true); }
    }
    if (!m_categoryTree) return;

    m_categoryTree->setStyleSheet(QStringLiteral(
        "QTreeWidget, QTreeView, QTreeWidget#preferencesCategoryTree { background-color: %1; color: %4; selection-background-color: %3; selection-color: %7; "
        "padding: 0px; margin: 0px; border: none; border-right: 1px solid %2; border-radius: 0px; outline: none; font-size: 13px; }"
        "QTreeWidget::item, QTreeView::item { padding: 5px 8px; min-height: 22px; background-color: %1; color: %4; }"
        "QTreeWidget::item:hover, QTreeView::item:hover { background-color: %5; color: %6; }"
        "QTreeWidget::item:selected, QTreeWidget::item:selected:active, QTreeWidget::item:selected:!active, "
        "QTreeView::item:selected, QTreeView::item:selected:active, QTreeView::item:selected:!active { "
        "background-color: %3; selection-background-color: %3; color: %7; selection-color: %7; font-weight: 600; }")
        .arg(pal.headerHex, pal.borderHex, pal.accentHex, pal.textHex, hoverBg, hoverFg, selFg));
    m_categoryTree->ensurePolished();

    QPalette sidePal = m_categoryTree->palette();
    for (auto cg : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        sidePal.setColor(cg, QPalette::Window, QColor(pal.headerHex)); sidePal.setColor(cg, QPalette::Base, QColor(pal.headerHex));
        sidePal.setColor(cg, QPalette::WindowText, QColor(pal.textHex)); sidePal.setColor(cg, QPalette::Text, QColor(pal.textHex));
        sidePal.setColor(cg, QPalette::Highlight, accentCol); sidePal.setColor(cg, QPalette::HighlightedText, QColor(selFg));
    }
    m_categoryTree->setPalette(sidePal);
    if (m_categoryTree->viewport()) { m_categoryTree->viewport()->setPalette(sidePal); m_categoryTree->viewport()->setAutoFillBackground(true); }

    const QColor selFgCol(selFg), textCol(pal.textHex);
    QTreeWidgetItem* activeItem = m_categoryTree->currentItem();
    auto paintIcon = [&](auto& self, QTreeWidgetItem* item) -> void {
        if (!item) return;
        QPixmap pm(16, 16); pm.fill(Qt::transparent);
        QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing, true);
        p.setFont(QFont(QStringLiteral("Symbols Nerd Font"), 11));
        p.setPen((item == activeItem) ? selFgCol : textCol);
        p.drawText(0, 0, 16, 16, Qt::AlignCenter, item->data(0, Qt::UserRole + 1).toString()); p.end();
        item->setIcon(0, QIcon(pm));
        for (int c = 0; c < item->childCount(); ++c) self(self, item->child(c));
    };
    for (int i = 0; i < m_categoryTree->topLevelItemCount(); ++i) paintIcon(paintIcon, m_categoryTree->topLevelItem(i));
}

QStringList PreferencesDialog::pendingToolbarLayout() const {
    if (!m_toolbarLayoutList) return gridlock::core::ConfigManager::instance().toolbarLayout();
    QStringList out;
    for (int i = 0; i < m_toolbarLayoutList->count(); ++i) {
        if (const QString tok = m_toolbarLayoutList->item(i)->data(Qt::UserRole).toString(); !tok.isEmpty()) out.append(tok);
    }
    return out.isEmpty() ? gridlock::core::ConfigManager::defaultToolbarLayout() : out;
}

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
