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
#include <QFontDatabase>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
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
 * @brief Returns a deduplicated list of 16+ curated and system-installed monospace font families.
 */
QStringList discoverMonospaceFontFamilies() {
    // Seed with 16 curated developer monospace families so headless/CI environments always have >= 15 entries.
    QStringList families = {
        QStringLiteral("JetBrains Mono"),     QStringLiteral("Fira Code"),
        QStringLiteral("FiraCode Nerd Font"), QStringLiteral("Hack"),
        QStringLiteral("Source Code Pro"),    QStringLiteral("Cascadia Code"),
        QStringLiteral("Iosevka"),            QStringLiteral("IBM Plex Mono"),
        QStringLiteral("DejaVu Sans Mono"),   QStringLiteral("Liberation Mono"),
        QStringLiteral("Ubuntu Mono"),        QStringLiteral("Noto Sans Mono"),
        QStringLiteral("Inconsolata"),        QStringLiteral("Victor Mono"),
        QStringLiteral("Terminus"),           QStringLiteral("Monospace")
    };
    // Merge all fixed-pitch or monospace-named system font families discovered via QFontDatabase.
    const QStringList sysFamilies = QFontDatabase::families();
    for (const QString& fam : sysFamilies) {
        const bool isMono = QFontDatabase::isFixedPitch(fam) ||
                            fam.contains(QStringLiteral("Mono"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Code"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Console"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Term"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Nerd"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Courier"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Iosevka"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Hack"), Qt::CaseInsensitive) ||
                            fam.contains(QStringLiteral("Inconsolata"), Qt::CaseInsensitive);
        if (isMono && !families.contains(fam, Qt::CaseInsensitive)) {
            families.append(fam);
        }
    }
    return families;
}

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
    browseBtn->setMinimumWidth(92);
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
    auto* form = createStandardPageForm(this, tr("<b>Appearance & Typography</b>"));

    // Curated workstation theme selector displaying human-readable titles (zero underscores).
    m_themeCombo = new QComboBox(this);
    m_themeCombo->setObjectName(QStringLiteral("themeSelectorCombo"));
    const QStringList themeIds =
        gridlock::core::managers::ThemeManager::instance().availableThemeIds();
    for (const QString& id : themeIds) {
        m_themeCombo->addItem(
            gridlock::core::managers::ThemeManager::themeDisplayName(id), id);
    }
    m_themeCombo->setToolTip(
        tr("Select a curated workstation color scheme. Changes apply live."));
    form->addRow(tr("Color Theme:"), m_themeCombo);

    // Live 5-color swatch preview strip (Canvas, Header, Border, Accent, Text).
    auto* swatchRow = new QHBoxLayout();
    swatchRow->setSpacing(6);
    const QStringList roles = {tr("Canvas"), tr("Header"), tr("Border"), tr("Accent"), tr("Text")};
    for (int i = 0; i < 5; ++i) {
        auto* box = new QLabel(roles[i], this);
        box->setAlignment(Qt::AlignCenter);
        box->setMinimumSize(96, 28);
        m_swatchBoxes.append(box);
        swatchRow->addWidget(box);
    }
    swatchRow->addStretch(1);
    form->addRow(tr("Palette Swatches:"), swatchRow);

    // Dark mode toggle checkbox synchronized with the active theme.
    m_darkModeCheck = new QCheckBox(tr("Enable Dark Mode"), this);
    form->addRow(QString(), m_darkModeCheck);

    // File tree presentation density.
    m_fileTreeStyleCombo = new QComboBox(this);
    m_fileTreeStyleCombo->addItems({tr("Compact"), tr("Comfortable"), tr("Large")});
    form->addRow(tr("File Tree Style:"), m_fileTreeStyleCombo);

    // File icon colorization toggle.
    m_colorizeIconsCheck = new QCheckBox(tr("Enable Colorful File Icons"), this);
    form->addRow(QString(), m_colorizeIconsCheck);

    // UI Font Family and Size selectors.
    m_uiFontFamilyCombo = new QComboBox(this);
    m_uiFontFamilyCombo->addItems({QStringLiteral("Inter"), QStringLiteral("Cantarell"),
                                   QStringLiteral("Ubuntu"), QStringLiteral("JetBrains Mono"),
                                   QStringLiteral("Monospace")});
    form->addRow(tr("UI Font Family:"), m_uiFontFamilyCombo);

    m_uiFontSizeBox = new QSpinBox(this);
    m_uiFontSizeBox->setRange(7, 32);
    form->addRow(tr("UI Font Size:"), m_uiFontSizeBox);

    // Code / Disassembly Font Family and Size selectors populated with 16+ monospace fonts.
    m_codeFontFamilyCombo = new QComboBox(this);
    m_codeFontFamilyCombo->addItems(discoverMonospaceFontFamilies());
    form->addRow(tr("Code Font Family:"), m_codeFontFamilyCombo);

    m_codeFontSizeBox = new QSpinBox(this);
    m_codeFontSizeBox->setRange(7, 48);
    form->addRow(tr("Code Font Size:"), m_codeFontSizeBox);

    addFormNote(this, form,
                tr("<small style='color:#888;'>All color themes, UI fonts, and code/disassembly "
                   "typography scale live upon clicking Apply or OK.</small>"));

    // Populate initial state with signals blocked so opening the dialog does not mutate QSS.
    loadFromSettings();

    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (idx < 0) return;
        const QString themeId = m_themeCombo->itemData(idx).toString();
        const bool isDark = !themeId.contains(QStringLiteral("light"), Qt::CaseInsensitive);
        m_darkModeCheck->blockSignals(true);
        m_darkModeCheck->setChecked(isDark);
        m_darkModeCheck->blockSignals(false);
        updateSwatchPreview(themeId);
        gridlock::core::managers::ThemeManager::instance().setTheme(themeId, isDark);
    });
    connect(m_darkModeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        gridlock::core::managers::ThemeManager::instance().setDarkMode(checked);
        updateSwatchPreview(selectedTheme());
    });
}

/**
 * @brief Updates the 5-swatch palette preview boxes (Canvas, Header, Border, Accent, Text)
 *        with automatic WCAG luminance contrast and role + hex label text.
 */
void AppearanceSettingsPage::updateSwatchPreview(const QString& themeId) {
    const QStringList sw = gridlock::core::managers::ThemeManager::themeSwatchColors(themeId);
    const QStringList roles = {tr("Canvas"), tr("Header"), tr("Border"), tr("Accent"), tr("Text")};
    for (int i = 0; i < m_swatchBoxes.size() && i < sw.size() && i < roles.size(); ++i) {
        const QColor bg(sw[i]);
        // Compute perceived luminance L = 0.299*R + 0.587*G + 0.114*B for WCAG contrast.
        const double lum = 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue();
        const QString fgHex = (lum >= 145.0) ? QStringLiteral("#16181d") : QStringLiteral("#ffffff");
        const QColor fgColor(fgHex);

        // Display both role name and hex code inside the swatch pill.
        m_swatchBoxes[i]->setText(QStringLiteral("%1 %2").arg(roles[i], sw[i].toLower()));
        QPalette pal = m_swatchBoxes[i]->palette();
        pal.setColor(QPalette::All, QPalette::WindowText, fgColor);
        pal.setColor(QPalette::All, QPalette::Text, fgColor);
        m_swatchBoxes[i]->setPalette(pal);
        m_swatchBoxes[i]->setStyleSheet(
            QStringLiteral("background-color: %1; color: %2; border: 1px solid #3b4048; "
                           "border-radius: 4px; min-width: 96px; min-height: 28px; "
                           "padding: 3px 8px; font-size: 11px; font-weight: 600;")
                .arg(sw[i], fgHex));
        m_swatchBoxes[i]->setToolTip(QStringLiteral("%1: %2").arg(roles[i], sw[i]));
    }
}

/** @brief Returns the active internal theme identifier selected in the combo box. */
QString AppearanceSettingsPage::selectedTheme() const {
    return m_themeCombo->currentData().toString().isEmpty() ? m_themeCombo->currentText() : m_themeCombo->currentData().toString();
}
bool AppearanceSettingsPage::isDarkMode() const { return m_darkModeCheck->isChecked(); }
QString AppearanceSettingsPage::fileTreeStyle() const { return m_fileTreeStyleCombo->currentText(); }
bool AppearanceSettingsPage::colorizeIcons() const { return m_colorizeIconsCheck->isChecked(); }
QString AppearanceSettingsPage::uiFontFamily() const { return m_uiFontFamilyCombo->currentText(); }
int AppearanceSettingsPage::uiFontSize() const { return m_uiFontSizeBox->value(); }
QString AppearanceSettingsPage::codeFontFamily() const { return m_codeFontFamilyCombo->currentText(); }
int AppearanceSettingsPage::codeFontSize() const { return m_codeFontSizeBox->value(); }

/** @brief Loads persisted appearance preferences from user settings with signals blocked. */
void AppearanceSettingsPage::loadFromSettings() {
    // Query persisted theme ID, polarity flag, and typography metrics from QSettings.
    QSettings s("gridlock", "debugger");
    const QString theme = s.value("appearance/theme", "gridlock_slate").toString();
    const bool isDark = s.value("appearance/dark_mode", true).toBool();
    const QVector<QWidget*> ctrls = {m_darkModeCheck, m_themeCombo, m_fileTreeStyleCombo, m_colorizeIconsCheck,
                                     m_uiFontFamilyCombo, m_uiFontSizeBox, m_codeFontFamilyCombo, m_codeFontSizeBox};
    // Temporarily suppress change signals while populating initial form state.
    for (QWidget* w : ctrls) w->blockSignals(true);

    m_darkModeCheck->setChecked(isDark);
    int idx = m_themeCombo->findData(theme);
    if (idx < 0) idx = m_themeCombo->findText(theme, Qt::MatchContains);
    if (idx >= 0) m_themeCombo->setCurrentIndex(idx);
    updateSwatchPreview(selectedTheme());

    // Restore file tree density, icon colorization, and UI/Code font families and sizes.
    const int ftsIdx = m_fileTreeStyleCombo->findText(s.value("appearance/file_tree_style", "Comfortable").toString());
    if (ftsIdx >= 0) m_fileTreeStyleCombo->setCurrentIndex(ftsIdx);
    m_colorizeIconsCheck->setChecked(s.value("appearance/colorize_icons", true).toBool());

    const int uiFamIdx = m_uiFontFamilyCombo->findText(s.value("appearance/ui_font_family", "Inter").toString());
    if (uiFamIdx >= 0) m_uiFontFamilyCombo->setCurrentIndex(uiFamIdx);
    m_uiFontSizeBox->setValue(s.value("appearance/ui_font_size", 11).toInt());

    const int codeFamIdx = m_codeFontFamilyCombo->findText(s.value("appearance/code_font_family", "JetBrains Mono").toString());
    if (codeFamIdx >= 0) m_codeFontFamilyCombo->setCurrentIndex(codeFamIdx);
    m_codeFontSizeBox->setValue(s.value("appearance/code_font_size", 11).toInt());

    for (QWidget* w : ctrls) w->blockSignals(false);
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
    browseBtn->setMinimumWidth(92);
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

    // Terminal monospace font family combo populated with 16+ curated and system monospace fonts.
    m_fontFamilyCombo = new QComboBox(this);
    m_fontFamilyCombo->setObjectName(QStringLiteral("terminalFontFamilyCombo"));
    m_fontFamilyCombo->addItems(discoverMonospaceFontFamilies());
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
