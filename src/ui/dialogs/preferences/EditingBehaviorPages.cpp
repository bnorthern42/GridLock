/**
 * @file EditingBehaviorPages.cpp
 * @brief Implementations of EditingSettingsPage and BehaviorSettingsPage widgets.
 */

#include "EditingBehaviorPages.hpp"
#include "PreferencesFormHelpers.hpp"
#include "../../../core/managers/ConfigManager.hpp"

#include <QFormLayout>
#include <QSettings>

namespace gridlock::ui {

using prefs_detail::addFormNote;
using prefs_detail::createStandardPageForm;

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

    // Rainbow Brackets nesting depth highlighting toggle.
    m_rainbowBracketsCheck = new QCheckBox(
        tr("Enable Rainbow Brackets (colorize nested (), {}, [] depth)"), this);
    m_rainbowBracketsCheck->setObjectName(QStringLiteral("rainbowBracketsCheck"));
    m_rainbowBracketsCheck->setToolTip(
        tr("Colorize nested parentheses, braces, and brackets by depth while ignoring strings and comments."));
    form->addRow(QString(), m_rainbowBracketsCheck);

    // TOML, YAML, Markdown, and CSV syntax highlighting toggle.
    m_dataDocHighlightingCheck = new QCheckBox(
        tr("Enable Syntax Highlighting for Config & Docs (TOML, YAML, Markdown, CSV)"), this);
    m_dataDocHighlightingCheck->setObjectName(QStringLiteral("dataDocHighlightingCheck"));
    m_dataDocHighlightingCheck->setToolTip(
        tr("Apply theme-aware syntax highlighting to TOML, YAML, Markdown, and CSV/TSV files."));
    form->addRow(QString(), m_dataDocHighlightingCheck);

    // CLion-style inline variable ghost values toggle when paused.
    m_inlineVarValuesCheck = new QCheckBox(
        tr("Show Inline Variable Values in Editor when Paused (CLion-style ghost text)"), this);
    m_inlineVarValuesCheck->setObjectName(QStringLiteral("inlineVarValuesCheck"));
    m_inlineVarValuesCheck->setToolTip(
        tr("Render live local variable values as muted italic ghost text after lines while paused."));
    form->addRow(QString(), m_inlineVarValuesCheck);

    loadFromSettings();
}

int EditingSettingsPage::tabWidth() const { return m_tabWidthBox->value(); }
bool EditingSettingsPage::insertSpaces() const { return m_indentModeCombo->currentIndex() == 0; }
bool EditingSettingsPage::showWhitespace() const { return m_whitespaceCombo->currentIndex() == 1; }
bool EditingSettingsPage::wordWrap() const { return m_wordWrapCheck->isChecked(); }
bool EditingSettingsPage::showEdgeColumn() const { return m_showEdgeColumnCheck->isChecked(); }
int EditingSettingsPage::edgeColumn() const { return m_edgeColumnBox->value(); }
bool EditingSettingsPage::rainbowBrackets() const { return m_rainbowBracketsCheck->isChecked(); }
bool EditingSettingsPage::dataDocHighlighting() const { return m_dataDocHighlightingCheck->isChecked(); }
bool EditingSettingsPage::inlineVariableValues() const { return m_inlineVarValuesCheck->isChecked(); }

/**
 * @brief Restores editing preferences from user settings storage.
 */
void EditingSettingsPage::loadFromSettings() {
    QSettings s("gridlock", "debugger");
    auto& cfg = gridlock::core::ConfigManager::instance();
    m_tabWidthBox->setValue(s.value("editing/tab_width", 4).toInt());
    m_indentModeCombo->setCurrentIndex(
        s.value("editing/insert_spaces", true).toBool() ? 0 : 1);
    m_whitespaceCombo->setCurrentIndex(
        s.value("editing/show_whitespace", false).toBool() ? 1 : 0);
    m_wordWrapCheck->setChecked(s.value("editing/word_wrap", false).toBool());
    m_showEdgeColumnCheck->setChecked(
        s.value("editing/show_edge_column", true).toBool());
    m_edgeColumnBox->setValue(s.value("editing/edge_column", 80).toInt());
    m_rainbowBracketsCheck->setChecked(cfg.rainbowBracketsEnabled());
    m_dataDocHighlightingCheck->setChecked(cfg.dataDocHighlightingEnabled());
    m_inlineVarValuesCheck->setChecked(cfg.inlineVariableValuesEnabled());
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

    // Status bar live Stack/Heap and system RAM/CPU telemetry presentation format selector.
    auto* telemetryFormatCombo = new QComboBox(this);
    telemetryFormatCombo->setObjectName(QStringLiteral("telemetryFormatCombo"));
    telemetryFormatCombo->addItem(tr("GB / Total GB + CPU %"), QStringLiteral("gb"));
    telemetryFormatCombo->addItem(tr("Percentage (%) Only"), QStringLiteral("percent"));
    telemetryFormatCombo->addItem(tr("Both (GB / Total GB + %)"), QStringLiteral("both"));
    telemetryFormatCombo->setToolTip(
        tr("Choose how system RAM and CPU utilization are formatted alongside per-rank Stack and Heap in the bottom status bar."));
    form->addRow(tr("Status Bar Resource Format:"), telemetryFormatCombo);

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
    if (rbIdx >= 0) m_restoreBreakpointsCheck->setCurrentIndex(rbIdx);
    m_confirmQuitCheck->setCurrentIndex(
        s.value("behavior/confirm_quit", true).toBool() ? 0 : 1);
    m_focusOnStopCheck->setCurrentIndex(
        s.value("behavior/focus_on_stop", true).toBool() ? 0 : 1);
    if (auto* telemetryCombo = findChild<QComboBox*>(QStringLiteral("telemetryFormatCombo"))) {
        const QString fmt = gridlock::core::ConfigManager::instance().telemetryDisplayFormat();
        const int fmtIdx = telemetryCombo->findData(fmt);
        telemetryCombo->setCurrentIndex(fmtIdx >= 0 ? fmtIdx : 0);
    }
}

}  // namespace gridlock::ui
