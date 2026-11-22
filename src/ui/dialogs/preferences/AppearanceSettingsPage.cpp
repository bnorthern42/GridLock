/**
 * @file AppearanceSettingsPage.cpp
 * @brief Implementation of Appearance & Typography settings page widget.
 */

#include "AppearanceSettingsPage.hpp"
#include "PreferencesFormHelpers.hpp"
#include "../../../core/managers/ConfigManager.hpp"
#include "../../../core/managers/ThemeManager.hpp"

#include <QColor>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPalette>
#include <QSettings>

namespace gridlock::ui {

using prefs_detail::addFormNote;
using prefs_detail::createStandardPageForm;
using prefs_detail::discoverMonospaceFontFamilies;

/**
 * @brief Constructs the visual appearance settings page.
 * @param parent Optional parent widget.
 */
AppearanceSettingsPage::AppearanceSettingsPage(QWidget* parent)
    : QWidget(parent) {
    auto* form = createStandardPageForm(this, tr("<b>Appearance & Typography</b>"));

    // Curated workstation theme selector displaying human-readable titles (zero underscores) for all 12 themes.
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
    swatchRow->setSpacing(8);
    swatchRow->setContentsMargins(0, 2, 0, 2);
    const QStringList roles = {QStringLiteral("Canvas"), QStringLiteral("Header"),
                               QStringLiteral("Border"), QStringLiteral("Accent"),
                               QStringLiteral("Text")};
    for (int i = 0; i < 5; ++i) {
        auto* box = new QLabel(roles[i], this);
        box->setAlignment(Qt::AlignCenter);
        box->setFixedHeight(26);
        box->setMinimumWidth(82);
        m_swatchBoxes.append(box);
        swatchRow->addWidget(box);
    }
    swatchRow->addStretch(1);
    form->addRow(tr("Palette Swatches:"), swatchRow);

    m_memoryMapColorSchemeCombo = new QComboBox(this);
    m_memoryMapColorSchemeCombo->setObjectName(QStringLiteral("memoryMapColorSchemeCombo"));
    m_memoryMapColorSchemeCombo->addItem(tr("Category & Library Distinct"), QStringLiteral("Distinct"));
    m_memoryMapColorSchemeCombo->addItem(tr("Viridis Scientific"), QStringLiteral("Viridis"));
    m_memoryMapColorSchemeCombo->addItem(tr("Plasma Thermal"), QStringLiteral("Plasma"));
    m_memoryMapColorSchemeCombo->addItem(tr("Ocean Cool-Warm"), QStringLiteral("Ocean"));
    m_memoryMapColorSchemeCombo->addItem(tr("High-Contrast Accessible"), QStringLiteral("HighContrast"));
    m_memoryMapColorSchemeCombo->setToolTip(tr("Color palette used by Memory Profiler treemap and stacked bar."));
    form->addRow(tr("Memory Map Color Scheme:"), m_memoryMapColorSchemeCombo);

    // Keep internal polarity state synchronized automatically with the selected theme.
    m_darkModeCheck = new QCheckBox(this);
    m_darkModeCheck->setVisible(false);

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
        const bool isDark = gridlock::core::managers::ThemeManager::paletteForTheme(themeId).isDark;
        m_darkModeCheck->blockSignals(true);
        m_darkModeCheck->setChecked(isDark);
        m_darkModeCheck->blockSignals(false);
        gridlock::core::managers::ThemeManager::instance().applyTheme(themeId);
        updateSwatchPreview(themeId);
    });
}

/**
 * @brief Updates the 5-swatch palette preview boxes (Canvas, Header, Border, Accent, Text)
 *        with clean role text, rounded pill styling, and hex code in tooltip.
 */
void AppearanceSettingsPage::updateSwatchPreview(const QString& themeId) {
    const QStringList sw = gridlock::core::managers::ThemeManager::themeSwatchColors(themeId);
    const QStringList roles = {QStringLiteral("Canvas"), QStringLiteral("Header"),
                               QStringLiteral("Border"), QStringLiteral("Accent"),
                               QStringLiteral("Text")};
    for (int i = 0; i < m_swatchBoxes.size() && i < sw.size() && i < roles.size(); ++i) {
        const QColor bgColor(sw[i]);
        // Compute perceived luminance L = 0.299*R + 0.587*G + 0.114*B for WCAG contrast.
        const double lum = 0.299 * bgColor.red() + 0.587 * bgColor.green() + 0.114 * bgColor.blue();
        const QString fgHex = (lum >= 145.0) ? QStringLiteral("#16181d") : QStringLiteral("#ffffff");
        const QString bdrHex = (lum >= 145.0) ? QStringLiteral("#b8b09d") : QStringLiteral("#434955");
        const QColor fgColor(fgHex);

        // Display ONLY clean role name inside the rounded pill chip, with hex in tooltip.
        m_swatchBoxes[i]->setText(roles[i]);
        m_swatchBoxes[i]->setFixedHeight(26);
        m_swatchBoxes[i]->setMinimumWidth(82);
        m_swatchBoxes[i]->setStyleSheet(
            QStringLiteral("background-color: %1; color: %2; border: 1px solid %3; "
                           "border-radius: 6px; padding: 4px 10px; min-width: 68px; max-width: 84px; "
                           "font-size: 11px; font-weight: 600;")
                .arg(sw[i], fgHex, bdrHex));
        QPalette swPal = m_swatchBoxes[i]->palette();
        for (auto grp : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            swPal.setColor(grp, QPalette::WindowText, fgColor);
            swPal.setColor(grp, QPalette::Text, fgColor);
            swPal.setColor(grp, QPalette::ButtonText, fgColor);
            swPal.setColor(grp, QPalette::Window, bgColor);
            swPal.setColor(grp, QPalette::Base, bgColor);
        }
        m_swatchBoxes[i]->setPalette(swPal);
        m_swatchBoxes[i]->setToolTip(QStringLiteral("%1: %2").arg(roles[i], sw[i]));
    }
}

/** @brief Returns the active internal theme identifier selected in the combo box. */
QString AppearanceSettingsPage::selectedTheme() const {
    return m_themeCombo->currentData().toString().isEmpty() ? m_themeCombo->currentText()
                                                            : m_themeCombo->currentData().toString();
}
bool AppearanceSettingsPage::isDarkMode() const {
    return gridlock::core::managers::ThemeManager::paletteForTheme(selectedTheme()).isDark;
}
QString AppearanceSettingsPage::fileTreeStyle() const { return m_fileTreeStyleCombo->currentText(); }
bool AppearanceSettingsPage::colorizeIcons() const { return m_colorizeIconsCheck->isChecked(); }
QString AppearanceSettingsPage::uiFontFamily() const { return m_uiFontFamilyCombo->currentText(); }
int AppearanceSettingsPage::uiFontSize() const { return m_uiFontSizeBox->value(); }
QString AppearanceSettingsPage::codeFontFamily() const { return m_codeFontFamilyCombo->currentText(); }
int AppearanceSettingsPage::codeFontSize() const { return m_codeFontSizeBox->value(); }
QString AppearanceSettingsPage::memoryMapColorScheme() const {
    return m_memoryMapColorSchemeCombo ? m_memoryMapColorSchemeCombo->currentData().toString()
                                       : QStringLiteral("Distinct");
}

/** @brief Loads persisted appearance preferences from user settings with signals blocked. */
void AppearanceSettingsPage::loadFromSettings() {
    // Query live active theme ID first (falling back to persisted QSettings), plus typography metrics.
    QSettings s("gridlock", "debugger");
    const QString activeId = gridlock::core::managers::ThemeManager::instance().activeThemeId().trimmed();
    const QString theme = !activeId.isEmpty()
                              ? activeId
                              : s.value("appearance/theme", QStringLiteral("gridlock_slate")).toString();
    const QVector<QWidget*> ctrls = {m_darkModeCheck, m_themeCombo, m_fileTreeStyleCombo, m_colorizeIconsCheck,
                                     m_uiFontFamilyCombo, m_uiFontSizeBox, m_codeFontFamilyCombo, m_codeFontSizeBox,
                                     m_memoryMapColorSchemeCombo};
    // Temporarily suppress change signals while populating initial form state.
    for (QWidget* w : ctrls) if (w) w->blockSignals(true);

    int idx = m_themeCombo->findData(theme);
    if (idx < 0) idx = m_themeCombo->findText(theme, Qt::MatchContains);
    if (idx >= 0) m_themeCombo->setCurrentIndex(idx);
    m_darkModeCheck->setChecked(isDarkMode());
    updateSwatchPreview(selectedTheme());

    if (m_memoryMapColorSchemeCombo) {
        const int schemeIdx = m_memoryMapColorSchemeCombo->findData(
            gridlock::core::ConfigManager::instance().memoryMapColorScheme());
        m_memoryMapColorSchemeCombo->setCurrentIndex(schemeIdx >= 0 ? schemeIdx : 0);
    }

    // Restore file tree density, icon colorization, and UI/Code font families and sizes.
    const int ftsIdx = m_fileTreeStyleCombo->findText(
        s.value("appearance/file_tree_style", "Comfortable").toString());
    if (ftsIdx >= 0) m_fileTreeStyleCombo->setCurrentIndex(ftsIdx);
    m_colorizeIconsCheck->setChecked(s.value("appearance/colorize_icons", true).toBool());

    const int uiFamIdx = m_uiFontFamilyCombo->findText(
        s.value("appearance/ui_font_family", "Inter").toString());
    if (uiFamIdx >= 0) m_uiFontFamilyCombo->setCurrentIndex(uiFamIdx);
    m_uiFontSizeBox->setValue(s.value("appearance/ui_font_size", 11).toInt());

    const int codeFamIdx = m_codeFontFamilyCombo->findText(
        s.value("appearance/code_font_family", "JetBrains Mono").toString());
    if (codeFamIdx >= 0) m_codeFontFamilyCombo->setCurrentIndex(codeFamIdx);
    m_codeFontSizeBox->setValue(s.value("appearance/code_font_size", 11).toInt());

    for (QWidget* w : ctrls) if (w) w->blockSignals(false);
}

}  // namespace gridlock::ui
