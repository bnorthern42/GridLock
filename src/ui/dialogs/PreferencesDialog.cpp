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
    setObjectName(QStringLiteral("PreferencesDialog"));
    setWindowTitle(tr("Preferences — GridLock"));
    setMinimumSize(780, 540);
    resize(860, 580);

    // Enable styled QSS background painting and solid palette auto-fill so editor text never bleeds through.
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);

    // Root vertical layout containing the horizontal category splitter and bottom action button bar.
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // Horizontal splitter separating the 195px category sidebar list from the active settings page stack.
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);

    // Construct navigation sidebar and stacked settings pages.
    setupSidebar();
    setupPages();

    // Mount sidebar and stacked page container with fixed sidebar stretch (0) and expanding content (1).
    splitter->addWidget(m_sidebar);
    splitter->addWidget(m_stack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({195, 665});

    rootLayout->addWidget(splitter, 1);

    // Subtle 1px horizontal divider line separating the page stack from the Ok/Cancel/Apply bar.
    auto* line = new QLabel(this);
    line->setFixedHeight(1);
    line->setObjectName(QStringLiteral("separator"));
    rootLayout->addWidget(line);

    setupButtonBox();
    rootLayout->addWidget(m_buttonBox);

    // Harmonize 26px control heights across settings pages while preserving modern footer button padding.
    for (QPushButton* btn : findChildren<QPushButton*>()) {
        if (m_buttonBox && m_buttonBox->buttons().contains(btn)) continue;
        btn->setFixedHeight(26);
        if (btn->text().contains(QStringLiteral("Browse"), Qt::CaseInsensitive)) {
            btn->setMinimumWidth(96);
        }
    }
    for (QLineEdit* edit : findChildren<QLineEdit*>()) {
        edit->setFixedHeight(26);
    }
    for (QComboBox* combo : findChildren<QComboBox*>()) {
        combo->setFixedHeight(26);
    }

    // Synchronize dialog theme chrome, sidebar contrast, and primary/secondary footer action buttons.
    applyDialogThemeChrome();
    connect(&gridlock::core::managers::ThemeManager::instance(),
            &gridlock::core::managers::ThemeManager::themeModeChanged,
            this, [this](bool) { applyDialogThemeChrome(); });
}

/**
 * @brief Synchronizes opaque dialog surfaces, sidebar typography, QSS rules, and QPalettes with the active theme.
 *
 * Queries semantic ThemePalette hex tokens (`canvasHex`, `headerHex`, `borderHex`, `accentHex`, `textHex`)
 * from ThemeManager and applies both explicit QSS rules and `Active`/`Inactive`/`Disabled` QPalette roles
 * across `PreferencesDialog`, `m_stack`, `m_sidebar`, and `m_sidebar->viewport()`.
 */
void PreferencesDialog::applyDialogThemeChrome() {
    auto& tm = gridlock::core::managers::ThemeManager::instance();
    const auto pal = gridlock::core::managers::ThemeManager::paletteForTheme(tm.activeThemeId());
    const QString canvas = pal.canvasHex;
    const QString header = pal.headerHex;
    const QString border = pal.borderHex;
    const QString accent = pal.accentHex;
    const QString text = pal.textHex;

    // Select crisp white (#ffffff) text when accent lightness < 165, otherwise dark charcoal (#16181d).
    const QColor accentCol(accent);
    const QString selFg = (accentCol.lightness() < 165)
                              ? QStringLiteral("#ffffff")
                              : QStringLiteral("#16181d");
    const QString hoverBg = pal.isDark ? QStringLiteral("#252932") : canvas;
    const QString hoverFg = pal.isDark ? QStringLiteral("#ffffff") : text;

    // Style root dialog, stacked pages, group boxes, 13px dialog controls, buttons, and divider explicitly.
    setStyleSheet(QStringLiteral(
        "QDialog, QDialog#PreferencesDialog, QWidget#PreferencesDialog, "
        "QStackedWidget, QStackedWidget > QWidget, QScrollArea { background-color: %1; color: %5; font-size: 13px; }"
        "QLabel, QCheckBox { color: %5; font-size: 13px; }"
        "QGroupBox { background-color: %2; border: 1px solid %3; border-radius: 4px; "
        "margin-top: 8px; padding-top: 10px; color: %5; font-size: 13px; font-weight: 600; }"
        "QLineEdit, QComboBox, QSpinBox { background-color: %1; color: %5; "
        "border: 1px solid %3; border-radius: 3px; padding: 1px 6px; min-height: 22px; max-height: 26px; font-size: 13px; }"
        "QStackedWidget QPushButton { background-color: %2; color: %5; border: 1px solid %3; "
        "border-radius: 3px; padding: 2px 10px; min-height: 22px; max-height: 26px; font-size: 13px; }"
        "QStackedWidget QPushButton:hover { background-color: %6; color: %7; border-color: %4; }"
        "QLabel#separator { background-color: %3; }")
                      .arg(canvas, header, border, accent, text, hoverBg, hoverFg));
    ensurePolished();

    const QColor canvasCol(canvas);
    const QColor headerCol(header);
    const QColor textCol(text);
    const QColor selFgCol(selFg);

    // Apply solid opaque QPalette roles across Active, Inactive, and Disabled groups for dialog & pages.
    QPalette dlgPal = palette();
    for (QPalette::ColorGroup cg : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        dlgPal.setColor(cg, QPalette::Window, canvasCol);
        dlgPal.setColor(cg, QPalette::Base, canvasCol);
        dlgPal.setColor(cg, QPalette::AlternateBase, headerCol);
        dlgPal.setColor(cg, QPalette::WindowText, textCol);
        dlgPal.setColor(cg, QPalette::Text, textCol);
        dlgPal.setColor(cg, QPalette::Button, headerCol);
        dlgPal.setColor(cg, QPalette::ButtonText, textCol);
        dlgPal.setColor(cg, QPalette::Highlight, accentCol);
        dlgPal.setColor(cg, QPalette::HighlightedText, selFgCol);
    }
    setPalette(dlgPal);
    // Re-assert autoFillBackground after QStyleSheetStyle::polish so backing-store erase stays active.
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);

    if (m_stack) {
        m_stack->setAttribute(Qt::WA_StyledBackground, true);
        m_stack->setPalette(dlgPal);
        m_stack->setAutoFillBackground(true);
        for (int i = 0; i < m_stack->count(); ++i) {
            if (QWidget* page = m_stack->widget(i)) {
                page->setAttribute(Qt::WA_StyledBackground, true);
                page->setPalette(dlgPal);
                page->setAutoFillBackground(true);
            }
        }
    }

    // Apply opaque header surface, 13px typography, 8px 12px padding, and high-contrast selection to m_sidebar + viewport().
    if (m_sidebar) {
        m_sidebar->setStyleSheet(QStringLiteral(
            "QListWidget, QListView, QListWidget#preferencesCategoryList { "
            "background-color: %1; color: %4; selection-background-color: %3; selection-color: %7; "
            "padding: 0px; margin: 0px; border: none; border-right: 1px solid %2; border-radius: 0px; "
            "outline: none; font-size: 13px; }"
            "QListWidget::item, QListView::item { "
            "padding: 8px 12px; min-height: 20px; background-color: %1; color: %4; "
            "selection-background-color: %3; selection-color: %7; }"
            "QListWidget::item:hover, QListView::item:hover { background-color: %5; color: %6; }"
            "QListWidget::item:selected, QListWidget::item:selected:focus, "
            "QListWidget::item:selected:active, QListWidget::item:selected:!active, "
            "QListWidget::item:selected:hover, QListView::item:selected, "
            "QListView::item:selected:focus, QListView::item:selected:active, "
            "QListView::item:selected:!active, QListView::item:selected:hover { "
            "background-color: %3; selection-background-color: %3; "
            "color: %7; selection-color: %7; font-weight: 600; }")
                                     .arg(header, border, accent, text, hoverBg, hoverFg, selFg));
        m_sidebar->ensurePolished();

        // Synchronize Highlight and HighlightedText on both m_sidebar and m_sidebar->viewport() across all groups.
        QPalette sidePal = m_sidebar->palette();
        for (QPalette::ColorGroup cg : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            sidePal.setColor(cg, QPalette::Window, headerCol);
            sidePal.setColor(cg, QPalette::Base, headerCol);
            sidePal.setColor(cg, QPalette::AlternateBase, headerCol);
            sidePal.setColor(cg, QPalette::WindowText, textCol);
            sidePal.setColor(cg, QPalette::Text, textCol);
            sidePal.setColor(cg, QPalette::Highlight, accentCol);
            sidePal.setColor(cg, QPalette::HighlightedText, selFgCol);
        }
        m_sidebar->setPalette(sidePal);
        m_sidebar->setAttribute(Qt::WA_StyledBackground, true);
        m_sidebar->setAutoFillBackground(true);
        if (m_sidebar->viewport()) {
            m_sidebar->viewport()->setPalette(sidePal);
            m_sidebar->viewport()->setAutoFillBackground(true);
        }
        refreshSidebarIcons();
    }

    // Style modern icon-free footer buttons: primary accent OK and secondary bordered Cancel / Apply.
    // Using 16..18px content box height plus 6px vertical padding and 1px border yields a crisp 32px button.
    if (m_buttonBox) {
        // Primary action button (OK): solid theme accent fill with crisp white 13px bold label text (30px outer height).
        const QString okCss = QStringLiteral(
            "QPushButton, QPushButton#prefsOkBtn { background-color: %1; color: #ffffff; border: 1px solid %1; "
            "font-size: 13px; font-weight: 600; text-transform: none; padding: 6px 18px; margin: 0px; border-radius: 4px; "
            "min-height: 16px; max-height: 16px; }"
            "QPushButton:hover, QPushButton#prefsOkBtn:hover { background-color: %1; color: #ffffff; border: 1px solid %2; }")
                                  .arg(accent, text);
        // Secondary action buttons (Cancel, Apply): subtle header fill with 1px theme border and 13px text (30px outer height).
        const QString secCss = QStringLiteral(
            "QPushButton, QPushButton#prefsSecBtn { background-color: %1; color: %2; border: 1px solid %3; "
            "font-size: 13px; text-transform: none; padding: 6px 16px; margin: 0px; border-radius: 4px; "
            "min-height: 16px; max-height: 16px; }"
            "QPushButton:hover, QPushButton#prefsSecBtn:hover { background-color: %4; color: %5; border: 1px solid %6; }")
                                   .arg(header, text, border, hoverBg, hoverFg, accent);
        // Apply primary style to OK and force immediate polish before clearing stock icons.
        if (auto* okBtn = m_buttonBox->button(QDialogButtonBox::Ok)) {
            okBtn->setObjectName(QStringLiteral("prefsOkBtn"));
            okBtn->setStyleSheet(okCss);
            okBtn->ensurePolished();
        }
        // Apply secondary bordered style to Cancel and Apply buttons.
        for (auto role : {QDialogButtonBox::Cancel, QDialogButtonBox::Apply}) {
            if (auto* secBtn = m_buttonBox->button(role)) {
                secBtn->setObjectName(QStringLiteral("prefsSecBtn"));
                secBtn->setStyleSheet(secCss);
                secBtn->ensurePolished();
            }
        }
        // Clear stock check/cross icons after QStyleSheetStyle polish so QDialogButtonBox cannot restore them.
        // Setting iconSize(0, 0) also eliminates left icon-gutter whitespace inside each button.
        for (QAbstractButton* btn : m_buttonBox->buttons()) {
            if (btn) {
                btn->setIcon(QIcon());
                btn->setIconSize(QSize(0, 0));
            }
        }
    }
}

/**
 * @brief Re-renders sidebar Nerd Font icons with contrast-matched ink for selected and unselected rows.
 *
 * Ensures the active category row's glyph matches the selected text contrast (`#ffffff` on dark/medium
 * accents or `#16181d` on bright accents) while unselected category rows use `pal.textHex` so icons
 * remain legible on both dark slate and warm light cream sidebars.
 */
void PreferencesDialog::refreshSidebarIcons() {
    if (!m_sidebar) return;
    auto& tm = gridlock::core::managers::ThemeManager::instance();
    const auto pal = gridlock::core::managers::ThemeManager::paletteForTheme(tm.activeThemeId());
    const QColor accentCol(pal.accentHex);
    const QColor selFgCol = (accentCol.lightness() < 165) ? QColor(QStringLiteral("#ffffff"))
                                                          : QColor(QStringLiteral("#16181d"));
    const QColor textCol(pal.textHex);
    const int activeRow = m_sidebar->currentRow();

    // Repaint each 16x16 transparent glyph pixmap with anti-aliased Symbols Nerd Font ink.
    for (int i = 0; i < m_sidebar->count(); ++i) {
        QListWidgetItem* item = m_sidebar->item(i);
        if (!item) continue;
        const QString glyph = item->data(Qt::UserRole).toString();
        const QColor iconColor = (i == activeRow) ? selFgCol : textCol;
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(QFont(QStringLiteral("Symbols Nerd Font"), 11));
        painter.setPen(iconColor);
        painter.drawText(0, 0, 16, 16, Qt::AlignCenter, glyph);
        item->setIcon(QIcon(pixmap));
    }
}

/**
 * @brief Builds the vertical category navigation list with icons and opaque surface attributes.
 *
 * Configures a 195px sidebar (`minimumWidth >= 185px`) with 13px typography, 36px row height,
 * suppressed horizontal scrollbars, and dynamic Nerd Font glyph rendering.
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
    if (m_sidebar->viewport()) {
        m_sidebar->viewport()->setAutoFillBackground(true);
    }

    // Apply clean 13px UI font sizing to category navigation labels.
    QFont sideFont = m_sidebar->font();
    sideFont.setPixelSize(13);
    m_sidebar->setFont(sideFont);

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

    // Populate category items with spacious 36px row hints and stored Nerd Font glyph metadata.
    for (const auto& e : entries) {
        auto* item = new QListWidgetItem(m_sidebar);
        item->setText(e.label);
        item->setData(Qt::UserRole, e.iconName);
        item->setSizeHint(QSize(190, 36));
        m_sidebar->addItem(item);
    }

    // Default to the first page (Appearance) and keep icons & stacked page synchronized on selection.
    m_sidebar->setCurrentRow(0);
    refreshSidebarIcons();
    connect(m_sidebar, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_stack) {
            m_stack->setCurrentIndex(row);
        }
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
        page->setAttribute(Qt::WA_StyledBackground, true);
        page->setAutoFillBackground(true);
        m_stack->addWidget(page);
    }
    m_stack->setCurrentIndex(0);
}

/**
 * @brief Constructs modern icon-free Ok, Apply, and Cancel dialog action buttons.
 */
void PreferencesDialog::setupButtonBox() {
    // Event filter intercepting Qt style/polish/show events so QDialogButtonBox never restores stock icons.
    class IconStripFilter final : public QObject {
    public:
        // Bind filter lifetime to the QDialogButtonBox parent widget.
        explicit IconStripFilter(QDialogButtonBox* box) : QObject(box), m_box(box) {}
        // Strip non-null button icons whenever QStyleSheetStyle re-polishes the button box.
        bool eventFilter(QObject* w, QEvent* ev) override {
            if (m_box && (ev->type() == QEvent::StyleChange || ev->type() == QEvent::Polish || ev->type() == QEvent::Show)) {
                for (QAbstractButton* b : m_box->buttons())
                    if (b && !b->icon().isNull()) { b->setIcon(QIcon()); b->setIconSize(QSize(0, 0)); }
            }
            return QObject::eventFilter(w, ev);
        }
    private:
        // Non-owning pointer to the monitored QDialogButtonBox instance.
        QDialogButtonBox* m_box = nullptr;
    };

    // Instantiate standard horizontal Ok / Apply / Cancel button bar with 12px/8px footer margins.
    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel,
        Qt::Horizontal, this);
    m_buttonBox->setContentsMargins(12, 8, 12, 8);
    // Strip stock check/cross icons immediately and guard against QStyleChange icon restoration.
    for (QAbstractButton* btn : m_buttonBox->buttons()) {
        if (btn) {
            btn->setIcon(QIcon());
            btn->setIconSize(QSize(0, 0));
        }
    }
    m_buttonBox->installEventFilter(new IconStripFilter(m_buttonBox));

    // Wire accepted, rejected, and Apply signals to persistent settings flush slots.
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
    s.setValue("appearance/ui_font_family", m_appearancePage->uiFontFamily());
    s.setValue("appearance/ui_font_size", m_appearancePage->uiFontSize());
    s.setValue("appearance/code_font_family", m_appearancePage->codeFontFamily());
    s.setValue("appearance/code_font_size", m_appearancePage->codeFontSize());
    s.sync();
    gridlock::core::managers::ThemeManager::instance().applyThemeAndFonts();

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

/**
 * @brief Parses a SLURM batch script and populates the HPC Integration page controls.
 */
bool PreferencesDialog::parseSlurmBatchScript(const QString& scriptText) {
    return m_hpcIntegrationPage ? m_hpcIntegrationPage->parseSlurmBatchScript(scriptText) : false;
}

/**
 * @brief Loads a SLURM `.sbatch`/`.slurm` file from disk into the HPC Integration page.
 */
bool PreferencesDialog::loadSlurmBatchScriptFile(const QString& filePath) {
    return m_hpcIntegrationPage ? m_hpcIntegrationPage->loadSlurmBatchScriptFile(filePath) : false;
}

/**
 * @brief Generates a SLURM batch script from the current HPC Integration page fields.
 */
QString PreferencesDialog::generateSlurmBatchScript() const {
    return m_hpcIntegrationPage ? m_hpcIntegrationPage->generateSlurmBatchScript() : QString();
}

/**
 * @brief Queries a specific SLURM field value by key from the HPC Integration page.
 */
QString PreferencesDialog::slurmFieldValue(const QString& key) const {
    return m_hpcIntegrationPage ? m_hpcIntegrationPage->slurmFieldValue(key) : QString();
}

}  // namespace gridlock::ui